#define _POSIX_C_SOURCE 200809L

#include "server/viewer_link.h"

#include "runtime/debug_log.h"

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

int viewer_link_open(struct viewer_link *link, uint16_t port,
                     uint32_t bind_addr, const char *required_token) {
    struct sockaddr_in addr;
    int one = 1;
    int i;

    memset(link, 0, sizeof(*link));
    for (i = 0; i < VIEWER_LINK_MAX_CLIENTS; i++) {
        link->clients[i].state = VIEWER_CLIENT_CLOSED;
        link->clients[i].fd = -1;
    }
    link->required_token = required_token;

    link->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (link->listen_fd < 0) {
        perror("viewer_link: socket");
        return -1;
    }
    setsockopt(link->listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    /* ADR-0027's amendment: the caller decides now, and options.c is the
       one place that decides it may only be something other than
       INADDR_LOOPBACK when `required_token` is also set. */
    addr.sin_addr.s_addr = htonl(bind_addr);
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

/*
 * Every stream's name, in `enum viewer_stream` order and sized by
 * `VIEWER_STREAM_COUNT` -- so a stream added to the enum without a name here
 * is a missing-initializer, not a silent empty string.
 *
 * This is also what `handle_subscribe_line()` matches against, which it did
 * not used to: that parser was one hand-written `else if` per name, and
 * ticket 07 found it two names short -- a client subscribing to
 * `survey_spectrum` received nothing, with no refusal and no error. Two
 * lists of the same names is one list and one place to forget.
 */
static const char *const stream_names[VIEWER_STREAM_COUNT] = {
    "spectrum", "waterfall", "receiver_state", "link_health", "command_result",
    "survey_spectrum", "survey_state", "fm_spectrum", "fm_state",
    "gsm_state"
};

/*
 * Decimal SI, matching AGENTS.md's units convention: KB at 1000 bytes,
 * MB at 1e6, never KiB/MiB and never a silent /1024 wearing a "KB" label.
 * A fixed static buffer rather than an allocation -- this is a diagnostic
 * line, one call site, printed and used immediately.
 */
static const char *format_bytes_decimal(double bytes) {
    static char buf[32];

    if (bytes >= 1e6)
        snprintf(buf, sizeof(buf), "%.2f MB", bytes / 1e6);
    else
        snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1e3);
    return buf;
}

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
                    "high-water %s\n",
            fd, format_bytes_decimal(c->send_queue_high_water));
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

void viewer_link_set_command_handler(struct viewer_link *link,
                                     viewer_command_handler handler,
                                     void *ctx) {
    link->command_handler = handler;
    link->command_handler_ctx = ctx;
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
 * disposition kills the whole process -- so an operator's server/web session
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

/*
 * Whether `req`'s path carries `?token=<link->required_token>` -- an
 * exact byte match, nothing URL-decoded, on the same principle
 * websocket.h's own header comment states about itself: this is not a
 * URL parser and must not become one. A token meant to survive this
 * check has to be safe unescaped in a URL (letters, digits, `-`, `_`);
 * `viewer_session.c` is the one place that generates one, and does so
 * accordingly.
 *
 * Always true when `link->required_token` is NULL -- the ADR-0027
 * default, where the bind address is still the whole boundary and
 * nothing here has anything to check.
 */
static int token_authorized(const struct viewer_link *link,
                            const struct websocket_request *req) {
    size_t token_len;
    const char *q;
    size_t remaining;

    if (!link->required_token)
        return 1;
    token_len = strlen(link->required_token);
    q = memchr(req->path, '?', req->path_len);
    if (!q)
        return 0;
    remaining = (size_t)(req->path + req->path_len - q - 1);
    q++;
    while (remaining > 0) {
        const char *amp = memchr(q, '&', remaining);
        size_t pair_len = amp ? (size_t)(amp - q) : remaining;

        if (pair_len > 6 && memcmp(q, "token=", 6) == 0 &&
            pair_len - 6 == token_len &&
            memcmp(q + 6, link->required_token, token_len) == 0)
            return 1;
        if (!amp)
            break;
        remaining -= pair_len + 1;
        q = amp + 1;
    }
    return 0;
}

/* The one response a request without a valid token gets, upgrade or
   plain page alike -- refused before either happens, not served a page
   whose own WebSocket connection would then fail anyway. */
static void refuse_unauthorized(struct viewer_client *c) {
    static const char body[] = "401 Unauthorized: missing or wrong ?token=\n";
    char header[160];
    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 401 Unauthorized\r\n"
                              "Content-Type: text/plain; charset=utf-8\r\n"
                              "Content-Length: %zu\r\n"
                              "Connection: close\r\n\r\n",
                              sizeof(body) - 1);

    if (send_best_effort(c->fd, header, (size_t)header_len) == 0)
        send_best_effort(c->fd, body, sizeof(body) - 1);
    client_close(c);
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

static void handle_handshake_data(struct viewer_link *link,
                                  struct viewer_client *c) {
    struct websocket_request req;
    int consumed = websocket_request_parse(c->handshake_buf, c->handshake_have,
                                           &req);

    if (consumed == 0)
        return; /* not a complete header block yet */
    if (consumed < 0 || c->handshake_have >= sizeof(c->handshake_buf)) {
        client_close(c);
        return;
    }
    /* Checked before either branch below: a request refused here never
       gets the upgrade response or the page, rather than being served a
       page whose own WebSocket open would fail moments later. */
    if (!token_authorized(link, &req)) {
        refuse_unauthorized(c);
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
            int s;

            /*
             * Against `stream_names[]` itself rather than a hand-written
             * branch per name. `command_result` is in that table and is
             * matched here like any other: a client may not usefully
             * unsubscribe from it (results are pushed whether asked for or
             * not, ticket 06), but refusing the *name* would be a second
             * rule nobody stated, and accepting it costs nothing.
             */
            for (s = 0; s < VIEWER_STREAM_COUNT; s++)
                if (strlen(stream_names[s]) == tok_len &&
                    memcmp(line + start, stream_names[s], tok_len) == 0) {
                    wanted[s] = 1;
                    break;
                }
        }
    }
    memcpy(c->subscribed, wanted, sizeof(wanted));
    if (debug_log_active()) {
        /* Every stream name, space-separated, plus the terminator -- 90
           bytes for all seven today. Sized generously rather than exactly:
           this was 64 and silently truncated mid-word the moment ticket
           07 added a sixth and seventh name, found live rather than
           read -- a log line truncated is a log line lying about what a
           client asked for, which is what this line exists to answer. */
        char summary[160] = "";
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

static int starts_with_word(const char *data, size_t len, const char *word) {
    size_t word_len = strlen(word);

    if (len < word_len)
        return 0;
    if (memcmp(data, word, word_len) != 0)
        return 0;
    return len == word_len || data[word_len] == ' ';
}

/*
 * A small JSON string escaper, scoped to this file rather than reusing
 * `survey_json_escape()` (src/runtime/survey_store.c) -- that would pull in the
 * whole survey/installation header graph for one function, exactly the
 * coupling this module goes out of its way to avoid (no app.h, no
 * raylib). Truncates on overflow rather than refusing outright: a
 * command result is diagnostic text for a person, not data anything
 * parses back, so a clipped echo is a smaller loss than no result.
 */
static void json_escape_into(char *out, size_t out_cap, const char *in, size_t in_len) {
    size_t used = 0;
    size_t i;

    if (out_cap == 0)
        return;
    for (i = 0; i < in_len && used + 1 < out_cap; i++) {
        unsigned char ch = (unsigned char)in[i];
        const char *replacement = NULL;

        switch (ch) {
        case '"':  replacement = "\\\""; break;
        case '\\': replacement = "\\\\"; break;
        case '\n': replacement = "\\n"; break;
        case '\r': replacement = "\\r"; break;
        case '\t': replacement = "\\t"; break;
        default:
            if (ch < 0x20)
                replacement = "\\ufffd";
            break;
        }
        if (replacement) {
            size_t rlen = strlen(replacement);

            if (used + rlen >= out_cap)
                break;
            memcpy(out + used, replacement, rlen);
            used += rlen;
        } else {
            out[used++] = (char)ch;
        }
    }
    out[used] = '\0';
}

static size_t build_command_result_json(char *out, size_t out_cap,
                                        const char *command, size_t command_len,
                                        int ok, const char *error) {
    char command_escaped[96];
    char error_json[176];
    int written;

    json_escape_into(command_escaped, sizeof(command_escaped), command, command_len);
    if (error) {
        char error_escaped[160];

        json_escape_into(error_escaped, sizeof(error_escaped), error, strlen(error));
        snprintf(error_json, sizeof(error_json), "\"%s\"", error_escaped);
    } else {
        snprintf(error_json, sizeof(error_json), "null");
    }
    written = snprintf(out, out_cap,
                       "{\"type\":\"command_result\",\"command\":\"%s\","
                       "\"ok\":%s,\"error\":%s}",
                       command_escaped, ok ? "true" : "false", error_json);
    return written > 0 ? (size_t)written : 0;
}

/*
 * If the command-result slot is free and something is queued, this is
 * the only place a queued result is ever loaded into it -- called right
 * after enqueuing (the common case: nothing else pending) and once more
 * at the end of flush_client() (the case that matters: an earlier result
 * was still in flight when this one queued behind it, and has just
 * finished). Unlike every other stream's slot, this one is never
 * replaced by a fresher message -- only ever advanced to the next one in
 * line, which is the whole of what "reliable and ordered" means here.
 */
static void load_command_result_into_slot(struct viewer_client *c) {
    struct viewer_stream_slot *slot = &c->slot[VIEWER_STREAM_COMMAND_RESULT];
    size_t frame_len;
    int idx;

    if (slot->length > 0 || c->result_queue_count == 0)
        return;
    idx = c->result_queue_head;
    frame_len = websocket_frame_encode(slot->data, sizeof(slot->data), 1,
                                       WEBSOCKET_OP_TEXT,
                                       (const uint8_t *)c->result_queue[idx],
                                       (size_t)c->result_queue_len[idx]);
    c->result_queue_head = (idx + 1) % VIEWER_COMMAND_RESULT_QUEUE_DEPTH;
    c->result_queue_count--;
    if (frame_len == 0)
        return; /* cannot happen: the slot is sized for the worst case */
    slot->length = frame_len;
    slot->sent = 0;
}

static void enqueue_command_result(struct viewer_client *c, const char *json,
                                   size_t json_len) {
    int idx;

    if (json_len >= VIEWER_COMMAND_RESULT_JSON_MAX)
        json_len = VIEWER_COMMAND_RESULT_JSON_MAX - 1;
    if (c->result_queue_count >= VIEWER_COMMAND_RESULT_QUEUE_DEPTH) {
        /* The documented bound (viewer_link.h): far past any human-paced
           command stream this ticket is for. Refusing to enqueue a 17th
           pending result is the honest choice over silently discarding
           one to make room for it. */
        return;
    }
    idx = (c->result_queue_head + c->result_queue_count) %
        VIEWER_COMMAND_RESULT_QUEUE_DEPTH;
    memcpy(c->result_queue[idx], json, json_len);
    c->result_queue[idx][json_len] = '\0';
    c->result_queue_len[idx] = (int)json_len;
    c->result_queue_count++;
    load_command_result_into_slot(c);
}

/*
 * The inbound half of ticket 06: a text line that is not a subscription
 * is a command. `viewer_command.h` owns what it says; this decides what
 * happens to the answer -- parsed but refused (a malformed line, quoting
 * the parser's own reason), parsed and executed (via whatever
 * `viewer_link_set_command_handler()` wired in -- `retune_receiver()`,
 * in production, through `viewer_session.c`), or parsed with nowhere to
 * send it (no handler set at all, which a unit check can exercise
 * without a receiver).
 */
static void dispatch_command_line(struct viewer_link *link, struct viewer_client *c,
                                  const char *line, size_t len) {
    struct viewer_command cmd;
    char error[160];
    char json[VIEWER_COMMAND_RESULT_JSON_MAX];
    size_t json_len;
    int ok;

    if (viewer_command_parse(line, len, &cmd, error, sizeof(error)) < 0) {
        json_len = build_command_result_json(json, sizeof(json), line, len, 0,
                                             error);
        enqueue_command_result(c, json, json_len);
        return;
    }
    if (link->command_handler) {
        ok = link->command_handler(link->command_handler_ctx, &cmd, error,
                                   sizeof(error)) == 0;
    } else {
        snprintf(error, sizeof(error), "no receiver attached to this link");
        ok = 0;
    }
    json_len = build_command_result_json(json, sizeof(json), line, len, ok,
                                         ok ? NULL : error);
    enqueue_command_result(c, json, json_len);
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

static void handle_open_data(struct viewer_link *link, struct viewer_client *c) {
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
            if (fed == 1 && opcode == WEBSOCKET_OP_TEXT) {
                if (starts_with_word((const char *)data, len, "subscribe"))
                    handle_subscribe_line(c, (const char *)data, len);
                else
                    dispatch_command_line(link, c, (const char *)data, len);
            }
        }

        memmove(c->read_buf, c->read_buf + consumed,
               c->read_have - (size_t)consumed);
        c->read_have -= (size_t)consumed;
    }
}

static void handle_readable(struct viewer_link *link, struct viewer_client *c) {
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
        handle_handshake_data(link, c);
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
        handle_open_data(link, c);
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
        int cleared_stream = c->inflight_stream;

        try_flush_slot(c, (enum viewer_stream)c->inflight_stream);
        if (c->state == VIEWER_CLIENT_CLOSED)
            return;
        if (slot->length > slot->sent)
            return; /* still not on the wire; nothing else may go ahead of it */
        /* try_flush_slot() already zeroed length/sent on completion, so
           the remaining count has to be captured before calling it. */
        debug_log_write("viewer", "client fd %d stream %s stall cleared "
                        "(%zu bytes were still pending)",
                        c->fd, stream_names[cleared_stream], was_pending);
        c->inflight_stream = -1;
        /* Ticket 06: the one stream with a queue behind its slot rather
           than a fresher message replacing it -- if another result was
           waiting, this is what loads it in to go out next. */
        if (cleared_stream == VIEWER_STREAM_COMMAND_RESULT)
            load_command_result_into_slot(c);
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
        if (stream == VIEWER_STREAM_COMMAND_RESULT)
            load_command_result_into_slot(c);
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
            handle_readable(link, c);
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
                   svm->receiver.tuning_generation, now_ms,
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
                   svm->receiver.tuning_generation, now_ms,
                   (uint32_t)svm->spectrum_bins, svm->waterfall_row, NULL);
}

/*
 * One float array whose frequencies are its own, not the receiver's: the
 * same binary framing every other stream uses, with the range the array
 * spans spliced into the header (`VIEWER_RANGE_HEADER_BYTES`). A bin index
 * means nothing for these without `lower_hz`/`upper_hz` beside it -- the
 * survey's spectrum sits wherever the sweep walked, and the FM multiplex
 * sits at baseband.
 *
 * A separate function from `publish_binary()` rather than a wider, optional
 * header on it: a parameter every Scope caller passes zero for is a question
 * the reader of `viewer_link_publish_spectrum()` should not have to answer.
 * It was written for the survey alone and *was* the survey's own publisher
 * until FM needed the identical forty lines; what made it worth generalising
 * is a second caller, not a second possibility.
 */
static void publish_range_binary(struct viewer_link *link,
                                 enum viewer_stream stream,
                                 enum viewer_message_type type,
                                 uint32_t tuning_generation, uint64_t now_ms,
                                 uint32_t bins, double lower_hz,
                                 double upper_hz, const float *array) {
    /* Sized for the larger of the two range streams, so one buffer serves
       both -- the survey's 8192 bins against FM's 1024. */
    uint8_t app_payload[VIEWER_SURVEY_MESSAGE_MAX];
    uint8_t *p = app_payload;
    size_t app_len;
    int i;

    p[0] = VIEWER_LINK_PROTOCOL_VERSION;
    p[1] = (uint8_t)type;
    p = put_u16le(p + 2, 0);
    p = put_u32le(p, tuning_generation);
    p = put_u64le(p, now_ms);
    p = put_u32le(p, bins);
    p = put_u32le(p, (uint32_t)lower_hz);
    p = put_u32le(p, (uint32_t)upper_hz);
    p = put_floats_le(p, array, (int)bins);
    app_len = (size_t)(p - app_payload);

    for (i = 0; i < VIEWER_LINK_MAX_CLIENTS; i++) {
        struct viewer_client *c = &link->clients[i];
        struct viewer_stream_slot *slot;
        size_t frame_len;

        if (c->state != VIEWER_CLIENT_OPEN || !c->subscribed[stream])
            continue;
        slot = &c->slot[stream];
        if (!slot_ready_for_new_message(slot))
            continue;
        frame_len = websocket_frame_encode(slot->data, sizeof(slot->data), 1,
                                          WEBSOCKET_OP_BINARY, app_payload,
                                          app_len);
        if (frame_len == 0)
            continue;
        slot->length = frame_len;
        slot->sent = 0;
    }
}

/* Ticket 07's survey chart, over whatever range the sweep walked. */
void viewer_link_publish_survey_spectrum(struct viewer_link *link,
                                         const struct survey_view_model *svm,
                                         uint32_t tuning_generation,
                                         uint64_t now_ms) {
    if (svm->bins <= 0 || svm->bins > SURVEY_VIEW_MODEL_MAX_BINS)
        return;
    publish_range_binary(link, VIEWER_STREAM_SURVEY_SPECTRUM,
                         VIEWER_MESSAGE_SURVEY_SPECTRUM, tuning_generation,
                         now_ms, (uint32_t)svm->bins, svm->lower_hz,
                         svm->upper_hz, svm->power);
}

/*
 * The FM multiplex, at baseband: 0 Hz to `bins * bin_hz`, about 60 kHz,
 * which is where the pilot at 19, the stereo subcarrier at 38 and the RDS
 * band at 57 are what a reader is looking for. The range is computed here
 * rather than carried in the view model because it is not a measurement --
 * it is what the bin count and the bin width already say.
 */
void viewer_link_publish_fm_spectrum(struct viewer_link *link,
                                     const struct fm_view_model *fvm,
                                     uint32_t tuning_generation,
                                     uint64_t now_ms) {
    if (fvm->spectrum_bins <= 0 ||
        fvm->spectrum_bins > FM_VIEW_MODEL_MAX_BINS)
        return;
    publish_range_binary(link, VIEWER_STREAM_FM_SPECTRUM,
                         VIEWER_MESSAGE_FM_SPECTRUM, tuning_generation,
                         now_ms, (uint32_t)fvm->spectrum_bins, 0.0,
                         (double)fvm->spectrum_bins * fvm->spectrum_bin_hz,
                         fvm->spectrum);
}

/*
 * Ticket 07's sweep status, alongside the chart above: what the window's
 * own status line would say, whether a sweep is walking the range, and the
 * candidate list -- each candidate's mark named the way `sdrgui.h` already
 * names the four the chart draws (`survey_mark_of()`), so a
 * browser reads the same verdict the window's marks encode rather than
 * reinterpreting the flag word itself.
 *
 * Built once and fanned out, like `receiver_state`: nothing here is
 * per-connection.
 */
void viewer_link_publish_survey_state(struct viewer_link *link,
                                      const struct survey_view_model *svm,
                                      uint64_t now_ms) {
    /* One candidate's JSON is at most about 110 bytes with a 200-byte
       margin for the escaped status string and the wrapper; SURVEY_MAX_PEAKS
       (512) of them comfortably inside 64 KiB. */
    static char json[65536];
    size_t len = 0;
    char status_escaped[400];
    int i;
    int n = svm->candidate_count;

    if (n < 0)
        n = 0;
    if (n > SURVEY_MAX_PEAKS)
        n = SURVEY_MAX_PEAKS;

    json_escape_into(status_escaped, sizeof(status_escaped), svm->status,
                    strlen(svm->status));

    len += (size_t)snprintf(json + len, sizeof(json) - len,
                            "{\"type\":\"survey_state\","
                            "\"timestamp_ms\":%llu,"
                            "\"sweeping\":%s,"
                            "\"status\":\"%s\","
                            "\"lower_hz\":%.0f,\"upper_hz\":%.0f,"
                            "\"candidate_count\":%d,\"candidates\":[",
                            (unsigned long long)now_ms,
                            svm->sweeping ? "true" : "false",
                            status_escaped, svm->lower_hz, svm->upper_hz, n);
    for (i = 0; i < n && len < sizeof(json) - 200; i++) {
        const struct survey_candidate_view *cnd = &svm->candidates[i];

        len += (size_t)snprintf(json + len, sizeof(json) - len,
                                "%s{\"hz\":%.0f,\"power_dbfs\":%.1f,"
                                "\"has_carrier\":%s,\"width_hz\":%.0f,"
                                "\"shape\":\"%s\",\"seen\":\"%s\","
                                "\"mark\":\"%s\"}",
                                i == 0 ? "" : ",", cnd->hz,
                                (double)cnd->power_dbfs,
                                cnd->has_carrier ? "true" : "false",
                                cnd->has_carrier ? cnd->width_hz : 0.0,
                                cnd->has_carrier
                                    ? survey_shape_name(cnd->shape) : "-",
                                site_seen_name(cnd->seen),
                                survey_mark_name(cnd->mark));
    }
    if (len < sizeof(json) - 2) {
        json[len++] = ']';
        json[len++] = '}';
    }

    for (i = 0; i < VIEWER_LINK_MAX_CLIENTS; i++) {
        struct viewer_client *c = &link->clients[i];
        struct viewer_stream_slot *slot;
        size_t frame_len;

        if (c->state != VIEWER_CLIENT_OPEN ||
            !c->subscribed[VIEWER_STREAM_SURVEY_STATE])
            continue;
        slot = &c->slot[VIEWER_STREAM_SURVEY_STATE];
        if (!slot_ready_for_new_message(slot))
            continue;
        frame_len = websocket_frame_encode(slot->data, sizeof(slot->data), 1,
                                          WEBSOCKET_OP_TEXT,
                                          (const uint8_t *)json, len);
        if (frame_len == 0)
            continue;
        slot->length = frame_len;
        slot->sent = 0;
    }
}

/*
 * The FM view's three panels, as one JSON message.
 *
 * `reading` and `reading_tone` arrive already decided (`fm_view_model.h`):
 * which of five sentences the funnel's counts amount to, and whether it
 * reads as working, as in progress, or as where the decode stopped. A
 * browser re-deriving that from the five counts beside it is the second
 * presentation this whole seam exists to prevent -- the same reason a survey
 * candidate's `mark` travels rather than its flag word.
 *
 * Built once and fanned out, like `receiver_state` and `survey_state`.
 */
void viewer_link_publish_fm_state(struct viewer_link *link,
                                  const struct fm_view_model *fvm,
                                  uint64_t now_ms) {
    /* Radio text is the long field at 64 characters, and every character of
       it can escape to six (\u001f); the fixed fields and the wrapper are a
       few hundred more. 2 KiB is generous against that worst case. */
    char json[2048];
    char ps[sizeof(fvm->ps) * 6 + 1];
    char rt[sizeof(fvm->rt) * 6 + 1];
    char reading[sizeof(fvm->reading) * 6 + 1];
    char pty_name[sizeof(fvm->pty_name) * 6 + 1];
    char traffic[sizeof(fvm->traffic) * 6 + 1];
    char audio_error[sizeof(fvm->audio_error) * 6 + 1];
    int json_len;
    int i;

    json_escape_into(ps, sizeof(ps), fvm->ps, strlen(fvm->ps));
    json_escape_into(rt, sizeof(rt), fvm->rt, strlen(fvm->rt));
    json_escape_into(reading, sizeof(reading), fvm->reading,
                     strlen(fvm->reading));
    json_escape_into(pty_name, sizeof(pty_name), fvm->pty_name,
                     strlen(fvm->pty_name));
    json_escape_into(traffic, sizeof(traffic), fvm->traffic,
                     strlen(fvm->traffic));
    json_escape_into(audio_error, sizeof(audio_error), fvm->audio_error,
                     strlen(fvm->audio_error));

    json_len = snprintf(json, sizeof(json),
                        "{\"type\":\"fm_state\",\"timestamp_ms\":%llu,"
                        "\"pilot_locked\":%s,\"pilot_hz\":%.2f,"
                        "\"pilot_ppm\":%.1f,\"pilot_coherence\":%.3f,"
                        "\"broadcast_stereo\":%s,"
                        "\"playing\":%s,\"audio_rate_hz\":%.0f,"
                        "\"audio_error\":\"%s\","
                        "\"timing_offset\":%d,"
                        "\"timing_samples_per_symbol\":%d,"
                        "\"axis_radians\":%.2f,"
                        "\"pi_valid\":%s,\"pi\":%u,\"pi_repeats\":%d,"
                        "\"ps_valid\":%s,\"ps\":\"%s\",\"ps_segments\":%d,"
                        "\"pty_valid\":%s,\"pty\":%d,\"pty_name\":\"%s\","
                        "\"tp\":%s,\"ta\":%s,\"traffic\":\"%s\","
                        "\"rt_valid\":%s,\"rt\":\"%s\","
                        "\"bits\":%ld,\"blocks_matched\":%ld,\"groups\":%ld,"
                        "\"identified\":%ld,\"named\":%ld,"
                        "\"reading\":\"%s\",\"reading_tone\":\"%s\"}",
                        (unsigned long long)now_ms,
                        fvm->pilot_locked ? "true" : "false", fvm->pilot_hz,
                        fvm->pilot_ppm, fvm->pilot_coherence,
                        fvm->broadcast_stereo ? "true" : "false",
                        fvm->playing ? "true" : "false", fvm->audio_rate_hz,
                        audio_error, fvm->timing_offset,
                        fvm->timing_samples_per_symbol, fvm->axis_radians,
                        fvm->pi_valid ? "true" : "false", fvm->pi,
                        fvm->pi_repeats,
                        fvm->ps_valid ? "true" : "false", ps,
                        fvm->ps_segments,
                        fvm->pty_valid ? "true" : "false", fvm->pty, pty_name,
                        fvm->tp ? "true" : "false",
                        fvm->ta ? "true" : "false", traffic,
                        fvm->rt_valid ? "true" : "false", rt,
                        fvm->bits, fvm->blocks_matched, fvm->groups,
                        fvm->identified, fvm->named, reading,
                        fm_reading_tone_name(fvm->reading_tone));
    if (json_len <= 0 || (size_t)json_len >= sizeof(json))
        return; /* truncated: a half-written object is not JSON */

    for (i = 0; i < VIEWER_LINK_MAX_CLIENTS; i++) {
        struct viewer_client *c = &link->clients[i];
        struct viewer_stream_slot *slot;
        size_t frame_len;

        if (c->state != VIEWER_CLIENT_OPEN ||
            !c->subscribed[VIEWER_STREAM_FM_STATE])
            continue;
        slot = &c->slot[VIEWER_STREAM_FM_STATE];
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
 * The GSM screen. One object, fanned out, like every other JSON stream.
 *
 * Both readouts travel as a **name** plus the fields that sentence carries:
 * `sch` is one of idle/searching/recording/decoded and `bcch` one of
 * none/waiting/missed/read, decided in `gsm_view_model.c` where a check can
 * reach it. Never as the enum's integer -- that is what drew the survey's
 * marks swapped for months (`web-visualization/15`).
 *
 * The 124-channel scan is the long part: 125 powers and 125 confidences at
 * up to seven characters each, so the buffer is sized for that rather than
 * for the readouts.
 */
void viewer_link_publish_gsm_state(struct viewer_link *link,
                                   const struct gsm_view_model *gvm,
                                   uint64_t now_ms) {
    char json[6144];
    int json_len, used, i;

    used = snprintf(json, sizeof(json),
                    "{\"type\":\"gsm_state\",\"timestamp_ms\":%llu,"
                    "\"arfcn\":%d,\"carrier_hz\":%.0f,"
                    "\"sch\":\"%s\",\"bsic\":%d,\"ncc\":%d,\"bcc\":%d,"
                    "\"frame_number\":%d,\"t1\":%d,\"t2\":%d,\"t3\":%d,"
                    "\"confidence\":%.2f,\"implausible\":%s,"
                    "\"bcch\":\"%s\",\"blocks\":%d,"
                    "\"have_lai\":%s,\"mcc\":%d,\"mnc\":%d,"
                    "\"mnc_digits\":%d,\"lac\":%d,"
                    "\"have_cell_id\":%s,\"cell_id\":%d,"
                    "\"stats_ready\":%s,\"noise\":%.2f,\"signal\":%.2f,"
                    "\"snr_db\":%.1f,\"clipping_percent\":%.4f,"
                    "\"headroom_db\":%.1f,"
                    "\"scanning\":%s,\"step\":%d,\"step_count\":%d,"
                    "\"have_scan\":%s",
                    (unsigned long long)now_ms,
                    gvm->selected_arfcn, gvm->selected_hz,
                    gsm_sch_reading_name(gvm->sch),
                    gvm->bsic, gvm->ncc, gvm->bcc, gvm->frame_number,
                    gvm->t1, gvm->t2, gvm->t3, (double)gvm->confidence,
                    gvm->implausible ? "true" : "false",
                    gsm_bcch_reading_name(gvm->bcch), gvm->blocks,
                    gvm->have_lai ? "true" : "false",
                    gvm->mcc, gvm->mnc, gvm->mnc_digits, gvm->lac,
                    gvm->have_cell_id ? "true" : "false", gvm->cell_id,
                    gvm->signal_stats_ready ? "true" : "false",
                    (double)gvm->signal_stats.noise_magnitude,
                    (double)gvm->signal_stats.signal_magnitude,
                    (double)gvm->signal_stats.snr_db,
                    (double)gvm->signal_stats.clipping_percent,
                    (double)gvm->signal_stats.headroom_db,
                    gvm->scanning ? "true" : "false",
                    gvm->step, gvm->step_count,
                    gvm->have_scan ? "true" : "false");
    if (used <= 0 || (size_t)used >= sizeof(json))
        return;

    used += snprintf(json + used, sizeof(json) - (size_t)used,
                     ",\"neighbours\":[");
    for (i = 0; i < gvm->neighbour_count && used < (int)sizeof(json) - 8; i++)
        used += snprintf(json + used, sizeof(json) - (size_t)used, "%s%d",
                         i ? "," : "", gvm->neighbours[i]);
    used += snprintf(json + used, sizeof(json) - (size_t)used, "]");

    /*
     * The band. Both arrays are sent whole rather than as the visited
     * subset: which channels were *not* looked at is the distinction
     * `have_scan` exists for, and a sparse object would make the browser
     * re-derive it.
     */
    used += snprintf(json + used, sizeof(json) - (size_t)used, ",\"power\":[");
    for (i = 0; i < GSM_VIEW_MODEL_CHANNELS && used < (int)sizeof(json) - 16;
         i++)
        used += snprintf(json + used, sizeof(json) - (size_t)used, "%s%.1f",
                         i ? "," : "", (double)gvm->power[i]);
    used += snprintf(json + used, sizeof(json) - (size_t)used,
                     "],\"bcch_confidence\":[");
    for (i = 0; i < GSM_VIEW_MODEL_CHANNELS && used < (int)sizeof(json) - 16;
         i++)
        used += snprintf(json + used, sizeof(json) - (size_t)used, "%s%.2f",
                         i ? "," : "", (double)gvm->bcch_confidence[i]);
    used += snprintf(json + used, sizeof(json) - (size_t)used, "]}");

    json_len = used;
    if (json_len <= 0 || (size_t)json_len >= sizeof(json))
        return; /* truncated: a half-written object is not JSON */

    for (i = 0; i < VIEWER_LINK_MAX_CLIENTS; i++) {
        struct viewer_client *c = &link->clients[i];
        struct viewer_stream_slot *slot;
        size_t frame_len;

        if (c->state != VIEWER_CLIENT_OPEN ||
            !c->subscribed[VIEWER_STREAM_GSM_STATE])
            continue;
        slot = &c->slot[VIEWER_STREAM_GSM_STATE];
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

void viewer_link_publish_receiver_state(struct viewer_link *link,
                                        const struct receiver_view_model *rvm,
                                        uint64_t now_ms) {
    char json[256];
    int json_len;
    int i;

    json_len = snprintf(json, sizeof(json),
                        "{\"type\":\"receiver_state\",\"screen\":\"%s\","
                        "\"center_hz\":%u,"
                        "\"sample_rate_hz\":%u,\"ppm\":%d,"
                        "\"tuning_generation\":%u,\"full_scale\":%g,"
                        "\"timestamp_ms\":%llu}",
                        rvm->screen, rvm->center_hz,
                        rvm->sample_rate_hz,
                        rvm->ppm, rvm->tuning_generation,
                        (double)rvm->full_scale,
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
        /*
         * Nine streams at up to 85 bytes each -- a name, two 20-digit
         * counts and their punctuation -- plus the wrapper, so 885 bytes
         * at the arithmetic worst case and nothing like it in practice.
         */
        char json[1280];
        int json_len = 0;
        size_t frame_len;
        int s;

        if (c->state != VIEWER_CLIENT_OPEN ||
            !c->subscribed[VIEWER_STREAM_LINK_HEALTH])
            continue;
        json_len = snprintf(json, sizeof(json),
                            "{\"type\":\"link_health\","
                            "\"timestamp_ms\":%llu,"
                            "\"server_cpu_percent\":%.2f,"
                            "\"send_queue_high_water\":%d,"
                            "\"streams\":{",
                            (unsigned long long)now_ms, server_cpu_percent,
                            c->send_queue_high_water);
        /*
         * Every stream, from `stream_names[]` itself.
         *
         * This named three of them -- spectrum, waterfall, receiver_state --
         * as nine hand-written format specifiers, which was the whole of the
         * link when it was written and had silently stopped being so twice
         * over: ticket 07's two survey streams and ticket 14's two FM ones
         * were invisible here, so a reader on either of those tabs was shown
         * counts for streams that tab does not use and none for the ones it
         * does. Same shape as the subscribe parser and the screen names, and
         * the same fix: one table, walked.
         */
        for (s = 0; s < VIEWER_STREAM_COUNT && json_len > 0 &&
                    (size_t)json_len < sizeof(json); s++)
            json_len += snprintf(json + json_len, sizeof(json) - (size_t)json_len,
                                 "%s\"%s\":{\"sent\":%llu,\"dropped\":%llu}",
                                 s ? "," : "", stream_names[s],
                                 (unsigned long long)c->slot[s].sent_count,
                                 (unsigned long long)c->slot[s].dropped_count);
        if (json_len > 0 && (size_t)json_len < sizeof(json) - 3)
            json_len += snprintf(json + json_len,
                                 sizeof(json) - (size_t)json_len, "}}");
        if (json_len <= 0 || (size_t)json_len >= sizeof(json))
            continue; /* truncated: a half-written object is not JSON */
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
