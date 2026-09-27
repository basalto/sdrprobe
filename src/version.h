#ifndef VERSION_H
#define VERSION_H

/*
 * What this build calls itself.
 *
 * Semantic Versioning 2.0.0 (https://semver.org), and for an application
 * rather than a library the three numbers are read against the things other
 * people's work depends on -- not against C symbols, which nobody links to.
 * Here that public surface is:
 *
 *   - the command line: flag names, their values, and what they refuse;
 *   - the headless output other programs parse -- `--decode`, `--survey`,
 *     `--lte-scan`, `--lte-chain`, `--calibrate`, and the `candidate` and
 *     `survey` record lines `scripts/survey_tool.py` reads;
 *   - the files written and read: `~/.config/sdrprobe/config`, the survey
 *     JSON under `surveys/`, `surveys/history-<site>.txt`, and the capture
 *     sidecars.
 *
 * MAJOR when one of those breaks, MINOR when one gains something backwards
 * compatible, PATCH when behaviour is corrected without either. A new view, a
 * new key, a rearranged panel: MINOR, because the screens are not a contract
 * anybody can depend on programmatically. A decode that starts reading a
 * field it previously got wrong: PATCH, even though the numbers change,
 * because the format did not.
 *
 * Merging `server` into `web` was **PATCH**, and the reasoning is worth
 * keeping because the instinct says otherwise. A command word disappearing
 * sounds MAJOR. But `server` still parses, and parses to the same thing it
 * always meant -- `web --no-browser` -- so every invocation that worked
 * yesterday produces a byte-identical `struct options` today, which
 * `check-options` asserts with a `memcmp` rather than field by field.
 * Nothing broke, so not MAJOR; nothing was gained, so not MINOR. What
 * changed is the help text and one enum value, and neither is a contract.
 *
 * A second *binary* is MINOR by the same reading. `sdrprobe-server` gains
 * nothing and breaks nothing: same flags, same subcommands, same headless
 * output, same files -- it is the same program built without a window, for a
 * machine that has no graphics stack to open one with. What is backwards
 * compatible is that `./sdrprobe` still does `headless` and `server` itself
 * and nothing a script runs today has to change.
 *
 * Still 0.x deliberately. Under SemVer the leading zero says the public
 * surface may still move without a MAJOR bump, and it does: the tabs were
 * reorganised this month, the survey stopped being a Scope view, the centre
 * frequency moved out of the Settings panel, and the Scope grew a header with
 * fields in it. 1.0.0 is a promise to stop doing that, and it should be made
 * when it is true rather than when the program feels finished.
 */

#define SDRPROBE_VERSION_MAJOR 0
#define SDRPROBE_VERSION_MINOR 63
#define SDRPROBE_VERSION_PATCH 1

#define SDRPROBE_STRINGIFY_(x) #x
#define SDRPROBE_STRINGIFY(x) SDRPROBE_STRINGIFY_(x)

#define SDRPROBE_VERSION                                                      \
    "v" SDRPROBE_STRINGIFY(SDRPROBE_VERSION_MAJOR) "."                        \
        SDRPROBE_STRINGIFY(SDRPROBE_VERSION_MINOR) "."                        \
        SDRPROBE_STRINGIFY(SDRPROBE_VERSION_PATCH)

#define SDRPROBE_CONTACT "basalto@gmail.com"

/* What the window's corner shows, and what --version prints. One string, so
   the two cannot disagree about which build this is. */
#define SDRPROBE_SIGNATURE SDRPROBE_CONTACT " " SDRPROBE_VERSION

#endif
