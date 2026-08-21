#include "cyclone/handshake.h"

#include <string.h>

const char *cyc_handshake_failure_name(cyc_handshake_failure failure) {
    switch (failure) {
    case CYC_FAIL_WRONG_VERSION:
        return "the two peers speak different handshake versions";
    case CYC_FAIL_SCHEMA_CONFLICT:
        return "a message both peers know puts different fields at a shared index";
    case CYC_FAIL_MALFORMED_HELLO:
        return "the hello was rejected as malformed";
    case CYC_FAIL_MALFORMED_PEER:
        return "the peer sent unreadable handshake traffic";
    case CYC_FAIL_TIMEOUT:
        return "the handshake did not finish in time";
    }
    return "unknown";
}

cyc_handshake_failure cyc_handshake_failure_of(cyc_verdict verdict) {
    switch (verdict) {
    case CYC_VERDICT_WRONG_VERSION:
        return CYC_FAIL_WRONG_VERSION;
    case CYC_VERDICT_SCHEMA_CONFLICT:
        return CYC_FAIL_SCHEMA_CONFLICT;
    case CYC_VERDICT_MALFORMED_HELLO:
        return CYC_FAIL_MALFORMED_HELLO;
    case CYC_VERDICT_ACCEPT:
        break;
    }
    return CYC_FAIL_MALFORMED_PEER;
}

static uint16_t read_u16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8));
}

static uint32_t read_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static uint64_t read_u64(const uint8_t *bytes) {
    uint64_t value = 0;
    int index;
    for (index = 7; index >= 0; --index) {
        value = (value << 8) | (uint64_t)bytes[index];
    }
    return value;
}

static void write_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value & 0xFFu);
    bytes[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void write_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value & 0xFFu);
    bytes[1] = (uint8_t)((value >> 8) & 0xFFu);
    bytes[2] = (uint8_t)((value >> 16) & 0xFFu);
    bytes[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void write_u64(uint8_t *bytes, uint64_t value) {
    int index;
    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)((value >> (index * 8)) & 0xFFu);
    }
}

size_t cyc_hello_len(const cyc_schema *schema) {
    return CYC_HELLO_HEADER_LEN + CYC_HELLO_ENTRY_LEN * schema->message_count;
}

size_t cyc_hello_encode(const cyc_schema *schema, uint8_t *out, size_t cap) {
    size_t total = cyc_hello_len(schema);
    size_t index;

    if (cap < total) {
        return 0;
    }
    write_u32(out, CYC_PROTOCOL_VERSION);
    write_u64(out + 4, schema->fingerprint);
    write_u32(out + 12, (uint32_t)schema->message_count);
    for (index = 0; index < schema->message_count; ++index) {
        const cyc_message_schema *message = &schema->messages[index];
        uint8_t *at = out + CYC_HELLO_HEADER_LEN + CYC_HELLO_ENTRY_LEN * index;
        write_u32(at, message->id);
        write_u16(at + 4, cyc_message_field_count(message));
        write_u64(at + 6, message->fingerprint);
    }
    return total;
}

bool cyc_hello_decode(const uint8_t *payload, size_t len, cyc_hello_view *hello) {
    uint32_t declared;

    if (len < CYC_HELLO_HEADER_LEN) {
        return false;
    }
    declared = read_u32(payload + 12);
    if ((size_t)declared > CYC_MAX_SCHEMA_MESSAGES) {
        return false;
    }
    if (len != CYC_HELLO_HEADER_LEN + CYC_HELLO_ENTRY_LEN * (size_t)declared) {
        return false;
    }
    hello->version = read_u32(payload);
    hello->fingerprint = read_u64(payload + 4);
    hello->count = (size_t)declared;
    hello->entries = payload + CYC_HELLO_HEADER_LEN;
    return true;
}

cyc_hello_entry cyc_hello_entry_at(const cyc_hello_view *hello, size_t index) {
    const uint8_t *at = hello->entries + CYC_HELLO_ENTRY_LEN * index;
    cyc_hello_entry entry;
    entry.id = read_u32(at);
    entry.field_count = read_u16(at + 4);
    entry.fingerprint = read_u64(at + 6);
    return entry;
}

cyc_decision cyc_handshake_decide(const cyc_schema *local, const cyc_hello_view *hello,
                                  cyc_query_item *queries, size_t query_cap) {
    cyc_decision decision;
    size_t index;

    decision.kind = CYC_DECISION_ACCEPT;
    decision.verdict = CYC_VERDICT_ACCEPT;
    decision.query_count = 0;

    if (hello->fingerprint == local->fingerprint) {
        return decision;
    }

    for (index = 0; index < hello->count; ++index) {
        cyc_hello_entry entry = cyc_hello_entry_at(hello, index);
        const cyc_message_schema *known = cyc_schema_message(local, entry.id);
        uint16_t local_fields;
        uint64_t prefix;

        if (known == NULL) {
            continue;
        }
        if (entry.fingerprint == known->fingerprint) {
            continue;
        }
        local_fields = cyc_message_field_count(known);
        if (entry.field_count == 0 || local_fields == 0) {
            continue;
        }
        if (entry.field_count == local_fields) {
            decision.kind = CYC_DECISION_REJECT;
            decision.verdict = CYC_VERDICT_SCHEMA_CONFLICT;
            decision.query_count = 0;
            return decision;
        }
        if (entry.field_count < local_fields) {
            if (!cyc_message_prefix(known, entry.field_count, &prefix) ||
                prefix != entry.fingerprint) {
                decision.kind = CYC_DECISION_REJECT;
                decision.verdict = CYC_VERDICT_SCHEMA_CONFLICT;
                decision.query_count = 0;
                return decision;
            }
            continue;
        }
        if (decision.query_count >= query_cap) {
            decision.kind = CYC_DECISION_REJECT;
            decision.verdict = CYC_VERDICT_MALFORMED_HELLO;
            decision.query_count = 0;
            return decision;
        }
        queries[decision.query_count].id = entry.id;
        queries[decision.query_count].field_count = local_fields;
        decision.query_count += 1;
    }

    if (decision.query_count > 0) {
        decision.kind = CYC_DECISION_QUERY;
    }
    return decision;
}

size_t cyc_query_encode(const cyc_query_item *items, size_t count, uint8_t *out, size_t cap) {
    size_t total = CYC_QUERY_HEADER_LEN + CYC_QUERY_ENTRY_LEN * count;
    size_t index;

    if (cap < total) {
        return 0;
    }
    out[0] = CYC_QUERY_TAG;
    write_u32(out + 1, (uint32_t)count);
    for (index = 0; index < count; ++index) {
        uint8_t *at = out + CYC_QUERY_HEADER_LEN + CYC_QUERY_ENTRY_LEN * index;
        write_u32(at, items[index].id);
        write_u16(at + 4, items[index].field_count);
    }
    return total;
}

bool cyc_query_decode(const uint8_t *payload, size_t len, cyc_query_item *items, size_t cap,
                      size_t *count) {
    uint32_t declared;
    size_t index;

    if (len < CYC_QUERY_HEADER_LEN || payload[0] != CYC_QUERY_TAG) {
        return false;
    }
    declared = read_u32(payload + 1);
    if ((size_t)declared > CYC_MAX_SCHEMA_MESSAGES) {
        return false;
    }
    if (len != CYC_QUERY_HEADER_LEN + CYC_QUERY_ENTRY_LEN * (size_t)declared) {
        return false;
    }
    if ((size_t)declared > cap) {
        return false;
    }
    for (index = 0; index < (size_t)declared; ++index) {
        const uint8_t *at = payload + CYC_QUERY_HEADER_LEN + CYC_QUERY_ENTRY_LEN * index;
        items[index].id = read_u32(at);
        items[index].field_count = read_u16(at + 4);
    }
    *count = (size_t)declared;
    return true;
}

size_t cyc_reply_encode(const cyc_reply_item *items, size_t count, uint8_t *out, size_t cap) {
    size_t total = CYC_REPLY_HEADER_LEN + CYC_REPLY_ENTRY_LEN * count;
    size_t index;

    if (cap < total) {
        return 0;
    }
    write_u32(out, (uint32_t)count);
    for (index = 0; index < count; ++index) {
        uint8_t *at = out + CYC_REPLY_HEADER_LEN + CYC_REPLY_ENTRY_LEN * index;
        write_u32(at, items[index].id);
        write_u64(at + 4, items[index].fingerprint);
    }
    return total;
}

bool cyc_reply_decode(const uint8_t *payload, size_t len, cyc_reply_item *items, size_t cap,
                      size_t *count) {
    uint32_t declared;
    size_t index;

    if (len < CYC_REPLY_HEADER_LEN) {
        return false;
    }
    declared = read_u32(payload);
    if ((size_t)declared > CYC_MAX_SCHEMA_MESSAGES) {
        return false;
    }
    if (len != CYC_REPLY_HEADER_LEN + CYC_REPLY_ENTRY_LEN * (size_t)declared) {
        return false;
    }
    if ((size_t)declared > cap) {
        return false;
    }
    for (index = 0; index < (size_t)declared; ++index) {
        const uint8_t *at = payload + CYC_REPLY_HEADER_LEN + CYC_REPLY_ENTRY_LEN * index;
        items[index].id = read_u32(at);
        items[index].fingerprint = read_u64(at + 4);
    }
    *count = (size_t)declared;
    return true;
}

bool cyc_query_answer(const cyc_schema *local, const cyc_query_item *items, size_t count,
                      cyc_reply_item *out) {
    size_t index;

    for (index = 0; index < count; ++index) {
        const cyc_message_schema *known = cyc_schema_message(local, items[index].id);
        uint64_t prefix;
        if (known == NULL) {
            return false;
        }
        if (items[index].field_count == 0 ||
            items[index].field_count >= cyc_message_field_count(known)) {
            return false;
        }
        if (!cyc_message_prefix(known, items[index].field_count, &prefix)) {
            return false;
        }
        out[index].id = items[index].id;
        out[index].fingerprint = prefix;
    }
    return true;
}

cyc_verdict cyc_reply_check(const cyc_schema *local, const cyc_query_item *asked, size_t asked_count,
                            const cyc_reply_item *reply, size_t reply_count) {
    size_t index;

    if (asked_count != reply_count) {
        return CYC_VERDICT_MALFORMED_HELLO;
    }
    for (index = 0; index < asked_count; ++index) {
        const cyc_message_schema *known;
        uint64_t prefix;
        if (asked[index].id != reply[index].id) {
            return CYC_VERDICT_MALFORMED_HELLO;
        }
        known = cyc_schema_message(local, asked[index].id);
        if (known == NULL || !cyc_message_prefix(known, asked[index].field_count, &prefix) ||
            prefix != reply[index].fingerprint) {
            return CYC_VERDICT_SCHEMA_CONFLICT;
        }
    }
    return CYC_VERDICT_ACCEPT;
}
