#include "portable.h"

#include "fomoxa/common.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

const char *fmx_result_name(fmx_result result) {
    switch (result) {
    case FMX_OK:
        return "ok";
    case FMX_ERR_NOT_READY:
        return "the handshake has not been accepted yet";
    case FMX_ERR_CONGESTED:
        return "a frame is still waiting to go out";
    case FMX_ERR_TOO_LARGE:
        return "the transport cannot carry a frame this large";
    case FMX_ERR_CLOSED:
        return "the session is closed";
    case FMX_ERR_NO_MEMORY:
        return "out of memory";
    case FMX_ERR_INVALID:
        return "invalid argument";
    }
    return "unknown";
}

void fmx_config_defaults(fmx_config *config) {
    if (config == NULL) {
        return;
    }
    config->handshake_timeout_ms = 5000;
    config->heartbeat_interval_ms = 5000;
    config->heartbeat_timeout_ms = 15000;
    config->max_frames_per_tick = 64;
    config->max_message_bytes = 64u * 1024u;
    config->max_peers = 256;
}

uint64_t fmx_now_ms(void) {
#if defined(_WIN32)
    return (uint64_t)GetTickCount64();
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)(now.tv_nsec / 1000000L);
#endif
}
