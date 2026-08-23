#include "fomoxa/session.h"

#include <stdlib.h>
#include <string.h>

#include "fomoxa/handshake.h"

struct fmx_session {
    fmx_role role;
    const fmx_schema *schema;
    fmx_config config;
    fmx_state state;
    uint64_t started_ms;
    uint64_t last_activity_ms;
    uint64_t probe_sent_ms;
    bool probing;
    bool terminated;
    bool query_seen;
    bool has_asked;
    size_t asked_count;
    fmx_query_item *asked;
    fmx_query_item *queries;
    fmx_reply_item *replies;
    uint8_t *scratch;
    size_t scratch_cap;
};

static uint64_t since(uint64_t now, uint64_t then) {
    return now >= then ? now - then : 0;
}

static void reaction_clear(fmx_reaction *out) {
    out->out = FMX_OUT_NONE;
    out->out_payload = NULL;
    out->out_payload_len = 0;
    out->has_event = false;
    out->event = FMX_EVENT_CONNECTED;
    out->message_id = 0;
    out->payload = NULL;
    out->payload_len = 0;
    out->reason = 0;
}

static void emit_event(fmx_reaction *out, fmx_event_kind kind, int reason) {
    out->has_event = true;
    out->event = kind;
    out->reason = reason;
}

static void emit_handshake(fmx_session *session, fmx_reaction *out, size_t len) {
    out->out = FMX_OUT_HANDSHAKE;
    out->out_payload = session->scratch;
    out->out_payload_len = len;
}

static void put_verdict(fmx_session *session, fmx_reaction *out, fmx_verdict verdict) {
    session->scratch[0] = (uint8_t)verdict;
    emit_handshake(session, out, 1);
}

static void accept_peer(fmx_session *session, fmx_reaction *out) {
    session->state = FMX_STATE_READY;
    put_verdict(session, out, FMX_VERDICT_ACCEPT);
    emit_event(out, FMX_EVENT_READY, 0);
}

static void reject_peer(fmx_session *session, fmx_reaction *out, fmx_verdict verdict) {
    session->state = FMX_STATE_CLOSED;
    session->terminated = true;
    put_verdict(session, out, verdict);
    emit_event(out, FMX_EVENT_HANDSHAKE_FAILED, (int)fmx_handshake_failure_of(verdict));
}

static void fail_locally(fmx_session *session, fmx_reaction *out, fmx_handshake_failure failure) {
    session->state = FMX_STATE_CLOSED;
    session->terminated = true;
    emit_event(out, FMX_EVENT_HANDSHAKE_FAILED, (int)failure);
}

fmx_session *fmx_session_create(fmx_role role, const fmx_schema *schema, const fmx_config *config,
                                uint64_t now_ms, fmx_reaction *opening) {
    fmx_session *session;
    size_t slots;
    size_t hello_len;
    size_t query_len;
    size_t reply_len;

    if (schema == NULL || config == NULL || opening == NULL) {
        return NULL;
    }
    reaction_clear(opening);

    session = (fmx_session *)calloc(1, sizeof(*session));
    if (session == NULL) {
        return NULL;
    }

    slots = schema->message_count > 0 ? schema->message_count : 1;
    hello_len = fmx_hello_len(schema);
    query_len = FMX_QUERY_HEADER_LEN + FMX_QUERY_ENTRY_LEN * slots;
    reply_len = FMX_REPLY_HEADER_LEN + FMX_REPLY_ENTRY_LEN * slots;

    session->scratch_cap = hello_len;
    if (query_len > session->scratch_cap) {
        session->scratch_cap = query_len;
    }
    if (reply_len > session->scratch_cap) {
        session->scratch_cap = reply_len;
    }

    session->scratch = (uint8_t *)malloc(session->scratch_cap);
    session->asked = (fmx_query_item *)malloc(slots * sizeof(fmx_query_item));
    session->queries = (fmx_query_item *)malloc(slots * sizeof(fmx_query_item));
    session->replies = (fmx_reply_item *)malloc(slots * sizeof(fmx_reply_item));
    if (session->scratch == NULL || session->asked == NULL || session->queries == NULL ||
        session->replies == NULL) {
        fmx_session_destroy(session);
        return NULL;
    }

    session->role = role;
    session->schema = schema;
    session->config = *config;
    session->state = FMX_STATE_HANDSHAKING;
    session->started_ms = now_ms;
    session->last_activity_ms = now_ms;
    session->probe_sent_ms = now_ms;
    session->probing = false;
    session->terminated = false;
    session->query_seen = false;
    session->has_asked = false;
    session->asked_count = 0;

    if (role == FMX_ROLE_CLIENT) {
        size_t written = fmx_hello_encode(schema, session->scratch, session->scratch_cap);
        emit_handshake(session, opening, written);
    }
    return session;
}

void fmx_session_destroy(fmx_session *session) {
    if (session == NULL) {
        return;
    }
    free(session->scratch);
    free(session->asked);
    free(session->queries);
    free(session->replies);
    free(session);
}

fmx_state fmx_session_state(const fmx_session *session) {
    return session->state;
}

fmx_role fmx_session_role(const fmx_session *session) {
    return session->role;
}

bool fmx_session_ready(const fmx_session *session) {
    return session->state == FMX_STATE_READY;
}

void fmx_session_close(fmx_session *session) {
    session->state = FMX_STATE_CLOSED;
    session->terminated = true;
}

static void client_handshake(fmx_session *session, const uint8_t *payload, size_t len,
                             fmx_reaction *out) {
    if (len > 0 && payload[0] == FMX_QUERY_TAG) {
        size_t count = 0;
        size_t written;

        if (session->query_seen) {
            fail_locally(session, out, FMX_FAIL_MALFORMED_PEER);
            return;
        }
        session->query_seen = true;

        if (!fmx_query_decode(payload, len, session->queries, session->schema->message_count,
                              &count)) {
            fail_locally(session, out, FMX_FAIL_MALFORMED_PEER);
            return;
        }
        if (!fmx_query_answer(session->schema, session->queries, count, session->replies)) {
            fail_locally(session, out, FMX_FAIL_MALFORMED_PEER);
            return;
        }
        written = fmx_reply_encode(session->replies, count, session->scratch, session->scratch_cap);
        emit_handshake(session, out, written);
        return;
    }

    if (len != 1 || payload[0] > (uint8_t)FMX_VERDICT_MALFORMED_HELLO) {
        fail_locally(session, out, FMX_FAIL_MALFORMED_PEER);
        return;
    }
    if (payload[0] == (uint8_t)FMX_VERDICT_ACCEPT) {
        session->state = FMX_STATE_READY;
        emit_event(out, FMX_EVENT_READY, 0);
        return;
    }
    fail_locally(session, out, fmx_handshake_failure_of((fmx_verdict)payload[0]));
}

static void server_handshake(fmx_session *session, const uint8_t *payload, size_t len,
                             fmx_reaction *out) {
    fmx_hello_view hello;
    fmx_decision decision;
    size_t written;

    if (session->has_asked) {
        size_t count = 0;
        fmx_verdict verdict;

        session->has_asked = false;
        if (!fmx_reply_decode(payload, len, session->replies, session->schema->message_count,
                              &count)) {
            reject_peer(session, out, FMX_VERDICT_MALFORMED_HELLO);
            return;
        }
        verdict = fmx_reply_check(session->schema, session->asked, session->asked_count,
                                  session->replies, count);
        if (verdict == FMX_VERDICT_ACCEPT) {
            accept_peer(session, out);
        } else {
            reject_peer(session, out, verdict);
        }
        return;
    }

    if (!fmx_hello_decode(payload, len, &hello)) {
        reject_peer(session, out, FMX_VERDICT_MALFORMED_HELLO);
        return;
    }
    if (hello.version != FMX_PROTOCOL_VERSION) {
        reject_peer(session, out, FMX_VERDICT_WRONG_VERSION);
        return;
    }

    decision = fmx_handshake_decide(session->schema, &hello, session->queries,
                                    session->schema->message_count);
    switch (decision.kind) {
    case FMX_DECISION_ACCEPT:
        accept_peer(session, out);
        return;
    case FMX_DECISION_REJECT:
        reject_peer(session, out, decision.verdict);
        return;
    case FMX_DECISION_QUERY:
        memcpy(session->asked, session->queries, decision.query_count * sizeof(fmx_query_item));
        session->asked_count = decision.query_count;
        session->has_asked = true;
        written = fmx_query_encode(session->asked, session->asked_count, session->scratch,
                                   session->scratch_cap);
        emit_handshake(session, out, written);
        return;
    }
}

void fmx_session_on_frame(fmx_session *session, const fmx_frame *frame, uint64_t now_ms,
                          fmx_reaction *out) {
    reaction_clear(out);
    if (session->state == FMX_STATE_CLOSED) {
        return;
    }

    session->last_activity_ms = now_ms;
    session->probing = false;

    switch (frame->type) {
    case FMX_FRAME_PROBE:
        out->out = FMX_OUT_ACK;
        if (fmx_session_ready(session)) {
            emit_event(out, FMX_EVENT_PROBE, 0);
        }
        return;

    case FMX_FRAME_ACK:
        if (fmx_session_ready(session)) {
            emit_event(out, FMX_EVENT_ACK, 0);
        }
        return;

    case FMX_FRAME_DATA:
        if (fmx_session_ready(session)) {
            emit_event(out, FMX_EVENT_MESSAGE, 0);
            out->message_id = frame->message_id;
            out->payload = frame->payload;
            out->payload_len = frame->payload_len;
        }
        return;

    case FMX_FRAME_HANDSHAKE:
        if (session->state != FMX_STATE_HANDSHAKING) {
            return;
        }
        if (session->role == FMX_ROLE_CLIENT) {
            client_handshake(session, frame->payload, frame->payload_len, out);
        } else {
            server_handshake(session, frame->payload, frame->payload_len, out);
        }
        return;

    default:
        return;
    }
}

void fmx_session_tick(fmx_session *session, uint64_t now_ms, fmx_reaction *out) {
    uint64_t silence_ms;

    reaction_clear(out);
    if (session->state == FMX_STATE_CLOSED) {
        return;
    }

    if (session->role == FMX_ROLE_CLIENT && session->state == FMX_STATE_HANDSHAKING) {
        if (since(now_ms, session->started_ms) >= (uint64_t)session->config.handshake_timeout_ms) {
            fail_locally(session, out, FMX_FAIL_TIMEOUT);
        }
        return;
    }

    if (session->probing) {
        if (since(now_ms, session->probe_sent_ms) >= (uint64_t)session->config.heartbeat_timeout_ms) {
            session->state = FMX_STATE_CLOSED;
            session->terminated = true;
            emit_event(out, FMX_EVENT_DISCONNECTED, (int)FMX_DISCONNECT_UNRESPONSIVE);
        }
        return;
    }

    silence_ms = session->role == FMX_ROLE_SERVER && session->state == FMX_STATE_HANDSHAKING
                     ? (uint64_t)session->config.handshake_timeout_ms
                     : (uint64_t)session->config.heartbeat_interval_ms;

    if (since(now_ms, session->last_activity_ms) >= silence_ms) {
        session->probing = true;
        session->probe_sent_ms = now_ms;
        out->out = FMX_OUT_PROBE;
    }
}

void fmx_session_transport_closed(fmx_session *session, fmx_disconnect reason, fmx_reaction *out) {
    reaction_clear(out);
    session->state = FMX_STATE_CLOSED;
    if (session->terminated) {
        return;
    }
    session->terminated = true;
    emit_event(out, FMX_EVENT_DISCONNECTED, (int)reason);
}
