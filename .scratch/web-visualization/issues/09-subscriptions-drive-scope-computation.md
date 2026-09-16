# 09 - Viewer subscriptions drive Scope computation

Status: needs-info -- the premise holds and the cost case is spent; see "What is left to decide"

## Description

Finish ADR-0027's rule that a Viewer subscription says what to compute.

The Viewer link currently records subscriptions and suppresses publication to
clients that did not ask for a stream. That decision stops at the transport:
`viewer_session_run()` always sets `TAB_SCOPE` / `VIEW_SPECTRUM`, and
`frame_advance()` therefore runs `process_block()` and the Scope spectrum for
every consumed block even with no Viewer connected or with only
`receiver_state` / `link_health` requested.

This is an ownership leak between the frontend adapter and frame advancement.
The Viewer link knows demand but cannot expose its aggregate; frame advancement
owns computation but can only infer it from native tab state. Filtering a
State update after paying for its DSP is not subscription-driven computation.

Deepen the existing `frame_advance` module with one small, plain demand value.
The raylib adapter derives demand from its active screen; the Viewer adapter
derives demand from the union of connected subscriptions. For the streams that
exist today, spectrum and waterfall require Scope block processing, while
receiver state and link health do not. Acquisition still consumes the latest
block when no Scope measurement is requested, preserving ADR-0002's freshness
rule instead of allowing the slot to back up.

## Plan

1. Expose the union of open clients' subscriptions from `viewer_link` as a
   read-only demand query. Resubscription replaces one client's set, and a
   disconnect removes that client's demand.
2. Add a plain frame-advance demand value that distinguishes consuming a block
   from performing Scope measurements. Keep technology dispatch explicit;
   this is not a uniform technology interface.
3. Have the window adapter derive the same demand it uses today from its active
   tab, view and overlays, preserving native behavior exactly.
4. Have `viewer_session` derive demand from the Viewer link before advancing
   each block. Spectrum or waterfall demand enables the existing Scope path;
   metadata-only or zero demand consumes and discards the block without FFT or
   waterfall advancement.
5. Check the two interfaces independently, then exercise the assembled serving
   path and compare native capture output.

## Tasks

- [ ] Add a `viewer_link` query for aggregate stream demand across all open
      clients; do not expose `struct viewer_client` or its subscription array.
- [ ] Check zero clients, one subscription, replacement by resubscription,
      two clients with different subscriptions, and disconnect cleanup.
- [ ] Add a plain demand argument to `frame_advance()` and update its contract.
- [ ] Preserve latest-block consumption when Scope measurement demand is off.
- [ ] Skip `process_block()`, spectrum decay and waterfall advancement when a
      headless Viewer requests no block-derived Scope stream.
- [ ] Treat either spectrum or waterfall subscription as Scope measurement
      demand; receiver state and link health alone must not enable it.
- [ ] Derive native-window demand from the existing screen state without
      changing decoder, survey, calibration or startup dispatch.
- [ ] Extend `check-frame-advance` with fakes proving demand-off consumes one
      block and performs zero Scope measurement calls, while demand-on performs
      the existing calls once.
- [ ] Extend `check-viewer-link` to pin the aggregate-demand lifecycle.
- [ ] Run the serving path with no client, metadata-only demand and spectrum
      demand; record the observed work counters or debug evidence in Comments.
- [ ] Run `make check-frame-advance check-viewer-link check-scope-view-model`
      while iterating, then `make check-touched` and `make check-pipelines`.

## Acceptance criteria

- [ ] With no Viewer connected, `--serve` consumes incoming blocks but performs
      no Scope spectrum or waterfall work.
- [ ] A Viewer subscribed only to `receiver_state` and/or `link_health` causes
      no Scope spectrum or waterfall work.
- [ ] The first spectrum or waterfall subscriber enables the existing Scope
      measurement path on the next block; removing the last such subscription
      disables it on the next block.
- [ ] Demand is the union across clients: one client's resubscription or
      disconnect cannot disable work still requested by another client.
- [ ] The native window's per-block dispatch and rendered screens are unchanged.
- [ ] Latest-block freshness, replaceable State updates and reliable Viewer
      commands keep their existing behavior.
- [ ] No raylib type enters `frame_advance`, `viewer_link` or the demand value.
- [ ] No uniform technology interface or registry is introduced.
- [ ] Focused checks, `make check-touched` and `make check-pipelines` pass.

## Not in scope

- Adding another State update or Viewer command.
- Migrating another native view to a view model.
- Computing every decoder for every connected Viewer.
- Retiring the raylib window.

## Comments

Opened from the 2026-09-16 architecture review. Ticket 05 implemented
subscription-aware publication and documented the stronger rule, but its
public interface has no way for `viewer_session` or `frame_advance` to learn
what is subscribed. The deletion test is positive: without demand at the
frame-advance seam, each frontend must impersonate native tab state or pay for
all work and discard the result afterward.

**2026-09-16, read against the code before building it.** The premise holds:
`viewer_session_run()` pins `TAB_SCOPE` / `VIEW_SPECTRUM` and calls
`frame_advance()` unconditionally, so `process_block()` and the waterfall run
with nothing connected. Three amendments before anybody starts.

**The native-window adapter cannot work as this describes, and the task should
go.** Plan step 3 and its task ask the window to derive demand from its active
screen. There is no screen that wants the block unconverted: every decode view
reads what `process_block()` fills -- `view_gsm.c:103`, `view_tetra.c:76`,
`view_srd.c:148`, `view_lte.c:469`, `view_adsb.c:127`, `view_fm.c:78` -- and
`view_survey.c:85-89` reads those *and* `spectrum_average`. Window demand can
only ever be on, which this ticket's own "rendered screens are unchanged"
criterion forces anyway. Say that out loud and let the window pass full demand,
rather than building an adapter that models a choice it does not have.

The split that would pay the window is one level lower, inside
`signal_frame_process()`: conversion, magnitudes and statistics, which every
screen needs, against the transform, peak hold and waterfall row, which the
five decode tabs do not. That is a real saving and a larger change than this
ticket describes -- worth its own, and it is where "a subscription says what to
compute" would generalise past the Scope.

**Measure the payoff first.** Nothing here says what idle `--serve` costs.
`CLAUDE.md` puts the whole Scope path at about 7 ms of a 65.5 ms block. If that
is what this saves, it is an ownership fix and should be argued as one rather
than as a performance fix -- which is also the repository's own rule
(`does-it-help`): the acceptance criteria ask for work counters after the
change, and the number that decides whether to make it is the one before.

**Nothing pins what has to survive demand-off.** With Scope demand off,
`frame_advance()` returns `spectrum_updated = 0` while `receiver_state`,
`link_health` and `command_result` are still published every iteration. A
`tune` command with no spectrum subscriber must still retune and publish the
new generation. Add that to the acceptance criteria; it is the case where
switching work off quietly switches control off with it.

~~Checked and not a risk: demand-off does not busy-spin.
`viewer_session.c:165` paces every iteration on `viewer_link_poll()`.~~

**Struck 2026-09-16, and it is the reason this ticket is now second.** That was
read off the source and never measured, and it is false in exactly the
configuration this ticket is about. Measured: a client subscribed to
`receiver_state` alone drives the serve loop to over 1700 publishes a second
and **98.6% of a core**, against 10.0% for a `spectrum` subscriber and 9.8%
for no client at all. The whole Scope path this ticket proposes to skip is the
9.8%; the metadata-only case it treats as the cheap one costs ninety.

That is ticket 10, and the payoff measurement this ticket asked for is in it.
Re-argue this one against those numbers once the loop is paced -- the case for
it is ownership, which stands, not cost, which was never checked.

### The measurement this ticket asked for, 2026-09-16

Two independent routes, and they agree.

**`make bench-dsp`, per 65.5 ms block, 2048-point transform**, which is the
Scope path stage by stage:

| stage | ms/block | of the budget |
| --- | --- | --- |
| byte -> I/Q + magnitude | 0.096 | 0.15% |
| signal statistics (two percentiles) | 0.949 | 1.45% |
| magnitude peak bins | 0.120 | 0.18% |
| DC removal (only when Remove DC is on) | 0.222 | 0.34% |
| spectrum: 64 x 2048-point FFT | **4.789** | **7.31%** |

With DC removal off, which is the default, the whole Scope path is 5.954 ms --
**9.09% of a block**. Measured `--serve` CPU with no client is 9.4-9.6%, so the
two agree to within half a point and acquisition plus the loop is the
remainder. **The transform is 80% of it.**

**And in situ**, sweeping the transform size against `--serve` CPU with no
client, 20 s windows:

| transform | CPU |
| --- | --- |
| 256 | 8.2% |
| 1024 | 8.7% |
| 4096 | 9.8% |
| 16384 | 12.5% |

4.3 points of spread across the sizes, on a total of 8-12, which is the same
conclusion from the other end: the transform is the dominant term and
everything every screen needs is about 2% of a block. (The rise is steeper
than the `N log2 N` the transform should follow -- +0.5, +1.1, +2.7 points per
two octaves. Cache, probably, at 16384 points; not established, and not needed
for this decision.)

**So the saving is real and it is small**: about 9% of one core, headless,
when a Viewer asked for no spectrum -- a configuration that after ticket 10
costs 9.4% in total.

### And the "larger change that would pay the window" is mostly not there

The comment above claimed the transform is what "the five decode tabs do not"
need. **That is wrong, and it is the second claim in this ticket to fail on
contact with the code.** Every decode view draws a waterfall, which is the
spectrum: `view_gsm.c:389`, `view_adsb.c:412`, `view_fm.c:886`,
`view_srd.c:692`, `view_tetra.c:276`, `view_lte.c:1067`, all through
`draw_waterfall_rect_with_markers()`.

What is true is narrower. Each of those draws is gated on `!analysis_mode`
(e.g. `view_tetra.c:259`), so a decode view in its **analysis arrangement**
draws no waterfall and needs no transform. That is a real demand distinction,
but it is a per-arrangement one a reader toggles, not a per-tab one -- so the
`signal_frame` split would pay 7.3% of a block only while somebody is looking
at charts instead of a log, rather than on every decode tab as claimed.

The window still has no screen that wants the block unconverted, and now it
also has no *default* screen that wants it untransformed.

## What is left to decide

The premise holds and the mechanism is sound; what is open is whether it is
worth building, and that is not a question this ticket can answer about
itself.

1. **Build, narrow, or defer.** The cost argument is spent: ~9% of one core,
   headless only. The ownership argument stands and is the only one left --
   ADR-0027 and `viewer_link.h:26` both state "a subscription says what to
   compute", and that is currently false, since the link gates sending rather
   than computing. Either implement it, or narrow both statements to "a
   subscription says what is sent" and close this.
2. **Or wait for ticket 07.** ADR-0027 justifies the rule by "the block budget
   does not allow computing every technology for every Viewer" -- which is
   about decoders. Only the Scope is served today and it is cheap, so the case
   that motivates the rule does not exist yet. It appears the moment a decode
   view is served, where `bench-dsp` above puts one GSM SCH decode at 14.2 ms
   a block against the whole Scope path's 6.0.
3. **If built, the window passes a constant.** Drop plan step 3 and its task,
   or keep it only as far as the analysis arrangement above, and say which.
4. **Add the control criterion**: a `tune` with no spectrum subscriber must
   still retune and publish the new generation. Note that ticket 10 has since
   changed that loop -- `receiver_state` is paced on a heartbeat plus a
   generation change now -- so the interaction wants checking rather than
   assuming.

Recommendation: **2**, with these numbers recorded so whoever picks it up
starts from them rather than from the cost argument that is now spent.
