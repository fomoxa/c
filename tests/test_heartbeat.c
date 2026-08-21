#include "cyc_test.h"

#include "cyclone/handshake.h"
#include "cyclone/session.h"

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

static void feed(cyc_session *session, uint8_t type, const uint8_t *payload, size_t len,
                 uint64_t now, cyc_reaction *reaction) {
    cyc_frame frame;
    frame.type = type;
    frame.message_id = 0;
    frame.payload = payload;
    frame.payload_len = len;
    cyc_session_on_frame(session, &frame, now, reaction);
}

static cyc_session *ready_client(uint64_t now) {
    cyc_config config;
    cyc_reaction opening;
    cyc_reaction reaction;
    const uint8_t accept[1] = {0};
    cyc_session *session;

    cyc_config_defaults(&config);
    session = cyc_session_create(CYC_ROLE_CLIENT, &SCHEMA, &config, now, &opening);
    feed(session, CYC_FRAME_HANDSHAKE, accept, 1, now, &reaction);
    CYC_CHECK(reaction.has_event && reaction.event == CYC_EVENT_READY);
    return session;
}

static cyc_session *ready_server(uint64_t now) {
    cyc_config config;
    cyc_reaction quiet;
    cyc_reaction reaction;
    uint8_t hello[64];
    size_t len;
    cyc_session *session;

    cyc_config_defaults(&config);
    session = cyc_session_create(CYC_ROLE_SERVER, &SCHEMA, &config, now, &quiet);
    len = cyc_hello_encode(&SCHEMA, hello, sizeof(hello));
    feed(session, CYC_FRAME_HANDSHAKE, hello, len, now, &reaction);
    CYC_CHECK(reaction.has_event && reaction.event == CYC_EVENT_READY);
    return session;
}

static void traffic_keeps_the_probe_away(void) {
    cyc_session *session = ready_client(1000);
    cyc_reaction reaction;
    uint64_t now;

    for (now = 2000; now <= 22000; now += 1000) {
        feed(session, CYC_FRAME_DATA, NULL, 0, now, &reaction);
        cyc_session_tick(session, now, &reaction);
        CYC_CHECK(reaction.out == CYC_OUT_NONE);
    }
    cyc_session_destroy(session);
}

static void silence_sends_exactly_one_probe(void) {
    cyc_session *session = ready_client(1000);
    cyc_reaction reaction;
    uint64_t now;

    cyc_session_tick(session, 5000, &reaction);
    CYC_CHECK(reaction.out == CYC_OUT_NONE);
    cyc_session_tick(session, 6000, &reaction);
    CYC_CHECK(reaction.out == CYC_OUT_PROBE);

    for (now = 7000; now < 16000; now += 1000) {
        cyc_session_tick(session, now, &reaction);
        CYC_CHECK(reaction.out == CYC_OUT_NONE);
        CYC_CHECK(!reaction.has_event);
    }
    cyc_session_destroy(session);
}

static void an_ack_puts_the_session_back_to_normal(void) {
    cyc_session *session = ready_client(1000);
    cyc_reaction reaction;

    cyc_session_tick(session, 6000, &reaction);
    CYC_CHECK(reaction.out == CYC_OUT_PROBE);

    feed(session, CYC_FRAME_ACK, NULL, 0, 7000, &reaction);
    CYC_CHECK(reaction.has_event && reaction.event == CYC_EVENT_ACK);

    cyc_session_tick(session, 11000, &reaction);
    CYC_CHECK(reaction.out == CYC_OUT_NONE);
    cyc_session_tick(session, 12000, &reaction);
    CYC_CHECK(reaction.out == CYC_OUT_PROBE);
    cyc_session_destroy(session);
}

static void any_frame_clears_the_probe(void) {
    uint8_t kinds[3] = {CYC_FRAME_DATA, CYC_FRAME_PROBE, CYC_FRAME_ACK};
    size_t index;

    for (index = 0; index < 3; ++index) {
        cyc_session *session = ready_client(1000);
        cyc_reaction reaction;

        cyc_session_tick(session, 6000, &reaction);
        CYC_CHECK(reaction.out == CYC_OUT_PROBE);
        feed(session, kinds[index], NULL, 0, 7000, &reaction);
        cyc_session_tick(session, 21500, &reaction);
        CYC_CHECK(!reaction.has_event);
        cyc_session_destroy(session);
    }
}

static void a_probe_is_always_answered_with_an_ack(void) {
    cyc_session *session = ready_client(1000);
    cyc_reaction reaction;

    feed(session, CYC_FRAME_PROBE, NULL, 0, 1500, &reaction);
    CYC_CHECK(reaction.out == CYC_OUT_ACK);
    CYC_CHECK(reaction.has_event && reaction.event == CYC_EVENT_PROBE);
    cyc_session_destroy(session);
}

static void a_handshaking_client_answers_probes_but_never_sends_one(void) {
    cyc_config config;
    cyc_reaction opening;
    cyc_reaction reaction;
    cyc_session *session;

    cyc_config_defaults(&config);
    session = cyc_session_create(CYC_ROLE_CLIENT, &SCHEMA, &config, 1000, &opening);

    feed(session, CYC_FRAME_PROBE, NULL, 0, 1500, &reaction);
    CYC_CHECK(reaction.out == CYC_OUT_ACK);
    CYC_CHECK(!reaction.has_event);

    cyc_session_tick(session, 4000, &reaction);
    CYC_CHECK(reaction.out == CYC_OUT_NONE);
    cyc_session_destroy(session);
}

static void silence_past_the_response_deadline_declares_the_peer_dead(void) {
    cyc_session *session = ready_client(1000);
    cyc_reaction reaction;

    cyc_session_tick(session, 6000, &reaction);
    CYC_CHECK(reaction.out == CYC_OUT_PROBE);
    cyc_session_tick(session, 20000, &reaction);
    CYC_CHECK(!reaction.has_event);
    cyc_session_tick(session, 21000, &reaction);
    CYC_CHECK(reaction.has_event && reaction.event == CYC_EVENT_DISCONNECTED);
    CYC_CHECK(reaction.reason == CYC_DISCONNECT_UNRESPONSIVE);
    CYC_CHECK(cyc_session_state(session) == CYC_STATE_CLOSED);
    cyc_session_destroy(session);
}

static void a_server_widens_its_window_while_a_peer_is_handshaking(void) {
    cyc_config config;
    cyc_reaction quiet;
    cyc_reaction reaction;
    cyc_session *handshaking;
    cyc_session *patient;

    cyc_config_defaults(&config);
    handshaking = cyc_session_create(CYC_ROLE_SERVER, &SCHEMA, &config, 1000, &quiet);
    cyc_session_tick(handshaking, 6000, &reaction);
    CYC_CHECK(reaction.out == CYC_OUT_PROBE);
    cyc_session_destroy(handshaking);

    config.handshake_timeout_ms = 30000;
    patient = cyc_session_create(CYC_ROLE_SERVER, &SCHEMA, &config, 1000, &quiet);
    cyc_session_tick(patient, 30000, &reaction);
    CYC_CHECK(reaction.out == CYC_OUT_NONE);
    cyc_session_tick(patient, 31000, &reaction);
    CYC_CHECK(reaction.out == CYC_OUT_PROBE);
    cyc_session_destroy(patient);
}

static void a_ready_server_narrows_its_window(void) {
    cyc_session *session = ready_server(1000);
    cyc_reaction reaction;

    cyc_session_tick(session, 5000, &reaction);
    CYC_CHECK(reaction.out == CYC_OUT_NONE);
    cyc_session_tick(session, 6000, &reaction);
    CYC_CHECK(reaction.out == CYC_OUT_PROBE);
    cyc_session_tick(session, 21000, &reaction);
    CYC_CHECK(reaction.has_event && reaction.event == CYC_EVENT_DISCONNECTED);
    cyc_session_destroy(session);
}

static void only_one_terminal_event_is_ever_raised(void) {
    cyc_session *session = ready_client(1000);
    cyc_reaction reaction;

    cyc_session_tick(session, 6000, &reaction);
    cyc_session_tick(session, 21000, &reaction);
    CYC_CHECK(reaction.has_event && reaction.event == CYC_EVENT_DISCONNECTED);

    cyc_session_transport_closed(session, CYC_DISCONNECT_PEER_CLOSED, &reaction);
    CYC_CHECK(!reaction.has_event);
    cyc_session_tick(session, 60000, &reaction);
    CYC_CHECK(!reaction.has_event);
    cyc_session_destroy(session);
}

int main(void) {
    build_schema();
    CYC_RUN(traffic_keeps_the_probe_away);
    CYC_RUN(silence_sends_exactly_one_probe);
    CYC_RUN(an_ack_puts_the_session_back_to_normal);
    CYC_RUN(any_frame_clears_the_probe);
    CYC_RUN(a_probe_is_always_answered_with_an_ack);
    CYC_RUN(a_handshaking_client_answers_probes_but_never_sends_one);
    CYC_RUN(silence_past_the_response_deadline_declares_the_peer_dead);
    CYC_RUN(a_server_widens_its_window_while_a_peer_is_handshaking);
    CYC_RUN(a_ready_server_narrows_its_window);
    CYC_RUN(only_one_terminal_event_is_ever_raised);
    CYC_DONE();
}
