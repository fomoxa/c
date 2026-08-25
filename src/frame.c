#include "fomoxa/frame.h"

#include <stdlib.h>
#include <string.h>

const char *fmx_frame_error_name(fmx_frame_error error) {
    switch (error) {
    case FMX_FRAME_OK:
        return "ok";
    case FMX_FRAME_INCOMPLETE:
        return "more bytes are needed";
    case FMX_FRAME_UNKNOWN_TYPE:
        return "frame type is not 0..=3";
    case FMX_FRAME_BAD_MAGIC:
        return "data frame magic is not 'C' 'Y'";
    case FMX_FRAME_MESSAGE_TOO_LARGE:
        return "message payload exceeds the accepted size";
    case FMX_FRAME_HANDSHAKE_TOO_LARGE:
        return "handshake payload exceeds the accepted size";
    case FMX_FRAME_TRUNCATED:
        return "packet ended before the frame it declared";
    case FMX_FRAME_TRAILING:
        return "bytes left over after a complete frame";
    }
    return "unknown";
}

static uint32_t read_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static void write_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value & 0xFFu);
    bytes[1] = (uint8_t)((value >> 8) & 0xFFu);
    bytes[2] = (uint8_t)((value >> 16) & 0xFFu);
    bytes[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static size_t message_limit(size_t configured) {
    if (configured == 0 || configured > FMX_MAX_MESSAGE_PAYLOAD) {
        return FMX_MAX_MESSAGE_PAYLOAD;
    }
    return configured;
}

fmx_frame_error fmx_frame_decode(const uint8_t *bytes, size_t len, size_t max_message_bytes,
                                 fmx_frame *frame, size_t *used) {
    size_t limit = message_limit(max_message_bytes);

    if (len == 0) {
        return FMX_FRAME_INCOMPLETE;
    }

    switch (bytes[0]) {
    case FMX_FRAME_PROBE:
    case FMX_FRAME_ACK:
        frame->type = bytes[0];
        frame->message_id = 0;
        frame->payload = NULL;
        frame->payload_len = 0;
        *used = 1;
        return FMX_FRAME_OK;

    case FMX_FRAME_DATA: {
        uint32_t declared;
        size_t total;
        if (len >= 3 && (bytes[1] != FMX_MAGIC_C || bytes[2] != FMX_MAGIC_Y)) {
            return FMX_FRAME_BAD_MAGIC;
        }
        if (len < FMX_DATA_HEADER_LEN) {
            return FMX_FRAME_INCOMPLETE;
        }
        declared = read_u32(bytes + 7);
        if ((size_t)declared > limit) {
            return FMX_FRAME_MESSAGE_TOO_LARGE;
        }
        total = FMX_DATA_HEADER_LEN + (size_t)declared;
        if (len < total) {
            return FMX_FRAME_INCOMPLETE;
        }
        frame->type = FMX_FRAME_DATA;
        frame->message_id = read_u32(bytes + 3);
        frame->payload = bytes + FMX_DATA_HEADER_LEN;
        frame->payload_len = (size_t)declared;
        *used = total;
        return FMX_FRAME_OK;
    }

    case FMX_FRAME_HANDSHAKE: {
        uint32_t declared;
        size_t total;
        if (len < FMX_HANDSHAKE_HEADER_LEN) {
            return FMX_FRAME_INCOMPLETE;
        }
        declared = read_u32(bytes + 1);
        if ((size_t)declared > FMX_MAX_HANDSHAKE_PAYLOAD) {
            return FMX_FRAME_HANDSHAKE_TOO_LARGE;
        }
        total = FMX_HANDSHAKE_HEADER_LEN + (size_t)declared;
        if (len < total) {
            return FMX_FRAME_INCOMPLETE;
        }
        frame->type = FMX_FRAME_HANDSHAKE;
        frame->message_id = 0;
        frame->payload = bytes + FMX_HANDSHAKE_HEADER_LEN;
        frame->payload_len = (size_t)declared;
        *used = total;
        return FMX_FRAME_OK;
    }

    default:
        return FMX_FRAME_UNKNOWN_TYPE;
    }
}

fmx_frame_error fmx_frame_decode_packet(const uint8_t *bytes, size_t len, size_t max_message_bytes,
                                        fmx_frame *frame) {
    size_t used = 0;
    fmx_frame_error error = fmx_frame_decode(bytes, len, max_message_bytes, frame, &used);
    if (error == FMX_FRAME_INCOMPLETE) {
        return FMX_FRAME_TRUNCATED;
    }
    if (error != FMX_FRAME_OK) {
        return error;
    }
    if (used != len) {
        return FMX_FRAME_TRAILING;
    }
    return FMX_FRAME_OK;
}

size_t fmx_frame_data_len(size_t payload_len) {
    return FMX_DATA_HEADER_LEN + payload_len;
}

size_t fmx_frame_handshake_len(size_t payload_len) {
    return FMX_HANDSHAKE_HEADER_LEN + payload_len;
}

fmx_frame_error fmx_frame_encode_data(uint32_t message_id, const uint8_t *payload,
                                      size_t payload_len, uint8_t *out, size_t cap,
                                      size_t *written) {
    size_t total = fmx_frame_data_len(payload_len);
    if (payload_len > FMX_MAX_MESSAGE_PAYLOAD) {
        return FMX_FRAME_MESSAGE_TOO_LARGE;
    }
    if (cap < total) {
        return FMX_FRAME_TRUNCATED;
    }
    out[0] = FMX_FRAME_DATA;
    out[1] = FMX_MAGIC_C;
    out[2] = FMX_MAGIC_Y;
    write_u32(out + 3, message_id);
    write_u32(out + 7, (uint32_t)payload_len);
    if (payload_len > 0) {
        memcpy(out + FMX_DATA_HEADER_LEN, payload, payload_len);
    }
    *written = total;
    return FMX_FRAME_OK;
}

fmx_frame_error fmx_frame_encode_handshake(const uint8_t *payload, size_t payload_len, uint8_t *out,
                                           size_t cap, size_t *written) {
    size_t total = fmx_frame_handshake_len(payload_len);
    if (payload_len > FMX_MAX_HANDSHAKE_PAYLOAD) {
        return FMX_FRAME_HANDSHAKE_TOO_LARGE;
    }
    if (cap < total) {
        return FMX_FRAME_TRUNCATED;
    }
    out[0] = FMX_FRAME_HANDSHAKE;
    write_u32(out + 1, (uint32_t)payload_len);
    if (payload_len > 0) {
        memcpy(out + FMX_HANDSHAKE_HEADER_LEN, payload, payload_len);
    }
    *written = total;
    return FMX_FRAME_OK;
}

size_t fmx_frame_encode_probe(uint8_t *out, size_t cap) {
    if (cap < 1) {
        return 0;
    }
    out[0] = FMX_FRAME_PROBE;
    return 1;
}

size_t fmx_frame_encode_ack(uint8_t *out, size_t cap) {
    if (cap < 1) {
        return 0;
    }
    out[0] = FMX_FRAME_ACK;
    return 1;
}

fmx_result fmx_stream_decoder_init(fmx_stream_decoder *decoder, size_t max_message_bytes) {
    if (decoder == NULL) {
        return FMX_ERR_INVALID;
    }
    decoder->buf = NULL;
    decoder->cap = 0;
    decoder->len = 0;
    decoder->start = 0;
    decoder->max_message_bytes = message_limit(max_message_bytes);
    decoder->poison = FMX_FRAME_OK;
    return FMX_OK;
}

void fmx_stream_decoder_release(fmx_stream_decoder *decoder) {
    if (decoder == NULL) {
        return;
    }
    free(decoder->buf);
    decoder->buf = NULL;
    decoder->cap = 0;
    decoder->len = 0;
    decoder->start = 0;
}

fmx_result fmx_stream_decoder_feed(fmx_stream_decoder *decoder, const uint8_t *bytes, size_t len) {
    size_t needed;

    if (decoder == NULL || (bytes == NULL && len > 0)) {
        return FMX_ERR_INVALID;
    }
    if (len == 0) {
        return FMX_OK;
    }

    if (decoder->start > 0) {
        memmove(decoder->buf, decoder->buf + decoder->start, decoder->len - decoder->start);
        decoder->len -= decoder->start;
        decoder->start = 0;
    }

    needed = decoder->len + len;
    if (needed > decoder->cap) {
        size_t grown = decoder->cap == 0 ? 1024 : decoder->cap;
        uint8_t *buf;
        while (grown < needed) {
            grown *= 2;
        }
        buf = (uint8_t *)realloc(decoder->buf, grown);
        if (buf == NULL) {
            return FMX_ERR_NO_MEMORY;
        }
        decoder->buf = buf;
        decoder->cap = grown;
    }

    memcpy(decoder->buf + decoder->len, bytes, len);
    decoder->len += len;
    return FMX_OK;
}

fmx_frame_error fmx_stream_decoder_next(fmx_stream_decoder *decoder, fmx_frame *frame,
                                        size_t *frame_len) {
    fmx_frame_error error;

    if (decoder->poison != FMX_FRAME_OK) {
        return decoder->poison;
    }
    error = fmx_frame_decode(decoder->buf + decoder->start, decoder->len - decoder->start,
                             decoder->max_message_bytes, frame, frame_len);
    if (error != FMX_FRAME_OK && error != FMX_FRAME_INCOMPLETE) {
        decoder->poison = error;
    }
    return error;
}

void fmx_stream_decoder_advance(fmx_stream_decoder *decoder, size_t frame_len) {
    decoder->start += frame_len;
    if (decoder->start >= decoder->len) {
        decoder->start = 0;
        decoder->len = 0;
    }
}

size_t fmx_stream_decoder_buffered(const fmx_stream_decoder *decoder) {
    return decoder->len - decoder->start;
}

bool fmx_stream_decoder_poisoned(const fmx_stream_decoder *decoder) {
    return decoder->poison != FMX_FRAME_OK;
}

void fmx_stream_decoder_shrink(fmx_stream_decoder *decoder) {
    if (decoder->len - decoder->start == 0 && decoder->cap > 0) {
        free(decoder->buf);
        decoder->buf = NULL;
        decoder->cap = 0;
        decoder->len = 0;
        decoder->start = 0;
    }
}
