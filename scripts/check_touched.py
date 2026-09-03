#!/usr/bin/env python3
"""Run the checks that cover what changed, and say what was skipped.

The full suite takes the best part of a minute and most of it has nothing to
do with any one change; a single suite takes under a second. Running everything
after every edit is how a fast loop becomes a slow one, so this picks the
suites whose own prerequisites name a file that changed.

The mapping is not maintained here. Every check target in the Makefile already
declares what it is a check *of* -- `check-layout` names the layout headers,
`check-gsm-dsp` names gsm_dsp.c -- so that list is read rather than restated,
and a new check is covered the moment it has a rule.

This is not the gate. It reports how many suites it skipped for exactly that
reason: a green tick over three of twenty-eight suites is worth what it says
and no more, and `make check` on push is what covers the rest.
"""

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VARS = {"$(TESTS)": "tests", "$(SRC)": "src", "$(BUILD)": "build"}


def check_rules(makefile):
    """{target: [prerequisite paths]} for every check-* rule with a recipe."""
    text = makefile.read_text()
    text = re.sub(r"\\\n\s*", " ", text)          # join continuations
    rules = {}
    for line in text.splitlines():
        match = re.match(r"^(check-[a-z0-9-]+):(.*)$", line)
        if not match:
            continue
        target, rest = match.group(1), match.group(2)
        prereqs = []
        for word in rest.split():
            for name, value in VARS.items():
                word = word.replace(name, value)
            prereqs.append(word)
        rules[target] = prereqs
    return rules


def changed_files():
    def run(*args):
        out = subprocess.run(["git", "-C", str(ROOT), *args],
                             capture_output=True, text=True)
        return [l for l in out.stdout.splitlines() if l.strip()]
    # Working tree against the last commit, plus anything not yet added.
    return sorted(set(run("diff", "--name-only", "HEAD") +
                      run("ls-files", "--others", "--exclude-standard")))


def main(argv):
    files = argv[1:] or changed_files()
    if not files:
        print("nothing changed; nothing to check")
        return 0

    rules = check_rules(ROOT / "Makefile")
    # Aggregates are other checks under one name; running them would undo the
    # whole point of picking.
    rules = {t: p for t, p in rules.items()
             if not all(q.startswith("check-") for q in p)}

    wanted = set()
    for target, prereqs in rules.items():
        if any(f in prereqs for f in files):
            wanted.add(target)
    # check-pipelines runs the built program over the captures, so it is the
    # one check that any source change can break and no source change names.
    if any(f.startswith("src/") for f in files) and "check-pipelines" in rules:
        wanted.add("check-pipelines")

    print("changed:")
    for f in files:
        print("   ", f)
    if not wanted:
        print("\nno check covers those files.")
        print("If they hold a decision, that is a gap, not a pass:")
        print("add a ticket under .scratch/testability/ or a check.")
        return 0

    order = [t for t in sorted(wanted)]
    print("\nrunning %d of %d suites:" % (len(order), len(rules)))
    result = subprocess.run(["make", *order], cwd=ROOT)
    skipped = len(rules) - len(order)
    print("\n%d suites not run. `make check` is the gate, and the pre-push "
          "hook runs it." % skipped)
    return result.returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv))
