#ifndef FOMOXA_EVENT_H
#define FOMOXA_EVENT_H

#include <stddef.h>
#include <stdint.h>

#include "fomoxa/handshake.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum fmx_disconnect {
    FMX_DISCONNECT_PEER_CLOSED = 1,
    FMX_DISCONNECT_TRANSPORT_ERROR = 2,
    FMX_DISCONNECT_UNRESPONSIVE = 3,
    FMX_DISCONNECT_LOCAL = 4
} fmx_disconnect;

const char *fmx_disconnect_name(fmx_disconnect reason);

typedef enum fmx_event_kind {
    FMX_EVENT_CONNECTED = 0,
    FMX_EVENT_READY = 1,
    FMX_EVENT_HANDSHAKE_FAILED = 2,
    FMX_EVENT_MESSAGE = 3,
    FMX_EVENT_PROBE = 4,
    FMX_EVENT_ACK = 5,
    FMX_EVENT_DISCONNECTED = 6
} fmx_event_kind;

const char *fmx_event_kind_name(fmx_event_kind kind);

typedef struct fmx_event {
    fmx_event_kind kind;
    uint64_t peer;
    uint32_t message_id;
    const uint8_t *payload;
    size_t payload_len;
    int reason;
} fmx_event;

#ifdef __cplusplus
}
#endif

#endif /* FOMOXA_EVENT_H */
