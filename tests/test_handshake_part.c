#include "fmx_test.h"

#include "fomoxa/connection.h"
#include "fomoxa/frame.h"
#include "fomoxa/handshake.h"
#include "fomoxa/server.h"

typedef struct packet {
    uint8_t *bytes;
    size_t len;
} packet;

typedef struct packets {
    packet *items;
    size_t count;
    size_t cap;
} packets;

typedef struct fake {
    fmx_transport_kind kind;
    packets sent;
    packets incoming;
    size_t head;
    bool blocked;
} fake;

static fake FAKE;

static void packets_push(packets *list, const uint8_t *bytes, size_t len) {
    if (list->count == list->cap) {
        list->cap = list->cap == 0 ? 64 : list->cap * 2;
        list->items = (packet *)realloc(list->items, list->cap * sizeof(packet));
    }
    list->items[list->count].bytes = (uint8_t *)malloc(len > 0 ? len : 1);
    memcpy(list->items[list->count].bytes, bytes, len);
    list->items[list->count].len = len;
    list->count += 1;
}

static void packets_free(packets *list) {
    size_t index;
    for (index = 0; index < list->count; ++index) {
        free(list->items[index].bytes);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static void packets_clear(packets *list) {
    size_t index;
    for (index = 0; index < list->count; ++index) {
        free(list->items[index].bytes);
    }
    list->count = 0;
}

static void fake_reset(fmx_transport_kind kind) {
    packets_free(&FAKE.sent);
    packets_free(&FAKE.incoming);
    memset(&FAKE, 0, sizeof(FAKE));
    FAKE.kind = kind;
}

static void fake_deliver(const uint8_t *bytes, size_t len) {
    packets_push(&FAKE.incoming, bytes, len);
}

static void fake_deliver_all(const packets *list) {
    size_t index;
    for (index = 0; index < list->count; ++index) {
        fake_deliver(list->items[index].bytes, list->items[index].len);
    }
}

static fmx_transport_kind fake_kind(const fmx_transport *transport) {
    (void)transport;
    return FAKE.kind;
}

static fmx_send_result fake_send(fmx_transport *transport, const uint8_t *bytes, size_t len,
                                 size_t *accepted) {
    (void)transport;
    *accepted = 0;
    if (FAKE.blocked) {
        return FMX_SEND_WOULD_BLOCK;
    }
    packets_push(&FAKE.sent, bytes, len);
    *accepted = len;
    return FMX_SEND_SENT;
}

static fmx_recv_result fake_recv(fmx_transport *transport, uint8_t *buffer, size_t cap,
                                 size_t *received, size_t *needed) {
    packet *front;
    (void)transport;
    *received = 0;
    *needed = 0;
    if (FAKE.head == FAKE.incoming.count) {
        return FMX_RECV_WOULD_BLOCK;
    }
    front = &FAKE.incoming.items[FAKE.head];
    if (front->len > cap) {
        *needed = front->len;
        return FMX_RECV_NEED_CAPACITY;
    }
    memcpy(buffer, front->bytes, front->len);
    *received = front->len;
    FAKE.head += 1;
    return FMX_RECV_RECEIVED;
}

static void fake_close(fmx_transport *transport) {
    (void)transport;
}

static const fmx_transport_vtable FAKE_VTABLE = {fake_kind, fake_send, fake_recv, fake_close,
                                                 fake_close};

static fmx_transport fake_transport(void) {
    fmx_transport transport;
    transport.vtable = &FAKE_VTABLE;
    transport.state = &FAKE;
    return transport;
}

static bool LISTENER_WAITING;

static fmx_accept_result listener_accept(fmx_listener *listener, fmx_transport *out) {
    (void)listener;
    if (!LISTENER_WAITING) {
        return FMX_ACCEPT_PENDING;
    }
    LISTENER_WAITING = false;
    *out = fake_transport();
    return FMX_ACCEPT_ACCEPTED;
}

static void listener_close(fmx_listener *listener) {
    (void)listener;
}

static const fmx_listener_vtable LISTENER_VTABLE = {listener_accept, listener_close};

#define MAX_TEST_MESSAGES 5000

static fmx_message_schema MESSAGES[MAX_TEST_MESSAGES];
static uint64_t PREFIXES[MAX_TEST_MESSAGES][2];
static fmx_schema SCHEMA;

static const fmx_schema *many(size_t count) {
    size_t index;
    for (index = 0; index < count; ++index) {
        uint32_t id = (uint32_t)(index + 1);
        PREFIXES[index][0] = id;
        MESSAGES[index].id = id;
        MESSAGES[index].fingerprint = id;
        MESSAGES[index].prefixes = PREFIXES[index];
        MESSAGES[index].prefix_count = 1;
    }
    SCHEMA.fingerprint = 0xC0FFEE00u + count;
    SCHEMA.messages = MESSAGES;
    SCHEMA.message_count = count;
    return &SCHEMA;
}

static size_t hello_len(size_t count) {
    return 16 + 14 * count;
}

static size_t parts_for(size_t content) {
    return (content + FMX_PART_CONTENT_LEN - 1) / FMX_PART_CONTENT_LEN;
}

static size_t u16_at(const uint8_t *bytes) {
    return (size_t)bytes[0] | ((size_t)bytes[1] << 8);
}

static size_t u32_at(const uint8_t *bytes) {
    return (size_t)bytes[0] | ((size_t)bytes[1] << 8) | ((size_t)bytes[2] << 16) |
           ((size_t)bytes[3] << 24);
}

static size_t part(size_t index, size_t count, const uint8_t *content, size_t len, uint8_t *out) {
    out[0] = FMX_FRAME_HANDSHAKE_PART;
    out[1] = (uint8_t)(index & 0xFFu);
    out[2] = (uint8_t)(index >> 8);
    out[3] = (uint8_t)(count & 0xFFu);
    out[4] = (uint8_t)(count >> 8);
    out[5] = (uint8_t)(len & 0xFFu);
    out[6] = (uint8_t)((len >> 8) & 0xFFu);
    out[7] = (uint8_t)((len >> 16) & 0xFFu);
    out[8] = (uint8_t)((len >> 24) & 0xFFu);
    if (len > 0) {
        memcpy(out + FMX_PART_HEADER_LEN, content, len);
    }
    return FMX_PART_HEADER_LEN + len;
}

static void deliver_part(size_t index, size_t count, size_t len) {
    static const uint8_t ZEROS[1024];
    uint8_t bytes[1024 + 9];
    fake_deliver(bytes, part(index, count, ZEROS, len, bytes));
}

static void hello_parts(const fmx_schema *schema, packets *out) {
    fmx_connection *connection;
    size_t index;
    fake_reset(FMX_TRANSPORT_MESSAGE);
    connection = fmx_connection_create(fake_transport(), schema, NULL, 1000);
    for (index = 0; index < FAKE.sent.count; ++index) {
        packets_push(out, FAKE.sent.items[index].bytes, FAKE.sent.items[index].len);
    }
    fmx_connection_destroy(connection);
}

static fmx_connection *client(const fmx_schema *schema, fmx_transport_kind kind) {
    fmx_connection *connection;
    size_t count;
    fake_reset(kind);
    connection = fmx_connection_create(fake_transport(), schema, NULL, 1000);
    count = fmx_connection_tick(connection, 1000);
    FMX_CHECK(count == 1);
    return connection;
}

static fmx_server *server_with(const fmx_schema *schema, const fmx_config *config,
                               fmx_transport_kind kind) {
    fmx_listener listener;
    fmx_server *server;
    const fmx_event *events;
    size_t count = 0;

    fake_reset(kind);
    LISTENER_WAITING = true;
    listener.vtable = &LISTENER_VTABLE;
    listener.state = NULL;
    server = fmx_server_create(listener, schema, config);
    FMX_CHECK(fmx_server_tick(server, 1000) == 1);
    events = fmx_server_events(server, &count);
    FMX_CHECK(count == 1 && events[0].kind == FMX_EVENT_CONNECTED);
    return server;
}

static fmx_server *server(const fmx_schema *schema) {
    return server_with(schema, NULL, FMX_TRANSPORT_MESSAGE);
}

static bool only_event(const fmx_event *events, size_t count, fmx_event_kind kind, int reason) {
    return count == 1 && events[0].kind == kind && events[0].reason == reason;
}

static bool server_ticks_to(fmx_server *srv, uint64_t now, fmx_event_kind kind, int reason) {
    size_t count = 0;
    const fmx_event *events;
    fmx_server_tick(srv, now);
    events = fmx_server_events(srv, &count);
    return only_event(events, count, kind, reason);
}

static size_t server_tick_count(fmx_server *srv, uint64_t now) {
    size_t count = 0;
    fmx_server_tick(srv, now);
    (void)fmx_server_events(srv, &count);
    return count;
}

static int last_verdict(void) {
    const packet *last;
    if (FAKE.sent.count == 0) {
        return -1;
    }
    last = &FAKE.sent.items[FAKE.sent.count - 1];
    if (last->len != 6 || last->bytes[0] != FMX_FRAME_HANDSHAKE || u32_at(last->bytes + 1) != 1) {
        return -2;
    }
    return last->bytes[5];
}

static void expect_refused(fmx_server *srv) {
    FMX_CHECK(server_ticks_to(srv, 1000, FMX_EVENT_HANDSHAKE_FAILED, FMX_FAIL_MALFORMED_HELLO));
    FMX_CHECK(last_verdict() == FMX_VERDICT_MALFORMED_HELLO);
    fmx_server_destroy(srv);
}

static void expect_accepted(fmx_server *srv) {
    FMX_CHECK(server_ticks_to(srv, 1000, FMX_EVENT_READY, 0));
    FMX_CHECK(last_verdict() == FMX_VERDICT_ACCEPT);
    fmx_server_destroy(srv);
}

static void a_hello_of_62_messages_leaves_as_one_handshake_frame(void) {
    packets sent = {0};
    hello_parts(many(62), &sent);
    FMX_CHECK(sent.count == 1);
    FMX_CHECK(sent.items[0].bytes[0] == FMX_FRAME_HANDSHAKE);
    FMX_CHECK(sent.items[0].len == FMX_HANDSHAKE_HEADER_LEN + hello_len(62));
    FMX_CHECK(sent.items[0].len <= FMX_PACKET_HANDSHAKE_BUDGET);
    packets_free(&sent);
}

static void a_hello_of_63_messages_on_a_packet_transport_leaves_in_two_parts(void) {
    packets sent = {0};
    uint8_t hello[16 + 14 * 63];
    const fmx_schema *schema = many(63);
    const uint8_t *first;
    const uint8_t *second;

    hello_parts(schema, &sent);
    FMX_CHECK(sent.count == 2);
    if (sent.count == 2) {
        first = sent.items[0].bytes;
        second = sent.items[1].bytes;
        FMX_CHECK(first[0] == FMX_FRAME_HANDSHAKE_PART && second[0] == FMX_FRAME_HANDSHAKE_PART);
        FMX_CHECK(u16_at(first + 1) == 0 && u16_at(second + 1) == 1);
        FMX_CHECK(u16_at(first + 3) == 2 && u16_at(second + 3) == 2);
        FMX_CHECK(u32_at(first + 5) == 891 && u32_at(second + 5) == 7);
        FMX_CHECK(fmx_hello_encode(schema, hello, sizeof(hello)) == sizeof(hello));
        FMX_CHECK(memcmp(first + 9, hello, 891) == 0);
        FMX_CHECK(memcmp(second + 9, hello + 891, 7) == 0);
    }
    packets_free(&sent);
}

static void every_part_is_at_most_900_bytes_and_only_the_last_is_short(void) {
    packets sent = {0};
    size_t index;
    hello_parts(many(500), &sent);
    FMX_CHECK(sent.count == parts_for(hello_len(500)));
    for (index = 0; index < sent.count; ++index) {
        FMX_CHECK(sent.items[index].len <= FMX_PACKET_HANDSHAKE_BUDGET);
        if (index < sent.count - 1) {
            FMX_CHECK(sent.items[index].len == FMX_PACKET_HANDSHAKE_BUDGET);
        }
    }
    packets_free(&sent);
}

static void a_stream_transport_sends_a_large_hello_as_one_handshake_frame(void) {
    fmx_connection *connection = client(many(200), FMX_TRANSPORT_STREAM);
    FMX_CHECK(FAKE.sent.count == 1);
    FMX_CHECK(FAKE.sent.items[0].bytes[0] == FMX_FRAME_HANDSHAKE);
    FMX_CHECK(FAKE.sent.items[0].len == FMX_HANDSHAKE_HEADER_LEN + hello_len(200));
    fmx_connection_destroy(connection);
}

static void a_server_and_client_finish_a_hello_sent_in_parts(void) {
    packets hello = {0};
    packets verdict = {0};
    const fmx_schema *schema = many(200);
    fmx_connection *connection;
    fmx_server *srv;
    const fmx_event *events;
    size_t count = 0;

    hello_parts(schema, &hello);
    FMX_CHECK(hello.count == parts_for(hello_len(200)));
    srv = server(schema);
    fake_deliver_all(&hello);
    FMX_CHECK(server_ticks_to(srv, 1000, FMX_EVENT_READY, 0));
    packets_push(&verdict, FAKE.sent.items[0].bytes, FAKE.sent.items[0].len);
    fmx_server_destroy(srv);

    connection = client(schema, FMX_TRANSPORT_MESSAGE);
    fake_deliver_all(&verdict);
    fmx_connection_tick(connection, 1000);
    events = fmx_connection_events(connection, &count);
    FMX_CHECK(only_event(events, count, FMX_EVENT_READY, 0));
    fmx_connection_destroy(connection);
    packets_free(&hello);
    packets_free(&verdict);
}

static void parts_that_arrive_in_reverse_order_are_assembled(void) {
    packets hello = {0};
    const fmx_schema *schema = many(200);
    fmx_server *srv;
    size_t index;

    hello_parts(schema, &hello);
    srv = server(schema);
    for (index = hello.count; index > 0; --index) {
        fake_deliver(hello.items[index - 1].bytes, hello.items[index - 1].len);
    }
    expect_accepted(srv);
    packets_free(&hello);
}

static void a_repeated_identical_part_is_ignored(void) {
    packets hello = {0};
    const fmx_schema *schema = many(200);
    fmx_server *srv;

    hello_parts(schema, &hello);
    srv = server(schema);
    fake_deliver(hello.items[0].bytes, hello.items[0].len);
    fake_deliver_all(&hello);
    expect_accepted(srv);
    packets_free(&hello);
}

static void a_part_that_differs_from_an_earlier_copy_is_refused(void) {
    packets hello = {0};
    const fmx_schema *schema = many(200);
    fmx_server *srv;

    hello_parts(schema, &hello);
    srv = server(schema);
    fake_deliver(hello.items[0].bytes, hello.items[0].len);
    hello.items[0].bytes[20] ^= 0xFFu;
    fake_deliver(hello.items[0].bytes, hello.items[0].len);
    expect_refused(srv);
    packets_free(&hello);
}

static void a_part_whose_count_changes_is_refused(void) {
    fmx_server *srv = server(many(200));
    deliver_part(0, 3, 891);
    deliver_part(1, 4, 891);
    expect_refused(srv);
}

static void a_short_part_before_the_last_is_refused(void) {
    fmx_server *srv = server(many(200));
    deliver_part(0, 3, 890);
    expect_refused(srv);
}

static void an_empty_last_part_is_refused(void) {
    fmx_server *srv = server(many(200));
    deliver_part(0, 2, 891);
    deliver_part(1, 2, 0);
    expect_refused(srv);
}

static void a_part_count_of_one_is_refused(void) {
    uint8_t hello[16 + 14 * 62];
    uint8_t bytes[sizeof(hello) + 9];
    const fmx_schema *schema = many(62);
    fmx_server *srv = server(schema);
    FMX_CHECK(fmx_hello_encode(schema, hello, sizeof(hello)) == sizeof(hello));
    fake_deliver(bytes, part(0, 1, hello, sizeof(hello), bytes));
    expect_refused(srv);
}

static void a_part_count_past_the_ceiling_is_refused(void) {
    fmx_server *srv = server(many(200));
    deliver_part(0, FMX_MAX_HANDSHAKE_PARTS + 1, 891);
    expect_refused(srv);
}

static void a_part_index_past_the_count_is_refused(void) {
    fmx_server *srv = server(many(200));
    deliver_part(2, 2, 891);
    expect_refused(srv);
}

static void a_part_carrying_more_than_891_bytes_is_dropped_as_a_broken_packet(void) {
    packets hello = {0};
    const fmx_schema *schema = many(200);
    fmx_server *srv;

    hello_parts(schema, &hello);
    srv = server(schema);
    deliver_part(0, 2, 892);
    FMX_CHECK(server_tick_count(srv, 1000) == 0);
    FMX_CHECK(FAKE.sent.count == 0);
    fake_deliver_all(&hello);
    expect_accepted(srv);
    packets_free(&hello);
}

static void a_handshake_frame_while_parts_are_pending_is_refused(void) {
    packets hello = {0};
    uint8_t body[16 + 14 * 200];
    uint8_t frame[sizeof(body) + 5];
    size_t written = 0;
    const fmx_schema *schema = many(200);
    fmx_server *srv;

    hello_parts(schema, &hello);
    FMX_CHECK(fmx_hello_encode(schema, body, sizeof(body)) == sizeof(body));
    FMX_CHECK(fmx_frame_encode_handshake(body, sizeof(body), frame, sizeof(frame), &written) ==
              FMX_FRAME_OK);
    srv = server(schema);
    fake_deliver(hello.items[0].bytes, hello.items[0].len);
    fake_deliver(frame, written);
    expect_refused(srv);
    packets_free(&hello);
}

static void content_assembled_past_1_mib_is_refused(void) {
    const size_t entries = 74900;
    size_t len = hello_len(entries);
    uint8_t *hello = (uint8_t *)calloc(len, 1);
    uint8_t bytes[FMX_PACKET_HANDSHAKE_BUDGET];
    const fmx_schema *schema = many(1);
    size_t count = parts_for(len);
    size_t index;
    fmx_config config;
    fmx_server *srv;

    FMX_CHECK(len > FMX_MAX_HANDSHAKE_PAYLOAD);
    FMX_CHECK(count <= FMX_MAX_HANDSHAKE_PARTS);
    hello[0] = 2;
    hello[12] = (uint8_t)(entries & 0xFFu);
    hello[13] = (uint8_t)((entries >> 8) & 0xFFu);
    hello[14] = (uint8_t)((entries >> 16) & 0xFFu);

    fmx_config_defaults(&config);
    config.max_frames_per_tick = 2048;
    srv = server_with(schema, &config, FMX_TRANSPORT_MESSAGE);
    for (index = 0; index < count; ++index) {
        size_t offset = index * FMX_PART_CONTENT_LEN;
        size_t chunk = len - offset < FMX_PART_CONTENT_LEN ? len - offset : FMX_PART_CONTENT_LEN;
        fake_deliver(bytes, part(index, count, hello + offset, chunk, bytes));
    }
    expect_refused(srv);
    free(hello);
}

static void a_part_after_the_handshake_is_dropped_and_is_not_activity(void) {
    uint8_t body[16 + 14];
    uint8_t frame[sizeof(body) + 5];
    size_t written = 0;
    const fmx_schema *schema = many(1);
    fmx_config config;
    fmx_server *srv;

    fmx_config_defaults(&config);
    FMX_CHECK(fmx_hello_encode(schema, body, sizeof(body)) == sizeof(body));
    fmx_frame_encode_handshake(body, sizeof(body), frame, sizeof(frame), &written);
    srv = server(schema);
    fake_deliver(frame, written);
    FMX_CHECK(server_ticks_to(srv, 1000, FMX_EVENT_READY, 0));
    packets_clear(&FAKE.sent);

    deliver_part(0, 2, 891);
    FMX_CHECK(server_tick_count(srv, 1000 + config.heartbeat_interval_ms - 1) == 0);
    FMX_CHECK(FAKE.sent.count == 0);
    FMX_CHECK(fmx_server_peer_ready(srv, fmx_server_peer_at(srv, 0)));

    fmx_server_tick(srv, 1000 + config.heartbeat_interval_ms);
    FMX_CHECK(FAKE.sent.count == 1 && FAKE.sent.items[0].len == 1 &&
              FAKE.sent.items[0].bytes[0] == FMX_FRAME_PROBE);
    fmx_server_destroy(srv);
}

static void a_part_on_a_byte_stream_is_a_fatal_framing_error(void) {
    fmx_server *srv = server_with(many(1), NULL, FMX_TRANSPORT_STREAM);
    deliver_part(0, 2, 891);
    FMX_CHECK(server_ticks_to(srv, 1000, FMX_EVENT_DISCONNECTED, FMX_DISCONNECT_TRANSPORT_ERROR));
    fmx_server_destroy(srv);
}

static void the_client_fails_on_a_broken_answer_sent_in_parts(void) {
    fmx_connection *connection = client(many(1), FMX_TRANSPORT_MESSAGE);
    const fmx_event *events;
    size_t count = 0;
    deliver_part(0, 2, 891);
    deliver_part(1, 2, 0);
    fmx_connection_tick(connection, 1000);
    events = fmx_connection_events(connection, &count);
    FMX_CHECK(only_event(events, count, FMX_EVENT_HANDSHAKE_FAILED, FMX_FAIL_MALFORMED_PEER));
    fmx_connection_destroy(connection);
}

static fmx_message_schema CLIENT_MESSAGES[160];
static uint64_t CLIENT_PREFIXES[160][2];

static void a_query_and_its_reply_that_exceed_the_budget_both_travel_in_parts(void) {
    fmx_schema client_schema;
    const fmx_schema *server_schema;
    packets hello = {0};
    packets query = {0};
    packets reply = {0};
    fmx_connection *connection;
    fmx_server *srv;
    size_t index;

    for (index = 0; index < 160; ++index) {
        uint32_t id = (uint32_t)(index + 1);
        CLIENT_PREFIXES[index][0] = (uint64_t)id * 10;
        CLIENT_PREFIXES[index][1] = (uint64_t)id * 10 + 1;
        CLIENT_MESSAGES[index].id = id;
        CLIENT_MESSAGES[index].fingerprint = (uint64_t)id * 10 + 1;
        CLIENT_MESSAGES[index].prefixes = CLIENT_PREFIXES[index];
        CLIENT_MESSAGES[index].prefix_count = 2;
    }
    client_schema.fingerprint = 1;
    client_schema.messages = CLIENT_MESSAGES;
    client_schema.message_count = 160;
    server_schema = many(160);
    for (index = 0; index < 160; ++index) {
        PREFIXES[index][0] = (uint64_t)(index + 1) * 10;
        MESSAGES[index].fingerprint = (uint64_t)(index + 1) * 10;
    }
    SCHEMA.fingerprint = 2;

    hello_parts(&client_schema, &hello);
    srv = server(server_schema);
    fake_deliver_all(&hello);
    FMX_CHECK(server_tick_count(srv, 1000) == 0);
    for (index = 0; index < FAKE.sent.count; ++index) {
        packets_push(&query, FAKE.sent.items[index].bytes, FAKE.sent.items[index].len);
    }
    FMX_CHECK(query.count == 2);
    FMX_CHECK(query.count > 0 && query.items[0].bytes[0] == FMX_FRAME_HANDSHAKE_PART);

    {
        fake saved = FAKE;
        memset(&FAKE, 0, sizeof(FAKE));
        connection = client(&client_schema, FMX_TRANSPORT_MESSAGE);
        packets_clear(&FAKE.sent);
        fake_deliver_all(&query);
        fmx_connection_tick(connection, 1000);
        for (index = 0; index < FAKE.sent.count; ++index) {
            packets_push(&reply, FAKE.sent.items[index].bytes, FAKE.sent.items[index].len);
        }
        fmx_connection_destroy(connection);
        packets_free(&FAKE.sent);
        packets_free(&FAKE.incoming);
        FAKE = saved;
    }
    FMX_CHECK(reply.count == 3);
    FMX_CHECK(reply.count > 0 && reply.items[0].bytes[0] == FMX_FRAME_HANDSHAKE_PART);

    fake_deliver_all(&reply);
    expect_accepted(srv);
    packets_free(&hello);
    packets_free(&query);
    packets_free(&reply);
}

static void parts_held_back_by_a_blocked_socket_leave_later_as_separate_packets(void) {
    fmx_connection *connection;
    size_t index;

    fake_reset(FMX_TRANSPORT_MESSAGE);
    FAKE.blocked = true;
    connection = fmx_connection_create(fake_transport(), many(200), NULL, 1000);
    fmx_connection_tick(connection, 1000);
    FMX_CHECK(FAKE.sent.count == 0);
    FMX_CHECK(fmx_connection_congested(connection));

    FAKE.blocked = false;
    fmx_connection_tick(connection, 1000);
    FMX_CHECK(FAKE.sent.count == parts_for(hello_len(200)));
    for (index = 0; index < FAKE.sent.count; ++index) {
        FMX_CHECK(FAKE.sent.items[index].bytes[0] == FMX_FRAME_HANDSHAKE_PART);
        FMX_CHECK(FAKE.sent.items[index].len ==
                  FMX_PART_HEADER_LEN + u32_at(FAKE.sent.items[index].bytes + 5));
    }
    FMX_CHECK(!fmx_connection_congested(connection));
    fmx_connection_destroy(connection);
}

static void a_hello_larger_than_64_kib_held_by_a_blocked_socket_does_not_hit_the_ceiling(void) {
    fmx_connection *connection;
    const fmx_event *events;
    size_t count = 0;

    FMX_CHECK(hello_len(5000) > 64u * 1024u);
    fake_reset(FMX_TRANSPORT_MESSAGE);
    FAKE.blocked = true;
    connection = fmx_connection_create(fake_transport(), many(5000), NULL, 1000);
    fmx_connection_tick(connection, 1000);
    events = fmx_connection_events(connection, &count);
    FMX_CHECK(only_event(events, count, FMX_EVENT_CONNECTED, 0));
    FMX_CHECK(fmx_connection_state(connection) == FMX_STATE_HANDSHAKING);

    FAKE.blocked = false;
    fmx_connection_tick(connection, 1000);
    FMX_CHECK(FAKE.sent.count == parts_for(hello_len(5000)));
    fmx_connection_destroy(connection);
}

static void a_pending_part_counts_as_activity_for_the_server(void) {
    packets hello = {0};
    const fmx_schema *schema = many(200);
    fmx_config config;
    fmx_server *srv;
    uint64_t almost;

    fmx_config_defaults(&config);
    almost = config.handshake_timeout_ms - 1;
    hello_parts(schema, &hello);
    srv = server(schema);
    fake_deliver(hello.items[0].bytes, hello.items[0].len);
    fmx_server_tick(srv, 1000 + almost);
    fmx_server_tick(srv, 1000 + almost + almost);
    FMX_CHECK(FAKE.sent.count == 0);
    fmx_server_destroy(srv);
    packets_free(&hello);
}

static void a_probe_queued_behind_a_blocked_message_leaves_as_its_own_packet(void) {
    uint8_t verdict[6];
    uint8_t data[16];
    size_t written = 0;
    fmx_config config;
    fmx_connection *connection = client(many(1), FMX_TRANSPORT_MESSAGE);

    fmx_config_defaults(&config);
    fmx_frame_encode_handshake((const uint8_t *)"\0", 1, verdict, sizeof(verdict), &written);
    fake_deliver(verdict, written);
    fmx_connection_tick(connection, 1000);
    FMX_CHECK(fmx_connection_ready(connection));
    packets_clear(&FAKE.sent);

    FAKE.blocked = true;
    FMX_CHECK(fmx_connection_send(connection, 1, (const uint8_t *)"held", 4) == FMX_OK);
    fmx_connection_tick(connection, 1000 + config.heartbeat_interval_ms);
    FAKE.blocked = false;
    fmx_connection_tick(connection, 1000 + config.heartbeat_interval_ms);

    fmx_frame_encode_data(1, (const uint8_t *)"held", 4, data, sizeof(data), &written);
    FMX_CHECK(FAKE.sent.count == 2);
    if (FAKE.sent.count == 2) {
        FMX_CHECK(FAKE.sent.items[0].len == written);
        FMX_CHECK(memcmp(FAKE.sent.items[0].bytes, data, written) == 0);
        FMX_CHECK(FAKE.sent.items[1].len == 1 && FAKE.sent.items[1].bytes[0] == FMX_FRAME_PROBE);
    }
    fmx_connection_destroy(connection);
}

static void a_part_type_is_still_unknown_to_the_frame_decoder(void) {
    uint8_t bytes[900];
    size_t len = part(0, 2, (const uint8_t *)"x", 1, bytes);
    fmx_frame frame;
    FMX_CHECK(fmx_frame_decode_packet(bytes, len, 0, &frame) == FMX_FRAME_UNKNOWN_TYPE);
    FMX_CHECK(strstr(fmx_frame_error_name(FMX_FRAME_UNKNOWN_TYPE), "packet transport") != NULL);
}

int main(void) {
    FMX_RUN(a_hello_of_62_messages_leaves_as_one_handshake_frame);
    FMX_RUN(a_hello_of_63_messages_on_a_packet_transport_leaves_in_two_parts);
    FMX_RUN(every_part_is_at_most_900_bytes_and_only_the_last_is_short);
    FMX_RUN(a_stream_transport_sends_a_large_hello_as_one_handshake_frame);
    FMX_RUN(a_server_and_client_finish_a_hello_sent_in_parts);
    FMX_RUN(parts_that_arrive_in_reverse_order_are_assembled);
    FMX_RUN(a_repeated_identical_part_is_ignored);
    FMX_RUN(a_part_that_differs_from_an_earlier_copy_is_refused);
    FMX_RUN(a_part_whose_count_changes_is_refused);
    FMX_RUN(a_short_part_before_the_last_is_refused);
    FMX_RUN(an_empty_last_part_is_refused);
    FMX_RUN(a_part_count_of_one_is_refused);
    FMX_RUN(a_part_count_past_the_ceiling_is_refused);
    FMX_RUN(a_part_index_past_the_count_is_refused);
    FMX_RUN(a_part_carrying_more_than_891_bytes_is_dropped_as_a_broken_packet);
    FMX_RUN(a_handshake_frame_while_parts_are_pending_is_refused);
    FMX_RUN(content_assembled_past_1_mib_is_refused);
    FMX_RUN(a_part_after_the_handshake_is_dropped_and_is_not_activity);
    FMX_RUN(a_part_on_a_byte_stream_is_a_fatal_framing_error);
    FMX_RUN(the_client_fails_on_a_broken_answer_sent_in_parts);
    FMX_RUN(a_query_and_its_reply_that_exceed_the_budget_both_travel_in_parts);
    FMX_RUN(parts_held_back_by_a_blocked_socket_leave_later_as_separate_packets);
    FMX_RUN(a_hello_larger_than_64_kib_held_by_a_blocked_socket_does_not_hit_the_ceiling);
    FMX_RUN(a_pending_part_counts_as_activity_for_the_server);
    FMX_RUN(a_probe_queued_behind_a_blocked_message_leaves_as_its_own_packet);
    FMX_RUN(a_part_type_is_still_unknown_to_the_frame_decoder);
    fake_reset(FMX_TRANSPORT_STREAM);
    FMX_DONE();
}
