#ifndef FOMOXA_HANDSHAKE_H
#define FOMOXA_HANDSHAKE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fomoxa/common.h"
#include "fomoxa/schema.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FMX_PROTOCOL_VERSION ((uint32_t)2)
#define FMX_QUERY_TAG ((uint8_t)4)

#define FMX_HELLO_HEADER_LEN ((size_t)16)
#define FMX_HELLO_ENTRY_LEN ((size_t)14)
#define FMX_QUERY_HEADER_LEN ((size_t)5)
#define FMX_QUERY_ENTRY_LEN ((size_t)6)
#define FMX_REPLY_HEADER_LEN ((size_t)4)
#define FMX_REPLY_ENTRY_LEN ((size_t)12)

typedef enum fmx_verdict {
    FMX_VERDICT_ACCEPT = 0,
    FMX_VERDICT_WRONG_VERSION = 1,
    FMX_VERDICT_SCHEMA_CONFLICT = 2,
    FMX_VERDICT_MALFORMED_HELLO = 3
} fmx_verdict;

typedef enum fmx_handshake_failure {
    FMX_FAIL_WRONG_VERSION = 1,
    FMX_FAIL_SCHEMA_CONFLICT = 2,
    FMX_FAIL_MALFORMED_HELLO = 3,
    FMX_FAIL_MALFORMED_PEER = 4,
    FMX_FAIL_TIMEOUT = 5
} fmx_handshake_failure;

const char *fmx_handshake_failure_name(fmx_handshake_failure failure);
fmx_handshake_failure fmx_handshake_failure_of(fmx_verdict verdict);

typedef struct fmx_hello_entry {
    uint32_t id;
    uint16_t field_count;
    uint64_t fingerprint;
} fmx_hello_entry;

typedef struct fmx_query_item {
    uint32_t id;
    uint16_t field_count;
} fmx_query_item;

typedef struct fmx_reply_item {
    uint32_t id;
    uint64_t fingerprint;
} fmx_reply_item;

size_t fmx_hello_len(const fmx_schema *schema);
size_t fmx_hello_encode(const fmx_schema *schema, uint8_t *out, size_t cap);

typedef struct fmx_hello_view {
    uint32_t version;
    uint64_t fingerprint;
    size_t count;
    const uint8_t *entries;
} fmx_hello_view;

bool fmx_hello_decode(const uint8_t *payload, size_t len, fmx_hello_view *hello);
fmx_hello_entry fmx_hello_entry_at(const fmx_hello_view *hello, size_t index);

typedef enum fmx_decision_kind {
    FMX_DECISION_ACCEPT = 0,
    FMX_DECISION_REJECT = 1,
    FMX_DECISION_QUERY = 2
} fmx_decision_kind;

typedef struct fmx_decision {
    fmx_decision_kind kind;
    fmx_verdict verdict;
    size_t query_count;
} fmx_decision;

fmx_decision fmx_handshake_decide(const fmx_schema *local, const fmx_hello_view *hello,
                                  fmx_query_item *queries, size_t query_cap);

size_t fmx_query_encode(const fmx_query_item *items, size_t count, uint8_t *out, size_t cap);
bool fmx_query_decode(const uint8_t *payload, size_t len, fmx_query_item *items, size_t cap,
                      size_t *count);

size_t fmx_reply_encode(const fmx_reply_item *items, size_t count, uint8_t *out, size_t cap);
bool fmx_reply_decode(const uint8_t *payload, size_t len, fmx_reply_item *items, size_t cap,
                      size_t *count);

bool fmx_query_answer(const fmx_schema *local, const fmx_query_item *items, size_t count,
                      fmx_reply_item *out);
fmx_verdict fmx_reply_check(const fmx_schema *local, const fmx_query_item *asked, size_t asked_count,
                            const fmx_reply_item *reply, size_t reply_count);

#ifdef __cplusplus
}
#endif

#endif /* FOMOXA_HANDSHAKE_H */
