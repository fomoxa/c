#ifndef CYCLONE_EVENT_H
#define CYCLONE_EVENT_H

#include <stddef.h>
#include <stdint.h>

#include "cyclone/handshake.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cyc_disconnect {
    CYC_DISCONNECT_PEER_CLOSED = 1,
    CYC_DISCONNECT_TRANSPORT_ERROR = 2,
    CYC_DISCONNECT_UNRESPONSIVE = 3,
    CYC_DISCONNECT_LOCAL = 4
} cyc_disconnect;

const char *cyc_disconnect_name(cyc_disconnect reason);

typedef enum cyc_event_kind {
    CYC_EVENT_CONNECTED = 0,
    CYC_EVENT_READY = 1,
    CYC_EVENT_HANDSHAKE_FAILED = 2,
    CYC_EVENT_MESSAGE = 3,
    CYC_EVENT_PROBE = 4,
    CYC_EVENT_ACK = 5,
    CYC_EVENT_DISCONNECTED = 6
} cyc_event_kind;

const char *cyc_event_kind_name(cyc_event_kind kind);

typedef struct cyc_event {
    cyc_event_kind kind;
    uint64_t peer;
    uint32_t message_id;
    const uint8_t *payload;
    size_t payload_len;
    int reason;
} cyc_event;

#ifdef __cplusplus
}
#endif

#endif /* CYCLONE_EVENT_H */
