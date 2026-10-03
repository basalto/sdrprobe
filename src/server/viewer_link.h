#ifndef VIEWER_LINK_H
#define VIEWER_LINK_H

#include <stddef.h>
#include <stdint.h>

#include "model/fm_view_model.h"
#include "model/adsb_view_model.h"
#include "model/gsm_view_model.h"
#include "model/lte_view_model.h"
#include "model/calibration_view_model.h"
#include "model/settings_view_model.h"
#include "model/srd_view_model.h"
#include "model/tetra_view_model.h"
#include "model/scope_view_model.h"
#include "model/survey_view_model.h"
#include "server/viewer_command.h"
#include "server/websocket.h"

/*
 * The Viewer link (ADR-0027): the Scope's view model, pushed to loopback
 * WebSocket clients as State updates. This is where `src/server/websocket.c`
 * (HTTP/1.1, RFC 6455 -- nothing here about sdrprobe) meets
 * `src/model/scope_view_model.h` (the Scope's measurements -- nothing there about
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
 * frames with a small fixed header (below); `receiver_state`,
 * `link_health` (ticket 08) and `command_result` (ticket 06) are JSON
 * text frames, because writing JSON is already solved
 * (`survey_json_escape()`, src/runtime/survey_store.c) and nothing here parses
 * it. The two things read from a client -- `subscribe <stream> ...` and
 * a command line (`tune <hz>`) -- are both whitespace-delimited lines,
 * on the same principle `src/core/capture_sidecar.h` states outright: this is
 * not a JSON parser and must not become one. `src/server/viewer_command.h` owns
 * what a command line actually says; this module only decides that a
 * line is one (anything that is not `subscribe ...`) and what happens to
 * its result.
 *
 * A third rule, ticket 06's own: a Viewer command is reliable and
 * ordered, unlike a State update. `command_result` does not follow the
 * replaceable-slot rule above -- it is a small FIFO per client instead,
 * because a dropped retune is not a stale picture, it is a receiver
 * pointed somewhere nobody asked for.
 *
 * ADR-0027's amendment, 2026-09-17: the bind address is no longer
 * unconditionally loopback. It is still the default, and still requires
 * nothing else when it is loopback -- reaching 127.0.0.1 already needs a
 * login on this machine, exactly as the ADR reasoned. Binding beyond it
 * (a LAN interface, `INADDR_ANY`) is now possible, and `options.c`
 * refuses that combination unless a shared-secret token accompanies it
 * (`--serve-token`), which every request must then carry as
 * `?token=...` in its path. A shared secret in a URL is a much weaker
 * boundary than "an account on this machine" -- no per-user identity, no
 * transport encryption, a token that leaks into browser history or a
 * proxy's access log is compromised until changed -- and this module
 * makes none of those claims for it; it is what the amendment decided is
 * enough for a trusted home LAN, not a substitute for real
 * authentication on a network that is not.
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
    VIEWER_MESSAGE_WATERFALL_ROW = 2,
    /* Ticket 07's survey chart: its own header (below) is 8 bytes wider
       than the two above, carrying the swept range -- a survey's spectrum
       has no fixed frequency grid the way the Scope's does, so a bin index
       alone says nothing without it. */
    VIEWER_MESSAGE_SURVEY_SPECTRUM = 3,
    /* The FM multiplex (ticket 14's Phase 4), on that same wider header and
       for the same reason: it is a baseband spectrum, 0 Hz to about 60 kHz,
       which is not the receiver's own grid either. */
    VIEWER_MESSAGE_FM_SPECTRUM = 4,
    /* The FM analysis charts behind "Show charts". The audio waveform is a
       plain float array on the base header (it is a time trace, not a
       spectrum, so it carries no range); the audio spectrum rides the wider
       range header (0 to ~16 kHz baseband, like the multiplex); and the RDS
       constellation is two float arrays on the base header, the same i-then-q
       layout the Scope's spectrum uses for average-then-peak. */
    VIEWER_MESSAGE_FM_AUDIO = 5,
    VIEWER_MESSAGE_FM_AUDIO_SPECTRUM = 6,
    VIEWER_MESSAGE_FM_SCATTER = 7,
    /* The TETRA analysis charts behind "Show charts": the phase-steps
       constellation (two arrays, x then y, like the FM one) and the
       repeats-within-a-slot bar profile (one array, base header). */
    VIEWER_MESSAGE_TETRA_SCATTER = 8,
    VIEWER_MESSAGE_TETRA_PROFILE = 9,
    /* The SRD analysis charts behind "Show charts": the demodulated envelope
       and the discretised chips, one float array each on the base header. */
    VIEWER_MESSAGE_SRD_ENVELOPE = 10,
    VIEWER_MESSAGE_SRD_CHIPS = 11,
    /* The ADS-B analysis charts behind "Show charts": the preamble-score
       landscape, the pulse-position bit confidence, the frame magnitude
       envelope (one array each), and the bit-decision scatter (two arrays,
       margin then amplitude). */
    VIEWER_MESSAGE_ADSB_LANDSCAPE = 12,
    VIEWER_MESSAGE_ADSB_CONFIDENCE = 13,
    VIEWER_MESSAGE_ADSB_ENVELOPE = 14,
    VIEWER_MESSAGE_ADSB_SCATTER = 15,
    /* The GSM analysis charts behind "View: Burst": the timing-correlation
       landscape, the soft symbol magnitudes and the differential phase
       trajectory (one array each), and the SCH constellation (two arrays,
       x then y). */
    VIEWER_MESSAGE_GSM_CORR = 16,
    VIEWER_MESSAGE_GSM_SOFT = 17,
    VIEWER_MESSAGE_GSM_PHASE = 18,
    VIEWER_MESSAGE_GSM_SCATTER = 19,
    /* The LTE analysis charts behind "Show charts": the PSS correlation, the
       SSS candidate scores, the channel across 72 subcarriers and the
       antenna-port coherence (one array each), and the PBCH constellation
       (two arrays, i then q). */
    VIEWER_MESSAGE_LTE_PSS = 20,
    VIEWER_MESSAGE_LTE_SSS = 21,
    VIEWER_MESSAGE_LTE_CHANNEL = 22,
    VIEWER_MESSAGE_LTE_PORTS = 23,
    VIEWER_MESSAGE_LTE_SCATTER = 24
};

/*
 * The header a binary message carries when its array does *not* sit on the
 * receiver's own frequency grid, and so cannot be read from `center_hz` and
 * `sample_rate_hz` the way `spectrum` and `waterfall_row` can. Wider than
 * `VIEWER_LINK_HEADER_BYTES` by the range the array spans: the first 20
 * bytes are identical to every other binary message (version, type,
 * reserved, generation, timestamp, bins), followed by
 *
 *   offset 20  u32  lower_hz
 *   offset 24  u32  upper_hz
 *   offset 28  ...  payload: `bins` float32, dBFS
 *
 * Two streams use it: the survey's swept spectrum, whose range is wherever
 * the sweep walked, and the FM multiplex, whose range is baseband. It was
 * `VIEWER_SURVEY_HEADER_BYTES` while the survey was the only one; the name
 * says what the eight bytes are *for* now that it is not, which is what
 * keeps the second user from looking like it is borrowing the first's.
 */
#define VIEWER_RANGE_HEADER_BYTES (VIEWER_LINK_HEADER_BYTES + 8)

/* The largest a message this link ever sends can be: a spectrum at the
   widest transform the Scope's resolution stepper reaches
   (SDR_DSP_FFT_MAX bins, both arrays). Every stream's pending slot is sized
   to this so publishing never has to ask whether this block's happens to
   be smaller than last block's. */
#define VIEWER_STREAM_MESSAGE_MAX \
    (VIEWER_LINK_HEADER_BYTES + 2 * SDR_DSP_FFT_MAX * (int)sizeof(float))

/*
 * `SURVEY_VIEW_MODEL_MAX_BINS` (`SURVEY_BINS`, 8192) floats plus the wider
 * range header -- smaller than `VIEWER_STREAM_MESSAGE_MAX` above with room
 * to spare (one array against a spectrum's two, at half the bin cap), kept
 * as its own constant so a future change to either does not silently resize
 * the other's slot.
 */
#define VIEWER_SURVEY_MESSAGE_MAX \
    (VIEWER_RANGE_HEADER_BYTES + SURVEY_VIEW_MODEL_MAX_BINS * (int)sizeof(float))

/* The FM multiplex, on the same header: 1024 bins, smaller again. Its own
   constant for the same reason the survey's is its own. */
#define VIEWER_FM_MESSAGE_MAX \
    (VIEWER_RANGE_HEADER_BYTES + FM_VIEW_MODEL_MAX_BINS * (int)sizeof(float))

enum viewer_stream {
    VIEWER_STREAM_SPECTRUM = 0,
    VIEWER_STREAM_WATERFALL,
    VIEWER_STREAM_RECEIVER_STATE,
    VIEWER_STREAM_LINK_HEALTH,
    VIEWER_STREAM_COMMAND_RESULT,
    /* Ticket 07: the Survey tab's own two streams, mirroring the Scope's
       binary/JSON split -- the bulk float array binary, the small
       structured state (status, sweeping, candidates) JSON. */
    VIEWER_STREAM_SURVEY_SPECTRUM,
    VIEWER_STREAM_SURVEY_STATE,
    /* The FM view (ticket 14's Phase 4), split the same way: the multiplex
       spectrum binary, the three panels' fields JSON. */
    VIEWER_STREAM_FM_SPECTRUM,
    VIEWER_STREAM_FM_STATE,
    /* The FM analysis charts behind "Show charts" -- the waveform, the audio
       spectrum and the RDS constellation. Paced on time at a 4 Hz heartbeat
       rather than on data (viewer_session.h), and subscribed only while the
       charts are showing, so they cost nothing when nobody is looking. */
    VIEWER_STREAM_FM_AUDIO,
    VIEWER_STREAM_FM_AUDIO_SPECTRUM,
    VIEWER_STREAM_FM_SCATTER,
    VIEWER_STREAM_GSM_STATE,
    /* The GSM analysis charts, paced on time at 4 Hz, the same as the rest. */
    VIEWER_STREAM_GSM_CORR,
    VIEWER_STREAM_GSM_SOFT,
    VIEWER_STREAM_GSM_PHASE,
    VIEWER_STREAM_GSM_SCATTER,
    VIEWER_STREAM_ADSB_STATE,
    /* The ADS-B analysis charts, paced on time at 4 Hz, the same as the rest. */
    VIEWER_STREAM_ADSB_LANDSCAPE,
    VIEWER_STREAM_ADSB_CONFIDENCE,
    VIEWER_STREAM_ADSB_ENVELOPE,
    VIEWER_STREAM_ADSB_SCATTER,
    VIEWER_STREAM_TETRA_STATE,
    /* The TETRA analysis charts, paced on time at 4 Hz and subscribed only
       while "Show charts" is up, the same as FM's. */
    VIEWER_STREAM_TETRA_SCATTER,
    VIEWER_STREAM_TETRA_PROFILE,
    VIEWER_STREAM_SRD_STATE,
    /* The SRD analysis charts, paced on time at 4 Hz and subscribed only while
       "Show charts" is up, the same as FM's and TETRA's. */
    VIEWER_STREAM_SRD_ENVELOPE,
    VIEWER_STREAM_SRD_CHIPS,
    VIEWER_STREAM_LTE_STATE,
    /* The LTE analysis charts, paced on time at 4 Hz, the same as the rest. */
    VIEWER_STREAM_LTE_PSS,
    VIEWER_STREAM_LTE_SSS,
    VIEWER_STREAM_LTE_CHANNEL,
    VIEWER_STREAM_LTE_PORTS,
    VIEWER_STREAM_LTE_SCATTER,
    VIEWER_STREAM_SETTINGS_STATE,
    VIEWER_STREAM_CAL_STATE,
    VIEWER_STREAM_COUNT
};

/*
 * A stream's name on the wire, as `subscribe` spells it.
 *
 * Public so a check can reach the table at all -- it was `static` in the
 * `.c`, and it is `stream_names[VIEWER_STREAM_COUNT]`, sized by the enum, so
 * a short initializer leaves the last entries **NULL** rather than
 * overrunning. C zero-fills; `%s` on a NULL is undefined and glibc happens
 * to render it "(null)". That is what happened when ticket 07 added two
 * values to the enum (`web-visualization/12`).
 *
 * Returns NULL outside the enum rather than a placeholder, so a caller
 * cannot mistake "no such stream" for a stream called something odd.
 */
const char *viewer_link_stream_name(enum viewer_stream stream);

/*
 * The longest a stream's name may be, and how much room every name together
 * needs.
 *
 * **Derived, not chosen.** The debug log's subscription summary was
 * `char summary[64]`, sized when there were five names, and two more
 * truncated it mid-word: `subscribed: spectrum waterfall receiver_state
 * link_health survey_spectrum s`. It was then raised to 160 with a comment
 * reading "90 bytes for all seven today" -- and seven more streams later
 * that needed 164, so it was **truncating again**, silently, at the moment
 * this ticket was picked up. A log line that lies about what a client asked
 * for, in the one line that exists to answer that question.
 *
 * Both numbers were right when written. Neither could stay right, because a
 * buffer sized from today's names is a caption that stops agreeing with its
 * picture. `check-viewer-link` asserts no name exceeds the bound and that
 * every name together fits inside it, so the next stream either fits or
 * fails the gate (`web-visualization/12`).
 */
#define VIEWER_STREAM_NAME_MAX 20
#define VIEWER_SUBSCRIPTION_SUMMARY_MAX \
    (VIEWER_STREAM_COUNT * (VIEWER_STREAM_NAME_MAX + 1) + 1)

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

/* How many command results (ticket 06) a client's own FIFO holds before
   this module refuses to enqueue another, and how large one JSON result
   text is allowed to be. Sized for a human typing commands, not for a
   sustained stream of them -- see viewer_link.c's enqueue_command_result(). */
#define VIEWER_COMMAND_RESULT_QUEUE_DEPTH 16
#define VIEWER_COMMAND_RESULT_JSON_MAX 256

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

    /*
     * Reliable, ordered results for ticket 06's commands -- a FIFO, not
     * a replaceable slot: `slot[VIEWER_STREAM_COMMAND_RESULT]` above
     * still carries whichever result is currently on the wire (or about
     * to be), following the exact same single-frame-in-flight discipline
     * every other stream does; this is the queue of ones not yet loaded
     * into it. 16 deep is headroom for a human typing commands, not a
     * tuned capacity -- see viewer_link.c's enqueue_command_result().
     */
    char result_queue[VIEWER_COMMAND_RESULT_QUEUE_DEPTH][VIEWER_COMMAND_RESULT_JSON_MAX];
    int result_queue_len[VIEWER_COMMAND_RESULT_QUEUE_DEPTH];
    int result_queue_head;
    int result_queue_count;
};

#define VIEWER_LINK_MAX_CLIENTS 8

/*
 * Executes one parsed command against the receiver -- the only place
 * this module reaches outside itself, and it reaches through an opaque
 * function pointer rather than a `struct app*`, the same seam
 * `device_backend.h` uses to keep hardware out of code that does not
 * need it. Returns 0 on success; on failure, writes a reason into
 * `error` (bounded by `error_cap`, always left NUL-terminated) --
 * expected to be `app->receiver_error` quoted, not a generic failure
 * (ticket 06's own acceptance criterion).
 */
typedef int (*viewer_command_handler)(void *ctx, const struct viewer_command *cmd,
                                      char *error, size_t error_cap);

struct viewer_link {
    int listen_fd;
    struct viewer_client clients[VIEWER_LINK_MAX_CLIENTS];
    viewer_command_handler command_handler;
    void *command_handler_ctx;
    /* ADR-0027's amendment of 2026-09-17: NULL (the default) means the
       bind address itself is still the whole authorization boundary,
       exactly as the ADR originally decided. Non-NULL means the caller
       chose to bind somewhere reachable beyond loopback and is relying on
       this instead -- every request, upgrade or plain page, must carry
       `?token=<this value>` in its path or is refused before it gets
       either. Not copied; the caller's string must outlive the link. */
    const char *required_token;
};

/*
 * Binds and listens on `port`, at `bind_addr` (host byte order --
 * `INADDR_LOOPBACK` for the original, unconditional default;
 * `INADDR_ANY` or a specific interface's address to reach beyond it).
 *
 * ADR-0027 originally read "this is not a configuration option here",
 * because the bind address was the *only* authorization check a Viewer
 * command had. Its 2026-09-17 amendment is what makes this a parameter
 * at all: binding beyond loopback now requires `required_token` to be
 * non-NULL (options.c refuses the combination that leaves it NULL before
 * this function is ever called), which is what stands in the bind
 * address's place once reaching the socket no longer implies a login on
 * this machine. `required_token`, if non-NULL, is the exact string every
 * request's `?token=` must equal -- not copied; must outlive `link`.
 *
 * Returns 0 on success, -1 on failure with a reason on stderr.
 */
int viewer_link_open(struct viewer_link *link, uint16_t port,
                     uint32_t bind_addr, const char *required_token);

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
 *
 * ----------------------------------------------------------------------
 * **What a publish costs, read this before adding a call.**
 *
 * A publish **queues into a replaceable slot; it does not send.** So an
 * unconditional republish leaves the serve loop's `select()` permanently
 * ready and the loop never waits -- it does not merely send more messages,
 * it stops idling at all. Measured twice, four and five orders of magnitude
 * over the block rate:
 *
 *   ticket 10   `receiver_state` republished every iteration: idle `--serve`
 *               went from 9.8% CPU to **98.6%** with one metadata subscriber.
 *   ticket 07   the survey pair, same shape, six days later, written by
 *               somebody who had just read ticket 10: **234216 messages in
 *               10 seconds** against the 15.26/s a live receiver's block rate
 *               caps it at.
 *
 * The second is why this paragraph is here rather than in a skill or in
 * `CLAUDE.md`: both are read before the work, and ticket 10's fix had
 * already produced a named, checked predicate that the new code did not use.
 * This is the file somebody opens to find the function they are about to
 * call.
 *
 * So: **a new stream is paced in `viewer_stream_pacing()`**
 * (`server/viewer_session.h`) and published through `viewer_publish_due()`.
 * `check-viewer-session` walks every value of `enum viewer_stream` and fails
 * on one nobody has paced. It cannot see the shape of the loop, so it cannot
 * catch a publish written outside the gate -- **`make bench-serve` is what
 * turns "seems fine" into a number**, and it belongs in the acceptance
 * criteria of any ticket that adds a stream.
 * ----------------------------------------------------------------------
 */
void viewer_link_publish_spectrum(struct viewer_link *link,
                                  const struct scope_view_model *svm,
                                  uint64_t now_ms);
void viewer_link_publish_waterfall_row(struct viewer_link *link,
                                       const struct scope_view_model *svm,
                                       uint64_t now_ms);
void viewer_link_publish_receiver_state(struct viewer_link *link,
                                        const struct receiver_view_model *rvm,
                                        uint64_t now_ms);

/*
 * The GSM screen, as JSON: the two readouts, the header's statistics and the
 * 124-channel power scan.
 *
 * No binary stream of its own. The waterfall the window draws over the ARFCN
 * axis is the *same* spectrum `waterfall` already carries -- only the axis
 * differs, and which ARFCN a frequency is is a decision, so it travels in
 * this object rather than being recomputed browser-side
 * (`web-visualization/07`).
 */
void viewer_link_publish_gsm_state(struct viewer_link *link,
                                   const struct gsm_view_model *gvm,
                                   uint64_t now_ms);

/*
 * The ADS-B screen: the funnel, the totals, and the newest 48 of the
 * message log -- whole every block rather than incrementally, because this
 * link drops messages and a lost increment loses decoded aircraft for good
 * (`model/adsb_view_model.h` has the arithmetic).
 */
void viewer_link_publish_adsb_state(struct viewer_link *link,
                                    const struct adsb_view_model *avm,
                                    uint64_t now_ms);

/* The TETRA screen: the identity, the funnel whose middle term separates a
   weak TETRA carrier from one that is not TETRA, and the whole identity
   log (64 rows is about 5 KB, so there is no newest-N question). */
void viewer_link_publish_tetra_state(struct viewer_link *link,
                                     const struct tetra_view_model *tvm,
                                     uint64_t now_ms);

/* The SRD screen: the tuning, the two counters, and the whole frame log --
   each row carrying the **absolute** frequency it was heard at, so a later
   retune does not drag the history with it. */
void viewer_link_publish_srd_state(struct viewer_link *link,
                                   const struct srd_view_model *svm,
                                   uint64_t now_ms);

/* The LTE screen: the funnel, the cell, what each measurement has done since
   the identity last changed, the broadcast, the findings already worded, and
   the band scan's rows. The largest of these objects by some way -- eight
   findings of 120 characters and 24 scan rows -- so its buffer is sized for
   both rather than for the usual case. */
void viewer_link_publish_lte_state(struct viewer_link *link,
                                   const struct lte_view_model *lvm,
                                   uint64_t now_ms);

/* The Settings panel: the staged set, what is applied, whether the two
   differ, and the panel's own failure line. Paced **on time** rather than on
   data -- it changes when somebody types, not when a block arrives. */
void viewer_link_publish_settings_state(struct viewer_link *link,
                                        const struct settings_view_model *svm,
                                        uint64_t now_ms);

/* The Calibration overlay: the staged reference, the residual buffer, which
   clause of the gate is unsatisfied, and what the two references make of
   each other. On time, like the Settings panel. */
void viewer_link_publish_cal_state(struct viewer_link *link,
                                   const struct calibration_view_model *cvm,
                                   uint64_t now_ms);

/*
 * Ticket 07's Survey tab, mirroring the pair above: the swept spectrum as a
 * binary message with its own range-carrying header (`VIEWER_MESSAGE_SURVEY_SPECTRUM`),
 * and the structured sweep state -- status, sweeping, candidates -- as one
 * JSON message per subscribed client, the same reason `link_health` is one
 * per client rather than encoded once: nothing else here is per-connection,
 * but this has no reason to be if a second one ever needs a reason to
 * differ.
 *
 * A caller with nobody subscribed to either stream should still call both,
 * cheaply, on the same principle as the pair above.
 */
void viewer_link_publish_survey_spectrum(struct viewer_link *link,
                                         const struct survey_view_model *svm,
                                         uint32_t tuning_generation,
                                         uint64_t now_ms);
void viewer_link_publish_survey_state(struct viewer_link *link,
                                      const struct survey_view_model *svm,
                                      uint64_t now_ms);

/*
 * The FM view (ticket 14's Phase 4), split the same way again: the multiplex
 * spectrum as a binary message on the range-carrying header -- baseband, so
 * `lower_hz` is 0 and `upper_hz` is `bins * bin_hz` -- and the three panels'
 * fields, including the funnel's own already-decided sentence, as JSON.
 *
 * `fm_view_model.h` is what decides which sentence and which emphasis; this
 * only spells it. A caller with nobody subscribed should still call both.
 */
void viewer_link_publish_fm_spectrum(struct viewer_link *link,
                                     const struct fm_view_model *fvm,
                                     uint32_t tuning_generation,
                                     uint64_t now_ms);
void viewer_link_publish_fm_state(struct viewer_link *link,
                                  const struct fm_view_model *fvm,
                                  uint64_t now_ms);
/* The three analysis charts behind "Show charts" (the waveform, the audio
   spectrum and the RDS constellation), paced on time at a 4 Hz heartbeat. */
void viewer_link_publish_fm_audio(struct viewer_link *link,
                                  const struct fm_view_model *fvm,
                                  uint32_t tuning_generation, uint64_t now_ms);
void viewer_link_publish_fm_audio_spectrum(struct viewer_link *link,
                                           const struct fm_view_model *fvm,
                                           uint32_t tuning_generation,
                                           uint64_t now_ms);
void viewer_link_publish_fm_scatter(struct viewer_link *link,
                                    const struct fm_view_model *fvm,
                                    uint32_t tuning_generation,
                                    uint64_t now_ms);
/* The TETRA analysis charts behind "Show charts": the phase-steps
   constellation and the repeats-within-a-slot profile. */
void viewer_link_publish_tetra_scatter(struct viewer_link *link,
                                       const struct tetra_view_model *tvm,
                                       uint32_t tuning_generation,
                                       uint64_t now_ms);
void viewer_link_publish_tetra_profile(struct viewer_link *link,
                                       const struct tetra_view_model *tvm,
                                       uint32_t tuning_generation,
                                       uint64_t now_ms);
/* The SRD analysis charts behind "Show charts": the demodulated envelope and
   the discretised chips. */
void viewer_link_publish_srd_envelope(struct viewer_link *link,
                                      const struct srd_view_model *svm,
                                      uint32_t tuning_generation,
                                      uint64_t now_ms);
void viewer_link_publish_srd_chips(struct viewer_link *link,
                                   const struct srd_view_model *svm,
                                   uint32_t tuning_generation,
                                   uint64_t now_ms);
/* The ADS-B analysis charts behind "Show charts": the preamble-score
   landscape, the pulse-position bit confidence, the frame magnitude envelope
   and the bit-decision scatter. */
void viewer_link_publish_adsb_landscape(struct viewer_link *link,
                                        const struct adsb_view_model *avm,
                                        uint32_t tuning_generation,
                                        uint64_t now_ms);
void viewer_link_publish_adsb_confidence(struct viewer_link *link,
                                         const struct adsb_view_model *avm,
                                         uint32_t tuning_generation,
                                         uint64_t now_ms);
void viewer_link_publish_adsb_envelope(struct viewer_link *link,
                                       const struct adsb_view_model *avm,
                                       uint32_t tuning_generation,
                                       uint64_t now_ms);
void viewer_link_publish_adsb_scatter(struct viewer_link *link,
                                      const struct adsb_view_model *avm,
                                      uint32_t tuning_generation,
                                      uint64_t now_ms);
/* The GSM analysis charts behind "View: Burst": the correlation landscape, the
   soft symbol magnitudes, the phase trajectory and the SCH constellation. */
void viewer_link_publish_gsm_corr(struct viewer_link *link,
                                  const struct gsm_view_model *gvm,
                                  uint32_t tuning_generation, uint64_t now_ms);
void viewer_link_publish_gsm_soft(struct viewer_link *link,
                                  const struct gsm_view_model *gvm,
                                  uint32_t tuning_generation, uint64_t now_ms);
void viewer_link_publish_gsm_phase(struct viewer_link *link,
                                   const struct gsm_view_model *gvm,
                                   uint32_t tuning_generation, uint64_t now_ms);
void viewer_link_publish_gsm_scatter(struct viewer_link *link,
                                     const struct gsm_view_model *gvm,
                                     uint32_t tuning_generation,
                                     uint64_t now_ms);
/* The LTE analysis charts behind "Show charts": the PSS correlation, the SSS
   candidate scores, the channel, the port coherence and the PBCH
   constellation. */
void viewer_link_publish_lte_pss(struct viewer_link *link,
                                 const struct lte_view_model *lvm,
                                 uint32_t tuning_generation, uint64_t now_ms);
void viewer_link_publish_lte_sss(struct viewer_link *link,
                                 const struct lte_view_model *lvm,
                                 uint32_t tuning_generation, uint64_t now_ms);
void viewer_link_publish_lte_channel(struct viewer_link *link,
                                     const struct lte_view_model *lvm,
                                     uint32_t tuning_generation,
                                     uint64_t now_ms);
void viewer_link_publish_lte_ports(struct viewer_link *link,
                                   const struct lte_view_model *lvm,
                                   uint32_t tuning_generation, uint64_t now_ms);
void viewer_link_publish_lte_scatter(struct viewer_link *link,
                                     const struct lte_view_model *lvm,
                                     uint32_t tuning_generation,
                                     uint64_t now_ms);

/*
 * Ticket 08's Health panel: what only the server knows about the link
 * itself, unlike the other streams this is *not* one shared payload --
 * each client's own sent/dropped/high-water counts are its own, so this
 * builds one JSON message per subscribed client rather than encoding
 * once and fanning it out. `server_cpu_percent` is handed in already
 * computed (`process_cpu.h`); this module stays as decoupled from
 * process accounting as it already is from raylib.
 *
 * The per-stream counts travel as one `streams` object keyed by the name
 * in `stream_names[]`, every stream in the enum, rather than as a
 * hand-written pair of fields per stream. The hand-written form named
 * three and had fallen two behind twice -- ticket 07's survey pair and
 * ticket 14's FM pair -- so a Viewer on either of those tabs was shown
 * counts for streams it was not using and none for the ones it was.
 */
void viewer_link_publish_link_health(struct viewer_link *link,
                                     double server_cpu_percent,
                                     uint64_t now_ms);

/* How many clients are currently open -- for a status line, or a check. */
int viewer_link_client_count(const struct viewer_link *link);

/*
 * Wires ticket 06's inbound half to whatever can execute a command --
 * viewer_session.c, in production, via retune_receiver(). `ctx` is
 * handed back unchanged as `handler`'s first argument. Without a handler
 * set, a well-formed command is refused with a fixed reason rather than
 * silently doing nothing (see viewer_link.c's dispatch_command_line()).
 */
void viewer_link_set_command_handler(struct viewer_link *link,
                                     viewer_command_handler handler,
                                     void *ctx);

#endif
