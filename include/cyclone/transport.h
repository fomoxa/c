#ifndef CYCLONE_TRANSPORT_H
#define CYCLONE_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cyc_transport_kind {
    CYC_TRANSPORT_STREAM = 0,
    CYC_TRANSPORT_MESSAGE = 1
} cyc_transport_kind;

typedef enum cyc_send_result {
    CYC_SEND_SENT = 0,
    CYC_SEND_PARTIAL = 1,
    CYC_SEND_WOULD_BLOCK = 2,
    CYC_SEND_TOO_LARGE = 3,
    CYC_SEND_CLOSED = 4,
    CYC_SEND_ERROR = 5
} cyc_send_result;

typedef enum cyc_recv_result {
    CYC_RECV_RECEIVED = 0,
    CYC_RECV_WOULD_BLOCK = 1,
    CYC_RECV_NEED_CAPACITY = 2,
    CYC_RECV_CLOSED = 3,
    CYC_RECV_ERROR = 4
} cyc_recv_result;

typedef struct cyc_transport cyc_transport;

typedef struct cyc_transport_vtable {
    cyc_transport_kind (*kind)(const cyc_transport *transport);
    cyc_send_result (*send)(cyc_transport *transport, const uint8_t *bytes, size_t len,
                            size_t *accepted);
    cyc_recv_result (*recv)(cyc_transport *transport, uint8_t *buffer, size_t cap, size_t *received,
                            size_t *needed);
    void (*close_soft)(cyc_transport *transport);
    void (*close_hard)(cyc_transport *transport);
} cyc_transport_vtable;

struct cyc_transport {
    const cyc_transport_vtable *vtable;
    void *state;
};

typedef enum cyc_accept_result {
    CYC_ACCEPT_ACCEPTED = 0,
    CYC_ACCEPT_PENDING = 1,
    CYC_ACCEPT_ERROR = 2
} cyc_accept_result;

typedef struct cyc_listener cyc_listener;

typedef struct cyc_listener_vtable {
    cyc_accept_result (*accept)(cyc_listener *listener, cyc_transport *out);
    void (*close)(cyc_listener *listener);
} cyc_listener_vtable;

struct cyc_listener {
    const cyc_listener_vtable *vtable;
    void *state;
};

#ifdef __cplusplus
}
#endif

#endif /* CYCLONE_TRANSPORT_H */
