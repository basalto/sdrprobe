# 13 - The page as files a person can edit, rather than a C string literal

Status: resolved, 2026-09-17

## What it is today

`src/viewer_page.h` is one `static const char VIEWER_PAGE_HTML[]`, built from
**307 adjacent string literals** -- HTML, CSS and JavaScript, each line
hand-escaped and hand-terminated:

```c
"  <button id=\"tab-scope\" class=\"active\">Scope</button>\n"
```

`viewer_link.c` includes it and `serve_page()` writes it verbatim: an
`HTTP/1.1 200 OK` with `Content-Type: text/html` and a `Content-Length` taken
as `sizeof(VIEWER_PAGE_HTML) - 1`, then `Connection: close`. There is no file
I/O, no template, no substitution and no second asset -- one buffer, compiled
in, sent whole to any GET that is not a WebSocket upgrade. Nothing is
generated at build time.

Measured: **14820 bytes reach the browser**; the C source spends **16116**
saying so -- 1296 bytes of quoting and escaping, 9%, including **68**
backslash-escaped quotes.

## The case for moving it out

**A person cannot comfortably read or change it.** Every attribute quote is
`\"`, every line ends `\n"` and begins `"`, and no editor highlights HTML,
CSS or JavaScript inside a C string. A stray unescaped quote is a C syntax
error pointing at the wrong line; a forgotten `\n` silently joins two lines
of JavaScript, which for a `//` comment means the next line is commented out
and the page simply stops working. That is a real failure mode of this exact
format, not a hypothetical.

**A check cannot read it either, and that is the stronger argument.** Ticket
11 wants the page's own JavaScript driven against a live server in a DOM
shim. From a C string, the check has to *extract* the script first --
un-escape the literals, find the `<script>` -- and that extractor is
untested code sitting between the check and its subject. A check that parses
its subject out of a C literal is measuring its own parser too.

**It is about to grow several times over.** Ticket 07's own remaining list is
FM plus five decode views, each wanting a panel, a chart and a table, plus the
overlays. 341 lines of header is not the problem; 341 lines is the last
comfortable moment to decide.

**Someone other than the author should be able to restyle it.** A colour, a
column, a label -- today that means editing C.

## The case against, which is real

- **A generated header has a staleness failure mode, and this repository has
  been bitten by exactly it.** `CLAUDE.md` records `version.h` sitting outside
  `APP_HDR`: *"the header says one version, `make` reports nothing to do, and
  the binary keeps claiming the last one it was built with"* -- quiet, because
  the two things that could disagree share the stale file. A generated page
  header has the same shape and needs a real prerequisite, not a convention.
- **Today's form is self-contained**: no codegen, no build-order question, one
  artifact in a diff, and `make` needs nothing that is not already needed.
- **`git blame` and review get one more hop** between the file a person edits
  and the bytes that ship.

None of these argue for keeping a C string; they argue for the generator
being a real Makefile rule with real prerequisites.

## Shape to implement

```
web/viewer.html          <- the page, as a page
web/viewer.css           <- optional split; decide below
web/viewer.js            <- optional split; the one ticket 11 wants as a file
scripts/embed_web.py     <- emits a C header from them
build/viewer_page.h      <- generated, gitignored, never committed
```

`scripts/` already carries `make_help.py`, `check_touched.py` and
`add_argument.py`, so python3 at build time is an existing dependency and not
a new one. The generator is the boring half -- read bytes, emit a
`static const char NAME[]` as escaped literals, one C string per source line
so a compiler error still points at a recognisable place.

### Decisions this ticket owes, and must not leave to the commit

1. **One file or three.** One `viewer.html` with inline `<style>` and
   `<script>` keeps `serve_page()` exactly as it is -- one buffer, one
   response, no routing. Three files means `serve_page()` must route on path
   and serve three content types, which is a small HTTP server growing a
   second responsibility. **Ticket 11 only needs the JavaScript reachable as a
   file at *check* time**, which a split at the source level satisfies even if
   the generator concatenates them back into one page. Prefer that: split for
   the editor and the check, concatenate for the wire.
2. **Where the generated header lands.** `build/` keeps `src/` free of
   generated files and keeps the `MISSING:` audit (`ls src/*.h` must appear in
   `APP_HDR`) meaning what it means -- at the cost of an `-Ibuild` on the
   compile line. Generating into `src/` avoids the include path and puts a
   gitignored file among tracked ones, which is new here. Lean `build/`.
3. **Where the rationale comment goes.** `viewer_page.h`'s 30-line header
   comment is the best explanation of the Viewer that exists -- ADR-0027's
   generation rule, ticket 08's health panel, and the honest note on what a
   browser tab cannot know about its own CPU. It must not be lost in the move.
   It is C-side reasoning about a page, so it most likely belongs next to
   `serve_page()` in `viewer_link.c`, or in `web/README.md`. **Not** an HTML
   comment: that ships 2 KB of rationale to every browser on every load.
4. **Whether the generated header is a prerequisite of `sdrprobe` or of
   `viewer_link.o`.** It has to be one of them by name, or editing
   `web/viewer.html` will not rebuild anything -- the exact `version.h`
   failure above.
5. **Whether anything is still called a "template".** Nothing is substituted
   today: the page is static and every value arrives over the WebSocket. If it
   stays that way these are *assets*, not templates, and no substitution
   syntax should be invented for a need nobody has. Say so explicitly, so the
   next person does not add one.

## Take into account

- **`make check` must still pass with no browser and no network**, and the
  generator must run on a machine with no receiver. It reads files and writes
  a header; keep it that way.
- **`-Wall -W` clean**: generated C is still C. A 14 KB string literal is well
  inside C99's 4095-character *minimum* for a single literal only because it
  is 307 adjacent ones; keep the one-literal-per-line shape rather than
  emitting a single enormous literal.
- **The `APP_HDR` and `CHECK_UNITS` audits** in `CLAUDE.md`, both one line,
  after any new header or rule.
- **`Content-Length` is `sizeof(...) - 1` today.** Whatever the generator
  emits must keep an exact byte count available at compile time, or
  `serve_page()` needs `strlen()` and a comment saying why.
- **No CDN, no framework, no build toolchain.** Ticket 01's "canvas, no
  framework, no CDN" is unchanged by this: files on disk, concatenated by a
  script, compiled in. Nothing here introduces npm.

## Acceptance criteria

- [x] `web/viewer.html` (and any split siblings) are the source of truth, with
      no C escaping in them.
- [x] Editing one and running `make` changes the served page -- verified by
      fetching it, not by reading the Makefile.
- [x] The generated header is gitignored and never committed.
- [x] The bytes served are **identical** to today's, before any other change:
      this is a move, and a move is measured by nothing changing
      (`curl -s localhost:PORT | cmp - <reference>`).
- [x] `viewer_page.h`'s rationale survives somewhere a reader will find it.
- [ ] Ticket 11's DOM-shim check can load the page's JavaScript as a file --
      not attempted here; ticket 11 is a separate ticket and this only clears
      the way for it (a plain file at `web/viewer.js`, no extractor needed).

## Relationship to the other tickets

- **Ticket 11 depends on this one**, or pays for an extractor it should not
  have to own. Do this first.
- Ticket 07's remaining views each add to this page; every one of them is
  cheaper after this and more expensive before it.

## Comments

**Done as decided, one file split into two.** `web/viewer.html` (the
markup and `<style>`, ending in a `<script>` tag holding one marker,
`/*BUILD:SCRIPT*/`) and `web/viewer.js` (what fills it) are the two source
files; three files (a separate `.css`) was considered and dropped -- nothing
here needs a colour or a layout changed independently of the markup that
uses it, and a third file is a third thing to keep in dependency order once
ticket 14's later split adds more JS files. `scripts/embed_web.py`
concatenates (today, one file; `JS_ORDER` is the list ticket 14's later
phases extend) and emits one C string literal per source line into
`build/viewer_page.h`, which is a real prerequisite of both `sdrprobe` and
`check-viewer-link` -- not a convention -- so editing `web/viewer.html` and
running `make` rebuilds the binary with the edit, verified by serving it and
`curl`ing the result rather than by reading the Makefile.

**Byte-identical, checked three ways.** The 341-line C literal was
unescaped programmatically (Python's own string-literal grammar reads a C
`"...\n"` segment identically) and diffed byte-for-byte against what the
*unmodified* binary served over a real socket before any file here changed
-- that comparison is the ground truth the rest of this ticket was
measured against, not a re-derivation from the new source. After the split:
the generated header, compiled standalone and dumped, matches that
reference exactly; the rebuilt `sdrprobe`, served over a real socket again,
matches it exactly; and editing `web/viewer.html`, rebuilding, and serving
again shows the edit -- confirming the dependency is real rather than
merely declared. `make check` (21774 checks, 77 suites) and `make
check-pipelines` both pass unchanged.

**The rationale comment moved into the generated header itself**, not into
`viewer_link.c` or a `web/README.md` as the ticket's decision 3 offered:
`scripts/embed_web.py` writes it at the top of `build/viewer_page.h`,
immediately above `VIEWER_PAGE_HTML`, which is where a reader who greps for
that symbol or opens the header (to see what shipped) lands -- one hop
closer than a comment living beside `serve_page()` in a different file
entirely. It does not ship to the browser: it is C-only, above the string
literal, never inside the HTML or `<script>` tag.

**`-Wall -W` clean cost one rewrite.** The header comment's own prose used
"web/views/*.js" to describe a future file, and `/` immediately before `*`
reads as `/*` to a C compiler scanning a `/* ... */` comment for a nested
open -- a real `-Wcomment` warning, caught by compiling the generated
header standalone before wiring it into the Makefile at all. Rephrased to
name the directory without the glob.

**`web/lib/`, `web/wire.js` and `web/views/` are not built yet.** That is
ticket 14's Phase 2, a separate step with its own behavioural verification
(this ticket's byte-identical page is the *mechanical* half; ticket 14's
Phase 2 is the *seam* half, splitting the one `web/viewer.js` this ticket
produced into the layers ADR-0007 already decided on).
