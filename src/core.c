#include "core.h"

#include <stdlib.h>
#include <string.h>

#include "cyclone/handshake.h"

cyc_result cyc_sink_init(cyc_sink *sink, size_t event_cap) {
    memset(sink, 0, sizeof(*sink));
    sink->events = (cyc_event *)malloc(event_cap * sizeof(cyc_event));
    sink->offsets = (size_t *)malloc(event_cap * sizeof(size_t));
    if (sink->events == NULL || sink->offsets == NULL) {
        cyc_sink_release(sink);
        return CYC_ERR_NO_MEMORY;
    }
    sink->event_cap = event_cap;
    return CYC_OK;
}

void cyc_sink_release(cyc_sink *sink) {
    free(sink->arena);
    free(sink->events);
    free(sink->offsets);
    memset(sink, 0, sizeof(*sink));
}

void cyc_sink_clear(cyc_sink *sink) {
    sink->arena_len = 0;
    sink->event_count = 0;
}

void cyc_sink_push(cyc_sink *sink, uint64_t peer, cyc_event_kind kind, uint32_t message_id,
                   const uint8_t *payload, size_t payload_len, int reason) {
    cyc_event *event;
    size_t offset = 0;

    if (sink->event_count >= sink->event_cap) {
        size_t grown = sink->event_cap == 0 ? 16 : sink->event_cap * 2;
        cyc_event *events = (cyc_event *)realloc(sink->events, grown * sizeof(cyc_event));
        size_t *offsets;
        if (events == NULL) {
            return;
        }
        sink->events = events;
        offsets = (size_t *)realloc(sink->offsets, grown * sizeof(size_t));
        if (offsets == NULL) {
            return;
        }
        sink->offsets = offsets;
        sink->event_cap = grown;
    }

    if (payload_len > 0) {
        size_t needed = sink->arena_len + payload_len;
        if (needed > sink->arena_cap) {
            size_t grown = sink->arena_cap == 0 ? 1024 : sink->arena_cap;
            uint8_t *arena;
            while (grown < needed) {
                grown *= 2;
            }
            arena = (uint8_t *)realloc(sink->arena, grown);
            if (arena == NULL) {
                return;
            }
            sink->arena = arena;
            sink->arena_cap = grown;
        }
        offset = sink->arena_len;
        memcpy(sink->arena + offset, payload, payload_len);
        sink->arena_len += payload_len;
    }

    event = &sink->events[sink->event_count];
    event->kind = kind;
    event->peer = peer;
    event->message_id = message_id;
    event->payload = NULL;
    event->payload_len = payload_len;
    event->reason = reason;
    sink->offsets[sink->event_count] = offset;
    sink->event_count += 1;
}

void cyc_sink_resolve(cyc_sink *sink) {
    size_t index;
    for (index = 0; index < sink->event_count; ++index) {
        if (sink->events[index].payload_len > 0) {
            sink->events[index].payload = sink->arena + sink->offsets[index];
        } else {
            sink->events[index].payload = NULL;
        }
    }
}

static void core_kill(cyc_core *core, cyc_disconnect reason) {
    if (!core->dead) {
        core->dead = true;
        core->dead_reason = reason;
    }
}

static void core_shutdown(cyc_core *core) {
    core_kill(core, CYC_DISCONNECT_LOCAL);
    core->outbox_len = 0;
    core->outbox_off = 0;
    core->transport.vtable->close_soft(&core->transport);
}

static cyc_result outbox_set(cyc_core *core, const uint8_t *bytes, size_t len) {
    if (len > core->outbox_cap) {
        uint8_t *grown = (uint8_t *)realloc(core->outbox, len);
        if (grown == NULL) {
            core_kill(core, CYC_DISCONNECT_TRANSPORT_ERROR);
            return CYC_ERR_NO_MEMORY;
        }
        core->outbox = grown;
        core->outbox_cap = len;
    }
    memcpy(core->outbox, bytes, len);
    core->outbox_len = len;
    core->outbox_off = 0;
    return CYC_OK;
}

static cyc_result outbox_append(cyc_core *core, const uint8_t *bytes, size_t len) {
    size_t needed = core->outbox_len + len;
    if (needed > core->outbox_cap) {
        uint8_t *grown = (uint8_t *)realloc(core->outbox, needed);
        if (grown == NULL) {
            core_kill(core, CYC_DISCONNECT_TRANSPORT_ERROR);
            return CYC_ERR_NO_MEMORY;
        }
        core->outbox = grown;
        core->outbox_cap = needed;
    }
    memcpy(core->outbox + core->outbox_len, bytes, len);
    core->outbox_len = needed;
    return CYC_OK;
}

static cyc_result write_frame(cyc_core *core, const uint8_t *bytes, size_t len) {
    size_t accepted = 0;
    cyc_send_result result = core->transport.vtable->send(&core->transport, bytes, len, &accepted);

    switch (result) {
    case CYC_SEND_SENT:
        return CYC_OK;
    case CYC_SEND_PARTIAL:
        if (accepted == 0 || accepted >= len) {
            return accepted >= len ? CYC_OK : outbox_set(core, bytes, len);
        }
        return outbox_set(core, bytes + accepted, len - accepted);
    case CYC_SEND_WOULD_BLOCK:
        return outbox_set(core, bytes, len);
    case CYC_SEND_TOO_LARGE:
        return CYC_ERR_TOO_LARGE;
    case CYC_SEND_CLOSED:
        core_kill(core, CYC_DISCONNECT_PEER_CLOSED);
        return CYC_ERR_CLOSED;
    case CYC_SEND_ERROR:
        core_kill(core, CYC_DISCONNECT_TRANSPORT_ERROR);
        return CYC_ERR_CLOSED;
    }
    return CYC_ERR_CLOSED;
}

static void send_control(cyc_core *core, const uint8_t *bytes, size_t len) {
    if (core->dead || len == 0) {
        return;
    }
    if (core->outbox_len > core->outbox_off) {
        (void)outbox_append(core, bytes, len);
        return;
    }
    (void)write_frame(core, bytes, len);
}

static void core_flush(cyc_core *core) {
    while (core->outbox_len > core->outbox_off) {
        size_t accepted = 0;
        cyc_send_result result =
            core->transport.vtable->send(&core->transport, core->outbox + core->outbox_off,
                                         core->outbox_len - core->outbox_off, &accepted);
        switch (result) {
        case CYC_SEND_SENT:
            core->outbox_len = 0;
            core->outbox_off = 0;
            return;
        case CYC_SEND_PARTIAL:
            if (accepted == 0) {
                return;
            }
            core->outbox_off += accepted;
            break;
        case CYC_SEND_WOULD_BLOCK:
            return;
        case CYC_SEND_TOO_LARGE:
            core->outbox_len = 0;
            core->outbox_off = 0;
            return;
        case CYC_SEND_CLOSED:
            core_kill(core, CYC_DISCONNECT_PEER_CLOSED);
            return;
        case CYC_SEND_ERROR:
            core_kill(core, CYC_DISCONNECT_TRANSPORT_ERROR);
            return;
        }
    }
    core->outbox_len = 0;
    core->outbox_off = 0;
}

static void emit_out(cyc_core *core, const cyc_reaction *reaction) {
    size_t written = 0;

    switch (reaction->out) {
    case CYC_OUT_PROBE:
        written = cyc_frame_encode_probe(core->scratch, core->scratch_cap);
        break;
    case CYC_OUT_ACK:
        written = cyc_frame_encode_ack(core->scratch, core->scratch_cap);
        break;
    case CYC_OUT_HANDSHAKE:
        if (cyc_frame_encode_handshake(reaction->out_payload, reaction->out_payload_len,
                                       core->scratch, core->scratch_cap,
                                       &written) != CYC_FRAME_OK) {
            return;
        }
        break;
    case CYC_OUT_NONE:
        return;
    }
    send_control(core, core->scratch, written);
}

static void apply(cyc_core *core, const cyc_reaction *reaction, uint64_t peer, cyc_sink *sink) {
    if (reaction->out != CYC_OUT_NONE) {
        emit_out(core, reaction);
    }
    if (reaction->has_event) {
        cyc_sink_push(sink, peer, reaction->event, reaction->message_id, reaction->payload,
                      reaction->payload_len, reaction->reason);
        if (reaction->event == CYC_EVENT_HANDSHAKE_FAILED) {
            core_shutdown(core);
        }
    }
}

static bool grow_recv(cyc_core *core, size_t needed) {
    uint8_t *grown;
    if (needed <= core->recv_cap) {
        return true;
    }
    grown = (uint8_t *)realloc(core->recv, needed);
    if (grown == NULL) {
        core_kill(core, CYC_DISCONNECT_TRANSPORT_ERROR);
        return false;
    }
    core->recv = grown;
    core->recv_cap = needed;
    return true;
}

static void drain(cyc_core *core, uint64_t now_ms, uint64_t peer, cyc_sink *sink) {
    size_t frames = core->config.max_frames_per_tick;
    size_t reads = frames;

    while (frames > 0 && reads > 0 && !core->dead) {
        size_t received = 0;
        size_t needed = 0;
        cyc_recv_result result;
        cyc_reaction reaction;

        if (core->stream) {
            cyc_frame frame;
            size_t frame_len = 0;
            cyc_frame_error error = cyc_stream_decoder_next(&core->decoder, &frame, &frame_len);

            if (error == CYC_FRAME_OK) {
                cyc_session_on_frame(core->session, &frame, now_ms, &reaction);
                apply(core, &reaction, peer, sink);
                cyc_stream_decoder_advance(&core->decoder, frame_len);
                frames -= 1;
                continue;
            }
            if (error != CYC_FRAME_INCOMPLETE) {
                core_kill(core, CYC_DISCONNECT_TRANSPORT_ERROR);
                return;
            }
        }

        result = core->transport.vtable->recv(&core->transport, core->recv, core->recv_cap,
                                              &received, &needed);
        switch (result) {
        case CYC_RECV_RECEIVED:
            if (core->stream) {
                if (cyc_stream_decoder_feed(&core->decoder, core->recv, received) != CYC_OK) {
                    core_kill(core, CYC_DISCONNECT_TRANSPORT_ERROR);
                    return;
                }
                reads -= 1;
            } else {
                cyc_frame frame;
                if (cyc_frame_decode_packet(core->recv, received, core->config.max_message_bytes,
                                            &frame) == CYC_FRAME_OK) {
                    cyc_session_on_frame(core->session, &frame, now_ms, &reaction);
                    apply(core, &reaction, peer, sink);
                }
                frames -= 1;
            }
            break;

        case CYC_RECV_NEED_CAPACITY:
            if (!grow_recv(core, needed)) {
                return;
            }
            reads -= 1;
            break;

        case CYC_RECV_WOULD_BLOCK:
            return;

        case CYC_RECV_CLOSED:
            core_kill(core, CYC_DISCONNECT_PEER_CLOSED);
            return;

        case CYC_RECV_ERROR:
            core_kill(core, CYC_DISCONNECT_TRANSPORT_ERROR);
            return;
        }
    }
}

cyc_result cyc_core_init(cyc_core *core, cyc_transport transport, const cyc_schema *schema,
                         const cyc_config *config, cyc_role role, uint64_t now_ms) {
    cyc_reaction opening;
    size_t handshake_frame;
    size_t data_frame;

    memset(core, 0, sizeof(*core));
    core->transport = transport;
    core->config = *config;
    core->stream = transport.vtable->kind(&transport) == CYC_TRANSPORT_STREAM;

    if (cyc_hello_len(schema) > CYC_MAX_HANDSHAKE_PAYLOAD) {
        return CYC_ERR_INVALID;
    }

    handshake_frame = cyc_frame_handshake_len(cyc_hello_len(schema));
    data_frame = cyc_frame_data_len(config->max_message_bytes);
    core->scratch_cap = handshake_frame > data_frame ? handshake_frame : data_frame;
    core->scratch = (uint8_t *)malloc(core->scratch_cap);

    core->recv_cap = 4096;
    core->recv = (uint8_t *)malloc(core->recv_cap);

    if (core->scratch == NULL || core->recv == NULL) {
        cyc_core_release(core);
        return CYC_ERR_NO_MEMORY;
    }

    if (core->stream && cyc_stream_decoder_init(&core->decoder, config->max_message_bytes) !=
                            CYC_OK) {
        cyc_core_release(core);
        return CYC_ERR_NO_MEMORY;
    }

    core->session = cyc_session_create(role, schema, config, now_ms, &opening);
    if (core->session == NULL) {
        cyc_core_release(core);
        return CYC_ERR_NO_MEMORY;
    }
    if (opening.out != CYC_OUT_NONE) {
        emit_out(core, &opening);
    }
    return CYC_OK;
}

void cyc_core_release(cyc_core *core) {
    if (core->transport.vtable != NULL) {
        core->transport.vtable->close_hard(&core->transport);
        core->transport.vtable = NULL;
    }
    if (core->session != NULL) {
        cyc_session_destroy(core->session);
        core->session = NULL;
    }
    if (core->stream) {
        cyc_stream_decoder_release(&core->decoder);
    }
    free(core->outbox);
    free(core->recv);
    free(core->scratch);
    core->outbox = NULL;
    core->recv = NULL;
    core->scratch = NULL;
}

void cyc_core_tick(cyc_core *core, uint64_t now_ms, uint64_t peer, cyc_sink *sink) {
    cyc_reaction reaction;

    if (!core->announced) {
        core->announced = true;
        cyc_sink_push(sink, peer, CYC_EVENT_CONNECTED, 0, NULL, 0, 0);
    }

    if (!core->dead) {
        core_flush(core);
    }
    if (!core->dead) {
        drain(core, now_ms, peer, sink);
    }
    if (!core->dead) {
        cyc_session_tick(core->session, now_ms, &reaction);
        apply(core, &reaction, peer, sink);
    }
    if (core->dead && cyc_session_state(core->session) != CYC_STATE_CLOSED) {
        cyc_session_transport_closed(core->session, core->dead_reason, &reaction);
        apply(core, &reaction, peer, sink);
    }
}

cyc_result cyc_core_send(cyc_core *core, uint32_t message_id, const uint8_t *payload, size_t len) {
    size_t written = 0;

    if (!cyc_session_ready(core->session)) {
        return CYC_ERR_NOT_READY;
    }
    if (core->dead) {
        return CYC_ERR_CLOSED;
    }
    if (len > (size_t)core->config.max_message_bytes) {
        return CYC_ERR_TOO_LARGE;
    }
    if (core->outbox_len > core->outbox_off) {
        return CYC_ERR_CONGESTED;
    }
    if (cyc_frame_encode_data(message_id, payload, len, core->scratch, core->scratch_cap,
                              &written) != CYC_FRAME_OK) {
        return CYC_ERR_TOO_LARGE;
    }
    return write_frame(core, core->scratch, written);
}

void cyc_core_close(cyc_core *core) {
    cyc_session_close(core->session);
    core_shutdown(core);
}

bool cyc_core_finished(const cyc_core *core) {
    return core->dead && cyc_session_state(core->session) == CYC_STATE_CLOSED;
}

bool cyc_core_congested(const cyc_core *core) {
    return core->outbox_len > core->outbox_off;
}
