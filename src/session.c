#include "cyclone/session.h"

#include <stdlib.h>
#include <string.h>

#include "cyclone/handshake.h"

struct cyc_session {
    cyc_role role;
    const cyc_schema *schema;
    cyc_config config;
    cyc_state state;
    uint64_t started_ms;
    uint64_t last_activity_ms;
    uint64_t probe_sent_ms;
    bool probing;
    bool terminated;
    bool query_seen;
    bool has_asked;
    size_t asked_count;
    cyc_query_item *asked;
    cyc_query_item *queries;
    cyc_reply_item *replies;
    uint8_t *scratch;
    size_t scratch_cap;
};

static uint64_t since(uint64_t now, uint64_t then) {
    return now >= then ? now - then : 0;
}

static void reaction_clear(cyc_reaction *out) {
    out->out = CYC_OUT_NONE;
    out->out_payload = NULL;
    out->out_payload_len = 0;
    out->has_event = false;
    out->event = CYC_EVENT_CONNECTED;
    out->message_id = 0;
    out->payload = NULL;
    out->payload_len = 0;
    out->reason = 0;
}

static void emit_event(cyc_reaction *out, cyc_event_kind kind, int reason) {
    out->has_event = true;
    out->event = kind;
    out->reason = reason;
}

static void emit_handshake(cyc_session *session, cyc_reaction *out, size_t len) {
    out->out = CYC_OUT_HANDSHAKE;
    out->out_payload = session->scratch;
    out->out_payload_len = len;
}

static void put_verdict(cyc_session *session, cyc_reaction *out, cyc_verdict verdict) {
    session->scratch[0] = (uint8_t)verdict;
    emit_handshake(session, out, 1);
}

static void accept_peer(cyc_session *session, cyc_reaction *out) {
    session->state = CYC_STATE_READY;
    put_verdict(session, out, CYC_VERDICT_ACCEPT);
    emit_event(out, CYC_EVENT_READY, 0);
}

static void reject_peer(cyc_session *session, cyc_reaction *out, cyc_verdict verdict) {
    session->state = CYC_STATE_CLOSED;
    session->terminated = true;
    put_verdict(session, out, verdict);
    emit_event(out, CYC_EVENT_HANDSHAKE_FAILED, (int)cyc_handshake_failure_of(verdict));
}

static void fail_locally(cyc_session *session, cyc_reaction *out, cyc_handshake_failure failure) {
    session->state = CYC_STATE_CLOSED;
    session->terminated = true;
    emit_event(out, CYC_EVENT_HANDSHAKE_FAILED, (int)failure);
}

cyc_session *cyc_session_create(cyc_role role, const cyc_schema *schema, const cyc_config *config,
                                uint64_t now_ms, cyc_reaction *opening) {
    cyc_session *session;
    size_t slots;
    size_t hello_len;
    size_t query_len;
    size_t reply_len;

    if (schema == NULL || config == NULL || opening == NULL) {
        return NULL;
    }
    reaction_clear(opening);

    session = (cyc_session *)calloc(1, sizeof(*session));
    if (session == NULL) {
        return NULL;
    }

    slots = schema->message_count > 0 ? schema->message_count : 1;
    hello_len = cyc_hello_len(schema);
    query_len = CYC_QUERY_HEADER_LEN + CYC_QUERY_ENTRY_LEN * slots;
    reply_len = CYC_REPLY_HEADER_LEN + CYC_REPLY_ENTRY_LEN * slots;

    session->scratch_cap = hello_len;
    if (query_len > session->scratch_cap) {
        session->scratch_cap = query_len;
    }
    if (reply_len > session->scratch_cap) {
        session->scratch_cap = reply_len;
    }

    session->scratch = (uint8_t *)malloc(session->scratch_cap);
    session->asked = (cyc_query_item *)malloc(slots * sizeof(cyc_query_item));
    session->queries = (cyc_query_item *)malloc(slots * sizeof(cyc_query_item));
    session->replies = (cyc_reply_item *)malloc(slots * sizeof(cyc_reply_item));
    if (session->scratch == NULL || session->asked == NULL || session->queries == NULL ||
        session->replies == NULL) {
        cyc_session_destroy(session);
        return NULL;
    }

    session->role = role;
    session->schema = schema;
    session->config = *config;
    session->state = CYC_STATE_HANDSHAKING;
    session->started_ms = now_ms;
    session->last_activity_ms = now_ms;
    session->probe_sent_ms = now_ms;
    session->probing = false;
    session->terminated = false;
    session->query_seen = false;
    session->has_asked = false;
    session->asked_count = 0;

    if (role == CYC_ROLE_CLIENT) {
        size_t written = cyc_hello_encode(schema, session->scratch, session->scratch_cap);
        emit_handshake(session, opening, written);
    }
    return session;
}

void cyc_session_destroy(cyc_session *session) {
    if (session == NULL) {
        return;
    }
    free(session->scratch);
    free(session->asked);
    free(session->queries);
    free(session->replies);
    free(session);
}

cyc_state cyc_session_state(const cyc_session *session) {
    return session->state;
}

cyc_role cyc_session_role(const cyc_session *session) {
    return session->role;
}

bool cyc_session_ready(const cyc_session *session) {
    return session->state == CYC_STATE_READY;
}

void cyc_session_close(cyc_session *session) {
    session->state = CYC_STATE_CLOSED;
    session->terminated = true;
}

static void client_handshake(cyc_session *session, const uint8_t *payload, size_t len,
                             cyc_reaction *out) {
    if (len > 0 && payload[0] == CYC_QUERY_TAG) {
        size_t count = 0;
        size_t written;

        if (session->query_seen) {
            fail_locally(session, out, CYC_FAIL_MALFORMED_PEER);
            return;
        }
        session->query_seen = true;

        if (!cyc_query_decode(payload, len, session->queries, session->schema->message_count,
                              &count)) {
            fail_locally(session, out, CYC_FAIL_MALFORMED_PEER);
            return;
        }
        if (!cyc_query_answer(session->schema, session->queries, count, session->replies)) {
            fail_locally(session, out, CYC_FAIL_MALFORMED_PEER);
            return;
        }
        written = cyc_reply_encode(session->replies, count, session->scratch, session->scratch_cap);
        emit_handshake(session, out, written);
        return;
    }

    if (len != 1 || payload[0] > (uint8_t)CYC_VERDICT_MALFORMED_HELLO) {
        fail_locally(session, out, CYC_FAIL_MALFORMED_PEER);
        return;
    }
    if (payload[0] == (uint8_t)CYC_VERDICT_ACCEPT) {
        session->state = CYC_STATE_READY;
        emit_event(out, CYC_EVENT_READY, 0);
        return;
    }
    fail_locally(session, out, cyc_handshake_failure_of((cyc_verdict)payload[0]));
}

static void server_handshake(cyc_session *session, const uint8_t *payload, size_t len,
                             cyc_reaction *out) {
    cyc_hello_view hello;
    cyc_decision decision;
    size_t written;

    if (session->has_asked) {
        size_t count = 0;
        cyc_verdict verdict;

        session->has_asked = false;
        if (!cyc_reply_decode(payload, len, session->replies, session->schema->message_count,
                              &count)) {
            reject_peer(session, out, CYC_VERDICT_MALFORMED_HELLO);
            return;
        }
        verdict = cyc_reply_check(session->schema, session->asked, session->asked_count,
                                  session->replies, count);
        if (verdict == CYC_VERDICT_ACCEPT) {
            accept_peer(session, out);
        } else {
            reject_peer(session, out, verdict);
        }
        return;
    }

    if (!cyc_hello_decode(payload, len, &hello)) {
        reject_peer(session, out, CYC_VERDICT_MALFORMED_HELLO);
        return;
    }
    if (hello.version != CYC_PROTOCOL_VERSION) {
        reject_peer(session, out, CYC_VERDICT_WRONG_VERSION);
        return;
    }

    decision = cyc_handshake_decide(session->schema, &hello, session->queries,
                                    session->schema->message_count);
    switch (decision.kind) {
    case CYC_DECISION_ACCEPT:
        accept_peer(session, out);
        return;
    case CYC_DECISION_REJECT:
        reject_peer(session, out, decision.verdict);
        return;
    case CYC_DECISION_QUERY:
        memcpy(session->asked, session->queries, decision.query_count * sizeof(cyc_query_item));
        session->asked_count = decision.query_count;
        session->has_asked = true;
        written = cyc_query_encode(session->asked, session->asked_count, session->scratch,
                                   session->scratch_cap);
        emit_handshake(session, out, written);
        return;
    }
}

void cyc_session_on_frame(cyc_session *session, const cyc_frame *frame, uint64_t now_ms,
                          cyc_reaction *out) {
    reaction_clear(out);
    if (session->state == CYC_STATE_CLOSED) {
        return;
    }

    session->last_activity_ms = now_ms;
    session->probing = false;

    switch (frame->type) {
    case CYC_FRAME_PROBE:
        out->out = CYC_OUT_ACK;
        if (cyc_session_ready(session)) {
            emit_event(out, CYC_EVENT_PROBE, 0);
        }
        return;

    case CYC_FRAME_ACK:
        if (cyc_session_ready(session)) {
            emit_event(out, CYC_EVENT_ACK, 0);
        }
        return;

    case CYC_FRAME_DATA:
        if (cyc_session_ready(session)) {
            emit_event(out, CYC_EVENT_MESSAGE, 0);
            out->message_id = frame->message_id;
            out->payload = frame->payload;
            out->payload_len = frame->payload_len;
        }
        return;

    case CYC_FRAME_HANDSHAKE:
        if (session->state != CYC_STATE_HANDSHAKING) {
            return;
        }
        if (session->role == CYC_ROLE_CLIENT) {
            client_handshake(session, frame->payload, frame->payload_len, out);
        } else {
            server_handshake(session, frame->payload, frame->payload_len, out);
        }
        return;

    default:
        return;
    }
}

void cyc_session_tick(cyc_session *session, uint64_t now_ms, cyc_reaction *out) {
    uint64_t silence_ms;

    reaction_clear(out);
    if (session->state == CYC_STATE_CLOSED) {
        return;
    }

    if (session->role == CYC_ROLE_CLIENT && session->state == CYC_STATE_HANDSHAKING) {
        if (since(now_ms, session->started_ms) >= (uint64_t)session->config.handshake_timeout_ms) {
            fail_locally(session, out, CYC_FAIL_TIMEOUT);
        }
        return;
    }

    if (session->probing) {
        if (since(now_ms, session->probe_sent_ms) >= (uint64_t)session->config.heartbeat_timeout_ms) {
            session->state = CYC_STATE_CLOSED;
            session->terminated = true;
            emit_event(out, CYC_EVENT_DISCONNECTED, (int)CYC_DISCONNECT_UNRESPONSIVE);
        }
        return;
    }

    silence_ms = session->role == CYC_ROLE_SERVER && session->state == CYC_STATE_HANDSHAKING
                     ? (uint64_t)session->config.handshake_timeout_ms
                     : (uint64_t)session->config.heartbeat_interval_ms;

    if (since(now_ms, session->last_activity_ms) >= silence_ms) {
        session->probing = true;
        session->probe_sent_ms = now_ms;
        out->out = CYC_OUT_PROBE;
    }
}

void cyc_session_transport_closed(cyc_session *session, cyc_disconnect reason, cyc_reaction *out) {
    reaction_clear(out);
    session->state = CYC_STATE_CLOSED;
    if (session->terminated) {
        return;
    }
    session->terminated = true;
    emit_event(out, CYC_EVENT_DISCONNECTED, (int)reason);
}
