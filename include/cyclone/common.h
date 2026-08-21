#ifndef CYCLONE_COMMON_H
#define CYCLONE_COMMON_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cyc_result {
    CYC_OK = 0,
    CYC_ERR_NOT_READY = -1,
    CYC_ERR_CONGESTED = -2,
    CYC_ERR_TOO_LARGE = -3,
    CYC_ERR_CLOSED = -4,
    CYC_ERR_NO_MEMORY = -5,
    CYC_ERR_INVALID = -6
} cyc_result;

const char *cyc_result_name(cyc_result result);

typedef struct cyc_config {
    uint32_t handshake_timeout_ms;
    uint32_t heartbeat_interval_ms;
    uint32_t heartbeat_timeout_ms;
    uint32_t max_frames_per_tick;
    uint32_t max_message_bytes;
    uint32_t max_peers;
} cyc_config;

void cyc_config_defaults(cyc_config *config);

uint64_t cyc_now_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* CYCLONE_COMMON_H */
