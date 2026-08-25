#ifndef FOMOXA_FRAME_H
#define FOMOXA_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fomoxa/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FMX_FRAME_DATA ((uint8_t)0)
#define FMX_FRAME_PROBE ((uint8_t)1)
#define FMX_FRAME_ACK ((uint8_t)2)
#define FMX_FRAME_HANDSHAKE ((uint8_t)3)

#define FMX_MAGIC_C ((uint8_t)0x43)
#define FMX_MAGIC_Y ((uint8_t)0x59)

#define FMX_DATA_HEADER_LEN ((size_t)11)
#define FMX_HANDSHAKE_HEADER_LEN ((size_t)5)

#define FMX_MAX_MESSAGE_PAYLOAD ((size_t)(16u * 1024u * 1024u))
#define FMX_MAX_HANDSHAKE_PAYLOAD ((size_t)(1u * 1024u * 1024u))

typedef enum fmx_frame_error {
    FMX_FRAME_OK = 0,
    FMX_FRAME_INCOMPLETE = 1,
    FMX_FRAME_UNKNOWN_TYPE = 2,
    FMX_FRAME_BAD_MAGIC = 3,
    FMX_FRAME_MESSAGE_TOO_LARGE = 4,
    FMX_FRAME_HANDSHAKE_TOO_LARGE = 5,
    FMX_FRAME_TRUNCATED = 6,
    FMX_FRAME_TRAILING = 7
} fmx_frame_error;

const char *fmx_frame_error_name(fmx_frame_error error);

typedef struct fmx_frame {
    uint8_t type;
    uint32_t message_id;
    const uint8_t *payload;
    size_t payload_len;
} fmx_frame;

fmx_frame_error fmx_frame_decode(const uint8_t *bytes, size_t len, size_t max_message_bytes,
                                 fmx_frame *frame, size_t *used);
fmx_frame_error fmx_frame_decode_packet(const uint8_t *bytes, size_t len, size_t max_message_bytes,
                                        fmx_frame *frame);

size_t fmx_frame_data_len(size_t payload_len);
size_t fmx_frame_handshake_len(size_t payload_len);

fmx_frame_error fmx_frame_encode_data(uint32_t message_id, const uint8_t *payload,
                                      size_t payload_len, uint8_t *out, size_t cap,
                                      size_t *written);
fmx_frame_error fmx_frame_encode_handshake(const uint8_t *payload, size_t payload_len, uint8_t *out,
                                           size_t cap, size_t *written);
size_t fmx_frame_encode_probe(uint8_t *out, size_t cap);
size_t fmx_frame_encode_ack(uint8_t *out, size_t cap);

typedef struct fmx_stream_decoder {
    uint8_t *buf;
    size_t cap;
    size_t len;
    size_t start;
    size_t max_message_bytes;
    fmx_frame_error poison;
} fmx_stream_decoder;

fmx_result fmx_stream_decoder_init(fmx_stream_decoder *decoder, size_t max_message_bytes);
void fmx_stream_decoder_release(fmx_stream_decoder *decoder);
fmx_result fmx_stream_decoder_feed(fmx_stream_decoder *decoder, const uint8_t *bytes, size_t len);
fmx_frame_error fmx_stream_decoder_next(fmx_stream_decoder *decoder, fmx_frame *frame,
                                        size_t *frame_len);
void fmx_stream_decoder_advance(fmx_stream_decoder *decoder, size_t frame_len);
size_t fmx_stream_decoder_buffered(const fmx_stream_decoder *decoder);
bool fmx_stream_decoder_poisoned(const fmx_stream_decoder *decoder);
void fmx_stream_decoder_shrink(fmx_stream_decoder *decoder);

#ifdef __cplusplus
}
#endif

#endif /* FOMOXA_FRAME_H */
