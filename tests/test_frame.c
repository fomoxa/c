#include "cyc_test.h"

#include "cyclone/frame.h"

static const size_t NO_LIMIT = 0;

static void data_frame_layout(void) {
    uint8_t out[64];
    size_t written = 0;
    const uint8_t payload[3] = {1, 2, 3};
    const uint8_t expect[14] = {0x00, 0x43, 0x59, 0x2A, 0, 0, 0, 0x03, 0, 0, 0, 1, 2, 3};
    cyc_frame frame;

    CYC_CHECK(cyc_frame_encode_data(42, payload, 3, out, sizeof(out), &written) == CYC_FRAME_OK);
    CYC_CHECK(written == 14);
    CYC_CHECK(memcmp(out, expect, sizeof(expect)) == 0);

    CYC_CHECK(cyc_frame_decode_packet(out, written, NO_LIMIT, &frame) == CYC_FRAME_OK);
    CYC_CHECK(frame.type == CYC_FRAME_DATA);
    CYC_CHECK(frame.message_id == 42);
    CYC_CHECK(frame.payload_len == 3);
    CYC_CHECK(memcmp(frame.payload, payload, 3) == 0);
}

static void probe_and_ack_are_one_byte(void) {
    uint8_t out[4];
    cyc_frame frame;

    CYC_CHECK(cyc_frame_encode_probe(out, sizeof(out)) == 1);
    CYC_CHECK(out[0] == 0x01);
    CYC_CHECK(cyc_frame_decode_packet(out, 1, NO_LIMIT, &frame) == CYC_FRAME_OK);
    CYC_CHECK(frame.type == CYC_FRAME_PROBE);

    CYC_CHECK(cyc_frame_encode_ack(out, sizeof(out)) == 1);
    CYC_CHECK(out[0] == 0x02);
    CYC_CHECK(cyc_frame_decode_packet(out, 1, NO_LIMIT, &frame) == CYC_FRAME_OK);
    CYC_CHECK(frame.type == CYC_FRAME_ACK);
}

static void handshake_frame_layout(void) {
    uint8_t out[16];
    size_t written = 0;
    const uint8_t payload[2] = {0xAA, 0xBB};
    const uint8_t expect[7] = {0x03, 0x02, 0, 0, 0, 0xAA, 0xBB};
    cyc_frame frame;

    CYC_CHECK(cyc_frame_encode_handshake(payload, 2, out, sizeof(out), &written) == CYC_FRAME_OK);
    CYC_CHECK(written == 7);
    CYC_CHECK(memcmp(out, expect, sizeof(expect)) == 0);
    CYC_CHECK(cyc_frame_decode_packet(out, written, NO_LIMIT, &frame) == CYC_FRAME_OK);
    CYC_CHECK(frame.type == CYC_FRAME_HANDSHAKE);
    CYC_CHECK(frame.payload_len == 2);
}

static size_t build_wire(uint8_t *out, size_t cap) {
    size_t total = 0;
    size_t written = 0;
    const uint8_t hello[5] = {'h', 'e', 'l', 'l', 'o'};
    const uint8_t body[3] = {1, 2, 3};

    cyc_frame_encode_data(7, hello, 5, out + total, cap - total, &written);
    total += written;
    total += cyc_frame_encode_probe(out + total, cap - total);
    cyc_frame_encode_handshake(body, 3, out + total, cap - total, &written);
    total += written;
    total += cyc_frame_encode_ack(out + total, cap - total);
    return total;
}

static void a_stream_fed_one_byte_at_a_time_still_yields_frames(void) {
    uint8_t wire[64];
    size_t total = build_wire(wire, sizeof(wire));
    cyc_stream_decoder decoder;
    size_t index;
    uint8_t seen[8];
    size_t seen_count = 0;

    CYC_CHECK(cyc_stream_decoder_init(&decoder, 0) == CYC_OK);
    for (index = 0; index < total; ++index) {
        CYC_CHECK(cyc_stream_decoder_feed(&decoder, wire + index, 1) == CYC_OK);
        for (;;) {
            cyc_frame frame;
            size_t frame_len = 0;
            if (cyc_stream_decoder_next(&decoder, &frame, &frame_len) != CYC_FRAME_OK) {
                break;
            }
            if (seen_count < sizeof(seen)) {
                seen[seen_count] = frame.type;
                seen_count += 1;
            }
            cyc_stream_decoder_advance(&decoder, frame_len);
        }
    }

    CYC_CHECK(seen_count == 4);
    CYC_CHECK(seen[0] == CYC_FRAME_DATA);
    CYC_CHECK(seen[1] == CYC_FRAME_PROBE);
    CYC_CHECK(seen[2] == CYC_FRAME_HANDSHAKE);
    CYC_CHECK(seen[3] == CYC_FRAME_ACK);
    CYC_CHECK(cyc_stream_decoder_buffered(&decoder) == 0);
    cyc_stream_decoder_release(&decoder);
}

static void several_frames_in_one_feed_are_split(void) {
    uint8_t wire[64];
    size_t total = build_wire(wire, sizeof(wire));
    cyc_stream_decoder decoder;
    size_t count = 0;

    CYC_CHECK(cyc_stream_decoder_init(&decoder, 0) == CYC_OK);
    CYC_CHECK(cyc_stream_decoder_feed(&decoder, wire, total) == CYC_OK);
    for (;;) {
        cyc_frame frame;
        size_t frame_len = 0;
        if (cyc_stream_decoder_next(&decoder, &frame, &frame_len) != CYC_FRAME_OK) {
            break;
        }
        cyc_stream_decoder_advance(&decoder, frame_len);
        count += 1;
    }
    CYC_CHECK(count == 4);
    CYC_CHECK(cyc_stream_decoder_buffered(&decoder) == 0);
    cyc_stream_decoder_release(&decoder);
}

static void a_broken_stream_stays_broken(void) {
    cyc_stream_decoder decoder;
    const uint8_t bad[1] = {0x09};
    uint8_t good[32];
    size_t written = 0;
    cyc_frame frame;
    size_t frame_len = 0;

    CYC_CHECK(cyc_stream_decoder_init(&decoder, 0) == CYC_OK);
    CYC_CHECK(cyc_stream_decoder_feed(&decoder, bad, 1) == CYC_OK);
    CYC_CHECK(cyc_stream_decoder_next(&decoder, &frame, &frame_len) == CYC_FRAME_UNKNOWN_TYPE);

    cyc_frame_encode_data(1, bad, 1, good, sizeof(good), &written);
    CYC_CHECK(cyc_stream_decoder_feed(&decoder, good, written) == CYC_OK);
    CYC_CHECK(cyc_stream_decoder_next(&decoder, &frame, &frame_len) == CYC_FRAME_UNKNOWN_TYPE);
    CYC_CHECK(cyc_stream_decoder_poisoned(&decoder));
    cyc_stream_decoder_release(&decoder);
}

static void bad_magic_is_caught_before_the_header_is_complete(void) {
    cyc_stream_decoder decoder;
    const uint8_t head[3] = {0x00, 0x43, 0x5A};
    cyc_frame frame;
    size_t frame_len = 0;

    CYC_CHECK(cyc_stream_decoder_init(&decoder, 0) == CYC_OK);
    CYC_CHECK(cyc_stream_decoder_feed(&decoder, head, 3) == CYC_OK);
    CYC_CHECK(cyc_stream_decoder_next(&decoder, &frame, &frame_len) == CYC_FRAME_BAD_MAGIC);
    cyc_stream_decoder_release(&decoder);
}

static void a_packet_holds_exactly_one_frame(void) {
    uint8_t out[32];
    size_t written = 0;
    const uint8_t payload[3] = {'a', 'b', 'c'};
    cyc_frame frame;

    cyc_frame_encode_data(1, payload, 3, out, sizeof(out), &written);
    CYC_CHECK(cyc_frame_decode_packet(out, written - 1, NO_LIMIT, &frame) == CYC_FRAME_TRUNCATED);

    out[written] = 0x01;
    CYC_CHECK(cyc_frame_decode_packet(out, written + 1, NO_LIMIT, &frame) == CYC_FRAME_TRAILING);
    CYC_CHECK(cyc_frame_decode_packet(out, 0, NO_LIMIT, &frame) == CYC_FRAME_TRUNCATED);
}

static void an_unknown_type_byte_is_rejected_in_a_packet(void) {
    const uint8_t bad[1] = {0x04};
    cyc_frame frame;
    CYC_CHECK(cyc_frame_decode_packet(bad, 1, NO_LIMIT, &frame) == CYC_FRAME_UNKNOWN_TYPE);
}

static void the_declared_length_is_checked_against_the_cap(void) {
    uint8_t header[11];
    cyc_frame frame;
    size_t used = 0;

    header[0] = CYC_FRAME_DATA;
    header[1] = CYC_MAGIC_C;
    header[2] = CYC_MAGIC_Y;
    memset(header + 3, 0, 4);
    header[7] = 0x01;
    header[8] = 0x00;
    header[9] = 0x00;
    header[10] = 0x00;

    CYC_CHECK(cyc_frame_decode(header, sizeof(header), 0, &frame, &used) == CYC_FRAME_INCOMPLETE);

    header[7] = 0x11;
    CYC_CHECK(cyc_frame_decode(header, sizeof(header), 16, &frame, &used) ==
              CYC_FRAME_MESSAGE_TOO_LARGE);
}

static void an_empty_payload_is_a_valid_data_frame(void) {
    uint8_t out[16];
    size_t written = 0;
    cyc_frame frame;

    CYC_CHECK(cyc_frame_encode_data(0, NULL, 0, out, sizeof(out), &written) == CYC_FRAME_OK);
    CYC_CHECK(written == 11);
    CYC_CHECK(cyc_frame_decode_packet(out, written, NO_LIMIT, &frame) == CYC_FRAME_OK);
    CYC_CHECK(frame.payload_len == 0);
}

int main(void) {
    CYC_RUN(data_frame_layout);
    CYC_RUN(probe_and_ack_are_one_byte);
    CYC_RUN(handshake_frame_layout);
    CYC_RUN(a_stream_fed_one_byte_at_a_time_still_yields_frames);
    CYC_RUN(several_frames_in_one_feed_are_split);
    CYC_RUN(a_broken_stream_stays_broken);
    CYC_RUN(bad_magic_is_caught_before_the_header_is_complete);
    CYC_RUN(a_packet_holds_exactly_one_frame);
    CYC_RUN(an_unknown_type_byte_is_rejected_in_a_packet);
    CYC_RUN(the_declared_length_is_checked_against_the_cap);
    CYC_RUN(an_empty_payload_is_a_valid_data_frame);
    CYC_DONE();
}
