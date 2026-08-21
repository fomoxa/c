#ifndef CYCLONE_SESSION_H
#define CYCLONE_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cyclone/common.h"
#include "cyclone/event.h"
#include "cyclone/frame.h"
#include "cyclone/schema.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cyc_role {
    CYC_ROLE_CLIENT = 0,
    CYC_ROLE_SERVER = 1
} cyc_role;

typedef enum cyc_state {
    CYC_STATE_HANDSHAKING = 0,
    CYC_STATE_READY = 1,
    CYC_STATE_CLOSED = 2
} cyc_state;

typedef enum cyc_out_kind {
    CYC_OUT_NONE = 0,
    CYC_OUT_PROBE = 1,
    CYC_OUT_ACK = 2,
    CYC_OUT_HANDSHAKE = 3
} cyc_out_kind;

typedef struct cyc_reaction {
    cyc_out_kind out;
    const uint8_t *out_payload;
    size_t out_payload_len;
    bool has_event;
    cyc_event_kind event;
    uint32_t message_id;
    const uint8_t *payload;
    size_t payload_len;
    int reason;
} cyc_reaction;

typedef struct cyc_session cyc_session;

cyc_session *cyc_session_create(cyc_role role, const cyc_schema *schema, const cyc_config *config,
                                uint64_t now_ms, cyc_reaction *opening);
void cyc_session_destroy(cyc_session *session);

void cyc_session_on_frame(cyc_session *session, const cyc_frame *frame, uint64_t now_ms,
                          cyc_reaction *out);
void cyc_session_tick(cyc_session *session, uint64_t now_ms, cyc_reaction *out);
void cyc_session_transport_closed(cyc_session *session, cyc_disconnect reason, cyc_reaction *out);
void cyc_session_close(cyc_session *session);

cyc_state cyc_session_state(const cyc_session *session);
cyc_role cyc_session_role(const cyc_session *session);
bool cyc_session_ready(const cyc_session *session);

#ifdef __cplusplus
}
#endif

#endif /* CYCLONE_SESSION_H */
