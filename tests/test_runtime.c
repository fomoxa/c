#include "cyc_test.h"

#include "cyclone/connection.h"
#include "cyclone/frame.h"
#include "cyclone/handshake.h"

#define CHUNKS 16
#define CHUNK 8192

typedef struct fake {
    cyc_transport_kind kind;
    uint8_t sent[CHUNK];
    size_t sent_len;
    uint8_t incoming[CHUNKS][CHUNK];
    size_t incoming_len[CHUNKS];
    size_t head;
    size_t count;
    bool blocked;
    size_t partial;
    size_t too_large_over;
    bool closed;
    size_t soft_closes;
    size_t hard_closes;
} fake;

static fake FAKE;

static void fake_reset(cyc_transport_kind kind) {
    memset(&FAKE, 0, sizeof(FAKE));
    FAKE.kind = kind;
    FAKE.too_large_over = 0;
}

static void fake_deliver(const uint8_t *bytes, size_t len) {
    size_t slot;
    if (FAKE.count >= CHUNKS || len > CHUNK) {
        return;
    }
    slot = (FAKE.head + FAKE.count) % CHUNKS;
    memcpy(FAKE.incoming[slot], bytes, len);
    FAKE.incoming_len[slot] = len;
    FAKE.count += 1;
}

static cyc_transport_kind fake_kind(const cyc_transport *transport) {
    (void)transport;
    return FAKE.kind;
}

static cyc_send_result fake_send(cyc_transport *transport, const uint8_t *bytes, size_t len,
                                 size_t *accepted) {
    size_t take = len;
    (void)transport;
    *accepted = 0;

    if (FAKE.closed) {
        return CYC_SEND_CLOSED;
    }
    if (FAKE.too_large_over > 0 && len > FAKE.too_large_over) {
        return CYC_SEND_TOO_LARGE;
    }
    if (FAKE.blocked) {
        return CYC_SEND_WOULD_BLOCK;
    }
    if (FAKE.partial > 0 && len > FAKE.partial) {
        take = FAKE.partial;
    }
    if (FAKE.sent_len + take > CHUNK) {
        return CYC_SEND_WOULD_BLOCK;
    }
    memcpy(FAKE.sent + FAKE.sent_len, bytes, take);
    FAKE.sent_len += take;
    *accepted = take;
    return take == len ? CYC_SEND_SENT : CYC_SEND_PARTIAL;
}

static cyc_recv_result fake_recv(cyc_transport *transport, uint8_t *buffer, size_t cap,
                                 size_t *received, size_t *needed) {
    size_t available;
    (void)transport;
    *received = 0;
    *needed = 0;

    if (FAKE.count == 0) {
        return FAKE.closed ? CYC_RECV_CLOSED : CYC_RECV_WOULD_BLOCK;
    }
    available = FAKE.incoming_len[FAKE.head];

    if (FAKE.kind == CYC_TRANSPORT_MESSAGE) {
        if (available > cap) {
            *needed = available;
            return CYC_RECV_NEED_CAPACITY;
        }
        memcpy(buffer, FAKE.incoming[FAKE.head], available);
        *received = available;
        FAKE.head = (FAKE.head + 1) % CHUNKS;
        FAKE.count -= 1;
        return CYC_RECV_RECEIVED;
    }

    if (available > cap) {
        memcpy(buffer, FAKE.incoming[FAKE.head], cap);
        memmove(FAKE.incoming[FAKE.head], FAKE.incoming[FAKE.head] + cap, available - cap);
        FAKE.incoming_len[FAKE.head] = available - cap;
        *received = cap;
        return CYC_RECV_RECEIVED;
    }
    memcpy(buffer, FAKE.incoming[FAKE.head], available);
    *received = available;
    FAKE.head = (FAKE.head + 1) % CHUNKS;
    FAKE.count -= 1;
    return CYC_RECV_RECEIVED;
}

static void fake_close_soft(cyc_transport *transport) {
    (void)transport;
    FAKE.soft_closes += 1;
}

static void fake_close_hard(cyc_transport *transport) {
    (void)transport;
    FAKE.hard_closes += 1;
}

static const cyc_transport_vtable FAKE_VTABLE = {fake_kind, fake_send, fake_recv, fake_close_soft,
                                                 fake_close_hard};

static cyc_transport fake_transport(void) {
    cyc_transport transport;
    transport.vtable = &FAKE_VTABLE;
    transport.state = &FAKE;
    return transport;
}

static const uint64_t PREFIXES[1] = {10};
static cyc_message_schema MESSAGE;
static cyc_schema SCHEMA;

static void build_schema(void) {
    MESSAGE.id = 1;
    MESSAGE.fingerprint = 10;
    MESSAGE.prefixes = PREFIXES;
    MESSAGE.prefix_count = 1;
    SCHEMA.fingerprint = 0x1234;
    SCHEMA.messages = &MESSAGE;
    SCHEMA.message_count = 1;
}

static size_t handshake_frame(uint8_t verdict, uint8_t *out, size_t cap) {
    size_t written = 0;
    cyc_frame_encode_handshake(&verdict, 1, out, cap, &written);
    return written;
}

static size_t data_frame(uint32_t id, const uint8_t *payload, size_t len, uint8_t *out,
                         size_t cap) {
    size_t written = 0;
    cyc_frame_encode_data(id, payload, len, out, cap, &written);
    return written;
}

static void deliver_accept(void) {
    uint8_t frame[16];
    size_t len = handshake_frame(0, frame, sizeof(frame));
    fake_deliver(frame, len);
}

static cyc_connection *ready_connection(cyc_transport_kind kind, const cyc_config *config) {
    cyc_connection *connection;
    size_t count = 0;
    const cyc_event *events;

    fake_reset(kind);
    connection = cyc_connection_create(fake_transport(), &SCHEMA, config, 1000);
    deliver_accept();
    count = cyc_connection_tick(connection, 1000);
    events = cyc_connection_events(connection, &count);
    CYC_CHECK(count == 2);
    CYC_CHECK(events[0].kind == CYC_EVENT_CONNECTED);
    CYC_CHECK(events[1].kind == CYC_EVENT_READY);
    return connection;
}

static void connected_comes_first_and_the_hello_goes_out_at_once(void) {
    cyc_connection *connection;
    const cyc_event *events;
    size_t count;

    fake_reset(CYC_TRANSPORT_STREAM);
    connection = cyc_connection_create(fake_transport(), &SCHEMA, NULL, 1000);
    CYC_CHECK(FAKE.sent_len > 0);
    CYC_CHECK(FAKE.sent[0] == CYC_FRAME_HANDSHAKE);

    count = cyc_connection_tick(connection, 1000);
    events = cyc_connection_events(connection, &count);
    CYC_CHECK(count == 1);
    CYC_CHECK(events[0].kind == CYC_EVENT_CONNECTED);
    CYC_CHECK(cyc_connection_state(connection) == CYC_STATE_HANDSHAKING);
    cyc_connection_destroy(connection);
}

static void sending_before_ready_is_refused(void) {
    cyc_connection *connection;
    const uint8_t payload[2] = {'h', 'i'};

    fake_reset(CYC_TRANSPORT_STREAM);
    connection = cyc_connection_create(fake_transport(), &SCHEMA, NULL, 1000);
    CYC_CHECK(cyc_connection_send(connection, 1, payload, 2) == CYC_ERR_NOT_READY);
    cyc_connection_destroy(connection);
}

static void a_blocked_transport_holds_one_frame_and_sends_it_once(void) {
    cyc_connection *connection = ready_connection(CYC_TRANSPORT_STREAM, NULL);
    const uint8_t first[5] = {'f', 'i', 'r', 's', 't'};
    uint8_t expect[32];
    size_t expect_len;

    FAKE.sent_len = 0;
    FAKE.blocked = true;
    CYC_CHECK(cyc_connection_send(connection, 1, first, 5) == CYC_OK);
    CYC_CHECK(cyc_connection_congested(connection));
    CYC_CHECK(FAKE.sent_len == 0);
    CYC_CHECK(cyc_connection_send(connection, 1, first, 5) == CYC_ERR_CONGESTED);

    FAKE.blocked = false;
    cyc_connection_tick(connection, 1000);
    expect_len = data_frame(1, first, 5, expect, sizeof(expect));
    CYC_CHECK(FAKE.sent_len == expect_len);
    CYC_CHECK(memcmp(FAKE.sent, expect, expect_len) == 0);
    CYC_CHECK(!cyc_connection_congested(connection));

    FAKE.sent_len = 0;
    cyc_connection_tick(connection, 1000);
    CYC_CHECK(FAKE.sent_len == 0);
    cyc_connection_destroy(connection);
}

static void a_partial_write_is_finished_before_anything_else(void) {
    cyc_connection *connection = ready_connection(CYC_TRANSPORT_STREAM, NULL);
    const uint8_t payload[7] = {'p', 'a', 'y', 'l', 'o', 'a', 'd'};
    uint8_t expect[32];
    size_t expect_len;

    FAKE.sent_len = 0;
    FAKE.partial = 3;
    CYC_CHECK(cyc_connection_send(connection, 1, payload, 7) == CYC_OK);
    CYC_CHECK(FAKE.sent_len == 3);

    FAKE.partial = 0;
    cyc_connection_tick(connection, 1000);
    expect_len = data_frame(1, payload, 7, expect, sizeof(expect));
    CYC_CHECK(FAKE.sent_len == expect_len);
    CYC_CHECK(memcmp(FAKE.sent, expect, expect_len) == 0);
    cyc_connection_destroy(connection);
}

static void a_frame_over_the_transport_cap_does_not_kill_the_session(void) {
    cyc_connection *connection = ready_connection(CYC_TRANSPORT_MESSAGE, NULL);
    uint8_t big[64];
    const uint8_t small[5] = {'s', 'm', 'a', 'l', 'l'};

    memset(big, 0, sizeof(big));
    FAKE.too_large_over = 16;
    CYC_CHECK(cyc_connection_send(connection, 1, big, sizeof(big)) == CYC_ERR_TOO_LARGE);
    CYC_CHECK(cyc_connection_state(connection) == CYC_STATE_READY);
    CYC_CHECK(!cyc_connection_congested(connection));

    FAKE.too_large_over = 0;
    CYC_CHECK(cyc_connection_send(connection, 1, small, 5) == CYC_OK);
    cyc_connection_destroy(connection);
}

static void a_packet_that_does_not_fit_is_not_lost(void) {
    cyc_config config;
    cyc_connection *connection;
    uint8_t payload[5000];
    uint8_t frame[5100];
    size_t len;
    const cyc_event *events;
    size_t count;

    cyc_config_defaults(&config);
    config.max_message_bytes = 8192;
    connection = ready_connection(CYC_TRANSPORT_MESSAGE, &config);

    memset(payload, 7, sizeof(payload));
    len = data_frame(1, payload, sizeof(payload), frame, sizeof(frame));
    fake_deliver(frame, len);

    count = cyc_connection_tick(connection, 1000);
    events = cyc_connection_events(connection, &count);
    CYC_CHECK(count == 1);
    CYC_CHECK(events[0].kind == CYC_EVENT_MESSAGE);
    CYC_CHECK(events[0].payload_len == sizeof(payload));
    CYC_CHECK(events[0].payload[0] == 7);
    cyc_connection_destroy(connection);
}

static void a_flood_of_frames_stops_at_the_budget(void) {
    cyc_config config;
    cyc_connection *connection;
    uint8_t wire[512];
    size_t total = 0;
    uint32_t index;
    const cyc_event *events;
    size_t count;
    size_t messages = 0;
    const uint8_t body[1] = {'x'};

    cyc_config_defaults(&config);
    config.max_frames_per_tick = 4;
    connection = ready_connection(CYC_TRANSPORT_STREAM, &config);

    for (index = 0; index < 10; ++index) {
        total += data_frame(index, body, 1, wire + total, sizeof(wire) - total);
    }
    fake_deliver(wire, total);

    count = cyc_connection_tick(connection, 1000);
    events = cyc_connection_events(connection, &count);
    for (index = 0; index < count; ++index) {
        if (events[index].kind == CYC_EVENT_MESSAGE) {
            CYC_CHECK(events[index].message_id == (uint32_t)messages);
            messages += 1;
        }
    }
    CYC_CHECK(messages == 4);

    count = cyc_connection_tick(connection, 1000);
    events = cyc_connection_events(connection, &count);
    messages = 0;
    for (index = 0; index < count; ++index) {
        if (events[index].kind == CYC_EVENT_MESSAGE) {
            CYC_CHECK(events[index].message_id == (uint32_t)(messages + 4));
            messages += 1;
        }
    }
    CYC_CHECK(messages == 4);
    cyc_connection_destroy(connection);
}

static void a_probe_is_answered_from_inside_the_tick(void) {
    cyc_connection *connection = ready_connection(CYC_TRANSPORT_STREAM, NULL);
    uint8_t probe[1];
    const cyc_event *events;
    size_t count;

    FAKE.sent_len = 0;
    cyc_frame_encode_probe(probe, sizeof(probe));
    fake_deliver(probe, 1);

    count = cyc_connection_tick(connection, 1000);
    events = cyc_connection_events(connection, &count);
    CYC_CHECK(count == 1);
    CYC_CHECK(events[0].kind == CYC_EVENT_PROBE);
    CYC_CHECK(FAKE.sent_len == 1);
    CYC_CHECK(FAKE.sent[0] == CYC_FRAME_ACK);
    cyc_connection_destroy(connection);
}

static void a_rejected_handshake_raises_one_terminal_event(void) {
    cyc_connection *connection;
    uint8_t frame[16];
    size_t len;
    const cyc_event *events;
    size_t count;
    int round;

    fake_reset(CYC_TRANSPORT_STREAM);
    connection = cyc_connection_create(fake_transport(), &SCHEMA, NULL, 1000);
    len = handshake_frame(2, frame, sizeof(frame));
    fake_deliver(frame, len);

    count = cyc_connection_tick(connection, 1000);
    events = cyc_connection_events(connection, &count);
    CYC_CHECK(count == 2);
    CYC_CHECK(events[1].kind == CYC_EVENT_HANDSHAKE_FAILED);
    CYC_CHECK(events[1].reason == CYC_FAIL_SCHEMA_CONFLICT);
    CYC_CHECK(FAKE.soft_closes == 1);

    FAKE.closed = true;
    for (round = 0; round < 3; ++round) {
        count = cyc_connection_tick(connection, 1000);
        CYC_CHECK(count == 0);
    }
    cyc_connection_destroy(connection);
}

static void a_closed_transport_disconnects_exactly_once(void) {
    cyc_connection *connection = ready_connection(CYC_TRANSPORT_STREAM, NULL);
    const cyc_event *events;
    size_t count;

    FAKE.closed = true;
    count = cyc_connection_tick(connection, 1000);
    events = cyc_connection_events(connection, &count);
    CYC_CHECK(count == 1);
    CYC_CHECK(events[0].kind == CYC_EVENT_DISCONNECTED);
    CYC_CHECK(events[0].reason == CYC_DISCONNECT_PEER_CLOSED);

    count = cyc_connection_tick(connection, 1000);
    CYC_CHECK(count == 0);
    cyc_connection_destroy(connection);
}

static void a_broken_stream_ends_the_session(void) {
    cyc_connection *connection = ready_connection(CYC_TRANSPORT_STREAM, NULL);
    const uint8_t junk[1] = {0x09};
    const cyc_event *events;
    size_t count;

    fake_deliver(junk, 1);
    count = cyc_connection_tick(connection, 1000);
    events = cyc_connection_events(connection, &count);
    CYC_CHECK(count == 1);
    CYC_CHECK(events[0].kind == CYC_EVENT_DISCONNECTED);
    CYC_CHECK(events[0].reason == CYC_DISCONNECT_TRANSPORT_ERROR);
    cyc_connection_destroy(connection);
}

static void a_broken_packet_is_dropped_and_the_session_survives(void) {
    cyc_connection *connection = ready_connection(CYC_TRANSPORT_MESSAGE, NULL);
    const uint8_t junk[2] = {0x09, 0x09};
    const uint8_t body[4] = {'l', 'i', 'v', 'e'};
    uint8_t frame[32];
    size_t len;
    const cyc_event *events;
    size_t count;

    fake_deliver(junk, 2);
    len = data_frame(1, body, 4, frame, sizeof(frame));
    fake_deliver(frame, len);

    count = cyc_connection_tick(connection, 1000);
    events = cyc_connection_events(connection, &count);
    CYC_CHECK(count == 1);
    CYC_CHECK(events[0].kind == CYC_EVENT_MESSAGE);
    CYC_CHECK(cyc_connection_state(connection) == CYC_STATE_READY);
    cyc_connection_destroy(connection);
}

static void heartbeat_runs_over_a_transport_without_waiting(void) {
    cyc_connection *connection = ready_connection(CYC_TRANSPORT_STREAM, NULL);
    const cyc_event *events;
    size_t count;

    FAKE.sent_len = 0;
    cyc_connection_tick(connection, 6000);
    CYC_CHECK(FAKE.sent_len == 1);
    CYC_CHECK(FAKE.sent[0] == CYC_FRAME_PROBE);

    count = cyc_connection_tick(connection, 21000);
    events = cyc_connection_events(connection, &count);
    CYC_CHECK(count == 1);
    CYC_CHECK(events[0].kind == CYC_EVENT_DISCONNECTED);
    CYC_CHECK(events[0].reason == CYC_DISCONNECT_UNRESPONSIVE);
    cyc_connection_destroy(connection);
}

static void closing_locally_raises_no_event(void) {
    cyc_connection *connection = ready_connection(CYC_TRANSPORT_STREAM, NULL);
    size_t count;

    cyc_connection_close(connection);
    count = cyc_connection_tick(connection, 1000);
    CYC_CHECK(count == 0);
    CYC_CHECK(FAKE.soft_closes == 1);

    cyc_connection_destroy(connection);
    CYC_CHECK(FAKE.hard_closes == 1);
}

int main(void) {
    build_schema();
    CYC_RUN(connected_comes_first_and_the_hello_goes_out_at_once);
    CYC_RUN(sending_before_ready_is_refused);
    CYC_RUN(a_blocked_transport_holds_one_frame_and_sends_it_once);
    CYC_RUN(a_partial_write_is_finished_before_anything_else);
    CYC_RUN(a_frame_over_the_transport_cap_does_not_kill_the_session);
    CYC_RUN(a_packet_that_does_not_fit_is_not_lost);
    CYC_RUN(a_flood_of_frames_stops_at_the_budget);
    CYC_RUN(a_probe_is_answered_from_inside_the_tick);
    CYC_RUN(a_rejected_handshake_raises_one_terminal_event);
    CYC_RUN(a_closed_transport_disconnects_exactly_once);
    CYC_RUN(a_broken_stream_ends_the_session);
    CYC_RUN(a_broken_packet_is_dropped_and_the_session_survives);
    CYC_RUN(heartbeat_runs_over_a_transport_without_waiting);
    CYC_RUN(closing_locally_raises_no_event);
    CYC_DONE();
}
