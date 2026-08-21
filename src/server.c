#include "cyclone/server.h"

#include <stdlib.h>
#include <string.h>

#include "core.h"

typedef struct cyc_peer {
    uint64_t id;
    cyc_core core;
} cyc_peer;

struct cyc_server {
    cyc_listener listener;
    const cyc_schema *schema;
    cyc_config config;
    cyc_peer *peers;
    size_t peer_count;
    size_t peer_cap;
    uint64_t next_id;
    cyc_sink sink;
};

cyc_server *cyc_server_create(cyc_listener listener, const cyc_schema *schema,
                              const cyc_config *config) {
    cyc_server *server;
    cyc_config effective;

    if (listener.vtable == NULL || schema == NULL) {
        return NULL;
    }
    if (config == NULL) {
        cyc_config_defaults(&effective);
    } else {
        effective = *config;
    }
    if (cyc_schema_check(schema) != CYC_OK || effective.max_peers == 0) {
        return NULL;
    }

    server = (cyc_server *)calloc(1, sizeof(*server));
    if (server == NULL) {
        return NULL;
    }
    server->peers = (cyc_peer *)calloc(effective.max_peers, sizeof(cyc_peer));
    if (server->peers == NULL) {
        free(server);
        return NULL;
    }
    if (cyc_sink_init(&server->sink, 64) != CYC_OK) {
        free(server->peers);
        free(server);
        return NULL;
    }
    server->listener = listener;
    server->schema = schema;
    server->config = effective;
    server->peer_cap = effective.max_peers;
    server->next_id = 1;
    return server;
}

void cyc_server_destroy(cyc_server *server) {
    size_t index;

    if (server == NULL) {
        return;
    }
    for (index = 0; index < server->peer_count; ++index) {
        cyc_core_release(&server->peers[index].core);
    }
    if (server->listener.vtable != NULL) {
        server->listener.vtable->close(&server->listener);
    }
    cyc_sink_release(&server->sink);
    free(server->peers);
    free(server);
}

static void accept_peers(cyc_server *server, uint64_t now_ms) {
    size_t room = server->config.max_frames_per_tick;

    while (room > 0 && server->peer_count < server->peer_cap) {
        cyc_transport transport;
        cyc_peer *peer;

        memset(&transport, 0, sizeof(transport));
        if (server->listener.vtable->accept(&server->listener, &transport) !=
            CYC_ACCEPT_ACCEPTED) {
            return;
        }

        peer = &server->peers[server->peer_count];
        peer->id = server->next_id;
        if (cyc_core_init(&peer->core, transport, server->schema, &server->config, CYC_ROLE_SERVER,
                          now_ms) != CYC_OK) {
            transport.vtable->close_hard(&transport);
            return;
        }
        server->next_id += 1;
        server->peer_count += 1;
        room -= 1;
    }
}

size_t cyc_server_tick(cyc_server *server, uint64_t now_ms) {
    size_t index = 0;

    cyc_sink_clear(&server->sink);
    accept_peers(server, now_ms);

    for (index = 0; index < server->peer_count; ++index) {
        cyc_core_tick(&server->peers[index].core, now_ms, server->peers[index].id, &server->sink);
    }

    index = 0;
    while (index < server->peer_count) {
        if (cyc_core_finished(&server->peers[index].core)) {
            cyc_core_release(&server->peers[index].core);
            if (index + 1 < server->peer_count) {
                memmove(&server->peers[index], &server->peers[index + 1],
                        (server->peer_count - index - 1) * sizeof(cyc_peer));
            }
            server->peer_count -= 1;
        } else {
            index += 1;
        }
    }

    cyc_sink_resolve(&server->sink);
    return server->sink.event_count;
}

const cyc_event *cyc_server_events(const cyc_server *server, size_t *count) {
    if (count != NULL) {
        *count = server->sink.event_count;
    }
    return server->sink.events;
}

static cyc_peer *find_peer(cyc_server *server, uint64_t peer) {
    size_t index;
    for (index = 0; index < server->peer_count; ++index) {
        if (server->peers[index].id == peer) {
            return &server->peers[index];
        }
    }
    return NULL;
}

cyc_result cyc_server_send(cyc_server *server, uint64_t peer, uint32_t message_id,
                           const uint8_t *payload, size_t len) {
    cyc_peer *found = find_peer(server, peer);
    if (found == NULL) {
        return CYC_ERR_CLOSED;
    }
    return cyc_core_send(&found->core, message_id, payload, len);
}

void cyc_server_broadcast(cyc_server *server, uint32_t message_id, const uint8_t *payload,
                          size_t len) {
    size_t index;
    for (index = 0; index < server->peer_count; ++index) {
        (void)cyc_core_send(&server->peers[index].core, message_id, payload, len);
    }
}

void cyc_server_disconnect(cyc_server *server, uint64_t peer) {
    cyc_peer *found = find_peer(server, peer);
    if (found != NULL) {
        cyc_core_close(&found->core);
    }
}

size_t cyc_server_peer_count(const cyc_server *server) {
    return server->peer_count;
}

uint64_t cyc_server_peer_at(const cyc_server *server, size_t index) {
    if (index >= server->peer_count) {
        return 0;
    }
    return server->peers[index].id;
}

bool cyc_server_peer_ready(const cyc_server *server, uint64_t peer) {
    cyc_peer *found = find_peer((cyc_server *)server, peer);
    return found != NULL && cyc_session_ready(found->core.session);
}
