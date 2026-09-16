#define _POSIX_C_SOURCE 200809L

#include "viewer_link.h"

#include "debug_log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "viewer_page.h"

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int viewer_link_open(struct viewer_link *link, uint16_t port) {
    struct sockaddr_in addr;
    int one = 1;
    int i;

    memset(link, 0, sizeof(*link));
    for (i = 0; i < VIEWER_LINK_MAX_CLIENTS; i++) {
        link->clients[i].state = VIEWER_CLIENT_CLOSED;
        link->clients[i].fd = -1;
    }

    link->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (link->listen_fd < 0) {
        perror("viewer_link: socket");
        return -1;
    }
    setsockopt(link->listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    /* Loopback only -- ADR-0027 is explicit that this is not a
       configuration option here. */
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (bind(link->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("viewer_link: bind");
        close(link->listen_fd);
        link->listen_fd = -1;
        return -1;
    }
    if (listen(link->listen_fd, 8) < 0) {
        perror("viewer_link: listen");
        close(link->listen_fd);
        link->listen_fd = -1;
        return -1;
    }
    if (set_nonblocking(link->listen_fd) < 0) {
        perror("viewer_link: fcntl");
        close(link->listen_fd);
        link->listen_fd = -1;
        return -1;
    }
    return 0;
}

static const char *const stream_names[VIEWER_STREAM_COUNT] = {
    "spectrum", "waterfall", "receiver_state", "link_health"
};

/*
 * Ticket 05's own falsifiability criterion: without this, "a slow Viewer
 * stays current" and "a fast Viewer misses nothing" are both unmeasurable
 * claims. Printed once, when a client disconnects (voluntarily or by
 * protocol fault) and again for anything still open at
 * viewer_link_close() -- both routes go through client_close(), which is
 * also where the counters this reads are about to be zeroed, so there is
 * exactly one place a client's lifetime stats can still be read.
 */
static void report_client_stats(int fd, const struct viewer_client *c) {
    int s;

    if (c->state == VIEWER_CLIENT_CLOSED)
        return;
    fprintf(stderr, "viewer link: client fd %d disconnecting, send queue "
                    "high-water %d bytes\n",
            fd, c->send_queue_high_water);
    for (s = 0; s < VIEWER_STREAM_COUNT; s++) {
        const struct viewer_stream_slot *slot = &c->slot[s];

        fprintf(stderr, "  %-14s sent %llu dropped %llu\n", stream_names[s],
                (unsigned long long)slot->sent_count,
                (unsigned long long)slot->dropped_count);
    }
}

static void client_close(struct viewer_client *c) {
    int fd = c->fd;

    report_client_stats(fd, c);
    if (fd >= 0)
        close(fd);
    memset(c, 0, sizeof(*c));
    c->fd = -1;
    c->state = VIEWER_CLIENT_CLOSED;
}

void viewer_link_close(struct viewer_link *link) {
    int i;

    for (i = 0; i < VIEWER_LINK_MAX_CLIENTS; i++)
        if (link->clients[i].state != VIEWER_CLIENT_CLOSED)
            client_close(&link->clients[i]);
    if (link->listen_fd >= 0)
        close(link->listen_fd);
    link->listen_fd = -1;
}

int viewer_link_client_count(const struct viewer_link *link) {
    int i, n = 0;

    for (i = 0; i < VIEWER_LINK_MAX_CLIENTS; i++)
        if (link->clients[i].state != VIEWER_CLIENT_CLOSED)
            n++;
    return n;
}

/*
 * A short, bounded best-effort send for the handshake response and the
 * served page -- both small, one-shot, and written to a socket whose
 * kernel send buffer has nothing else queued yet. Not the streaming path:
 * that is try_flush_slot(), which never blocks and never retries beyond
 * one poll cycle.
 *
 * Every send() in this file passes MSG_NOSIGNAL. Without it, sending on a
 * socket the peer has already reset raises SIGPIPE, whose default
 * disposition kills the whole process -- so an operator's --serve session
 * on a live receiver used to die the moment a browser tab was closed
 * mid-stream, taking the acquisition down with it. MSG_NOSIGNAL turns
 * that into an ordinary send() failure (errno EPIPE), which the existing
 * error handling in try_flush_slot() and here already treats as "close
 * this one client" -- nothing else needed changing once the crash itself
 * was found.
 */
static int send_best_effort(int fd, const void *data, size_t len) {
    const char *p = data;
    int attempts = 0;

    while (len > 0 && attempts < 1000) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);

        if (n > 0) {
            p += n;
            len -= (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            attempts++;
            continue;
        }
        return -1;
    }
    return len == 0 ? 0 : -1;
}

static void serve_page(struct viewer_client *c) {
    char header[256];
    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/html; charset=utf-8\r\n"
                              "Content-Length: %zu\r\n"
                              "Connection: close\r\n\r\n",
                              sizeof(VIEWER_PAGE_HTML) - 1);

    if (send_best_effort(c->fd, header, (size_t)header_len) == 0)
        send_best_effort(c->fd, VIEWER_PAGE_HTML, sizeof(VIEWER_PAGE_HTML) - 1);
    client_close(c);
}

static void serve_upgrade(struct viewer_client *c,
                         const struct websocket_request *req) {
    const struct websocket_header *key =
        websocket_request_header(req, "Sec-WebSocket-Key");
    char key_copy[256];
    char accept[64];
    char response[256];
    int response_len;

    if (!key || key->value_len >= sizeof(key_copy)) {
        client_close(c);
        return;
    }
    memcpy(key_copy, key->value, key->value_len);
    key_copy[key->value_len] = '\0';
    if (websocket_accept_value(key_copy, accept, sizeof(accept)) == 0) {
        client_close(c);
        return;
    }
    response_len = snprintf(response, sizeof(response),
                            "HTTP/1.1 101 Switching Protocols\r\n"
                            "Upgrade: websocket\r\n"
                            "Connection: Upgrade\r\n"
                            "Sec-WebSocket-Accept: %s\r\n\r\n",
                            accept);
    if (send_best_effort(c->fd, response, (size_t)response_len) < 0) {
        client_close(c);
        return;
    }
    c->state = VIEWER_CLIENT_OPEN;
    c->handshake_have = 0;
}

static void handle_handshake_data(struct viewer_client *c) {
    struct websocket_request req;
    int consumed = websocket_request_parse(c->handshake_buf, c->handshake_have,
                                           &req);

    if (consumed == 0)
        return; /* not a complete header block yet */
    if (consumed < 0 || c->handshake_have >= sizeof(c->handshake_buf)) {
        client_close(c);
        return;
    }
    if (websocket_request_is_upgrade(&req))
        serve_upgrade(c, &req);
    else
        serve_page(c); /* closes the connection itself */
}

/* "subscribe spectrum waterfall" -- a fresh line replaces the whole
   subscription set (an empty one clears it), never accumulates one stream
   at a time, so a client that changes its mind cannot end up subscribed to
   something it meant to drop. */
static void handle_subscribe_line(struct viewer_client *c, const char *line,
                                  size_t len) {
    size_t i = 0;
    int wanted[VIEWER_STREAM_COUNT];

    memset(wanted, 0, sizeof(wanted));
    while (i < len && line[i] != ' ')
        i++; /* skip the "subscribe" word itself */
    while (i < len) {
        size_t start;

        while (i < len && line[i] == ' ')
            i++;
        start = i;
        while (i < len && line[i] != ' ')
            i++;
        if (i > start) {
            size_t tok_len = i - start;

            if (tok_len == 8 && memcmp(line + start, "spectrum", 8) == 0)
                wanted[VIEWER_STREAM_SPECTRUM] = 1;
            else if (tok_len == 9 && memcmp(line + start, "waterfall", 9) == 0)
                wanted[VIEWER_STREAM_WATERFALL] = 1;
            else if (tok_len == 14 &&
                    memcmp(line + start, "receiver_state", 14) == 0)
                wanted[VIEWER_STREAM_RECEIVER_STATE] = 1;
            else if (tok_len == 11 &&
                    memcmp(line + start, "link_health", 11) == 0)
                wanted[VIEWER_STREAM_LINK_HEALTH] = 1;
        }
    }
    memcpy(c->subscribed, wanted, sizeof(wanted));
    if (debug_log_active()) {
        char summary[64] = "";
        int s;

        for (s = 0; s < VIEWER_STREAM_COUNT; s++)
            if (wanted[s]) {
                if (summary[0])
                    strncat(summary, " ", sizeof(summary) - strlen(summary) - 1);
                strncat(summary, stream_names[s],
                       sizeof(summary) - strlen(summary) - 1);
            }
        debug_log_write("viewer", "client fd %d subscribed: %s", c->fd,
                        summary[0] ? summary : "(nothing)");
    }
}

/*
 * Whether a stream's slot may be overwritten with a new message.
 *
 * A message that has not started sending (`sent == 0`) is safe to replace
 * outright -- the client has seen none of its bytes. A message that is
 * only *partially* sent is not: once any byte of a WebSocket frame has
 * reached the kernel's send buffer, the rest of that exact frame must
 * follow it, because a byte stream carries no boundary a client could
 * resynchronize on if it stopped partway and a different frame's bytes
 * came next. That was a real bug here, caught by the CLI Viewer emulator
 * (scripts/viewer_client.py)'s --slow mode: a slow client accumulates
 * partially-sent messages (that is what "slow" means), and every publish
 * during that window was clobbering the in-flight buffer, splicing two
 * frames' bytes together into something no decoder could parse.
 *
 * Either way a replaced-or-would-replace message counts as dropped -- the
 * ticket's "updates dropped" is about the Viewer never seeing it, and that
 * is equally true whether the old one was thrown away or the new one was.
 */
static int slot_ready_for_new_message(struct viewer_stream_slot *slot) {
    if (slot->length == 0)
        return 1;
    slot->dropped_count++;
    return slot->sent == 0;
}

static void try_flush_slot(struct viewer_client *c, enum viewer_stream stream) {
    struct viewer_stream_slot *slot = &c->slot[stream];
    int queued = 0;

    if (slot->length == 0 || slot->sent >= slot->length)
        return;
    for (;;) {
        ssize_t n = send(c->fd, slot->data + slot->sent,
                        slot->length - slot->sent, MSG_NOSIGNAL);
        if (n > 0) {
            slot->sent += (size_t)n;
            if (slot->sent >= slot->length) {
                /* Fully sent: stop, rather than loop back into another
                   send() of the now-empty remainder -- send(fd, p, 0, 0)
                   returns 0, which is neither the success case above nor
                   EAGAIN below, and was being read as a hard failure. */
                slot->length = 0;
                slot->sent = 0;
                slot->sent_count++;
                break;
            }
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        client_close(c);
        return;
    }
    if (ioctl(c->fd, TIOCOUTQ, &queued) == 0 &&
        queued > c->send_queue_high_water)
        c->send_queue_high_water = queued;
}

static void handle_open_data(struct viewer_client *c) {
    for (;;) {
        struct websocket_frame frame;
        long consumed = websocket_frame_decode(c->read_buf, c->read_have,
                                               &frame);

        if (consumed == 0)
            return;
        if (consumed < 0) {
            client_close(c);
            return;
        }
        if (frame.opcode == WEBSOCKET_OP_PING) {
            uint8_t pong[256];
            size_t pong_len = websocket_pong_for(pong, sizeof(pong),
                                                 frame.payload,
                                                 frame.payload_len);

            if (pong_len > 0)
                send_best_effort(c->fd, pong, pong_len);
        } else if (frame.opcode == WEBSOCKET_OP_CLOSE) {
            uint8_t reply[8];
            size_t reply_len = websocket_close_frame(reply, sizeof(reply),
                                                      1000);

            send_best_effort(c->fd, reply, reply_len);
            client_close(c);
            return;
        } else if (frame.opcode != WEBSOCKET_OP_PONG) {
            int opcode;
            const uint8_t *data;
            size_t len;
            int fed = websocket_message_feed(&c->assembler, &frame, &opcode,
                                             &data, &len);

            if (fed < 0) {
                client_close(c);
                return;
            }
            if (fed == 1 && opcode == WEBSOCKET_OP_TEXT)
                handle_subscribe_line(c, (const char *)data, len);
        }

        memmove(c->read_buf, c->read_buf + consumed,
               c->read_have - (size_t)consumed);
        c->read_have -= (size_t)consumed;
    }
}

static void handle_readable(struct viewer_client *c) {
    if (c->state == VIEWER_CLIENT_HANDSHAKING) {
        ssize_t n;

        if (c->handshake_have >= sizeof(c->handshake_buf)) {
            client_close(c);
            return;
        }
        n = recv(c->fd, c->handshake_buf + c->handshake_have,
                sizeof(c->handshake_buf) - c->handshake_have, 0);
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                return;
            client_close(c);
            return;
        }
        c->handshake_have += (size_t)n;
        handle_handshake_data(c);
    } else if (c->state == VIEWER_CLIENT_OPEN) {
        ssize_t n;

        if (c->read_have >= sizeof(c->read_buf)) {
            client_close(c); /* a frame too large for this link */
            return;
        }
        n = recv(c->fd, c->read_buf + c->read_have,
                sizeof(c->read_buf) - c->read_have, 0);
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                return;
            client_close(c);
            return;
        }
        c->read_have += (size_t)n;
        handle_open_data(c);
    }
}

/*
 * Linux auto-tunes a socket's send buffer up as it goes, and measured
 * against this link's traffic that reaches multiple megabytes -- a
 * deliberately unread client here still had 4.6 MB of fully-formed
 * messages sitting in the kernel's buffer after four seconds, none of
 * them ever reaching the one-pending-per-stream logic above because the
 * kernel was silently doing the queueing this link exists not to do.
 * ADR-0002's freshness rule is enforced by this module refusing to hold
 * more than one unsent message per stream, and that refusal is only ever
 * consulted once `send()` actually returns EAGAIN -- so the kernel buffer
 * has to be kept small enough that it does, not left at whatever a
 * general-purpose default auto-tunes to. 256 KiB comfortably holds one
 * full round of all three streams' worst case (spectrum's ~131 KB is the
 * largest single message) so a keeping-up client is never fragmented by
 * it, while a client stalled for even a few hundred milliseconds fills it
 * and starts seeing exactly the drop-and-replace behaviour this is for.
 */
#define VIEWER_LINK_CLIENT_SNDBUF (256 * 1024)

static void accept_new(struct viewer_link *link) {
    for (;;) {
        int fd = accept(link->listen_fd, NULL, NULL);
        int i;
        struct viewer_client *slot = NULL;
        int sndbuf = VIEWER_LINK_CLIENT_SNDBUF;

        if (fd < 0)
            return; /* EAGAIN: nothing more pending */
        set_nonblocking(fd);
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
        for (i = 0; i < VIEWER_LINK_MAX_CLIENTS; i++)
            if (link->clients[i].state == VIEWER_CLIENT_CLOSED) {
                slot = &link->clients[i];
                break;
            }
        if (!slot) {
            close(fd); /* no room; a Viewer link this ticket does not size
                          past VIEWER_LINK_MAX_CLIENTS for */
            continue;
        }
        memset(slot, 0, sizeof(*slot));
        slot->fd = fd;
        slot->state = VIEWER_CLIENT_HANDSHAKING;
        slot->inflight_stream = -1;
        debug_log_write("viewer", "client fd %d connected", fd);
    }
}

/*
 * The three streams share one socket, so "flush whatever is pending" has
 * to mean one frame at a time, not one stream at a time. A stream that
 * only partly reached the wire on an earlier call is finished first,
 * and nothing else is attempted until it is -- discovered by the
 * raw-byte diagnostic this ticket asked for, in the --slow scenario
 * that scenario exists to exercise: with the slow client not draining
 * the socket, spectrum and receiver_state (both replaceable while
 * `sent == 0`) kept refreshing every block while a partially-sent
 * waterfall frame sat waiting for room, and the moment the socket had
 * room again the old per-stream loop sent spectrum's *whole* fresh
 * frame before returning to finish waterfall's leftover bytes --
 * splicing a foreign frame into the middle of another one's payload,
 * which is corruption no per-frame length or checksum can express.
 */
static void flush_client(struct viewer_client *c) {
    int stream;

    if (c->inflight_stream >= 0) {
        struct viewer_stream_slot *slot = &c->slot[c->inflight_stream];
        size_t was_pending = slot->length - slot->sent;

        try_flush_slot(c, (enum viewer_stream)c->inflight_stream);
        if (c->state == VIEWER_CLIENT_CLOSED)
            return;
        if (slot->length > slot->sent)
            return; /* still not on the wire; nothing else may go ahead of it */
        /* try_flush_slot() already zeroed length/sent on completion, so
           the remaining count has to be captured before calling it. */
        debug_log_write("viewer", "client fd %d stream %s stall cleared "
                        "(%zu bytes were still pending)",
                        c->fd, stream_names[c->inflight_stream], was_pending);
        c->inflight_stream = -1;
    }
    for (stream = 0; stream < VIEWER_STREAM_COUNT; stream++) {
        struct viewer_stream_slot *slot = &c->slot[stream];

        if (slot->length <= slot->sent)
            continue;
        try_flush_slot(c, (enum viewer_stream)stream);
        if (c->state == VIEWER_CLIENT_CLOSED)
            return;
        if (slot->length > slot->sent) {
            debug_log_write("viewer", "client fd %d stream %s stalled at "
                            "%zu/%zu bytes",
                            c->fd, stream_names[stream], slot->sent,
                            slot->length);
            c->inflight_stream = stream;
            return; /* blocked on this one; the rest wait for next time */
        }
    }
}

void viewer_link_poll(struct viewer_link *link, int timeout_ms) {
    fd_set read_set, write_set;
    int max_fd = link->listen_fd;
    struct timeval tv;
    int i;

    FD_ZERO(&read_set);
    FD_ZERO(&write_set);
    FD_SET(link->listen_fd, &read_set);

    for (i = 0; i < VIEWER_LINK_MAX_CLIENTS; i++) {
        struct viewer_client *c = &link->clients[i];
        int stream;
        int pending = 0;

        if (c->state == VIEWER_CLIENT_CLOSED)
            continue;
        FD_SET(c->fd, &read_set);
        for (stream = 0; stream < VIEWER_STREAM_COUNT; stream++)
            if (c->slot[stream].length > c->slot[stream].sent)
                pending = 1;
        if (pending)
            FD_SET(c->fd, &write_set);
        if (c->fd > max_fd)
            max_fd = c->fd;
    }

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (select(max_fd + 1, &read_set, &write_set, NULL, &tv) < 0)
        return; /* EINTR or similar; the caller's next poll tries again */

    if (FD_ISSET(link->listen_fd, &read_set))
        accept_new(link);

    for (i = 0; i < VIEWER_LINK_MAX_CLIENTS; i++) {
        struct viewer_client *c = &link->clients[i];

        if (c->state == VIEWER_CLIENT_CLOSED)
            continue;
        if (FD_ISSET(c->fd, &read_set))
            handle_readable(c);
        if (c->state == VIEWER_CLIENT_CLOSED)
            continue;
        if (FD_ISSET(c->fd, &write_set))
            flush_client(c);
    }
}

/* Little-endian writers -- see viewer_link.h's header comment. memcpy
   rather than a cast, so this makes no assumption about alignment. */
static uint8_t *put_u16le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    return p + 2;
}

static uint8_t *put_u32le(uint8_t *p, uint32_t v) {
    int i;

    for (i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
    return p + 4;
}

static uint8_t *put_u64le(uint8_t *p, uint64_t v) {
    int i;

    for (i = 0; i < 8; i++)
        p[i] = (uint8_t)(v >> (8 * i));
    return p + 8;
}

static uint8_t *put_floats_le(uint8_t *p, const float *values, int count) {
    int i;

    for (i = 0; i < count; i++) {
        uint32_t bits;

        memcpy(&bits, &values[i], sizeof(bits));
        p = put_u32le(p, bits);
    }
    return p;
}

/*
 * Encodes one binary message (header + payload) and hands it to every
 * client subscribed to `stream`, via the same replace-and-count-dropped
 * rule for each. Shared by the two binary publishers below; the header
 * fields and payload are the only things that differ between them.
 *
 * Queues only -- it does not call try_flush_slot() itself. Every actual
 * send() happens from viewer_link_poll()'s write-ready branch, gated on
 * select() having said this socket can take bytes right now, and
 * ordered by flush_client() so that at most one of a client's three
 * streams is ever mid-frame on the wire at a time (see its own comment).
 *
 * An earlier version also sent optimistically the moment a message was
 * queued here, reasoning that a socket with room is safe to write to
 * immediately without waiting for the next poll cycle. That was removed
 * because it produced framing an RFC 6455 decoder could not parse under
 * sustained backpressure -- but the removal was a correlated fix, not
 * the actual one: it changed the timing enough to make the real bug
 * rarer, not gone, which is why one further raw-byte diagnostic run of
 * the exact --slow scenario still reproduced it (see flush_client()).
 * The lesson is not "avoid a second call site"; it is that this queue
 * never needed one in the first place, since select()'s write-ready
 * event already tells poll() everything it needs to flush on time.
 */
static void publish_binary(struct viewer_link *link, enum viewer_stream stream,
                          enum viewer_message_type type,
                          uint32_t tuning_generation, uint64_t now_ms,
                          uint32_t bins, const float *array_a,
                          const float *array_b) {
    uint8_t app_payload[VIEWER_STREAM_MESSAGE_MAX];
    uint8_t *p = app_payload;
    size_t app_len;
    int i;

    p[0] = VIEWER_LINK_PROTOCOL_VERSION;
    p[1] = (uint8_t)type;
    p = put_u16le(p + 2, 0);
    p = put_u32le(p, tuning_generation);
    p = put_u64le(p, now_ms);
    p = put_u32le(p, bins);
    p = put_floats_le(p, array_a, (int)bins);
    if (array_b)
        p = put_floats_le(p, array_b, (int)bins);
    app_len = (size_t)(p - app_payload);

    for (i = 0; i < VIEWER_LINK_MAX_CLIENTS; i++) {
        struct viewer_client *c = &link->clients[i];
        struct viewer_stream_slot *slot;
        size_t frame_len;

        if (c->state != VIEWER_CLIENT_OPEN || !c->subscribed[stream])
            continue;
        slot = &c->slot[stream];
        if (!slot_ready_for_new_message(slot))
            continue; /* a previous message is still only partly sent */
        frame_len = websocket_frame_encode(slot->data, sizeof(slot->data), 1,
                                          WEBSOCKET_OP_BINARY, app_payload,
                                          app_len);
        if (frame_len == 0)
            continue; /* cannot happen: the slot is sized for the worst
                         case app_payload plus frame overhead */
        slot->length = frame_len;
        slot->sent = 0;
    }
}

void viewer_link_publish_spectrum(struct viewer_link *link,
                                  const struct scope_view_model *svm,
                                  uint64_t now_ms) {
    if (!svm->spectrum_ready)
        return;
    /* svm's own arrays are sized to SDR_DSP_FFT_MAX; a bins value outside
       0..SDR_DSP_FFT_MAX would be a bug upstream of this module, and this
       module is not the place to guess what such a bug meant -- it refuses
       to encode a message from it rather than propagate whatever
       app_payload's fixed-size buffer would otherwise do with it. */
    if (svm->spectrum_bins < 0 || svm->spectrum_bins > SDR_DSP_FFT_MAX)
        return;
    publish_binary(link, VIEWER_STREAM_SPECTRUM, VIEWER_MESSAGE_SPECTRUM,
                   svm->tuning_generation, now_ms,
                   (uint32_t)svm->spectrum_bins, svm->spectrum_average,
                   svm->spectrum_peak);
}

void viewer_link_publish_waterfall_row(struct viewer_link *link,
                                       const struct scope_view_model *svm,
                                       uint64_t now_ms) {
    if (!svm->waterfall_ready || !svm->spectrum_ready)
        return;
    if (svm->spectrum_bins < 0 || svm->spectrum_bins > SDR_DSP_FFT_MAX)
        return;
    publish_binary(link, VIEWER_STREAM_WATERFALL, VIEWER_MESSAGE_WATERFALL_ROW,
                   svm->tuning_generation, now_ms,
                   (uint32_t)svm->spectrum_bins, svm->waterfall_row, NULL);
}

void viewer_link_publish_receiver_state(struct viewer_link *link,
                                        const struct scope_view_model *svm,
                                        uint64_t now_ms) {
    char json[256];
    int json_len;
    int i;

    json_len = snprintf(json, sizeof(json),
                        "{\"type\":\"receiver_state\",\"center_hz\":%u,"
                        "\"sample_rate_hz\":%u,\"ppm\":%d,"
                        "\"tuning_generation\":%u,\"full_scale\":%g,"
                        "\"timestamp_ms\":%llu}",
                        svm->center_hz, svm->sample_rate_hz, svm->ppm,
                        svm->tuning_generation, (double)svm->full_scale,
                        (unsigned long long)now_ms);
    if (json_len <= 0)
        return;

    /* Queues only -- see publish_binary()'s comment; the same reasoning
       and the same fix apply here. */
    for (i = 0; i < VIEWER_LINK_MAX_CLIENTS; i++) {
        struct viewer_client *c = &link->clients[i];
        struct viewer_stream_slot *slot;
        size_t frame_len;

        if (c->state != VIEWER_CLIENT_OPEN ||
            !c->subscribed[VIEWER_STREAM_RECEIVER_STATE])
            continue;
        slot = &c->slot[VIEWER_STREAM_RECEIVER_STATE];
        if (!slot_ready_for_new_message(slot))
            continue;
        frame_len = websocket_frame_encode(slot->data, sizeof(slot->data), 1,
                                          WEBSOCKET_OP_TEXT,
                                          (const uint8_t *)json,
                                          (size_t)json_len);
        if (frame_len == 0)
            continue;
        slot->length = frame_len;
        slot->sent = 0;
    }
}

/*
 * Unlike the three streams above, this is not one payload fanned out to
 * every subscriber: each client's own sent/dropped/high-water counts are
 * its own, so the JSON is built once per subscribed client rather than
 * once per publish. `server_cpu_percent` is the one field every client
 * shares, computed by the caller (viewer_session.c, via process_cpu.h) --
 * this module reads no clock and touches no process accounting of its
 * own, the same way it touches no raylib.
 */
void viewer_link_publish_link_health(struct viewer_link *link,
                                     double server_cpu_percent,
                                     uint64_t now_ms) {
    int i;

    for (i = 0; i < VIEWER_LINK_MAX_CLIENTS; i++) {
        struct viewer_client *c = &link->clients[i];
        struct viewer_stream_slot *slot;
        char json[320];
        int json_len;
        size_t frame_len;

        if (c->state != VIEWER_CLIENT_OPEN ||
            !c->subscribed[VIEWER_STREAM_LINK_HEALTH])
            continue;
        json_len = snprintf(json, sizeof(json),
                            "{\"type\":\"link_health\","
                            "\"timestamp_ms\":%llu,"
                            "\"server_cpu_percent\":%.2f,"
                            "\"spectrum_sent\":%llu,\"spectrum_dropped\":%llu,"
                            "\"waterfall_sent\":%llu,\"waterfall_dropped\":%llu,"
                            "\"receiver_state_sent\":%llu,"
                            "\"receiver_state_dropped\":%llu,"
                            "\"send_queue_high_water\":%d}",
                            (unsigned long long)now_ms, server_cpu_percent,
                            (unsigned long long)
                                c->slot[VIEWER_STREAM_SPECTRUM].sent_count,
                            (unsigned long long)
                                c->slot[VIEWER_STREAM_SPECTRUM].dropped_count,
                            (unsigned long long)
                                c->slot[VIEWER_STREAM_WATERFALL].sent_count,
                            (unsigned long long)
                                c->slot[VIEWER_STREAM_WATERFALL].dropped_count,
                            (unsigned long long)
                                c->slot[VIEWER_STREAM_RECEIVER_STATE].sent_count,
                            (unsigned long long)
                                c->slot[VIEWER_STREAM_RECEIVER_STATE].dropped_count,
                            c->send_queue_high_water);
        if (json_len <= 0)
            continue;
        slot = &c->slot[VIEWER_STREAM_LINK_HEALTH];
        if (!slot_ready_for_new_message(slot))
            continue;
        frame_len = websocket_frame_encode(slot->data, sizeof(slot->data), 1,
                                          WEBSOCKET_OP_TEXT,
                                          (const uint8_t *)json,
                                          (size_t)json_len);
        if (frame_len == 0)
            continue;
        slot->length = frame_len;
        slot->sent = 0;
    }
}
