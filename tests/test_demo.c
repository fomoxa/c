#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "fmx_test.h"

#include "fomoxa/net.h"

#include "game_message_fomoxa.h"
#include "game_message_state.h"
#include "player_input_input.h"
#include "schema.h"

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

static struct GameMessage sample_state(void) {
    struct GameMessage state;
    memset(&state, 0, sizeof(state));
    state.player_id = 42;
    state.player_name = "Xin ch\xC3\xA0o";
    state.position.x = 10.5f;
    state.position.y = 20.3f;
    state.position.z = -5.1f;
    state.health = 100;
    state.is_alive = true;
    state.last_seen_at = 1700000000u;
    return state;
}

static struct PlayerInput sample_input(void) {
    struct PlayerInput input;
    memset(&input, 0, sizeof(input));
    input.tick = 1234567890123ull;
    input.direction.x = -0.0f;
    input.direction.y = 1.5f;
    input.direction.z = 2.5f;
    input.firing = true;
    return input;
}

static void a_generated_codec_travels_over_a_real_socket(void) {
    const fmx_schema *schema = demo_schema();
    fmx_listener listener;
    fmx_transport transport;
    fmx_server *server;
    fmx_connection *connection;
    FomoxaWriter writer;
    FomoxaReader reader;
    struct GameMessage received_state;
    struct PlayerInput received_input;
    bool got_state = false;
    bool got_input = false;
    bool client_ready = false;
    uint64_t peer = 0;
    bool sent = false;
    uint16_t port;
    int round;

    memset(&received_state, 0, sizeof(received_state));
    memset(&received_input, 0, sizeof(received_input));

    FMX_CHECK(fmx_tcp_listen("127.0.0.1", 0, &listener) == FMX_OK);
    port = fmx_tcp_listener_port(&listener);
    server = fmx_server_create(listener, schema, NULL);
    FMX_CHECK(fmx_tcp_connect("127.0.0.1", port, &transport) == FMX_OK);
    connection = fmx_connection_create(transport, schema, NULL, fmx_now_ms());

    for (round = 0; round < 200 && !(got_state && got_input); ++round) {
        const fmx_event *events;
        size_t count;
        size_t index;
        uint64_t now = fmx_now_ms();

        count = fmx_connection_tick(connection, now);
        events = fmx_connection_events(connection, &count);
        for (index = 0; index < count; ++index) {
            if (events[index].kind == FMX_EVENT_READY) {
                client_ready = true;
            } else if (events[index].kind == FMX_EVENT_MESSAGE &&
                       events[index].message_id == GAME_MESSAGE_STATE_MESSAGE_ID) {
                fomoxa_reader_init(&reader, events[index].payload, events[index].payload_len,
                                    fomoxa_limits_unlimited());
                FMX_CHECK(GameMessageStateCodec_decode(&reader, &received_state).kind ==
                          FOMOXA_DECODE_OK);
                got_state = true;
            }
        }

        count = fmx_server_tick(server, now);
        events = fmx_server_events(server, &count);
        for (index = 0; index < count; ++index) {
            if (events[index].kind == FMX_EVENT_READY) {
                peer = events[index].peer;
            } else if (events[index].kind == FMX_EVENT_MESSAGE &&
                       events[index].message_id == PLAYER_INPUT_INPUT_MESSAGE_ID) {
                fomoxa_reader_init(&reader, events[index].payload, events[index].payload_len,
                                    fomoxa_limits_unlimited());
                FMX_CHECK(PlayerInputInputCodec_decode(&reader, &received_input).kind ==
                          FOMOXA_DECODE_OK);
                got_input = true;
            }
        }

        if (client_ready && peer != 0 && !sent) {
            struct GameMessage state = sample_state();
            struct PlayerInput input = sample_input();

            fomoxa_writer_init(&writer);
            FMX_CHECK(PlayerInputInputCodec_encode(&writer, &input));
            FMX_CHECK(fmx_connection_send(connection, PLAYER_INPUT_INPUT_MESSAGE_ID, writer.data,
                                          writer.len) == FMX_OK);
            fomoxa_writer_free(&writer);

            fomoxa_writer_init(&writer);
            FMX_CHECK(GameMessageStateCodec_encode(&writer, &state));
            FMX_CHECK(fmx_server_send(server, peer, GAME_MESSAGE_STATE_MESSAGE_ID, writer.data,
                                      writer.len) == FMX_OK);
            fomoxa_writer_free(&writer);
            sent = true;
        }
        nap();
    }

    FMX_CHECK(got_input);
    FMX_CHECK(received_input.tick == 1234567890123ull);
    FMX_CHECK(received_input.firing);
    FMX_CHECK(received_input.direction.y == 1.5f);

    FMX_CHECK(got_state);
    FMX_CHECK(received_state.player_id == 42);
    FMX_CHECK(received_state.player_name != NULL &&
              strcmp(received_state.player_name, "Xin ch\xC3\xA0o") == 0);
    FMX_CHECK(received_state.position.x == 10.5f);
    FMX_CHECK(received_state.is_alive);
    FMX_CHECK(received_state.last_seen_at == 0);

    GameMessage_free(&received_state);
    fmx_connection_destroy(connection);
    fmx_server_destroy(server);
}

int main(void) {
    FMX_RUN(a_generated_codec_travels_over_a_real_socket);
    FMX_DONE();
}
