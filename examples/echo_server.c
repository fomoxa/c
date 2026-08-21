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

static struct GameMessage STATE;

static void step(const struct PlayerInput *input) {
    STATE.position.x += input->direction.x;
    STATE.position.y += input->direction.y;
    STATE.position.z += input->direction.z;
    if (input->firing && STATE.health >= 10) {
        STATE.health -= 10;
        STATE.is_alive = STATE.health > 0;
    }
}

static void send_state(cyc_server *server, uint64_t peer) {
    CycloneWriter writer;

    cyclone_writer_init(&writer);
    if (GameMessageStateCodec_encode(&writer, &STATE)) {
        (void)cyc_server_send(server, peer, GAME_MESSAGE_STATE_MESSAGE_ID, writer.data, writer.len);
    }
    cyclone_writer_free(&writer);
}

int main(void) {
    cyc_listener listener;
    cyc_server *server;

    memset(&STATE, 0, sizeof(STATE));
    STATE.player_id = 1;
    STATE.player_name = "Knight";
    STATE.position.x = 10.5f;
    STATE.position.y = 20.3f;
    STATE.position.z = -5.1f;
    STATE.health = 100;
    STATE.is_alive = true;

    if (cyc_tcp_listen("127.0.0.1", PORT, &listener) != CYC_OK) {
        fprintf(stderr, "cannot listen on 127.0.0.1:%d\n", PORT);
        return 1;
    }
    server = cyc_server_create(listener, demo_schema(), NULL);
    if (server == NULL) {
        fprintf(stderr, "cannot create the server\n");
        return 1;
    }
    printf("cyclone-c server listening on 127.0.0.1:%d\n", PORT);

    for (;;) {
        const cyc_event *events;
        size_t count = cyc_server_tick(server, cyc_now_ms());
        size_t index;

        events = cyc_server_events(server, &count);
        for (index = 0; index < count; ++index) {
            const cyc_event *event = &events[index];

            switch (event->kind) {
            case CYC_EVENT_CONNECTED:
                printf("peer#%llu connected\n", (unsigned long long)event->peer);
                break;
            case CYC_EVENT_READY:
                printf("peer#%llu handshake accepted\n", (unsigned long long)event->peer);
                send_state(server, event->peer);
                break;
            case CYC_EVENT_MESSAGE:
                if (event->message_id == PLAYER_INPUT_INPUT_MESSAGE_ID) {
                    struct PlayerInput input;
                    CycloneReader reader;
                    memset(&input, 0, sizeof(input));
                    cyclone_reader_init(&reader, event->payload, event->payload_len,
                                        cyclone_limits_unlimited());
                    if (PlayerInputInputCodec_decode(&reader, &input).kind == CYCLONE_DECODE_OK) {
                        step(&input);
                        printf("peer#%llu input at tick %llu -> hp %u\n",
                               (unsigned long long)event->peer, (unsigned long long)input.tick,
                               (unsigned)STATE.health);
                        send_state(server, event->peer);
                    }
                    PlayerInput_free(&input);
                }
                break;
            case CYC_EVENT_HANDSHAKE_FAILED:
                printf("peer#%llu refused: %s\n", (unsigned long long)event->peer,
                       cyc_handshake_failure_name((cyc_handshake_failure)event->reason));
                break;
            case CYC_EVENT_DISCONNECTED:
                printf("peer#%llu gone: %s\n", (unsigned long long)event->peer,
                       cyc_disconnect_name((cyc_disconnect)event->reason));
                break;
            case CYC_EVENT_PROBE:
            case CYC_EVENT_ACK:
                break;
            }
        }
        nap();
    }
}
