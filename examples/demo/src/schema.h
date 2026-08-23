#ifndef FOMOXA_DEMO_SCHEMA_H
#define FOMOXA_DEMO_SCHEMA_H

#include <stddef.h>

#include "fomoxa/schema.h"

#include "handshake.h"

static fmx_message_schema DEMO_MESSAGES[sizeof(FOMOXA_MESSAGES) / sizeof(FOMOXA_MESSAGES[0])];
static fmx_schema DEMO_SCHEMA;

static const fmx_schema *demo_schema(void) {
    size_t index;

    for (index = 0; index < FOMOXA_MESSAGES_COUNT; ++index) {
        DEMO_MESSAGES[index].id = FOMOXA_MESSAGES[index].id;
        DEMO_MESSAGES[index].fingerprint = FOMOXA_MESSAGES[index].fingerprint;
        DEMO_MESSAGES[index].prefixes = FOMOXA_MESSAGES[index].prefixes;
        DEMO_MESSAGES[index].prefix_count = FOMOXA_MESSAGES[index].prefix_count;
    }
    DEMO_SCHEMA.fingerprint = FOMOXA_SCHEMA_FINGERPRINT;
    DEMO_SCHEMA.messages = DEMO_MESSAGES;
    DEMO_SCHEMA.message_count = FOMOXA_MESSAGES_COUNT;
    return &DEMO_SCHEMA;
}

#endif /* FOMOXA_DEMO_SCHEMA_H */
