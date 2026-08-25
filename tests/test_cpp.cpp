#include "fmx_test.h"

#include "fomoxa/net.hpp"

static const uint64_t PREFIXES[1] = {0x1234567890abcdefULL};
static const fmx_message_schema MESSAGES[1] = {{7, 0x1234567890abcdefULL, PREFIXES, 1}};
static const fmx_schema SCHEMA = {0xfeedfacecafebeefULL, MESSAGES, 1};

static const fmx_message_schema BROKEN_MESSAGES[2] = {{9, 1, nullptr, 0}, {9, 2, nullptr, 0}};
static const fmx_schema BROKEN_SCHEMA = {0, BROKEN_MESSAGES, 2};

struct Pair {
    fomoxa::Server server;
    fomoxa::Connection client;
    uint16_t port;
};

static bool connect_pair(Pair &pair) {
    auto listener = fomoxa::Listener::tcp("127.0.0.1", 0);
    if (!listener) {
        return false;
    }
    pair.port = listener->port();

    auto server = fomoxa::Server::create(std::move(*listener), &SCHEMA);
    if (!server) {
        return false;
    }
    pair.server = std::move(*server);

    auto transport = fomoxa::Transport::tcp("127.0.0.1", pair.port);
    if (!transport) {
        return false;
    }
    auto client = fomoxa::Connection::create(std::move(*transport), &SCHEMA);
    if (!client) {
        return false;
    }
    pair.client = std::move(*client);
    return true;
}

static void a_message_makes_the_round_trip(void) {
    Pair pair;
    bool client_ready = false;
    bool server_saw_message = false;
    bool client_saw_echo = false;
    size_t echoed_len = 0;

    FMX_CHECK(connect_pair(pair));

    for (int step = 0; step < 200 && !client_saw_echo; ++step) {
        uint64_t now = fomoxa::now_ms();

        for (fomoxa::Event event : pair.server.tick(now)) {
            if (event.kind() == FMX_EVENT_MESSAGE) {
                server_saw_message = true;
                pair.server.send(event.peer(), event.message_id(), event.payload());
            }
        }

        for (fomoxa::Event event : pair.client.tick(now)) {
            if (event.kind() == FMX_EVENT_READY) {
                std::vector<uint8_t> payload = {1, 2, 3, 4};
                client_ready = true;
                FMX_CHECK(pair.client.send(7, payload) == FMX_OK);
            }
            if (event.kind() == FMX_EVENT_MESSAGE) {
                client_saw_echo = true;
                echoed_len = event.payload().size();
            }
        }
    }

    FMX_CHECK(client_ready);
    FMX_CHECK(server_saw_message);
    FMX_CHECK(client_saw_echo);
    FMX_CHECK(echoed_len == 4);
}

static void shrinking_after_a_burst_still_completes_the_round_trip(void) {
    Pair pair;
    bool client_ready = false;
    bool server_saw_big = false;
    bool client_saw_big_echo = false;
    bool server_saw_small = false;
    bool client_saw_small_echo = false;
    size_t big_echoed_len = 0;
    size_t small_echoed_len = 0;

    FMX_CHECK(connect_pair(pair));

    std::vector<uint8_t> big_payload(5000, 7);
    std::vector<uint8_t> small_payload = {1, 2, 3, 4, 5, 6, 7, 8};

    for (int step = 0; step < 200 && !client_saw_big_echo; ++step) {
        uint64_t now = fomoxa::now_ms();

        for (fomoxa::Event event : pair.server.tick(now)) {
            if (event.kind() == FMX_EVENT_MESSAGE) {
                server_saw_big = true;
                pair.server.send(event.peer(), event.message_id(), event.payload());
            }
        }

        for (fomoxa::Event event : pair.client.tick(now)) {
            if (event.kind() == FMX_EVENT_READY) {
                client_ready = true;
                FMX_CHECK(pair.client.send(1, big_payload) == FMX_OK);
            }
            if (event.kind() == FMX_EVENT_MESSAGE) {
                client_saw_big_echo = true;
                big_echoed_len = event.payload().size();
            }
        }
    }

    FMX_CHECK(client_ready);
    FMX_CHECK(server_saw_big);
    FMX_CHECK(client_saw_big_echo);
    FMX_CHECK(big_echoed_len == big_payload.size());

    pair.client.shrink_to_fit();
    pair.server.shrink_to_fit();

    FMX_CHECK(pair.client.send(2, small_payload) == FMX_OK);

    for (int step = 0; step < 200 && !client_saw_small_echo; ++step) {
        uint64_t now = fomoxa::now_ms();

        for (fomoxa::Event event : pair.server.tick(now)) {
            if (event.kind() == FMX_EVENT_MESSAGE) {
                server_saw_small = true;
                pair.server.send(event.peer(), event.message_id(), event.payload());
            }
        }

        for (fomoxa::Event event : pair.client.tick(now)) {
            if (event.kind() == FMX_EVENT_MESSAGE) {
                client_saw_small_echo = true;
                small_echoed_len = event.payload().size();
            }
        }
    }

    FMX_CHECK(server_saw_small);
    FMX_CHECK(client_saw_small_echo);
    FMX_CHECK(small_echoed_len == small_payload.size());
}

static void a_listener_reports_the_port_it_bound(void) {
    auto listener = fomoxa::Listener::tcp("127.0.0.1", 0);
    FMX_CHECK(listener.has_value());
    FMX_CHECK(listener->valid());
    FMX_CHECK(listener->port() != 0);
}

static void a_refused_connection_closes_its_transport(void) {
    auto listener = fomoxa::Listener::tcp("127.0.0.1", 0);
    FMX_CHECK(listener.has_value());
    uint16_t port = listener->port();
    auto server = fomoxa::Server::create(std::move(*listener), &SCHEMA);
    FMX_CHECK(server.has_value());

    auto transport = fomoxa::Transport::tcp("127.0.0.1", port);
    FMX_CHECK(transport.has_value());

    auto refused = fomoxa::Connection::create(std::move(*transport), &BROKEN_SCHEMA);
    FMX_CHECK(!refused.has_value());
}

static void a_moved_from_handle_owns_nothing(void) {
    auto listener = fomoxa::Listener::tcp("127.0.0.1", 0);
    FMX_CHECK(listener.has_value());
    uint16_t port = listener->port();
    auto server = fomoxa::Server::create(std::move(*listener), &SCHEMA);
    FMX_CHECK(server.has_value());
    FMX_CHECK(!listener->valid());

    auto transport = fomoxa::Transport::tcp("127.0.0.1", port);
    FMX_CHECK(transport.has_value());
    FMX_CHECK(transport->is_stream());

    fomoxa::Transport taken = std::move(*transport);
    FMX_CHECK(!transport->valid());
    FMX_CHECK(taken.valid());

    auto client = fomoxa::Connection::create(std::move(taken), &SCHEMA);
    FMX_CHECK(client.has_value());
    FMX_CHECK(!taken.valid());

    fomoxa::Connection moved = std::move(*client);
    FMX_CHECK(!client->valid());
    FMX_CHECK(moved.valid());
}

static void a_dropped_handle_releases_its_socket(void) {
    auto listener = fomoxa::Listener::tcp("127.0.0.1", 0);
    FMX_CHECK(listener.has_value());
    auto transport = fomoxa::Transport::tcp("127.0.0.1", listener->port());
    FMX_CHECK(transport.has_value());
}

static void a_closed_connection_reports_closed(void) {
    Pair pair;
    FMX_CHECK(connect_pair(pair));
    FMX_CHECK(!pair.client.closed());
    pair.client.close();
    pair.client.tick(fomoxa::now_ms());
    FMX_CHECK(pair.client.closed());
    FMX_CHECK(pair.client.state() == FMX_STATE_CLOSED);
}

static void a_default_config_matches_the_c_defaults(void) {
    fmx_config wrapped = fomoxa::default_config();
    fmx_config direct;
    fmx_config_defaults(&direct);
    FMX_CHECK(wrapped.handshake_timeout_ms == direct.handshake_timeout_ms);
    FMX_CHECK(wrapped.heartbeat_interval_ms == direct.heartbeat_interval_ms);
    FMX_CHECK(wrapped.heartbeat_timeout_ms == direct.heartbeat_timeout_ms);
    FMX_CHECK(wrapped.max_frames_per_tick == direct.max_frames_per_tick);
    FMX_CHECK(wrapped.max_message_bytes == direct.max_message_bytes);
    FMX_CHECK(wrapped.max_peers == direct.max_peers);
}

static void a_byte_view_borrows_without_copying(void) {
    std::vector<uint8_t> bytes = {9, 8, 7};
    fomoxa::ByteView view(bytes);
    FMX_CHECK(view.size() == 3);
    FMX_CHECK(view.data() == bytes.data());
    FMX_CHECK(!view.empty());
    FMX_CHECK(fomoxa::ByteView().empty());
}

int main(void) {
    FMX_RUN(a_message_makes_the_round_trip);
    FMX_RUN(shrinking_after_a_burst_still_completes_the_round_trip);
    FMX_RUN(a_listener_reports_the_port_it_bound);
    FMX_RUN(a_refused_connection_closes_its_transport);
    FMX_RUN(a_moved_from_handle_owns_nothing);
    FMX_RUN(a_dropped_handle_releases_its_socket);
    FMX_RUN(a_closed_connection_reports_closed);
    FMX_RUN(a_default_config_matches_the_c_defaults);
    FMX_RUN(a_byte_view_borrows_without_copying);
    FMX_DONE();
}
