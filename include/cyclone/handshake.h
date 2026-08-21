#ifndef CYCLONE_HANDSHAKE_H
#define CYCLONE_HANDSHAKE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cyclone/common.h"
#include "cyclone/schema.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CYC_PROTOCOL_VERSION ((uint32_t)2)
#define CYC_QUERY_TAG ((uint8_t)4)

#define CYC_HELLO_HEADER_LEN ((size_t)16)
#define CYC_HELLO_ENTRY_LEN ((size_t)14)
#define CYC_QUERY_HEADER_LEN ((size_t)5)
#define CYC_QUERY_ENTRY_LEN ((size_t)6)
#define CYC_REPLY_HEADER_LEN ((size_t)4)
#define CYC_REPLY_ENTRY_LEN ((size_t)12)

typedef enum cyc_verdict {
    CYC_VERDICT_ACCEPT = 0,
    CYC_VERDICT_WRONG_VERSION = 1,
    CYC_VERDICT_SCHEMA_CONFLICT = 2,
    CYC_VERDICT_MALFORMED_HELLO = 3
} cyc_verdict;

typedef enum cyc_handshake_failure {
    CYC_FAIL_WRONG_VERSION = 1,
    CYC_FAIL_SCHEMA_CONFLICT = 2,
    CYC_FAIL_MALFORMED_HELLO = 3,
    CYC_FAIL_MALFORMED_PEER = 4,
    CYC_FAIL_TIMEOUT = 5
} cyc_handshake_failure;

const char *cyc_handshake_failure_name(cyc_handshake_failure failure);
cyc_handshake_failure cyc_handshake_failure_of(cyc_verdict verdict);

typedef struct cyc_hello_entry {
    uint32_t id;
    uint16_t field_count;
    uint64_t fingerprint;
} cyc_hello_entry;

typedef struct cyc_query_item {
    uint32_t id;
    uint16_t field_count;
} cyc_query_item;

typedef struct cyc_reply_item {
    uint32_t id;
    uint64_t fingerprint;
} cyc_reply_item;

size_t cyc_hello_len(const cyc_schema *schema);
size_t cyc_hello_encode(const cyc_schema *schema, uint8_t *out, size_t cap);

typedef struct cyc_hello_view {
    uint32_t version;
    uint64_t fingerprint;
    size_t count;
    const uint8_t *entries;
} cyc_hello_view;

bool cyc_hello_decode(const uint8_t *payload, size_t len, cyc_hello_view *hello);
cyc_hello_entry cyc_hello_entry_at(const cyc_hello_view *hello, size_t index);

typedef enum cyc_decision_kind {
    CYC_DECISION_ACCEPT = 0,
    CYC_DECISION_REJECT = 1,
    CYC_DECISION_QUERY = 2
} cyc_decision_kind;

typedef struct cyc_decision {
    cyc_decision_kind kind;
    cyc_verdict verdict;
    size_t query_count;
} cyc_decision;

cyc_decision cyc_handshake_decide(const cyc_schema *local, const cyc_hello_view *hello,
                                  cyc_query_item *queries, size_t query_cap);

size_t cyc_query_encode(const cyc_query_item *items, size_t count, uint8_t *out, size_t cap);
bool cyc_query_decode(const uint8_t *payload, size_t len, cyc_query_item *items, size_t cap,
                      size_t *count);

size_t cyc_reply_encode(const cyc_reply_item *items, size_t count, uint8_t *out, size_t cap);
bool cyc_reply_decode(const uint8_t *payload, size_t len, cyc_reply_item *items, size_t cap,
                      size_t *count);

bool cyc_query_answer(const cyc_schema *local, const cyc_query_item *items, size_t count,
                      cyc_reply_item *out);
cyc_verdict cyc_reply_check(const cyc_schema *local, const cyc_query_item *asked, size_t asked_count,
                            const cyc_reply_item *reply, size_t reply_count);

#ifdef __cplusplus
}
#endif

#endif /* CYCLONE_HANDSHAKE_H */
