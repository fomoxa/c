#ifndef CYCLONE_TCP_H
#define CYCLONE_TCP_H

#include <stdbool.h>
#include <stdint.h>

#include "cyclone/common.h"
#include "cyclone/transport.h"

#ifdef __cplusplus
extern "C" {
#endif

cyc_result cyc_tcp_connect(const char *host, uint16_t port, cyc_transport *out);
cyc_result cyc_tcp_listen(const char *host, uint16_t port, cyc_listener *out);
uint16_t cyc_tcp_listener_port(const cyc_listener *listener);

#ifdef __cplusplus
}
#endif

#endif /* CYCLONE_TCP_H */
