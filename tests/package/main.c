#include <stdio.h>

#include "cyclone/net.h"

static const uint64_t PREFIXES[1] = {0x1234567890abcdefULL};
static const cyc_message_schema MESSAGES[1] = {{7, 0x1234567890abcdefULL, PREFIXES, 1}};
static const cyc_schema SCHEMA = {0xfeedfacecafebeefULL, MESSAGES, 1};

int main(void) {
    cyc_listener listener;
    cyc_server *server;
    cyc_config config;

    cyc_config_defaults(&config);
    if (cyc_schema_check(&SCHEMA) != CYC_OK) {
        return 1;
    }
    if (cyc_tcp_listen("127.0.0.1", 0, &listener) != CYC_OK) {
        return 1;
    }
    server = cyc_server_create(listener, &SCHEMA, &config);
    if (server == NULL) {
        return 1;
    }
    cyc_server_tick(server, cyc_now_ms());
    cyc_server_destroy(server);

    printf("consumed cyclone from an install prefix\n");
    return 0;
}
