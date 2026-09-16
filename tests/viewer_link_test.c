#include "check.h"

#include "scope_view_model.h"
#include "viewer_link.h"
#include "websocket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/*
 * The Viewer link over real loopback sockets -- no window, no receiver, no
 * hardware, and no reason to fake what a socket does when the thing being
 * checked *is* what happens on a socket. Every client here is a from-scratch
 * WebSocket client written for this file, sharing no code with
 * src/websocket.c beyond the RFC both implement, the same principle behind
 * scripts/viewer_client.py's own independence.
 *
 * `struct viewer_link` is close to 12 MB (VIEWER_LINK_MAX_CLIENTS clients,
 * each sized for the largest message this link ever sends) -- never a stack
 * local; a fresh one is reset per test via viewer_link_open()/_close().
 */
static struct viewer_link vlink;

static uint16_t open_test_link(void) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);

    check_true("the vlink opens on an OS-assigned loopback port",
              viewer_link_open(&vlink, 0) == 0);
    memset(&addr, 0, sizeof(addr));
    getsockname(vlink.listen_fd, (struct sockaddr *)&addr, &addr_len);
    return ntohs(addr.sin_port);
}

/* A from-scratch client: connect, mask outgoing frames (RFC 6455 5.1), parse
   incoming ones (never masked -- a server's own rule this file is not the
   one to enforce, only to rely on). */
struct test_client {
    int fd;
    uint8_t buf[1 << 20];
    size_t have;
};

/*
 * Non-blocking from the moment it connects. This is one process playing
 * both roles -- client and server -- so nothing drives viewer_link_poll()
 * while a blocking call sits waiting for the other side; every read below
 * interleaves with a poll() instead, the same shape client_pump() already
 * uses for reading a response after a request is sent.
 */
/*
 * `rcvbuf`, when nonzero, is set before connect() -- the window a TCP
 * handshake advertises is fixed at that point, so shrinking SO_RCVBUF
 * afterwards does not un-advertise a larger window already given out.
 * Used by the interleaving test below to make its peer's window close
 * quickly and on purpose rather than depending on how many rounds a
 * tight, unpaced loop happens to need to fill the kernel's own buffer --
 * observed to vary between "never in 5000 rounds" and "in one jump,
 * never landing on a partial write" on this machine.
 */
static int client_connect_rcvbuf(struct test_client *tc, uint16_t port,
                                 int rcvbuf) {
    struct sockaddr_in addr;
    int flags;

    tc->fd = socket(AF_INET, SOCK_STREAM, 0);
    tc->have = 0;
    if (rcvbuf > 0)
        setsockopt(tc->fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    flags = fcntl(tc->fd, F_GETFL, 0);
    fcntl(tc->fd, F_SETFL, flags | O_NONBLOCK);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    connect(tc->fd, (struct sockaddr *)&addr, sizeof(addr)); /* EINPROGRESS ok */
    return 0;
}

static int client_connect(struct test_client *tc, uint16_t port) {
    return client_connect_rcvbuf(tc, port, 0);
}

static int client_handshake(struct test_client *tc) {
    char req[512];
    char resp[4096];
    size_t resp_have = 0;
    int len;
    int round;

    len = snprintf(req, sizeof(req),
                  "GET /viewer HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\n"
                  "Connection: Upgrade\r\n"
                  "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                  "Sec-WebSocket-Version: 13\r\n\r\n");
    send(tc->fd, req, (size_t)len, 0);
    for (round = 0; round < 200; round++) {
        ssize_t n;

        viewer_link_poll(&vlink, 5);
        n = recv(tc->fd, resp + resp_have, sizeof(resp) - 1 - resp_have, 0);
        if (n > 0) {
            resp_have += (size_t)n;
            resp[resp_have] = '\0';
            if (strstr(resp, "\r\n\r\n"))
                return strstr(resp, "101") ? 0 : -1;
        }
    }
    return -1; /* never completed */
}

static void client_send_frame(struct test_client *tc, int opcode,
                             const uint8_t *payload, size_t len) {
    uint8_t header[14];
    uint8_t mask[4] = { 0x11, 0x22, 0x33, 0x44 };
    size_t header_len;
    uint8_t masked[512];
    size_t i;

    header[0] = (uint8_t)(0x80 | opcode);
    if (len <= 125) {
        header[1] = (uint8_t)(0x80 | len);
        header_len = 2;
    } else {
        header[1] = 0x80 | 126;
        header[2] = (uint8_t)(len >> 8);
        header[3] = (uint8_t)len;
        header_len = 4;
    }
    memcpy(header + header_len, mask, 4);
    header_len += 4;
    for (i = 0; i < len; i++)
        masked[i] = (uint8_t)(payload[i] ^ mask[i % 4]);
    send(tc->fd, header, header_len, 0);
    if (len > 0)
        send(tc->fd, masked, len, 0);
}

static void client_send_text(struct test_client *tc, const char *text) {
    client_send_frame(tc, WEBSOCKET_OP_TEXT, (const uint8_t *)text,
                     strlen(text));
}

/* Reads whatever is available (non-blocking) into the client's own buffer,
   without decoding -- a few poll() cycles are usually enough for a small
   message on loopback. */
static void client_pump(struct test_client *tc, int rounds) {
    int i;
    int flags = fcntl(tc->fd, F_GETFL, 0);

    fcntl(tc->fd, F_SETFL, flags | O_NONBLOCK);
    for (i = 0; i < rounds; i++) {
        ssize_t n;

        viewer_link_poll(&vlink, 5);
        n = recv(tc->fd, tc->buf + tc->have, sizeof(tc->buf) - tc->have, 0);
        if (n > 0)
            tc->have += (size_t)n;
    }
}

/* Decodes exactly one frame from the front of the client's buffer, per
   RFC 6455 -- server frames are never masked, which this asserts rather
   than assumes. */
static int client_next_frame(struct test_client *tc, int *opcode,
                            const uint8_t **payload, size_t *len) {
    size_t off;
    size_t length;

    if (tc->have < 2)
        return 0;
    check_true("a server frame is never masked", (tc->buf[1] & 0x80) == 0);
    *opcode = tc->buf[0] & 0x0F;
    length = tc->buf[1] & 0x7F;
    off = 2;
    if (length == 126) {
        if (tc->have < 4)
            return 0;
        length = ((size_t)tc->buf[2] << 8) | tc->buf[3];
        off = 4;
    }
    if (tc->have < off + length)
        return 0;
    *payload = tc->buf + off;
    *len = length;
    memmove(tc->buf, tc->buf + off + length, tc->have - (off + length));
    tc->have -= off + length;
    return 1;
}

static void client_close_conn(struct test_client *tc) {
    close(tc->fd);
}

/* memmem() is a GNU/BSD extension, not POSIX -- this repo's own portability
   posture (_POSIX_C_SOURCE, nothing wider) says no to it, so a byte-string
   search this small is worth five lines rather than a feature-test macro. */
static int contains(const uint8_t *haystack, size_t haystack_len,
                    const char *needle) {
    size_t needle_len = strlen(needle);
    size_t i;

    if (needle_len == 0 || needle_len > haystack_len)
        return 0;
    for (i = 0; i + needle_len <= haystack_len; i++)
        if (memcmp(haystack + i, needle, needle_len) == 0)
            return 1;
    return 0;
}

static struct scope_view_model a_view_model(void) {
    static float average[2048], peak[2048], waterfall_row[2048];
    struct scope_view_model svm;
    int i;

    memset(&svm, 0, sizeof(svm));
    for (i = 0; i < 2048; i++) {
        average[i] = -50.0f - (float)(i % 10);
        peak[i] = -30.0f - (float)(i % 7);
        waterfall_row[i] = -45.0f - (float)(i % 5);
    }
    svm.spectrum_ready = 1;
    svm.spectrum_bins = 2048;
    svm.spectrum_windows = 4;
    svm.spectrum_average = average;
    svm.spectrum_peak = peak;
    svm.waterfall_ready = 1;
    svm.waterfall_row = waterfall_row;
    svm.center_hz = 948400000;
    svm.sample_rate_hz = 2000000;
    svm.ppm = 3;
    svm.tuning_generation = 1;
    svm.full_scale = 127.5f;
    return svm;
}

static void test_plain_get_serves_the_page(void) {
    uint16_t port = open_test_link();
    struct test_client tc;
    char req[] = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    char resp[8192];
    size_t have = 0;
    int i;

    client_connect(&tc, port);
    for (i = 0; i < 10; i++)
        viewer_link_poll(&vlink, 5);
    send(tc.fd, req, strlen(req), 0);
    for (i = 0; i < 60 && have < sizeof(resp) - 1; i++) {
        ssize_t n;

        viewer_link_poll(&vlink, 5);
        n = recv(tc.fd, resp + have, sizeof(resp) - 1 - have, 0);
        if (n > 0)
            have += (size_t)n;
    }
    resp[have] = '\0';
    check_true("a plain GET gets a response", have > 0);
    check_true("the response is 200 OK", strstr(resp, "200 OK") != NULL);
    check_true("the page mentions the Viewer", strstr(resp, "Viewer") != NULL);
    client_close_conn(&tc);
    viewer_link_close(&vlink);
}

static void test_upgrade_and_receiver_state(void) {
    uint16_t port = open_test_link();
    struct test_client tc;
    struct scope_view_model svm = a_view_model();
    int opcode;
    const uint8_t *payload;
    size_t len;

    client_connect(&tc, port);
    client_pump(&tc, 10);
    check_int("the handshake succeeds", client_handshake(&tc), 0);
    client_send_text(&tc, "subscribe receiver_state");
    client_pump(&tc, 10);

    viewer_link_publish_receiver_state(&vlink, &svm, 12345);
    client_pump(&tc, 10);

    check_true("a receiver_state message arrived",
              client_next_frame(&tc, &opcode, &payload, &len));
    check_int("it is a text frame", opcode, WEBSOCKET_OP_TEXT);
    check_true("it names the center frequency",
              contains(payload, len, "948400000"));
    check_true("it names the tuning generation",
              contains(payload, len, "\"tuning_generation\":1"));

    client_close_conn(&tc);
    viewer_link_close(&vlink);
}

/*
 * Ticket 08: what only the server knows about the link -- per-stream
 * sent/dropped counts and the kernel send-queue high-water mark -- read
 * back out of a real subscribed client, against known values driven in
 * through the same publish functions every other test uses. Unlike
 * receiver_state this is per client rather than one shared payload, so
 * the values checked are this one client's own.
 */
static void test_link_health_reports_this_clients_own_counters(void) {
    uint16_t port = open_test_link();
    struct test_client tc;
    struct scope_view_model svm = a_view_model();
    int opcode;
    const uint8_t *payload;
    size_t len;

    client_connect(&tc, port);
    client_pump(&tc, 10);
    client_handshake(&tc);
    client_send_text(&tc, "subscribe spectrum link_health");
    client_pump(&tc, 10);

    /* One spectrum message sent, one dropped -- queued twice with no
       poll() in between, so the first is replaced-unsent rather than
       reaching the wire (the same freshness rule ticket 05 pins). */
    viewer_link_publish_spectrum(&vlink, &svm, 0);
    viewer_link_publish_spectrum(&vlink, &svm, 0);
    client_pump(&tc, 10);
    check_true("the first spectrum frame arrived",
              client_next_frame(&tc, &opcode, &payload, &len));

    vlink.clients[0].send_queue_high_water = 4096;
    viewer_link_publish_link_health(&vlink, 42.5, 999);
    client_pump(&tc, 10);

    check_true("a link_health message arrived",
              client_next_frame(&tc, &opcode, &payload, &len));
    check_int("it is a text frame", opcode, WEBSOCKET_OP_TEXT);
    check_true("it names the type", contains(payload, len, "\"type\":\"link_health\""));
    check_true("it reports one spectrum message sent",
              contains(payload, len, "\"spectrum_sent\":1"));
    check_true("it reports one spectrum message dropped",
              contains(payload, len, "\"spectrum_dropped\":1"));
    check_true("it reports the send-queue high-water mark",
              contains(payload, len, "\"send_queue_high_water\":4096"));
    check_true("it reports the server CPU percentage handed in",
              contains(payload, len, "\"server_cpu_percent\":42.50"));

    client_close_conn(&tc);
    viewer_link_close(&vlink);
}

static void test_link_health_is_not_sent_when_unsubscribed(void) {
    uint16_t port = open_test_link();
    struct test_client tc;

    client_connect(&tc, port);
    client_pump(&tc, 10);
    client_handshake(&tc);
    client_send_text(&tc, "subscribe spectrum"); /* not link_health */
    client_pump(&tc, 10);

    viewer_link_publish_link_health(&vlink, 10.0, 0);
    client_pump(&tc, 10);

    check_size("nothing arrives for a stream never subscribed to", tc.have, 0);

    client_close_conn(&tc);
    viewer_link_close(&vlink);
}

static void test_spectrum_wire_format(void) {
    uint16_t port = open_test_link();
    struct test_client tc;
    struct scope_view_model svm = a_view_model();
    int opcode;
    const uint8_t *payload;
    size_t len;
    uint32_t generation, bins;
    uint64_t timestamp_ms;
    float first_average, first_peak;

    client_connect(&tc, port);
    client_pump(&tc, 10);
    client_handshake(&tc);
    client_send_text(&tc, "subscribe spectrum");
    client_pump(&tc, 10);

    viewer_link_publish_spectrum(&vlink, &svm, 999);
    client_pump(&tc, 10);

    check_true("a spectrum message arrived",
              client_next_frame(&tc, &opcode, &payload, &len));
    check_int("it is a binary frame", opcode, WEBSOCKET_OP_BINARY);
    check_size("its length matches header + two 2048-bin float arrays", len,
              20 + 2 * 2048 * 4);
    check_int("the protocol version is 1", payload[0], 1);
    check_int("the message type is spectrum (1)", payload[1], 1);
    memcpy(&generation, payload + 4, 4);
    check_int("the tuning generation round-trips", (int)generation, 1);
    memcpy(&timestamp_ms, payload + 8, 8);
    check_int("the timestamp round-trips", (int)timestamp_ms, 999);
    memcpy(&bins, payload + 16, 4);
    check_int("bins round-trips", (int)bins, 2048);
    memcpy(&first_average, payload + 20, 4);
    memcpy(&first_peak, payload + 20 + 2048 * 4, 4);
    check_close("the first average bin matches what was published",
               first_average, -50.0, 1e-6);
    check_close("the first peak bin matches what was published", first_peak,
               -30.0, 1e-6);

    client_close_conn(&tc);
    viewer_link_close(&vlink);
}

static void test_waterfall_wire_format(void) {
    uint16_t port = open_test_link();
    struct test_client tc;
    struct scope_view_model svm = a_view_model();
    int opcode;
    const uint8_t *payload;
    size_t len;
    float first_row;

    client_connect(&tc, port);
    client_pump(&tc, 10);
    client_handshake(&tc);
    client_send_text(&tc, "subscribe waterfall");
    client_pump(&tc, 10);

    viewer_link_publish_waterfall_row(&vlink, &svm, 0);
    client_pump(&tc, 10);

    check_true("a waterfall message arrived",
              client_next_frame(&tc, &opcode, &payload, &len));
    check_size("its length matches header + one 2048-bin float array", len,
              20 + 2048 * 4);
    check_int("the message type is waterfall_row (2)", payload[1], 2);
    memcpy(&first_row, payload + 20, 4);
    check_close("the row's first bin matches what was published", first_row,
               -45.0, 1e-6);

    client_close_conn(&tc);
    viewer_link_close(&vlink);
}

static void test_unsubscribed_stream_receives_nothing(void) {
    uint16_t port = open_test_link();
    struct test_client tc;
    struct scope_view_model svm = a_view_model();

    client_connect(&tc, port);
    client_pump(&tc, 10);
    client_handshake(&tc);
    client_send_text(&tc, "subscribe spectrum"); /* not waterfall */
    client_pump(&tc, 10);

    viewer_link_publish_waterfall_row(&vlink, &svm, 0);
    client_pump(&tc, 10);

    check_size("nothing arrives for a stream never subscribed to", tc.have, 0);

    client_close_conn(&tc);
    viewer_link_close(&vlink);
}

static void test_resubscribe_replaces_the_whole_set(void) {
    uint16_t port = open_test_link();
    struct test_client tc;
    struct scope_view_model svm = a_view_model();
    int opcode;
    const uint8_t *payload;
    size_t len;

    client_connect(&tc, port);
    client_pump(&tc, 10);
    client_handshake(&tc);
    client_send_text(&tc, "subscribe spectrum waterfall");
    client_pump(&tc, 10);
    client_send_text(&tc, "subscribe waterfall"); /* drops spectrum */
    client_pump(&tc, 10);

    viewer_link_publish_spectrum(&vlink, &svm, 0);
    viewer_link_publish_waterfall_row(&vlink, &svm, 0);
    client_pump(&tc, 10);

    check_true("the surviving subscription's message arrived",
              client_next_frame(&tc, &opcode, &payload, &len));
    check_int("it is the waterfall message, not spectrum", payload[1], 2);
    check_true("nothing else follows -- spectrum was dropped by the "
              "resubscribe",
              !client_next_frame(&tc, &opcode, &payload, &len));

    client_close_conn(&tc);
    viewer_link_close(&vlink);
}

/* ADR-0027's own rule: at most one unsent message per stream. A client that
   never reads must see the *dropped* count rise on the exact stream it is
   not draining, and never accumulate a queue behind it. */
static void test_a_client_that_never_reads_drops_not_queues(void) {
    uint16_t port = open_test_link();
    struct test_client tc;
    struct scope_view_model svm = a_view_model();
    int i;

    client_connect(&tc, port);
    client_pump(&tc, 10);
    client_handshake(&tc);
    client_send_text(&tc, "subscribe spectrum");
    client_pump(&tc, 10);

    /* Publish far more than one client-side buffer's worth without this
       test ever reading -- 2048 bins is ~16 KB per message; a couple of
       hundred of those exceeds any socket buffer this vlink would use. */
    for (i = 0; i < 200; i++) {
        svm.tuning_generation = (uint32_t)i;
        viewer_link_publish_spectrum(&vlink, &svm, (uint64_t)i);
        viewer_link_poll(&vlink, 0);
    }

    check_true("the stream's dropped count rose",
              vlink.clients[0].slot[VIEWER_STREAM_SPECTRUM].dropped_count > 0);

    client_close_conn(&tc);
    viewer_link_close(&vlink);
}

/*
 * Two binary streams over one socket: whichever one only partly reached
 * the wire on an earlier poll() (sent > 0, < length) owns the connection
 * until it finishes. This is the exact condition flush_client() exists
 * for -- before it, viewer_link_poll() flushed every stream with pending
 * data on every call, in a fixed order, whether or not an *earlier*
 * stream still had bytes in flight. Under real backpressure (a slow
 * Viewer's kernel buffer filling while its socket stays writable enough
 * for occasional partial sends) that let a fresh, fully-ready frame reach
 * send() ahead of another frame's leftover bytes -- and since both go
 * out on the *same* socket, "ahead of" means spliced into the middle of
 * it, which corrupts framing no per-message length or checksum can
 * express. Found with a raw-byte diagnostic client that trusts no
 * framing assumptions, unlike this file's own client_next_frame(), which
 * -- like a real Viewer -- believes a length field once it has read one.
 *
 * A genuine partial send needs real kernel backpressure, and how many
 * rounds that takes turned out to depend on this machine's TCP tuning
 * more than on anything worth pinning a check to -- a tight, unpaced
 * loop was observed to either fill the send buffer in one jump (the
 * window closes to exactly 0 and every further send() is refused
 * outright, never partial) or never fill it at all in 5000 rounds. So
 * this drives the exact state a real partial send leaves behind directly
 * -- one stream's slot with 0 < sent < length -- and checks the one
 * thing that state must guarantee: nothing else on this client's socket
 * moves until that stream's own remaining bytes do.
 */
static void test_no_cross_stream_interleaving_under_backpressure(void) {
    uint16_t port = open_test_link();
    struct test_client tc;
    struct scope_view_model svm = a_view_model();
    struct viewer_client *c;
    struct viewer_stream_slot *wf, *sp;
    size_t wf_stalled_at;

    client_connect(&tc, port);
    client_pump(&tc, 10);
    client_handshake(&tc);
    client_send_text(&tc, "subscribe spectrum waterfall");
    client_pump(&tc, 10);

    c = &vlink.clients[0];
    wf = &c->slot[VIEWER_STREAM_WATERFALL];
    sp = &c->slot[VIEWER_STREAM_SPECTRUM];

    /* A real waterfall frame, actually half-written to the real socket --
       not just accounted as such -- so the client genuinely holds the
       first half of it, exactly as a real partial send() leaves things,
       however many rounds that happens to take on a given machine. */
    viewer_link_publish_waterfall_row(&vlink, &svm, 0);
    check_true("the waterfall frame was queued", wf->length > 0);
    wf_stalled_at = wf->length / 2;
    check_size("the simulated partial send actually reached the socket",
              (size_t)send(c->fd, wf->data, wf_stalled_at, MSG_NOSIGNAL),
              wf_stalled_at);
    wf->sent = wf_stalled_at;
    c->inflight_stream = VIEWER_STREAM_WATERFALL;

    /* A fresh, fully-ready spectrum frame -- exactly what a real block's
       publish leaves queued alongside a stalled stream. */
    viewer_link_publish_spectrum(&vlink, &svm, 0);
    check_true("the spectrum frame was queued", sp->length > 0);
    check_size("spectrum has not been touched yet", sp->sent, 0);

    /* One poll: the socket is genuinely writable here (nothing upstream
       is actually full), so an unguarded per-stream loop would send
       spectrum's whole frame in this same call, ahead of finishing
       waterfall's stalled remainder. */
    viewer_link_poll(&vlink, 5);

    check_size("spectrum was not sent ahead of the in-flight waterfall frame",
              sp->sent, 0);
    check_true("the in-flight waterfall frame made progress instead",
              wf->length == 0 || wf->sent > wf_stalled_at);

    /* Let it fully drain, then confirm spectrum only goes out once
       waterfall's stall is behind it, and both arrive as one clean
       binary frame each -- no third, spliced frame in between. */
    client_pump(&tc, 50);
    {
        int opcode;
        const uint8_t *payload;
        size_t len;

        check_true("a frame arrived", client_next_frame(&tc, &opcode, &payload, &len));
        check_int("...binary", opcode, WEBSOCKET_OP_BINARY);
        check_true("a second frame arrived",
                  client_next_frame(&tc, &opcode, &payload, &len));
        check_int("...binary, too", opcode, WEBSOCKET_OP_BINARY);
        check_true("nothing else follows",
                  !client_next_frame(&tc, &opcode, &payload, &len));
        check_size("nothing undecodable left over", tc.have, 0);
    }

    client_close_conn(&tc);
    viewer_link_close(&vlink);
}

static void test_ping_answered_by_pong(void) {
    uint16_t port = open_test_link();
    struct test_client tc;
    int opcode;
    const uint8_t *payload;
    size_t len;

    client_connect(&tc, port);
    client_pump(&tc, 10);
    client_handshake(&tc);
    client_send_frame(&tc, WEBSOCKET_OP_PING, (const uint8_t *)"hello", 5);
    client_pump(&tc, 10);

    check_true("a pong arrived", client_next_frame(&tc, &opcode, &payload, &len));
    check_int("opcode is PONG", opcode, WEBSOCKET_OP_PONG);
    check_size("the pong's payload length matches the ping's", len, 5);
    check_true("the pong carries the ping's payload unchanged",
              memcmp(payload, "hello", 5) == 0);

    client_close_conn(&tc);
    viewer_link_close(&vlink);
}

static void test_close_handshake(void) {
    uint16_t port = open_test_link();
    struct test_client tc;
    uint8_t code[2] = { 0x03, 0xe8 }; /* 1000, normal closure */
    int opcode;
    const uint8_t *payload;
    size_t len;

    client_connect(&tc, port);
    client_pump(&tc, 10);
    client_handshake(&tc);
    client_send_frame(&tc, WEBSOCKET_OP_CLOSE, code, sizeof(code));
    client_pump(&tc, 10);

    check_true("a close reply arrived",
              client_next_frame(&tc, &opcode, &payload, &len));
    check_int("opcode is CLOSE", opcode, WEBSOCKET_OP_CLOSE);
    check_true("the client's slot is freed",
              vlink.clients[0].state == VIEWER_CLIENT_CLOSED);

    client_close_conn(&tc);
    viewer_link_close(&vlink);
}

static void test_two_clients_are_independent(void) {
    uint16_t port = open_test_link();
    struct test_client spectrum_client, state_client;
    struct scope_view_model svm = a_view_model();
    int opcode;
    const uint8_t *payload;
    size_t len;

    client_connect(&spectrum_client, port);
    client_connect(&state_client, port);
    client_pump(&spectrum_client, 10);
    client_handshake(&spectrum_client);
    client_handshake(&state_client);
    client_send_text(&spectrum_client, "subscribe spectrum");
    client_send_text(&state_client, "subscribe receiver_state");
    client_pump(&spectrum_client, 10);
    client_pump(&state_client, 10);

    viewer_link_publish_spectrum(&vlink, &svm, 0);
    viewer_link_publish_receiver_state(&vlink, &svm, 0);
    client_pump(&spectrum_client, 10);
    client_pump(&state_client, 10);

    check_true("the spectrum client got its message",
              client_next_frame(&spectrum_client, &opcode, &payload, &len));
    check_int("...binary", opcode, WEBSOCKET_OP_BINARY);
    check_true("the spectrum client got nothing else",
              !client_next_frame(&spectrum_client, &opcode, &payload, &len));

    check_true("the receiver_state client got its message",
              client_next_frame(&state_client, &opcode, &payload, &len));
    check_int("...text", opcode, WEBSOCKET_OP_TEXT);
    check_true("the receiver_state client got nothing else",
              !client_next_frame(&state_client, &opcode, &payload, &len));

    client_close_conn(&spectrum_client);
    client_close_conn(&state_client);
    viewer_link_close(&vlink);
}

int main(void) {
    test_plain_get_serves_the_page();
    test_upgrade_and_receiver_state();
    test_link_health_reports_this_clients_own_counters();
    test_link_health_is_not_sent_when_unsubscribed();
    test_spectrum_wire_format();
    test_waterfall_wire_format();
    test_unsubscribed_stream_receives_nothing();
    test_resubscribe_replaces_the_whole_set();
    test_a_client_that_never_reads_drops_not_queues();
    test_no_cross_stream_interleaving_under_backpressure();
    test_ping_answered_by_pong();
    test_close_handshake();
    test_two_clients_are_independent();
    return check_report("the Viewer link over real loopback sockets");
}
