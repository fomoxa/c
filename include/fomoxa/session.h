#ifndef FOMOXA_SESSION_H
#define FOMOXA_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fomoxa/common.h"
#include "fomoxa/event.h"
#include "fomoxa/frame.h"
#include "fomoxa/schema.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum fmx_role {
    FMX_ROLE_CLIENT = 0,
    FMX_ROLE_SERVER = 1
} fmx_role;

typedef enum fmx_state {
    FMX_STATE_HANDSHAKING = 0,
    FMX_STATE_READY = 1,
    FMX_STATE_CLOSED = 2
} fmx_state;

typedef enum fmx_out_kind {
    FMX_OUT_NONE = 0,
    FMX_OUT_PROBE = 1,
    FMX_OUT_ACK = 2,
    FMX_OUT_HANDSHAKE = 3
} fmx_out_kind;

typedef struct fmx_reaction {
    fmx_out_kind out;
    const uint8_t *out_payload;
    size_t out_payload_len;
    bool has_event;
    fmx_event_kind event;
    uint32_t message_id;
    const uint8_t *payload;
    size_t payload_len;
    int reason;
} fmx_reaction;

typedef struct fmx_session fmx_session;

fmx_session *fmx_session_create(fmx_role role, const fmx_schema *schema, const fmx_config *config,
                                uint64_t now_ms, fmx_reaction *opening);
void fmx_session_destroy(fmx_session *session);

void fmx_session_on_frame(fmx_session *session, const fmx_frame *frame, uint64_t now_ms,
                          fmx_reaction *out);
void fmx_session_tick(fmx_session *session, uint64_t now_ms, fmx_reaction *out);
void fmx_session_transport_closed(fmx_session *session, fmx_disconnect reason, fmx_reaction *out);
void fmx_session_close(fmx_session *session);

fmx_state fmx_session_state(const fmx_session *session);
fmx_role fmx_session_role(const fmx_session *session);
bool fmx_session_ready(const fmx_session *session);

#ifdef __cplusplus
}
#endif

#endif /* FOMOXA_SESSION_H */
