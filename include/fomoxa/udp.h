#ifndef FOMOXA_UDP_H
#define FOMOXA_UDP_H

#include <stdbool.h>
#include <stdint.h>

#include "fomoxa/common.h"
#include "fomoxa/transport.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FMX_MAX_DATAGRAM ((size_t)65507)

fmx_result fmx_udp_connect(const char *host, uint16_t port, fmx_transport *out);
fmx_result fmx_udp_listen(const char *host, uint16_t port, fmx_listener *out);
uint16_t fmx_udp_listener_port(const fmx_listener *listener);

#ifdef __cplusplus
}
#endif

#endif /* FOMOXA_UDP_H */
