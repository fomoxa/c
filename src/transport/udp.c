#include "socket.h"

#include "cyclone/udp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define CYC_UDP_SCRATCH ((size_t)65535)
#define CYC_UDP_MAX_PEERS ((size_t)256)
#define CYC_UDP_MAX_QUEUED ((size_t)64)
#define CYC_UDP_MAX_READS ((size_t)256)

typedef struct cyc_datagram {
    struct cyc_datagram *next;
    size_t len;
    uint8_t bytes[1];
} cyc_datagram;

typedef struct cyc_udp_slot {
    bool used;
    struct sockaddr_storage addr;
    socklen_t addr_len;
    cyc_datagram *head;
    cyc_datagram *tail;
    size_t queued;
} cyc_udp_slot;

typedef struct cyc_udp_hub {
    cyc_fd fd;
    int refs;
    bool closed;
    uint8_t *scratch;
    cyc_udp_slot slots[CYC_UDP_MAX_PEERS];
    size_t arrivals[CYC_UDP_MAX_PEERS];
    size_t arrival_head;
    size_t arrival_count;
} cyc_udp_hub;

typedef struct cyc_udp {
    cyc_fd fd;
    uint8_t *scratch;
    uint8_t *inbox;
    size_t inbox_len;
    bool holding;
    bool closed;
} cyc_udp;

typedef struct cyc_udp_peer {
    cyc_udp_hub *hub;
    size_t slot;
    bool closed;
} cyc_udp_peer;

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

static cyc_transport_kind udp_kind(const cyc_transport *transport) {
    (void)transport;
    return CYC_TRANSPORT_MESSAGE;
}

static cyc_send_result udp_send(cyc_transport *transport, const uint8_t *bytes, size_t len,
                                size_t *accepted) {
    cyc_udp *udp = (cyc_udp *)transport->state;
    ptrdiff_t written;

    *accepted = 0;
    if (udp->closed) {
        return CYC_SEND_CLOSED;
    }
    if (len > CYC_MAX_DATAGRAM) {
        return CYC_SEND_TOO_LARGE;
    }

    written = cyc_socket_send(udp->fd, bytes, len);
    if (written < 0) {
        int error = cyc_socket_last_error();
        if (cyc_socket_would_block(error)) {
            return CYC_SEND_WOULD_BLOCK;
        }
        if (cyc_socket_message_too_long(error)) {
            return CYC_SEND_TOO_LARGE;
        }
        udp->closed = true;
        return CYC_SEND_ERROR;
    }
    if ((size_t)written != len) {
        return CYC_SEND_TOO_LARGE;
    }
    *accepted = len;
    return CYC_SEND_SENT;
}

static cyc_recv_result udp_recv(cyc_transport *transport, uint8_t *buffer, size_t cap,
                                size_t *received, size_t *needed) {
    cyc_udp *udp = (cyc_udp *)transport->state;

    *received = 0;
    *needed = 0;

    if (!udp->holding) {
        ptrdiff_t count;
        if (udp->closed) {
            return CYC_RECV_CLOSED;
        }
        count = cyc_socket_recv(udp->fd, udp->scratch, CYC_UDP_SCRATCH);
        if (count < 0) {
            int error = cyc_socket_last_error();
            if (cyc_socket_would_block(error)) {
                return CYC_RECV_WOULD_BLOCK;
            }
            udp->closed = true;
            return CYC_RECV_ERROR;
        }
        memcpy(udp->inbox, udp->scratch, (size_t)count);
        udp->inbox_len = (size_t)count;
        udp->holding = true;
    }

    if (udp->inbox_len > cap) {
        *needed = udp->inbox_len;
        return CYC_RECV_NEED_CAPACITY;
    }
    if (udp->inbox_len > 0) {
        memcpy(buffer, udp->inbox, udp->inbox_len);
    }
    *received = udp->inbox_len;
    udp->holding = false;
    return CYC_RECV_RECEIVED;
}

static void udp_close_soft(cyc_transport *transport) {
    (void)transport;
}

static void udp_close_hard(cyc_transport *transport) {
    cyc_udp *udp = (cyc_udp *)transport->state;
    if (udp == NULL) {
        return;
    }
    cyc_socket_close(udp->fd);
    free(udp->scratch);
    free(udp->inbox);
    free(udp);
    transport->state = NULL;
}

static const cyc_transport_vtable UDP_VTABLE = {udp_kind, udp_send, udp_recv, udp_close_soft,
                                                udp_close_hard};

cyc_result cyc_udp_connect(const char *host, uint16_t port, cyc_transport *out) {
    struct addrinfo *candidates;
    struct addrinfo *candidate;

    cyc_net_startup();
    candidates = resolve(host, port, false);
    if (candidates == NULL) {
        return CYC_ERR_INVALID;
    }

    for (candidate = candidates; candidate != NULL; candidate = candidate->ai_next) {
        cyc_udp *udp;
        cyc_fd fd = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (fd == CYC_INVALID_FD) {
            continue;
        }
        if (connect(fd, candidate->ai_addr, (socklen_t)candidate->ai_addrlen) != 0 ||
            !cyc_socket_set_nonblocking(fd)) {
            cyc_socket_close(fd);
            continue;
        }
        udp = (cyc_udp *)calloc(1, sizeof(*udp));
        if (udp == NULL) {
            cyc_socket_close(fd);
            freeaddrinfo(candidates);
            return CYC_ERR_NO_MEMORY;
        }
        udp->fd = fd;
        udp->scratch = (uint8_t *)malloc(CYC_UDP_SCRATCH);
        udp->inbox = (uint8_t *)malloc(CYC_UDP_SCRATCH);
        if (udp->scratch == NULL || udp->inbox == NULL) {
            free(udp->scratch);
            free(udp->inbox);
            free(udp);
            cyc_socket_close(fd);
            freeaddrinfo(candidates);
            return CYC_ERR_NO_MEMORY;
        }
        out->vtable = &UDP_VTABLE;
        out->state = udp;
        freeaddrinfo(candidates);
        return CYC_OK;
    }

    freeaddrinfo(candidates);
    return CYC_ERR_INVALID;
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

static void slot_clear(cyc_udp_slot *slot) {
    cyc_datagram *walk = slot->head;
    while (walk != NULL) {
        cyc_datagram *next = walk->next;
        free(walk);
        walk = next;
    }
    slot->head = NULL;
    slot->tail = NULL;
    slot->queued = 0;
    slot->used = false;
}

static void hub_release(cyc_udp_hub *hub) {
    size_t index;

    hub->refs -= 1;
    if (hub->refs > 0) {
        return;
    }
    for (index = 0; index < CYC_UDP_MAX_PEERS; ++index) {
        slot_clear(&hub->slots[index]);
    }
    cyc_socket_close(hub->fd);
    free(hub->scratch);
    free(hub);
}

static void slot_enqueue(cyc_udp_slot *slot, const uint8_t *bytes, size_t len) {
    cyc_datagram *datagram;

    if (slot->queued >= CYC_UDP_MAX_QUEUED) {
        cyc_datagram *oldest = slot->head;
        if (oldest != NULL) {
            slot->head = oldest->next;
            if (slot->head == NULL) {
                slot->tail = NULL;
            }
            slot->queued -= 1;
            free(oldest);
        }
    }

    datagram = (cyc_datagram *)malloc(sizeof(cyc_datagram) + len);
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

static void hub_pump(cyc_udp_hub *hub) {
    size_t round;

    if (hub->closed) {
        return;
    }
    for (round = 0; round < CYC_UDP_MAX_READS; ++round) {
        struct sockaddr_storage from;
        socklen_t from_len = (socklen_t)sizeof(from);
        ptrdiff_t count;
        size_t index;
        size_t chosen = CYC_UDP_MAX_PEERS;

        memset(&from, 0, sizeof(from));
        count = cyc_socket_recvfrom(hub->fd, hub->scratch, CYC_UDP_SCRATCH,
                                    (struct sockaddr *)&from, &from_len);
        if (count < 0) {
            return;
        }

        for (index = 0; index < CYC_UDP_MAX_PEERS; ++index) {
            if (hub->slots[index].used &&
                same_address(&hub->slots[index].addr, hub->slots[index].addr_len, &from,
                             from_len)) {
                chosen = index;
                break;
            }
        }

        if (chosen == CYC_UDP_MAX_PEERS) {
            for (index = 0; index < CYC_UDP_MAX_PEERS; ++index) {
                if (!hub->slots[index].used) {
                    chosen = index;
                    break;
                }
            }
            if (chosen == CYC_UDP_MAX_PEERS) {
                continue;
            }
            hub->slots[chosen].used = true;
            hub->slots[chosen].addr = from;
            hub->slots[chosen].addr_len = from_len;
            hub->slots[chosen].head = NULL;
            hub->slots[chosen].tail = NULL;
            hub->slots[chosen].queued = 0;
            if (hub->arrival_count < CYC_UDP_MAX_PEERS) {
                size_t at = (hub->arrival_head + hub->arrival_count) % CYC_UDP_MAX_PEERS;
                hub->arrivals[at] = chosen;
                hub->arrival_count += 1;
            }
        }

        slot_enqueue(&hub->slots[chosen], hub->scratch, (size_t)count);
    }
}

static cyc_transport_kind udp_peer_kind(const cyc_transport *transport) {
    (void)transport;
    return CYC_TRANSPORT_MESSAGE;
}

static cyc_send_result udp_peer_send(cyc_transport *transport, const uint8_t *bytes, size_t len,
                                     size_t *accepted) {
    cyc_udp_peer *peer = (cyc_udp_peer *)transport->state;
    cyc_udp_slot *slot;
    ptrdiff_t written;

    *accepted = 0;
    if (peer->closed || peer->hub->closed) {
        return CYC_SEND_CLOSED;
    }
    if (len > CYC_MAX_DATAGRAM) {
        return CYC_SEND_TOO_LARGE;
    }

    slot = &peer->hub->slots[peer->slot];
    written = cyc_socket_sendto(peer->hub->fd, bytes, len,
                                (const struct sockaddr *)&slot->addr, slot->addr_len);
    if (written < 0) {
        int error = cyc_socket_last_error();
        if (cyc_socket_would_block(error)) {
            return CYC_SEND_WOULD_BLOCK;
        }
        if (cyc_socket_message_too_long(error)) {
            return CYC_SEND_TOO_LARGE;
        }
        return CYC_SEND_ERROR;
    }
    if ((size_t)written != len) {
        return CYC_SEND_TOO_LARGE;
    }
    *accepted = len;
    return CYC_SEND_SENT;
}

static cyc_recv_result udp_peer_recv(cyc_transport *transport, uint8_t *buffer, size_t cap,
                                     size_t *received, size_t *needed) {
    cyc_udp_peer *peer = (cyc_udp_peer *)transport->state;
    cyc_udp_slot *slot;
    cyc_datagram *front;

    *received = 0;
    *needed = 0;
    if (peer->closed) {
        return CYC_RECV_CLOSED;
    }

    hub_pump(peer->hub);
    slot = &peer->hub->slots[peer->slot];
    front = slot->head;
    if (front == NULL) {
        return CYC_RECV_WOULD_BLOCK;
    }
    if (front->len > cap) {
        *needed = front->len;
        return CYC_RECV_NEED_CAPACITY;
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
    return CYC_RECV_RECEIVED;
}

static void udp_peer_close_soft(cyc_transport *transport) {
    (void)transport;
}

static void udp_peer_close_hard(cyc_transport *transport) {
    cyc_udp_peer *peer = (cyc_udp_peer *)transport->state;
    if (peer == NULL) {
        return;
    }
    slot_clear(&peer->hub->slots[peer->slot]);
    hub_release(peer->hub);
    free(peer);
    transport->state = NULL;
}

static const cyc_transport_vtable UDP_PEER_VTABLE = {udp_peer_kind, udp_peer_send, udp_peer_recv,
                                                     udp_peer_close_soft, udp_peer_close_hard};

static cyc_accept_result udp_accept(cyc_listener *listener, cyc_transport *out) {
    cyc_udp_hub *hub = (cyc_udp_hub *)listener->state;
    cyc_udp_peer *peer;
    size_t slot;

    hub_pump(hub);
    if (hub->arrival_count == 0) {
        return CYC_ACCEPT_PENDING;
    }
    slot = hub->arrivals[hub->arrival_head];
    hub->arrival_head = (hub->arrival_head + 1) % CYC_UDP_MAX_PEERS;
    hub->arrival_count -= 1;

    peer = (cyc_udp_peer *)calloc(1, sizeof(*peer));
    if (peer == NULL) {
        return CYC_ACCEPT_ERROR;
    }
    peer->hub = hub;
    peer->slot = slot;
    hub->refs += 1;
    out->vtable = &UDP_PEER_VTABLE;
    out->state = peer;
    return CYC_ACCEPT_ACCEPTED;
}

static void udp_listener_close(cyc_listener *listener) {
    cyc_udp_hub *hub = (cyc_udp_hub *)listener->state;
    if (hub == NULL) {
        return;
    }
    hub->closed = true;
    hub_release(hub);
    listener->state = NULL;
}

static const cyc_listener_vtable UDP_LISTENER_VTABLE = {udp_accept, udp_listener_close};

cyc_result cyc_udp_listen(const char *host, uint16_t port, cyc_listener *out) {
    struct addrinfo *candidates;
    struct addrinfo *candidate;

    cyc_net_startup();
    candidates = resolve(host, port, true);
    if (candidates == NULL) {
        return CYC_ERR_INVALID;
    }

    for (candidate = candidates; candidate != NULL; candidate = candidate->ai_next) {
        cyc_udp_hub *hub;
        cyc_fd fd = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (fd == CYC_INVALID_FD) {
            continue;
        }
        if (bind(fd, candidate->ai_addr, (socklen_t)candidate->ai_addrlen) != 0 ||
            !cyc_socket_set_nonblocking(fd)) {
            cyc_socket_close(fd);
            continue;
        }
        hub = (cyc_udp_hub *)calloc(1, sizeof(*hub));
        if (hub == NULL) {
            cyc_socket_close(fd);
            freeaddrinfo(candidates);
            return CYC_ERR_NO_MEMORY;
        }
        hub->scratch = (uint8_t *)malloc(CYC_UDP_SCRATCH);
        if (hub->scratch == NULL) {
            free(hub);
            cyc_socket_close(fd);
            freeaddrinfo(candidates);
            return CYC_ERR_NO_MEMORY;
        }
        hub->fd = fd;
        hub->refs = 1;
        out->vtable = &UDP_LISTENER_VTABLE;
        out->state = hub;
        freeaddrinfo(candidates);
        return CYC_OK;
    }

    freeaddrinfo(candidates);
    return CYC_ERR_INVALID;
}

uint16_t cyc_udp_listener_port(const cyc_listener *listener) {
    const cyc_udp_hub *hub = (const cyc_udp_hub *)listener->state;
    if (hub == NULL) {
        return 0;
    }
    return cyc_socket_port(hub->fd);
}
