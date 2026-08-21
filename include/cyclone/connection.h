#ifndef CYCLONE_CONNECTION_H
#define CYCLONE_CONNECTION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cyclone/common.h"
#include "cyclone/event.h"
#include "cyclone/schema.h"
#include "cyclone/session.h"
#include "cyclone/transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cyc_connection cyc_connection;

cyc_connection *cyc_connection_create(cyc_transport transport, const cyc_schema *schema,
                                      const cyc_config *config, uint64_t now_ms);
void cyc_connection_destroy(cyc_connection *connection);

size_t cyc_connection_tick(cyc_connection *connection, uint64_t now_ms);
const cyc_event *cyc_connection_events(const cyc_connection *connection, size_t *count);

cyc_result cyc_connection_send(cyc_connection *connection, uint32_t message_id,
                               const uint8_t *payload, size_t len);
void cyc_connection_close(cyc_connection *connection);

cyc_state cyc_connection_state(const cyc_connection *connection);
bool cyc_connection_ready(const cyc_connection *connection);
bool cyc_connection_congested(const cyc_connection *connection);

#ifdef __cplusplus
}
#endif

#endif /* CYCLONE_CONNECTION_H */
