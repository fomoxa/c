#include "fmx_test.h"

#include "fomoxa/udp.h"

#include "transport/socket.h"

#include <stdint.h>
#include <string.h>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <poll.h>
#endif

static fmx_fd raw_socket(void) {
    struct sockaddr_in any;
    fmx_fd fd;

    fmx_net_startup();
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&any, 0, sizeof(any));
    any.sin_family = AF_INET;
    any.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    FMX_CHECK(fd != FMX_INVALID_FD);
    FMX_CHECK(bind(fd, (struct sockaddr *)&any, sizeof(any)) == 0);
    FMX_CHECK(fmx_socket_set_nonblocking(fd));
    return fd;
}

static void sleep_briefly(void) {
#if defined(_WIN32)
    Sleep(50);
#else
    (void)poll(NULL, 0, 50);
#endif
}

static struct sockaddr_in listener_address(uint16_t port) {
    struct sockaddr_in target;
    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return target;
}

static void send_datagram(fmx_fd fd, uint64_t session, const uint8_t *frame, size_t len,
                          const struct sockaddr_in *target) {
    uint8_t datagram[64];
    size_t index;

    for (index = 0; index < 8; ++index) {
        datagram[index] = (uint8_t)(session >> (8 * index));
    }
    if (len > 0) {
        memcpy(datagram + 8, frame, len);
    }
    FMX_CHECK(fmx_socket_sendto(fd, datagram, 8 + len, (const struct sockaddr *)target,
                                (socklen_t)sizeof(*target)) == (ptrdiff_t)(8 + len));
}

static uint64_t read_session(fmx_fd fd, size_t *count) {
    uint8_t datagram[64];
    uint64_t session = 0;
    ptrdiff_t got = fmx_socket_recv(fd, datagram, sizeof(datagram));
    size_t index;

    *count = got < 0 ? 0 : (size_t)got;
    for (index = 0; got >= 8 && index < 8; ++index) {
        session |= (uint64_t)datagram[index] << (8 * index);
    }
    return session;
}

static bool accept_peer(fmx_listener *listener, fmx_transport *out) {
    int attempt;
    for (attempt = 0; attempt < 100; ++attempt) {
        if (listener->vtable->accept(listener, out) == FMX_ACCEPT_ACCEPTED) {
            return true;
        }
        sleep_briefly();
    }
    return false;
}

static void drain(fmx_listener *listener) {
    fmx_transport extra;
    sleep_briefly();
    while (listener->vtable->accept(listener, &extra) == FMX_ACCEPT_ACCEPTED) {
        extra.vtable->close_hard(&extra);
    }
}

static fmx_recv_result peer_recv(fmx_transport *peer, uint8_t *buffer, size_t *received) {
    size_t needed = 0;
    return peer->vtable->recv(peer, buffer, 64, received, &needed);
}

static void a_peer_keeps_its_session_when_its_address_changes(void) {
    fmx_listener listener;
    fmx_transport peer;
    struct sockaddr_in target;
    fmx_fd before;
    fmx_fd after;
    uint8_t buffer[64];
    uint8_t frame[2] = {1, 2};
    uint8_t moved[1] = {3};
    uint8_t reply[1] = {9};
    size_t received = 0;
    size_t accepted = 0;
    size_t count = 0;
    uint64_t session;

    FMX_CHECK(fmx_udp_listen("127.0.0.1", 0, &listener) == FMX_OK);
    target = listener_address(fmx_udp_listener_port(&listener));
    before = raw_socket();
    after = raw_socket();

    send_datagram(before, 0, frame, sizeof(frame), &target);
    FMX_CHECK(accept_peer(&listener, &peer));
    FMX_CHECK(peer_recv(&peer, buffer, &received) == FMX_RECV_RECEIVED);
    FMX_CHECK(received == 2 && buffer[0] == 1 && buffer[1] == 2);

    FMX_CHECK(peer.vtable->send(&peer, reply, 1, &accepted) == FMX_SEND_SENT);
    sleep_briefly();
    session = read_session(before, &count);
    FMX_CHECK(session != 0);
    FMX_CHECK(count == 9);

    send_datagram(after, session, moved, 1, &target);
    drain(&listener);
    FMX_CHECK(peer_recv(&peer, buffer, &received) == FMX_RECV_RECEIVED);
    FMX_CHECK(received == 1 && buffer[0] == 3);

    FMX_CHECK(peer.vtable->send(&peer, reply, 1, &accepted) == FMX_SEND_SENT);
    sleep_briefly();
    FMX_CHECK(read_session(after, &count) == session);
    FMX_CHECK(count == 9);

    peer.vtable->close_hard(&peer);
    listener.vtable->close(&listener);
    fmx_socket_close(before);
    fmx_socket_close(after);
}

static void unknown_ids_and_short_datagrams_move_nobody(void) {
    fmx_listener listener;
    fmx_transport peer;
    fmx_transport other;
    struct sockaddr_in target;
    fmx_fd first;
    fmx_fd second;
    uint8_t buffer[64];
    uint8_t one[1] = {1};
    uint8_t five[1] = {5};
    uint8_t six[1] = {6};
    uint8_t seven[1] = {7};
    uint8_t zero[8] = {0};
    size_t received = 0;
    size_t accepted = 0;
    size_t count = 0;
    uint64_t session;
    uint64_t other_session;

    FMX_CHECK(fmx_udp_listen("127.0.0.1", 0, &listener) == FMX_OK);
    target = listener_address(fmx_udp_listener_port(&listener));
    first = raw_socket();
    second = raw_socket();

    send_datagram(first, 0, one, 1, &target);
    FMX_CHECK(accept_peer(&listener, &peer));
    FMX_CHECK(peer_recv(&peer, buffer, &received) == FMX_RECV_RECEIVED);
    FMX_CHECK(peer.vtable->send(&peer, one, 1, &accepted) == FMX_SEND_SENT);
    sleep_briefly();
    session = read_session(first, &count);

    FMX_CHECK(fmx_socket_sendto(first, zero, 8, (const struct sockaddr *)&target,
                                (socklen_t)sizeof(target)) == 8);
    send_datagram(second, session ^ 1u, five, 1, &target);
    drain(&listener);
    FMX_CHECK(peer_recv(&peer, buffer, &received) == FMX_RECV_WOULD_BLOCK);

    send_datagram(second, 0, six, 1, &target);
    FMX_CHECK(accept_peer(&listener, &other));
    FMX_CHECK(peer_recv(&other, buffer, &received) == FMX_RECV_RECEIVED);
    FMX_CHECK(other.vtable->send(&other, one, 1, &accepted) == FMX_SEND_SENT);
    sleep_briefly();
    other_session = read_session(second, &count);

    send_datagram(first, other_session, seven, 1, &target);
    drain(&listener);
    FMX_CHECK(peer_recv(&other, buffer, &received) == FMX_RECV_WOULD_BLOCK);
    FMX_CHECK(other.vtable->send(&other, one, 1, &accepted) == FMX_SEND_SENT);
    sleep_briefly();
    FMX_CHECK(read_session(second, &count) == other_session);

    other.vtable->close_hard(&other);
    peer.vtable->close_hard(&peer);
    listener.vtable->close(&listener);
    fmx_socket_close(first);
    fmx_socket_close(second);
}

static void a_client_sends_zero_first_and_keeps_the_first_id(void) {
    fmx_transport client;
    struct sockaddr_in from;
    socklen_t from_len = (socklen_t)sizeof(from);
    fmx_fd server = raw_socket();
    struct sockaddr_in bound;
    socklen_t bound_len = (socklen_t)sizeof(bound);
    uint8_t request[64];
    uint8_t buffer[64];
    uint8_t one[1] = {1};
    uint8_t two[1] = {2};
    uint8_t three[1] = {3};
    uint8_t four[1] = {4};
    uint8_t eight[1] = {8};
    size_t received = 0;
    size_t accepted = 0;
    size_t needed = 0;
    ptrdiff_t got;

    FMX_CHECK(getsockname(server, (struct sockaddr *)&bound, &bound_len) == 0);
    FMX_CHECK(fmx_udp_connect("127.0.0.1", ntohs(bound.sin_port), &client) == FMX_OK);

    FMX_CHECK(client.vtable->send(&client, one, 1, &accepted) == FMX_SEND_SENT);
    sleep_briefly();
    got = fmx_socket_recvfrom(server, request, sizeof(request), (struct sockaddr *)&from, &from_len);
    FMX_CHECK(got == 9);
    FMX_CHECK(memcmp(request, "\0\0\0\0\0\0\0\0", 8) == 0);

    send_datagram(server, 0, two, 1, &from);
    send_datagram(server, 11, three, 1, &from);
    send_datagram(server, 22, four, 1, &from);
    sleep_briefly();

    FMX_CHECK(client.vtable->recv(&client, buffer, sizeof(buffer), &received, &needed) == FMX_RECV_RECEIVED);
    FMX_CHECK(received == 1 && buffer[0] == 3);
    FMX_CHECK(client.vtable->recv(&client, buffer, sizeof(buffer), &received, &needed) == FMX_RECV_WOULD_BLOCK);

    FMX_CHECK(client.vtable->send(&client, eight, 1, &accepted) == FMX_SEND_SENT);
    sleep_briefly();
    got = fmx_socket_recvfrom(server, request, sizeof(request), (struct sockaddr *)&from, &from_len);
    FMX_CHECK(got == 9 && request[0] == 11);

    client.vtable->close_hard(&client);
    fmx_socket_close(server);
}

int main(void) {
    FMX_RUN(a_peer_keeps_its_session_when_its_address_changes);
    FMX_RUN(unknown_ids_and_short_datagrams_move_nobody);
    FMX_RUN(a_client_sends_zero_first_and_keeps_the_first_id);
    FMX_DONE();
}
