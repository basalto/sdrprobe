/*
 * check-installation -- a measurement belongs to the arrangement that made it.
 *
 * `.scratch/deepening/issues/03-installation-module.md`, and the two ADRs it
 * lands. Both refuse the same convenience for the same reason, and these are
 * the checks that keep the refusal:
 *
 *   - **ADR-0018**: a tuning correction compensates one crystal, so it is
 *     keyed by receiver and site. A site-only value from an older file is
 *     preserved and **not applied** until the operator claims it, because
 *     assigning it to whatever is plugged in makes a provenance claim the
 *     program cannot justify.
 *   - **ADR-0022**: a survey history is a baseline of what a place sounds like
 *     through one antenna, so it is keyed by receiver, site and antenna. Its
 *     legacy-baseline clause was amended on 2026-09-11 -- a site-only file is
 *     inert rather than claimable -- and `test_a_legacy_baseline_is_inert`
 *     below is the property that amendment turns on.
 *
 * The failure both prevent is silent. A wrong correction does not look wrong;
 * it looks like a tuning error somewhere else. A history taken through a
 * different antenna does not look like a different antenna; it looks like
 * carriers appearing and disappearing on air.
 */

#include "check.h"

#include "installation.h"
#include "site_history.h"

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

/* ADR-0018: the serial when there is one, a label when the operator sets one,
   and the device index never. */
static void test_who_the_receiver_is(void) {
    struct installation inst;

    installation_reset(&inst);
    check_true("nothing plugged in is no identity",
               !installation_identified(&inst));

    installation_identify(&inst, "77771111153705700", NULL);
    check_str("a USB serial identifies it", inst.receiver,
              "77771111153705700");
    check_true("so it is identified", installation_identified(&inst));

    /*
     * A label wins over a serial, and that is the right way round: many of
     * these dongles ship with the same serial or none, and the operator sets a
     * label precisely to resolve that. Uniqueness cannot be judged from one
     * device, so the program does not pretend to.
     */
    installation_identify(&inst, "00000001", "rooftop-dongle");
    check_str("an operator's label wins", inst.receiver, "rooftop-dongle");

    installation_identify(&inst, "", "");
    check_true("neither is no identity", !installation_identified(&inst));
    installation_identify(&inst, NULL, NULL);
    check_true("and nulls are the same", !installation_identified(&inst));
}

/*
 * The refusal at the centre of ADR-0018: a legacy value is kept, offered, and
 * not applied.
 */
static void test_a_legacy_correction_is_not_applied_until_claimed(void) {
    struct installation inst;
    int ppm = 0;

    installation_reset(&inst);
    /* A file written before ADR-0018: a site, a correction, no receiver. */
    inst.profile_count = 1;
    installation_set_id(inst.profile[0].site, "home-sala-estar");
    inst.profile[0].receiver[0] = '\0';
    inst.profile[0].ppm = -31;

    installation_set_id(inst.site, "home-sala-estar");
    installation_identify(&inst, "77771111153705700", NULL);

    check_int("it is not applied", installation_ppm(&inst, &ppm), 0);
    check_int("but it is there to be offered",
              installation_legacy_ppm(&inst, &ppm), 1);
    check_int("and it is the value from the file", ppm, -31);

    check_int("claiming it succeeds", installation_claim_legacy(&inst), 0);
    ppm = 0;
    check_int("and now it applies", installation_ppm(&inst, &ppm), 1);
    check_int("with the same value", ppm, -31);
    check_int("and is no longer legacy",
              installation_legacy_ppm(&inst, &ppm), 0);
    check_int("without inventing a second entry", inst.profile_count, 1);
    check_true("the commit will write it", inst.dirty != 0);

    /* Claiming again has nothing to claim. */
    check_int("claiming twice is refused", installation_claim_legacy(&inst),
              -1);
}

/* A receiver with no identity cannot claim, and cannot be persisted. */
static void test_an_unidentified_receiver_cannot_claim(void) {
    struct installation inst;
    int ppm = 0;

    installation_reset(&inst);
    inst.profile_count = 1;
    installation_set_id(inst.profile[0].site, "field");
    inst.profile[0].ppm = 12;
    installation_set_id(inst.site, "field");

    check_true("no identity", !installation_identified(&inst));
    check_int("the legacy value is still visible",
              installation_legacy_ppm(&inst, &ppm), 1);
    check_int("but cannot be claimed", installation_claim_legacy(&inst), -1);
    check_int("nor can a correction be recorded",
              installation_record_ppm(&inst, 5), -1);
    check_int("and nothing applies", installation_ppm(&inst, &ppm), 0);
    check_int("so nothing would be written", inst.dirty, 0);
}

/*
 * Two receivers at one site keep separate profiles. This is the case
 * ADR-0018 exists for, and the one the B210 makes real: measuring the new
 * board must not overwrite the dongle's correction for the same room.
 */
static void test_two_receivers_at_one_site(void) {
    struct installation inst;
    int ppm = 0;

    installation_reset(&inst);
    installation_set_id(inst.site, "home-sala-estar");

    installation_identify(&inst, "77771111153705700", NULL);
    check_int("the dongle records its correction",
              installation_record_ppm(&inst, -31), 0);

    installation_identify(&inst, "b210-0001", NULL);
    check_int("the other receiver has none yet",
              installation_ppm(&inst, &ppm), 0);
    check_int("it records its own", installation_record_ppm(&inst, 2), 0);
    check_int("which applies", installation_ppm(&inst, &ppm), 1);
    check_int("and is its own value", ppm, 2);

    installation_identify(&inst, "77771111153705700", NULL);
    check_int("the dongle's is untouched", installation_ppm(&inst, &ppm), 1);
    check_int("still -31", ppm, -31);
    check_int("two profiles, one site", inst.profile_count, 2);
}

/* And one receiver at two sites, which is the other half of the key. */
static void test_one_receiver_at_two_sites(void) {
    struct installation inst;
    int ppm = 0;

    installation_reset(&inst);
    installation_identify(&inst, "77771111153705700", NULL);

    installation_set_id(inst.site, "home-sala-estar");
    installation_record_ppm(&inst, -31);
    installation_set_id(inst.site, "field");
    check_int("a different site has no correction yet",
              installation_ppm(&inst, &ppm), 0);
    installation_record_ppm(&inst, -28);

    installation_set_id(inst.site, "home-sala-estar");
    check_int("walking back restores that site's", installation_ppm(&inst,
                                                                   &ppm),
              1);
    check_int("which is -31", ppm, -31);
    check_int("two profiles, one receiver", inst.profile_count, 2);
}

/*
 * ADR-0022: a history belongs to a receiving setup -- receiver, site and
 * antenna. Changing any member selects a different baseline rather than
 * mutating the previous one.
 */
static void test_the_history_key_is_the_whole_setup(void) {
    struct installation inst;
    char whip[160], roof[160], other_site[160], other_receiver[160];

    installation_reset(&inst);
    installation_identify(&inst, "77771111153705700", NULL);
    installation_set_id(inst.site, "home-sala-estar");
    installation_set_id(inst.antenna, "telescopic");
    check_true("a complete setup has a key",
               installation_history_key(&inst, whip, sizeof(whip)) > 0);

    installation_set_id(inst.antenna, "rooftop");
    check_true("a different antenna has a key too",
               installation_history_key(&inst, roof, sizeof(roof)) > 0);
    check_true("and it is a different one", strcmp(whip, roof) != 0);

    installation_set_id(inst.antenna, "telescopic");
    installation_set_id(inst.site, "field");
    installation_history_key(&inst, other_site, sizeof(other_site));
    check_true("a different site is a different baseline",
               strcmp(whip, other_site) != 0);

    installation_set_id(inst.site, "home-sala-estar");
    installation_identify(&inst, "b210-0001", NULL);
    installation_history_key(&inst, other_receiver, sizeof(other_receiver));
    check_true("and a different receiver too",
               strcmp(whip, other_receiver) != 0);

    /* Returning to the first setup returns the first key: the identity is the
       three names and nothing about the order they were set in. */
    installation_identify(&inst, "77771111153705700", NULL);
    {
        char again[160];
        installation_history_key(&inst, again, sizeof(again));
        check_str("the same setup is the same baseline", again, whip);
    }
}

/*
 * An incomplete setup has no key, and that is the refusal rather than a
 * shortfall: a history that cannot say what produced it is what ADR-0022
 * declines to write.
 */
static void test_an_incomplete_setup_has_no_key(void) {
    struct installation inst;
    char key[160];

    installation_reset(&inst);
    check_int("nothing set", installation_history_key(&inst, key,
                                                      sizeof(key)),
              -1);

    installation_set_id(inst.site, "home-sala-estar");
    check_int("a site alone is not a setup",
              installation_history_key(&inst, key, sizeof(key)), -1);

    installation_set_id(inst.antenna, "telescopic");
    check_int("nor a site and an antenna with no receiver",
              installation_history_key(&inst, key, sizeof(key)), -1);

    installation_identify(&inst, "77771111153705700", NULL);
    check_true("all three is",
               installation_history_key(&inst, key, sizeof(key)) > 0);

    /* And it refuses to truncate: a cut key would collide with another. */
    check_int("a buffer too small is refused",
              installation_history_key(&inst, key, 8), -1);
}

/* Bounded copies, because a site name comes from a text field. */
static void test_identifiers_are_bounded(void) {
    struct installation inst;
    char lengthy[INSTALLATION_ID_MAX * 2];

    installation_reset(&inst);
    memset(lengthy, 'x', sizeof(lengthy) - 1);
    lengthy[sizeof(lengthy) - 1] = '\0';

    installation_set_id(inst.site, lengthy);
    check_size("cut to the field", strlen(inst.site), INSTALLATION_ID_MAX - 1);
    installation_set_id(inst.site, NULL);
    check_str("a null empties it", inst.site, "");
}


/*
 * One name, and it names the whole setup.
 *
 * This used to assert that the receiver-scoped name and the legacy
 * `history-<site>.txt` name coexist, via `site_history_path()`. They do not
 * coexist in any useful sense and that function is gone: ADR-0022's amendment
 * says a legacy file is inert, so the only thing worth asserting about the old
 * name is that this is not it. The legacy string is written out literally here
 * rather than built, because nothing in the program builds it any more and a
 * helper that did would be the dead code this closes.
 */
static void test_the_history_path(void) {
    struct installation inst;
    char setup[256], other[256];
    const char *legacy = "surveys/history-home-sala-estar.txt";

    installation_reset(&inst);
    installation_identify(&inst, "77771111153705700", NULL);
    installation_set_id(inst.site, "home-sala-estar");
    installation_set_id(inst.antenna, "telescopic");

    check_int("a complete setup has a path",
              installation_history_path(&inst, setup, sizeof(setup)), 0);
    check_true("under surveys/",
               strncmp(setup, "surveys/history-", 16) == 0);
    check_true("and it names all three",
               strstr(setup, "77771111153705700") != NULL &&
               strstr(setup, "home-sala-estar") != NULL &&
               strstr(setup, "telescopic") != NULL);

    /* And it is not the old site-only name, which nothing builds or reads. */
    check_true("the setup's file is not the legacy one",
               strcmp(setup, legacy) != 0);

    /* A different antenna is a different file, which is the whole point. */
    installation_set_id(inst.antenna, "rooftop");
    installation_history_path(&inst, other, sizeof(other));
    check_true("a different antenna, a different file",
               strcmp(setup, other) != 0);

    /*
     * A site typed with spaces or a slash still names one file, and one
     * inside surveys/ -- a person types what they like into that field, and a
     * slash would write somewhere else or fail. This was asserted of
     * `site_history_path()` in check-site-history until that function was
     * deleted with ADR-0022's amendment; this is the only builder left.
     */
    installation_set_id(inst.site, "Rua da Prata 2");
    installation_set_id(inst.antenna, "telescopic");
    check_int("an awkward name still resolves",
              installation_history_path(&inst, other, sizeof(other)), 0);
    check_true("under surveys/ still",
               strncmp(other, "surveys/history-", 16) == 0);
    check_true("with nothing outside the safe set",
               strpbrk(other + 16, " /.") == NULL ||
               strcmp(strpbrk(other + 16, " /.") , ".txt") == 0);

    /* An incomplete setup has no path, for the same reason it has no key. */
    installation_reset(&inst);
    installation_set_id(inst.site, "home-sala-estar");
    check_int("a site alone has no history file",
              installation_history_path(&inst, other, sizeof(other)), -1);
}

/*
 * ADR-0022's amended clause, in code: a legacy site-only baseline is inert.
 *
 * This is the property the amendment turns on, and it is worth asserting
 * whichever way the ADR had gone -- "not merged" is the thing an operator is
 * owed, and today it is implemented as "not opened". If the reading half is
 * ever built, this check keeps meaning what it says.
 *
 * Both directions matter and they fail differently. With a receiver-scoped
 * file present, folding the legacy one in would *add* entries from an unknown
 * antenna to a known baseline -- carriers that appear on air without ever
 * having been transmitted. With no receiver-scoped file, adopting the legacy
 * one would hand a fresh setup somebody else's memory, and every real signal
 * it then hears reads as steady rather than new.
 *
 * It writes files, so it runs in a temporary directory of its own: the history
 * path is relative to the working directory, and this must not touch the
 * operator's `surveys/`.
 */
static void test_a_legacy_baseline_is_inert(void) {
    char scratch[] = "/tmp/sdrprobe-history-XXXXXX";
    char setup_path[512];
    char cwd[4096];
    struct installation inst;
    struct site_history legacy, kept, got;
    const char *legacy_path = "surveys/history-home-sala-estar.txt";
    const double legacy_hz = 100100000.0, kept_hz = 89500000.0;
    const float one_db = -20.0f;
    int hour = 12;

    if (!getcwd(cwd, sizeof(cwd)) || !mkdtemp(scratch) || chdir(scratch) < 0) {
        check_true("a temporary directory to write histories in", 0);
        return;
    }
    if (mkdir("surveys", 0755) < 0) {
        check_true("surveys/ in it", 0);
        (void)chdir(cwd);
        return;
    }

    installation_reset(&inst);
    installation_identify(&inst, "77771111153705700", NULL);
    installation_set_id(inst.site, "home-sala-estar");
    installation_set_id(inst.antenna, "telescopic");
    check_int("the setup has a history path",
              installation_history_path(&inst, setup_path, sizeof(setup_path)),
              0);
    check_true("which is not the legacy name",
               strcmp(setup_path, legacy_path) != 0);

    /* A rich legacy baseline: nine sweeps, from nobody knows what. */
    site_history_init(&legacy, "home-sala-estar");
    site_history_merge(&legacy, &legacy_hz, &one_db, &one_db, 1, 2000.0, hour);
    check_int("the legacy file is written",
              site_history_save_path(legacy_path, &legacy), 0);

    /* And a thin one that does say what took it. */
    site_history_init(&kept, "home-sala-estar");
    site_history_merge(&kept, &kept_hz, &one_db, &one_db, 1, 2000.0, hour);
    check_int("and the receiving setup's own",
              installation_history_save(&inst, &kept), 0);

    check_int("the setup's history loads", installation_history_load(&inst,
                                                                    &got), 0);
    check_int("with its own one entry and no more", got.count, 1);
    check_true("which is the frequency it recorded",
               site_history_find(&got, kept_hz, 2000.0) != NULL);
    check_true("and the legacy baseline's is not in it",
               site_history_find(&got, legacy_hz, 2000.0) == NULL);
    check_int("nor are its sweeps counted", got.sweeps, 1);

    /* Now take the setup's file away and leave only the legacy one. */
    check_int("the setup's file is removed", remove(setup_path), 0);
    check_int("a setup with no history of its own reads none",
              installation_history_load(&inst, &got), 1);
    check_int("rather than adopting the legacy baseline", got.count, 0);
    check_int("and it starts at no sweeps", got.sweeps, 0);

    (void)remove(legacy_path);
    (void)rmdir("surveys");
    (void)chdir(cwd);
    (void)rmdir(scratch);
}

int main(void) {
    test_the_history_path();
    test_a_legacy_baseline_is_inert();
    test_who_the_receiver_is();
    test_a_legacy_correction_is_not_applied_until_claimed();
    test_an_unidentified_receiver_cannot_claim();
    test_two_receivers_at_one_site();
    test_one_receiver_at_two_sites();
    test_the_history_key_is_the_whole_setup();
    test_an_incomplete_setup_has_no_key();
    test_identifiers_are_bounded();
    return check_report("what a measurement belongs to");
}
