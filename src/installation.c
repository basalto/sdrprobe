#include "installation.h"

#include "config.h"
#include "site_history.h"

#include <string.h>

/*
 * The installation and the files, joined in one place.
 *
 * `.scratch/deepening/issues/03-installation-module.md`: site, antenna and the
 * tuning correction were remembered and saved from four call sites, each
 * repeating the same three calls, and the site history was loaded from disk in
 * five more. Every one of them had to know when a save was due. Now they set a
 * field and this decides.
 *
 * `config.{c,h}` and `site_history.{c,h}` stay as the format layer -- they
 * read and write a file and have no opinion about what identifies one. What
 * lives here is the joining: which profile answers for the receiver in hand,
 * and which file a receiving setup's history is in.
 */

void installation_load(struct installation *inst, const struct config *config,
                       const char *serial, const char *label) {
    int i;

    if (!inst || !config)
        return;
    installation_reset(inst);
    installation_identify(inst, serial, label);
    installation_set_id(inst->site, config->site);
    installation_set_id(inst->antenna, config->antenna);

    /* Profiles that know their receiver (ADR-0018). */
    for (i = 0; i < config->calibration_count &&
                inst->profile_count < INSTALLATION_PROFILES_MAX;
         i++) {
        struct calibration_profile *p = &inst->profile[inst->profile_count++];

        installation_set_id(p->receiver, config->calibrations[i].receiver);
        installation_set_id(p->site, config->calibrations[i].site);
        p->ppm = config->calibrations[i].ppm;
    }
    /*
     * And the legacy ones, which are a site and a number and nothing else.
     * Held with an empty receiver so `installation_ppm()` will not return
     * them: the operator claims one or it stays unassigned, which is the whole
     * of ADR-0018's refusal.
     */
    for (i = 0; i < config->site_count &&
                inst->profile_count < INSTALLATION_PROFILES_MAX;
         i++) {
        struct calibration_profile *p;

        if (config->sites[i].ppm == 0)
            continue;   /* no correction to be legacy about */
        p = &inst->profile[inst->profile_count++];
        p->receiver[0] = '\0';
        installation_set_id(p->site, config->sites[i].label);
        p->ppm = config->sites[i].ppm;
    }
    inst->dirty = 0;
}

int installation_commit(struct installation *inst, struct config *config) {
    int i, wrote = 0;

    if (!inst || !config)
        return -1;
    if (!inst->dirty)
        return 0;

    /* The site and antenna in hand, and their lists, so the combos can offer
       them again -- one place, where four used to each remember to. */
    if (inst->site[0]) {
        snprintf(config->site, sizeof(config->site), "%s", inst->site);
        config_remember_site(config, inst->site);
    }
    if (inst->antenna[0]) {
        snprintf(config->antenna, sizeof(config->antenna), "%s",
                 inst->antenna);
        config_remember_antenna(config, inst->antenna);
    }

    for (i = 0; i < inst->profile_count; i++) {
        const struct calibration_profile *p = &inst->profile[i];

        if (!p->receiver[0] || !p->site[0])
            continue;   /* still legacy; not this build's to assign */
        config_set_calibration(config, p->receiver, p->site, p->ppm);
        /* A claimed value is no longer unassigned. */
        config_clear_legacy_ppm(config, p->site);
    }

    /* One write, which is the point of the module. */
    wrote = config_save(config);
    if (wrote == 0)
        inst->dirty = 0;
    return wrote;
}

int installation_history_load(const struct installation *inst,
                              struct site_history *history) {
    char path[512];

    if (!inst || !history)
        return -1;
    if (installation_history_path(inst, path, sizeof(path)) < 0) {
        /* An incomplete setup gets an empty history rather than somebody
           else's: ADR-0022 would rather say nothing. */
        site_history_init(history, inst ? inst->site : "");
        return -1;
    }
    return site_history_load_path(path, inst->site, history);
}

int installation_history_save(const struct installation *inst,
                              const struct site_history *history) {
    char path[512];

    if (!inst || !history)
        return -1;
    if (installation_history_path(inst, path, sizeof(path)) < 0)
        return -1;
    return site_history_save_path(path, history);
}
