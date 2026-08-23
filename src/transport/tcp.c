#include "socket.h"

#include "fomoxa/tcp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef struct fmx_tcp {
    fmx_fd fd;
    bool closed;
} fmx_tcp;

typedef struct fmx_tcp_listener {
    fmx_fd fd;
} fmx_tcp_listener;

static struct addrinfo *resolve(const char *host, uint16_t port, int socktype, bool passive) {
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    char service[16];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socktype;
    if (passive) {
        hints.ai_flags = AI_PASSIVE;
    }
    (void)snprintf(service, sizeof(service), "%u", (unsigned)port);
    if (getaddrinfo(host, service, &hints, &result) != 0) {
        return NULL;
    }
    return result;
}

static fmx_transport_kind tcp_kind(const fmx_transport *transport) {
    (void)transport;
    return FMX_TRANSPORT_STREAM;
}

static fmx_send_result tcp_send(fmx_transport *transport, const uint8_t *bytes, size_t len,
                                size_t *accepted) {
    fmx_tcp *tcp = (fmx_tcp *)transport->state;
    ptrdiff_t written;

    *accepted = 0;
    if (tcp->closed) {
        return FMX_SEND_CLOSED;
    }
    if (len == 0) {
        return FMX_SEND_SENT;
    }

    written = fmx_socket_send(tcp->fd, bytes, len);
    if (written < 0) {
        int error = fmx_socket_last_error();
        if (fmx_socket_would_block(error)) {
            return FMX_SEND_WOULD_BLOCK;
        }
        tcp->closed = true;
        return FMX_SEND_ERROR;
    }
    if (written == 0) {
        return FMX_SEND_WOULD_BLOCK;
    }
    *accepted = (size_t)written;
    return (size_t)written == len ? FMX_SEND_SENT : FMX_SEND_PARTIAL;
}

static fmx_recv_result tcp_recv(fmx_transport *transport, uint8_t *buffer, size_t cap,
                                size_t *received, size_t *needed) {
    fmx_tcp *tcp = (fmx_tcp *)transport->state;
    ptrdiff_t count;

    *received = 0;
    *needed = 0;
    if (tcp->closed) {
        return FMX_RECV_CLOSED;
    }

    count = fmx_socket_recv(tcp->fd, buffer, cap);
    if (count < 0) {
        int error = fmx_socket_last_error();
        if (fmx_socket_would_block(error)) {
            return FMX_RECV_WOULD_BLOCK;
        }
        tcp->closed = true;
        return FMX_RECV_ERROR;
    }
    if (count == 0) {
        tcp->closed = true;
        return FMX_RECV_CLOSED;
    }
    *received = (size_t)count;
    return FMX_RECV_RECEIVED;
}

static void tcp_close_soft(fmx_transport *transport) {
    fmx_tcp *tcp = (fmx_tcp *)transport->state;
    if (!tcp->closed) {
        fmx_socket_shutdown_write(tcp->fd);
    }
}

static void tcp_close_hard(fmx_transport *transport) {
    fmx_tcp *tcp = (fmx_tcp *)transport->state;
    if (tcp == NULL) {
        return;
    }
    fmx_socket_close(tcp->fd);
    free(tcp);
    transport->state = NULL;
}

static const fmx_transport_vtable TCP_VTABLE = {tcp_kind, tcp_send, tcp_recv, tcp_close_soft,
                                                tcp_close_hard};

static fmx_result tcp_wrap(fmx_fd fd, fmx_transport *out) {
    fmx_tcp *tcp;
    int flag = 1;

    if (!fmx_socket_set_nonblocking(fd)) {
        fmx_socket_close(fd);
        return FMX_ERR_INVALID;
    }
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, (socklen_t)sizeof(flag));

    tcp = (fmx_tcp *)calloc(1, sizeof(*tcp));
    if (tcp == NULL) {
        fmx_socket_close(fd);
        return FMX_ERR_NO_MEMORY;
    }
    tcp->fd = fd;
    out->vtable = &TCP_VTABLE;
    out->state = tcp;
    return FMX_OK;
}

fmx_result fmx_tcp_connect(const char *host, uint16_t port, fmx_transport *out) {
    struct addrinfo *candidates;
    struct addrinfo *candidate;

    fmx_net_startup();
    candidates = resolve(host, port, SOCK_STREAM, false);
    if (candidates == NULL) {
        return FMX_ERR_INVALID;
    }

    for (candidate = candidates; candidate != NULL; candidate = candidate->ai_next) {
        fmx_fd fd = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (fd == FMX_INVALID_FD) {
            continue;
        }
        if (connect(fd, candidate->ai_addr, (socklen_t)candidate->ai_addrlen) == 0) {
            freeaddrinfo(candidates);
            return tcp_wrap(fd, out);
        }
        fmx_socket_close(fd);
    }

    freeaddrinfo(candidates);
    return FMX_ERR_CLOSED;
}

static fmx_accept_result tcp_accept(fmx_listener *listener, fmx_transport *out) {
    fmx_tcp_listener *state = (fmx_tcp_listener *)listener->state;
    fmx_fd fd = accept(state->fd, NULL, NULL);

    if (fd == FMX_INVALID_FD) {
        int error = fmx_socket_last_error();
        return fmx_socket_would_block(error) ? FMX_ACCEPT_PENDING : FMX_ACCEPT_ERROR;
    }
    if (tcp_wrap(fd, out) != FMX_OK) {
        return FMX_ACCEPT_ERROR;
    }
    return FMX_ACCEPT_ACCEPTED;
}

static void tcp_listener_close(fmx_listener *listener) {
    fmx_tcp_listener *state = (fmx_tcp_listener *)listener->state;
    if (state == NULL) {
        return;
    }
    fmx_socket_close(state->fd);
    free(state);
    listener->state = NULL;
}

static const fmx_listener_vtable TCP_LISTENER_VTABLE = {tcp_accept, tcp_listener_close};

fmx_result fmx_tcp_listen(const char *host, uint16_t port, fmx_listener *out) {
    struct addrinfo *candidates;
    struct addrinfo *candidate;

    fmx_net_startup();
    candidates = resolve(host, port, SOCK_STREAM, true);
    if (candidates == NULL) {
        return FMX_ERR_INVALID;
    }

    for (candidate = candidates; candidate != NULL; candidate = candidate->ai_next) {
        int reuse = 1;
        fmx_tcp_listener *state;
        fmx_fd fd = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (fd == FMX_INVALID_FD) {
            continue;
        }
        (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse,
                         (socklen_t)sizeof(reuse));
        if (bind(fd, candidate->ai_addr, (socklen_t)candidate->ai_addrlen) != 0 ||
            listen(fd, 64) != 0 || !fmx_socket_set_nonblocking(fd)) {
            fmx_socket_close(fd);
            continue;
        }
        state = (fmx_tcp_listener *)calloc(1, sizeof(*state));
        if (state == NULL) {
            fmx_socket_close(fd);
            freeaddrinfo(candidates);
            return FMX_ERR_NO_MEMORY;
        }
        state->fd = fd;
        out->vtable = &TCP_LISTENER_VTABLE;
        out->state = state;
        freeaddrinfo(candidates);
        return FMX_OK;
    }

    freeaddrinfo(candidates);
    return FMX_ERR_INVALID;
}

uint16_t fmx_tcp_listener_port(const fmx_listener *listener) {
    const fmx_tcp_listener *state = (const fmx_tcp_listener *)listener->state;
    if (state == NULL) {
        return 0;
    }
    return fmx_socket_port(state->fd);
}
