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
# `model` sits **beneath** `runtime`, and getting there was the point of the
# exercise rather than a detail. A model is a *contract*: plain structs a
# reader depends on -- the Viewer link serialises them, the browser keys
# tables by the names in them. A contract that reaches up into the layer
# which happens to compute it is a contract its readers cannot have without
# dragging the application in behind it.
#
# The four builders that *fill* the models therefore live in `runtime/`,
# where they can read `struct app` freely, and `model/` holds the headers
# they fill. Two values had to come down with them: `enum site_seen` (out of
# `site_history.h`, which reads a file) and `struct survey_record_tuning`
# (out of `survey_record.h`), both of which cross the wire and neither of
# which needs the machinery that produces it.
LAYERS = ["core", "tech", "model", "runtime", "server", "gui", "app"]

# Either `"core/sdr_dsp.h"` or a bare `"sdr_dsp.h"`. Both are read, and the
# difference matters: since ADR-0028's second half there is one `-I`, so a
# bare name resolves *only* within the including file's own directory. A
# bare cross-layer include therefore cannot compile -- and this says so by
# name rather than leaving a reader with "No such file or directory".
INCLUDE = re.compile(
    r'^\s*#\s*include\s+"(?:([a-z]+)/)?([a-z_0-9]+\.h)"', re.M)


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
            for spelled, header in INCLUDE.findall(text):
                # A header from vendor/ or the system is not ours to rank.
                if header not in home:
                    continue
                target = home[header]
                if rank[target] > rank[layer]:
                    found.append((layer, name, header, target, 'upward'))
                elif target != layer and spelled != target:
                    # Legal direction, illegal spelling: it names no layer,
                    # or names the wrong one. Neither compiles under one
                    # `-I`, and both would start compiling again the moment
                    # somebody restored a per-folder `-I`.
                    found.append((layer, name, header, target, 'unspelled'))
    return checked, found


def main(argv):
    root = argv[1] if len(argv) > 1 else "src"
    checked, found = violations(root)

    if found:
        upward = [f for f in found if f[4] == 'upward']
        unspelled = [f for f in found if f[4] == 'unspelled']
        if upward:
            print("  FAIL  a layer reached upward:")
            for layer, name, header, target, _ in upward:
                print("        src/%s/%s includes %s, which is %s/" %
                      (layer, name, header, target))
            print()
            print("  The order is: " + " -> ".join(LAYERS))
            print("  A layer may include only itself and what is beneath it.")
            print("  Either the include belongs lower, or the file is in the")
            print("  wrong folder -- both have happened here, and the second")
            print("  was the commoner of the two.")
        if unspelled:
            print("  FAIL  a cross-layer include does not name its layer:")
            for layer, name, header, target, _ in unspelled:
                print('        src/%s/%s should say "%s/%s"' %
                      (layer, name, target, header))
            print()
            print("  There is one -I (ADR-0028), so a bare name resolves only")
            print("  inside the including file's own directory -- these do not")
            print("  compile. Spelling the layer is also the point of the")
            print("  move: the dependency is visible where it is written.")
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
