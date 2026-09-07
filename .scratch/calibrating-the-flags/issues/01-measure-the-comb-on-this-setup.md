# 01 - Measure the comb on the receiving setup in front of you

Status: needs-triage

The comb spacing is derived from a crystal frequency compiled into
`survey_suspect.h`. Measure it instead.

**Against the receiving setup, not the device.** Three of twelve comb tones
stay when the antenna is unplugged and nine go with it, because the dongle
radiates its clock and hears itself coming back -- so most of the observable
comb depends on the antenna as much as on the crystal. The spec has the
argument; ADR-0022 has the identity.

## The procedure

Sweep the tuner's range with the antenna disconnected. What is left is the
receiver: `survey_suspect.h` already says so and the candidate panel already
tells the operator to do it by hand. Then find the spacing that best explains
those frequencies -- fit rather than assume, because the point is to stop
assuming.

The fit is a search over candidate spacings for the one whose multiples
account for the most of what the disconnected sweep found, weighted by how
strongly each stands. A spacing that explains twenty peaks is evidence; one
that explains three is arithmetic on noise, and the result has to be able to
come back **"no comb found"** rather than always returning its best guess --
which is the failure mode of every fit ever written.

## Where the answer is kept, and it is not this ticket

`.scratch/deepening/03` owns the installation module: receiver identity, the
calibration profile, the receiving setup history, one `commit()`. The comb
belongs in that profile beside the tuning correction, and this ticket does not
design a second store for it. ADR-0018 already settles the identity -- USB
serial when unique, a stable operator label otherwise, never the device index.

This ticket produces the number. Storing it, recommending it when it is
missing, and migrating what exists are all `deepening/03`.

## What has to be true of the answer before it is kept

`RECEIVER_COMB_MAX_FRACTION` already encodes the argument and it applies to a
measured spacing as much as to a compiled one: a tolerance more than a
fortieth of the spacing flags by chance rather than by evidence. A measured
comb that cannot satisfy that at the sweep's resolution is not usable, and
saying so is better than storing it.

## Two ways this goes wrong quietly

**A sweep with the antenna still connected.** The operator is being asked to
unplug something and there is no way for the program to check. What it *can*
check is whether the sweep looks like an antenna was attached -- a band II
sweep with twenty broadcast stations in it plainly did not have the antenna
off -- and refuse rather than record the air as the instrument.

**A comb that fits too well.** Every frequency is a multiple of something if
the tolerance is loose enough. The fit must report how much of the sweep it
explains and how that compares with a spacing chosen at random, which is the
same shape of evidence `probe-nbiot` demands: a detector nobody has seen fail
is not a detector.

## What must be checkable

The fit, on synthetic peak lists (ADR-0012): a clean comb at a known spacing
recovered exactly; a comb with a few real signals mixed in still recovered;
a list of real signals returning no comb; and a list of pure noise returning
no comb. The last two matter more than the first two.

Then on air -- and this is where the ticket is weakest, so it is worth saying
before anybody starts. The disconnected sweep of *this* receiver must return
14.4 MHz, because that is what was measured by hand and written into the
header. **That is a round trip**: a fit returning the constant it was meant to
replace, on the one device whose answer is already known.
`.claude/skills/dsp-validation/` says what a round trip cannot establish.

**A second dongle is the prerequisite**, ideally one whose comb differs. Until
then the fit can be shown to be self-consistent and cannot be shown to be
right, which is precisely the position this repository has twice spent months
in.
