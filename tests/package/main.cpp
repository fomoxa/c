#include <cstdio>

#include "cyclone/net.hpp"

static const uint64_t PREFIXES[1] = {0x1234567890abcdefULL};
static const cyc_message_schema MESSAGES[1] = {{7, 0x1234567890abcdefULL, PREFIXES, 1}};
static const cyc_schema SCHEMA = {0xfeedfacecafebeefULL, MESSAGES, 1};

int main() {
    auto listener = cyclone::Listener::tcp("127.0.0.1", 0);
    if (!listener) {
        return 1;
    }

    cyc_config config = cyclone::default_config();
    auto server = cyclone::Server::create(std::move(*listener), &SCHEMA, &config);
    if (!server) {
        return 1;
    }
    server->tick(cyclone::now_ms());

    printf("consumed the cyclone C++ wrapper from an install prefix\n");
    return 0;
}
