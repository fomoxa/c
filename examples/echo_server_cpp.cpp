#include "cyclone/net.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

#include "game_message_cyclone.h"
#include "game_message_state.h"
#include "player_input_cyclone.h"
#include "player_input_input.h"
#include "schema.h"

static GameMessage STATE = {};

static void step(const PlayerInput &input) {
    STATE.position.x += input.direction.x;
    STATE.position.y += input.direction.y;
    STATE.position.z += input.direction.z;
    if (input.firing && STATE.health >= 10) {
        STATE.health -= 10;
        STATE.is_alive = STATE.health > 0;
    }
}

static void send_state(cyclone::Server &server, uint64_t peer) {
    CycloneWriter writer;

    cyclone_writer_init(&writer);
    if (GameMessageStateCodec_encode(&writer, &STATE)) {
        server.send(peer, GAME_MESSAGE_STATE_MESSAGE_ID,
                    cyclone::ByteView(writer.data, writer.len));
    }
    cyclone_writer_free(&writer);
}

int main() {
    STATE.player_id = 1;
    STATE.player_name = "Knight";
    STATE.health = 100;
    STATE.is_alive = true;

    auto listener = cyclone::Listener::tcp("127.0.0.1", 9321);
    if (!listener) {
        fprintf(stderr, "cannot listen on 127.0.0.1:9321\n");
        return 1;
    }

    cyc_config config = cyclone::default_config();
    config.max_peers = 64;

    auto server = cyclone::Server::create(std::move(*listener), demo_schema(), &config);
    if (!server) {
        fprintf(stderr, "cannot create the server\n");
        return 1;
    }
    printf("cyclone-c++ server listening on 127.0.0.1:9321\n");

    for (;;) {
        for (cyclone::Event event : server->tick(cyclone::now_ms())) {
            switch (event.kind()) {
            case CYC_EVENT_CONNECTED:
                printf("peer#%llu connected\n", (unsigned long long)event.peer());
                break;
            case CYC_EVENT_READY:
                printf("peer#%llu handshake accepted\n", (unsigned long long)event.peer());
                send_state(*server, event.peer());
                break;
            case CYC_EVENT_MESSAGE:
                if (event.message_id() == PLAYER_INPUT_INPUT_MESSAGE_ID) {
                    PlayerInput input = {};
                    CycloneReader reader;
                    cyclone_reader_init(&reader, event.payload().data(), event.payload().size(),
                                        cyclone_limits_unlimited());
                    if (PlayerInputInputCodec_decode(&reader, &input).kind == CYCLONE_DECODE_OK) {
                        step(input);
                        send_state(*server, event.peer());
                    }
                    PlayerInput_free(&input);
                }
                break;
            case CYC_EVENT_HANDSHAKE_FAILED:
                printf("peer#%llu refused: %s\n", (unsigned long long)event.peer(),
                       cyc_handshake_failure_name(event.handshake_failure()));
                break;
            case CYC_EVENT_DISCONNECTED:
                printf("peer#%llu gone: %s\n", (unsigned long long)event.peer(),
                       cyc_disconnect_name(event.disconnect_reason()));
                break;
            case CYC_EVENT_PROBE:
            case CYC_EVENT_ACK:
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}
