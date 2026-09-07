#!/usr/bin/env python3
"""Turn a band survey's output into something that accumulates.

`--headless --survey` prints one line per candidate and then a total. That is
the right shape to read once and the wrong shape to keep: the interesting
question is not what is transmitting now but what has changed since last time,
and answering that means the surveys have to be comparable and kept.

    ingest   survey output  -> surveys/<date>-<range>.json
    report   one survey     -> what is on air, grouped by allocation
    diff     two surveys    -> what appeared, what went, what moved

The band plan's own name for a frequency is the grouping key, which is what
makes a survey readable at all: 247 candidates is a list, "22 of them FM
broadcast and 88 in gaps in the band plan" is a finding. Candidates the survey
flagged as resembling the receiver are kept and counted, never dropped --
ADR-0015's position, and the survey's own.
"""
import argparse
import datetime
import json
import os
import re
import sys

SCHEMA = 1
HERE = os.path.dirname(os.path.abspath(__file__))
SURVEYS = os.path.join(os.path.dirname(HERE), "surveys")

# Two candidates this close are the same signal seen in two sweeps. The sweep
# bins at about 200 kHz and a peak wanders within its bin, so anything tighter
# reports a drift that is really a rounding difference.
SAME_SIGNAL_HZ = 300000.0
# A level change smaller than this is the weather, the gain, or where the
# dongle was pointing; larger is worth a look.
NOTABLE_DB = 8.0


def parse(text):
    """Survey output (stdout, optionally with stderr mixed in) to a record."""
    out = {
        "schema": SCHEMA,
        "recorded_at": datetime.datetime.now().replace(microsecond=0).isoformat(),
        "range_hz": None,
        "sweep": {},
        "receiver": {},
        "site": {},
        "totals": {},
        # Whether anybody asked again, and what they found. Written even when
        # no pass ran: "asked 0" and a missing block are different facts, and
        # a reader has to tell a signal that held up from one nobody checked.
        "confirmation": {"asked": 0, "confirmed": 0, "intermittent": 0,
                         "refuted": 0, "targets": []},
        "candidates": [],
        "carriers": [],
    }
    kinds = {}
    for line in text.splitlines():
        f = line.split()
        if not f:
            continue
        if line.startswith("candidate ") and len(f) >= 10:
            # extent_hz is the width in the sweep's own bins and `resolved`
            # says whether that width means anything: at 212 kHz a bin a
            # 25 kHz carrier and a 3 kHz spur are both one bin, and the
            # number is a floor. Older output has neither field.
            flags = [] if f[8] == "-" else f[8].split(",")
            allocation = " ".join(f[9:])
            out["candidates"].append({
                "hz": int(float(f[1])),
                "dbfs": float(f[2]),
                "prominence_db": float(f[3]),
                "centre_hz": None if f[4] == "-" else int(float(f[4])),
                "width_hz": None if f[5] == "-" else int(float(f[5])),
                "extent_hz": int(float(f[6])),
                "resolved": f[7] == "resolved",
                "flags": flags,
                "allocation": None if allocation == "-" else allocation,
            })
        elif line.startswith("candidate ") and len(f) >= 8:
            flags = [] if f[6] == "-" else f[6].split(",")
            allocation = " ".join(f[7:])
            out["candidates"].append({
                "hz": int(float(f[1])),
                "dbfs": float(f[2]),
                "prominence_db": float(f[3]),
                "centre_hz": None if f[4] == "-" else int(float(f[4])),
                "width_hz": None if f[5] == "-" else int(float(f[5])),
                "extent_hz": None,
                "resolved": None,
                "flags": flags,
                "allocation": None if allocation == "-" else allocation,
            })
        elif f[:2] == ["survey", "range"] and len(f) >= 4:
            out["range_hz"] = [int(f[2]), int(f[3])]
        elif f[:2] == ["survey", "steps"]:
            pairs = dict(zip(f[1::2], f[2::2]))
            for key, cast in (("steps", int), ("bins", int), ("bin_hz", float),
                              ("dwell", float)):
                if key in pairs:
                    out["sweep"][key if key != "dwell" else "dwell_s"] = cast(pairs[key])
        elif line.startswith("carrier ") and len(f) >= 10:
            allocation = " ".join(f[9:])
            out["carriers"].append({
                "centre_hz": int(float(f[1])),
                "power_centre_hz": int(float(f[2])),
                "lower_hz": int(float(f[3])),
                "upper_hz": int(float(f[4])),
                "width_hz": int(float(f[5])),
                "dbfs": float(f[6]),
                "prominence_db": float(f[7]),
                "maxima": int(f[8]),
                "allocation": None if allocation == "-" else allocation,
            })
        elif f[:2] == ["survey", "antenna"]:
            out["receiver"]["antenna"] = " ".join(f[2:])
        elif f[:2] == ["survey", "site"]:
            out["site"]["label"] = " ".join(f[2:])
        elif f[:2] == ["survey", "gain"]:
            out["receiver"]["gain_db"] = float(f[2])
        elif f[:2] == ["survey", "carriers"]:
            out["totals"]["carriers"] = int(f[2])
        elif f[:2] == ["survey", "blocks"]:
            out["sweep"]["blocks"] = int(f[2])
            # Blocks dropped because the tuner was still settling. Worth
            # keeping: it is a third of a short dwell, and a sweep that folded
            # them measured the previous step at this step's frequencies.
            if "settling" in f:
                out["sweep"]["settling"] = int(f[f.index("settling") + 1])
        elif line.startswith("confirm ") and len(f) >= 5:
            target = {
                "hz": int(float(f[1])),
                "claim": f[2],
                "verdict": f[3],
                "prominence_db": float(f[4]),
            }
            # hits/looks: the count behind the verdict, so a reader can tell
            # one burst in six from five. Optional, because sweeps recorded
            # before the third verdict existed do not carry it.
            if len(f) >= 6 and "/" in f[5]:
                hits, _, looks = f[5].partition("/")
                target["hits"], target["looks"] = int(hits), int(looks)
            # The width and the flags the *pass* measured, at 244 Hz where the
            # sweep could only see 212 kHz bins. Both were being read past and
            # dropped, which threw away the only honest measurement of either.
            if len(f) >= 7:
                target["width_hz"] = int(float(f[6]))
            # Always written, null when there are none. Omitting the field
            # rather than writing null made the same sweep saved two ways
            # carry different keys, and a reader comparing two files cannot
            # tell a field a writer skipped from one the data never had.
            if len(f) >= 8:
                target["flags"] = None if f[7] == "-" else f[7]
            out["confirmation"]["targets"].append(target)
        elif line.startswith("kind ") and len(f) >= 8:
            # What kind of thing the pass found. Kept against the frequency
            # rather than appended to the last target, so a reordering of the
            # output cannot attribute one signal's kind to another.
            kinds[int(float(f[1]))] = {
                # The carrier verdict is several words, and the numbers after
                # it are fixed in count, so it is read from the end.
                "carrier": " ".join(f[2:-5]),
                "over_noise_db": float(f[-5]),
                "standing_share": float(f[-4]),
                "envelope": float(f[-3]),
                "bursts": f[-2],
                "occupancy": float(f[-1]),
            }
        elif f[:1] == ["confirm-summary"]:
            pairs = dict(zip(f[1::2], f[2::2]))
            for key in ("asked", "confirmed", "intermittent", "refuted"):
                if key in pairs:
                    out["confirmation"][key] = int(pairs[key])
        elif f[:2] == ["survey", "candidates"]:
            pairs = dict(zip(f[1::2], f[2::2]))
            for key in ("candidates", "suspicious"):
                if key in pairs:
                    out["totals"][key] = int(pairs[key])
        else:
            tuner = re.match(r"Found (.+) tuner", line)
            if tuner:
                out["receiver"]["tuner"] = tuner.group(1)
    # Attach each kind to the carrier it describes and to the target that
    # asked, by nearest frequency. On the carrier because that is what `diff`
    # compares and what the history remembers; on the target because that is
    # where the rest of the pass's answer lives.
    # A carrier's verdict, and a candidate's from the carrier it belongs to.
    #
    # The C writer records this on both and this one did not, so the same
    # sweep saved two ways carried different fields -- and the shape a reader
    # gets depended on which button was pressed. A candidate takes its
    # carrier's verdict because the pass asks about carriers: a station's own
    # shoulders are maxima of the same signal a few bins away, and reading
    # each of those as "never asked" would leave most of a confirmed station
    # marked unconfirmed.
    verdicts = {t["hz"]: t["verdict"] for t in out["confirmation"]["targets"]}

    def verdict_at(hz):
        if not verdicts:
            return "unconfirmed"
        near = min(verdicts, key=lambda k: abs(k - hz))
        return verdicts[near] if abs(near - hz) <= SAME_SIGNAL_HZ \
            else "unconfirmed"

    for carrier in out["carriers"]:
        carrier["confirmed"] = verdict_at(carrier["centre_hz"])
    for candidate in out["candidates"]:
        # Through the carrier holding it, when one does; the candidate's own
        # frequency otherwise.
        holder = None
        for carrier in out["carriers"]:
            if carrier["lower_hz"] <= candidate["hz"] <= carrier["upper_hz"]:
                holder = carrier
                break
        candidate["confirmed"] = holder["confirmed"] if holder \
            else verdict_at(candidate["hz"])

    for group in (out["carriers"], out["confirmation"]["targets"]):
        for item in group:
            hz = item.get("centre_hz", item.get("hz"))
            if hz is None or not kinds:
                continue
            near = min(kinds, key=lambda k: abs(k - hz))
            if abs(near - hz) <= KIND_MATCH_HZ:
                item["kind"] = kinds[near]
    return out


# How far a `kind` line may sit from the carrier it describes. The pass asks
# at a carrier's measured centre and reports at the same number, so this is
# slack for rounding rather than for searching.
KIND_MATCH_HZ = 2000


# Obfuscation, not secrecy: enough that a survey file does not carry the
# identifiers of the networks around it, while still comparing to the next one.
# The files are local and gitignored; do not treat these as secret.
FINGERPRINT_SALT = b"sdrprobe survey site v1"
FINGERPRINT_KEEP = 12


def fingerprint():
    """A hash per visible WiFi network, as a check on the site label.

    Not a location. It answers only "is this the same place as last time",
    which is the question that matters -- coordinates would not distinguish
    two spots in one room that differ by 20 dB, and the visible networks do.
    Returns None when there is no way to look, which is not an error.
    """
    import hashlib
    import subprocess
    try:
        out = subprocess.run(
            ["nmcli", "-t", "-f", "BSSID", "device", "wifi", "list",
             "--rescan", "no"],
            capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return None
    if out.returncode != 0:
        return None
    seen = set()
    for line in out.stdout.splitlines():
        bssid = line.replace("\\", "").strip().upper()
        if len(bssid) < 17:
            continue
        digest = hashlib.sha256(FINGERPRINT_SALT + bssid.encode()).hexdigest()
        seen.add(digest[:FINGERPRINT_KEEP])
    return sorted(seen) or None


def overlap(a, b):
    """How much two fingerprints share, 0 to 1. Compared as sets rather than
    by equality, because two or three networks come and go between any two
    scans and an exact match would almost never happen."""
    if not a or not b:
        return None
    sa, sb = set(a), set(b)
    return len(sa & sb) / float(len(sa | sb))


def name_for(record):
    """`2026-09-02-185703-24M-1766M.json`.

    The time is in the name because the date was not enough: four sweeps of
    the whole tuner in one day left one file, each silently overwriting the
    last. A directory whose whole purpose is that sweeps accumulate must not
    lose them for being taken on the same afternoon.
    """
    lo, hi = record.get("range_hz") or [0, 0]
    stamp = record["recorded_at"][:19].replace("-", "").replace(":", "")
    day = "%s-%s-%s-%s" % (stamp[0:4], stamp[4:6], stamp[6:8], stamp[9:15])
    return "%s-%s-%s.json" % (day, mhz(lo), mhz(hi))


def mhz(hz):
    return ("%gM" % (hz / 1e6))


def load(path):
    with open(path) as handle:
        return json.load(handle)


def by_allocation(record):
    groups = {}
    for c in record["candidates"]:
        groups.setdefault(c["allocation"] or "(no band plan entry)", []).append(c)
    return groups


def cmd_ingest(args):
    text = open(args.file).read() if args.file else sys.stdin.read()
    record = parse(text)
    if not record["candidates"]:
        sys.exit("no candidate lines found; is this survey output?")
    if args.note:
        record["note"] = args.note
    if args.gain is not None:
        record["receiver"]["gain_db"] = args.gain
    if not args.no_fingerprint:
        marks = fingerprint()
        if marks:
            record["site"]["fingerprint"] = marks
    os.makedirs(SURVEYS, exist_ok=True)
    path = args.out or os.path.join(SURVEYS, name_for(record))
    if not args.out:
        # Never over one already there. A sweep costs minutes and cannot be
        # recovered; two in the same second is unlikely and this makes losing
        # one impossible.
        stem, attempt = path[:-5], 2
        while os.path.exists(path) and attempt < 100:
            path = "%s-%d.json" % (stem, attempt)
            attempt += 1
    with open(path, "w") as handle:
        json.dump(record, handle, indent=1, sort_keys=True)
        handle.write("\n")
    print("%s: %d candidates, %d flagged as the receiver's own"
          % (path, len(record["candidates"]),
             record["totals"].get("suspicious", 0)))


def kind_lines(record):
    """One line per signal the confirmation pass measured the kind of."""
    rows = []
    for c in record.get("carriers") or []:
        kind = c.get("kind")
        if not kind:
            continue
        rows.append((c, kind))
    return rows


def cmd_report(args):
    record = load(args.survey)
    lo, hi = record["range_hz"]
    flagged = [c for c in record["candidates"] if c["flags"]]
    clean = [c for c in record["candidates"] if not c["flags"]]
    site = record.get("site", {})
    rx = record.get("receiver", {})
    print("%s  %s to %s  %s" % (args.survey, mhz(lo), mhz(hi),
                                record["recorded_at"]))
    print("  site %s   antenna %s%s"
          % (site.get("label") or "UNSET",
             rx.get("antenna") or "unrecorded",
             "   gain %.1f dB" % rx["gain_db"] if rx.get("gain_db") else ""))
    if not site.get("label"):
        print("  ! no site. Levels cannot be compared with another sweep;"
              " set one with --site.")
    carriers = record.get("carriers") or []
    print("  %d candidates, %d clean, %d resembling the receiver"
          % (len(record["candidates"]), len(clean), len(flagged)))
    if carriers:
        merged = sum(1 for c in carriers if c.get("maxima", 1) > 1)
        print("  grouped into %d signals%s" % (
            len(carriers),
            ", %d of them holding several maxima" % merged if merged else ""))
    # How much of this the sweep could actually resolve. A bin is the span
    # over the transform size, so a full-tuner sweep puts 212 kHz in one and
    # anything narrower than that comes back as one or two bins whatever it
    # is. Saying so beside the count is the difference between a list of
    # signals and a list of places where something might be.
    # What kind of thing the confirmation pass found. Only signals it asked
    # about have one, so this is a subset of the list above rather than a
    # summary of it.
    kinds = kind_lines(record)
    if kinds:
        bare = [(c, k) for c, k in kinds if "bare" in k["carrier"]]
        print("  the pass measured what kind of thing %d of them were%s"
              % (len(kinds),
                 ", and %d %s nothing"
                 % (len(bare), "carries" if len(bare) == 1 else "carry")
                 if bare else ""))
        for c, k in sorted(bare, key=lambda p: -p[0]["dbfs"])[:6]:
            # Worth its own line: strong, confirmed, continuous and with
            # nothing riding it is the answer that saves somebody an
            # afternoon, and it is what 75.000 MHz turned out to be.
            print("    %10.3f MHz  %6.1f dBFS  bare carrier, %.0f%% of the"
                  " channel standing still  %s"
                  % (c["centre_hz"] / 1e6, c["dbfs"],
                     k["standing_share"] * 100.0,
                     c.get("allocation") or "no entry"))
    bin_hz = (record.get("sweep") or {}).get("bin_hz")
    floors = [c for c in record["candidates"] if c.get("resolved") is False]
    unknown = sum(1 for c in record["candidates"] if c.get("resolved") is None)
    known = len(record["candidates"]) - unknown
    if bin_hz and known:
        if floors:
            print("  %.1f kHz to a bin, so %d of %d are at the resolution"
                  " floor -- their widths are lower bounds, not measurements"
                  % (bin_hz / 1e3, len(floors), known))
        else:
            print("  %.1f kHz to a bin, and every candidate is wider than it"
                  % (bin_hz / 1e3))
    elif bin_hz:
        # Saying "every candidate is wider than it" here would be a claim
        # about data the file does not contain.
        print("  %.1f kHz to a bin; this sweep predates the extent being"
              " recorded, so how much of it was resolvable is unknown"
              % (bin_hz / 1e3))
    if unknown and known:
        print("  (%d of them predate the extent being recorded)" % unknown)
    # A sweep step is a tenth of a second, so its marks are claims. Whether
    # anybody went back and looked belongs beside the count, not buried.
    confirmation = record.get("confirmation") or {}
    if confirmation.get("asked"):
        print("  asked again about %d: %d held up, %d came and went, %d did not"
              % (confirmation["asked"], confirmation.get("confirmed", 0),
                 confirmation.get("intermittent", 0),
                 confirmation.get("refuted", 0)))
    else:
        print("  ! nobody asked again. Every signal here is one sweep's word;"
              " re-run with --survey-confirm.")
    print("")
    groups = by_allocation({"candidates": clean})
    print("  %-38s %4s %10s %10s  %s" % ("allocation", "n", "best dBFS",
                                         "prominence", "at"))
    for allocation, members in sorted(
            groups.items(), key=lambda kv: -max(m["dbfs"] for m in kv[1])):
        best = max(members, key=lambda m: m["dbfs"])
        print("  %-38s %4d %10.1f %10.1f  %.3f MHz"
              % (allocation[:38], len(members), best["dbfs"],
                 best["prominence_db"], best["hz"] / 1e6))
    gaps = groups.get("(no band plan entry)", [])
    if gaps:
        print("\n  strongest with no band plan entry:")
        for c in sorted(gaps, key=lambda m: -m["dbfs"])[:args.top]:
            print("    %10.3f MHz  %6.1f dBFS  prominence %4.1f"
                  % (c["hz"] / 1e6, c["dbfs"], c["prominence_db"]))


def nearest(candidate, pool):
    best, gap = None, SAME_SIGNAL_HZ
    for other in pool:
        d = abs(other["hz"] - candidate["hz"])
        if d < gap:
            best, gap = other, d
    return best


def cmd_diff(args):
    old, new = load(args.old), load(args.new)
    o_clean = [c for c in old["candidates"] if not c["flags"]]
    n_clean = [c for c in new["candidates"] if not c["flags"]]
    print("%s -> %s" % (old["recorded_at"], new["recorded_at"]))

    # Two sweeps from different places are not a before and after. A diff
    # across them reports the move as though the band had changed, which is
    # the one way this archive can mislead rather than merely disappoint.
    o_site = old.get("site", {}).get("label")
    n_site = new.get("site", {}).get("label")
    if not o_site or not n_site:
        print("  ! one of these has no site recorded, so this comparison"
              " cannot be trusted.")
    elif o_site != n_site:
        message = ("these are different places: %r and %r" % (o_site, n_site))
        if not args.force:
            sys.exit("  refusing: %s.\n"
                     "  A sweep somewhere else is not a baseline. Pass --force"
                     " if you know why you want this." % message)
        print("  ! forced across sites: %s" % message)
    else:
        print("  site %s" % o_site)
        share = overlap(old.get("site", {}).get("fingerprint"),
                        new.get("site", {}).get("fingerprint"))
        if share is not None and share < 0.5:
            print("  ! both are labelled %r but the networks around them"
                  " share only %.0f%%.\n    One of these was probably taken"
                  " somewhere else." % (o_site, share * 100))

    o_ant = old.get("receiver", {}).get("antenna")
    n_ant = new.get("receiver", {}).get("antenna")
    if o_ant and n_ant and o_ant != n_ant:
        print("  ! antenna differs, %r then %r. Levels are not comparable."
              % (o_ant, n_ant))
    o_lo, o_hi = old["range_hz"]
    n_lo, n_hi = new["range_hz"]
    lo, hi = max(o_lo, n_lo), min(o_hi, n_hi)
    if lo >= hi:
        sys.exit("the two surveys do not overlap in frequency")
    if (o_lo, o_hi) != (n_lo, n_hi):
        print("  comparing only where they overlap: %s to %s" % (mhz(lo), mhz(hi)))
    # Sensitivity is not a property of the band. A longer dwell finds weaker
    # peaks, so a diff across two dwells reports the difference between the
    # sweeps as though it were a difference in the air.
    o_dwell = old["sweep"].get("dwell_s")
    n_dwell = new["sweep"].get("dwell_s")
    if o_dwell and n_dwell and abs(o_dwell - n_dwell) > 1e-9:
        print("  ! dwell differs, %.2f s then %.2f s. The longer sweep sees"
              " deeper,\n    so much of what follows is sensitivity rather than"
              " change." % (o_dwell, n_dwell))
    o_gain = old.get("receiver", {}).get("gain_db")
    n_gain = new.get("receiver", {}).get("gain_db")
    if o_gain is not None and n_gain is not None and o_gain != n_gain:
        print("  ! gain differs, %g dB then %g dB. Levels are not comparable."
              % (o_gain, n_gain))
    o_clean = [c for c in o_clean if lo <= c["hz"] <= hi]
    n_clean = [c for c in n_clean if lo <= c["hz"] <= hi]

    # A change of kind is the comparison this archive exists for, and it is
    # not visible in any of the numbers below: a carrier that was bare in one
    # sweep and modulated in the next has the same frequency, much the same
    # level and much the same width. Only sweeps that both asked can be
    # compared, so a file recorded before the pass measured kind contributes
    # nothing here rather than reading as a change.
    kind_changes = []
    o_carriers = [c for c in (old.get("carriers") or [])
                  if lo <= c["centre_hz"] <= hi and c.get("kind")]
    n_carriers = [c for c in (new.get("carriers") or [])
                  if lo <= c["centre_hz"] <= hi and c.get("kind")]
    for c in n_carriers:
        was = min(o_carriers,
                  key=lambda w: abs(w["centre_hz"] - c["centre_hz"]),
                  default=None)
        if not was or abs(was["centre_hz"] - c["centre_hz"]) > SAME_SIGNAL_HZ:
            continue
        if was["kind"]["carrier"] != c["kind"]["carrier"]:
            kind_changes.append((c, was))

    appeared = [c for c in n_clean if not nearest(c, o_clean)]
    gone = [c for c in o_clean if not nearest(c, n_clean)]
    moved = []
    for c in n_clean:
        was = nearest(c, o_clean)
        if was and abs(c["dbfs"] - was["dbfs"]) >= NOTABLE_DB:
            moved.append((c, was))

    def show(title, rows, fmt):
        print("\n  %s (%d)" % (title, len(rows)))
        if not rows:
            print("    none")
        for row in rows[:args.top]:
            print(fmt(row))
        if len(rows) > args.top:
            print("    ... and %d more" % (len(rows) - args.top))

    show("appeared", sorted(appeared, key=lambda c: -c["dbfs"]),
         lambda c: "    %10.3f MHz  %6.1f dBFS  %s"
                   % (c["hz"] / 1e6, c["dbfs"], c["allocation"] or "no entry"))
    show("gone", sorted(gone, key=lambda c: -c["dbfs"]),
         lambda c: "    %10.3f MHz  %6.1f dBFS  %s"
                   % (c["hz"] / 1e6, c["dbfs"], c["allocation"] or "no entry"))
    show("changed by %g dB or more" % NOTABLE_DB,
         sorted(moved, key=lambda p: -abs(p[0]["dbfs"] - p[1]["dbfs"])),
         lambda p: "    %10.3f MHz  %6.1f -> %6.1f dBFS  %s"
                   % (p[0]["hz"] / 1e6, p[1]["dbfs"], p[0]["dbfs"],
                      p[0]["allocation"] or "no entry"))
    show("changed kind", kind_changes,
         lambda p: "    %10.3f MHz  %s -> %s%s  %s"
                   % (p[0]["centre_hz"] / 1e6,
                      p[1]["kind"]["carrier"], p[0]["kind"]["carrier"],
                      "   standing %.3f -> %.3f"
                      % (p[1]["kind"]["standing_share"],
                         p[0]["kind"]["standing_share"]),
                      p[0].get("allocation") or "no entry"))
    if kind_changes:
        print("    A signal that stops being a bare carrier, or starts, has"
              "\n    changed in a way no level and no width would show.")
    print("\n  A signal is 'gone' only where both surveys looked. Absence in a"
          "\n  single sweep is weak evidence: a 0.12 s dwell catches a bursty"
          "\n  transmitter about as often as it misses it.")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("ingest", help="survey output -> a stored survey")
    p.add_argument("file", nargs="?", help="defaults to stdin")
    p.add_argument("--out", help="where to write; defaults to surveys/")
    p.add_argument("--note", help="what this sweep was for, or where it was taken")
    p.add_argument("--gain", type=float, help="receiver gain in dB, if it was set")
    p.add_argument("--no-fingerprint", action="store_true",
                   help="skip the WiFi-derived check on the site label")
    p.set_defaults(func=cmd_ingest)

    p = sub.add_parser("report", help="what one survey found")
    p.add_argument("survey")
    p.add_argument("--top", type=int, default=10)
    p.set_defaults(func=cmd_report)

    p = sub.add_parser("diff", help="what changed between two surveys")
    p.add_argument("old")
    p.add_argument("new")
    p.add_argument("--top", type=int, default=10)
    p.add_argument("--force", action="store_true",
                   help="compare two sites anyway; almost always a mistake")
    p.set_defaults(func=cmd_diff)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
