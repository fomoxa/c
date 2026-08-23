#ifndef FOMOXA_CONNECTION_H
#define FOMOXA_CONNECTION_H

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

typedef struct fmx_connection fmx_connection;

fmx_connection *fmx_connection_create(fmx_transport transport, const fmx_schema *schema,
                                      const fmx_config *config, uint64_t now_ms);
void fmx_connection_destroy(fmx_connection *connection);

size_t fmx_connection_tick(fmx_connection *connection, uint64_t now_ms);
const fmx_event *fmx_connection_events(const fmx_connection *connection, size_t *count);

fmx_result fmx_connection_send(fmx_connection *connection, uint32_t message_id,
                               const uint8_t *payload, size_t len);
void fmx_connection_close(fmx_connection *connection);

fmx_state fmx_connection_state(const fmx_connection *connection);
bool fmx_connection_ready(const fmx_connection *connection);
bool fmx_connection_congested(const fmx_connection *connection);

#ifdef __cplusplus
}
#endif

#endif /* FOMOXA_CONNECTION_H */
