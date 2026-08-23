#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <string.h>

#include "fomoxa/net.h"

#include "game_message_fomoxa.h"
#include "game_message_state.h"
#include "player_input_fomoxa.h"
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

static void send_state(fmx_server *server, uint64_t peer) {
    FomoxaWriter writer;

    fomoxa_writer_init(&writer);
    if (GameMessageStateCodec_encode(&writer, &STATE)) {
        (void)fmx_server_send(server, peer, GAME_MESSAGE_STATE_MESSAGE_ID, writer.data, writer.len);
    }
    fomoxa_writer_free(&writer);
}

int main(void) {
    fmx_listener listener;
    fmx_server *server;

    memset(&STATE, 0, sizeof(STATE));
    STATE.player_id = 1;
    STATE.player_name = "Knight";
    STATE.position.x = 10.5f;
    STATE.position.y = 20.3f;
    STATE.position.z = -5.1f;
    STATE.health = 100;
    STATE.is_alive = true;

    if (fmx_tcp_listen("127.0.0.1", PORT, &listener) != FMX_OK) {
        fprintf(stderr, "cannot listen on 127.0.0.1:%d\n", PORT);
        return 1;
    }
    server = fmx_server_create(listener, demo_schema(), NULL);
    if (server == NULL) {
        fprintf(stderr, "cannot create the server\n");
        return 1;
    }
    printf("fomoxa-c server listening on 127.0.0.1:%d\n", PORT);

    for (;;) {
        const fmx_event *events;
        size_t count = fmx_server_tick(server, fmx_now_ms());
        size_t index;

        events = fmx_server_events(server, &count);
        for (index = 0; index < count; ++index) {
            const fmx_event *event = &events[index];

            switch (event->kind) {
            case FMX_EVENT_CONNECTED:
                printf("peer#%llu connected\n", (unsigned long long)event->peer);
                break;
            case FMX_EVENT_READY:
                printf("peer#%llu handshake accepted\n", (unsigned long long)event->peer);
                send_state(server, event->peer);
                break;
            case FMX_EVENT_MESSAGE:
                if (event->message_id == PLAYER_INPUT_INPUT_MESSAGE_ID) {
                    struct PlayerInput input;
                    FomoxaReader reader;
                    memset(&input, 0, sizeof(input));
                    fomoxa_reader_init(&reader, event->payload, event->payload_len,
                                        fomoxa_limits_unlimited());
                    if (PlayerInputInputCodec_decode(&reader, &input).kind == FOMOXA_DECODE_OK) {
                        step(&input);
                        printf("peer#%llu input at tick %llu -> hp %u\n",
                               (unsigned long long)event->peer, (unsigned long long)input.tick,
                               (unsigned)STATE.health);
                        send_state(server, event->peer);
                    }
                    PlayerInput_free(&input);
                }
                break;
            case FMX_EVENT_HANDSHAKE_FAILED:
                printf("peer#%llu refused: %s\n", (unsigned long long)event->peer,
                       fmx_handshake_failure_name((fmx_handshake_failure)event->reason));
                break;
            case FMX_EVENT_DISCONNECTED:
                printf("peer#%llu gone: %s\n", (unsigned long long)event->peer,
                       fmx_disconnect_name((fmx_disconnect)event->reason));
                break;
            case FMX_EVENT_PROBE:
            case FMX_EVENT_ACK:
                break;
            }
        }
        nap();
    }
}
