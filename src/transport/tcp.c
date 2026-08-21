#include "socket.h"

#include "cyclone/tcp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef struct cyc_tcp {
    cyc_fd fd;
    bool closed;
} cyc_tcp;

typedef struct cyc_tcp_listener {
    cyc_fd fd;
} cyc_tcp_listener;

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

static cyc_transport_kind tcp_kind(const cyc_transport *transport) {
    (void)transport;
    return CYC_TRANSPORT_STREAM;
}

static cyc_send_result tcp_send(cyc_transport *transport, const uint8_t *bytes, size_t len,
                                size_t *accepted) {
    cyc_tcp *tcp = (cyc_tcp *)transport->state;
    ptrdiff_t written;

    *accepted = 0;
    if (tcp->closed) {
        return CYC_SEND_CLOSED;
    }
    if (len == 0) {
        return CYC_SEND_SENT;
    }

    written = cyc_socket_send(tcp->fd, bytes, len);
    if (written < 0) {
        int error = cyc_socket_last_error();
        if (cyc_socket_would_block(error)) {
            return CYC_SEND_WOULD_BLOCK;
        }
        tcp->closed = true;
        return CYC_SEND_ERROR;
    }
    if (written == 0) {
        return CYC_SEND_WOULD_BLOCK;
    }
    *accepted = (size_t)written;
    return (size_t)written == len ? CYC_SEND_SENT : CYC_SEND_PARTIAL;
}

static cyc_recv_result tcp_recv(cyc_transport *transport, uint8_t *buffer, size_t cap,
                                size_t *received, size_t *needed) {
    cyc_tcp *tcp = (cyc_tcp *)transport->state;
    ptrdiff_t count;

    *received = 0;
    *needed = 0;
    if (tcp->closed) {
        return CYC_RECV_CLOSED;
    }

    count = cyc_socket_recv(tcp->fd, buffer, cap);
    if (count < 0) {
        int error = cyc_socket_last_error();
        if (cyc_socket_would_block(error)) {
            return CYC_RECV_WOULD_BLOCK;
        }
        tcp->closed = true;
        return CYC_RECV_ERROR;
    }
    if (count == 0) {
        tcp->closed = true;
        return CYC_RECV_CLOSED;
    }
    *received = (size_t)count;
    return CYC_RECV_RECEIVED;
}

static void tcp_close_soft(cyc_transport *transport) {
    cyc_tcp *tcp = (cyc_tcp *)transport->state;
    if (!tcp->closed) {
        cyc_socket_shutdown_write(tcp->fd);
    }
}

static void tcp_close_hard(cyc_transport *transport) {
    cyc_tcp *tcp = (cyc_tcp *)transport->state;
    if (tcp == NULL) {
        return;
    }
    cyc_socket_close(tcp->fd);
    free(tcp);
    transport->state = NULL;
}

static const cyc_transport_vtable TCP_VTABLE = {tcp_kind, tcp_send, tcp_recv, tcp_close_soft,
                                                tcp_close_hard};

static cyc_result tcp_wrap(cyc_fd fd, cyc_transport *out) {
    cyc_tcp *tcp;
    int flag = 1;

    if (!cyc_socket_set_nonblocking(fd)) {
        cyc_socket_close(fd);
        return CYC_ERR_INVALID;
    }
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, (socklen_t)sizeof(flag));

    tcp = (cyc_tcp *)calloc(1, sizeof(*tcp));
    if (tcp == NULL) {
        cyc_socket_close(fd);
        return CYC_ERR_NO_MEMORY;
    }
    tcp->fd = fd;
    out->vtable = &TCP_VTABLE;
    out->state = tcp;
    return CYC_OK;
}

cyc_result cyc_tcp_connect(const char *host, uint16_t port, cyc_transport *out) {
    struct addrinfo *candidates;
    struct addrinfo *candidate;

    cyc_net_startup();
    candidates = resolve(host, port, SOCK_STREAM, false);
    if (candidates == NULL) {
        return CYC_ERR_INVALID;
    }

    for (candidate = candidates; candidate != NULL; candidate = candidate->ai_next) {
        cyc_fd fd = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (fd == CYC_INVALID_FD) {
            continue;
        }
        if (connect(fd, candidate->ai_addr, (socklen_t)candidate->ai_addrlen) == 0) {
            freeaddrinfo(candidates);
            return tcp_wrap(fd, out);
        }
        cyc_socket_close(fd);
    }

    freeaddrinfo(candidates);
    return CYC_ERR_CLOSED;
}

static cyc_accept_result tcp_accept(cyc_listener *listener, cyc_transport *out) {
    cyc_tcp_listener *state = (cyc_tcp_listener *)listener->state;
    cyc_fd fd = accept(state->fd, NULL, NULL);

    if (fd == CYC_INVALID_FD) {
        int error = cyc_socket_last_error();
        return cyc_socket_would_block(error) ? CYC_ACCEPT_PENDING : CYC_ACCEPT_ERROR;
    }
    if (tcp_wrap(fd, out) != CYC_OK) {
        return CYC_ACCEPT_ERROR;
    }
    return CYC_ACCEPT_ACCEPTED;
}

static void tcp_listener_close(cyc_listener *listener) {
    cyc_tcp_listener *state = (cyc_tcp_listener *)listener->state;
    if (state == NULL) {
        return;
    }
    cyc_socket_close(state->fd);
    free(state);
    listener->state = NULL;
}

static const cyc_listener_vtable TCP_LISTENER_VTABLE = {tcp_accept, tcp_listener_close};

cyc_result cyc_tcp_listen(const char *host, uint16_t port, cyc_listener *out) {
    struct addrinfo *candidates;
    struct addrinfo *candidate;

    cyc_net_startup();
    candidates = resolve(host, port, SOCK_STREAM, true);
    if (candidates == NULL) {
        return CYC_ERR_INVALID;
    }

    for (candidate = candidates; candidate != NULL; candidate = candidate->ai_next) {
        int reuse = 1;
        cyc_tcp_listener *state;
        cyc_fd fd = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (fd == CYC_INVALID_FD) {
            continue;
        }
        (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse,
                         (socklen_t)sizeof(reuse));
        if (bind(fd, candidate->ai_addr, (socklen_t)candidate->ai_addrlen) != 0 ||
            listen(fd, 64) != 0 || !cyc_socket_set_nonblocking(fd)) {
            cyc_socket_close(fd);
            continue;
        }
        state = (cyc_tcp_listener *)calloc(1, sizeof(*state));
        if (state == NULL) {
            cyc_socket_close(fd);
            freeaddrinfo(candidates);
            return CYC_ERR_NO_MEMORY;
        }
        state->fd = fd;
        out->vtable = &TCP_LISTENER_VTABLE;
        out->state = state;
        freeaddrinfo(candidates);
        return CYC_OK;
    }

    freeaddrinfo(candidates);
    return CYC_ERR_INVALID;
}

uint16_t cyc_tcp_listener_port(const cyc_listener *listener) {
    const cyc_tcp_listener *state = (const cyc_tcp_listener *)listener->state;
    if (state == NULL) {
        return 0;
    }
    return cyc_socket_port(state->fd);
}
