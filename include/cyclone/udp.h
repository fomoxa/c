#ifndef CYCLONE_UDP_H
#define CYCLONE_UDP_H

#include <stdbool.h>
#include <stdint.h>

#include "cyclone/common.h"
#include "cyclone/transport.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CYC_MAX_DATAGRAM ((size_t)65507)

cyc_result cyc_udp_connect(const char *host, uint16_t port, cyc_transport *out);
cyc_result cyc_udp_listen(const char *host, uint16_t port, cyc_listener *out);
uint16_t cyc_udp_listener_port(const cyc_listener *listener);

#ifdef __cplusplus
}
#endif

#endif /* CYCLONE_UDP_H */
