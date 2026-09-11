#ifndef INSTALLATION_H
#define INSTALLATION_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/*
 * The receiving setup: which receiver, at which site, on which antenna -- and
 * what has been calibrated for it.
 *
 * `.scratch/deepening/issues/03-installation-module.md`, and it is where
 * **ADR-0018 and ADR-0022 land**. Both say the same thing about different
 * data: a measurement belongs to the physical arrangement that produced it,
 * and attaching one arrangement's number to another is silent rather than
 * wrong-looking.
 *
 *   - a **tuning correction** compensates one crystal, measured against
 *     whatever reference one place offers, so it is keyed by **receiver and
 *     site** (ADR-0018);
 *   - a **survey history** is a baseline of what a place sounds like through
 *     one antenna, so it is keyed by **receiver, site and antenna**
 *     (ADR-0022). Swapping a whip for a rooftop makes known carriers vanish
 *     and new ones appear, and a history that could not tell that from a
 *     change on air would report it as one.
 *
 * **Legacy values are kept and not applied.** Both ADRs refuse to guess which
 * receiver produced a site-only value: doing so makes an irreversible
 * provenance claim the program cannot justify. So a legacy correction is held
 * unclaimed and the operator claims it, after which it is that receiver's.
 *
 * Pure: a model and some string handling, no files (ADR-0012). `config.{c,h}`
 * and `site_history.{c,h}` stay as the format layer and keep their own checks;
 * `installation_commit()` in the .c is the one place that writes.
 */

/* A USB serial, an operator label, a site or an antenna name. */
#define INSTALLATION_ID_MAX 64
/* Receivers times sites. Small: this is one operator's equipment. */
#define INSTALLATION_PROFILES_MAX 32

/*
 * One calibration profile.
 *
 * `receiver` empty means a **legacy** value: read from a file written before
 * ADR-0018, belonging to a site and to no known crystal. It is never applied
 * until claimed.
 */
struct calibration_profile {
    char receiver[INSTALLATION_ID_MAX];
    char site[INSTALLATION_ID_MAX];
    int ppm;
};

struct installation {
    /* Who is plugged in, where it is, and what it is listening through. */
    char receiver[INSTALLATION_ID_MAX];
    char site[INSTALLATION_ID_MAX];
    char antenna[INSTALLATION_ID_MAX];

    struct calibration_profile profile[INSTALLATION_PROFILES_MAX];
    int profile_count;

    /* Something changed that a commit would write. One flag, so the four
       callers that used to each decide when to save no longer have to. */
    int dirty;
};

static inline void installation_reset(struct installation *inst) {
    if (inst)
        memset(inst, 0, sizeof(*inst));
}

/* Copy an identifier in, bounded. */
static inline void installation_set_id(char *field, const char *value) {
    size_t n = value ? strlen(value) : 0;

    if (!field)
        return;
    if (n > INSTALLATION_ID_MAX - 1)
        n = INSTALLATION_ID_MAX - 1;
    if (n)
        memcpy(field, value, n);
    field[n] = '\0';
}

/*
 * Which identity a receiver has, from what the device offered.
 *
 * ADR-0018: the USB serial when it is present and unique, otherwise a stable
 * operator-assigned label. **Uniqueness cannot be judged from one device**, and
 * these dongles are the reason the ADR says so -- many ship with the same
 * string or none at all. So the rule here is: a label always wins when the
 * operator has set one, because they set it precisely to resolve this; failing
 * that a non-empty serial; failing that nothing, and a receiver with no
 * identity cannot have a profile persisted.
 *
 * The device *index* is never an identity. It changes with enumeration order.
 */
static inline void installation_identify(struct installation *inst,
                                         const char *serial,
                                         const char *label) {
    if (!inst)
        return;
    if (label && label[0])
        installation_set_id(inst->receiver, label);
    else if (serial && serial[0])
        installation_set_id(inst->receiver, serial);
    else
        inst->receiver[0] = '\0';
}

static inline int installation_identified(const struct installation *inst) {
    return inst && inst->receiver[0] != '\0';
}

/* Find a profile, or -1. `receiver` NULL or empty matches a legacy value. */
static inline int installation_find(const struct installation *inst,
                                    const char *receiver, const char *site) {
    int i;

    if (!inst || !site || !site[0])
        return -1;
    for (i = 0; i < inst->profile_count; i++) {
        if (strcmp(inst->profile[i].site, site) != 0)
            continue;
        if (receiver && receiver[0]) {
            if (strcmp(inst->profile[i].receiver, receiver) == 0)
                return i;
        } else if (inst->profile[i].receiver[0] == '\0') {
            return i;
        }
    }
    return -1;
}

/*
 * The correction to apply here, if there is one. Returns 1 and writes `ppm`,
 * or 0.
 *
 * A legacy value is **not** an answer: that is the whole of ADR-0018's
 * refusal. `installation_legacy_ppm` says one exists so an operator can be
 * offered it.
 */
static inline int installation_ppm(const struct installation *inst, int *ppm) {
    int at;

    if (!inst || !installation_identified(inst))
        return 0;
    at = installation_find(inst, inst->receiver, inst->site);
    if (at < 0)
        return 0;
    if (ppm)
        *ppm = inst->profile[at].ppm;
    return 1;
}

/* Whether an unclaimed legacy correction exists for this site. */
static inline int installation_legacy_ppm(const struct installation *inst,
                                          int *ppm) {
    int at = installation_find(inst, NULL, inst ? inst->site : NULL);

    if (at < 0)
        return 0;
    if (ppm)
        *ppm = inst->profile[at].ppm;
    return 1;
}

/*
 * Record a correction for the receiver and site in hand. Returns 0, or -1 when
 * there is no room or no receiver identity -- a correction that cannot say
 * whose crystal it compensates is not persisted.
 */
static inline int installation_record_ppm(struct installation *inst, int ppm) {
    int at;

    if (!inst || !installation_identified(inst) || !inst->site[0])
        return -1;
    at = installation_find(inst, inst->receiver, inst->site);
    if (at < 0) {
        if (inst->profile_count >= INSTALLATION_PROFILES_MAX)
            return -1;
        at = inst->profile_count++;
        installation_set_id(inst->profile[at].receiver, inst->receiver);
        installation_set_id(inst->profile[at].site, inst->site);
    }
    if (inst->profile[at].ppm != ppm) {
        inst->profile[at].ppm = ppm;
        inst->dirty = 1;
    }
    return 0;
}

/*
 * Claim this site's legacy correction for the receiver in hand.
 *
 * The operator's explicit act, which is what ADR-0018 requires instead of the
 * program guessing. Returns 0 when a legacy value was claimed, -1 otherwise --
 * there was none, or there is no receiver identity to claim it for.
 */
static inline int installation_claim_legacy(struct installation *inst) {
    int legacy, ppm = 0;

    if (!inst || !installation_identified(inst))
        return -1;
    legacy = installation_find(inst, NULL, inst->site);
    if (legacy < 0)
        return -1;
    ppm = inst->profile[legacy].ppm;
    /* The legacy entry becomes this receiver's, rather than being copied:
       claiming it twice for two receivers would invent provenance. */
    installation_set_id(inst->profile[legacy].receiver, inst->receiver);
    inst->profile[legacy].ppm = ppm;
    inst->dirty = 1;
    return 0;
}

/*
 * The key a survey history is stored under: receiver, site and antenna
 * together (ADR-0022).
 *
 * Gain is deliberately **not** in it. The history's presence and prominence
 * claims should survive an ordinary gain adjustment, while the recorded
 * absolute level keeps the setting needed to read it -- which is observation
 * metadata rather than identity.
 *
 * A setup missing any member has no key: writing a history that cannot say
 * what produced it is what ADR-0022 refuses. Returns the length, or -1.
 */
static inline int installation_history_key(const struct installation *inst,
                                           char *out, size_t size) {
    size_t need;

    if (!inst || !out || size == 0)
        return -1;
    out[0] = '\0';
    if (!inst->site[0] || !inst->antenna[0] || !installation_identified(inst))
        return -1;
    need = strlen(inst->receiver) + strlen(inst->site) +
           strlen(inst->antenna) + 3;
    if (need > size)
        return -1;   /* a truncated key would collide with another setup's */
    /*
     * Receiver first so one operator's files sort together, and a separator
     * none of the three may contain. Built by hand rather than with snprintf:
     * the length is already checked above, and snprintf makes the compiler
     * warn about a truncation that cannot happen.
     */
    {
        size_t at = 0;
        const char *part[3];
        int k;

        part[0] = inst->receiver;
        part[1] = inst->site;
        part[2] = inst->antenna;
        for (k = 0; k < 3; k++) {
            size_t n = strlen(part[k]);

            if (k)
                out[at++] = '/';
            memcpy(out + at, part[k], n);
            at += n;
        }
        out[at] = '\0';
        return (int)at;
    }
}

/*
 * Where a receiving setup's history lives.
 *
 * `surveys/history-<receiver>-<site>-<antenna>.txt`, with everything outside
 * `[A-Za-z0-9_-]` reduced to a dash so a site typed with spaces or accents
 * still names one file.
 *
 * **This is the only history name the program builds.** A file under the old
 * shape, `surveys/history-<site>.txt`, is inert: nothing opens it, nothing
 * offers it and nothing merges it. ADR-0022 promised an unassigned legacy
 * baseline the operator could claim; that was never implemented and the ADR
 * was amended on 2026-09-11 to say so, taking the by-site entry points in
 * `site_history.h` with it. A file written before the ADR cannot say which
 * receiver and antenna produced it, and inventing that is the one thing the
 * ADR refuses -- so an operator who does know renames it to this name, which
 * puts the assertion where the knowledge is.
 *
 * Returns 0, or -1 when the setup is incomplete or the name will not fit.
 */
static inline int installation_history_path(const struct installation *inst,
                                            char *out, size_t size) {
    char key[INSTALLATION_ID_MAX * 3 + 3];
    char safe[sizeof(key)];
    size_t i;
    int written;

    if (installation_history_key(inst, key, sizeof(key)) < 0 || !out)
        return -1;
    for (i = 0; i + 1 < sizeof(safe) && key[i]; i++) {
        char c = key[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '-' || c == '_';
        safe[i] = ok ? c : '-';
    }
    safe[i] = '\0';
    if (!safe[0])
        return -1;
    written = snprintf(out, size, "surveys/history-%s.txt", safe);
    return (written < 0 || (size_t)written >= size) ? -1 : 0;
}

/*
 * The file-touching half, in installation.c. Declared here rather than in a
 * second header because they are the same module; the split is only that these
 * cannot be checked without a filesystem.
 */
struct config;
struct site_history;

/* Fill the installation in from a loaded config and whatever the device said
   about itself. Legacy `known_site` corrections arrive unclaimed. */
void installation_load(struct installation *inst, const struct config *config,
                       const char *serial, const char *label);
/* Write everything that changed, once. Returns 0, or -1. Does nothing and
   returns 0 when nothing changed. */
int installation_commit(struct installation *inst, struct config *config);

/* This receiving setup's history, by ADR-0022's key. */
int installation_history_load(const struct installation *inst,
                              struct site_history *history);
int installation_history_save(const struct installation *inst,
                              const struct site_history *history);

#endif /* INSTALLATION_H */
