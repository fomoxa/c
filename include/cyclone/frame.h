#ifndef CYCLONE_FRAME_H
#define CYCLONE_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cyclone/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CYC_FRAME_DATA ((uint8_t)0)
#define CYC_FRAME_PROBE ((uint8_t)1)
#define CYC_FRAME_ACK ((uint8_t)2)
#define CYC_FRAME_HANDSHAKE ((uint8_t)3)

#define CYC_MAGIC_C ((uint8_t)0x43)
#define CYC_MAGIC_Y ((uint8_t)0x59)

#define CYC_DATA_HEADER_LEN ((size_t)11)
#define CYC_HANDSHAKE_HEADER_LEN ((size_t)5)

#define CYC_MAX_MESSAGE_PAYLOAD ((size_t)(16u * 1024u * 1024u))
#define CYC_MAX_HANDSHAKE_PAYLOAD ((size_t)(1u * 1024u * 1024u))

typedef enum cyc_frame_error {
    CYC_FRAME_OK = 0,
    CYC_FRAME_INCOMPLETE = 1,
    CYC_FRAME_UNKNOWN_TYPE = 2,
    CYC_FRAME_BAD_MAGIC = 3,
    CYC_FRAME_MESSAGE_TOO_LARGE = 4,
    CYC_FRAME_HANDSHAKE_TOO_LARGE = 5,
    CYC_FRAME_TRUNCATED = 6,
    CYC_FRAME_TRAILING = 7
} cyc_frame_error;

const char *cyc_frame_error_name(cyc_frame_error error);

typedef struct cyc_frame {
    uint8_t type;
    uint32_t message_id;
    const uint8_t *payload;
    size_t payload_len;
} cyc_frame;

cyc_frame_error cyc_frame_decode(const uint8_t *bytes, size_t len, size_t max_message_bytes,
                                 cyc_frame *frame, size_t *used);
cyc_frame_error cyc_frame_decode_packet(const uint8_t *bytes, size_t len, size_t max_message_bytes,
                                        cyc_frame *frame);

size_t cyc_frame_data_len(size_t payload_len);
size_t cyc_frame_handshake_len(size_t payload_len);

cyc_frame_error cyc_frame_encode_data(uint32_t message_id, const uint8_t *payload,
                                      size_t payload_len, uint8_t *out, size_t cap,
                                      size_t *written);
cyc_frame_error cyc_frame_encode_handshake(const uint8_t *payload, size_t payload_len, uint8_t *out,
                                           size_t cap, size_t *written);
size_t cyc_frame_encode_probe(uint8_t *out, size_t cap);
size_t cyc_frame_encode_ack(uint8_t *out, size_t cap);

typedef struct cyc_stream_decoder {
    uint8_t *buf;
    size_t cap;
    size_t len;
    size_t start;
    size_t max_message_bytes;
    cyc_frame_error poison;
} cyc_stream_decoder;

cyc_result cyc_stream_decoder_init(cyc_stream_decoder *decoder, size_t max_message_bytes);
void cyc_stream_decoder_release(cyc_stream_decoder *decoder);
cyc_result cyc_stream_decoder_feed(cyc_stream_decoder *decoder, const uint8_t *bytes, size_t len);
cyc_frame_error cyc_stream_decoder_next(cyc_stream_decoder *decoder, cyc_frame *frame,
                                        size_t *frame_len);
void cyc_stream_decoder_advance(cyc_stream_decoder *decoder, size_t frame_len);
size_t cyc_stream_decoder_buffered(const cyc_stream_decoder *decoder);
bool cyc_stream_decoder_poisoned(const cyc_stream_decoder *decoder);

#ifdef __cplusplus
}
#endif

#endif /* CYCLONE_FRAME_H */
