#ifndef CYCLONE_INTERNAL_CORE_H
#define CYCLONE_INTERNAL_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cyclone/common.h"
#include "cyclone/event.h"
#include "cyclone/frame.h"
#include "cyclone/schema.h"
#include "cyclone/session.h"
#include "cyclone/transport.h"

typedef struct cyc_sink {
    uint8_t *arena;
    size_t arena_cap;
    size_t arena_len;
    cyc_event *events;
    size_t *offsets;
    size_t event_cap;
    size_t event_count;
} cyc_sink;

cyc_result cyc_sink_init(cyc_sink *sink, size_t event_cap);
void cyc_sink_release(cyc_sink *sink);
void cyc_sink_clear(cyc_sink *sink);
void cyc_sink_push(cyc_sink *sink, uint64_t peer, cyc_event_kind kind, uint32_t message_id,
                   const uint8_t *payload, size_t payload_len, int reason);
void cyc_sink_resolve(cyc_sink *sink);

typedef struct cyc_core {
    cyc_transport transport;
    cyc_session *session;
    cyc_config config;
    bool stream;
    cyc_stream_decoder decoder;
    uint8_t *outbox;
    size_t outbox_cap;
    size_t outbox_len;
    size_t outbox_off;
    uint8_t *recv;
    size_t recv_cap;
    uint8_t *scratch;
    size_t scratch_cap;
    bool dead;
    cyc_disconnect dead_reason;
    bool announced;
} cyc_core;

cyc_result cyc_core_init(cyc_core *core, cyc_transport transport, const cyc_schema *schema,
                         const cyc_config *config, cyc_role role, uint64_t now_ms);
void cyc_core_release(cyc_core *core);
void cyc_core_tick(cyc_core *core, uint64_t now_ms, uint64_t peer, cyc_sink *sink);
cyc_result cyc_core_send(cyc_core *core, uint32_t message_id, const uint8_t *payload, size_t len);
void cyc_core_close(cyc_core *core);
bool cyc_core_finished(const cyc_core *core);
bool cyc_core_congested(const cyc_core *core);

#endif /* CYCLONE_INTERNAL_CORE_H */
