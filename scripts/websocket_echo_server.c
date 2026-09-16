/*
 * A throwaway server for one manual proof ticket 04 asks for: that a real
 * browser can open a page this program serves, upgrade to a WebSocket, and
 * round-trip a binary payload through src/websocket.c's frame codec. It is
 * not part of sdrprobe and never will be -- wiring a Viewer link into the
 * real program is ticket 05's job, over this module.
 *
 * One client at a time, blocking I/O, no threads: this exists to be looked
 * at once, not to serve anything.
 *
 *   make websocket-echo-server && build/websocket_echo_server [port]
 *   then open http://127.0.0.1:<port>/ in a browser
 */
#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "websocket.h"

static const char PAGE[] =
    "<!doctype html><title>WebSocket echo test</title>\n"
    "<body style=\"background:#0c131c;color:#5adca4;font:24px monospace;"
    "padding:2em\">\n"
    "<div id=\"out\">connecting...</div>\n"
    "<script>\n"
    "const out = document.getElementById('out');\n"
    "function fail(msg) { out.textContent = 'FAIL: ' + msg; "
    "out.style.color = '#ff6864'; document.title = 'FAIL'; }\n"
    "function pass() { out.textContent = 'PASS'; document.title = 'PASS'; }\n"
    "try {\n"
    "  const ws = new WebSocket('ws://' + location.host + '/echo');\n"
    "  ws.binaryType = 'arraybuffer';\n"
    "  const payload = new Uint8Array([0,1,2,253,254,255,42,7]);\n"
    "  ws.onopen = () => ws.send(payload);\n"
    "  ws.onmessage = (ev) => {\n"
    "    const got = new Uint8Array(ev.data);\n"
    "    if (got.length !== payload.length) { fail('length ' + got.length); "
    "return; }\n"
    "    for (let i = 0; i < payload.length; i++)\n"
    "      if (got[i] !== payload[i]) { fail('byte ' + i + ' was ' + "
    "got[i]); return; }\n"
    "    pass();\n"
    "    ws.close(1000, 'done');\n"
    "  };\n"
    "  ws.onerror = () => fail('onerror');\n"
    "} catch (e) { fail(String(e)); }\n"
    "</script>\n";

/* Reads from `fd` into `buf[*have..cap)` until `websocket_request_parse()`
   sees a complete header block or the buffer fills. Returns the parsed
   request's consumed length (> 0), or -1 on EOF, a read error, or a
   request too large for `cap`. */
static int read_http_request(int fd, char *buf, size_t cap, size_t *have,
                             struct websocket_request *req) {
    for (;;) {
        int consumed = websocket_request_parse(buf, *have, req);

        if (consumed > 0)
            return consumed;
        if (consumed < 0)
            return -1;
        if (*have >= cap)
            return -1;
        {
            ssize_t n = recv(fd, buf + *have, cap - *have, 0);

            if (n <= 0)
                return -1;
            *have += (size_t)n;
        }
    }
}

static int send_all(int fd, const void *data, size_t len) {
    const char *p = data;

    while (len > 0) {
        ssize_t n = send(fd, p, len, 0);

        if (n <= 0)
            return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static void serve_page(int fd) {
    char header[256];
    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/html; charset=utf-8\r\n"
                              "Content-Length: %zu\r\n"
                              "Connection: close\r\n\r\n",
                              sizeof(PAGE) - 1);

    if (send_all(fd, header, (size_t)header_len) == 0)
        send_all(fd, PAGE, sizeof(PAGE) - 1);
}

/* The handshake response (RFC 6455 4.2.2), then the echo loop: every text
   or binary message that completes (through the fragment assembler, so a
   browser that fragments a large send is handled the same as one that
   does not) is sent straight back with the same opcode; a ping gets a
   pong; a close gets a close reply and ends the connection. */
static void serve_websocket(int fd, const struct websocket_request *req) {
    const struct websocket_header *key =
        websocket_request_header(req, "Sec-WebSocket-Key");
    char accept[64];
    char response[256];
    int response_len;
    uint8_t buf[1 << 16];
    size_t have = 0;
    struct websocket_message_assembler assembler;
    char key_copy[256];

    if (!key || key->value_len >= sizeof(key_copy))
        return;
    memcpy(key_copy, key->value, key->value_len);
    key_copy[key->value_len] = '\0';
    if (websocket_accept_value(key_copy, accept, sizeof(accept)) == 0)
        return;

    response_len = snprintf(response, sizeof(response),
                            "HTTP/1.1 101 Switching Protocols\r\n"
                            "Upgrade: websocket\r\n"
                            "Connection: Upgrade\r\n"
                            "Sec-WebSocket-Accept: %s\r\n\r\n",
                            accept);
    if (send_all(fd, response, (size_t)response_len) < 0)
        return;

    memset(&assembler, 0, sizeof(assembler));
    for (;;) {
        long consumed;
        struct websocket_frame frame;

        if (have < sizeof(buf)) {
            ssize_t n = recv(fd, buf + have, sizeof(buf) - have, 0);

            if (n <= 0)
                return;
            have += (size_t)n;
        }

        consumed = websocket_frame_decode(buf, have, &frame);
        if (consumed < 0)
            return; /* a protocol violation: RFC 6455 says close, not reply */
        if (consumed == 0) {
            if (have == sizeof(buf))
                return; /* a frame too large for this throwaway buffer */
            continue;   /* more bytes needed */
        }

        if (frame.opcode == WEBSOCKET_OP_PING) {
            uint8_t pong[256];
            size_t pong_len = websocket_pong_for(pong, sizeof(pong),
                                                 frame.payload,
                                                 frame.payload_len);

            if (pong_len > 0)
                send_all(fd, pong, pong_len);
        } else if (frame.opcode == WEBSOCKET_OP_CLOSE) {
            uint8_t close_reply[8];
            size_t reply_len = websocket_close_frame(close_reply,
                                                     sizeof(close_reply),
                                                     1000);

            send_all(fd, close_reply, reply_len);
            return;
        } else if (frame.opcode != WEBSOCKET_OP_PONG) {
            int opcode;
            const uint8_t *data;
            size_t len;
            int fed = websocket_message_feed(&assembler, &frame, &opcode,
                                             &data, &len);

            if (fed < 0)
                return;
            if (fed == 1) {
                uint8_t out[1 << 17];
                size_t out_len = websocket_frame_encode(out, sizeof(out), 1,
                                                        opcode, data, len);

                if (out_len > 0)
                    send_all(fd, out, out_len);
            }
        }

        memmove(buf, buf + consumed, have - (size_t)consumed);
        have -= (size_t)consumed;
    }
}

int main(int argc, char **argv) {
    int port = argc > 1 ? atoi(argv[1]) : 8765;
    int listener;
    struct sockaddr_in addr;

    signal(SIGPIPE, SIG_IGN); /* a client closing mid-send is not a crash */

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        perror("socket");
        return 1;
    }
    {
        int one = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    /* Loopback only, per ADR-0027: this ticket does not make the bind
       address a configuration option. */
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(listener, 4) < 0) {
        perror("listen");
        return 1;
    }
    fprintf(stderr, "listening on 127.0.0.1:%d -- open http://127.0.0.1:%d/ "
                    "in a browser\n", port, port);

    for (;;) {
        int fd = accept(listener, NULL, NULL);
        char buf[4096];
        size_t have = 0;
        struct websocket_request req;
        int consumed;

        if (fd < 0)
            continue;
        consumed = read_http_request(fd, buf, sizeof(buf), &have, &req);
        if (consumed > 0) {
            fprintf(stderr, "%.*s %.*s -> %s\n", (int)req.method_len,
                   req.method, (int)req.path_len, req.path,
                   websocket_request_is_upgrade(&req) ? "upgrade" : "page");
            if (websocket_request_is_upgrade(&req))
                serve_websocket(fd, &req);
            else
                serve_page(fd);
        } else {
            fprintf(stderr, "request did not parse (consumed=%d)\n", consumed);
        }
        close(fd);
    }
}
