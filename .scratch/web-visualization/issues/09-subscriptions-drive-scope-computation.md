# 09 - Viewer subscriptions drive Scope computation

Status: ready-for-agent

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