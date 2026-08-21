#include "socket.h"

#if !defined(_WIN32)
#include <errno.h>
#include <fcntl.h>
#endif

void cyc_net_startup(void) {
#if defined(_WIN32)
    static int started = 0;
    if (!started) {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) == 0) {
            started = 1;
        }
    }
#endif
}

void cyc_socket_close(cyc_fd fd) {
    if (fd == CYC_INVALID_FD) {
        return;
    }
#if defined(_WIN32)
    closesocket(fd);
#else
    close(fd);
#endif
}

bool cyc_socket_set_nonblocking(cyc_fd fd) {
#if defined(_WIN32)
    u_long mode = 1;
    return ioctlsocket(fd, (long)FIONBIO, &mode) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

void cyc_socket_shutdown_write(cyc_fd fd) {
    if (fd == CYC_INVALID_FD) {
        return;
    }
#if defined(_WIN32)
    shutdown(fd, SD_SEND);
#else
    shutdown(fd, SHUT_WR);
#endif
}

int cyc_socket_last_error(void) {
#if defined(_WIN32)
    return WSAGetLastError();
#else
    return errno;
#endif
}

bool cyc_socket_would_block(int error) {
#if defined(_WIN32)
    return error == WSAEWOULDBLOCK || error == WSAEINTR;
#else
    return error == EAGAIN || error == EWOULDBLOCK || error == EINTR;
#endif
}

bool cyc_socket_message_too_long(int error) {
#if defined(_WIN32)
    return error == WSAEMSGSIZE;
#else
    return error == EMSGSIZE;
#endif
}

bool cyc_socket_reset(int error) {
#if defined(_WIN32)
    return error == WSAECONNRESET;
#else
    return error == ECONNRESET;
#endif
}

uint16_t cyc_socket_port(cyc_fd fd) {
    struct sockaddr_storage address;
    socklen_t length = (socklen_t)sizeof(address);

    if (fd == CYC_INVALID_FD) {
        return 0;
    }
    if (getsockname(fd, (struct sockaddr *)&address, &length) != 0) {
        return 0;
    }
    if (address.ss_family == AF_INET) {
        return ntohs(((struct sockaddr_in *)&address)->sin_port);
    }
    if (address.ss_family == AF_INET6) {
        return ntohs(((struct sockaddr_in6 *)&address)->sin6_port);
    }
    return 0;
}

ptrdiff_t cyc_socket_send(cyc_fd fd, const uint8_t *bytes, size_t len) {
#if defined(_WIN32)
    return (ptrdiff_t)send(fd, (const char *)bytes, (int)len, 0);
#else
    return (ptrdiff_t)send(fd, bytes, len, 0);
#endif
}

ptrdiff_t cyc_socket_recv(cyc_fd fd, uint8_t *buffer, size_t cap) {
#if defined(_WIN32)
    return (ptrdiff_t)recv(fd, (char *)buffer, (int)cap, 0);
#else
    return (ptrdiff_t)recv(fd, buffer, cap, 0);
#endif
}

ptrdiff_t cyc_socket_sendto(cyc_fd fd, const uint8_t *bytes, size_t len,
                            const struct sockaddr *address, socklen_t address_len) {
#if defined(_WIN32)
    return (ptrdiff_t)sendto(fd, (const char *)bytes, (int)len, 0, address, address_len);
#else
    return (ptrdiff_t)sendto(fd, bytes, len, 0, address, address_len);
#endif
}

ptrdiff_t cyc_socket_recvfrom(cyc_fd fd, uint8_t *buffer, size_t cap, struct sockaddr *address,
                              socklen_t *address_len) {
#if defined(_WIN32)
    return (ptrdiff_t)recvfrom(fd, (char *)buffer, (int)cap, 0, address, address_len);
#else
    return (ptrdiff_t)recvfrom(fd, buffer, cap, 0, address, address_len);
#endif
}
