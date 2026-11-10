#ifndef FOMOXA_INTERNAL_ASSEMBLER_H
#define FOMOXA_INTERNAL_ASSEMBLER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fomoxa/frame.h"

typedef struct fmx_part {
    size_t index;
    size_t count;
    const uint8_t *bytes;
    size_t len;
} fmx_part;

fmx_frame_error fmx_part_decode(const uint8_t *bytes, size_t len, fmx_part *part);
size_t fmx_part_encode(size_t index, size_t count, const uint8_t *content, size_t len,
                       uint8_t *out);

typedef enum fmx_assembly {
    FMX_ASSEMBLY_PENDING = 0,
    FMX_ASSEMBLY_COMPLETE = 1,
    FMX_ASSEMBLY_BROKEN = 2,
    FMX_ASSEMBLY_NO_MEMORY = 3
} fmx_assembly;

typedef struct fmx_assembler {
    uint8_t *content;
    size_t content_cap;
    bool *received;
    size_t received_cap;
    size_t count;
    size_t arrived;
    size_t last_len;
} fmx_assembler;

fmx_assembly fmx_assembler_add(fmx_assembler *assembler, const fmx_part *part);
bool fmx_assembler_pending(const fmx_assembler *assembler);
const uint8_t *fmx_assembler_body(const fmx_assembler *assembler, size_t *len);
void fmx_assembler_reset(fmx_assembler *assembler);
void fmx_assembler_shrink(fmx_assembler *assembler);
void fmx_assembler_release(fmx_assembler *assembler);

#endif /* FOMOXA_INTERNAL_ASSEMBLER_H */
