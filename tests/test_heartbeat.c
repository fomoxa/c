#include "fmx_test.h"

#include "fomoxa/handshake.h"
#include "fomoxa/session.h"

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

static void feed(fmx_session *session, uint8_t type, const uint8_t *payload, size_t len,
                 uint64_t now, fmx_reaction *reaction) {
    fmx_frame frame;
    frame.type = type;
    frame.message_id = 0;
    frame.payload = payload;
    frame.payload_len = len;
    fmx_session_on_frame(session, &frame, now, reaction);
}

static fmx_session *ready_client(uint64_t now) {
    fmx_config config;
    fmx_reaction opening;
    fmx_reaction reaction;
    const uint8_t accept[1] = {0};
    fmx_session *session;

    fmx_config_defaults(&config);
    session = fmx_session_create(FMX_ROLE_CLIENT, &SCHEMA, &config, now, &opening);
    feed(session, FMX_FRAME_HANDSHAKE, accept, 1, now, &reaction);
    FMX_CHECK(reaction.has_event && reaction.event == FMX_EVENT_READY);
    return session;
}

static fmx_session *ready_server(uint64_t now) {
    fmx_config config;
    fmx_reaction quiet;
    fmx_reaction reaction;
    uint8_t hello[64];
    size_t len;
    fmx_session *session;

    fmx_config_defaults(&config);
    session = fmx_session_create(FMX_ROLE_SERVER, &SCHEMA, &config, now, &quiet);
    len = fmx_hello_encode(&SCHEMA, hello, sizeof(hello));
    feed(session, FMX_FRAME_HANDSHAKE, hello, len, now, &reaction);
    FMX_CHECK(reaction.has_event && reaction.event == FMX_EVENT_READY);
    return session;
}

static void traffic_keeps_the_probe_away(void) {
    fmx_session *session = ready_client(1000);
    fmx_reaction reaction;
    uint64_t now;

    for (now = 2000; now <= 22000; now += 1000) {
        feed(session, FMX_FRAME_DATA, NULL, 0, now, &reaction);
        fmx_session_tick(session, now, &reaction);
        FMX_CHECK(reaction.out == FMX_OUT_NONE);
    }
    fmx_session_destroy(session);
}

static void silence_sends_exactly_one_probe(void) {
    fmx_session *session = ready_client(1000);
    fmx_reaction reaction;
    uint64_t now;

    fmx_session_tick(session, 5000, &reaction);
    FMX_CHECK(reaction.out == FMX_OUT_NONE);
    fmx_session_tick(session, 6000, &reaction);
    FMX_CHECK(reaction.out == FMX_OUT_PROBE);

    for (now = 7000; now < 16000; now += 1000) {
        fmx_session_tick(session, now, &reaction);
        FMX_CHECK(reaction.out == FMX_OUT_NONE);
        FMX_CHECK(!reaction.has_event);
    }
    fmx_session_destroy(session);
}

static void an_ack_puts_the_session_back_to_normal(void) {
    fmx_session *session = ready_client(1000);
    fmx_reaction reaction;

    fmx_session_tick(session, 6000, &reaction);
    FMX_CHECK(reaction.out == FMX_OUT_PROBE);

    feed(session, FMX_FRAME_ACK, NULL, 0, 7000, &reaction);
    FMX_CHECK(reaction.has_event && reaction.event == FMX_EVENT_ACK);

    fmx_session_tick(session, 11000, &reaction);
    FMX_CHECK(reaction.out == FMX_OUT_NONE);
    fmx_session_tick(session, 12000, &reaction);
    FMX_CHECK(reaction.out == FMX_OUT_PROBE);
    fmx_session_destroy(session);
}

static void any_frame_clears_the_probe(void) {
    uint8_t kinds[3] = {FMX_FRAME_DATA, FMX_FRAME_PROBE, FMX_FRAME_ACK};
    size_t index;

    for (index = 0; index < 3; ++index) {
        fmx_session *session = ready_client(1000);
        fmx_reaction reaction;

        fmx_session_tick(session, 6000, &reaction);
        FMX_CHECK(reaction.out == FMX_OUT_PROBE);
        feed(session, kinds[index], NULL, 0, 7000, &reaction);
        fmx_session_tick(session, 21500, &reaction);
        FMX_CHECK(!reaction.has_event);
        fmx_session_destroy(session);
    }
}

static void a_probe_is_always_answered_with_an_ack(void) {
    fmx_session *session = ready_client(1000);
    fmx_reaction reaction;

    feed(session, FMX_FRAME_PROBE, NULL, 0, 1500, &reaction);
    FMX_CHECK(reaction.out == FMX_OUT_ACK);
    FMX_CHECK(reaction.has_event && reaction.event == FMX_EVENT_PROBE);
    fmx_session_destroy(session);
}

static void a_handshaking_client_answers_probes_but_never_sends_one(void) {
    fmx_config config;
    fmx_reaction opening;
    fmx_reaction reaction;
    fmx_session *session;

    fmx_config_defaults(&config);
    session = fmx_session_create(FMX_ROLE_CLIENT, &SCHEMA, &config, 1000, &opening);

    feed(session, FMX_FRAME_PROBE, NULL, 0, 1500, &reaction);
    FMX_CHECK(reaction.out == FMX_OUT_ACK);
    FMX_CHECK(!reaction.has_event);

    fmx_session_tick(session, 4000, &reaction);
    FMX_CHECK(reaction.out == FMX_OUT_NONE);
    fmx_session_destroy(session);
}

static void silence_past_the_response_deadline_declares_the_peer_dead(void) {
    fmx_session *session = ready_client(1000);
    fmx_reaction reaction;

    fmx_session_tick(session, 6000, &reaction);
    FMX_CHECK(reaction.out == FMX_OUT_PROBE);
    fmx_session_tick(session, 20000, &reaction);
    FMX_CHECK(!reaction.has_event);
    fmx_session_tick(session, 21000, &reaction);
    FMX_CHECK(reaction.has_event && reaction.event == FMX_EVENT_DISCONNECTED);
    FMX_CHECK(reaction.reason == FMX_DISCONNECT_UNRESPONSIVE);
    FMX_CHECK(fmx_session_state(session) == FMX_STATE_CLOSED);
    fmx_session_destroy(session);
}

static void a_server_widens_its_window_while_a_peer_is_handshaking(void) {
    fmx_config config;
    fmx_reaction quiet;
    fmx_reaction reaction;
    fmx_session *handshaking;
    fmx_session *patient;

    fmx_config_defaults(&config);
    handshaking = fmx_session_create(FMX_ROLE_SERVER, &SCHEMA, &config, 1000, &quiet);
    fmx_session_tick(handshaking, 6000, &reaction);
    FMX_CHECK(reaction.out == FMX_OUT_PROBE);
    fmx_session_destroy(handshaking);

    config.handshake_timeout_ms = 30000;
    patient = fmx_session_create(FMX_ROLE_SERVER, &SCHEMA, &config, 1000, &quiet);
    fmx_session_tick(patient, 30000, &reaction);
    FMX_CHECK(reaction.out == FMX_OUT_NONE);
    fmx_session_tick(patient, 31000, &reaction);
    FMX_CHECK(reaction.out == FMX_OUT_PROBE);
    fmx_session_destroy(patient);
}

static void a_ready_server_narrows_its_window(void) {
    fmx_session *session = ready_server(1000);
    fmx_reaction reaction;

    fmx_session_tick(session, 5000, &reaction);
    FMX_CHECK(reaction.out == FMX_OUT_NONE);
    fmx_session_tick(session, 6000, &reaction);
    FMX_CHECK(reaction.out == FMX_OUT_PROBE);
    fmx_session_tick(session, 21000, &reaction);
    FMX_CHECK(reaction.has_event && reaction.event == FMX_EVENT_DISCONNECTED);
    fmx_session_destroy(session);
}

static void only_one_terminal_event_is_ever_raised(void) {
    fmx_session *session = ready_client(1000);
    fmx_reaction reaction;

    fmx_session_tick(session, 6000, &reaction);
    fmx_session_tick(session, 21000, &reaction);
    FMX_CHECK(reaction.has_event && reaction.event == FMX_EVENT_DISCONNECTED);

    fmx_session_transport_closed(session, FMX_DISCONNECT_PEER_CLOSED, &reaction);
    FMX_CHECK(!reaction.has_event);
    fmx_session_tick(session, 60000, &reaction);
    FMX_CHECK(!reaction.has_event);
    fmx_session_destroy(session);
}

int main(void) {
    build_schema();
    FMX_RUN(traffic_keeps_the_probe_away);
    FMX_RUN(silence_sends_exactly_one_probe);
    FMX_RUN(an_ack_puts_the_session_back_to_normal);
    FMX_RUN(any_frame_clears_the_probe);
    FMX_RUN(a_probe_is_always_answered_with_an_ack);
    FMX_RUN(a_handshaking_client_answers_probes_but_never_sends_one);
    FMX_RUN(silence_past_the_response_deadline_declares_the_peer_dead);
    FMX_RUN(a_server_widens_its_window_while_a_peer_is_handshaking);
    FMX_RUN(a_ready_server_narrows_its_window);
    FMX_RUN(only_one_terminal_event_is_ever_raised);
    FMX_DONE();
}
