#!/usr/bin/env python3
"""Folders are link boundaries: a layer may include only what is beneath it.

`src/` is seven directories and the order below is the whole rule. It is
checked rather than described because the alternative is a list kept by hand,
and this repository already knows what those cost: `check-signal-probe`
existed, passed and was never run by the gate; `link_health` named three
streams of nine for weeks; and ticket 04's own header audit silently started
reporting 104 false positives the moment the files moved, because it grepped
`$(SRC)/<file>` and the paths had grown a folder.

**What this catches that the linker cannot.** A file may include a header and
call nothing from it -- which is how `viewer_session.c` and `survey_report.c`
came to include `view.h`, and so `<raylib.h>`, while
`check-no-window-link` passed and every symbol resolved
(`.scratch/layer-boundaries/issues/04-*`). An include is a compile-time
dependency whether or not it is used, and only reading the includes finds it.

`check-no-raylib-headers` is the same idea aimed at one library; this is the
general form, and the two are kept separate deliberately: that one names the
*symptom* a reader on a headless box actually hits, and would survive this
being deleted.
"""

import os
import re
import sys

# Beneath to above. A layer may include itself and anything earlier.
#
# `model` sits above `runtime` and that was decided on evidence rather than
# taste -- both orders were measured against the real include graph. Above,
# three violations; beneath, six. The reason is that a view model is built
# *for a reader* and so reads runtime state (`struct scope_view`, `struct
# fm_view`, `enum decode_kind`), while the thing that looked like a
# counter-example -- `survey_record` and `survey_store`, which runtime writes
# -- turned out not to be a view model at all. Moving those two into
# `runtime/` took the count to zero and gave `model/` a sharper definition:
# **what crosses the seam to a reader**, which is the four view models and
# nothing else.
LAYERS = ["core", "tech", "runtime", "model", "server", "gui", "app"]

INCLUDE = re.compile(r'^\s*#\s*include\s+"([a-z_0-9]+\.h)"', re.M)


def home_of_each_header(root):
    """{header basename: the layer it lives in}."""
    home = {}
    for layer in LAYERS:
        for name in os.listdir(os.path.join(root, layer)):
            home[name] = layer
    return home


def violations(root):
    home = home_of_each_header(root)
    rank = {layer: i for i, layer in enumerate(LAYERS)}
    found = []
    checked = 0
    for layer in LAYERS:
        directory = os.path.join(root, layer)
        for name in sorted(os.listdir(directory)):
            checked += 1
            with open(os.path.join(directory, name)) as handle:
                text = handle.read()
            for header in INCLUDE.findall(text):
                # A header from vendor/ or the system is not ours to rank.
                if header not in home:
                    continue
                if rank[home[header]] > rank[layer]:
                    found.append((layer, name, header, home[header]))
    return checked, found


def main(argv):
    root = argv[1] if len(argv) > 1 else "src"
    checked, found = violations(root)

    if found:
        print("  FAIL  a layer reached upward:")
        for layer, name, header, target in found:
            print("        src/%s/%s includes %s, which is %s/" %
                  (layer, name, header, target))
        print()
        print("  The order is: " + " -> ".join(LAYERS))
        print("  A layer may include only itself and what is beneath it.")
        print("  Either the include belongs lower, or the file is in the")
        print("  wrong folder -- both have happened here, and the second")
        print("  was the commoner of the two.")
        return 1

    print("  %-56s %5d checks   ok" %
          ("every layer includes only what is beneath it", checked))
    tally = os.environ.get("CHECK_TALLY")
    if tally:
        with open(tally, "a") as handle:
            handle.write("%d 0\n" % checked)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
