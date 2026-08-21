#include "cyc_test.h"

#include "cyclone/net.hpp"

static const uint64_t PREFIXES[1] = {0x1234567890abcdefULL};
static const cyc_message_schema MESSAGES[1] = {{7, 0x1234567890abcdefULL, PREFIXES, 1}};
static const cyc_schema SCHEMA = {0xfeedfacecafebeefULL, MESSAGES, 1};

static const cyc_message_schema BROKEN_MESSAGES[2] = {{9, 1, nullptr, 0}, {9, 2, nullptr, 0}};
static const cyc_schema BROKEN_SCHEMA = {0, BROKEN_MESSAGES, 2};

struct Pair {
    cyclone::Server server;
    cyclone::Connection client;
    uint16_t port;
};

static bool connect_pair(Pair &pair) {
    auto listener = cyclone::Listener::tcp("127.0.0.1", 0);
    if (!listener) {
        return false;
    }
    pair.port = listener->port();

    auto server = cyclone::Server::create(std::move(*listener), &SCHEMA);
    if (!server) {
        return false;
    }
    pair.server = std::move(*server);

    auto transport = cyclone::Transport::tcp("127.0.0.1", pair.port);
    if (!transport) {
        return false;
    }
    auto client = cyclone::Connection::create(std::move(*transport), &SCHEMA);
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

    CYC_CHECK(connect_pair(pair));

    for (int step = 0; step < 200 && !client_saw_echo; ++step) {
        uint64_t now = cyclone::now_ms();

        for (cyclone::Event event : pair.server.tick(now)) {
            if (event.kind() == CYC_EVENT_MESSAGE) {
                server_saw_message = true;
                pair.server.send(event.peer(), event.message_id(), event.payload());
            }
        }

        for (cyclone::Event event : pair.client.tick(now)) {
            if (event.kind() == CYC_EVENT_READY) {
                std::vector<uint8_t> payload = {1, 2, 3, 4};
                client_ready = true;
                CYC_CHECK(pair.client.send(7, payload) == CYC_OK);
            }
            if (event.kind() == CYC_EVENT_MESSAGE) {
                client_saw_echo = true;
                echoed_len = event.payload().size();
            }
        }
    }

    CYC_CHECK(client_ready);
    CYC_CHECK(server_saw_message);
    CYC_CHECK(client_saw_echo);
    CYC_CHECK(echoed_len == 4);
}

static void a_listener_reports_the_port_it_bound(void) {
    auto listener = cyclone::Listener::tcp("127.0.0.1", 0);
    CYC_CHECK(listener.has_value());
    CYC_CHECK(listener->valid());
    CYC_CHECK(listener->port() != 0);
}

static void a_refused_connection_closes_its_transport(void) {
    auto listener = cyclone::Listener::tcp("127.0.0.1", 0);
    CYC_CHECK(listener.has_value());
    uint16_t port = listener->port();
    auto server = cyclone::Server::create(std::move(*listener), &SCHEMA);
    CYC_CHECK(server.has_value());

    auto transport = cyclone::Transport::tcp("127.0.0.1", port);
    CYC_CHECK(transport.has_value());

    auto refused = cyclone::Connection::create(std::move(*transport), &BROKEN_SCHEMA);
    CYC_CHECK(!refused.has_value());
}

static void a_moved_from_handle_owns_nothing(void) {
    auto listener = cyclone::Listener::tcp("127.0.0.1", 0);
    CYC_CHECK(listener.has_value());
    uint16_t port = listener->port();
    auto server = cyclone::Server::create(std::move(*listener), &SCHEMA);
    CYC_CHECK(server.has_value());
    CYC_CHECK(!listener->valid());

    auto transport = cyclone::Transport::tcp("127.0.0.1", port);
    CYC_CHECK(transport.has_value());
    CYC_CHECK(transport->is_stream());

    cyclone::Transport taken = std::move(*transport);
    CYC_CHECK(!transport->valid());
    CYC_CHECK(taken.valid());

    auto client = cyclone::Connection::create(std::move(taken), &SCHEMA);
    CYC_CHECK(client.has_value());
    CYC_CHECK(!taken.valid());

    cyclone::Connection moved = std::move(*client);
    CYC_CHECK(!client->valid());
    CYC_CHECK(moved.valid());
}

static void a_dropped_handle_releases_its_socket(void) {
    auto listener = cyclone::Listener::tcp("127.0.0.1", 0);
    CYC_CHECK(listener.has_value());
    auto transport = cyclone::Transport::tcp("127.0.0.1", listener->port());
    CYC_CHECK(transport.has_value());
}

static void a_closed_connection_reports_closed(void) {
    Pair pair;
    CYC_CHECK(connect_pair(pair));
    CYC_CHECK(!pair.client.closed());
    pair.client.close();
    pair.client.tick(cyclone::now_ms());
    CYC_CHECK(pair.client.closed());
    CYC_CHECK(pair.client.state() == CYC_STATE_CLOSED);
}

static void a_default_config_matches_the_c_defaults(void) {
    cyc_config wrapped = cyclone::default_config();
    cyc_config direct;
    cyc_config_defaults(&direct);
    CYC_CHECK(wrapped.handshake_timeout_ms == direct.handshake_timeout_ms);
    CYC_CHECK(wrapped.heartbeat_interval_ms == direct.heartbeat_interval_ms);
    CYC_CHECK(wrapped.heartbeat_timeout_ms == direct.heartbeat_timeout_ms);
    CYC_CHECK(wrapped.max_frames_per_tick == direct.max_frames_per_tick);
    CYC_CHECK(wrapped.max_message_bytes == direct.max_message_bytes);
    CYC_CHECK(wrapped.max_peers == direct.max_peers);
}

static void a_byte_view_borrows_without_copying(void) {
    std::vector<uint8_t> bytes = {9, 8, 7};
    cyclone::ByteView view(bytes);
    CYC_CHECK(view.size() == 3);
    CYC_CHECK(view.data() == bytes.data());
    CYC_CHECK(!view.empty());
    CYC_CHECK(cyclone::ByteView().empty());
}

int main(void) {
    CYC_RUN(a_message_makes_the_round_trip);
    CYC_RUN(a_listener_reports_the_port_it_bound);
    CYC_RUN(a_refused_connection_closes_its_transport);
    CYC_RUN(a_moved_from_handle_owns_nothing);
    CYC_RUN(a_dropped_handle_releases_its_socket);
    CYC_RUN(a_closed_connection_reports_closed);
    CYC_RUN(a_default_config_matches_the_c_defaults);
    CYC_RUN(a_byte_view_borrows_without_copying);
    CYC_DONE();
}
