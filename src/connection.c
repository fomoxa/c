#include "fomoxa/connection.h"

#include <stdlib.h>
#include <string.h>

#include "core.h"

struct fmx_connection {
    fmx_core core;
    fmx_sink sink;
};

fmx_connection *fmx_connection_create(fmx_transport transport, const fmx_schema *schema,
                                      const fmx_config *config, uint64_t now_ms) {
    fmx_connection *connection;
    fmx_config effective;

    if (transport.vtable == NULL || schema == NULL) {
        return NULL;
    }
    if (config == NULL) {
        fmx_config_defaults(&effective);
    } else {
        effective = *config;
    }
    if (fmx_schema_check(schema) != FMX_OK) {
        return NULL;
    }

    connection = (fmx_connection *)calloc(1, sizeof(*connection));
    if (connection == NULL) {
        return NULL;
    }
    if (fmx_sink_init(&connection->sink, (size_t)effective.max_frames_per_tick + 4) != FMX_OK) {
        free(connection);
        return NULL;
    }
    if (fmx_core_init(&connection->core, transport, schema, &effective, FMX_ROLE_CLIENT, now_ms) !=
        FMX_OK) {
        fmx_sink_release(&connection->sink);
        free(connection);
        return NULL;
    }
    return connection;
}

void fmx_connection_destroy(fmx_connection *connection) {
    if (connection == NULL) {
        return;
    }
    fmx_core_release(&connection->core);
    fmx_sink_release(&connection->sink);
    free(connection);
}

size_t fmx_connection_tick(fmx_connection *connection, uint64_t now_ms) {
    fmx_sink_clear(&connection->sink);
    fmx_core_tick(&connection->core, now_ms, 0, &connection->sink);
    fmx_sink_resolve(&connection->sink);
    return connection->sink.event_count;
}

const fmx_event *fmx_connection_events(const fmx_connection *connection, size_t *count) {
    if (count != NULL) {
        *count = connection->sink.event_count;
    }
    return connection->sink.events;
}

fmx_result fmx_connection_send(fmx_connection *connection, uint32_t message_id,
                               const uint8_t *payload, size_t len) {
    return fmx_core_send(&connection->core, message_id, payload, len);
}

void fmx_connection_close(fmx_connection *connection) {
    fmx_core_close(&connection->core);
}

fmx_state fmx_connection_state(const fmx_connection *connection) {
    return fmx_session_state(connection->core.session);
}

bool fmx_connection_ready(const fmx_connection *connection) {
    return fmx_session_ready(connection->core.session);
}

bool fmx_connection_congested(const fmx_connection *connection) {
    return fmx_core_congested(&connection->core);
}

void fmx_connection_shrink(fmx_connection *connection) {
    fmx_sink_shrink(&connection->sink);
    fmx_core_shrink(&connection->core);
}
