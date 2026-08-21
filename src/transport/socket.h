#ifndef CYCLONE_INTERNAL_SOCKET_H
#define CYCLONE_INTERNAL_SOCKET_H

#include "../portable.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET cyc_fd;
#define CYC_INVALID_FD INVALID_SOCKET
#else
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef int cyc_fd;
#define CYC_INVALID_FD (-1)
#endif

void cyc_net_startup(void);
void cyc_socket_close(cyc_fd fd);
bool cyc_socket_set_nonblocking(cyc_fd fd);
void cyc_socket_shutdown_write(cyc_fd fd);
int cyc_socket_last_error(void);
bool cyc_socket_would_block(int error);
bool cyc_socket_message_too_long(int error);
bool cyc_socket_reset(int error);
uint16_t cyc_socket_port(cyc_fd fd);

ptrdiff_t cyc_socket_send(cyc_fd fd, const uint8_t *bytes, size_t len);
ptrdiff_t cyc_socket_recv(cyc_fd fd, uint8_t *buffer, size_t cap);
ptrdiff_t cyc_socket_sendto(cyc_fd fd, const uint8_t *bytes, size_t len,
                            const struct sockaddr *address, socklen_t address_len);
ptrdiff_t cyc_socket_recvfrom(cyc_fd fd, uint8_t *buffer, size_t cap, struct sockaddr *address,
                              socklen_t *address_len);

#endif /* CYCLONE_INTERNAL_SOCKET_H */
