#include "check.h"

#include "websocket.h"

#include <string.h>

static void to_hex(const uint8_t *data, size_t len, char *out) {
    static const char digits[] = "0123456789abcdef";
    size_t i;

    for (i = 0; i < len; i++) {
        out[i * 2] = digits[data[i] >> 4];
        out[i * 2 + 1] = digits[data[i] & 0x0F];
    }
    out[len * 2] = '\0';
}

/* `len` is `(size_t)-1` for the common case of an ordinary C string --
   which means "measure it", so a hand-counted length can never drift from
   what is actually there, the way the fox sentence's did below. */
static void check_sha1(const char *name, const char *message, size_t len,
                       const char *expected_hex) {
    uint8_t digest[20];
    char hex[41];

    if (len == (size_t)-1)
        len = strlen(message);
    websocket_sha1((const uint8_t *)message, len, digest);
    to_hex(digest, sizeof(digest), hex);
    check_str(name, hex, expected_hex);
}

/*
 * Every expected value below was computed independently on this machine
 * (`sha1sum`, `base64`, and a five-line Python script for the RFC 6455
 * worked example) rather than transcribed from memory -- see the ticket's
 * comments for the exact commands.
 */
static void test_sha1_vectors(void) {
    check_sha1("the empty string", "", (size_t)-1,
              "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    check_sha1("\"abc\"", "abc", (size_t)-1, "a9993e364706816aba3e25717850c26c9cd0d89d");
    check_sha1("the FIPS 56-byte multi-block vector",
              "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
              (size_t)-1,
              "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    check_sha1("the fox", "The quick brown fox jumps over the lazy dog",
              (size_t)-1,
              "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12");

    /* The padding boundary: a message pads with 0x80 plus an 8-byte length,
       9 bytes in all, so 55 bytes is the longest message a single 64-byte
       block still has room for; 56 forces a second block. */
    {
        char fifty_five[55];
        char fifty_six[56];

        memset(fifty_five, 'a', sizeof(fifty_five));
        memset(fifty_six, 'a', sizeof(fifty_six));
        check_sha1("55 bytes: the longest single-block message", fifty_five,
                  sizeof(fifty_five),
                  "c1c8bbdc22796e28c0e15163d20899b65621d65a");
        check_sha1("56 bytes: one byte over, and a second block",
                  fifty_six, sizeof(fifty_six),
                  "c2db330f6083854c99d4b5bfb6e8f29f201be699");
    }
}

/* RFC 4648's own test vectors, chosen because "f".."foobar" walk through
   both padding cases (one input byte over a multiple of three pads with
   "==", two bytes pads with "=") and the no-padding case. */
static void test_base64_vectors(void) {
    static const struct {
        const char *input;
        const char *expected;
    } cases[] = {
        { "", "" },
        { "f", "Zg==" },
        { "fo", "Zm8=" },
        { "foo", "Zm9v" },
        { "foob", "Zm9vYg==" },
        { "fooba", "Zm9vYmE=" },
        { "foobar", "Zm9vYmFy" },
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char out[16];
        size_t len = strlen(cases[i].input);
        size_t written = websocket_base64_encode((const uint8_t *)cases[i].input,
                                                 len, out, sizeof(out));

        check_size(cases[i].input[0] ? cases[i].input : "the empty string",
                  written, strlen(cases[i].expected));
        check_str(cases[i].expected, out, cases[i].expected);
    }

    {
        /* A buffer exactly one byte short of what "foo" needs (4 chars + a
           nul) must refuse rather than truncate. */
        char tiny[4];
        check_size("a buffer too small to hold the encoding plus a nul",
                  websocket_base64_encode((const uint8_t *)"foo", 3, tiny,
                                          sizeof(tiny)),
                  0);
    }
}

/* RFC 6455 section 1.3's own worked example. */
static void test_handshake_accept_value(void) {
    char out[32];
    size_t len = websocket_accept_value("dGhlIHNhbXBsZSBub25jZQ==", out,
                                        sizeof(out));

    check_size("the accept value's length", len, 28);
    check_str("RFC 6455's worked example", out, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

static void test_request_parse_upgrade(void) {
    const char *request =
        "GET /viewer HTTP/1.1\r\n"
        "Host: 127.0.0.1:8765\r\n"
        "Upgrade: websocket\r\n"
        "Connection: keep-alive, Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    struct websocket_request req;
    int consumed = websocket_request_parse(request, strlen(request), &req);
    const struct websocket_header *key;

    check_int("the whole header block is consumed", consumed,
              (int)strlen(request));
    check_size("method length", req.method_len, 3);
    check_true("method is GET", memcmp(req.method, "GET", 3) == 0);
    check_size("path length", req.path_len, 7);
    check_true("path is /viewer", memcmp(req.path, "/viewer", 7) == 0);
    check_int("five headers", req.header_count, 5);
    check_true("this request is an upgrade", websocket_request_is_upgrade(&req));

    key = websocket_request_header(&req, "sec-websocket-key");
    check_true("header lookup is case-insensitive", key != NULL);
    if (key)
        check_true("the key's value is exact",
                  key->value_len == 24 &&
                      memcmp(key->value, "dGhlIHNhbXBsZSBub25jZQ==", 24) == 0);
}

static void test_request_parse_plain_get(void) {
    const char *request = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    struct websocket_request req;
    int consumed = websocket_request_parse(request, strlen(request), &req);

    check_true("a plain GET parses", consumed > 0);
    check_true("a plain GET is not an upgrade",
              !websocket_request_is_upgrade(&req));
}

static void test_request_parse_incomplete(void) {
    const char *partial = "GET / HTTP/1.1\r\nHost: x\r\n";
    struct websocket_request req;

    check_int("no blank line yet: asks for more, not a failure",
              websocket_request_parse(partial, strlen(partial), &req), 0);
}

static void test_request_parse_malformed(void) {
    const char *no_second_space = "GET /\r\n\r\n";
    struct websocket_request req;

    check_int("a request line with no HTTP version is refused",
              websocket_request_parse(no_second_space,
                                      strlen(no_second_space), &req),
              -1);
}

/* A real client always masks. */
static void mask_in_place(uint8_t *payload, size_t len,
                          const uint8_t mask_key[4]) {
    size_t i;

    for (i = 0; i < len; i++)
        payload[i] ^= mask_key[i % 4];
}

static size_t build_client_frame(uint8_t *out, int fin, int opcode,
                                 const uint8_t *payload, size_t payload_len,
                                 const uint8_t mask_key[4]) {
    size_t header_len;

    out[0] = (uint8_t)((fin ? 0x80 : 0) | (opcode & 0x0F));
    if (payload_len <= 125) {
        out[1] = (uint8_t)(0x80 | payload_len);
        header_len = 2;
    } else {
        out[1] = 0x80 | 126;
        out[2] = (uint8_t)(payload_len >> 8);
        out[3] = (uint8_t)payload_len;
        header_len = 4;
    }
    memcpy(out + header_len, mask_key, 4);
    header_len += 4;
    memcpy(out + header_len, payload, payload_len);
    mask_in_place(out + header_len, payload_len, mask_key);
    return header_len + payload_len;
}

static void test_frame_decode_masked_text(void) {
    uint8_t buf[64];
    uint8_t mask_key[4] = { 0x12, 0x34, 0x56, 0x78 };
    const char *text = "hello";
    size_t frame_len = build_client_frame(buf, 1, WEBSOCKET_OP_TEXT,
                                          (const uint8_t *)text, 5, mask_key);
    struct websocket_frame frame;
    long consumed = websocket_frame_decode(buf, frame_len, &frame);

    check_int("the whole frame is consumed", (int)consumed, (int)frame_len);
    check_int("fin is set", frame.fin, 1);
    check_int("opcode is TEXT", frame.opcode, WEBSOCKET_OP_TEXT);
    check_size("payload length", frame.payload_len, 5);
    check_true("the payload is unmasked back to the original",
              memcmp(frame.payload, "hello", 5) == 0);
}

static void test_frame_decode_needs_more_data(void) {
    uint8_t buf[64];
    uint8_t mask_key[4] = { 1, 2, 3, 4 };
    size_t frame_len = build_client_frame(buf, 1, WEBSOCKET_OP_TEXT,
                                          (const uint8_t *)"hi", 2, mask_key);
    struct websocket_frame frame;

    check_int("an empty buffer asks for more",
              (int)websocket_frame_decode(buf, 0, &frame), 0);
    check_int("one byte asks for more",
              (int)websocket_frame_decode(buf, 1, &frame), 0);
    check_int("the header without the payload asks for more",
              (int)websocket_frame_decode(buf, frame_len - 1, &frame), 0);
    check_true("the complete frame decodes",
              websocket_frame_decode(buf, frame_len, &frame) > 0);
}

/*
 * The round-trip trap: a decoder that ignores the mask bit would decode
 * `websocket_frame_encode()`'s own (unmasked) output successfully, since
 * an all-zero "mask key" applied via XOR changes nothing. That is exactly
 * the shape of bug a round trip through one side's own two functions
 * cannot catch, and it is why this frame is unmasked on purpose -- built
 * by the server-side encoder, then handed to the decoder that is only
 * ever supposed to accept client frames.
 */
static void test_frame_decode_refuses_unmasked(void) {
    uint8_t buf[64];
    size_t frame_len = websocket_frame_encode(buf, sizeof(buf), 1,
                                              WEBSOCKET_OP_TEXT,
                                              (const uint8_t *)"hello", 5);
    struct websocket_frame frame;

    check_true("an unmasked frame is refused, not silently accepted",
              websocket_frame_decode(buf, frame_len, &frame) == -1);
}

static void test_frame_decode_refuses_rsv_bits(void) {
    uint8_t buf[64];
    uint8_t mask_key[4] = { 1, 2, 3, 4 };
    size_t frame_len = build_client_frame(buf, 1, WEBSOCKET_OP_TEXT,
                                          (const uint8_t *)"x", 1, mask_key);
    struct websocket_frame frame;

    buf[0] |= 0x40; /* RSV1, meaningful only with an extension */
    check_true("an RSV bit with no extension negotiated is refused",
              websocket_frame_decode(buf, frame_len, &frame) == -1);
}

static void test_frame_decode_extended_length(void) {
    uint8_t buf[400];
    uint8_t mask_key[4] = { 9, 8, 7, 6 };
    uint8_t payload[200];
    size_t frame_len;
    struct websocket_frame frame;
    size_t i;

    for (i = 0; i < sizeof(payload); i++)
        payload[i] = (uint8_t)i;
    frame_len = build_client_frame(buf, 1, WEBSOCKET_OP_BINARY, payload,
                                   sizeof(payload), mask_key);
    check_true("a payload over 125 bytes needs the 16-bit length form",
              frame_len == 4 + 4 + sizeof(payload));
    check_true("it decodes to the same payload",
              websocket_frame_decode(buf, frame_len, &frame) > 0 &&
                  frame.payload_len == sizeof(payload) &&
                  memcmp(frame.payload, payload, sizeof(payload)) == 0);
}

static void test_frame_encode_length_forms(void) {
    uint8_t out[70000];
    uint8_t payload[70000];

    memset(payload, 0, sizeof(payload)); /* content is irrelevant here --
                                            only the header form is asserted */
    check_size("125 bytes: the 2-byte header form",
              websocket_frame_encode(out, sizeof(out), 1, WEBSOCKET_OP_BINARY,
                                     payload, 125),
              2 + 125);
    check_size("126 bytes: the 4-byte header form",
              websocket_frame_encode(out, sizeof(out), 1, WEBSOCKET_OP_BINARY,
                                     payload, 126),
              4 + 126);
    check_size("65535 bytes: still the 4-byte form",
              websocket_frame_encode(out, sizeof(out), 1, WEBSOCKET_OP_BINARY,
                                     payload, 65535),
              4 + 65535);
    check_size("65536 bytes: the 10-byte form",
              websocket_frame_encode(out, sizeof(out), 1, WEBSOCKET_OP_BINARY,
                                     payload, 65536),
              10 + 65536);
    check_size("a server frame is never masked -- MASK bit clear",
              websocket_frame_encode(out, sizeof(out), 1, WEBSOCKET_OP_BINARY,
                                     payload, 10) > 0 &&
                  (out[1] & 0x80) == 0,
              1);
}

static void test_fragmented_message(void) {
    uint8_t buf1[32], buf2[32];
    uint8_t mask_key[4] = { 5, 6, 7, 8 };
    size_t len1 = build_client_frame(buf1, 0, WEBSOCKET_OP_TEXT,
                                     (const uint8_t *)"hel", 3, mask_key);
    size_t len2 = build_client_frame(buf2, 1, WEBSOCKET_OP_CONTINUATION,
                                     (const uint8_t *)"lo", 2, mask_key);
    struct websocket_frame frame1, frame2;
    struct websocket_message_assembler assembler;
    int opcode;
    const uint8_t *data;
    size_t out_len;

    memset(&assembler, 0, sizeof(assembler));
    check_true("first fragment decodes",
              websocket_frame_decode(buf1, len1, &frame1) > 0);
    check_int("first fragment is not final", frame1.fin, 0);
    check_int("first fragment feeds without completing",
              websocket_message_feed(&assembler, &frame1, &opcode, &data,
                                     &out_len),
              0);

    check_true("second fragment decodes",
              websocket_frame_decode(buf2, len2, &frame2) > 0);
    check_int("the message completes on the second fragment",
              websocket_message_feed(&assembler, &frame2, &opcode, &data,
                                     &out_len),
              1);
    check_int("the reassembled opcode is the first fragment's", opcode,
              WEBSOCKET_OP_TEXT);
    check_size("the reassembled length is both fragments together", out_len,
              5);
    check_true("the reassembled bytes are exactly \"hello\"",
              memcmp(data, "hello", 5) == 0);
}

static void test_message_feed_protocol_violations(void) {
    struct websocket_message_assembler assembler;
    struct websocket_frame continuation = { 1, WEBSOCKET_OP_CONTINUATION,
                                            (const uint8_t *)"x", 1 };
    struct websocket_frame text_a = { 0, WEBSOCKET_OP_TEXT,
                                      (const uint8_t *)"a", 1 };
    struct websocket_frame text_b = { 0, WEBSOCKET_OP_TEXT,
                                      (const uint8_t *)"b", 1 };
    int opcode;
    const uint8_t *data;
    size_t out_len;

    memset(&assembler, 0, sizeof(assembler));
    check_int("a continuation with nothing in progress is a violation",
              websocket_message_feed(&assembler, &continuation, &opcode,
                                     &data, &out_len),
              -1);

    memset(&assembler, 0, sizeof(assembler));
    websocket_message_feed(&assembler, &text_a, &opcode, &data, &out_len);
    check_int("a new message while one is already in progress is a violation",
              websocket_message_feed(&assembler, &text_b, &opcode, &data,
                                     &out_len),
              -1);
}

/* A ping arriving between two fragments of a data message: control frames
   are never fragmented, so they are dispatched straight from decode
   rather than through the assembler, and this is what proves that path
   independently of it. */
static void test_ping_mid_stream(void) {
    uint8_t buf[32], pong[32];
    uint8_t expected_pong[32];
    uint8_t mask_key[4] = { 3, 1, 4, 1 };
    size_t frame_len = build_client_frame(buf, 1, WEBSOCKET_OP_PING,
                                          (const uint8_t *)"are you there", 13,
                                          mask_key);
    struct websocket_frame frame;
    size_t pong_len, expected_len;

    check_true("the ping decodes", websocket_frame_decode(buf, frame_len,
                                                          &frame) > 0);
    check_int("opcode is PING", frame.opcode, WEBSOCKET_OP_PING);

    pong_len = websocket_pong_for(pong, sizeof(pong), frame.payload,
                                  frame.payload_len);
    expected_len = websocket_frame_encode(expected_pong, sizeof(expected_pong),
                                          1, WEBSOCKET_OP_PONG,
                                          (const uint8_t *)"are you there", 13);
    check_size("the pong's length matches a plain PONG encode", pong_len,
              expected_len);
    check_true("the pong carries the ping's payload unchanged",
              memcmp(pong, expected_pong, pong_len) == 0);
}

static void test_close_handshake(void) {
    uint8_t close_out[16];
    uint8_t mask_key[4] = { 2, 4, 6, 8 };
    uint8_t client_close[16];
    size_t close_len = websocket_close_frame(close_out, sizeof(close_out),
                                             1000 /* normal closure */);
    struct websocket_frame frame;

    check_true("a close frame encodes", close_len == 2 + 2);

    /* A client's own close, masked, decoded the way the server would see
       it arrive. */
    memcpy(client_close, close_out, close_len);
    client_close[1] |= 0x80;
    memcpy(client_close + 2, mask_key, 4);
    memcpy(client_close + 6, close_out + 2, 2);
    mask_in_place(client_close + 6, 2, mask_key);

    check_true("the client's close decodes",
              websocket_frame_decode(client_close, 6 + 2, &frame) > 0);
    check_int("opcode is CLOSE", frame.opcode, WEBSOCKET_OP_CLOSE);
    check_int("the status code reads back as 1000",
              (frame.payload[0] << 8) | frame.payload[1], 1000);
}

int main(void) {
    test_sha1_vectors();
    test_base64_vectors();
    test_handshake_accept_value();
    test_request_parse_upgrade();
    test_request_parse_plain_get();
    test_request_parse_incomplete();
    test_request_parse_malformed();
    test_frame_decode_masked_text();
    test_frame_decode_needs_more_data();
    test_frame_decode_refuses_unmasked();
    test_frame_decode_refuses_rsv_bits();
    test_frame_decode_extended_length();
    test_frame_encode_length_forms();
    test_fragmented_message();
    test_message_feed_protocol_violations();
    test_ping_mid_stream();
    test_close_handshake();
    return check_report("HTTP/1.1 and RFC 6455, hand-written and checkable "
                        "with no window, no receiver, no network");
}
