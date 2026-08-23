#include "fmx_test.h"

#include "fomoxa/connection.h"
#include "fomoxa/frame.h"
#include "fomoxa/handshake.h"

#define CHUNKS 16
#define CHUNK 8192

typedef struct fake {
    fmx_transport_kind kind;
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

static void fake_reset(fmx_transport_kind kind) {
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

static fmx_transport_kind fake_kind(const fmx_transport *transport) {
    (void)transport;
    return FAKE.kind;
}

static fmx_send_result fake_send(fmx_transport *transport, const uint8_t *bytes, size_t len,
                                 size_t *accepted) {
    size_t take = len;
    (void)transport;
    *accepted = 0;

    if (FAKE.closed) {
        return FMX_SEND_CLOSED;
    }
    if (FAKE.too_large_over > 0 && len > FAKE.too_large_over) {
        return FMX_SEND_TOO_LARGE;
    }
    if (FAKE.blocked) {
        return FMX_SEND_WOULD_BLOCK;
    }
    if (FAKE.partial > 0 && len > FAKE.partial) {
        take = FAKE.partial;
    }
    if (FAKE.sent_len + take > CHUNK) {
        return FMX_SEND_WOULD_BLOCK;
    }
    memcpy(FAKE.sent + FAKE.sent_len, bytes, take);
    FAKE.sent_len += take;
    *accepted = take;
    return take == len ? FMX_SEND_SENT : FMX_SEND_PARTIAL;
}

static fmx_recv_result fake_recv(fmx_transport *transport, uint8_t *buffer, size_t cap,
                                 size_t *received, size_t *needed) {
    size_t available;
    (void)transport;
    *received = 0;
    *needed = 0;

    if (FAKE.count == 0) {
        return FAKE.closed ? FMX_RECV_CLOSED : FMX_RECV_WOULD_BLOCK;
    }
    available = FAKE.incoming_len[FAKE.head];

    if (FAKE.kind == FMX_TRANSPORT_MESSAGE) {
        if (available > cap) {
            *needed = available;
            return FMX_RECV_NEED_CAPACITY;
        }
        memcpy(buffer, FAKE.incoming[FAKE.head], available);
        *received = available;
        FAKE.head = (FAKE.head + 1) % CHUNKS;
        FAKE.count -= 1;
        return FMX_RECV_RECEIVED;
    }

    if (available > cap) {
        memcpy(buffer, FAKE.incoming[FAKE.head], cap);
        memmove(FAKE.incoming[FAKE.head], FAKE.incoming[FAKE.head] + cap, available - cap);
        FAKE.incoming_len[FAKE.head] = available - cap;
        *received = cap;
        return FMX_RECV_RECEIVED;
    }
    memcpy(buffer, FAKE.incoming[FAKE.head], available);
    *received = available;
    FAKE.head = (FAKE.head + 1) % CHUNKS;
    FAKE.count -= 1;
    return FMX_RECV_RECEIVED;
}

static void fake_close_soft(fmx_transport *transport) {
    (void)transport;
    FAKE.soft_closes += 1;
}

static void fake_close_hard(fmx_transport *transport) {
    (void)transport;
    FAKE.hard_closes += 1;
}

static const fmx_transport_vtable FAKE_VTABLE = {fake_kind, fake_send, fake_recv, fake_close_soft,
                                                 fake_close_hard};

static fmx_transport fake_transport(void) {
    fmx_transport transport;
    transport.vtable = &FAKE_VTABLE;
    transport.state = &FAKE;
    return transport;
}

static const uint64_t PREFIXES[1] = {10};
static fmx_message_schema MESSAGE;
static fmx_schema SCHEMA;

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
    fmx_frame_encode_handshake(&verdict, 1, out, cap, &written);
    return written;
}

static size_t data_frame(uint32_t id, const uint8_t *payload, size_t len, uint8_t *out,
                         size_t cap) {
    size_t written = 0;
    fmx_frame_encode_data(id, payload, len, out, cap, &written);
    return written;
}

static void deliver_accept(void) {
    uint8_t frame[16];
    size_t len = handshake_frame(0, frame, sizeof(frame));
    fake_deliver(frame, len);
}

static fmx_connection *ready_connection(fmx_transport_kind kind, const fmx_config *config) {
    fmx_connection *connection;
    size_t count = 0;
    const fmx_event *events;

    fake_reset(kind);
    connection = fmx_connection_create(fake_transport(), &SCHEMA, config, 1000);
    deliver_accept();
    count = fmx_connection_tick(connection, 1000);
    events = fmx_connection_events(connection, &count);
    FMX_CHECK(count == 2);
    FMX_CHECK(events[0].kind == FMX_EVENT_CONNECTED);
    FMX_CHECK(events[1].kind == FMX_EVENT_READY);
    return connection;
}

static void connected_comes_first_and_the_hello_goes_out_at_once(void) {
    fmx_connection *connection;
    const fmx_event *events;
    size_t count;

    fake_reset(FMX_TRANSPORT_STREAM);
    connection = fmx_connection_create(fake_transport(), &SCHEMA, NULL, 1000);
    FMX_CHECK(FAKE.sent_len > 0);
    FMX_CHECK(FAKE.sent[0] == FMX_FRAME_HANDSHAKE);

    count = fmx_connection_tick(connection, 1000);
    events = fmx_connection_events(connection, &count);
    FMX_CHECK(count == 1);
    FMX_CHECK(events[0].kind == FMX_EVENT_CONNECTED);
    FMX_CHECK(fmx_connection_state(connection) == FMX_STATE_HANDSHAKING);
    fmx_connection_destroy(connection);
}

static void sending_before_ready_is_refused(void) {
    fmx_connection *connection;
    const uint8_t payload[2] = {'h', 'i'};

    fake_reset(FMX_TRANSPORT_STREAM);
    connection = fmx_connection_create(fake_transport(), &SCHEMA, NULL, 1000);
    FMX_CHECK(fmx_connection_send(connection, 1, payload, 2) == FMX_ERR_NOT_READY);
    fmx_connection_destroy(connection);
}

/* 02 §8: the pending queue must have a ceiling. A peer that probes every tick
   while never reading keeps our silence clock alive, so the heartbeat never
   ends the session - only the ceiling does. */
static void a_blocked_transport_and_a_probing_peer_stop_at_the_outbox_ceiling(void) {
    fmx_connection *connection = ready_connection(FMX_TRANSPORT_STREAM, NULL);
    const uint8_t probe[1] = {FMX_FRAME_PROBE};
    bool ended = false;
    size_t round;

    FAKE.blocked = true;
    for (round = 0; round < 200000u && !ended; ++round) {
        const fmx_event *events;
        size_t count = 0;
        size_t index;

        fake_deliver(probe, 1);
        fmx_connection_tick(connection, 1000 + (uint64_t)round);
        events = fmx_connection_events(connection, &count);
        for (index = 0; index < count; ++index) {
            if (events[index].kind == FMX_EVENT_DISCONNECTED) {
                FMX_CHECK(events[index].reason == (int)FMX_DISCONNECT_UNRESPONSIVE);
                ended = true;
            }
        }
    }

    FMX_CHECK(ended);
    fmx_connection_destroy(connection);
}

static void a_blocked_transport_holds_one_frame_and_sends_it_once(void) {
    fmx_connection *connection = ready_connection(FMX_TRANSPORT_STREAM, NULL);
    const uint8_t first[5] = {'f', 'i', 'r', 's', 't'};
    uint8_t expect[32];
    size_t expect_len;

    FAKE.sent_len = 0;
    FAKE.blocked = true;
    FMX_CHECK(fmx_connection_send(connection, 1, first, 5) == FMX_OK);
    FMX_CHECK(fmx_connection_congested(connection));
    FMX_CHECK(FAKE.sent_len == 0);
    FMX_CHECK(fmx_connection_send(connection, 1, first, 5) == FMX_ERR_CONGESTED);

    FAKE.blocked = false;
    fmx_connection_tick(connection, 1000);
    expect_len = data_frame(1, first, 5, expect, sizeof(expect));
    FMX_CHECK(FAKE.sent_len == expect_len);
    FMX_CHECK(memcmp(FAKE.sent, expect, expect_len) == 0);
    FMX_CHECK(!fmx_connection_congested(connection));

    FAKE.sent_len = 0;
    fmx_connection_tick(connection, 1000);
    FMX_CHECK(FAKE.sent_len == 0);
    fmx_connection_destroy(connection);
}

static void a_partial_write_is_finished_before_anything_else(void) {
    fmx_connection *connection = ready_connection(FMX_TRANSPORT_STREAM, NULL);
    const uint8_t payload[7] = {'p', 'a', 'y', 'l', 'o', 'a', 'd'};
    uint8_t expect[32];
    size_t expect_len;

    FAKE.sent_len = 0;
    FAKE.partial = 3;
    FMX_CHECK(fmx_connection_send(connection, 1, payload, 7) == FMX_OK);
    FMX_CHECK(FAKE.sent_len == 3);

    FAKE.partial = 0;
    fmx_connection_tick(connection, 1000);
    expect_len = data_frame(1, payload, 7, expect, sizeof(expect));
    FMX_CHECK(FAKE.sent_len == expect_len);
    FMX_CHECK(memcmp(FAKE.sent, expect, expect_len) == 0);
    fmx_connection_destroy(connection);
}

static void a_frame_over_the_transport_cap_does_not_kill_the_session(void) {
    fmx_connection *connection = ready_connection(FMX_TRANSPORT_MESSAGE, NULL);
    uint8_t big[64];
    const uint8_t small[5] = {'s', 'm', 'a', 'l', 'l'};

    memset(big, 0, sizeof(big));
    FAKE.too_large_over = 16;
    FMX_CHECK(fmx_connection_send(connection, 1, big, sizeof(big)) == FMX_ERR_TOO_LARGE);
    FMX_CHECK(fmx_connection_state(connection) == FMX_STATE_READY);
    FMX_CHECK(!fmx_connection_congested(connection));

    FAKE.too_large_over = 0;
    FMX_CHECK(fmx_connection_send(connection, 1, small, 5) == FMX_OK);
    fmx_connection_destroy(connection);
}

static void a_packet_that_does_not_fit_is_not_lost(void) {
    fmx_config config;
    fmx_connection *connection;
    uint8_t payload[5000];
    uint8_t frame[5100];
    size_t len;
    const fmx_event *events;
    size_t count;

    fmx_config_defaults(&config);
    config.max_message_bytes = 8192;
    connection = ready_connection(FMX_TRANSPORT_MESSAGE, &config);

    memset(payload, 7, sizeof(payload));
    len = data_frame(1, payload, sizeof(payload), frame, sizeof(frame));
    fake_deliver(frame, len);

    count = fmx_connection_tick(connection, 1000);
    events = fmx_connection_events(connection, &count);
    FMX_CHECK(count == 1);
    FMX_CHECK(events[0].kind == FMX_EVENT_MESSAGE);
    FMX_CHECK(events[0].payload_len == sizeof(payload));
    FMX_CHECK(events[0].payload[0] == 7);
    fmx_connection_destroy(connection);
}

static void a_flood_of_frames_stops_at_the_budget(void) {
    fmx_config config;
    fmx_connection *connection;
    uint8_t wire[512];
    size_t total = 0;
    uint32_t index;
    const fmx_event *events;
    size_t count;
    size_t messages = 0;
    const uint8_t body[1] = {'x'};

    fmx_config_defaults(&config);
    config.max_frames_per_tick = 4;
    connection = ready_connection(FMX_TRANSPORT_STREAM, &config);

    for (index = 0; index < 10; ++index) {
        total += data_frame(index, body, 1, wire + total, sizeof(wire) - total);
    }
    fake_deliver(wire, total);

    count = fmx_connection_tick(connection, 1000);
    events = fmx_connection_events(connection, &count);
    for (index = 0; index < count; ++index) {
        if (events[index].kind == FMX_EVENT_MESSAGE) {
            FMX_CHECK(events[index].message_id == (uint32_t)messages);
            messages += 1;
        }
    }
    FMX_CHECK(messages == 4);

    count = fmx_connection_tick(connection, 1000);
    events = fmx_connection_events(connection, &count);
    messages = 0;
    for (index = 0; index < count; ++index) {
        if (events[index].kind == FMX_EVENT_MESSAGE) {
            FMX_CHECK(events[index].message_id == (uint32_t)(messages + 4));
            messages += 1;
        }
    }
    FMX_CHECK(messages == 4);
    fmx_connection_destroy(connection);
}

static void a_probe_is_answered_from_inside_the_tick(void) {
    fmx_connection *connection = ready_connection(FMX_TRANSPORT_STREAM, NULL);
    uint8_t probe[1];
    const fmx_event *events;
    size_t count;

    FAKE.sent_len = 0;
    fmx_frame_encode_probe(probe, sizeof(probe));
    fake_deliver(probe, 1);

    count = fmx_connection_tick(connection, 1000);
    events = fmx_connection_events(connection, &count);
    FMX_CHECK(count == 1);
    FMX_CHECK(events[0].kind == FMX_EVENT_PROBE);
    FMX_CHECK(FAKE.sent_len == 1);
    FMX_CHECK(FAKE.sent[0] == FMX_FRAME_ACK);
    fmx_connection_destroy(connection);
}

static void a_rejected_handshake_raises_one_terminal_event(void) {
    fmx_connection *connection;
    uint8_t frame[16];
    size_t len;
    const fmx_event *events;
    size_t count;
    int round;

    fake_reset(FMX_TRANSPORT_STREAM);
    connection = fmx_connection_create(fake_transport(), &SCHEMA, NULL, 1000);
    len = handshake_frame(2, frame, sizeof(frame));
    fake_deliver(frame, len);

    count = fmx_connection_tick(connection, 1000);
    events = fmx_connection_events(connection, &count);
    FMX_CHECK(count == 2);
    FMX_CHECK(events[1].kind == FMX_EVENT_HANDSHAKE_FAILED);
    FMX_CHECK(events[1].reason == FMX_FAIL_SCHEMA_CONFLICT);
    FMX_CHECK(FAKE.soft_closes == 1);

    FAKE.closed = true;
    for (round = 0; round < 3; ++round) {
        count = fmx_connection_tick(connection, 1000);
        FMX_CHECK(count == 0);
    }
    fmx_connection_destroy(connection);
}

static void a_closed_transport_disconnects_exactly_once(void) {
    fmx_connection *connection = ready_connection(FMX_TRANSPORT_STREAM, NULL);
    const fmx_event *events;
    size_t count;

    FAKE.closed = true;
    count = fmx_connection_tick(connection, 1000);
    events = fmx_connection_events(connection, &count);
    FMX_CHECK(count == 1);
    FMX_CHECK(events[0].kind == FMX_EVENT_DISCONNECTED);
    FMX_CHECK(events[0].reason == FMX_DISCONNECT_PEER_CLOSED);

    count = fmx_connection_tick(connection, 1000);
    FMX_CHECK(count == 0);
    fmx_connection_destroy(connection);
}

static void a_broken_stream_ends_the_session(void) {
    fmx_connection *connection = ready_connection(FMX_TRANSPORT_STREAM, NULL);
    const uint8_t junk[1] = {0x09};
    const fmx_event *events;
    size_t count;

    fake_deliver(junk, 1);
    count = fmx_connection_tick(connection, 1000);
    events = fmx_connection_events(connection, &count);
    FMX_CHECK(count == 1);
    FMX_CHECK(events[0].kind == FMX_EVENT_DISCONNECTED);
    FMX_CHECK(events[0].reason == FMX_DISCONNECT_TRANSPORT_ERROR);
    fmx_connection_destroy(connection);
}

static void a_broken_packet_is_dropped_and_the_session_survives(void) {
    fmx_connection *connection = ready_connection(FMX_TRANSPORT_MESSAGE, NULL);
    const uint8_t junk[2] = {0x09, 0x09};
    const uint8_t body[4] = {'l', 'i', 'v', 'e'};
    uint8_t frame[32];
    size_t len;
    const fmx_event *events;
    size_t count;

    fake_deliver(junk, 2);
    len = data_frame(1, body, 4, frame, sizeof(frame));
    fake_deliver(frame, len);

    count = fmx_connection_tick(connection, 1000);
    events = fmx_connection_events(connection, &count);
    FMX_CHECK(count == 1);
    FMX_CHECK(events[0].kind == FMX_EVENT_MESSAGE);
    FMX_CHECK(fmx_connection_state(connection) == FMX_STATE_READY);
    fmx_connection_destroy(connection);
}

static void heartbeat_runs_over_a_transport_without_waiting(void) {
    fmx_connection *connection = ready_connection(FMX_TRANSPORT_STREAM, NULL);
    const fmx_event *events;
    size_t count;

    FAKE.sent_len = 0;
    fmx_connection_tick(connection, 6000);
    FMX_CHECK(FAKE.sent_len == 1);
    FMX_CHECK(FAKE.sent[0] == FMX_FRAME_PROBE);

    count = fmx_connection_tick(connection, 21000);
    events = fmx_connection_events(connection, &count);
    FMX_CHECK(count == 1);
    FMX_CHECK(events[0].kind == FMX_EVENT_DISCONNECTED);
    FMX_CHECK(events[0].reason == FMX_DISCONNECT_UNRESPONSIVE);
    fmx_connection_destroy(connection);
}

static void closing_locally_raises_no_event(void) {
    fmx_connection *connection = ready_connection(FMX_TRANSPORT_STREAM, NULL);
    size_t count;

    fmx_connection_close(connection);
    count = fmx_connection_tick(connection, 1000);
    FMX_CHECK(count == 0);
    FMX_CHECK(FAKE.soft_closes == 1);

    fmx_connection_destroy(connection);
    FMX_CHECK(FAKE.hard_closes == 1);
}

int main(void) {
    build_schema();
    FMX_RUN(connected_comes_first_and_the_hello_goes_out_at_once);
    FMX_RUN(sending_before_ready_is_refused);
    FMX_RUN(a_blocked_transport_holds_one_frame_and_sends_it_once);
    FMX_RUN(a_blocked_transport_and_a_probing_peer_stop_at_the_outbox_ceiling);
    FMX_RUN(a_partial_write_is_finished_before_anything_else);
    FMX_RUN(a_frame_over_the_transport_cap_does_not_kill_the_session);
    FMX_RUN(a_packet_that_does_not_fit_is_not_lost);
    FMX_RUN(a_flood_of_frames_stops_at_the_budget);
    FMX_RUN(a_probe_is_answered_from_inside_the_tick);
    FMX_RUN(a_rejected_handshake_raises_one_terminal_event);
    FMX_RUN(a_closed_transport_disconnects_exactly_once);
    FMX_RUN(a_broken_stream_ends_the_session);
    FMX_RUN(a_broken_packet_is_dropped_and_the_session_survives);
    FMX_RUN(heartbeat_runs_over_a_transport_without_waiting);
    FMX_RUN(closing_locally_raises_no_event);
    FMX_DONE();
}
