#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <sys/stat.h>
#include <time.h>

#include "tech/adsb_dsp.h"
#include "runtime/app.h"
#include "core/capture_sidecar.h"
#include "runtime/debug_log.h"
#include "core/device_profile.h"
#include "runtime/frame_advance.h"
#include "tech/gsm_dsp.h"
#include "tech/lte_chain_analysis.h"
#include "tech/lte_confirm.h"
#include "tech/lte_findings.h"
#include "tech/lte_stats.h"
#include "runtime/options.h"
#include "runtime/runtime.h"
#include "core/sdr_dsp.h"
#include "tech/tetra_dsp.h"
#include "tech/tetra_sync.h"
#include "runtime/version.h"
#include "server/viewer_session.h"

/*
 * Every run with no window: `headless` and `server`.
 *
 * It was the back half of `sdrprobe.c`, behind `run_gui()` and `InitWindow()`
 * in the same translation unit -- so a build for a machine with no graphics
 * stack had to compile the window to reach the decode
 * (`.scratch/layer-boundaries/issues/04-*`). Nothing here draws, and the
 * split is *window / no window* rather than gui / server: a headless decode
 * on a box beside an antenna is the use `./sdrprobe` exists for, and it
 * needed raylib for nothing.
 *
 * `run_headless()` is the one entry point; `main()` is in `app_main.c`,
 * shared by both binaries.
 */

/* Enumeration is the backend's: UHD enumerates by device args, not by index
   (device_backend.h). */
int list_devices(void) {
    return device_backend_rtlsdr_list();
}


/* Print what one block's worth of decoding produced. The views keep the same
   results on screen; this is the same data as lines, so a capture can be
   checked from a script without a display or a person. */
/*
 * What the cell is saying, when this SCH is the one a broadcast block follows.
 *
 * The BCCH occupies frames 2 to 5 of the 51-multiframe, so only the SCH at
 * frame 1 is followed by one -- one in five of them. The other four are
 * followed by paging and access grants, which this does not read.
 */
static void print_broadcast(struct app *app, const struct gsm_sch_result *sch)
{
    float soft[GSM_BCCH_BURSTS * GSM_BURST_DATA_BITS];
    float bursts[GSM_BCCH_BURSTS][GSM_BURST_DATA_BITS];
    float coded[GSM_BCCH_CODED_BITS];
    struct gsm_bcch_block block;
    struct gsm_si si;

    if (sch->frame_number % 51 != 1)
        return;
    memset(soft, 0, sizeof(soft));
    if (gsm_normal_bursts(app->frame.i_samples, app->frame.q_samples, app->frame.pair_count,
                          (double)app->applied.sample_rate_hz, sch,
                          GSM_BCCH_BURSTS, soft) < GSM_BCCH_BURSTS)
        return; /* the block ran past the end of this sample block */
    for (int b = 0; b < GSM_BCCH_BURSTS; b++)
        memcpy(bursts[b], &soft[b * GSM_BURST_DATA_BITS], sizeof(bursts[b]));
    gsm_bcch_deinterleave((const float (*)[GSM_BURST_DATA_BITS])bursts, coded);
    if (!gsm_bcch_decode_block(coded, &block))
        return; /* the Fire code refused it, so it is not a message */
    if (!gsm_si_parse(block.octets, &si))
        return; /* a broadcast this does not read */

    printf("BCCH %s", gsm_si_type_name(si.type));
    if (si.have_lai)
        printf("  MCC %d MNC %0*d  LAC %d", si.mcc, si.mnc_digits, si.mnc,
               si.lac);
    if (si.have_cell_id)
        printf("  CI %d", si.cell_id);
    if (si.neighbour_count) {
        printf("  ARFCN");
        for (int i = 0; i < si.neighbour_count; i++)
            printf(" %d", si.neighbours[i]);
    }
    printf("\n");
}

/*
 * What an LTE block yielded, printed only when it says something new.
 *
 * A cell is announced once and then only when it changes, because the search
 * finds the same one in every block and a line per block would bury the
 * message. The message itself is printed whenever it decodes: its frame number
 * advances, so consecutive lines are the cell's clock ticking rather than
 * repetition.
 */
static void print_lte(struct app *app, double now)
{
    uint64_t cells_before = app->lte.session.cells_found;
    uint64_t confirmed_before = app->lte.session.mibs_confirmed;
    const struct lte_cell *cell = &app->lte.session.cell;
    const struct lte_mib *mib = &app->lte.session.mib;

    update_lte(app, now);

    if (app->lte.session.cells_found > cells_before &&
        cell->pci != app->lte.announced_pci) {
        printf("LTE  cell %d (N_ID_1 %d, N_ID_2 %d)  %s CP"
               "  offset %+.1f kHz (%+d subcarriers)  PSS %.2f  SSS %.2f\n",
               cell->pci, cell->n_id_1, cell->n_id_2,
               cell->extended_cp ? "extended" : "normal",
               cell->frequency_offset_hz / 1e3, cell->integer_offset,
               (double)cell->pss_correlation, (double)cell->sss_correlation);
        app->lte.announced_pci = cell->pci;
    }
    /* Only a message that repeated counts, so this prints at most once per
       cell rather than once per lucky parity. */
    if (app->lte.session.mibs_confirmed > confirmed_before)
        printf("MIB  %d blocks (%.2f MHz)  PHICH %s %s  SFN %d"
               "  %d antenna port%s\n",
               mib->bandwidth_prb,
               lte_mib_occupied_hz(mib->bandwidth_prb) / 1e6,
               mib->phich_extended ? "extended" : "normal",
               lte_phich_resource_name(mib->phich_resource_sixths),
               mib->system_frame_number, mib->antenna_ports,
               mib->antenna_ports == 1 ? "" : "s");
}

/*
 * TETRA: one block of samples to whatever the network says about itself.
 *
 * Stateless per block, unlike the other decoders here, because a TETRA
 * downlink is continuous and every block carries several synchronization
 * bursts -- this base station sends one in every timeslot, about seventy a
 * second. There is nothing to accumulate across blocks and nothing to lose
 * lock on.
 *
 * One line per identity rather than one per burst. The identity repeats
 * seventy times a second and is the same every time; what a reader wants is
 * the network, and how many bursts stood behind it.
 */
/*
 * One block of TETRA, printed.
 *
 * This was a second copy of the whole chain -- coarse offset, filter,
 * demodulate, walk the symbols, decode, pull the fields -- differing from the
 * view's only in spelling: it unrolled four bit loops where the view called a
 * `field()` helper. Both are transcriptions from ETSI EN 300 392-2 and either
 * could have been corrected without the other. Now this is an adapter that
 * prints what a session read.
 */
static void print_tetra(struct app *app, double now)
{
    struct tetra_session_event event;
    struct tetra_session *t = &app->tetra.session;

    (void)now;
    tetra_session_feed(t, app->frame.i_samples, app->frame.q_samples, app->frame.pair_count,
                       (double)app->applied.sample_rate_hz, &event);

    if (event.rate_unsupported) {
        static int complained;
        if (!complained++)
            fprintf(stderr, "TETRA needs a sample rate that is a whole "
                            "multiple of %.0f S/s; %u is not.\n",
                    TETRA_WORK_RATE_HZ, app->applied.sample_rate_hz);
        return;
    }
    if (!event.demodulated)
        return;

    if (t->blocks == 0) {
        printf("TETRA  lock %.2f  %d burst(s), none with the parity "
               "checking\n", (double)t->lock, t->bursts);
        fflush(stdout);
        return;
    }
    /* Only when the network moved: a base station here sends a
       synchronisation burst in every timeslot, about seventy a second, and
       printing each would bury the one line that says something. */
    if (event.identity_changed) {
        printf("TETRA  MCC %d  MNC %d  colour %d%s%d  "
               "(%d burst(s), %d block(s), %d broadcast)\n",
               t->mcc, t->mnc, t->colour, t->la >= 0 ? "  LA " : "  LA unread",
               t->la >= 0 ? t->la : 0, t->bursts, t->blocks, t->broadcast);
        fflush(stdout);
    }
}

static void print_new_decodes(struct app *app, double now,
                              enum decode_kind decoder)
{
    if (decoder == DECODE_TETRA) {
        print_tetra(app, now);
        return;
    }
    if (decoder == DECODE_SRD) {
        struct srd_session_event event;
        double sample_rate = (double)app->applied.sample_rate_hz;
        float full_scale = app->device.full_scale > 0.0f ? app->device.full_scale : 127.5f;

        srd_session_feed(&app->srd.session, app->frame.i_samples, app->frame.q_samples,
                         app->frame.pair_count, sample_rate, full_scale,
                         app->srd.polarity, now, &event);

        for (int i = 0; i < event.undecoded_count; i++) {
            struct srd_session_undecoded_event *ue = &event.undecoded[i];

            /*
             * The kind is the session's, not this adapter's. It used to call
             * every 2-FSK burst a WAKEUP and print "preamble 0 chips" for
             * one that had no chip period at all, while the window called
             * the same event UNDECODED.
             */
            if (ue->kind == SRD_FRAME_FSK_DETECTED) {
                printf("SRD  2FSK    WAKEUP   %6.1fs  %+.1f kHz  preamble %zu chips (%.0fus)  ",
                       ue->at, ue->carrier_hz / 1e3, ue->chip_count, ue->chip_us);
                uint8_t packed[8] = {0};
                size_t pb = srd_pack_bits(ue->raw_chips, ue->chip_count > 64 ? 64 : ue->chip_count, packed, 8);
                for (size_t b = 0; b < pb; b++)
                    printf("%02X ", packed[b]);
                printf("\n");
            } else {
                printf("SRD  %s  UNDECODED  %6.1fs  %+.1f kHz  "
                       "detected burst (no frame)\n",
                       ue->modulation == SRD_MOD_FSK2 ? "2FSK" : "OOK ",
                       ue->at, ue->carrier_hz / 1e3);
            }
        }

        for (int i = 0; i < event.frame_count; i++) {
            struct srd_session_frame_event *fe = &event.frames[i];
            const struct srd_frame *f = &fe->frame;
            enum srd_device_type device =
                srd_device_type_of(f->kind, f->bytes, f->byte_count);

            printf("SRD  %s  %s  %6.1fs  %+.1f kHz  %-14s  ",
                   f->modulation == SRD_MOD_FSK2 ? "2FSK" : "OOK ",
                   f->kind == SRD_FRAME_FULL ? "FULL   " :
                   f->kind == SRD_FRAME_REPEAT ? "REPEAT " : "GENERIC",
                   fe->at, fe->carrier_hz / 1e3,
                   srd_device_type_name(device));
            /* The prefix test lives in srd_frame.c; this asks it rather than
               spelling it out a third time. */
            if (device == SRD_DEVICE_REMOTE_FSK) {
                uint32_t id = ((uint32_t)f->bytes[4] << 24) |
                              ((uint32_t)f->bytes[5] << 16) |
                              ((uint32_t)f->bytes[6] << 8) |
                              (uint32_t)f->bytes[7];
                uint16_t seq = ((uint16_t)f->bytes[8] << 8) | f->bytes[9];
                uint8_t flags = f->bytes[3];
                printf("id %08X seq %04X flg %02X  ", id, seq, flags);
            }
            for (size_t b = 0; b < f->byte_count; b++)
                printf("%02X ", f->bytes[b]);
            printf("\n");
        }
        fflush(stdout);
        return;
    }
    if (decoder == DECODE_LTE) {
        print_lte(app, now);
        fflush(stdout);
        return;
    }
    if (decoder == DECODE_FM) {
        /*
         * One line whenever the station's account of itself changes, rather
         * than one per block: FM is continuous and a block adds a couple of
         * groups, so per-block output would be four hundred lines saying the
         * same thing. What changes is worth printing; what repeats is not.
         */
        static uint16_t announced_pi;
        static int announced_valid;
        static char announced_ps[9];
        const struct rds_station *s = &app->fm.session.station;

        update_fm(app, now);
        if (s->pi_valid && (!announced_valid || s->pi != announced_pi)) {
            printf("FM   station 0x%04X  %.3f MHz\n", s->pi,
                   app->applied.frequency_hz / 1e6);
            announced_pi = s->pi;
            announced_valid = 1;
            announced_ps[0] = '\0';
        }
        {
            static char announced_rt[65];
            if (s->rt_valid && strcmp(s->rt, announced_rt) != 0) {
                printf("RT   \"%s\"\n", s->rt);
                snprintf(announced_rt, sizeof(announced_rt), "%s", s->rt);
            }
        }
        if (s->ps_valid && strcmp(s->ps, announced_ps) != 0) {
            printf("RDS  \"%s\"  %s, %s  identification 0x%04X "
                   "(%d agreeing)\n", s->ps,
                   rds_pty_name(s->pty) ? rds_pty_name(s->pty) : "?",
                   rds_traffic_name(s->tp, s->ta), s->pi, s->pi_repeats);
            snprintf(announced_ps, sizeof(announced_ps), "%s", s->ps);
        }
        fflush(stdout);
        return;
    }
    if (decoder == DECODE_GSM) {
        int had = app->gsm.session.sch_valid;
        double before = app->gsm.session.sch_time;
        update_gsm_sch(app, now);
        if (!app->gsm.session.sch_valid || (had && app->gsm.session.sch_time == before))
            return;
        const struct gsm_sch_result *sch = &app->gsm.session.sch;
        printf("SCH  BSIC %d (NCC %d, BCC %d)  frame %d (T1/T2/T3 %d/%d/%d)"
               "  match %.2f%s\n",
               sch->bsic, sch->ncc, sch->bcc, sch->frame_number, sch->t1,
               sch->t2, sch->t3, (double)sch->confidence,
               app->gsm.session.continuity.implausible ? "  [T1 JUMPED]" : "");
        print_broadcast(app, sch);
    } else {
        int before = app->adsb.log_count;
        uint64_t frames = app->adsb.session.frames_total;
        update_adsb(app, now);
        int added = (int)(app->adsb.session.frames_total - frames);
        if (added <= 0)
            return;
        (void)before;
        /* The log is newest-first, so walk the new rows back to front to
           print them in the order they arrived. */
        for (int i = added - 1; i >= 0; i--) {
            const struct adsb_log_entry *e = &app->adsb.log[i];
            printf("%s  %s  %-3s  %-42s  %s\n", e->stamp, e->icao, e->label,
                   e->detail, e->raw);
        }
    }
    fflush(stdout);
}

/* Acquire with no window: start the worker, optionally record, and stop when
   the recording finishes, the duration elapses, or a signal arrives. Nothing
   here touches raylib, and nothing needs the frame loop -- recording tees off
   inside the acquisition thread, upstream of the display's block slot. */
/* An integer as text, for one printf field that takes either an ARFCN or an
   EARFCN. Two %d fields would mean two format strings for one line. */
static const char *int_text(int value) {
    static char buffer[16];

    snprintf(buffer, sizeof(buffer), "%d", value);
    return buffer;
}

/*
 * raylib used to be silenced here -- `SetTraceLogLevel(LOG_NONE)` -- because
 * it writes its own notices to stdout and stdout on this path is a data
 * stream someone is parsing, so a stray line would corrupt a survey rather
 * than merely clutter it.
 *
 * The comment beside it said "nothing should reach raylib on this path", and
 * that is now **enforced rather than hoped for**: this file compiles with no
 * raylib header and links into `./sdrprobe` with no raylib at all
 * (ticket 04). A defence against a call that cannot exist is a defence whose
 * failure nobody would notice, so it is gone and this is why.
 */
int run_headless(struct app *app) {
    const char *basename;
    const char *technology;
    int recording_started = 0;
    int result = 0;
    double started;

    /* Nothing is watching a headless run, so playing a capture at the speed of
       a receiver buys nothing and costs blocks: the idle poll below is longer
       than the 65.5 ms a block covers, so the slot overwrites and the same
       capture decodes a different number of messages each run. Read it whole
       instead, as fast as this loop can take it. A live receiver keeps the
       overwriteable slot -- it cannot be asked to wait. */
    if (app->options.file_path)
        acquisition_set_lossless(&app->acq, 1);

    if (start_acquisition(app) < 0) {
        fprintf(stderr, "Cannot start acquisition: %s\n",
                app->receiver_error[0] ? app->receiver_error : "unknown");
        return -1;
    }
    started = monotonic_seconds();
    if (app->options.record_seconds > 0.0) {
        cli_record_labels(&app->options, &basename, &technology);
        if (start_capture_record(app, basename, technology,
                                 app->options.arfcn,
                                 app->options.arfcn ? 400000.0 : 0.0,
                                 app->options.record_seconds) < 0) {
            stop_acquisition(app);
            return -1;
        }
        recording_started = 1;
    } else if (app->options.duration_seconds <= 0.0 &&
               !app->options.play_once && !app->options.survey_report) {
        fprintf(stderr, "Acquiring headless; Ctrl-C to stop.\n");
    }

    /*
     * A headless band scan is its own run, like the survey: it retunes its
     * way across a band and prints what it found. It exists for the same
     * reason the headless survey does -- the scan is otherwise a button, and
     * a button is not something a script or an agent can press (ADR-0012).
     */
    if (app->options.lte_scan_band) {
        const struct lte_band *band = NULL;
        double began = monotonic_seconds();
        int i;

        sdr_dsp_init(&app->frame.dsp);
        for (i = 0; i < lte_band_count(); i++)
            if (lte_band_at(i)->band == app->options.lte_scan_band)
                band = lte_band_at(i);
        if (!band || lte_scan_begin(app, app->options.lte_scan_band,
                                    monotonic_seconds() - began) < 0) {
            fprintf(stderr, "Cannot scan band %d.\n",
                    app->options.lte_scan_band);
            stop_acquisition(app);
            return -1;
        }
        fprintf(stderr, "Scanning band %d (%s): %d channels, about %.0f s.\n",
                band->band, band->name, lte_scan_count(band),
                lte_scan_seconds(band));
        printf("# cell <earfcn> <frequency_hz> <pci> <n_id_1> <n_id_2>"
               " <pss> <sss_margin>\n");
        while (lte_scan_running(app) && !stop_requested()) {
            struct timespec tick = { 0, 5 * 1000000L };
            struct slot_snapshot snapshot;
            double now = monotonic_seconds() - began;
            int have_new = consume_latest(&app->acq, &snapshot);
            if (have_new)
                process_block(app, now, scope_requested_fft_size(app));
            if (snapshot.worker_failed) {
                fprintf(stderr, "Acquisition failed: %s\n",
                        snapshot.worker_error);
                break;
            }
            update_lte_scan(app, now, have_new);
            if (!have_new)
                nanosleep(&tick, NULL);
        }
        for (i = 0; i < app->lte.scan.found_count; i++) {
            const struct lte_found_cell *found = &app->lte.scan.found[i];
            printf("cell %u %u %d %d %d %.3f %.3f\n", found->earfcn,
                   found->frequency_hz, found->pci, found->pci / 3,
                   found->pci % 3, (double)found->pss,
                   (double)found->sss_margin);
        }
        /* `found` is what survived; `dropped` is what the confirmation pass
           took away. Reporting only the survivors would hide the difference
           between a quiet band and a noisy one that argued and lost. */
        printf("lte-scan band %d channels %d searched %d found %d "
               "dropped %d\n",
               band->band, lte_scan_count(band), app->lte.scan.candidate,
               app->lte.scan.found_count, app->lte.scan.confirm_dropped);
        fflush(stdout);
        if (stop_acquisition(app) < 0)
            return -1;
        return 0;
    }

    /*
     * Walking the LTE chain over a live cell.
     *
     * probe-lte-chain does this for a capture, and a capture is two seconds of
     * one afternoon. What the chain does over a live cell for a minute is a
     * different question and the one that matters when it is not working: a
     * cell that decodes in half its blocks and a cell that never decodes look
     * identical in a single block, and completely different in sixty.
     *
     * A stage line per block rather than a verdict. Which stage stops is the
     * whole diagnosis -- no PSS is a tuning or a band problem, PSS without SSS
     * was the conjugated-sequence bug, SSS without parity is the broadcast
     * channel, and parity without a repeat is chance.
     */
    if (app->options.lte_chain) {
        double began, limit = app->options.lte_chain_seconds > 0.0
                                  ? app->options.lte_chain_seconds : 30.0;
        /*
         * Everything the run accumulates lives in the shared module now
         * (`.scratch/deepening/issues/14-*`): the block, cell, decode and
         * agreement counts, the per-identity tally, the primary's statistics
         * and the repeated-message rule. They were nine locals here and the
         * same nine in `scripts/lte_chain_probe.c`, which is how the two
         * drifted.
         */
        struct lte_chain_run run;

        lte_chain_run_reset(&run);
        uint32_t carrier = 0;
        int earfcn = app->options.earfcn;

        sdr_dsp_init(&app->frame.dsp);
        if (app->options.lte_chain_band) {
            printf("lte-chain scanning band %d\n", app->options.lte_chain_band);
            fflush(stdout);
            if (retune_receiver_at_rate(app, app->applied.frequency_hz,
                                        LTE_SAMPLE_RATE_HZ,
                                        app->applied.ppm) < 0 ||
                lte_scan_begin(app, app->options.lte_chain_band,
                               monotonic_seconds()) != 0) {
                fprintf(stderr, "Could not start the band scan\n");
                return -1;
            }
            while (lte_scan_running(app) && !stop_requested()) {
                struct timespec tick = { 0, 5 * 1000000L };
                struct slot_snapshot snapshot;
                int have_new = consume_latest(&app->acq, &snapshot);
                if (have_new)
                    process_block(app, monotonic_seconds(), scope_requested_fft_size(app));
                if (snapshot.worker_failed) {
                    fprintf(stderr, "Acquisition failed: %s\n",
                            snapshot.worker_error);
                    return -1;
                }
                update_lte_scan(app, monotonic_seconds(), have_new);
                if (!have_new)
                    nanosleep(&tick, NULL);
            }
            if (app->lte.scan.found_count < 1) {
                printf("lte-chain-summary blocks 0 cells 0 decoded 0 "
                       "agreed 0 reason no-cell\n");
                fflush(stdout);
                return stop_acquisition(app) < 0 ? -1 : 0;
            }
            earfcn = (int)app->lte.scan.found[0].earfcn;
        }
        if (!lte_earfcn_downlink_hz((unsigned int)earfcn, &carrier)) {
            fprintf(stderr, "EARFCN %d is not a downlink channel\n", earfcn);
            return -1;
        }
        if (retune_receiver_at_rate(app, carrier, LTE_SAMPLE_RATE_HZ,
                                    app->applied.ppm) < 0)
            return -1;
        printf("lte-chain earfcn %d carrier_hz %u rate %u ppm %d\n", earfcn,
               carrier, app->applied.sample_rate_hz, app->applied.ppm);
        printf("# chain <block> pss <corr> <runner_up> n_id_2 <n> timing <sample> "
               "offset_hz <hz> integer <subcarriers>\n");
        printf("# chain <block> sss <corr> <runner_up> n_id_1 <n> pci <n> cp "
               "<normal|extended> half_frame <0|1>\n");
        printf("# chain <block> mib ports <n> prb <n> phich <duration> "
               "<resource> sfn <n> quarter <n> combining <ports>\n");
        fflush(stdout);
        began = monotonic_seconds();

        while (!stop_requested() &&
               monotonic_seconds() - began < limit) {
            struct timespec tick = { 0, 5 * 1000000L };
            struct slot_snapshot snapshot;
            struct lte_chain_block input;
            struct lte_chain_result r;
            int have_new = consume_latest(&app->acq, &snapshot);
            int verdict, c;

            if (!have_new) {
                nanosleep(&tick, NULL);
                continue;
            }
            process_block(app, monotonic_seconds(), scope_requested_fft_size(app));
            if (snapshot.worker_failed) {
                fprintf(stderr, "Acquisition failed: %s\n",
                        snapshot.worker_error);
                return -1;
            }
            /*
             * The walk itself is `lte_chain_analysis.{c,h}` and is shared with
             * `probe-lte-chain` (`.scratch/deepening/issues/14-*`). What stays
             * here is acquisition, the duration, the stopping policy and the
             * spelling of every line below -- an adapter formats, it does not
             * decide.
             */
            input.i_samples = app->frame.i_samples;
            input.q_samples = app->frame.q_samples;
            input.pair_count = app->frame.pair_count;
            input.sample_rate_hz = (double)app->applied.sample_rate_hz;
            input.full_scale = app->device.full_scale;
            verdict = lte_chain_analyse(&run, &input, &r);
            if (verdict < 0)
                continue;   /* too short to look at, and counted as nothing */
            if (verdict == 0) {
                printf("chain %lu pss %.3f %.3f n_id_2 %d timing - "
                       "offset_hz - integer - no-cell\n", run.blocks,
                       (double)r.primary.pss_correlation,
                       (double)r.primary.pss_runner_up, r.primary.n_id_2);
                fflush(stdout);
                continue;
            }
            printf("chain %lu pss %.3f %.3f n_id_2 %d timing %zu offset_hz "
                   "%.0f integer %d\n", run.blocks,
                   (double)r.primary.pss_correlation,
                   (double)r.primary.pss_runner_up, r.primary.n_id_2,
                   r.primary.subframe0_start, r.primary.frequency_offset_hz,
                   r.primary.integer_offset);
            printf("chain %lu sss %.3f %.3f n_id_1 %d pci %d cp %s "
                   "half_frame %d\n", run.blocks,
                   (double)r.primary.sss_correlation,
                   (double)r.primary.sss_runner_up, r.primary.n_id_1,
                   r.primary.pci,
                   r.primary.extended_cp ? "extended" : "normal",
                   r.primary.half_frame);
            for (c = 0; c < r.neighbour_count; c++) {
                const struct lte_chain_neighbour *n = &r.neighbour[c];

                printf("chain %lu neighbour pci %d n_id_1 %d n_id_2 %d "
                       "at %+ld pss %.3f sss %.3f rsrp_dbfs %.1f "
                       "mib %s\n", run.blocks, n->cell.pci, n->cell.n_id_1,
                       n->cell.n_id_2, n->timing_from_primary,
                       (double)n->cell.pss_correlation,
                       (double)n->cell.sss_correlation,
                       n->have_power ? (double)n->power.rsrp_dbfs : 0.0,
                       n->mib_decoded ? "yes" : "no");
            }
            if (r.have_shape)
                printf("chain %lu channel delay_ns %.0f spread_ns %.0f "
                       "drift_hz %.0f\n", run.blocks, (double)r.shape.delay_ns,
                       (double)r.shape.delay_spread_ns,
                       (double)r.shape.drift_hz);
            if (r.have_power)
                printf("chain %lu power rsrp_dbfs %.1f rssi_dbfs %.1f "
                       "rsrq_db %.1f sinr_db %.1f blocks %d\n", run.blocks,
                       (double)r.power.rsrp_dbfs, (double)r.power.rssi_dbfs,
                       (double)r.power.rsrq_db, (double)r.power.sinr_db,
                       r.power.resource_blocks);
            if (r.have_mib) {
                /* The resource as the standard names it -- 1/6, 1/2, 1, 2 --
                   not the raw count of sixths, which reads as a different
                   number entirely. */
                const char *res =
                    lte_phich_resource_name(r.mib.phich_resource_sixths);
                printf("chain %lu mib ports %d prb %d phich %s %s sfn %d "
                       "quarter %d combining %d\n", run.blocks,
                       r.mib.antenna_ports, r.mib.bandwidth_prb,
                       r.mib.phich_extended ? "extended" : "normal",
                       res ? res : "?", r.mib.system_frame_number,
                       r.mib.quarter, r.mib_ports_combined);
            }
            fflush(stdout);
        }
        /*
         * `decoded` and `agreed`, not `parity` and `messages`.
         *
         * This line said "messages" for the agreement count while the
         * per-identity line below said "messages" for the number of blocks
         * whose broadcast decoded -- two different quantities sharing a word
         * in one report. `decoded` now means the same thing in both.
         */
        printf("lte-chain-summary blocks %lu cells %lu decoded %lu agreed "
               "%lu\n", run.blocks, run.cells, run.decoded, run.agreed);
        /*
         * And a verdict per identity, which the per-block lines cannot give.
         * Seeing an identity often is not evidence that it is a cell: the
         * search repeats its mistakes, so a sidelobe reported every block
         * looks exactly like a neighbour. What cannot be repeated into
         * existence is a broadcast channel scrambled with that identity and
         * checked by a CRC.
         */
        /*
         * What each measurement did over the run. The statistics belong to
         * whichever cell was last seen -- lte_stats_for_cell clears them when
         * the identity changes, so on a carrier that alternates these
         * describe the survivor and the `blocks` count says how few that was.
         */
        if (run.stats.valid) {
            static const struct {
                const char *name;
                size_t offset;
                const char *format;
            } columns[] = {
                { "freq_khz", offsetof(struct lte_cell_stats, frequency_khz),
                  "%+.2f" },
                { "pss", offsetof(struct lte_cell_stats, pss), "%.3f" },
                { "sss", offsetof(struct lte_cell_stats, sss), "%.3f" },
                { "rsrp_dbfs", offsetof(struct lte_cell_stats, rsrp_dbfs),
                  "%.1f" },
                { "rsrq_db", offsetof(struct lte_cell_stats, rsrq_db), "%.1f" },
                { "sinr_db", offsetof(struct lte_cell_stats, sinr_db), "%.1f" },
                { "delay_ns", offsetof(struct lte_cell_stats, delay_ns),
                  "%+.0f" },
                { "spread_ns", offsetof(struct lte_cell_stats, spread_ns),
                  "%.0f" },
                { "drift_hz", offsetof(struct lte_cell_stats, drift_hz),
                  "%+.0f" },
                { "ports", offsetof(struct lte_cell_stats, ports), "%.2f" }
            };
            unsigned c;
            for (c = 0; c < sizeof(columns) / sizeof(columns[0]); c++) {
                const struct lte_stat *st =
                    (const struct lte_stat *)((const char *)&run.stats +
                                              columns[c].offset);
                char line[160];
                int n;
                if (!st->count)
                    continue;
                n = snprintf(line, sizeof(line),
                             "lte-chain-stat pci %d %s min ", run.stats.pci,
                             columns[c].name);
                n += snprintf(line + n, sizeof(line) - (size_t)n,
                              columns[c].format, (double)st->min);
                n += snprintf(line + n, sizeof(line) - (size_t)n, " mean ");
                n += snprintf(line + n, sizeof(line) - (size_t)n,
                              columns[c].format,
                              (double)lte_stat_mean(st));
                n += snprintf(line + n, sizeof(line) - (size_t)n, " max ");
                n += snprintf(line + n, sizeof(line) - (size_t)n,
                              columns[c].format, (double)st->max);
                snprintf(line + n, sizeof(line) - (size_t)n, " blocks %lu",
                         st->count);
                printf("%s\n", line);
            }
        }
        /*
         * And what the run amounts to. The statistics above are ten rows of
         * numbers; these are the conclusions they support, and two of them
         * are refusals -- a reader who is not told the channel profile cannot
         * be named will assume nobody asked.
         */
        {
            struct lte_findings findings;
            int fi, n = lte_findings_from(&run.stats,
                                          (double)app->applied.frequency_hz,
                                          &findings);
            for (fi = 0; fi < n; fi++)
                printf("lte-chain-finding %s\n", findings.line[fi]);
        }
        {
            int t;
            for (t = 0; t < run.tally.count; t++) {
                const struct lte_cell_sighting *seen = &run.tally.cell[t];
                printf("lte-chain-cell pci %d looks %d decoded %d %s\n",
                       seen->pci, seen->looks, seen->decodes,
                       lte_cell_verdict_name(lte_cell_verdict_for(seen)));
            }
        }
        fflush(stdout);
        if (stop_acquisition(app) < 0)
            return -1;
        return 0;
    }

    /*
     * A headless calibration. Its own run, like the survey and the band scan,
     * and for the same reason: the lock gate decides whether a correction may
     * be applied, and a decision reachable only by somebody clicking Start is
     * a decision no check can reach (ADR-0012).
     *
     * It prints every measurement rather than only the verdict. The verdict is
     * one bit; the sequence is what shows *why* -- whether the residuals are
     * converging, and if they are not, whether the scatter is in the estimator
     * or in the crystal.
     */
    if (app->options.calibrate) {
        /*
         * The same walk the startup form runs, printed instead of drawn.
         *
         * `startup_session.{c,h}` owns the sequence and the vocabulary; this
         * is one of its two adapters and `overlay_startup.c` is the other.
         * The walk was written out twice before, once here and once there,
         * which is how `--lte-chain` and `probe-lte-chain` drifted twice
         * before `lte_chain_analysis` was extracted -- the same lesson,
         * applied before the drift rather than after it
         * (.scratch/startup-installation/issues/06-*).
         *
         * **No lease, on purpose**: a headless calibration exits when it is
         * done, so there is nobody to give the receiver back to. The window's
         * paths borrow because a screen outlives the measurement.
         */
        struct startup_session *session = &app->startup.session;
        struct startup_session_event ev;
        double began = monotonic_seconds();
        double limit = app->options.calibrate_seconds > 0.0
                           ? app->options.calibrate_seconds : 90.0;
        int reported = 0;
        int started;
        uint32_t pending_hz = 0;
        uint32_t pending_rate = 0;
        int retune_pending = 0;

        sdr_dsp_init(&app->frame.dsp);

        if (app->options.calibrate == 3) {
            /*
             * The machine's own search: GSM 900 first, an LTE band only if
             * nothing there carried a broadcast carrier. `--calibrate-band`
             * names the fall-back; without one the lowest band this tuner can
             * reach is used, which is what the form's picker defaults to.
             */
            int bands[LTE_BANDS_MAX];
            int count = view_lte_bands(app, bands);
            const struct lte_band *band = NULL;

            if (app->options.calibrate_band)
                band = lte_band_for_number(app->options.calibrate_band);
            else if (count > 0)
                band = lte_band_for_number(bands[0]);
            started = startup_session_begin(
                session, (double)app->applied.sample_rate_hz,
                app->applied.ppm,
                app->device.tune_lower_hz <= SCAN_BAND_LOWER_HZ &&
                    app->device.tune_upper_hz >= SCAN_BAND_UPPER_HZ,
                band, began, &ev);
            printf("calibrate searching, gsm first\n");
        } else if (app->options.calibrate == 2 &&
                   app->options.calibrate_band) {
            /*
             * Find something to calibrate against rather than being told --
             * the decode view's own band scan, because a calibration scan
             * that quietly did something simpler would find different cells
             * than the view finds on the same band.
             */
            printf("calibrate scanning band %d\n", app->options.calibrate_band);
            fflush(stdout);
            if (retune_receiver_at_rate(app, app->applied.frequency_hz,
                                        LTE_SAMPLE_RATE_HZ,
                                        app->applied.ppm) < 0 ||
                lte_scan_begin(app, app->options.calibrate_band,
                               monotonic_seconds()) != 0) {
                fprintf(stderr, "Could not start the band scan\n");
                return -1;
            }
            while (lte_scan_running(app) && !stop_requested()) {
                struct timespec tick = { 0, 5 * 1000000L };
                struct slot_snapshot snapshot;
                int have_new = consume_latest(&app->acq, &snapshot);
                if (have_new)
                    process_block(app, monotonic_seconds() - began, scope_requested_fft_size(app));
                if (snapshot.worker_failed) {
                    fprintf(stderr, "Acquisition failed: %s\n",
                            snapshot.worker_error);
                    return -1;
                }
                update_lte_scan(app, monotonic_seconds(), have_new);
                if (!have_new)
                    nanosleep(&tick, NULL);
            }
            if (app->lte.scan.found_count < 1) {
                printf("calibrate-result locked 0 reason no-cell\n");
                fflush(stdout);
                return stop_acquisition(app) < 0 ? -1 : 0;
            }
            /* Strongest first, so the head of the list is the best reference
               the band has to offer. */
            printf("calibrate chose earfcn %u cell %d pss %.2f\n",
                   app->lte.scan.found[0].earfcn, app->lte.scan.found[0].pci,
                   (double)app->lte.scan.found[0].pss);
            fflush(stdout);
            /* The budget is for the calibration, not for finding something to
               calibrate against: a band scan runs for minutes and would eat
               the whole of it before a single residual was measured. */
            began = monotonic_seconds();
            started = startup_session_measure_lte(
                session, app->lte.scan.found[0].earfcn, app->applied.ppm,
                began, &ev);
        } else if (app->options.calibrate == 2) {
            started = startup_session_measure_lte(
                session, (unsigned int)app->options.earfcn, app->applied.ppm,
                began, &ev);
        } else {
            /*
             * A named GSM channel, **with the centroid allowed**.
             *
             * This is the one place the two adapters genuinely differ, and it
             * is a real difference rather than a convenience: the startup form
             * files a correction unattended and refuses a centroid residual,
             * while here an operator is reading every residual as it arrives
             * and a reading that keeps moving between bursts is worth having.
             * The gate already treats the centroid as the weakest source, and
             * `calibration_track()` is the same state machine either way.
             */
            started = startup_session_measure_gsm(
                session, app->options.arfcn, app->applied.ppm, 1, began, &ev);
        }
        startup_session_set_budget(session, limit);

        if (started < 0) {
            fprintf(stderr, "%s\n", session->status);
            return -1;
        }
        if (ev.retune_hz) {
            pending_hz = ev.retune_hz;
            pending_rate = ev.retune_rate_hz;
            retune_pending = 1;
        }
        printf("calibrate technology %s channel %s expected_hz %u "
               "applied_ppm %d\n",
               app->options.calibrate == 1 ? "gsm"
                   : app->options.calibrate == 2 ? "lte" : "auto",
               session->arfcn > 0 ? int_text(session->arfcn)
                                  : int_text((int)session->earfcn),
               session->expected_hz, app->applied.ppm);
        fflush(stdout);

        while (!stop_requested()) {
            struct timespec tick = { 0, 5 * 1000000L };
            struct slot_snapshot snapshot;
            struct startup_block block;
            int have_new;
            double now;

            /* The tuning the machine asked for, obeyed here. The settle
               starts when the tuner has moved and not when it was asked to,
               which is the fault no capture can reach. */
            if (retune_pending) {
                int ok;

                retune_pending = 0;
                if (pending_rate)
                    ok = retune_receiver_at_rate(app, pending_hz, pending_rate,
                                                 app->applied.ppm) == 0;
                else
                    ok = retune_receiver(app, pending_hz,
                                         app->applied.ppm) == 0;
                if (ok)
                    startup_session_retuned(session, monotonic_seconds());
                else
                    startup_session_retune_failed(session, pending_hz,
                                                  app->receiver_error, &ev);
            }

            have_new = consume_latest(&app->acq, &snapshot);
            now = monotonic_seconds();
            if (have_new)
                process_block(app, now - began, scope_requested_fft_size(app));
            if (snapshot.worker_failed) {
                fprintf(stderr, "Acquisition failed: %s\n",
                        snapshot.worker_error);
                return -1;
            }

            memset(&block, 0, sizeof(block));
            block.i_samples = app->frame.i_samples;
            block.q_samples = app->frame.q_samples;
            block.pair_count = app->frame.pair_count;
            block.spectrum = app->frame.spectrum_average;
            block.scratch = app->cal.workspace;
            block.centre_hz = (double)app->applied.frequency_hz;
            block.sample_rate = (double)app->applied.sample_rate_hz;
            startup_session_tick(session, &block,
                                 have_new && app->frame.spectrum_ready, now,
                                 &ev);
            if (ev.scan_finished && session->arfcn > 0)
                printf("calibrate checking arfcn %d fcch %.2f\n",
                       session->arfcn, (double)session->arfcn_confidence);
            if (ev.candidate_rejected)
                printf("calibrate rejected arfcn %d: %s\n",
                       session->rejected_arfcn, session->rejected_why);
            if (ev.measure_began && session->verified)
                printf("calibrate verified arfcn %d bsic %d tone moved "
                       "%+.0f Hz\n", session->arfcn, session->bsic,
                       session->tone_moved_hz);
            if (ev.scan_finished && session->earfcn > 0 &&
                session->phase == STARTUP_MEASURE_LTE)
                printf("calibrate chose earfcn %u cell %d pss %.2f\n",
                       session->earfcn, session->pci, (double)session->pss);
            if (ev.measure_began) {
                printf("calibrate measuring expected_hz %u\n",
                       session->expected_hz);
                /* The budget is the measurement's, not the search's: a scan
                   runs for minutes and would eat the whole of it before a
                   single residual was measured. */
                began = monotonic_seconds();
                startup_session_set_budget(session, limit);
            }
            if (ev.retune_hz) {
                pending_hz = ev.retune_hz;
                pending_rate = ev.retune_rate_hz;
                retune_pending = 1;
            }
            if (session->track.measurements > reported) {
                reported = session->track.measurements;
                /* Every measurement, not a summary: the sequence is what shows
                   whether the scatter is the estimator or the crystal. */
                printf("cal-measure %d observed_ppm %.2f centre_ppm %.2f "
                       "sem_ppm %.2f spread_ppm %.2f source %s quality %.2f\n",
                       reported, session->expected_hz
                           ? session->offset_hz /
                                 (double)session->expected_hz * 1e6
                           : 0.0,
                       session->track.recent_center, session->track.recent_sem,
                       session->track.recent_spread,
                       session->track.source == CALIBRATION_SOURCE_FCCH
                           ? "fcch"
                           : session->track.source == CALIBRATION_SOURCE_LTE
                                 ? "lte" : "centroid",
                       (double)session->quality);
                fflush(stdout);
            }
            if (ev.finished) {
                if (session->references >= 2)
                    printf("calibrate cross-check arfcn %d %+d ppm, "
                           "arfcn %d %+d ppm\n", session->first_arfcn,
                           session->first_ppm, session->second_arfcn,
                           session->second_ppm);
                break;
            }
            if (!have_new)
                nanosleep(&tick, NULL);
        }
        printf("calibrate-result locked %d measurements %d centre_ppm %.2f "
               "sem_ppm %.2f spread_ppm %.2f suggested_ppm %d reason %s\n",
               session->phase == STARTUP_LOCKED ? 1 : 0,
               session->track.measurements, session->track.recent_center,
               session->track.recent_sem, session->track.recent_spread,
               session->suggested_ppm,
               startup_reason_name(session->reason));
        fflush(stdout);
        if (stop_acquisition(app) < 0)
            return -1;
        return 0;
    }

    /* A headless survey is its own run: it sweeps, prints, and returns, rather
       than sharing the block loop below with a decode. */
    if (app->options.survey_report) {
        int survey_result;

        sdr_dsp_init(&app->frame.dsp);
        survey_result = survey_report_run(app);
        if (stop_acquisition(app) < 0)
            survey_result = -1;
        return survey_result;
    }

    if (app->options.serve) {
        int serve_result = viewer_session_run(app);

        if (stop_acquisition(app) < 0)
            serve_result = -1;
        return serve_result;
    }

    enum decode_kind decoder = DECODE_ADSB;
    if (app->options.technology &&
        strcmp(app->options.technology, "gsm") == 0)
        decoder = DECODE_GSM;
    else if (app->options.technology &&
             strcmp(app->options.technology, "lte") == 0)
        decoder = DECODE_LTE;
    else if (app->options.technology &&
             strcmp(app->options.technology, "fm") == 0)
        decoder = DECODE_FM;
    else if (app->options.technology &&
             strcmp(app->options.technology, "tetra") == 0)
        decoder = DECODE_TETRA;
    else if (app->options.technology &&
             strcmp(app->options.technology, "srd") == 0)
        decoder = DECODE_SRD;
    if (app->options.decode)
        sdr_dsp_init(&app->frame.dsp);

    while (!stop_requested()) {
        struct timespec tick = { 0, 100 * 1000000L };
        struct slot_snapshot snapshot;
        uint64_t bytes = 0;
        char path[ACQUISITION_PATH_MAX];
        int recording = acquisition_recording_status(&app->acq, &bytes, path,
                                                     sizeof(path));

        int have_new = consume_latest(&app->acq, &snapshot);
        if (have_new && app->options.decode) {
            double now = monotonic_seconds() - started;
            /* The spectrum is not needed to decode -- process_block's return
               only says whether it updated -- but the magnitudes and centred
               I/Q it fills in are. */
            process_block(app, now, scope_requested_fft_size(app));
            if (app->frame.pair_count > 0)
                print_new_decodes(app, now, decoder);
        }
        if (snapshot.worker_failed) {
            fprintf(stderr, "Acquisition failed: %s\n", snapshot.worker_error);
            result = -1;
            break;
        }
        if (snapshot.worker_done && !recording) {
            if (app->options.decode)
                fprintf(stderr, "End of capture.\n");
            break;
        }
        if (recording_started && !recording) {
            printf("%s\n", path);
            break;
        }
        if (app->options.duration_seconds > 0.0 &&
            monotonic_seconds() - started >= app->options.duration_seconds)
            break;
        /* Only idle when there was nothing to do: a capture being decoded
           delivers blocks as fast as the pacer allows, and sleeping through
           them would drop the ones the slot overwrites. */
        if (!have_new)
            nanosleep(&tick, NULL);
    }
    if (recording_started && stop_requested())
        fprintf(stderr, "Stopped early; the capture is short but its sidecar "
                        "records what was written.\n");
    if (stop_acquisition(app) < 0)
        result = -1;
    return result;
}
