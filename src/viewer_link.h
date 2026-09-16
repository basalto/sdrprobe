#ifndef VIEWER_LINK_H
#define VIEWER_LINK_H

#include <stddef.h>
#include <stdint.h>

#include "scope_view_model.h"
#include "websocket.h"

/*
 * The Viewer link (ADR-0027): the Scope's view model, pushed to loopback
 * WebSocket clients as State updates. This is where `src/websocket.c`
 * (HTTP/1.1, RFC 6455 -- nothing here about sdrprobe) meets
 * `src/scope_view_model.h` (the Scope's measurements -- nothing there about
 * a network). Neither knows the other exists; this is what joins them.
 *
 * Two rules from ADR-0027 are load-bearing here and nowhere softened:
 *
 * A State update is replaceable. At most one unsent message per client per
 * stream is kept; a newer publish overwrites an unsent one rather than
 * queueing behind it, and the overwritten one counts as dropped. This is
 * ADR-0002's freshness rule carried onto the wire, and it is why publishing
 * never blocks -- a slow Viewer falls behind on content, never on time.
 *
 * A subscription says what to compute. Nothing is sent to a client for a
 * stream it has not asked for, because with no screen there is nothing
 * else to gate a technology's per-block cost with (ticket 05's own point).
 *
 * Binary down, text up: `spectrum` and `waterfall_row` are binary WS
 * frames with a small fixed header (below); `receiver_state` is a JSON
 * text frame, because writing JSON is already solved
 * (`survey_json_escape()`, src/survey_store.c) and nothing here parses it.
 * The one thing read from a client -- `subscribe <stream> ...` -- is a
 * whitespace-delimited line, on the same principle
 * `src/capture_sidecar.h` states outright: this is not a JSON parser and
 * must not become one.
 */

/* -------------------------------------------------------------------- */
/* The wire format for the two binary streams.                           */
/* -------------------------------------------------------------------- */

/*
 * Every multi-byte field is little-endian, fixed and explicit -- not "host
 * order" -- so the format does not depend on what this happens to run on,
 * and a browser's DataView reads it with `littleEndian: true` throughout.
 *
 *   offset  0  u8   protocol version (1)
 *   offset  1  u8   message type (viewer_message_type)
 *   offset  2  u16  reserved, always 0
 *   offset  4  u32  tuning generation (ADR-0027)
 *   offset  8  u64  server monotonic timestamp, milliseconds
 *   offset 16  u32  bins: how many float32 samples make up one array
 *   offset 20  ...  payload: `bins` float32 for a waterfall row; `bins`
 *                   float32 average followed by `bins` float32 peak for a
 *                   spectrum
 */
#define VIEWER_LINK_PROTOCOL_VERSION 1
#define VIEWER_LINK_HEADER_BYTES 20

enum viewer_message_type {
    VIEWER_MESSAGE_SPECTRUM = 1,
    VIEWER_MESSAGE_WATERFALL_ROW = 2
};

/* The largest a message this link ever sends can be: a spectrum at the
   widest transform the Scope's resolution stepper reaches
   (SDR_DSP_FFT_MAX bins, both arrays). Every stream's pending slot is sized
   to this so publishing never has to ask whether this block's happens to
   be smaller than last block's. */
#define VIEWER_STREAM_MESSAGE_MAX \
    (VIEWER_LINK_HEADER_BYTES + 2 * SDR_DSP_FFT_MAX * (int)sizeof(float))

enum viewer_stream {
    VIEWER_STREAM_SPECTRUM = 0,
    VIEWER_STREAM_WATERFALL,
    VIEWER_STREAM_RECEIVER_STATE,
    VIEWER_STREAM_COUNT
};

/* -------------------------------------------------------------------- */
/* One connected client.                                                  */
/* -------------------------------------------------------------------- */

enum viewer_client_state {
    VIEWER_CLIENT_HANDSHAKING = 0, /* reading the HTTP upgrade request */
    VIEWER_CLIENT_OPEN,            /* the WebSocket is up */
    VIEWER_CLIENT_CLOSED           /* the slot is free */
};

struct viewer_stream_slot {
    uint8_t data[VIEWER_STREAM_MESSAGE_MAX + 16]; /* +WS frame header room */
    size_t length;   /* 0: nothing pending */
    size_t sent;     /* bytes of `data[0..length)` already written */
    uint64_t sent_count;
    uint64_t dropped_count;
};

struct viewer_client {
    enum viewer_client_state state;
    int fd;
    int subscribed[VIEWER_STREAM_COUNT];
    struct viewer_stream_slot slot[VIEWER_STREAM_COUNT];
    struct websocket_message_assembler assembler;

    /* The handshake's own small read buffer -- discarded once OPEN. */
    char handshake_buf[4096];
    size_t handshake_have;

    /* Bytes read off the wire once OPEN, awaiting a complete frame. */
    uint8_t read_buf[1 << 16];
    size_t read_have;

    /* The largest `ioctl(fd, TIOCOUTQ, ...)` this client's socket has
       reported: what ticket 05 calls the send buffer's high-water mark,
       and a fact about the kernel's own queue, not this module's one-slot
       one. */
    int send_queue_high_water;

    /*
     * Three streams, one socket: whichever stream is only partly on the
     * wire (slot[stream].sent > 0, < length) owns this connection's next
     * byte until it finishes. -1 when nothing is in flight. Without this,
     * a client one poll cycle behind can have its partially-sent frame
     * pre-empted by a *different*, fully-ready stream's frame -- both
     * write to the same fd, so the pre-emption does not queue behind the
     * partial send, it splices into the middle of it. See
     * viewer_link_poll()'s flush_client().
     */
    int inflight_stream;
};

#define VIEWER_LINK_MAX_CLIENTS 8

struct viewer_link {
    int listen_fd;
    struct viewer_client clients[VIEWER_LINK_MAX_CLIENTS];
};

/* Binds and listens on 127.0.0.1:`port` (ADR-0027: loopback only -- this
   is not a configuration option here). Returns 0 on success, -1 on
   failure with a reason on stderr. */
int viewer_link_open(struct viewer_link *link, uint16_t port);

/* Closes every client and the listening socket. */
void viewer_link_close(struct viewer_link *link);

/*
 * One pass: accept new connections, read whatever is available from each
 * client (completing a handshake, parsing subscribe lines, answering
 * pings, retiring a client that closed or violated the protocol), and
 * retry sending any client's still-pending stream slots. Never blocks
 * longer than `timeout_ms` even if nothing is ready.
 */
void viewer_link_poll(struct viewer_link *link, int timeout_ms);

/*
 * Publishes this block's view model to every client subscribed to the
 * named stream: encodes once, then for each such client either sends
 * immediately or -- if that would block, or a previous message for this
 * stream is still unsent -- replaces the pending slot, counting the
 * replaced one as dropped. `now_ms` is the caller's monotonic clock in
 * milliseconds, stamped into the header unchanged.
 *
 * A caller with nobody subscribed to a stream should still call the
 * matching publish function -- these are cheap no-ops in that case -- as
 * an alternative to a caller checking subscriptions first (either is
 * fine); what a caller must not do is skip work implied by "a subscription
 * says what to compute" at a higher level (ticket 05), which these
 * functions have no way to see.
 */
void viewer_link_publish_spectrum(struct viewer_link *link,
                                  const struct scope_view_model *svm,
                                  uint64_t now_ms);
void viewer_link_publish_waterfall_row(struct viewer_link *link,
                                       const struct scope_view_model *svm,
                                       uint64_t now_ms);
void viewer_link_publish_receiver_state(struct viewer_link *link,
                                        const struct scope_view_model *svm,
                                        uint64_t now_ms);

/* How many clients are currently open -- for a status line, or a check. */
int viewer_link_client_count(const struct viewer_link *link);

#endif
