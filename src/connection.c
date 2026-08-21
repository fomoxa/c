#include "cyclone/connection.h"

#include <stdlib.h>
#include <string.h>

#include "core.h"

struct cyc_connection {
    cyc_core core;
    cyc_sink sink;
};

cyc_connection *cyc_connection_create(cyc_transport transport, const cyc_schema *schema,
                                      const cyc_config *config, uint64_t now_ms) {
    cyc_connection *connection;
    cyc_config effective;

    if (transport.vtable == NULL || schema == NULL) {
        return NULL;
    }
    if (config == NULL) {
        cyc_config_defaults(&effective);
    } else {
        effective = *config;
    }
    if (cyc_schema_check(schema) != CYC_OK) {
        return NULL;
    }

    connection = (cyc_connection *)calloc(1, sizeof(*connection));
    if (connection == NULL) {
        return NULL;
    }
    if (cyc_sink_init(&connection->sink, (size_t)effective.max_frames_per_tick + 4) != CYC_OK) {
        free(connection);
        return NULL;
    }
    if (cyc_core_init(&connection->core, transport, schema, &effective, CYC_ROLE_CLIENT, now_ms) !=
        CYC_OK) {
        cyc_sink_release(&connection->sink);
        free(connection);
        return NULL;
    }
    return connection;
}

void cyc_connection_destroy(cyc_connection *connection) {
    if (connection == NULL) {
        return;
    }
    cyc_core_release(&connection->core);
    cyc_sink_release(&connection->sink);
    free(connection);
}

size_t cyc_connection_tick(cyc_connection *connection, uint64_t now_ms) {
    cyc_sink_clear(&connection->sink);
    cyc_core_tick(&connection->core, now_ms, 0, &connection->sink);
    cyc_sink_resolve(&connection->sink);
    return connection->sink.event_count;
}

const cyc_event *cyc_connection_events(const cyc_connection *connection, size_t *count) {
    if (count != NULL) {
        *count = connection->sink.event_count;
    }
    return connection->sink.events;
}

cyc_result cyc_connection_send(cyc_connection *connection, uint32_t message_id,
                               const uint8_t *payload, size_t len) {
    return cyc_core_send(&connection->core, message_id, payload, len);
}

void cyc_connection_close(cyc_connection *connection) {
    cyc_core_close(&connection->core);
}

cyc_state cyc_connection_state(const cyc_connection *connection) {
    return cyc_session_state(connection->core.session);
}

bool cyc_connection_ready(const cyc_connection *connection) {
    return cyc_session_ready(connection->core.session);
}

bool cyc_connection_congested(const cyc_connection *connection) {
    return cyc_core_congested(&connection->core);
}
