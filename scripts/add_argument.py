#!/usr/bin/env python3
"""Insert an argument into every call to a function, across a C file.

    make add-argument FILE=src/foo.c FUNC=bar INDEX=1 VALUE='&app->source'
    make add-argument FILE=--self-test

Why this exists
---------------

This repository keeps threading a new parameter through a function that has
dozens of call sites: `sdr_dsp_convert_iq` gained a device profile across six
files, `lte_reference_power` gained a full scale across three, and
`survey_suspect` gained a reference clock. Each of those was a scratch script,
and `CLAUDE.md`'s rule is that a harness written three times is a tool.

What it is careful about
------------------------

**String and character literals, and comments.** The scratch version was not,
and it was wrong: asked to insert at index 2 in

    f(a, "comma, inside", b);

it produced

    f(a, "comma, NEW, inside", b);

silently, because it split arguments on every comma at brace depth 0 and a
comma inside a string is at depth 0. It survived three real refactors only
because every insertion happened to be at index 0 or 1, ahead of any such
string. `--self-test` pins that case and the others below.

It is a text tool, not a parser. It does not know about macros that expand to
calls, function *pointers* through a different name, or a declaration as
opposed to a call -- it rewrites a prototype the same way it rewrites a call,
which is usually what is wanted and is worth knowing when it is not. Read the
diff.
"""

import os
import re
import sys

_STRING = 0
_CHAR = 1
_LINE_COMMENT = 2
_BLOCK_COMMENT = 3


def _spans(text):
    """Every region of `text` whose commas and parens must be ignored.

    Yields (start, end) for string literals, character literals and comments.
    """
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            quote, start = c, i
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            yield (start, i)
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            start = i
            while i < n and text[i] != "\n":
                i += 1
            yield (start, i)
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            start = i
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                i += 1
            i = min(i + 2, n)
            yield (start, i)
            continue
        i += 1


def _mask(text):
    """`text` with every literal and comment blanked out.

    Positions are preserved, so an index into the mask is an index into the
    original. Scanning the mask is what makes the depth counting honest.
    """
    out = list(text)
    for start, end in _spans(text):
        for k in range(start, end):
            if out[k] != "\n":
                out[k] = " "
    return "".join(out)


def split_args(text, mask):
    """Split a call's argument text at top-level commas, using `mask`."""
    args, depth, start = [], 0, 0
    for i, c in enumerate(mask):
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == "," and depth == 0:
            args.append(text[start:i])
            start = i + 1
    args.append(text[start:])
    return args


def insert_arg(src, func, index, value):
    """Insert `value` as argument `index` (0-based) of every call to `func`."""
    mask = _mask(src)
    pattern = re.compile(re.escape(func) + r"\s*\(")
    out, i, count = "", 0, 0
    while True:
        m = pattern.search(mask, i)
        if not m:
            out += src[i:]
            break
        # A call, not a longer identifier ending in this name.
        before = mask[m.start() - 1] if m.start() else " "
        if before.isalnum() or before == "_":
            out += src[i:m.end()]
            i = m.end()
            continue

        out += src[i:m.end()]
        depth, j = 1, m.end()
        while j < len(mask) and depth:
            if mask[j] == "(":
                depth += 1
            elif mask[j] == ")":
                depth -= 1
            j += 1
        inner, inner_mask = src[m.end():j - 1], mask[m.end():j - 1]
        args = split_args(inner, inner_mask)
        if len(args) <= index or (len(args) == 1 and not args[0].strip()):
            out += inner + ")"
        else:
            lead = re.match(r"\s*", args[index]).group(0)
            args.insert(index, (lead if "\n" in lead else " ") + value)
            if index == 0:
                args[0] = value
                args[1] = " " + args[1].lstrip()
            out += ",".join(args) + ")"
            count += 1
        i = j
    return out, count


def _self_test():
    """The cases the scratch version got wrong, and the ones it got right."""
    cases = [
        # (source, func, index, value, expected)
        ('f(a, b);', 'f', 1, 'N', 'f(a, N, b);'),
        ('f(a, b);', 'f', 0, 'N', 'f(N, a, b);'),
        # the bug: a comma inside a string is not an argument separator
        ('f(a, "comma, inside", b);', 'f', 2, 'N',
         'f(a, "comma, inside", N, b);'),
        # a paren inside a string must not end the call
        ('f(a, "paren ) in", b);', 'f', 2, 'N', 'f(a, "paren ) in", N, b);'),
        # a char literal holding a quote
        ("f(a, '\\'', b);", 'f', 2, 'N', "f(a, '\\'', N, b);"),
        # nesting still works
        ('f(a, g(x, y), b);', 'f', 2, 'N', 'f(a, g(x, y), N, b);'),
        # A comma in a comment is not a separator, so this call has two
        # arguments and not three -- inserting at 1 lands before the comment,
        # and inserting at 2 is out of range and leaves it alone. The first
        # expectation written here was the second of those, and it was wrong
        # about the call rather than about the code.
        ('f(a, /* one, two */ b);', 'f', 1, 'N',
         'f(a, N, /* one, two */ b);'),
        ('f(a, /* one, two */ b);', 'f', 2, 'N', 'f(a, /* one, two */ b);'),
        # a call whose name is a suffix of another identifier is left alone
        ('my_f(a, b); f(a, b);', 'f', 1, 'N', 'my_f(a, b); f(a, N, b);'),
        # too few arguments: left alone rather than guessed at
        ('f(a);', 'f', 3, 'N', 'f(a);'),
        # no arguments at all
        ('f();', 'f', 0, 'N', 'f();'),
    ]
    failures = 0
    for source, func, index, value, expected in cases:
        got, _ = insert_arg(source, func, index, value)
        if got != expected:
            failures += 1
            print("    FAIL  %r -> %r, expected %r" % (source, got, expected))
    total = len(cases)
    # The same contract tests/check.h and tests/pipelines.sh give: `make check`
    # sums the counts through CHECK_TALLY, and a suite that does not write to it
    # is a suite the total silently leaves out.
    tally = os.environ.get("CHECK_TALLY")
    if tally:
        with open(tally, "a") as handle:
            handle.write("%d %d\n" % (total, failures))
    if failures:
        print("  %-34s %4d checks   %d FAILED"
              % ("add_argument", total, failures))
        return 1
    print("  %-34s %4d checks   ok" % ("add_argument", total))
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "--self-test":
        return _self_test()
    if len(argv) != 5:
        print(__doc__)
        return 2
    path, func, index, value = argv[1], argv[2], int(argv[3]), argv[4]
    with open(path) as handle:
        source = handle.read()
    patched, count = insert_arg(source, func, index, value)
    with open(path, "w") as handle:
        handle.write(patched)
    print("%s: %s x%d" % (path, func, count))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
