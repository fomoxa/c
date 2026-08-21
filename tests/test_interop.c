#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "cyc_test.h"

#include "cyclone/net.h"

#if defined(_WIN32)
#include <windows.h>
static void nap(void) {
    Sleep(1);
}
#else
#include <time.h>
static void nap(void) {
    struct timespec pause;
    pause.tv_sec = 0;
    pause.tv_nsec = 1000000L;
    nanosleep(&pause, NULL);
}
#endif

static const uint64_t STATE_PREFIXES[2] = {10, 70};
static const uint64_t INPUT_PREFIXES[1] = {90};
static cyc_message_schema MESSAGES[2];
static cyc_schema SCHEMA;

static void build_schema(void) {
    MESSAGES[0].id = 7;
    MESSAGES[0].fingerprint = 70;
    MESSAGES[0].prefixes = STATE_PREFIXES;
    MESSAGES[0].prefix_count = 2;
    MESSAGES[1].id = 9;
    MESSAGES[1].fingerprint = 90;
    MESSAGES[1].prefixes = INPUT_PREFIXES;
    MESSAGES[1].prefix_count = 1;
    SCHEMA.fingerprint = 0xC1C10E00;
    SCHEMA.messages = MESSAGES;
    SCHEMA.message_count = 2;
}

typedef struct trace {
    bool client_ready;
    bool server_ready;
    bool client_got;
    bool server_got;
    bool server_disconnected;
    uint64_t peer;
} trace;

static void pump(cyc_connection *connection, cyc_server *server, trace *seen, int rounds) {
    int round;

    for (round = 0; round < rounds; ++round) {
        const cyc_event *events;
        size_t count;
        size_t index;
        uint64_t now = cyc_now_ms();

        count = cyc_connection_tick(connection, now);
        events = cyc_connection_events(connection, &count);
        for (index = 0; index < count; ++index) {
            if (events[index].kind == CYC_EVENT_READY) {
                seen->client_ready = true;
            } else if (events[index].kind == CYC_EVENT_MESSAGE) {
                seen->client_got = events[index].message_id == 7 &&
                                   events[index].payload_len == 4 &&
                                   memcmp(events[index].payload, "down", 4) == 0;
            } else if (events[index].kind == CYC_EVENT_HANDSHAKE_FAILED) {
                CYC_CHECK(false);
            }
        }

        count = cyc_server_tick(server, now);
        events = cyc_server_events(server, &count);
        for (index = 0; index < count; ++index) {
            if (events[index].kind == CYC_EVENT_READY) {
                seen->server_ready = true;
                seen->peer = events[index].peer;
            } else if (events[index].kind == CYC_EVENT_MESSAGE) {
                seen->server_got = events[index].message_id == 9 &&
                                   events[index].payload_len == 2 &&
                                   memcmp(events[index].payload, "up", 2) == 0;
            } else if (events[index].kind == CYC_EVENT_DISCONNECTED) {
                seen->server_disconnected = true;
            } else if (events[index].kind == CYC_EVENT_HANDSHAKE_FAILED) {
                CYC_CHECK(false);
            }
        }
        nap();
    }
}

static void exercise(bool datagram) {
    cyc_listener listener;
    cyc_transport transport;
    cyc_server *server;
    cyc_connection *connection;
    trace seen;
    uint16_t port;

    memset(&seen, 0, sizeof(seen));

    if (datagram) {
        CYC_CHECK(cyc_udp_listen("127.0.0.1", 0, &listener) == CYC_OK);
        port = cyc_udp_listener_port(&listener);
    } else {
        CYC_CHECK(cyc_tcp_listen("127.0.0.1", 0, &listener) == CYC_OK);
        port = cyc_tcp_listener_port(&listener);
    }
    CYC_CHECK(port != 0);

    server = cyc_server_create(listener, &SCHEMA, NULL);
    CYC_CHECK(server != NULL);

    if (datagram) {
        CYC_CHECK(cyc_udp_connect("127.0.0.1", port, &transport) == CYC_OK);
    } else {
        CYC_CHECK(cyc_tcp_connect("127.0.0.1", port, &transport) == CYC_OK);
    }
    connection = cyc_connection_create(transport, &SCHEMA, NULL, cyc_now_ms());
    CYC_CHECK(connection != NULL);

    pump(connection, server, &seen, 60);
    CYC_CHECK(seen.client_ready);
    CYC_CHECK(seen.server_ready);

    CYC_CHECK(cyc_server_send(server, seen.peer, 7, (const uint8_t *)"down", 4) == CYC_OK);
    CYC_CHECK(cyc_connection_send(connection, 9, (const uint8_t *)"up", 2) == CYC_OK);

    pump(connection, server, &seen, 60);
    CYC_CHECK(seen.client_got);
    CYC_CHECK(seen.server_got);

    if (!datagram) {
        cyc_connection_close(connection);
        pump(connection, server, &seen, 60);
        CYC_CHECK(seen.server_disconnected);
        CYC_CHECK(cyc_server_peer_count(server) == 0);
    }

    cyc_connection_destroy(connection);
    cyc_server_destroy(server);
}

static void tcp_talks_both_ways(void) {
    exercise(false);
}

static void udp_talks_both_ways(void) {
    exercise(true);
}

static void a_schema_conflict_is_refused_over_a_real_socket(void) {
    const uint64_t other_prefixes[2] = {11, 71};
    cyc_message_schema other[2];
    cyc_schema conflicting;
    cyc_listener listener;
    cyc_transport transport;
    cyc_server *server;
    cyc_connection *connection;
    uint16_t port;
    bool client_failed = false;
    bool server_failed = false;
    int round;

    other[0].id = 7;
    other[0].fingerprint = 71;
    other[0].prefixes = other_prefixes;
    other[0].prefix_count = 2;
    other[1] = MESSAGES[1];
    conflicting.fingerprint = 0xDEADBEEF;
    conflicting.messages = other;
    conflicting.message_count = 2;

    CYC_CHECK(cyc_tcp_listen("127.0.0.1", 0, &listener) == CYC_OK);
    port = cyc_tcp_listener_port(&listener);
    server = cyc_server_create(listener, &SCHEMA, NULL);
    CYC_CHECK(cyc_tcp_connect("127.0.0.1", port, &transport) == CYC_OK);
    connection = cyc_connection_create(transport, &conflicting, NULL, cyc_now_ms());

    for (round = 0; round < 60; ++round) {
        const cyc_event *events;
        size_t count;
        size_t index;
        uint64_t now = cyc_now_ms();

        count = cyc_connection_tick(connection, now);
        events = cyc_connection_events(connection, &count);
        for (index = 0; index < count; ++index) {
            if (events[index].kind == CYC_EVENT_HANDSHAKE_FAILED) {
                client_failed = events[index].reason == CYC_FAIL_SCHEMA_CONFLICT;
            }
            CYC_CHECK(events[index].kind != CYC_EVENT_READY);
        }

        count = cyc_server_tick(server, now);
        events = cyc_server_events(server, &count);
        for (index = 0; index < count; ++index) {
            if (events[index].kind == CYC_EVENT_HANDSHAKE_FAILED) {
                server_failed = events[index].reason == CYC_FAIL_SCHEMA_CONFLICT;
            }
        }
        nap();
    }

    CYC_CHECK(client_failed);
    CYC_CHECK(server_failed);
    CYC_CHECK(cyc_server_peer_count(server) == 0);
    cyc_connection_destroy(connection);
    cyc_server_destroy(server);
}

static void a_peer_that_appends_a_field_still_connects(void) {
    const uint64_t extended[3] = {10, 70, 71};
    cyc_message_schema longer[2];
    cyc_schema schema;
    cyc_listener listener;
    cyc_transport transport;
    cyc_server *server;
    cyc_connection *connection;
    trace seen;
    uint16_t port;

    memset(&seen, 0, sizeof(seen));
    longer[0].id = 7;
    longer[0].fingerprint = 71;
    longer[0].prefixes = extended;
    longer[0].prefix_count = 3;
    longer[1] = MESSAGES[1];
    schema.fingerprint = 0x0BEEF000;
    schema.messages = longer;
    schema.message_count = 2;

    CYC_CHECK(cyc_tcp_listen("127.0.0.1", 0, &listener) == CYC_OK);
    port = cyc_tcp_listener_port(&listener);
    server = cyc_server_create(listener, &SCHEMA, NULL);
    CYC_CHECK(cyc_tcp_connect("127.0.0.1", port, &transport) == CYC_OK);
    connection = cyc_connection_create(transport, &schema, NULL, cyc_now_ms());

    pump(connection, server, &seen, 60);
    CYC_CHECK(seen.client_ready);
    CYC_CHECK(seen.server_ready);

    cyc_connection_destroy(connection);
    cyc_server_destroy(server);
}

int main(void) {
    build_schema();
    CYC_RUN(tcp_talks_both_ways);
    CYC_RUN(udp_talks_both_ways);
    CYC_RUN(a_schema_conflict_is_refused_over_a_real_socket);
    CYC_RUN(a_peer_that_appends_a_field_still_connects);
    CYC_DONE();
}
