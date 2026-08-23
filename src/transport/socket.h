#ifndef FOMOXA_INTERNAL_SOCKET_H
#define FOMOXA_INTERNAL_SOCKET_H

#include "../portable.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET fmx_fd;
#define FMX_INVALID_FD INVALID_SOCKET
#else
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef int fmx_fd;
#define FMX_INVALID_FD (-1)
#endif

void fmx_net_startup(void);
void fmx_socket_close(fmx_fd fd);
bool fmx_socket_set_nonblocking(fmx_fd fd);
void fmx_socket_shutdown_write(fmx_fd fd);
int fmx_socket_last_error(void);
bool fmx_socket_would_block(int error);
bool fmx_socket_message_too_long(int error);
bool fmx_socket_reset(int error);
uint16_t fmx_socket_port(fmx_fd fd);

ptrdiff_t fmx_socket_send(fmx_fd fd, const uint8_t *bytes, size_t len);
ptrdiff_t fmx_socket_recv(fmx_fd fd, uint8_t *buffer, size_t cap);
ptrdiff_t fmx_socket_sendto(fmx_fd fd, const uint8_t *bytes, size_t len,
                            const struct sockaddr *address, socklen_t address_len);
ptrdiff_t fmx_socket_recvfrom(fmx_fd fd, uint8_t *buffer, size_t cap, struct sockaddr *address,
                              socklen_t *address_len);

#endif /* FOMOXA_INTERNAL_SOCKET_H */
