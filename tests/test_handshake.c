#include "cyc_test.h"

#include "cyclone/handshake.h"
#include "cyclone/session.h"

#define BUF 512

typedef struct end {
    bool ready;
    bool failed;
    int reason;
} end;

static cyc_message_schema msg(uint32_t id, const uint64_t *prefixes, size_t count) {
    cyc_message_schema message;
    message.id = id;
    message.fingerprint = count > 0 ? prefixes[count - 1] : (0xE000000000000000ull | (uint64_t)id);
    message.prefixes = prefixes;
    message.prefix_count = count;
    return message;
}

static void note(end *slot, const cyc_reaction *reaction) {
    if (!reaction->has_event) {
        return;
    }
    if (reaction->event == CYC_EVENT_READY) {
        slot->ready = true;
    } else if (reaction->event == CYC_EVENT_HANDSHAKE_FAILED) {
        slot->failed = true;
        slot->reason = reaction->reason;
    }
}

static int run(const cyc_schema *client_schema, const cyc_schema *server_schema, end *client_end,
               end *server_end) {
    cyc_config config;
    cyc_reaction opening;
    cyc_reaction quiet;
    cyc_session *client;
    cyc_session *server;
    uint8_t up[BUF];
    uint8_t down[BUF];
    size_t up_len;
    size_t down_len;
    cyc_frame frame;
    int rounds = 0;

    memset(client_end, 0, sizeof(*client_end));
    memset(server_end, 0, sizeof(*server_end));
    cyc_config_defaults(&config);

    client = cyc_session_create(CYC_ROLE_CLIENT, client_schema, &config, 1000, &opening);
    server = cyc_session_create(CYC_ROLE_SERVER, server_schema, &config, 1000, &quiet);
    if (client == NULL || server == NULL) {
        return -1;
    }
    memcpy(up, opening.out_payload, opening.out_payload_len);
    up_len = opening.out_payload_len;

    for (;;) {
        cyc_reaction reaction;
        rounds += 1;
        if (rounds > 2) {
            break;
        }

        frame.type = CYC_FRAME_HANDSHAKE;
        frame.message_id = 0;
        frame.payload = up;
        frame.payload_len = up_len;
        cyc_session_on_frame(server, &frame, 1000, &reaction);
        note(server_end, &reaction);
        if (reaction.out != CYC_OUT_HANDSHAKE) {
            break;
        }
        memcpy(down, reaction.out_payload, reaction.out_payload_len);
        down_len = reaction.out_payload_len;

        frame.payload = down;
        frame.payload_len = down_len;
        cyc_session_on_frame(client, &frame, 1000, &reaction);
        note(client_end, &reaction);
        if (reaction.has_event) {
            break;
        }
        if (reaction.out != CYC_OUT_HANDSHAKE) {
            break;
        }
        memcpy(up, reaction.out_payload, reaction.out_payload_len);
        up_len = reaction.out_payload_len;
    }

    cyc_session_destroy(client);
    cyc_session_destroy(server);
    return rounds;
}

static const uint64_t P3[3] = {10, 20, 30};
static const uint64_t P2[2] = {10, 20};
static const uint64_t P4[4] = {10, 20, 30, 40};
static const uint64_t P3_BAD_TAIL[3] = {10, 20, 99};
static const uint64_t P2_BAD_TAIL[2] = {10, 77};
static const uint64_t P4_BAD_MID[4] = {10, 20, 99, 40};

static void identical_fingerprints_accept_without_reading_an_entry(void) {
    const uint64_t left[2] = {0xAA, 0xBB};
    const uint64_t right[2] = {0x11, 0x22};
    cyc_message_schema cm = msg(1, left, 2);
    cyc_message_schema sm = msg(1, right, 2);
    cyc_schema cs = {0x5EED, &cm, 1};
    cyc_schema ss = {0x5EED, &sm, 1};
    end client_end;
    end server_end;

    CYC_CHECK(run(&cs, &ss, &client_end, &server_end) == 1);
    CYC_CHECK(client_end.ready);
    CYC_CHECK(server_end.ready);
}

static void branch_a_equal_message_fingerprints_are_accepted(void) {
    const uint64_t extra[1] = {40};
    cyc_message_schema cm[2] = {{0, 0, NULL, 0}, {0, 0, NULL, 0}};
    cyc_message_schema sm = msg(1, P3, 3);
    cyc_schema cs;
    cyc_schema ss = {0x2222, &sm, 1};
    end client_end;
    end server_end;

    cm[0] = msg(1, P3, 3);
    cm[1] = msg(2, extra, 1);
    cs.fingerprint = 0x1111;
    cs.messages = cm;
    cs.message_count = 2;

    CYC_CHECK(run(&cs, &ss, &client_end, &server_end) == 1);
    CYC_CHECK(client_end.ready);
    CYC_CHECK(server_end.ready);
}

static void branch_b_same_count_different_content_is_rejected(void) {
    cyc_message_schema cm = msg(1, P3_BAD_TAIL, 3);
    cyc_message_schema sm = msg(1, P3, 3);
    cyc_schema cs = {0x1111, &cm, 1};
    cyc_schema ss = {0x2222, &sm, 1};
    end client_end;
    end server_end;

    CYC_CHECK(run(&cs, &ss, &client_end, &server_end) == 1);
    CYC_CHECK(client_end.failed && client_end.reason == CYC_FAIL_SCHEMA_CONFLICT);
    CYC_CHECK(server_end.failed && server_end.reason == CYC_FAIL_SCHEMA_CONFLICT);
}

static void branch_c_client_with_fewer_fields_needs_no_query(void) {
    cyc_message_schema cm = msg(1, P2, 2);
    cyc_message_schema sm = msg(1, P3, 3);
    cyc_schema cs = {0x1111, &cm, 1};
    cyc_schema ss = {0x2222, &sm, 1};
    end client_end;
    end server_end;

    CYC_CHECK(run(&cs, &ss, &client_end, &server_end) == 1);
    CYC_CHECK(client_end.ready);
    CYC_CHECK(server_end.ready);
}

static void branch_c_with_a_diverging_prefix_is_rejected(void) {
    cyc_message_schema cm = msg(1, P2_BAD_TAIL, 2);
    cyc_message_schema sm = msg(1, P3, 3);
    cyc_schema cs = {0x1111, &cm, 1};
    cyc_schema ss = {0x2222, &sm, 1};
    end client_end;
    end server_end;

    CYC_CHECK(run(&cs, &ss, &client_end, &server_end) == 1);
    CYC_CHECK(client_end.failed && client_end.reason == CYC_FAIL_SCHEMA_CONFLICT);
}

static void branch_d_client_appending_a_field_is_accepted_after_a_query(void) {
    cyc_message_schema cm = msg(1, P4, 4);
    cyc_message_schema sm = msg(1, P3, 3);
    cyc_schema cs = {0x1111, &cm, 1};
    cyc_schema ss = {0x2222, &sm, 1};
    end client_end;
    end server_end;

    CYC_CHECK(run(&cs, &ss, &client_end, &server_end) == 2);
    CYC_CHECK(client_end.ready);
    CYC_CHECK(server_end.ready);
}

static void branch_d_server_dropping_a_trailing_field_is_accepted_after_a_query(void) {
    cyc_message_schema cm = msg(1, P3, 3);
    cyc_message_schema sm = msg(1, P2, 2);
    cyc_schema cs = {0x1111, &cm, 1};
    cyc_schema ss = {0x2222, &sm, 1};
    end client_end;
    end server_end;

    CYC_CHECK(run(&cs, &ss, &client_end, &server_end) == 2);
    CYC_CHECK(client_end.ready);
    CYC_CHECK(server_end.ready);
}

static void branch_d_with_a_diverging_prefix_is_rejected_after_the_query(void) {
    cyc_message_schema cm = msg(1, P4_BAD_MID, 4);
    cyc_message_schema sm = msg(1, P3, 3);
    cyc_schema cs = {0x1111, &cm, 1};
    cyc_schema ss = {0x2222, &sm, 1};
    end client_end;
    end server_end;

    CYC_CHECK(run(&cs, &ss, &client_end, &server_end) == 2);
    CYC_CHECK(client_end.failed && client_end.reason == CYC_FAIL_SCHEMA_CONFLICT);
    CYC_CHECK(server_end.failed && server_end.reason == CYC_FAIL_SCHEMA_CONFLICT);
}

static void branch_d_with_an_empty_local_message_needs_no_query(void) {
    cyc_message_schema cm = msg(1, P2, 2);
    cyc_message_schema sm = msg(1, NULL, 0);
    cyc_schema cs = {0x1111, &cm, 1};
    cyc_schema ss = {0x2222, &sm, 1};
    end client_end;
    end server_end;

    CYC_CHECK(run(&cs, &ss, &client_end, &server_end) == 1);
    CYC_CHECK(client_end.ready);
}

static void messages_only_one_side_knows_do_not_block_the_session(void) {
    const uint64_t only_client[1] = {90};
    const uint64_t only_server[1] = {70};
    cyc_message_schema cm[2];
    cyc_message_schema sm[2];
    cyc_schema cs;
    cyc_schema ss;
    end client_end;
    end server_end;

    cm[0] = msg(1, P2, 2);
    cm[1] = msg(9, only_client, 1);
    sm[0] = msg(1, P2, 2);
    sm[1] = msg(7, only_server, 1);
    cs.fingerprint = 0x1111;
    cs.messages = cm;
    cs.message_count = 2;
    ss.fingerprint = 0x2222;
    ss.messages = sm;
    ss.message_count = 2;

    CYC_CHECK(run(&cs, &ss, &client_end, &server_end) == 1);
    CYC_CHECK(client_end.ready);
}

static void one_conflicting_message_rejects_the_whole_session(void) {
    const uint64_t shared[1] = {10};
    const uint64_t good[2] = {20, 21};
    const uint64_t bad[2] = {20, 99};
    cyc_message_schema cm[2];
    cyc_message_schema sm[2];
    cyc_schema cs;
    cyc_schema ss;
    end client_end;
    end server_end;

    cm[0] = msg(1, shared, 1);
    cm[1] = msg(2, good, 2);
    sm[0] = msg(1, shared, 1);
    sm[1] = msg(2, bad, 2);
    cs.fingerprint = 0x1111;
    cs.messages = cm;
    cs.message_count = 2;
    ss.fingerprint = 0x2222;
    ss.messages = sm;
    ss.message_count = 2;

    run(&cs, &ss, &client_end, &server_end);
    CYC_CHECK(client_end.failed && client_end.reason == CYC_FAIL_SCHEMA_CONFLICT);
}

static void write_u16(uint8_t *at, uint16_t value) {
    at[0] = (uint8_t)(value & 0xFFu);
    at[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void write_u32(uint8_t *at, uint32_t value) {
    at[0] = (uint8_t)(value & 0xFFu);
    at[1] = (uint8_t)((value >> 8) & 0xFFu);
    at[2] = (uint8_t)((value >> 16) & 0xFFu);
    at[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void write_u64(uint8_t *at, uint64_t value) {
    int index;
    for (index = 0; index < 8; ++index) {
        at[index] = (uint8_t)((value >> (index * 8)) & 0xFFu);
    }
}

static size_t build_hello(uint8_t *out, uint32_t version, uint64_t fingerprint, uint32_t count,
                          uint32_t id, uint16_t fields, uint64_t message_fingerprint) {
    write_u32(out, version);
    write_u64(out + 4, fingerprint);
    write_u32(out + 12, count);
    if (count == 1) {
        write_u32(out + 16, id);
        write_u16(out + 20, fields);
        write_u64(out + 22, message_fingerprint);
        return 30;
    }
    return 16;
}

static uint8_t server_verdict(const uint8_t *hello, size_t len) {
    cyc_config config;
    cyc_reaction quiet;
    cyc_reaction reaction;
    cyc_message_schema sm = msg(1, P3, 3);
    cyc_schema ss = {0x2222, &sm, 1};
    cyc_frame frame;
    uint8_t verdict;
    cyc_session *server;

    cyc_config_defaults(&config);
    server = cyc_session_create(CYC_ROLE_SERVER, &ss, &config, 1000, &quiet);
    frame.type = CYC_FRAME_HANDSHAKE;
    frame.message_id = 0;
    frame.payload = hello;
    frame.payload_len = len;
    cyc_session_on_frame(server, &frame, 1000, &reaction);
    verdict = reaction.out == CYC_OUT_HANDSHAKE && reaction.out_payload_len == 1
                  ? reaction.out_payload[0]
                  : 0xFF;
    cyc_session_destroy(server);
    return verdict;
}

static void a_hello_from_another_version_is_rejected_with_one(void) {
    uint8_t hello[64];
    size_t len = build_hello(hello, 1, 0x1111, 1, 1, 3, 30);
    CYC_CHECK(server_verdict(hello, len) == 1);
}

static void a_malformed_hello_is_rejected_with_three(void) {
    uint8_t hello[64];
    size_t len = build_hello(hello, CYC_PROTOCOL_VERSION, 0x1111, 1, 1, 3, 30);

    CYC_CHECK(server_verdict(hello, len + 1) == 3);
    CYC_CHECK(server_verdict(hello, len - 1) == 3);
    CYC_CHECK(server_verdict(hello, 15) == 3);
    CYC_CHECK(server_verdict(hello, 0) == 3);

    write_u32(hello + 12, 0xFFFFFFFFu);
    CYC_CHECK(server_verdict(hello, 16) == 3);
    write_u32(hello + 12, (uint32_t)(CYC_MAX_SCHEMA_MESSAGES + 1));
    CYC_CHECK(server_verdict(hello, 16) == 3);
}

static cyc_session *client_after_hello(cyc_schema *schema, cyc_message_schema *message) {
    cyc_config config;
    cyc_reaction opening;
    *message = msg(1, P3, 3);
    schema->fingerprint = 0x1111;
    schema->messages = message;
    schema->message_count = 1;
    cyc_config_defaults(&config);
    return cyc_session_create(CYC_ROLE_CLIENT, schema, &config, 1000, &opening);
}

static void feed_client(cyc_session *client, const uint8_t *payload, size_t len,
                        cyc_reaction *reaction) {
    cyc_frame frame;
    frame.type = CYC_FRAME_HANDSHAKE;
    frame.message_id = 0;
    frame.payload = payload;
    frame.payload_len = len;
    cyc_session_on_frame(client, &frame, 1000, reaction);
}

static void a_second_query_is_treated_as_broken(void) {
    cyc_schema schema;
    cyc_message_schema message;
    cyc_session *client = client_after_hello(&schema, &message);
    uint8_t query[16];
    cyc_reaction reaction;

    query[0] = CYC_QUERY_TAG;
    write_u32(query + 1, 1);
    write_u32(query + 5, 1);
    write_u16(query + 9, 2);

    feed_client(client, query, 11, &reaction);
    CYC_CHECK(!reaction.has_event);
    CYC_CHECK(reaction.out == CYC_OUT_HANDSHAKE);

    feed_client(client, query, 11, &reaction);
    CYC_CHECK(reaction.has_event && reaction.event == CYC_EVENT_HANDSHAKE_FAILED);
    CYC_CHECK(reaction.reason == CYC_FAIL_MALFORMED_PEER);
    cyc_session_destroy(client);
}

static void a_bad_query_is_treated_as_broken(void) {
    uint16_t indexes[3] = {0, 3, 4};
    size_t which;
    cyc_schema schema;
    cyc_message_schema message;
    cyc_session *client;
    uint8_t query[16];
    cyc_reaction reaction;

    client = client_after_hello(&schema, &message);
    query[0] = CYC_QUERY_TAG;
    write_u32(query + 1, 1);
    write_u32(query + 5, 99);
    write_u16(query + 9, 1);
    feed_client(client, query, 11, &reaction);
    CYC_CHECK(reaction.has_event && reaction.reason == CYC_FAIL_MALFORMED_PEER);
    cyc_session_destroy(client);

    for (which = 0; which < 3; ++which) {
        client = client_after_hello(&schema, &message);
        query[0] = CYC_QUERY_TAG;
        write_u32(query + 1, 1);
        write_u32(query + 5, 1);
        write_u16(query + 9, indexes[which]);
        feed_client(client, query, 11, &reaction);
        CYC_CHECK(reaction.has_event && reaction.reason == CYC_FAIL_MALFORMED_PEER);
        cyc_session_destroy(client);
    }
}

static void a_bad_verdict_is_treated_as_broken(void) {
    uint8_t bytes[2] = {5, 0};
    cyc_schema schema;
    cyc_message_schema message;
    cyc_session *client;
    cyc_reaction reaction;

    client = client_after_hello(&schema, &message);
    feed_client(client, bytes, 1, &reaction);
    CYC_CHECK(reaction.has_event && reaction.reason == CYC_FAIL_MALFORMED_PEER);
    cyc_session_destroy(client);

    client = client_after_hello(&schema, &message);
    bytes[0] = 0;
    feed_client(client, bytes, 2, &reaction);
    CYC_CHECK(reaction.has_event && reaction.reason == CYC_FAIL_MALFORMED_PEER);
    cyc_session_destroy(client);
}

static uint8_t server_after_query(const uint8_t *reply, size_t len) {
    cyc_config config;
    cyc_reaction quiet;
    cyc_reaction reaction;
    cyc_message_schema sm[2];
    cyc_schema ss;
    cyc_frame frame;
    uint8_t hello[64];
    uint8_t verdict;
    const uint64_t two[2] = {10, 20};
    const uint64_t one[1] = {30};
    cyc_session *server;

    sm[0] = msg(1, two, 2);
    sm[1] = msg(2, one, 1);
    ss.fingerprint = 0x2222;
    ss.messages = sm;
    ss.message_count = 2;

    cyc_config_defaults(&config);
    server = cyc_session_create(CYC_ROLE_SERVER, &ss, &config, 1000, &quiet);

    write_u32(hello, CYC_PROTOCOL_VERSION);
    write_u64(hello + 4, 0x1111);
    write_u32(hello + 12, 2);
    write_u32(hello + 16, 1);
    write_u16(hello + 20, 3);
    write_u64(hello + 22, 99);
    write_u32(hello + 30, 2);
    write_u16(hello + 34, 2);
    write_u64(hello + 36, 88);

    frame.type = CYC_FRAME_HANDSHAKE;
    frame.message_id = 0;
    frame.payload = hello;
    frame.payload_len = 44;
    cyc_session_on_frame(server, &frame, 1000, &reaction);

    frame.payload = reply;
    frame.payload_len = len;
    cyc_session_on_frame(server, &frame, 1000, &reaction);
    verdict = reaction.out == CYC_OUT_HANDSHAKE && reaction.out_payload_len == 1
                  ? reaction.out_payload[0]
                  : 0xFF;
    cyc_session_destroy(server);
    return verdict;
}

static size_t build_reply(uint8_t *out, size_t count, const uint32_t *ids,
                          const uint64_t *fingerprints) {
    size_t index;
    write_u32(out, (uint32_t)count);
    for (index = 0; index < count; ++index) {
        write_u32(out + 4 + 12 * index, ids[index]);
        write_u64(out + 8 + 12 * index, fingerprints[index]);
    }
    return 4 + 12 * count;
}

static void the_query_reply_decides_the_session(void) {
    uint8_t reply[64];
    uint32_t ids[3] = {1, 2, 3};
    uint64_t good[2] = {20, 30};
    uint64_t bad[2] = {20, 77};
    uint32_t swapped[2] = {2, 1};
    uint64_t swapped_values[2] = {30, 20};
    size_t len;

    len = build_reply(reply, 2, ids, good);
    CYC_CHECK(server_after_query(reply, len) == 0);

    len = build_reply(reply, 2, ids, bad);
    CYC_CHECK(server_after_query(reply, len) == 2);

    len = build_reply(reply, 1, ids, good);
    CYC_CHECK(server_after_query(reply, len) == 3);

    len = build_reply(reply, 2, swapped, swapped_values);
    CYC_CHECK(server_after_query(reply, len) == 3);

    reply[0] = 0x00;
    CYC_CHECK(server_after_query(reply, 1) == 3);
}

static void the_client_deadline_covers_both_rounds(void) {
    cyc_schema schema;
    cyc_message_schema message;
    cyc_session *client = client_after_hello(&schema, &message);
    uint8_t query[16];
    cyc_reaction reaction;

    query[0] = CYC_QUERY_TAG;
    write_u32(query + 1, 1);
    write_u32(query + 5, 1);
    write_u16(query + 9, 2);

    {
        cyc_frame frame;
        frame.type = CYC_FRAME_HANDSHAKE;
        frame.message_id = 0;
        frame.payload = query;
        frame.payload_len = 11;
        cyc_session_on_frame(client, &frame, 4000, &reaction);
    }
    CYC_CHECK(reaction.out == CYC_OUT_HANDSHAKE);
    CYC_CHECK(!reaction.has_event);

    cyc_session_tick(client, 6000, &reaction);
    CYC_CHECK(reaction.has_event && reaction.reason == CYC_FAIL_TIMEOUT);
    CYC_CHECK(cyc_session_state(client) == CYC_STATE_CLOSED);
    cyc_session_destroy(client);
}

static void a_client_that_never_hears_back_times_out(void) {
    cyc_schema schema;
    cyc_message_schema message;
    cyc_session *client = client_after_hello(&schema, &message);
    cyc_reaction reaction;

    cyc_session_tick(client, 5000, &reaction);
    CYC_CHECK(!reaction.has_event);
    cyc_session_tick(client, 6000, &reaction);
    CYC_CHECK(reaction.has_event && reaction.reason == CYC_FAIL_TIMEOUT);
    cyc_session_destroy(client);
}

static void a_client_answering_probes_holds_its_slot(void) {
    cyc_config config;
    cyc_reaction quiet;
    cyc_reaction reaction;
    cyc_message_schema sm = msg(1, P3, 3);
    cyc_schema ss = {0x2222, &sm, 1};
    cyc_session *server;
    cyc_frame frame;
    uint64_t now = 1000;
    int round;

    cyc_config_defaults(&config);
    server = cyc_session_create(CYC_ROLE_SERVER, &ss, &config, now, &quiet);

    frame.type = CYC_FRAME_ACK;
    frame.message_id = 0;
    frame.payload = NULL;
    frame.payload_len = 0;

    for (round = 0; round < 20; ++round) {
        now += 5000;
        cyc_session_tick(server, now, &reaction);
        CYC_CHECK(reaction.out == CYC_OUT_PROBE);
        CYC_CHECK(!reaction.has_event);

        now += 1000;
        cyc_session_on_frame(server, &frame, now, &reaction);
        CYC_CHECK(!reaction.has_event);
    }
    CYC_CHECK(cyc_session_state(server) == CYC_STATE_HANDSHAKING);
    cyc_session_destroy(server);
}

int main(void) {
    CYC_RUN(identical_fingerprints_accept_without_reading_an_entry);
    CYC_RUN(branch_a_equal_message_fingerprints_are_accepted);
    CYC_RUN(branch_b_same_count_different_content_is_rejected);
    CYC_RUN(branch_c_client_with_fewer_fields_needs_no_query);
    CYC_RUN(branch_c_with_a_diverging_prefix_is_rejected);
    CYC_RUN(branch_d_client_appending_a_field_is_accepted_after_a_query);
    CYC_RUN(branch_d_server_dropping_a_trailing_field_is_accepted_after_a_query);
    CYC_RUN(branch_d_with_a_diverging_prefix_is_rejected_after_the_query);
    CYC_RUN(branch_d_with_an_empty_local_message_needs_no_query);
    CYC_RUN(messages_only_one_side_knows_do_not_block_the_session);
    CYC_RUN(one_conflicting_message_rejects_the_whole_session);
    CYC_RUN(a_hello_from_another_version_is_rejected_with_one);
    CYC_RUN(a_malformed_hello_is_rejected_with_three);
    CYC_RUN(a_second_query_is_treated_as_broken);
    CYC_RUN(a_bad_query_is_treated_as_broken);
    CYC_RUN(a_bad_verdict_is_treated_as_broken);
    CYC_RUN(the_query_reply_decides_the_session);
    CYC_RUN(the_client_deadline_covers_both_rounds);
    CYC_RUN(a_client_that_never_hears_back_times_out);
    CYC_RUN(a_client_answering_probes_holds_its_slot);
    CYC_DONE();
}
