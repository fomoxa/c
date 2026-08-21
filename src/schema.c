#include "cyclone/schema.h"

cyc_result cyc_schema_check(const cyc_schema *schema) {
    size_t index;

    if (schema == NULL || (schema->messages == NULL && schema->message_count > 0)) {
        return CYC_ERR_INVALID;
    }
    if (schema->message_count > CYC_MAX_SCHEMA_MESSAGES) {
        return CYC_ERR_INVALID;
    }

    for (index = 0; index < schema->message_count; ++index) {
        const cyc_message_schema *message = &schema->messages[index];
        if (message->prefix_count > (size_t)UINT16_MAX) {
            return CYC_ERR_INVALID;
        }
        if (message->prefix_count > 0 && message->prefixes == NULL) {
            return CYC_ERR_INVALID;
        }
        if (message->prefix_count > 0 &&
            message->prefixes[message->prefix_count - 1] != message->fingerprint) {
            return CYC_ERR_INVALID;
        }
        if (index > 0 && schema->messages[index - 1].id >= message->id) {
            return CYC_ERR_INVALID;
        }
    }
    return CYC_OK;
}

const cyc_message_schema *cyc_schema_message(const cyc_schema *schema, uint32_t id) {
    size_t low = 0;
    size_t high;

    if (schema == NULL) {
        return NULL;
    }
    high = schema->message_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (schema->messages[middle].id < id) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    if (low < schema->message_count && schema->messages[low].id == id) {
        return &schema->messages[low];
    }
    return NULL;
}

uint16_t cyc_message_field_count(const cyc_message_schema *message) {
    if (message == NULL) {
        return 0;
    }
    return (uint16_t)message->prefix_count;
}

bool cyc_message_prefix(const cyc_message_schema *message, uint16_t field_count, uint64_t *out) {
    if (message == NULL || field_count == 0 || (size_t)field_count > message->prefix_count) {
        return false;
    }
    *out = message->prefixes[(size_t)field_count - 1];
    return true;
}
