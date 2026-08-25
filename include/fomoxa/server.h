#ifndef FOMOXA_SERVER_H
#define FOMOXA_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fomoxa/common.h"
#include "fomoxa/event.h"
#include "fomoxa/schema.h"
#include "fomoxa/session.h"
#include "fomoxa/transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fmx_server fmx_server;

fmx_server *fmx_server_create(fmx_listener listener, const fmx_schema *schema,
                              const fmx_config *config);
void fmx_server_destroy(fmx_server *server);

size_t fmx_server_tick(fmx_server *server, uint64_t now_ms);
const fmx_event *fmx_server_events(const fmx_server *server, size_t *count);

fmx_result fmx_server_send(fmx_server *server, uint64_t peer, uint32_t message_id,
                           const uint8_t *payload, size_t len);
void fmx_server_broadcast(fmx_server *server, uint32_t message_id, const uint8_t *payload,
                          size_t len);
void fmx_server_disconnect(fmx_server *server, uint64_t peer);

size_t fmx_server_peer_count(const fmx_server *server);
uint64_t fmx_server_peer_at(const fmx_server *server, size_t index);
bool fmx_server_peer_ready(const fmx_server *server, uint64_t peer);
void fmx_server_shrink(fmx_server *server);

#ifdef __cplusplus
}
#endif

#endif /* FOMOXA_SERVER_H */
