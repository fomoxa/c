#include "fomoxa/server.h"

#include <stdlib.h>
#include <string.h>

#include "core.h"

typedef struct fmx_peer {
    uint64_t id;
    fmx_core core;
} fmx_peer;

struct fmx_server {
    fmx_listener listener;
    const fmx_schema *schema;
    fmx_config config;
    fmx_peer *peers;
    size_t peer_count;
    size_t peer_cap;
    uint64_t next_id;
    fmx_sink sink;
};

fmx_server *fmx_server_create(fmx_listener listener, const fmx_schema *schema,
                              const fmx_config *config) {
    fmx_server *server;
    fmx_config effective;

    if (listener.vtable == NULL || schema == NULL) {
        return NULL;
    }
    if (config == NULL) {
        fmx_config_defaults(&effective);
    } else {
        effective = *config;
    }
    if (fmx_schema_check(schema) != FMX_OK || effective.max_peers == 0) {
        return NULL;
    }

    server = (fmx_server *)calloc(1, sizeof(*server));
    if (server == NULL) {
        return NULL;
    }
    server->peers = (fmx_peer *)calloc(effective.max_peers, sizeof(fmx_peer));
    if (server->peers == NULL) {
        free(server);
        return NULL;
    }
    if (fmx_sink_init(&server->sink, 64) != FMX_OK) {
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

void fmx_server_destroy(fmx_server *server) {
    size_t index;

    if (server == NULL) {
        return;
    }
    for (index = 0; index < server->peer_count; ++index) {
        fmx_core_release(&server->peers[index].core);
    }
    if (server->listener.vtable != NULL) {
        server->listener.vtable->close(&server->listener);
    }
    fmx_sink_release(&server->sink);
    free(server->peers);
    free(server);
}

static void accept_peers(fmx_server *server, uint64_t now_ms) {
    size_t room = server->config.max_frames_per_tick;

    while (room > 0 && server->peer_count < server->peer_cap) {
        fmx_transport transport;
        fmx_peer *peer;

        memset(&transport, 0, sizeof(transport));
        if (server->listener.vtable->accept(&server->listener, &transport) !=
            FMX_ACCEPT_ACCEPTED) {
            return;
        }

        peer = &server->peers[server->peer_count];
        peer->id = server->next_id;
        if (fmx_core_init(&peer->core, transport, server->schema, &server->config, FMX_ROLE_SERVER,
                          now_ms) != FMX_OK) {
            transport.vtable->close_hard(&transport);
            return;
        }
        server->next_id += 1;
        server->peer_count += 1;
        room -= 1;
    }
}

size_t fmx_server_tick(fmx_server *server, uint64_t now_ms) {
    size_t index = 0;

    fmx_sink_clear(&server->sink);
    accept_peers(server, now_ms);

    for (index = 0; index < server->peer_count; ++index) {
        fmx_core_tick(&server->peers[index].core, now_ms, server->peers[index].id, &server->sink);
    }

    index = 0;
    while (index < server->peer_count) {
        if (fmx_core_finished(&server->peers[index].core)) {
            fmx_core_release(&server->peers[index].core);
            if (index + 1 < server->peer_count) {
                memmove(&server->peers[index], &server->peers[index + 1],
                        (server->peer_count - index - 1) * sizeof(fmx_peer));
            }
            server->peer_count -= 1;
        } else {
            index += 1;
        }
    }

    fmx_sink_resolve(&server->sink);
    return server->sink.event_count;
}

const fmx_event *fmx_server_events(const fmx_server *server, size_t *count) {
    if (count != NULL) {
        *count = server->sink.event_count;
    }
    return server->sink.events;
}

static fmx_peer *find_peer(fmx_server *server, uint64_t peer) {
    size_t index;
    for (index = 0; index < server->peer_count; ++index) {
        if (server->peers[index].id == peer) {
            return &server->peers[index];
        }
    }
    return NULL;
}

fmx_result fmx_server_send(fmx_server *server, uint64_t peer, uint32_t message_id,
                           const uint8_t *payload, size_t len) {
    fmx_peer *found = find_peer(server, peer);
    if (found == NULL) {
        return FMX_ERR_CLOSED;
    }
    return fmx_core_send(&found->core, message_id, payload, len);
}

void fmx_server_broadcast(fmx_server *server, uint32_t message_id, const uint8_t *payload,
                          size_t len) {
    size_t index;
    for (index = 0; index < server->peer_count; ++index) {
        (void)fmx_core_send(&server->peers[index].core, message_id, payload, len);
    }
}

void fmx_server_disconnect(fmx_server *server, uint64_t peer) {
    fmx_peer *found = find_peer(server, peer);
    if (found != NULL) {
        fmx_core_close(&found->core);
    }
}

size_t fmx_server_peer_count(const fmx_server *server) {
    return server->peer_count;
}

uint64_t fmx_server_peer_at(const fmx_server *server, size_t index) {
    if (index >= server->peer_count) {
        return 0;
    }
    return server->peers[index].id;
}

bool fmx_server_peer_ready(const fmx_server *server, uint64_t peer) {
    fmx_peer *found = find_peer((fmx_server *)server, peer);
    return found != NULL && fmx_session_ready(found->core.session);
}
