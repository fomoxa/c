#include "cyclone/frame.h"

#include <stdlib.h>
#include <string.h>

const char *cyc_frame_error_name(cyc_frame_error error) {
    switch (error) {
    case CYC_FRAME_OK:
        return "ok";
    case CYC_FRAME_INCOMPLETE:
        return "more bytes are needed";
    case CYC_FRAME_UNKNOWN_TYPE:
        return "frame type is not 0..=3";
    case CYC_FRAME_BAD_MAGIC:
        return "data frame magic is not 'C' 'Y'";
    case CYC_FRAME_MESSAGE_TOO_LARGE:
        return "message payload exceeds the accepted size";
    case CYC_FRAME_HANDSHAKE_TOO_LARGE:
        return "handshake payload exceeds the accepted size";
    case CYC_FRAME_TRUNCATED:
        return "packet ended before the frame it declared";
    case CYC_FRAME_TRAILING:
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
    if (configured == 0 || configured > CYC_MAX_MESSAGE_PAYLOAD) {
        return CYC_MAX_MESSAGE_PAYLOAD;
    }
    return configured;
}

cyc_frame_error cyc_frame_decode(const uint8_t *bytes, size_t len, size_t max_message_bytes,
                                 cyc_frame *frame, size_t *used) {
    size_t limit = message_limit(max_message_bytes);

    if (len == 0) {
        return CYC_FRAME_INCOMPLETE;
    }

    switch (bytes[0]) {
    case CYC_FRAME_PROBE:
    case CYC_FRAME_ACK:
        frame->type = bytes[0];
        frame->message_id = 0;
        frame->payload = NULL;
        frame->payload_len = 0;
        *used = 1;
        return CYC_FRAME_OK;

    case CYC_FRAME_DATA: {
        uint32_t declared;
        size_t total;
        if (len >= 3 && (bytes[1] != CYC_MAGIC_C || bytes[2] != CYC_MAGIC_Y)) {
            return CYC_FRAME_BAD_MAGIC;
        }
        if (len < CYC_DATA_HEADER_LEN) {
            return CYC_FRAME_INCOMPLETE;
        }
        declared = read_u32(bytes + 7);
        if ((size_t)declared > limit) {
            return CYC_FRAME_MESSAGE_TOO_LARGE;
        }
        total = CYC_DATA_HEADER_LEN + (size_t)declared;
        if (len < total) {
            return CYC_FRAME_INCOMPLETE;
        }
        frame->type = CYC_FRAME_DATA;
        frame->message_id = read_u32(bytes + 3);
        frame->payload = bytes + CYC_DATA_HEADER_LEN;
        frame->payload_len = (size_t)declared;
        *used = total;
        return CYC_FRAME_OK;
    }

    case CYC_FRAME_HANDSHAKE: {
        uint32_t declared;
        size_t total;
        if (len < CYC_HANDSHAKE_HEADER_LEN) {
            return CYC_FRAME_INCOMPLETE;
        }
        declared = read_u32(bytes + 1);
        if ((size_t)declared > CYC_MAX_HANDSHAKE_PAYLOAD) {
            return CYC_FRAME_HANDSHAKE_TOO_LARGE;
        }
        total = CYC_HANDSHAKE_HEADER_LEN + (size_t)declared;
        if (len < total) {
            return CYC_FRAME_INCOMPLETE;
        }
        frame->type = CYC_FRAME_HANDSHAKE;
        frame->message_id = 0;
        frame->payload = bytes + CYC_HANDSHAKE_HEADER_LEN;
        frame->payload_len = (size_t)declared;
        *used = total;
        return CYC_FRAME_OK;
    }

    default:
        return CYC_FRAME_UNKNOWN_TYPE;
    }
}

cyc_frame_error cyc_frame_decode_packet(const uint8_t *bytes, size_t len, size_t max_message_bytes,
                                        cyc_frame *frame) {
    size_t used = 0;
    cyc_frame_error error = cyc_frame_decode(bytes, len, max_message_bytes, frame, &used);
    if (error == CYC_FRAME_INCOMPLETE) {
        return CYC_FRAME_TRUNCATED;
    }
    if (error != CYC_FRAME_OK) {
        return error;
    }
    if (used != len) {
        return CYC_FRAME_TRAILING;
    }
    return CYC_FRAME_OK;
}

size_t cyc_frame_data_len(size_t payload_len) {
    return CYC_DATA_HEADER_LEN + payload_len;
}

size_t cyc_frame_handshake_len(size_t payload_len) {
    return CYC_HANDSHAKE_HEADER_LEN + payload_len;
}

cyc_frame_error cyc_frame_encode_data(uint32_t message_id, const uint8_t *payload,
                                      size_t payload_len, uint8_t *out, size_t cap,
                                      size_t *written) {
    size_t total = cyc_frame_data_len(payload_len);
    if (payload_len > CYC_MAX_MESSAGE_PAYLOAD) {
        return CYC_FRAME_MESSAGE_TOO_LARGE;
    }
    if (cap < total) {
        return CYC_FRAME_TRUNCATED;
    }
    out[0] = CYC_FRAME_DATA;
    out[1] = CYC_MAGIC_C;
    out[2] = CYC_MAGIC_Y;
    write_u32(out + 3, message_id);
    write_u32(out + 7, (uint32_t)payload_len);
    if (payload_len > 0) {
        memcpy(out + CYC_DATA_HEADER_LEN, payload, payload_len);
    }
    *written = total;
    return CYC_FRAME_OK;
}

cyc_frame_error cyc_frame_encode_handshake(const uint8_t *payload, size_t payload_len, uint8_t *out,
                                           size_t cap, size_t *written) {
    size_t total = cyc_frame_handshake_len(payload_len);
    if (payload_len > CYC_MAX_HANDSHAKE_PAYLOAD) {
        return CYC_FRAME_HANDSHAKE_TOO_LARGE;
    }
    if (cap < total) {
        return CYC_FRAME_TRUNCATED;
    }
    out[0] = CYC_FRAME_HANDSHAKE;
    write_u32(out + 1, (uint32_t)payload_len);
    if (payload_len > 0) {
        memcpy(out + CYC_HANDSHAKE_HEADER_LEN, payload, payload_len);
    }
    *written = total;
    return CYC_FRAME_OK;
}

size_t cyc_frame_encode_probe(uint8_t *out, size_t cap) {
    if (cap < 1) {
        return 0;
    }
    out[0] = CYC_FRAME_PROBE;
    return 1;
}

size_t cyc_frame_encode_ack(uint8_t *out, size_t cap) {
    if (cap < 1) {
        return 0;
    }
    out[0] = CYC_FRAME_ACK;
    return 1;
}

cyc_result cyc_stream_decoder_init(cyc_stream_decoder *decoder, size_t max_message_bytes) {
    if (decoder == NULL) {
        return CYC_ERR_INVALID;
    }
    decoder->buf = NULL;
    decoder->cap = 0;
    decoder->len = 0;
    decoder->start = 0;
    decoder->max_message_bytes = message_limit(max_message_bytes);
    decoder->poison = CYC_FRAME_OK;
    return CYC_OK;
}

void cyc_stream_decoder_release(cyc_stream_decoder *decoder) {
    if (decoder == NULL) {
        return;
    }
    free(decoder->buf);
    decoder->buf = NULL;
    decoder->cap = 0;
    decoder->len = 0;
    decoder->start = 0;
}

cyc_result cyc_stream_decoder_feed(cyc_stream_decoder *decoder, const uint8_t *bytes, size_t len) {
    size_t needed;

    if (decoder == NULL || (bytes == NULL && len > 0)) {
        return CYC_ERR_INVALID;
    }
    if (len == 0) {
        return CYC_OK;
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
            return CYC_ERR_NO_MEMORY;
        }
        decoder->buf = buf;
        decoder->cap = grown;
    }

    memcpy(decoder->buf + decoder->len, bytes, len);
    decoder->len += len;
    return CYC_OK;
}

cyc_frame_error cyc_stream_decoder_next(cyc_stream_decoder *decoder, cyc_frame *frame,
                                        size_t *frame_len) {
    cyc_frame_error error;

    if (decoder->poison != CYC_FRAME_OK) {
        return decoder->poison;
    }
    error = cyc_frame_decode(decoder->buf + decoder->start, decoder->len - decoder->start,
                             decoder->max_message_bytes, frame, frame_len);
    if (error != CYC_FRAME_OK && error != CYC_FRAME_INCOMPLETE) {
        decoder->poison = error;
    }
    return error;
}

void cyc_stream_decoder_advance(cyc_stream_decoder *decoder, size_t frame_len) {
    decoder->start += frame_len;
    if (decoder->start >= decoder->len) {
        decoder->start = 0;
        decoder->len = 0;
    }
}

size_t cyc_stream_decoder_buffered(const cyc_stream_decoder *decoder) {
    return decoder->len - decoder->start;
}

bool cyc_stream_decoder_poisoned(const cyc_stream_decoder *decoder) {
    return decoder->poison != CYC_FRAME_OK;
}
