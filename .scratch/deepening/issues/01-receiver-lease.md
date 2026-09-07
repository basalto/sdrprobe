# 01 - One receiver lease instead of nine `return_frequency` fields

Status: ready-for-agent

Every screen that retunes the receiver keeps its own copy of what to put back:

| owner | fields | restore site |
| --- | --- | --- |
| `gsm` | `return_frequency`, `return_valid` | `view_gsm.c` |
| `lte` | `return_frequency`, `return_sample_rate`, `return_valid` | `view_lte.c` |
| `fm.scan` | `return_frequency`, `return_valid` | `view_fm.c` |
| `survey` | `return_frequency`, `return_valid` | `view_survey.c` |
| `survey.confirm` | `return_frequency` | `view_survey.c` |
| calibration measurement | `cal.return_frequency`, `cal_return_sample_rate` | `overlay_calibration.c` |
| automatic drift check | `cal.drift_saved_frequency` | `overlay_calibration.c` |
| GSM band scan | `bandscan.return_frequency` | `overlay_scan.c` |

Each owner can restore itself, but no shared rule proves that overlapping
owners return in the reverse order they acquired the receiver. Real overlaps
include LTE -> calibration, GSM -> band scan, Survey -> confirmation, and any
decode view -> automatic drift check. A drift check does **not** run while
calibration is open, so it is not a third level inside calibration.

## Local hypothesis

A single strict-LIFO stack of prior center-frequency/sample-rate pairs can
replace every return field without changing tuning behavior. It is false if
any reachable path must release an older owner while a newer owner remains
active.

The cheapest disproof is a pure check containing nested frequency and rate
leases, an attempted out-of-order return, and then successful reverse-order
returns. The integration cases are LTE -> calibration (rate restoration),
GSM -> band scan and Survey -> confirmation (nested frequency restoration).

## Decisions

- The lease owns **prior tuning snapshots**, not hardware state. A snapshot is
    `{ center_frequency_hz, sample_rate_hz }`.
- PPM is not leased. Applying a new calibration is intentional persistent
    state, and every current restore uses the latest `app->applied_ppm`.
- `app->applied_frequency` and `app->applied_sample_rate` remain the sole truth
    for what hardware currently reports. The lease does not duplicate them.
- A token contains an opaque generation, not a stack index alone, so a stale
    or twice-returned token cannot accidentally name a later lease.
- The stack has a small fixed capacity and refuses overflow. All current UI
    paths are at most two owners deep, but the checks exercise three levels.
- Return is two-phase: validate/peek the top snapshot, restore hardware, then
    pop only after success. A failed restore keeps the token active and retryable.
- A failed initial retune cancels the just-created token because the existing
    `retune_receiver*` functions already roll hardware back.
- Commit pops the top token without restoring. This explicitly represents
    Survey's "Open waterfall" handoff, where the selected tuning becomes the
    new baseline.
- File playback is not a second tuning adapter: it cannot retune and current
    code rejects the operation. Capture metadata establishes its frequency and
    rate. Do not invent an adapter interface until a second implementation exists.
- Keep `retune_receiver()` and `retune_receiver_at_rate()` as the sole hardware
    implementation initially. Their stop/apply/restart/rollback behavior and
    spectrum invalidation stay unchanged.

## State API

Add a raylib/librtlsdr-free `src/receiver_lease.h` containing:

- `struct receiver_tuning` -- center frequency and sample rate.
- `struct receiver_lease_token` -- opaque generation; zero means inactive.
- `struct receiver_lease` -- fixed stack, depth and next generation.
- acquire-current -- push the supplied applied tuning and issue a token.
- cancel-acquire -- pop only the matching top token after an apply failure.
- begin-return -- reject a stale/out-of-order token and expose its snapshot
    without mutating the stack.
- finish-return -- pop the same top token after hardware restoration succeeds.
- commit -- pop the matching top token without restoration.

Add application-level helpers, declared in `view.h` and implemented beside the
existing retune functions in `sdrprobe.c`:

- borrow here: snapshot the current applied frequency/rate without retuning;
- borrow and tune: snapshot, call the appropriate existing retune function,
    and cancel the token if that call fails;
- restore without release: retune to the token's snapshot but keep it active,
    for Survey returning to its entry tuning between sweeps;
- return: begin-return, restore frequency/rate with the **current** applied PPM,
    and finish-return only on success;
- commit: commit the token and clear the owner's token.

Owners may continue calling `retune_receiver()` while holding a token to sweep
or inspect several frequencies. The token records where that activity began.

## Implementation plan

### 1. Pure lease state

Create `src/receiver_lease.h` and `tests/receiver_lease_test.c`. Add the header
to `APP_HDR`, add `check-receiver-lease` with every included header named as a
prerequisite, and include the suite in `make check`/clean bookkeeping.

Checks:

- one acquire and return exposes the original tuning;
- frequency-only and rate-changing leases nest and unwind in LIFO order;
- a three-level stack unwinds exactly;
- out-of-order return, stale token, double return and overflow are refused;
- cancel removes only the newest acquisition;
- begin-return does not pop, and a later finish does;
- commit pops without requesting a restore;
- PPM is absent from the state and therefore cannot be rolled back.

Focused validation: `make check-receiver-lease`.

### 2. Application orchestration

Add `struct receiver_lease` to `struct app`, add one token to each current
owner, and implement the five helpers above. Do not remove old return fields
yet. This slice establishes failure semantics without changing a caller.

The helpers must:

- reject file playback before acquiring a token;
- choose `retune_receiver()` when the rate is unchanged and
    `retune_receiver_at_rate()` when it differs;
- validate that restore-without-release names the top token, retune to its
    snapshot, and leave the stack unchanged;
- leave the token active after a failed restore;
- clear an owner token only after finish-return or commit succeeds;
- report order violations without changing hardware or lease state.

Focused validation: `make check-receiver-lease` and build `sdrprobe`.

### 3. First nested path: GSM and band scan

Convert `enter_gsm`/`leave_gsm` to an outer token and `start_scan`/scan finish
to an inner token. `gsm_tune_selected` must stop creating ownership implicitly;
it only moves the receiver while GSM already owns it. Startup and direct calls
that tune without entering GSM must remain guarded explicitly.

This is the first behavioral discriminator: finishing the scan returns to the
GSM tuning, then leaving GSM returns to the pre-GSM tuning. Remove only the GSM
and band-scan return fields after their call sites are gone.

Focused validation: `make check-scan`, `make check-input`, and
`make check-pipelines`.

### 4. Rate path: LTE and calibration

Convert LTE entry/leave first. A failed move to 1.92 MS/s cancels its token;
leaving restores both the prior frequency and prior rate.

Give calibration a token only while a measurement owns the receiver. Acquire
immediately before GSM/LTE measurement retuning; Back or close returns it.
This preserves the existing ability to stop a measurement, remain in the
overlay, and start another. LTE calibration no longer needs
`cal_return_sample_rate`; its snapshot already contains both values. Applying
PPM changes global calibration state and must survive the return.

Focused validation: `make check-calibration`, `make check-lte-dsp`, and
`make check-pipelines`.

### 5. Remaining nested and temporary owners

Convert in this order, validating after each local edit:

1. automatic drift check;
2. Survey entry/leave;
3. Survey confirmation;
4. FM scan.

The drift check acquires only when its idle phase successfully begins and
returns when measurement completes; overlays already prevent it from starting
during calibration/settings/scan. Survey keeps its outer token while a sweep
returns temporarily to the saved tuning, because the view still owns the
right to sweep again. Confirmation gets its own nested token. FM scan gets a
single token around the scan.

Focused validation: `make check-survey-confirm`, `make check-survey-sweep`,
`make check-fm-dsp`, and `make check-pipelines` as each area moves.

### 6. Explicit Survey handoff

Replace `survey.return_valid = 0` in "Open waterfall" with lease commit after
the selected-frequency retune succeeds. If waterfall recreation fails after
the retune, keep current behavior explicit: either complete the handoff and
switch views, or return the lease and report failure; choose and pin one result
before changing that path.

Focused validation: `make check-survey`, `make check-input`, then render the
Survey and Waterfall screens because this path changes a view transition.

### 7. Remove duplicated ownership state

Delete every old `return_frequency`, `return_sample_rate`, `return_valid`,
`drift_saved_frequency` and `cal_return_sample_rate` field after grep shows no
remaining reader. Update comments and architecture documentation to name the
lease as the owner of temporary receiver tuning.

Final validation: `make check-touched`, then `make check`.

## Acceptance criteria

- No view or overlay stores its own prior frequency or sample rate.
- Every temporary retune has one active token or occurs inside an owner's
    active token.
- Returning out of order changes neither hardware nor lease state.
- Failed acquire leaves no token; failed restore leaves a retryable token.
- LTE -> calibration returns to LTE's frequency and 1.92 MS/s, then leaving
    LTE returns to the pre-LTE frequency and rate.
- GSM -> band scan and Survey -> confirmation each restore their immediate
    parent before the parent restores its own predecessor.
- Applying PPM during calibration survives closing calibration.
- "Open waterfall" keeps the selected tuning through an explicit commit.
- File playback and all decode pipeline outputs are unchanged.

## Not in scope

- Rewriting the acquisition slot or worker lifecycle.
- Changing retune rollback behavior.
- Making captures pretend to support tuning.
- A generic receiver adapter/vtable with only one real implementation.
- A uniform interface across technology DSP modules.
- Moving unrelated state out of `struct app`.
