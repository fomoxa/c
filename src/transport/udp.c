#include "socket.h"

#include "fomoxa/udp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define FMX_UDP_SCRATCH ((size_t)65535)
#define FMX_UDP_MAX_PEERS ((size_t)256)
#define FMX_UDP_MAX_QUEUED ((size_t)64)
#define FMX_UDP_MAX_READS ((size_t)256)

typedef struct fmx_datagram {
    struct fmx_datagram *next;
    size_t len;
    uint8_t bytes[1];
} fmx_datagram;

typedef struct fmx_udp_slot {
    bool used;
    struct sockaddr_storage addr;
    socklen_t addr_len;
    fmx_datagram *head;
    fmx_datagram *tail;
    size_t queued;
} fmx_udp_slot;

typedef struct fmx_udp_hub {
    fmx_fd fd;
    int refs;
    bool closed;
    uint8_t *scratch;
    fmx_udp_slot slots[FMX_UDP_MAX_PEERS];
    size_t arrivals[FMX_UDP_MAX_PEERS];
    size_t arrival_head;
    size_t arrival_count;
} fmx_udp_hub;

typedef struct fmx_udp {
    fmx_fd fd;
    uint8_t *scratch;
    uint8_t *inbox;
    size_t inbox_len;
    bool holding;
    bool closed;
} fmx_udp;

typedef struct fmx_udp_peer {
    fmx_udp_hub *hub;
    size_t slot;
    bool closed;
} fmx_udp_peer;

static struct addrinfo *resolve(const char *host, uint16_t port, bool passive) {
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    char service[16];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (passive) {
        hints.ai_flags = AI_PASSIVE;
    }
    (void)snprintf(service, sizeof(service), "%u", (unsigned)port);
    if (getaddrinfo(host, service, &hints, &result) != 0) {
        return NULL;
    }
    return result;
}

/* ---- client transport --------------------------------------------------- */

static fmx_transport_kind udp_kind(const fmx_transport *transport) {
    (void)transport;
    return FMX_TRANSPORT_MESSAGE;
}

static fmx_send_result udp_send(fmx_transport *transport, const uint8_t *bytes, size_t len,
                                size_t *accepted) {
    fmx_udp *udp = (fmx_udp *)transport->state;
    ptrdiff_t written;

    *accepted = 0;
    if (udp->closed) {
        return FMX_SEND_CLOSED;
    }
    if (len > FMX_MAX_DATAGRAM) {
        return FMX_SEND_TOO_LARGE;
    }

    written = fmx_socket_send(udp->fd, bytes, len);
    if (written < 0) {
        int error = fmx_socket_last_error();
        if (fmx_socket_would_block(error)) {
            return FMX_SEND_WOULD_BLOCK;
        }
        if (fmx_socket_message_too_long(error)) {
            return FMX_SEND_TOO_LARGE;
        }
        udp->closed = true;
        return FMX_SEND_ERROR;
    }
    if ((size_t)written != len) {
        return FMX_SEND_TOO_LARGE;
    }
    *accepted = len;
    return FMX_SEND_SENT;
}

static fmx_recv_result udp_recv(fmx_transport *transport, uint8_t *buffer, size_t cap,
                                size_t *received, size_t *needed) {
    fmx_udp *udp = (fmx_udp *)transport->state;

    *received = 0;
    *needed = 0;

    if (!udp->holding) {
        ptrdiff_t count;
        if (udp->closed) {
            return FMX_RECV_CLOSED;
        }
        count = fmx_socket_recv(udp->fd, udp->scratch, FMX_UDP_SCRATCH);
        if (count < 0) {
            int error = fmx_socket_last_error();
            if (fmx_socket_would_block(error)) {
                return FMX_RECV_WOULD_BLOCK;
            }
            udp->closed = true;
            return FMX_RECV_ERROR;
        }
        memcpy(udp->inbox, udp->scratch, (size_t)count);
        udp->inbox_len = (size_t)count;
        udp->holding = true;
    }

    if (udp->inbox_len > cap) {
        *needed = udp->inbox_len;
        return FMX_RECV_NEED_CAPACITY;
    }
    if (udp->inbox_len > 0) {
        memcpy(buffer, udp->inbox, udp->inbox_len);
    }
    *received = udp->inbox_len;
    udp->holding = false;
    return FMX_RECV_RECEIVED;
}

static void udp_close_soft(fmx_transport *transport) {
    (void)transport;
}

static void udp_close_hard(fmx_transport *transport) {
    fmx_udp *udp = (fmx_udp *)transport->state;
    if (udp == NULL) {
        return;
    }
    fmx_socket_close(udp->fd);
    free(udp->scratch);
    free(udp->inbox);
    free(udp);
    transport->state = NULL;
}

static const fmx_transport_vtable UDP_VTABLE = {udp_kind, udp_send, udp_recv, udp_close_soft,
                                                udp_close_hard};

fmx_result fmx_udp_connect(const char *host, uint16_t port, fmx_transport *out) {
    struct addrinfo *candidates;
    struct addrinfo *candidate;

    fmx_net_startup();
    candidates = resolve(host, port, false);
    if (candidates == NULL) {
        return FMX_ERR_INVALID;
    }

    for (candidate = candidates; candidate != NULL; candidate = candidate->ai_next) {
        fmx_udp *udp;
        fmx_fd fd = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (fd == FMX_INVALID_FD) {
            continue;
        }
        if (connect(fd, candidate->ai_addr, (socklen_t)candidate->ai_addrlen) != 0 ||
            !fmx_socket_set_nonblocking(fd)) {
            fmx_socket_close(fd);
            continue;
        }
        udp = (fmx_udp *)calloc(1, sizeof(*udp));
        if (udp == NULL) {
            fmx_socket_close(fd);
            freeaddrinfo(candidates);
            return FMX_ERR_NO_MEMORY;
        }
        udp->fd = fd;
        udp->scratch = (uint8_t *)malloc(FMX_UDP_SCRATCH);
        udp->inbox = (uint8_t *)malloc(FMX_UDP_SCRATCH);
        if (udp->scratch == NULL || udp->inbox == NULL) {
            free(udp->scratch);
            free(udp->inbox);
            free(udp);
            fmx_socket_close(fd);
            freeaddrinfo(candidates);
            return FMX_ERR_NO_MEMORY;
        }
        out->vtable = &UDP_VTABLE;
        out->state = udp;
        freeaddrinfo(candidates);
        return FMX_OK;
    }

    freeaddrinfo(candidates);
    return FMX_ERR_INVALID;
}

/* ---- server hub --------------------------------------------------------- */

static bool same_address(const struct sockaddr_storage *left, socklen_t left_len,
                         const struct sockaddr_storage *right, socklen_t right_len) {
    if (left->ss_family != right->ss_family || left_len != right_len) {
        return false;
    }
    if (left->ss_family == AF_INET) {
        const struct sockaddr_in *a = (const struct sockaddr_in *)left;
        const struct sockaddr_in *b = (const struct sockaddr_in *)right;
        return a->sin_port == b->sin_port &&
               memcmp(&a->sin_addr, &b->sin_addr, sizeof(a->sin_addr)) == 0;
    }
    if (left->ss_family == AF_INET6) {
        const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)left;
        const struct sockaddr_in6 *b = (const struct sockaddr_in6 *)right;
        return a->sin6_port == b->sin6_port &&
               memcmp(&a->sin6_addr, &b->sin6_addr, sizeof(a->sin6_addr)) == 0;
    }
    return false;
}

static void slot_clear(fmx_udp_slot *slot) {
    fmx_datagram *walk = slot->head;
    while (walk != NULL) {
        fmx_datagram *next = walk->next;
        free(walk);
        walk = next;
    }
    slot->head = NULL;
    slot->tail = NULL;
    slot->queued = 0;
    slot->used = false;
}

static void hub_release(fmx_udp_hub *hub) {
    size_t index;

    hub->refs -= 1;
    if (hub->refs > 0) {
        return;
    }
    for (index = 0; index < FMX_UDP_MAX_PEERS; ++index) {
        slot_clear(&hub->slots[index]);
    }
    fmx_socket_close(hub->fd);
    free(hub->scratch);
    free(hub);
}

static void slot_enqueue(fmx_udp_slot *slot, const uint8_t *bytes, size_t len) {
    fmx_datagram *datagram;

    if (slot->queued >= FMX_UDP_MAX_QUEUED) {
        fmx_datagram *oldest = slot->head;
        if (oldest != NULL) {
            slot->head = oldest->next;
            if (slot->head == NULL) {
                slot->tail = NULL;
            }
            slot->queued -= 1;
            free(oldest);
        }
    }

    datagram = (fmx_datagram *)malloc(sizeof(fmx_datagram) + len);
    if (datagram == NULL) {
        return;
    }
    datagram->next = NULL;
    datagram->len = len;
    if (len > 0) {
        memcpy(datagram->bytes, bytes, len);
    }
    if (slot->tail == NULL) {
        slot->head = datagram;
    } else {
        slot->tail->next = datagram;
    }
    slot->tail = datagram;
    slot->queued += 1;
}

static void hub_pump(fmx_udp_hub *hub) {
    size_t round;

    if (hub->closed) {
        return;
    }
    for (round = 0; round < FMX_UDP_MAX_READS; ++round) {
        struct sockaddr_storage from;
        socklen_t from_len = (socklen_t)sizeof(from);
        ptrdiff_t count;
        size_t index;
        size_t chosen = FMX_UDP_MAX_PEERS;

        memset(&from, 0, sizeof(from));
        count = fmx_socket_recvfrom(hub->fd, hub->scratch, FMX_UDP_SCRATCH,
                                    (struct sockaddr *)&from, &from_len);
        if (count < 0) {
            return;
        }

        for (index = 0; index < FMX_UDP_MAX_PEERS; ++index) {
            if (hub->slots[index].used &&
                same_address(&hub->slots[index].addr, hub->slots[index].addr_len, &from,
                             from_len)) {
                chosen = index;
                break;
            }
        }

        if (chosen == FMX_UDP_MAX_PEERS) {
            for (index = 0; index < FMX_UDP_MAX_PEERS; ++index) {
                if (!hub->slots[index].used) {
                    chosen = index;
                    break;
                }
            }
            if (chosen == FMX_UDP_MAX_PEERS) {
                continue;
            }
            hub->slots[chosen].used = true;
            hub->slots[chosen].addr = from;
            hub->slots[chosen].addr_len = from_len;
            hub->slots[chosen].head = NULL;
            hub->slots[chosen].tail = NULL;
            hub->slots[chosen].queued = 0;
            if (hub->arrival_count < FMX_UDP_MAX_PEERS) {
                size_t at = (hub->arrival_head + hub->arrival_count) % FMX_UDP_MAX_PEERS;
                hub->arrivals[at] = chosen;
                hub->arrival_count += 1;
            }
        }

        slot_enqueue(&hub->slots[chosen], hub->scratch, (size_t)count);
    }
}

static fmx_transport_kind udp_peer_kind(const fmx_transport *transport) {
    (void)transport;
    return FMX_TRANSPORT_MESSAGE;
}

static fmx_send_result udp_peer_send(fmx_transport *transport, const uint8_t *bytes, size_t len,
                                     size_t *accepted) {
    fmx_udp_peer *peer = (fmx_udp_peer *)transport->state;
    fmx_udp_slot *slot;
    ptrdiff_t written;

    *accepted = 0;
    if (peer->closed || peer->hub->closed) {
        return FMX_SEND_CLOSED;
    }
    if (len > FMX_MAX_DATAGRAM) {
        return FMX_SEND_TOO_LARGE;
    }

    slot = &peer->hub->slots[peer->slot];
    written = fmx_socket_sendto(peer->hub->fd, bytes, len,
                                (const struct sockaddr *)&slot->addr, slot->addr_len);
    if (written < 0) {
        int error = fmx_socket_last_error();
        if (fmx_socket_would_block(error)) {
            return FMX_SEND_WOULD_BLOCK;
        }
        if (fmx_socket_message_too_long(error)) {
            return FMX_SEND_TOO_LARGE;
        }
        return FMX_SEND_ERROR;
    }
    if ((size_t)written != len) {
        return FMX_SEND_TOO_LARGE;
    }
    *accepted = len;
    return FMX_SEND_SENT;
}

static fmx_recv_result udp_peer_recv(fmx_transport *transport, uint8_t *buffer, size_t cap,
                                     size_t *received, size_t *needed) {
    fmx_udp_peer *peer = (fmx_udp_peer *)transport->state;
    fmx_udp_slot *slot;
    fmx_datagram *front;

    *received = 0;
    *needed = 0;
    if (peer->closed) {
        return FMX_RECV_CLOSED;
    }

    hub_pump(peer->hub);
    slot = &peer->hub->slots[peer->slot];
    front = slot->head;
    if (front == NULL) {
        return FMX_RECV_WOULD_BLOCK;
    }
    if (front->len > cap) {
        *needed = front->len;
        return FMX_RECV_NEED_CAPACITY;
    }
    if (front->len > 0) {
        memcpy(buffer, front->bytes, front->len);
    }
    *received = front->len;
    slot->head = front->next;
    if (slot->head == NULL) {
        slot->tail = NULL;
    }
    slot->queued -= 1;
    free(front);
    return FMX_RECV_RECEIVED;
}

static void udp_peer_close_soft(fmx_transport *transport) {
    (void)transport;
}

static void udp_peer_close_hard(fmx_transport *transport) {
    fmx_udp_peer *peer = (fmx_udp_peer *)transport->state;
    if (peer == NULL) {
        return;
    }
    slot_clear(&peer->hub->slots[peer->slot]);
    hub_release(peer->hub);
    free(peer);
    transport->state = NULL;
}

static const fmx_transport_vtable UDP_PEER_VTABLE = {udp_peer_kind, udp_peer_send, udp_peer_recv,
                                                     udp_peer_close_soft, udp_peer_close_hard};

static fmx_accept_result udp_accept(fmx_listener *listener, fmx_transport *out) {
    fmx_udp_hub *hub = (fmx_udp_hub *)listener->state;
    fmx_udp_peer *peer;
    size_t slot;

    hub_pump(hub);
    if (hub->arrival_count == 0) {
        return FMX_ACCEPT_PENDING;
    }
    slot = hub->arrivals[hub->arrival_head];
    hub->arrival_head = (hub->arrival_head + 1) % FMX_UDP_MAX_PEERS;
    hub->arrival_count -= 1;

    peer = (fmx_udp_peer *)calloc(1, sizeof(*peer));
    if (peer == NULL) {
        return FMX_ACCEPT_ERROR;
    }
    peer->hub = hub;
    peer->slot = slot;
    hub->refs += 1;
    out->vtable = &UDP_PEER_VTABLE;
    out->state = peer;
    return FMX_ACCEPT_ACCEPTED;
}

static void udp_listener_close(fmx_listener *listener) {
    fmx_udp_hub *hub = (fmx_udp_hub *)listener->state;
    if (hub == NULL) {
        return;
    }
    hub->closed = true;
    hub_release(hub);
    listener->state = NULL;
}

static const fmx_listener_vtable UDP_LISTENER_VTABLE = {udp_accept, udp_listener_close};

fmx_result fmx_udp_listen(const char *host, uint16_t port, fmx_listener *out) {
    struct addrinfo *candidates;
    struct addrinfo *candidate;

    fmx_net_startup();
    candidates = resolve(host, port, true);
    if (candidates == NULL) {
        return FMX_ERR_INVALID;
    }

    for (candidate = candidates; candidate != NULL; candidate = candidate->ai_next) {
        fmx_udp_hub *hub;
        fmx_fd fd = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (fd == FMX_INVALID_FD) {
            continue;
        }
        if (bind(fd, candidate->ai_addr, (socklen_t)candidate->ai_addrlen) != 0 ||
            !fmx_socket_set_nonblocking(fd)) {
            fmx_socket_close(fd);
            continue;
        }
        hub = (fmx_udp_hub *)calloc(1, sizeof(*hub));
        if (hub == NULL) {
            fmx_socket_close(fd);
            freeaddrinfo(candidates);
            return FMX_ERR_NO_MEMORY;
        }
        hub->scratch = (uint8_t *)malloc(FMX_UDP_SCRATCH);
        if (hub->scratch == NULL) {
            free(hub);
            fmx_socket_close(fd);
            freeaddrinfo(candidates);
            return FMX_ERR_NO_MEMORY;
        }
        hub->fd = fd;
        hub->refs = 1;
        out->vtable = &UDP_LISTENER_VTABLE;
        out->state = hub;
        freeaddrinfo(candidates);
        return FMX_OK;
    }

    freeaddrinfo(candidates);
    return FMX_ERR_INVALID;
}

uint16_t fmx_udp_listener_port(const fmx_listener *listener) {
    const fmx_udp_hub *hub = (const fmx_udp_hub *)listener->state;
    if (hub == NULL) {
        return 0;
    }
    return fmx_socket_port(hub->fd);
}
