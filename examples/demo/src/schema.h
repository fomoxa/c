#ifndef CYCLONE_DEMO_SCHEMA_H
#define CYCLONE_DEMO_SCHEMA_H

#include <stddef.h>

#include "cyclone/schema.h"

#include "handshake.h"

static cyc_message_schema DEMO_MESSAGES[sizeof(CYCLONE_MESSAGES) / sizeof(CYCLONE_MESSAGES[0])];
static cyc_schema DEMO_SCHEMA;

static const cyc_schema *demo_schema(void) {
    size_t index;

    for (index = 0; index < CYCLONE_MESSAGES_COUNT; ++index) {
        DEMO_MESSAGES[index].id = CYCLONE_MESSAGES[index].id;
        DEMO_MESSAGES[index].fingerprint = CYCLONE_MESSAGES[index].fingerprint;
        DEMO_MESSAGES[index].prefixes = CYCLONE_MESSAGES[index].prefixes;
        DEMO_MESSAGES[index].prefix_count = CYCLONE_MESSAGES[index].prefix_count;
    }
    DEMO_SCHEMA.fingerprint = CYCLONE_SCHEMA_FINGERPRINT;
    DEMO_SCHEMA.messages = DEMO_MESSAGES;
    DEMO_SCHEMA.message_count = CYCLONE_MESSAGES_COUNT;
    return &DEMO_SCHEMA;
}

#endif /* CYCLONE_DEMO_SCHEMA_H */
