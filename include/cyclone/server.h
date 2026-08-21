#ifndef CYCLONE_SERVER_H
#define CYCLONE_SERVER_H

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

typedef struct cyc_server cyc_server;

cyc_server *cyc_server_create(cyc_listener listener, const cyc_schema *schema,
                              const cyc_config *config);
void cyc_server_destroy(cyc_server *server);

size_t cyc_server_tick(cyc_server *server, uint64_t now_ms);
const cyc_event *cyc_server_events(const cyc_server *server, size_t *count);

cyc_result cyc_server_send(cyc_server *server, uint64_t peer, uint32_t message_id,
                           const uint8_t *payload, size_t len);
void cyc_server_broadcast(cyc_server *server, uint32_t message_id, const uint8_t *payload,
                          size_t len);
void cyc_server_disconnect(cyc_server *server, uint64_t peer);

size_t cyc_server_peer_count(const cyc_server *server);
uint64_t cyc_server_peer_at(const cyc_server *server, size_t index);
bool cyc_server_peer_ready(const cyc_server *server, uint64_t peer);

#ifdef __cplusplus
}
#endif

#endif /* CYCLONE_SERVER_H */
