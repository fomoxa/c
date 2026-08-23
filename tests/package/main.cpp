#include <cstdio>

#include "fomoxa/net.hpp"

static const uint64_t PREFIXES[1] = {0x1234567890abcdefULL};
static const fmx_message_schema MESSAGES[1] = {{7, 0x1234567890abcdefULL, PREFIXES, 1}};
static const fmx_schema SCHEMA = {0xfeedfacecafebeefULL, MESSAGES, 1};

int main() {
    auto listener = fomoxa::Listener::tcp("127.0.0.1", 0);
    if (!listener) {
        return 1;
    }

    fmx_config config = fomoxa::default_config();
    auto server = fomoxa::Server::create(std::move(*listener), &SCHEMA, &config);
    if (!server) {
        return 1;
    }
    server->tick(fomoxa::now_ms());

    printf("consumed the fomoxa C++ wrapper from an install prefix\n");
    return 0;
}
