#include <stdio.h>

#include "fomoxa/net.h"

static const uint64_t PREFIXES[1] = {0x1234567890abcdefULL};
static const fmx_message_schema MESSAGES[1] = {{7, 0x1234567890abcdefULL, PREFIXES, 1}};
static const fmx_schema SCHEMA = {0xfeedfacecafebeefULL, MESSAGES, 1};

int main(void) {
    fmx_listener listener;
    fmx_server *server;
    fmx_config config;

    fmx_config_defaults(&config);
    if (fmx_schema_check(&SCHEMA) != FMX_OK) {
        return 1;
    }
    if (fmx_tcp_listen("127.0.0.1", 0, &listener) != FMX_OK) {
        return 1;
    }
    server = fmx_server_create(listener, &SCHEMA, &config);
    if (server == NULL) {
        return 1;
    }
    fmx_server_tick(server, fmx_now_ms());
    fmx_server_destroy(server);

    printf("consumed fomoxa from an install prefix\n");
    return 0;
}
