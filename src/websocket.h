#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <stddef.h>
#include <stdint.h>

/*
 * HTTP/1.1 and RFC 6455, hand-written -- the sibling of
 * `src/capture_sidecar.h`, whose comment reads "This is not a JSON parser
 * and must not become one." This is not an HTTP server: it parses the one
 * request line and header block a WebSocket upgrade needs, and nothing a
 * body, a query string or chunked transfer would want.
 *
 * ADR-0027 is why this exists: the Viewer link needs a wire a browser can
 * open, and a browser cannot open a raw socket. Nothing in this module
 * knows sdrprobe exists -- no `struct app`, no signal frame, no
 * subscription -- that is ticket 05's, over this.
 */

/* -------------------------------------------------------------------- */
/* SHA-1 (FIPS 180-4) and base64 (RFC 4648): the handshake's two          */
/* primitives, exposed because `check-websocket` needs published vectors  */
/* to trust them and nothing here would trust them otherwise.             */
/* -------------------------------------------------------------------- */

/* One-shot SHA-1 over `data[0..len)`, written to `digest[0..20)`. */
void websocket_sha1(const uint8_t *data, size_t len, uint8_t digest[20]);

/*
 * Base64-encodes `data[0..len)` into `out`, nul-terminated. `out_cap` must
 * be at least `websocket_base64_encoded_len(len) + 1`. Returns the encoded
 * length (excluding the nul), or 0 if `out_cap` is too small.
 */
size_t websocket_base64_encode(const uint8_t *data, size_t len, char *out,
                               size_t out_cap);

/* The exact output length websocket_base64_encode() writes for `len` input
   bytes, padding included -- so a caller can size its own buffer. */
size_t websocket_base64_encoded_len(size_t len);

/*
 * The handshake's one computed value: base64(SHA-1(key + the RFC 6455
 * GUID)). `key` is the Sec-WebSocket-Key header's value, NUL-terminated.
 * Writes a NUL-terminated string to `out` and returns its length, or 0 if
 * `out_cap` is too small (28 bytes plus the terminator covers every case,
 * since SHA-1 is a fixed 20 bytes).
 */
size_t websocket_accept_value(const char *key, char *out, size_t out_cap);

/* -------------------------------------------------------------------- */
/* The one HTTP request this module reads: enough to tell a WebSocket     */
/* upgrade from a plain GET, and to find the page it asked for.           */
/* -------------------------------------------------------------------- */

#define WEBSOCKET_MAX_HEADERS 32

struct websocket_header {
    const char *name;   /* points into the caller's buffer, not owned */
    size_t name_len;
    const char *value;  /* likewise */
    size_t value_len;
};

struct websocket_request {
    const char *method;
    size_t method_len;
    const char *path;
    size_t path_len;
    struct websocket_header headers[WEBSOCKET_MAX_HEADERS];
    int header_count;
};

/*
 * Parses one HTTP/1.1 request's method, path and headers out of `buf`, up
 * to and including the blank line that ends the header block. Every field
 * in `out` points into `buf` -- nothing is copied or allocated, so `buf`
 * must outlive `out`.
 *
 * Returns the offset of the byte just past the blank line (where a body,
 * if any, would begin) on success; 0 if `buf[0..len)` does not yet hold a
 * complete header block (the caller should read more and try again); -1 if
 * it is malformed, or holds more headers than WEBSOCKET_MAX_HEADERS.
 */
int websocket_request_parse(const char *buf, size_t len,
                            struct websocket_request *out);

/*
 * Case-insensitive header lookup by name (a NUL-terminated ASCII string).
 * Returns NULL if absent. The value found is not NUL-terminated -- read
 * exactly `value_len` bytes of it.
 */
const struct websocket_header *
websocket_request_header(const struct websocket_request *req, const char *name);

/*
 * Whether a parsed request is a WebSocket upgrade: an `Upgrade` header
 * whose value is "websocket" (case-insensitive, RFC 6455 4.2.1), and a
 * `Connection` header whose comma-separated tokens include "Upgrade"
 * (case-insensitive -- real clients send "keep-alive, Upgrade").
 */
int websocket_request_is_upgrade(const struct websocket_request *req);

/* -------------------------------------------------------------------- */
/* Frames (RFC 6455 section 5).                                          */
/* -------------------------------------------------------------------- */

enum websocket_opcode {
    WEBSOCKET_OP_CONTINUATION = 0x0,
    WEBSOCKET_OP_TEXT = 0x1,
    WEBSOCKET_OP_BINARY = 0x2,
    WEBSOCKET_OP_CLOSE = 0x8,
    WEBSOCKET_OP_PING = 0x9,
    WEBSOCKET_OP_PONG = 0xA
};

struct websocket_frame {
    int fin;
    int opcode;
    const uint8_t *payload; /* unmasked; points into the caller's buffer */
    size_t payload_len;
};

/*
 * Decodes one frame from the front of `buf[0..len)`. A client frame is
 * always masked (RFC 6455 5.1); this unmasks it **in place**, inside
 * `buf`, since decode owns no storage of its own -- `out->payload` then
 * points at the now-unmasked bytes within `buf`.
 *
 * Returns the number of bytes the frame occupied (> 0) on success; 0 if
 * `buf` does not yet hold a complete frame (read more and try again); -1
 * if it is malformed -- an RSV bit is set, the mask bit is clear on what
 * must be a client frame, or a control frame (opcode >= 0x8) is fragmented
 * or longer than 125 bytes, all of which RFC 6455 5.1/5.5 make a
 * connection-ending protocol error rather than something to tolerate.
 */
long websocket_frame_decode(uint8_t *buf, size_t len,
                            struct websocket_frame *out);

/*
 * Encodes one server-to-client frame into `out`. Never masked -- RFC 6455
 * 5.1 forbids a server from masking, which is the other half of the same
 * rule `websocket_frame_decode()` enforces on what it reads. Returns the
 * number of bytes written, or 0 if `out_cap` is too small.
 */
size_t websocket_frame_encode(uint8_t *out, size_t out_cap, int fin,
                              int opcode, const uint8_t *payload,
                              size_t payload_len);

/* A pong carrying a ping's own application data unchanged (RFC 6455
   5.5.3). */
size_t websocket_pong_for(uint8_t *out, size_t out_cap,
                          const uint8_t *ping_payload, size_t ping_len);

/* A close frame carrying a status code (RFC 6455 5.5.1), for replying to a
   peer's close or for closing on this end's own initiative. */
size_t websocket_close_frame(uint8_t *out, size_t out_cap, uint16_t code);

/* -------------------------------------------------------------------- */
/* Reassembling a fragmented message (RFC 6455 5.4).                     */
/* -------------------------------------------------------------------- */

/* A generous ceiling on one reassembled message -- large enough for a
   handshake or a control exchange, and deliberately not sized for the
   Viewer link's real payloads, which are ticket 05's own concern. */
#define WEBSOCKET_MESSAGE_MAX (1 << 20)

struct websocket_message_assembler {
    uint8_t buffer[WEBSOCKET_MESSAGE_MAX];
    size_t length;
    int opcode;  /* the first fragment's opcode: TEXT or BINARY */
    int active;  /* whether a fragmented message is in progress */
};

/*
 * Feeds one decoded data frame (never a control frame -- RFC 6455 5.4
 * forbids fragmenting those, and a caller dispatches ping/pong/close
 * straight from `websocket_frame_decode()`) into the assembler.
 *
 * Returns 1 when `frame->fin` completes a message: `*out_opcode`,
 * `*out_data` and `*out_len` describe the whole thing, and the assembler
 * resets. Returns 0 if more fragments are still expected. Returns -1 on a
 * protocol violation -- a continuation frame with nothing in progress, or
 * a new TEXT/BINARY frame while one already is (RFC 6455 5.4: a peer must
 * not interleave). Returns -2 if the assembled message would exceed
 * WEBSOCKET_MESSAGE_MAX.
 */
int websocket_message_feed(struct websocket_message_assembler *assembler,
                          const struct websocket_frame *frame,
                          int *out_opcode, const uint8_t **out_data,
                          size_t *out_len);

#endif
