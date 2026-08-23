#include "fomoxa/net.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

#include "game_message_fomoxa.h"
#include "game_message_state.h"
#include "player_input_fomoxa.h"
#include "player_input_input.h"
#include "schema.h"

static void send_input(fomoxa::Connection &connection, uint64_t tick) {
    PlayerInput input = {};
    FomoxaWriter writer;

    input.tick = tick;
    input.direction.x = 1.0f;
    input.direction.z = -0.5f;
    input.firing = tick % 120 == 0;

    fomoxa_writer_init(&writer);
    if (PlayerInputInputCodec_encode(&writer, &input)) {
        fmx_result sent = connection.send(PLAYER_INPUT_INPUT_MESSAGE_ID,
                                          fomoxa::ByteView(writer.data, writer.len));
        if (sent != FMX_OK) {
            printf("send refused: %s\n", fmx_result_name(sent));
        }
    }
    fomoxa_writer_free(&writer);
}

int main() {
    auto transport = fomoxa::Transport::tcp("127.0.0.1", 9321);
    if (!transport) {
        fprintf(stderr, "cannot reach 127.0.0.1:9321\n");
        return 1;
    }

    auto connection = fomoxa::Connection::create(std::move(*transport), demo_schema());
    if (!connection) {
        fprintf(stderr, "cannot create the connection\n");
        return 1;
    }

    uint64_t tick = 0;
    bool ready = false;

    while (!connection->closed()) {
        for (fomoxa::Event event : connection->tick(fomoxa::now_ms())) {
            switch (event.kind()) {
            case FMX_EVENT_CONNECTED:
                printf("transport up, handshaking\n");
                break;
            case FMX_EVENT_READY:
                printf("handshake accepted\n");
                ready = true;
                break;
            case FMX_EVENT_MESSAGE:
                if (event.message_id() == GAME_MESSAGE_STATE_MESSAGE_ID) {
                    GameMessage state = {};
                    FomoxaReader reader;
                    fomoxa_reader_init(&reader, event.payload().data(), event.payload().size(),
                                        fomoxa_limits_unlimited());
                    if (GameMessageStateCodec_decode(&reader, &state).kind == FOMOXA_DECODE_OK) {
                        printf("%s hp %u\n", state.player_name ? state.player_name : "",
                               (unsigned)state.health);
                    }
                    GameMessage_free(&state);
                }
                break;
            case FMX_EVENT_HANDSHAKE_FAILED:
                printf("handshake refused: %s\n",
                       fmx_handshake_failure_name(event.handshake_failure()));
                break;
            case FMX_EVENT_DISCONNECTED:
                printf("disconnected: %s\n", fmx_disconnect_name(event.disconnect_reason()));
                break;
            case FMX_EVENT_PROBE:
            case FMX_EVENT_ACK:
                break;
            }
        }

        if (ready && tick % 60 == 0) {
            send_input(*connection, tick);
        }
        tick += 1;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    return 0;
}
