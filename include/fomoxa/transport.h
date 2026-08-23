#ifndef FOMOXA_TRANSPORT_H
#define FOMOXA_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum fmx_transport_kind {
    FMX_TRANSPORT_STREAM = 0,
    FMX_TRANSPORT_MESSAGE = 1
} fmx_transport_kind;

typedef enum fmx_send_result {
    FMX_SEND_SENT = 0,
    FMX_SEND_PARTIAL = 1,
    FMX_SEND_WOULD_BLOCK = 2,
    FMX_SEND_TOO_LARGE = 3,
    FMX_SEND_CLOSED = 4,
    FMX_SEND_ERROR = 5
} fmx_send_result;

typedef enum fmx_recv_result {
    FMX_RECV_RECEIVED = 0,
    FMX_RECV_WOULD_BLOCK = 1,
    FMX_RECV_NEED_CAPACITY = 2,
    FMX_RECV_CLOSED = 3,
    FMX_RECV_ERROR = 4
} fmx_recv_result;

typedef struct fmx_transport fmx_transport;

typedef struct fmx_transport_vtable {
    fmx_transport_kind (*kind)(const fmx_transport *transport);
    fmx_send_result (*send)(fmx_transport *transport, const uint8_t *bytes, size_t len,
                            size_t *accepted);
    fmx_recv_result (*recv)(fmx_transport *transport, uint8_t *buffer, size_t cap, size_t *received,
                            size_t *needed);
    void (*close_soft)(fmx_transport *transport);
    void (*close_hard)(fmx_transport *transport);
} fmx_transport_vtable;

struct fmx_transport {
    const fmx_transport_vtable *vtable;
    void *state;
};

typedef enum fmx_accept_result {
    FMX_ACCEPT_ACCEPTED = 0,
    FMX_ACCEPT_PENDING = 1,
    FMX_ACCEPT_ERROR = 2
} fmx_accept_result;

typedef struct fmx_listener fmx_listener;

typedef struct fmx_listener_vtable {
    fmx_accept_result (*accept)(fmx_listener *listener, fmx_transport *out);
    void (*close)(fmx_listener *listener);
} fmx_listener_vtable;

struct fmx_listener {
    const fmx_listener_vtable *vtable;
    void *state;
};

#ifdef __cplusplus
}
#endif

#endif /* FOMOXA_TRANSPORT_H */
