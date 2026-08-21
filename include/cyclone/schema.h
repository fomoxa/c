#ifndef CYCLONE_SCHEMA_H
#define CYCLONE_SCHEMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cyclone/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CYC_MAX_SCHEMA_MESSAGES ((size_t)1000000)

typedef struct cyc_message_schema {
    uint32_t id;
    uint64_t fingerprint;
    const uint64_t *prefixes;
    size_t prefix_count;
} cyc_message_schema;

typedef struct cyc_schema {
    uint64_t fingerprint;
    const cyc_message_schema *messages;
    size_t message_count;
} cyc_schema;

cyc_result cyc_schema_check(const cyc_schema *schema);
const cyc_message_schema *cyc_schema_message(const cyc_schema *schema, uint32_t id);
uint16_t cyc_message_field_count(const cyc_message_schema *message);
bool cyc_message_prefix(const cyc_message_schema *message, uint16_t field_count, uint64_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CYCLONE_SCHEMA_H */
