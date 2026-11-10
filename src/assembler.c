#include "assembler.h"

#include <stdlib.h>
#include <string.h>

static size_t read_u16(const uint8_t *bytes) {
    return (size_t)bytes[0] | ((size_t)bytes[1] << 8);
}

static size_t read_u32(const uint8_t *bytes) {
    return (size_t)((uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
                    ((uint32_t)bytes[3] << 24));
}

fmx_frame_error fmx_part_decode(const uint8_t *bytes, size_t len, fmx_part *part) {
    size_t declared;
    if (len < FMX_PART_HEADER_LEN) {
        return FMX_FRAME_TRUNCATED;
    }
    declared = read_u32(bytes + 5);
    if (declared > FMX_PART_CONTENT_LEN) {
        return FMX_FRAME_HANDSHAKE_TOO_LARGE;
    }
    if (len < FMX_PART_HEADER_LEN + declared) {
        return FMX_FRAME_TRUNCATED;
    }
    if (len > FMX_PART_HEADER_LEN + declared) {
        return FMX_FRAME_TRAILING;
    }
    part->index = read_u16(bytes + 1);
    part->count = read_u16(bytes + 3);
    part->bytes = bytes + FMX_PART_HEADER_LEN;
    part->len = declared;
    return FMX_FRAME_OK;
}

size_t fmx_part_encode(size_t index, size_t count, const uint8_t *content, size_t len,
                       uint8_t *out) {
    out[0] = FMX_FRAME_HANDSHAKE_PART;
    out[1] = (uint8_t)(index & 0xFFu);
    out[2] = (uint8_t)((index >> 8) & 0xFFu);
    out[3] = (uint8_t)(count & 0xFFu);
    out[4] = (uint8_t)((count >> 8) & 0xFFu);
    out[5] = (uint8_t)(len & 0xFFu);
    out[6] = (uint8_t)((len >> 8) & 0xFFu);
    out[7] = (uint8_t)((len >> 16) & 0xFFu);
    out[8] = (uint8_t)((len >> 24) & 0xFFu);
    if (len > 0) {
        memcpy(out + FMX_PART_HEADER_LEN, content, len);
    }
    return FMX_PART_HEADER_LEN + len;
}

static bool begin(fmx_assembler *assembler, size_t count) {
    size_t capacity = count * FMX_PART_CONTENT_LEN;
    if (assembler->content_cap < capacity) {
        uint8_t *content = (uint8_t *)malloc(capacity);
        if (content == NULL) {
            return false;
        }
        free(assembler->content);
        assembler->content = content;
        assembler->content_cap = capacity;
    }
    if (assembler->received_cap < count) {
        bool *received = (bool *)calloc(count, sizeof(bool));
        if (received == NULL) {
            return false;
        }
        free(assembler->received);
        assembler->received = received;
        assembler->received_cap = count;
    }
    assembler->count = count;
    return true;
}

fmx_assembly fmx_assembler_add(fmx_assembler *assembler, const fmx_part *part) {
    bool last;
    size_t offset;

    if (part->count < 2 || part->count > FMX_MAX_HANDSHAKE_PARTS || part->index >= part->count) {
        return FMX_ASSEMBLY_BROKEN;
    }
    last = part->index == part->count - 1;
    if (last ? part->len == 0 : part->len != FMX_PART_CONTENT_LEN) {
        return FMX_ASSEMBLY_BROKEN;
    }

    if (assembler->count == 0) {
        if (!begin(assembler, part->count)) {
            return FMX_ASSEMBLY_NO_MEMORY;
        }
    } else if (part->count != assembler->count) {
        return FMX_ASSEMBLY_BROKEN;
    }

    offset = part->index * FMX_PART_CONTENT_LEN;
    if (assembler->received[part->index]) {
        if (last && part->len != assembler->last_len) {
            return FMX_ASSEMBLY_BROKEN;
        }
        if (memcmp(part->bytes, assembler->content + offset, part->len) != 0) {
            return FMX_ASSEMBLY_BROKEN;
        }
        return FMX_ASSEMBLY_PENDING;
    }

    memcpy(assembler->content + offset, part->bytes, part->len);
    assembler->received[part->index] = true;
    assembler->arrived += 1;
    if (last) {
        assembler->last_len = part->len;
    }

    if (assembler->arrived < assembler->count) {
        return FMX_ASSEMBLY_PENDING;
    }
    if ((assembler->count - 1) * FMX_PART_CONTENT_LEN + assembler->last_len >
        FMX_MAX_HANDSHAKE_PAYLOAD) {
        return FMX_ASSEMBLY_BROKEN;
    }
    return FMX_ASSEMBLY_COMPLETE;
}

bool fmx_assembler_pending(const fmx_assembler *assembler) {
    return assembler->count > 0;
}

const uint8_t *fmx_assembler_body(const fmx_assembler *assembler, size_t *len) {
    *len = (assembler->count - 1) * FMX_PART_CONTENT_LEN + assembler->last_len;
    return assembler->content;
}

void fmx_assembler_reset(fmx_assembler *assembler) {
    if (assembler->count > 0) {
        memset(assembler->received, 0, assembler->count * sizeof(bool));
    }
    assembler->count = 0;
    assembler->arrived = 0;
    assembler->last_len = 0;
}

void fmx_assembler_shrink(fmx_assembler *assembler) {
    if (assembler->count == 0) {
        fmx_assembler_release(assembler);
    }
}

void fmx_assembler_release(fmx_assembler *assembler) {
    free(assembler->content);
    free(assembler->received);
    memset(assembler, 0, sizeof(*assembler));
}
