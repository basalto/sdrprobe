#define _POSIX_C_SOURCE 200809L

#include "websocket.h"

#include <string.h>

/* ====================================================================== */
/* SHA-1 (FIPS 180-4).                                                    */
/* ====================================================================== */

static uint32_t sha1_rotl(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

/* One 64-byte block. `h` is the five running words, updated in place. */
static void sha1_block(uint32_t h[5], const uint8_t block[64]) {
    uint32_t w[80];
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    int i;

    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               (uint32_t)block[i * 4 + 3];
    for (i = 16; i < 80; i++)
        w[i] = sha1_rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    for (i = 0; i < 80; i++) {
        uint32_t f, k, temp;

        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        temp = sha1_rotl(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = sha1_rotl(b, 30);
        b = a;
        a = temp;
    }

    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

void websocket_sha1(const uint8_t *data, size_t len, uint8_t digest[20]) {
    uint32_t h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u,
                      0xC3D2E1F0u };
    size_t full_blocks = len / 64;
    size_t tail_len = len % 64;
    uint8_t tail[128]; /* the last partial block plus padding: at most two
                          64-byte blocks, when the partial block has no room
                          left for the 0x80 byte and the 8-byte length. */
    size_t tail_total;
    size_t i;
    uint64_t bit_len = (uint64_t)len * 8;

    for (i = 0; i < full_blocks; i++)
        sha1_block(h, data + i * 64);

    memcpy(tail, data + full_blocks * 64, tail_len);
    tail[tail_len] = 0x80;
    tail_total = tail_len + 1;
    if (tail_total <= 56) {
        memset(tail + tail_total, 0, 56 - tail_total);
        tail_total = 56;
    } else {
        memset(tail + tail_total, 0, 64 - tail_total);
        tail_total = 64;
        memset(tail + tail_total, 0, 56);
        tail_total += 56;
    }
    for (i = 0; i < 8; i++)
        tail[tail_total + i] = (uint8_t)(bit_len >> (56 - 8 * i));
    tail_total += 8;

    for (i = 0; i < tail_total; i += 64)
        sha1_block(h, tail + i);

    for (i = 0; i < 5; i++) {
        digest[i * 4] = (uint8_t)(h[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(h[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(h[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)h[i];
    }
}

/* ====================================================================== */
/* Base64 (RFC 4648).                                                     */
/* ====================================================================== */

static const char base64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t websocket_base64_encoded_len(size_t len) {
    return ((len + 2) / 3) * 4;
}

size_t websocket_base64_encode(const uint8_t *data, size_t len, char *out,
                               size_t out_cap) {
    size_t encoded_len = websocket_base64_encoded_len(len);
    size_t i, o = 0;

    if (out_cap < encoded_len + 1)
        return 0;

    for (i = 0; i + 3 <= len; i += 3) {
        uint32_t chunk = ((uint32_t)data[i] << 16) |
                         ((uint32_t)data[i + 1] << 8) | (uint32_t)data[i + 2];
        out[o++] = base64_alphabet[(chunk >> 18) & 0x3F];
        out[o++] = base64_alphabet[(chunk >> 12) & 0x3F];
        out[o++] = base64_alphabet[(chunk >> 6) & 0x3F];
        out[o++] = base64_alphabet[chunk & 0x3F];
    }
    if (len - i == 1) {
        uint32_t chunk = (uint32_t)data[i] << 16;
        out[o++] = base64_alphabet[(chunk >> 18) & 0x3F];
        out[o++] = base64_alphabet[(chunk >> 12) & 0x3F];
        out[o++] = '=';
        out[o++] = '=';
    } else if (len - i == 2) {
        uint32_t chunk = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
        out[o++] = base64_alphabet[(chunk >> 18) & 0x3F];
        out[o++] = base64_alphabet[(chunk >> 12) & 0x3F];
        out[o++] = base64_alphabet[(chunk >> 6) & 0x3F];
        out[o++] = '=';
    }
    out[o] = '\0';
    return o;
}

/* ====================================================================== */
/* The handshake.                                                         */
/* ====================================================================== */

/* RFC 6455 1.3: a fixed GUID, concatenated onto the client's key before
   hashing -- not a secret, just a way to prove the peer meant this
   handshake rather than some other protocol sharing the same port. */
#define WEBSOCKET_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

size_t websocket_accept_value(const char *key, char *out, size_t out_cap) {
    char concatenated[256];
    size_t key_len = strlen(key);
    size_t guid_len = strlen(WEBSOCKET_GUID);
    uint8_t digest[20];

    if (key_len + guid_len >= sizeof(concatenated))
        return 0;
    memcpy(concatenated, key, key_len);
    memcpy(concatenated + key_len, WEBSOCKET_GUID, guid_len);
    websocket_sha1((const uint8_t *)concatenated, key_len + guid_len, digest);
    return websocket_base64_encode(digest, sizeof(digest), out, out_cap);
}

/* ====================================================================== */
/* HTTP request parsing -- one request line, one header block, no body.   */
/* ====================================================================== */

static int header_end(const char *buf, size_t len, size_t *end_out) {
    size_t i;

    for (i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' &&
            buf[i + 3] == '\n') {
            *end_out = i + 4;
            return 1;
        }
    }
    return 0;
}

static const char *find_crlf(const char *buf, size_t len) {
    size_t i;

    for (i = 0; i + 1 < len; i++)
        if (buf[i] == '\r' && buf[i + 1] == '\n')
            return buf + i;
    return NULL;
}

static int ci_char_eq(char a, char b) {
    if (a >= 'A' && a <= 'Z')
        a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z')
        b = (char)(b - 'A' + 'a');
    return a == b;
}

static int ci_eq(const char *a, size_t a_len, const char *b) {
    size_t b_len = strlen(b);
    size_t i;

    if (a_len != b_len)
        return 0;
    for (i = 0; i < a_len; i++)
        if (!ci_char_eq(a[i], b[i]))
            return 0;
    return 1;
}

int websocket_request_parse(const char *buf, size_t len,
                            struct websocket_request *out) {
    size_t header_block_end;
    const char *line;
    const char *cursor;
    const char *line_end;
    const char *sp1, *sp2;

    memset(out, 0, sizeof(*out));
    if (!header_end(buf, len, &header_block_end))
        return 0;

    line = buf;
    line_end = find_crlf(line, (size_t)(buf + len - line));
    if (!line_end)
        return -1;

    /* "METHOD SP path SP HTTP-version" -- two spaces, the path in between. */
    sp1 = memchr(line, ' ', (size_t)(line_end - line));
    if (!sp1)
        return -1;
    sp2 = memchr(sp1 + 1, ' ', (size_t)(line_end - (sp1 + 1)));
    if (!sp2)
        return -1;
    out->method = line;
    out->method_len = (size_t)(sp1 - line);
    out->path = sp1 + 1;
    out->path_len = (size_t)(sp2 - (sp1 + 1));

    cursor = line_end + 2;
    while (cursor < buf + header_block_end - 2) {
        const char *colon;
        const char *value_start;
        const char *this_line_end = find_crlf(cursor,
                                              (size_t)(buf + header_block_end - cursor));

        if (!this_line_end || this_line_end == cursor)
            break; /* the blank line ending the block */
        colon = memchr(cursor, ':', (size_t)(this_line_end - cursor));
        if (!colon)
            return -1;
        value_start = colon + 1;
        while (value_start < this_line_end && *value_start == ' ')
            value_start++;
        if (out->header_count >= WEBSOCKET_MAX_HEADERS)
            return -1;
        out->headers[out->header_count].name = cursor;
        out->headers[out->header_count].name_len = (size_t)(colon - cursor);
        out->headers[out->header_count].value = value_start;
        out->headers[out->header_count].value_len =
            (size_t)(this_line_end - value_start);
        out->header_count++;
        cursor = this_line_end + 2;
    }
    return (int)header_block_end;
}

const struct websocket_header *
websocket_request_header(const struct websocket_request *req, const char *name) {
    int i;

    for (i = 0; i < req->header_count; i++)
        if (ci_eq(req->headers[i].name, req->headers[i].name_len, name))
            return &req->headers[i];
    return NULL;
}

/* Whether one comma-separated token in `value[0..len)` case-insensitively
   equals `token`, ignoring surrounding spaces around each token -- what
   RFC 6455 4.2.1 asks of the Connection header's contents. */
static int token_list_contains(const char *value, size_t len,
                               const char *token) {
    size_t start = 0;
    size_t i;

    for (i = 0; i <= len; i++) {
        if (i == len || value[i] == ',') {
            size_t a = start, b = i;

            while (a < b && value[a] == ' ')
                a++;
            while (b > a && value[b - 1] == ' ')
                b--;
            if (ci_eq(value + a, b - a, token))
                return 1;
            start = i + 1;
        }
    }
    return 0;
}

int websocket_request_is_upgrade(const struct websocket_request *req) {
    const struct websocket_header *upgrade =
        websocket_request_header(req, "Upgrade");
    const struct websocket_header *connection =
        websocket_request_header(req, "Connection");

    if (!upgrade || !connection)
        return 0;
    if (!ci_eq(upgrade->value, upgrade->value_len, "websocket"))
        return 0;
    return token_list_contains(connection->value, connection->value_len,
                               "Upgrade");
}

/* ====================================================================== */
/* Frames (RFC 6455 section 5).                                           */
/* ====================================================================== */

long websocket_frame_decode(uint8_t *buf, size_t len,
                            struct websocket_frame *out) {
    int fin, opcode, masked;
    uint64_t payload_len;
    size_t header_len;
    uint8_t mask_key[4];
    size_t i;

    if (len < 2)
        return 0;
    if (buf[0] & 0x70) /* an RSV bit set with no extension negotiated */
        return -1;
    fin = (buf[0] & 0x80) != 0;
    opcode = buf[0] & 0x0F;
    if (opcode != WEBSOCKET_OP_CONTINUATION && opcode != WEBSOCKET_OP_TEXT &&
        opcode != WEBSOCKET_OP_BINARY && opcode != WEBSOCKET_OP_CLOSE &&
        opcode != WEBSOCKET_OP_PING && opcode != WEBSOCKET_OP_PONG)
        /* RFC 6455 5.2: an unknown opcode fails the connection rather than
           being tolerated or guessed at. */
        return -1;
    masked = (buf[1] & 0x80) != 0;
    payload_len = buf[1] & 0x7F;
    header_len = 2;

    if (payload_len == 126) {
        if (len < header_len + 2)
            return 0;
        payload_len = ((uint64_t)buf[header_len] << 8) | buf[header_len + 1];
        header_len += 2;
    } else if (payload_len == 127) {
        if (len < header_len + 8)
            return 0;
        payload_len = 0;
        for (i = 0; i < 8; i++)
            payload_len = (payload_len << 8) | buf[header_len + i];
        header_len += 8;
    }

    if (opcode >= 0x8) {
        /* Control frames: never fragmented, never longer than 125 bytes
           (RFC 6455 5.5). */
        if (!fin || payload_len > 125)
            return -1;
    }
    if (!masked)
        /* RFC 6455 5.1: "a server MUST close the connection upon receiving
           a frame that is not masked." This decoder only ever reads client
           frames, so that is every frame it is handed. */
        return -1;

    if (len < header_len + 4)
        return 0;
    memcpy(mask_key, buf + header_len, 4);
    header_len += 4;

    if (len < header_len + payload_len)
        return 0;
    for (i = 0; i < payload_len; i++)
        buf[header_len + i] ^= mask_key[i % 4];

    out->fin = fin;
    out->opcode = opcode;
    out->payload = buf + header_len;
    out->payload_len = (size_t)payload_len;
    return (long)(header_len + payload_len);
}

size_t websocket_frame_encode(uint8_t *out, size_t out_cap, int fin,
                              int opcode, const uint8_t *payload,
                              size_t payload_len) {
    size_t header_len;

    if (payload_len <= 125)
        header_len = 2;
    else if (payload_len <= 0xFFFF)
        header_len = 4;
    else
        header_len = 10;
    if (out_cap < header_len + payload_len)
        return 0;

    out[0] = (uint8_t)((fin ? 0x80 : 0x00) | (opcode & 0x0F));
    if (payload_len <= 125) {
        out[1] = (uint8_t)payload_len; /* MASK bit clear: a server never masks */
    } else if (payload_len <= 0xFFFF) {
        out[1] = 126;
        out[2] = (uint8_t)(payload_len >> 8);
        out[3] = (uint8_t)payload_len;
    } else {
        int i;

        out[1] = 127;
        for (i = 0; i < 8; i++)
            out[2 + i] = (uint8_t)((uint64_t)payload_len >> (56 - 8 * i));
    }
    if (payload_len > 0)
        memcpy(out + header_len, payload, payload_len);
    return header_len + payload_len;
}

size_t websocket_pong_for(uint8_t *out, size_t out_cap,
                          const uint8_t *ping_payload, size_t ping_len) {
    return websocket_frame_encode(out, out_cap, 1, WEBSOCKET_OP_PONG,
                                  ping_payload, ping_len);
}

size_t websocket_close_frame(uint8_t *out, size_t out_cap, uint16_t code) {
    uint8_t payload[2];

    payload[0] = (uint8_t)(code >> 8);
    payload[1] = (uint8_t)code;
    return websocket_frame_encode(out, out_cap, 1, WEBSOCKET_OP_CLOSE, payload,
                                  sizeof(payload));
}

/* ====================================================================== */
/* Reassembly (RFC 6455 section 5.4).                                     */
/* ====================================================================== */

int websocket_message_feed(struct websocket_message_assembler *assembler,
                          const struct websocket_frame *frame,
                          int *out_opcode, const uint8_t **out_data,
                          size_t *out_len) {
    int opcode = frame->opcode;

    if (opcode == WEBSOCKET_OP_CONTINUATION) {
        if (!assembler->active)
            return -1;
    } else {
        if (assembler->active)
            return -1; /* a new message started before the last one's FIN */
        assembler->active = 1;
        assembler->length = 0;
        assembler->opcode = opcode;
    }

    if (assembler->length + frame->payload_len > WEBSOCKET_MESSAGE_MAX) {
        assembler->active = 0;
        assembler->length = 0;
        return -2;
    }
    memcpy(assembler->buffer + assembler->length, frame->payload,
          frame->payload_len);
    assembler->length += frame->payload_len;

    if (!frame->fin)
        return 0;

    *out_opcode = assembler->opcode;
    *out_data = assembler->buffer;
    *out_len = assembler->length;
    assembler->active = 0;
    assembler->length = 0;
    return 1;
}
