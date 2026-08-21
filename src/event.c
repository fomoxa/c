#include "cyclone/event.h"

const char *cyc_disconnect_name(cyc_disconnect reason) {
    switch (reason) {
    case CYC_DISCONNECT_PEER_CLOSED:
        return "the peer closed the connection";
    case CYC_DISCONNECT_TRANSPORT_ERROR:
        return "the connection broke";
    case CYC_DISCONNECT_UNRESPONSIVE:
        return "the peer stopped answering probes";
    case CYC_DISCONNECT_LOCAL:
        return "the session was closed locally";
    }
    return "unknown";
}

const char *cyc_event_kind_name(cyc_event_kind kind) {
    switch (kind) {
    case CYC_EVENT_CONNECTED:
        return "connected";
    case CYC_EVENT_READY:
        return "ready";
    case CYC_EVENT_HANDSHAKE_FAILED:
        return "handshake failed";
    case CYC_EVENT_MESSAGE:
        return "message";
    case CYC_EVENT_PROBE:
        return "probe";
    case CYC_EVENT_ACK:
        return "ack";
    case CYC_EVENT_DISCONNECTED:
        return "disconnected";
    }
    return "unknown";
}
