#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

/*
 * The few things that describe the receiver's situation rather than a run.
 *
 * A survey's levels mean nothing without them. The same band swept with a
 * different antenna, or from a different room, produces a different answer,
 * and a reader comparing two sweeps has no way to know unless the sweeps say
 * so. These belong to the installation, not to the command line: they change
 * when the operator changes something physical, and they should still be true
 * next week without being retyped.
 *
 * So they persist. A flag sets one and writes it back, and the next run starts
 * from what the last one left.
 *
 * The file is `$XDG_CONFIG_HOME/sdrprobe/config`, or `~/.config/sdrprobe/
 * config`. One `key value` per line, `#` starts a comment, and the value is
 * the rest of the line so an antenna may have spaces in its name. An unknown
 * key is kept and written back untouched, so a newer version's settings
 * survive an older binary.
 *
 * Parsing and formatting are pure and take no path, which is what lets
 * tests/config_test.c reach every decision without a home directory to write
 * into (ADR-0012).
 */

#define CONFIG_VALUE_MAX 96
#define CONFIG_UNKNOWN_MAX 8
/* How many places this receiver remembers having been. Enough for anyone
   carrying a dongle around; the list is a convenience for picking, not a
   register of everywhere it has ever been. */
#define CONFIG_SITES_MAX 12

/* What an installation looks like before anyone has said otherwise. A whip is
   what an RTL-SDR ships with, so it is the honest default rather than a
   flattering one. */
#define CONFIG_ANTENNA_DEFAULT "telescopic whip"

struct config {
    char antenna[CONFIG_VALUE_MAX];
    /* Where the receiver is. Empty until the operator says, because there is
       no default that could be right and a wrong one is worse than none: two
       sweeps both labelled "unknown" would compare as the same place. */
    char site[CONFIG_VALUE_MAX];
    /* Every site named so far, most recent first, so the survey window can
       offer them instead of asking the operator to spell one again --
       and spelling it differently is how one place becomes two. */
    char sites[CONFIG_SITES_MAX][CONFIG_VALUE_MAX];
    int site_count;
    /* Lines this build did not recognise, kept verbatim so writing the file
       back does not discard a newer version's settings. */
    char unknown[CONFIG_UNKNOWN_MAX][CONFIG_VALUE_MAX * 2];
    int unknown_count;
};

void config_defaults(struct config *config);

/*
 * Note that a site has been used, moving it to the front of the list. Returns
 * 1 when it was not there before. Pure, and the ordering is the point: the
 * place you were last is the place you are most likely to be again.
 */
int config_remember_site(struct config *config, const char *site);

/* Text in, settings out. Returns the number of keys recognised. */
int config_parse(const char *text, struct config *config);

/* Settings out as the file's text. Returns the length written, or -1 if it
   did not fit. */
int config_format(const struct config *config, char *out, size_t size);

/* Where the file lives. Returns 0, or -1 when there is no home to put it in.
   Reads the environment, so it is the one part a check cannot pin. */
int config_path(char *out, size_t size);

/* Read it, filling in defaults for anything absent. Returns 0 when a file was
   read, 1 when there was none (defaults stand), -1 on an unreadable one. */
int config_load(struct config *config);

/* Write it, creating the directory if need be. Returns 0, or -1. */
int config_save(const struct config *config);

#endif
