#include "core.h"

#include <stdlib.h>
#include <string.h>

#include "fomoxa/handshake.h"

fmx_result fmx_sink_init(fmx_sink *sink, size_t event_cap) {
    memset(sink, 0, sizeof(*sink));
    sink->events = (fmx_event *)malloc(event_cap * sizeof(fmx_event));
    sink->offsets = (size_t *)malloc(event_cap * sizeof(size_t));
    if (sink->events == NULL || sink->offsets == NULL) {
        fmx_sink_release(sink);
        return FMX_ERR_NO_MEMORY;
    }
    sink->event_cap = event_cap;
    return FMX_OK;
}

void fmx_sink_release(fmx_sink *sink) {
    free(sink->arena);
    free(sink->events);
    free(sink->offsets);
    memset(sink, 0, sizeof(*sink));
}

void fmx_sink_clear(fmx_sink *sink) {
    sink->arena_len = 0;
    sink->event_count = 0;
}

void fmx_sink_push(fmx_sink *sink, uint64_t peer, fmx_event_kind kind, uint32_t message_id,
                   const uint8_t *payload, size_t payload_len, int reason) {
    fmx_event *event;
    size_t offset = 0;

    if (sink->event_count >= sink->event_cap) {
        size_t grown = sink->event_cap == 0 ? 16 : sink->event_cap * 2;
        fmx_event *events = (fmx_event *)realloc(sink->events, grown * sizeof(fmx_event));
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

void fmx_sink_resolve(fmx_sink *sink) {
    size_t index;
    for (index = 0; index < sink->event_count; ++index) {
        if (sink->events[index].payload_len > 0) {
            sink->events[index].payload = sink->arena + sink->offsets[index];
        } else {
            sink->events[index].payload = NULL;
        }
    }
}

/* One data frame plus the handful of control frames the protocol can owe at
   any moment: one probe per silence window, one ack per probe, and at most one
   query round per session. */
#define FMX_MAX_OUTBOX_BYTES ((size_t)(64u * 1024u))

static void core_kill(fmx_core *core, fmx_disconnect reason) {
    if (!core->dead) {
        core->dead = true;
        core->dead_reason = reason;
    }
}

static void core_shutdown(fmx_core *core) {
    core_kill(core, FMX_DISCONNECT_LOCAL);
    core->outbox_len = 0;
    core->outbox_off = 0;
    core->transport.vtable->close_soft(&core->transport);
}

static fmx_result outbox_set(fmx_core *core, const uint8_t *bytes, size_t len) {
    if (len > core->outbox_cap) {
        uint8_t *grown = (uint8_t *)realloc(core->outbox, len);
        if (grown == NULL) {
            core_kill(core, FMX_DISCONNECT_TRANSPORT_ERROR);
            return FMX_ERR_NO_MEMORY;
        }
        core->outbox = grown;
        core->outbox_cap = len;
    }
    memcpy(core->outbox, bytes, len);
    core->outbox_len = len;
    core->outbox_off = 0;
    return FMX_OK;
}

static fmx_result outbox_append(fmx_core *core, const uint8_t *bytes, size_t len) {
    size_t needed = core->outbox_len + len;
    if (needed > core->outbox_cap) {
        uint8_t *grown = (uint8_t *)realloc(core->outbox, needed);
        if (grown == NULL) {
            core_kill(core, FMX_DISCONNECT_TRANSPORT_ERROR);
            return FMX_ERR_NO_MEMORY;
        }
        core->outbox = grown;
        core->outbox_cap = needed;
    }
    memcpy(core->outbox + core->outbox_len, bytes, len);
    core->outbox_len = needed;
    return FMX_OK;
}

static fmx_result write_frame(fmx_core *core, const uint8_t *bytes, size_t len) {
    size_t accepted = 0;
    fmx_send_result result = core->transport.vtable->send(&core->transport, bytes, len, &accepted);

    switch (result) {
    case FMX_SEND_SENT:
        return FMX_OK;
    case FMX_SEND_PARTIAL:
        if (accepted == 0 || accepted >= len) {
            return accepted >= len ? FMX_OK : outbox_set(core, bytes, len);
        }
        return outbox_set(core, bytes + accepted, len - accepted);
    case FMX_SEND_WOULD_BLOCK:
        return outbox_set(core, bytes, len);
    case FMX_SEND_TOO_LARGE:
        return FMX_ERR_TOO_LARGE;
    case FMX_SEND_CLOSED:
        core_kill(core, FMX_DISCONNECT_PEER_CLOSED);
        return FMX_ERR_CLOSED;
    case FMX_SEND_ERROR:
        core_kill(core, FMX_DISCONNECT_TRANSPORT_ERROR);
        return FMX_ERR_CLOSED;
    }
    return FMX_ERR_CLOSED;
}

static void send_control(fmx_core *core, const uint8_t *bytes, size_t len) {
    if (core->dead || len == 0) {
        return;
    }
    if (core->outbox_len > core->outbox_off) {
        /* Queue behind what is already waiting, never overwrite it: a refusal
           verdict lost that way leaves the peer waiting out its deadline
           without ever learning why. The protocol caps how many control frames
           can be owed at once, so passing the ceiling means an assumption
           broke - the peer stopped reading, which is the same "not keeping up"
           a heartbeat timeout reports. The transport itself is fine, so a
           transport error would be untrue. (02 §8) */
        if (core->outbox_len - core->outbox_off + len > FMX_MAX_OUTBOX_BYTES) {
            core_kill(core, FMX_DISCONNECT_UNRESPONSIVE);
            return;
        }
        (void)outbox_append(core, bytes, len);
        return;
    }
    (void)write_frame(core, bytes, len);
}

static void core_flush(fmx_core *core) {
    while (core->outbox_len > core->outbox_off) {
        size_t accepted = 0;
        fmx_send_result result =
            core->transport.vtable->send(&core->transport, core->outbox + core->outbox_off,
                                         core->outbox_len - core->outbox_off, &accepted);
        switch (result) {
        case FMX_SEND_SENT:
            core->outbox_len = 0;
            core->outbox_off = 0;
            return;
        case FMX_SEND_PARTIAL:
            if (accepted == 0) {
                return;
            }
            core->outbox_off += accepted;
            break;
        case FMX_SEND_WOULD_BLOCK:
            return;
        case FMX_SEND_TOO_LARGE:
            core->outbox_len = 0;
            core->outbox_off = 0;
            return;
        case FMX_SEND_CLOSED:
            core_kill(core, FMX_DISCONNECT_PEER_CLOSED);
            return;
        case FMX_SEND_ERROR:
            core_kill(core, FMX_DISCONNECT_TRANSPORT_ERROR);
            return;
        }
    }
    core->outbox_len = 0;
    core->outbox_off = 0;
}

static void emit_out(fmx_core *core, const fmx_reaction *reaction) {
    size_t written = 0;

    switch (reaction->out) {
    case FMX_OUT_PROBE:
        written = fmx_frame_encode_probe(core->scratch, core->scratch_cap);
        break;
    case FMX_OUT_ACK:
        written = fmx_frame_encode_ack(core->scratch, core->scratch_cap);
        break;
    case FMX_OUT_HANDSHAKE:
        if (fmx_frame_encode_handshake(reaction->out_payload, reaction->out_payload_len,
                                       core->scratch, core->scratch_cap,
                                       &written) != FMX_FRAME_OK) {
            return;
        }
        break;
    case FMX_OUT_NONE:
        return;
    }
    send_control(core, core->scratch, written);
}

static void apply(fmx_core *core, const fmx_reaction *reaction, uint64_t peer, fmx_sink *sink) {
    if (reaction->out != FMX_OUT_NONE) {
        emit_out(core, reaction);
    }
    if (reaction->has_event) {
        fmx_sink_push(sink, peer, reaction->event, reaction->message_id, reaction->payload,
                      reaction->payload_len, reaction->reason);
        if (reaction->event == FMX_EVENT_HANDSHAKE_FAILED) {
            core_shutdown(core);
        }
    }
}

static bool grow_recv(fmx_core *core, size_t needed) {
    uint8_t *grown;
    if (needed <= core->recv_cap) {
        return true;
    }
    grown = (uint8_t *)realloc(core->recv, needed);
    if (grown == NULL) {
        core_kill(core, FMX_DISCONNECT_TRANSPORT_ERROR);
        return false;
    }
    core->recv = grown;
    core->recv_cap = needed;
    return true;
}

static void drain(fmx_core *core, uint64_t now_ms, uint64_t peer, fmx_sink *sink) {
    size_t frames = core->config.max_frames_per_tick;
    size_t reads = frames;

    while (frames > 0 && reads > 0 && !core->dead) {
        size_t received = 0;
        size_t needed = 0;
        fmx_recv_result result;
        fmx_reaction reaction;

        if (core->stream) {
            fmx_frame frame;
            size_t frame_len = 0;
            fmx_frame_error error = fmx_stream_decoder_next(&core->decoder, &frame, &frame_len);

            if (error == FMX_FRAME_OK) {
                fmx_session_on_frame(core->session, &frame, now_ms, &reaction);
                apply(core, &reaction, peer, sink);
                fmx_stream_decoder_advance(&core->decoder, frame_len);
                frames -= 1;
                continue;
            }
            if (error != FMX_FRAME_INCOMPLETE) {
                core_kill(core, FMX_DISCONNECT_TRANSPORT_ERROR);
                return;
            }
        }

        result = core->transport.vtable->recv(&core->transport, core->recv, core->recv_cap,
                                              &received, &needed);
        switch (result) {
        case FMX_RECV_RECEIVED:
            if (core->stream) {
                if (fmx_stream_decoder_feed(&core->decoder, core->recv, received) != FMX_OK) {
                    core_kill(core, FMX_DISCONNECT_TRANSPORT_ERROR);
                    return;
                }
                reads -= 1;
            } else {
                fmx_frame frame;
                if (fmx_frame_decode_packet(core->recv, received, core->config.max_message_bytes,
                                            &frame) == FMX_FRAME_OK) {
                    fmx_session_on_frame(core->session, &frame, now_ms, &reaction);
                    apply(core, &reaction, peer, sink);
                }
                frames -= 1;
            }
            break;

        case FMX_RECV_NEED_CAPACITY:
            if (!grow_recv(core, needed)) {
                return;
            }
            reads -= 1;
            break;

        case FMX_RECV_WOULD_BLOCK:
            return;

        case FMX_RECV_CLOSED:
            core_kill(core, FMX_DISCONNECT_PEER_CLOSED);
            return;

        case FMX_RECV_ERROR:
            core_kill(core, FMX_DISCONNECT_TRANSPORT_ERROR);
            return;
        }
    }
}

fmx_result fmx_core_init(fmx_core *core, fmx_transport transport, const fmx_schema *schema,
                         const fmx_config *config, fmx_role role, uint64_t now_ms) {
    fmx_reaction opening;
    size_t handshake_frame;
    size_t data_frame;

    memset(core, 0, sizeof(*core));
    core->transport = transport;
    core->config = *config;
    core->stream = transport.vtable->kind(&transport) == FMX_TRANSPORT_STREAM;

    if (fmx_hello_len(schema) > FMX_MAX_HANDSHAKE_PAYLOAD) {
        return FMX_ERR_INVALID;
    }

    handshake_frame = fmx_frame_handshake_len(fmx_hello_len(schema));
    data_frame = fmx_frame_data_len(config->max_message_bytes);
    core->scratch_cap = handshake_frame > data_frame ? handshake_frame : data_frame;
    core->scratch = (uint8_t *)malloc(core->scratch_cap);

    core->recv_cap = 4096;
    core->recv = (uint8_t *)malloc(core->recv_cap);

    if (core->scratch == NULL || core->recv == NULL) {
        fmx_core_release(core);
        return FMX_ERR_NO_MEMORY;
    }

    if (core->stream && fmx_stream_decoder_init(&core->decoder, config->max_message_bytes) !=
                            FMX_OK) {
        fmx_core_release(core);
        return FMX_ERR_NO_MEMORY;
    }

    core->session = fmx_session_create(role, schema, config, now_ms, &opening);
    if (core->session == NULL) {
        fmx_core_release(core);
        return FMX_ERR_NO_MEMORY;
    }
    if (opening.out != FMX_OUT_NONE) {
        emit_out(core, &opening);
    }
    return FMX_OK;
}

void fmx_core_release(fmx_core *core) {
    if (core->transport.vtable != NULL) {
        core->transport.vtable->close_hard(&core->transport);
        core->transport.vtable = NULL;
    }
    if (core->session != NULL) {
        fmx_session_destroy(core->session);
        core->session = NULL;
    }
    if (core->stream) {
        fmx_stream_decoder_release(&core->decoder);
    }
    free(core->outbox);
    free(core->recv);
    free(core->scratch);
    core->outbox = NULL;
    core->recv = NULL;
    core->scratch = NULL;
}

void fmx_core_tick(fmx_core *core, uint64_t now_ms, uint64_t peer, fmx_sink *sink) {
    fmx_reaction reaction;

    if (!core->announced) {
        core->announced = true;
        fmx_sink_push(sink, peer, FMX_EVENT_CONNECTED, 0, NULL, 0, 0);
    }

    if (!core->dead) {
        core_flush(core);
    }
    if (!core->dead) {
        drain(core, now_ms, peer, sink);
    }
    if (!core->dead) {
        fmx_session_tick(core->session, now_ms, &reaction);
        apply(core, &reaction, peer, sink);
    }
    if (core->dead && fmx_session_state(core->session) != FMX_STATE_CLOSED) {
        fmx_session_transport_closed(core->session, core->dead_reason, &reaction);
        apply(core, &reaction, peer, sink);
    }
}

fmx_result fmx_core_send(fmx_core *core, uint32_t message_id, const uint8_t *payload, size_t len) {
    size_t written = 0;

    if (!fmx_session_ready(core->session)) {
        return FMX_ERR_NOT_READY;
    }
    if (core->dead) {
        return FMX_ERR_CLOSED;
    }
    if (len > (size_t)core->config.max_message_bytes) {
        return FMX_ERR_TOO_LARGE;
    }
    if (core->outbox_len > core->outbox_off) {
        return FMX_ERR_CONGESTED;
    }
    if (fmx_frame_encode_data(message_id, payload, len, core->scratch, core->scratch_cap,
                              &written) != FMX_FRAME_OK) {
        return FMX_ERR_TOO_LARGE;
    }
    return write_frame(core, core->scratch, written);
}

void fmx_core_close(fmx_core *core) {
    fmx_session_close(core->session);
    core_shutdown(core);
}

bool fmx_core_finished(const fmx_core *core) {
    return core->dead && fmx_session_state(core->session) == FMX_STATE_CLOSED;
}

bool fmx_core_congested(const fmx_core *core) {
    return core->outbox_len > core->outbox_off;
}
