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
 * Still 0.x deliberately. Under SemVer the leading zero says the public
 * surface may still move without a MAJOR bump, and it does: the tabs were
 * reorganised this month, the survey stopped being a Scope view, the centre
 * frequency moved out of the Settings panel, and the Scope grew a header with
 * fields in it. 1.0.0 is a promise to stop doing that, and it should be made
 * when it is true rather than when the program feels finished.
 */

#define SDRPROBE_VERSION_MAJOR 0
#define SDRPROBE_VERSION_MINOR 22
#define SDRPROBE_VERSION_PATCH 0

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
