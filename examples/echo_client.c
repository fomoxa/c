#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <string.h>

#include "cyclone/net.h"

#include "game_message_cyclone.h"
#include "game_message_state.h"
#include "player_input_cyclone.h"
#include "player_input_input.h"
#include "schema.h"

#if defined(_WIN32)
#include <windows.h>
static void nap(void) {
    Sleep(16);
}
#else
#include <time.h>
static void nap(void) {
    struct timespec pause;
    pause.tv_sec = 0;
    pause.tv_nsec = 16000000L;
    nanosleep(&pause, NULL);
}
#endif

#define PORT 9321

static void send_input(cyc_connection *connection, uint64_t tick) {
    struct PlayerInput input;
    CycloneWriter writer;

    memset(&input, 0, sizeof(input));
    input.tick = tick;
    input.direction.x = 1.0f;
    input.direction.z = -0.5f;
    input.firing = tick % 120 == 0;

    cyclone_writer_init(&writer);
    if (PlayerInputInputCodec_encode(&writer, &input)) {
        cyc_result sent =
            cyc_connection_send(connection, PLAYER_INPUT_INPUT_MESSAGE_ID, writer.data, writer.len);
        if (sent != CYC_OK) {
            printf("send refused: %s\n", cyc_result_name(sent));
        }
    }
    cyclone_writer_free(&writer);
}

int main(void) {
    cyc_transport transport;
    cyc_connection *connection;
    uint64_t tick = 0;
    bool ready = false;
    bool done = false;

    if (cyc_tcp_connect("127.0.0.1", PORT, &transport) != CYC_OK) {
        fprintf(stderr, "cannot reach 127.0.0.1:%d - is the server example running?\n", PORT);
        return 1;
    }
    connection = cyc_connection_create(transport, demo_schema(), NULL, cyc_now_ms());
    if (connection == NULL) {
        fprintf(stderr, "cannot create the connection\n");
        return 1;
    }
    printf("cyclone-c client connected to 127.0.0.1:%d\n", PORT);

    while (!done) {
        const cyc_event *events;
        size_t count = cyc_connection_tick(connection, cyc_now_ms());
        size_t index;

        events = cyc_connection_events(connection, &count);
        for (index = 0; index < count; ++index) {
            const cyc_event *event = &events[index];

            switch (event->kind) {
            case CYC_EVENT_CONNECTED:
                printf("transport up, handshaking\n");
                break;
            case CYC_EVENT_READY:
                printf("handshake accepted\n");
                ready = true;
                break;
            case CYC_EVENT_MESSAGE:
                if (event->message_id == GAME_MESSAGE_STATE_MESSAGE_ID) {
                    struct GameMessage state;
                    CycloneReader reader;
                    memset(&state, 0, sizeof(state));
                    cyclone_reader_init(&reader, event->payload, event->payload_len,
                                        cyclone_limits_unlimited());
                    if (GameMessageStateCodec_decode(&reader, &state).kind == CYCLONE_DECODE_OK) {
                        printf("%s at (%.1f, %.1f, %.1f) hp %u alive %d\n",
                               state.player_name != NULL ? state.player_name : "",
                               (double)state.position.x, (double)state.position.y,
                               (double)state.position.z, (unsigned)state.health,
                               state.is_alive ? 1 : 0);
                    }
                    GameMessage_free(&state);
                }
                break;
            case CYC_EVENT_HANDSHAKE_FAILED:
                printf("handshake refused: %s\n",
                       cyc_handshake_failure_name((cyc_handshake_failure)event->reason));
                done = true;
                break;
            case CYC_EVENT_DISCONNECTED:
                printf("disconnected: %s\n", cyc_disconnect_name((cyc_disconnect)event->reason));
                done = true;
                break;
            case CYC_EVENT_PROBE:
            case CYC_EVENT_ACK:
                break;
            }
        }

        if (ready && tick % 60 == 0) {
            send_input(connection, tick);
        }
        tick += 1;
        nap();
    }

    cyc_connection_destroy(connection);
    return 0;
}
