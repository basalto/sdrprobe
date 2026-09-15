#!/usr/bin/env python3
"""What `make` with no target prints: every target, and what it is for.

A help text kept by hand is a caption that stops agreeing with the picture
above it -- so this one is read out of the Makefile and out of the checks
themselves.

Two sources, and neither is a copy:

  * a `#:` line immediately above a target is that target's description, with
    an optional `[Group]` tag saying where it belongs;

  * a `check-*` rule with no `#:` line is described by **its own suite**: the
    rule names a `$(TESTS)/<name>_test.c` and that file ends in
    `check_report("what it covers")`, which is the same sentence the suite
    prints when it runs. A check that changes what it covers changes this
    help with it, and one whose sentence is missing says so here rather than
    disappearing.

Targets with neither are listed as undocumented rather than hidden: a target
nobody can find is worse than an ugly line in a list.
"""

import os
import re
import sys
import textwrap

TARGET = re.compile(r"^([A-Za-z0-9][A-Za-z0-9_.-]*)\s*:(?!=)")
DESCRIBED = re.compile(r"^#:\s?(.*)$")
GROUPED = re.compile(r"^\[([^\]]+)\]\s*(.*)$")
TEST_SRC = re.compile(r"\$\(TESTS\)/([A-Za-z0-9_]+\.c)")
REPORTS = re.compile(r'check_report\(\s*"([^"]*)"')

GATE = {"check", "check-touched", "check-dsp", "check-pipelines"}

ORDER = ["Build", "Gate", "Checks", "Diagnostics", "Tools", "Other"]

HEADINGS = {
    "Build": "Build and run",
    "Gate": "The gate -- run the suite that covers the change, not all of them",
    "Checks": "Checks -- one suite each, no window and no receiver",
    "Diagnostics": "Diagnostics -- a walk through a decode, not a pass or fail",
    "Tools": "Tools",
    "Other": "Other",
}

FOOTER = """\
make V=1 <target>          show the compiler commands behind the report
make CHECK_JOBS=8 check    how many suites at once (default: half the cores)
make CC=clang check        a second implementation, which has earned its place

AGENTS.md has the per-file tour; CLAUDE.md has the build loop and the
invariants that are easy to break."""


def group_of(name, tagged):
    if tagged:
        return tagged
    if name in GATE:
        return "Gate"
    if name.startswith("check-"):
        return "Checks"
    if name.startswith("probe-"):
        return "Diagnostics"
    return "Other"


def suite_sentence(root, body):
    """What a check's own suite says it covers, from its check_report()."""
    found = TEST_SRC.search(body)
    if not found:
        return None
    path = os.path.join(root, "tests", found.group(1))
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            said = REPORTS.findall(handle.read())
    except OSError:
        return None
    return said[-1] if said else None


def targets(makefile):
    """Every target in the file, with the `#:` line above it and its body."""
    out = []
    pending = None
    with open(makefile, "r", encoding="utf-8", errors="replace") as handle:
        lines = handle.read().splitlines()
    index = 0
    while index < len(lines):
        line = lines[index]
        described = DESCRIBED.match(line)
        if described:
            pending = described.group(1).strip()
            index += 1
            continue
        found = TARGET.match(line)
        if found and "$(" not in found.group(1) and "%" not in found.group(1):
            body = line
            while body.endswith("\\") and index + 1 < len(lines):
                index += 1
                body += " " + lines[index].strip()
            out.append((found.group(1), pending, body))
        pending = None
        index += 1
    return out


def described_targets(makefile):
    """Every target, its group and its description, resolved."""
    root = os.path.dirname(os.path.abspath(makefile))
    rows = {}
    for name, described, body in targets(makefile):
        if name.startswith(".") or name in rows:
            continue
        tagged = None
        if described:
            tag = GROUPED.match(described)
            if tag:
                tagged, described = tag.group(1), tag.group(2).strip()
        if not described and name.startswith("check-"):
            described = suite_sentence(root, body)
        rows[name] = (group_of(name, tagged), described)
    return rows


def self_test(makefile):
    """Every target this Makefile has is in the list, and says what it is.

    The failure this guards is the one a green run cannot show: a target
    added without a `#:` line is still *listed*, so nothing breaks and
    nobody notices -- it just sits there saying nothing, and `make` stops
    being an answer to "what can I run here". The same shape as the
    NOT GATED and MISSING audits in CLAUDE.md, and gated rather than
    written down.
    """
    total = failures = 0
    rows = described_targets(makefile)
    for name, (group, described) in sorted(rows.items()):
        total += 1
        if not described:
            failures += 1
            print("  make-help: %s has no description: add a `#:` line above "
                  "it, or a check_report() to its suite" % name)
        total += 1
        if group == "Other":
            failures += 1
            print("  make-help: %s is in no group: tag its `#:` line "
                  "[Build], [Tools] or [Diagnostics]" % name)
    total += 1
    if not rows:
        failures += 1
        print("  make-help: no targets found at all")

    tally = os.environ.get("CHECK_TALLY")
    if tally:
        with open(tally, "a") as handle:
            handle.write("%d %d\n" % (total, failures))
    label = "every target is listed, and says what it is"
    if failures:
        print("  %-56s %4d checks   %d FAILED" % (label, total, failures))
        return 1
    print("  %-56s %4d checks   ok" % (label, total))
    return 0


def main(argv):
    rest = [a for a in argv[1:] if a != "--self-test"]
    makefile = rest[0] if rest else "Makefile"
    if "--self-test" in argv:
        return self_test(makefile)
    rows = {}
    for name, (group, described) in described_targets(makefile).items():
        rows.setdefault(group, []).append(
            (name, described or "(undocumented)"))

    print("sdrprobe -- make <target>. With no target, this list.\n")
    width = max(len(n) for group in rows.values() for n, _ in group) + 2
    for group in ORDER + sorted(set(rows) - set(ORDER)):
        if group not in rows:
            continue
        print(HEADINGS.get(group, group))
        for name, described in sorted(rows[group]):
            wrapped = textwrap.wrap(described, 78 - width - 2,
                                    break_on_hyphens=False,
                                    break_long_words=False) or [""]
            print("  %-*s%s" % (width, name, wrapped[0]))
            for more in wrapped[1:]:
                print("  %-*s%s" % (width, "", more))
        print()
    print(FOOTER)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
