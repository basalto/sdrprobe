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
        "candidates": [],
        "carriers": [],
    }
    for line in text.splitlines():
        f = line.split()
        if not f:
            continue
        if line.startswith("candidate ") and len(f) >= 8:
            flags = [] if f[6] == "-" else f[6].split(",")
            allocation = " ".join(f[7:])
            out["candidates"].append({
                "hz": int(float(f[1])),
                "dbfs": float(f[2]),
                "prominence_db": float(f[3]),
                "centre_hz": None if f[4] == "-" else int(float(f[4])),
                "width_hz": None if f[5] == "-" else int(float(f[5])),
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
        elif f[:2] == ["survey", "candidates"]:
            pairs = dict(zip(f[1::2], f[2::2]))
            for key in ("candidates", "suspicious"):
                if key in pairs:
                    out["totals"][key] = int(pairs[key])
        else:
            tuner = re.match(r"Found (.+) tuner", line)
            if tuner:
                out["receiver"]["tuner"] = tuner.group(1)
    return out


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
    lo, hi = record.get("range_hz") or [0, 0]
    day = record["recorded_at"][:10]
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
    with open(path, "w") as handle:
        json.dump(record, handle, indent=1, sort_keys=True)
        handle.write("\n")
    print("%s: %d candidates, %d flagged as the receiver's own"
          % (path, len(record["candidates"]),
             record["totals"].get("suspicious", 0)))


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
