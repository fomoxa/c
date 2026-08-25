#ifndef FOMOXA_INTERNAL_CORE_H
#define FOMOXA_INTERNAL_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fomoxa/common.h"
#include "fomoxa/event.h"
#include "fomoxa/frame.h"
#include "fomoxa/schema.h"
#include "fomoxa/session.h"
#include "fomoxa/transport.h"

typedef struct fmx_sink {
    uint8_t *arena;
    size_t arena_cap;
    size_t arena_len;
    fmx_event *events;
    size_t *offsets;
    size_t event_cap;
    size_t event_count;
} fmx_sink;

fmx_result fmx_sink_init(fmx_sink *sink, size_t event_cap);
void fmx_sink_release(fmx_sink *sink);
void fmx_sink_clear(fmx_sink *sink);
void fmx_sink_push(fmx_sink *sink, uint64_t peer, fmx_event_kind kind, uint32_t message_id,
                   const uint8_t *payload, size_t payload_len, int reason);
void fmx_sink_resolve(fmx_sink *sink);
void fmx_sink_shrink(fmx_sink *sink);

typedef struct fmx_core {
    fmx_transport transport;
    fmx_session *session;
    fmx_config config;
    bool stream;
    fmx_stream_decoder decoder;
    uint8_t *outbox;
    size_t outbox_cap;
    size_t outbox_len;
    size_t outbox_off;
    uint8_t *recv;
    size_t recv_cap;
    uint8_t *scratch;
    size_t scratch_cap;
    bool dead;
    fmx_disconnect dead_reason;
    bool announced;
} fmx_core;

fmx_result fmx_core_init(fmx_core *core, fmx_transport transport, const fmx_schema *schema,
                         const fmx_config *config, fmx_role role, uint64_t now_ms);
void fmx_core_release(fmx_core *core);
void fmx_core_tick(fmx_core *core, uint64_t now_ms, uint64_t peer, fmx_sink *sink);
fmx_result fmx_core_send(fmx_core *core, uint32_t message_id, const uint8_t *payload, size_t len);
void fmx_core_close(fmx_core *core);
bool fmx_core_finished(const fmx_core *core);
bool fmx_core_congested(const fmx_core *core);
void fmx_core_shrink(fmx_core *core);

#endif /* FOMOXA_INTERNAL_CORE_H */
