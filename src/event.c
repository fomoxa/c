#include "fomoxa/event.h"

const char *fmx_disconnect_name(fmx_disconnect reason) {
    switch (reason) {
    case FMX_DISCONNECT_PEER_CLOSED:
        return "the peer closed the connection";
    case FMX_DISCONNECT_TRANSPORT_ERROR:
        return "the connection broke";
    case FMX_DISCONNECT_UNRESPONSIVE:
        return "the peer stopped answering probes";
    case FMX_DISCONNECT_LOCAL:
        return "the session was closed locally";
    }
    return "unknown";
}

const char *fmx_event_kind_name(fmx_event_kind kind) {
    switch (kind) {
    case FMX_EVENT_CONNECTED:
        return "connected";
    case FMX_EVENT_READY:
        return "ready";
    case FMX_EVENT_HANDSHAKE_FAILED:
        return "handshake failed";
    case FMX_EVENT_MESSAGE:
        return "message";
    case FMX_EVENT_PROBE:
        return "probe";
    case FMX_EVENT_ACK:
        return "ack";
    case FMX_EVENT_DISCONNECTED:
        return "disconnected";
    }
    return "unknown";
}
