#ifndef FOMOXA_TCP_H
#define FOMOXA_TCP_H

#include <stdbool.h>
#include <stdint.h>

#include "fomoxa/common.h"
#include "fomoxa/transport.h"

#ifdef __cplusplus
extern "C" {
#endif

fmx_result fmx_tcp_connect(const char *host, uint16_t port, fmx_transport *out);
fmx_result fmx_tcp_listen(const char *host, uint16_t port, fmx_listener *out);
uint16_t fmx_tcp_listener_port(const fmx_listener *listener);

#ifdef __cplusplus
}
#endif

#endif /* FOMOXA_TCP_H */
