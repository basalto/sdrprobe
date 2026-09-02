#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void copy_value(char *out, size_t size, const char *value) {
    size_t length = strlen(value);
    if (length >= size)
        length = size - 1;
    memcpy(out, value, length);
    out[length] = '\0';
}

void config_defaults(struct config *config) {
    if (!config)
        return;
    memset(config, 0, sizeof(*config));
    copy_value(config->antenna, sizeof(config->antenna),
               CONFIG_ANTENNA_DEFAULT);
    config->site[0] = '\0';
    /* The default is in the list from the start, so the picker always has
       something in it rather than being empty until somebody types. */
    copy_value(config->antennas[0], sizeof(config->antennas[0]),
               CONFIG_ANTENNA_DEFAULT);
    config->antenna_count = 1;
}

int config_remember_site(struct config *config, const char *site) {
    int i, found = -1;
    struct config_site moved;

    if (!config || !site || !*site)
        return 0;
    for (i = 0; i < config->site_count; i++)
        if (!strcmp(config->sites[i].label, site))
            found = i;
    if (found == 0)
        return 0;                       /* already where it belongs */
    if (found > 0) {
        moved = config->sites[found];
        for (i = found; i > 0; i--)
            config->sites[i] = config->sites[i - 1];
        config->sites[0] = moved;       /* its correction travels with it */
        return 0;
    }
    /* New: push it on the front, dropping the least recently used if full. */
    if (config->site_count < CONFIG_SITES_MAX)
        config->site_count++;
    for (i = config->site_count - 1; i > 0; i--)
        config->sites[i] = config->sites[i - 1];
    memset(&config->sites[0], 0, sizeof(config->sites[0]));
    copy_value(config->sites[0].label, sizeof(config->sites[0].label), site);
    return 1;
}

int config_remember_antenna(struct config *config, const char *antenna) {
    int i, found = -1;
    char moved[CONFIG_VALUE_MAX];

    if (!config || !antenna || !*antenna)
        return 0;
    for (i = 0; i < config->antenna_count; i++)
        if (!strcmp(config->antennas[i], antenna))
            found = i;
    if (found == 0)
        return 0;
    if (found > 0) {
        copy_value(moved, sizeof(moved), config->antennas[found]);
        for (i = found; i > 0; i--)
            copy_value(config->antennas[i], sizeof(config->antennas[i]),
                       config->antennas[i - 1]);
        copy_value(config->antennas[0], sizeof(config->antennas[0]), moved);
        return 0;
    }
    if (config->antenna_count < CONFIG_ANTENNAS_MAX)
        config->antenna_count++;
    for (i = config->antenna_count - 1; i > 0; i--)
        copy_value(config->antennas[i], sizeof(config->antennas[i]),
                   config->antennas[i - 1]);
    copy_value(config->antennas[0], sizeof(config->antennas[0]), antenna);
    return 1;
}

int config_site_ppm(const struct config *config, const char *site) {
    int i;
    if (!config || !site || !*site)
        return 0;
    for (i = 0; i < config->site_count; i++)
        if (!strcmp(config->sites[i].label, site))
            return config->sites[i].ppm;
    return 0;
}

int config_set_site_ppm(struct config *config, const char *site, int ppm) {
    int i;
    if (!config || !site || !*site)
        return 0;
    config_remember_site(config, site);
    for (i = 0; i < config->site_count; i++)
        if (!strcmp(config->sites[i].label, site)) {
            if (config->sites[i].ppm == ppm)
                return 0;
            config->sites[i].ppm = ppm;
            return 1;
        }
    return 0;
}

int config_parse(const char *text, struct config *config) {
    const char *line = text;
    int recognised = 0;

    if (!text || !config)
        return 0;
    config_defaults(config);
    while (*line) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        char buffer[CONFIG_VALUE_MAX * 2];
        char *key, *value;

        if (length >= sizeof(buffer))
            length = sizeof(buffer) - 1;
        memcpy(buffer, line, length);
        buffer[length] = '\0';
        line = end ? end + 1 : line + strlen(line);

        /* Trim, then skip blanks and comments. */
        key = buffer;
        while (*key == ' ' || *key == '\t')
            key++;
        while (*key && buffer[strlen(buffer) - 1] == ' ')
            buffer[strlen(buffer) - 1] = '\0';
        if (!*key || *key == '#')
            continue;

        value = key;
        while (*value && *value != ' ' && *value != '\t')
            value++;
        if (*value)
            *value++ = '\0';
        while (*value == ' ' || *value == '\t')
            value++;

        if (!strcmp(key, "antenna")) {
            copy_value(config->antenna, sizeof(config->antenna), value);
            recognised++;
        } else if (!strcmp(key, "site")) {
            copy_value(config->site, sizeof(config->site), value);
            recognised++;
        } else if (!strcmp(key, "known_antenna")) {
            if (*value) {
                /* Read straight in, in file order; the default is already
                   there from config_defaults, so remember rather than append
                   -- otherwise a file naming it once holds it twice. */
                config_remember_antenna(config, value);
            }
            recognised++;
        } else if (!strcmp(key, "known_site")) {
            /* `known_site <ppm> <label>`, appended in file order, which is
               most recent first. A file written before corrections were kept
               per site has no number in front; the label then starts where the
               number would be, and it reads as uncorrected. */
            char *label = value;
            int ppm = 0;
            if ((*value == '-' || *value == '+' ||
                 (*value >= '0' && *value <= '9'))) {
                char *after = value;
                long parsed = strtol(value, &after, 10);
                if (after != value && (*after == ' ' || *after == '\t')) {
                    ppm = (int)parsed;
                    label = after;
                    while (*label == ' ' || *label == '\t')
                        label++;
                }
            }
            if (config->site_count < CONFIG_SITES_MAX && *label) {
                struct config_site *entry = &config->sites[config->site_count++];
                memset(entry, 0, sizeof(*entry));
                copy_value(entry->label, sizeof(entry->label), label);
                entry->ppm = ppm;
            }
            recognised++;
        } else if (config->unknown_count < CONFIG_UNKNOWN_MAX) {
            /* Not ours. Keep it so saving does not throw away a setting a
               newer build understands. */
            snprintf(config->unknown[config->unknown_count],
                     sizeof(config->unknown[0]), "%s %s", key, value);
            config->unknown_count++;
        }
    }
    return recognised;
}

int config_format(const struct config *config, char *out, size_t size) {
    int written, i;
    if (!config || !out)
        return -1;
    written = snprintf(out, size,
                       "# sdrprobe. What describes the receiver's situation\n"
                       "# rather than one run of it. Edited by --antenna and\n"
                       "# --site, and by hand if you prefer.\n"
                       "antenna %s\n", config->antenna);
    if (written < 0 || (size_t)written >= size)
        return -1;
    if (config->site[0]) {
        int more = snprintf(out + written, size - (size_t)written,
                            "site %s\n", config->site);
        if (more < 0 || (size_t)(written + more) >= size)
            return -1;
        written += more;
    }
    for (i = config->antenna_count - 1; i >= 0; i--) {
        /* Written oldest first so that reading them back through
           config_remember_antenna() -- which pushes each to the front --
           restores the order they are in now. */
        int more = snprintf(out + written, size - (size_t)written,
                            "known_antenna %s\n", config->antennas[i]);
        if (more < 0 || (size_t)(written + more) >= size)
            return -1;
        written += more;
    }
    for (i = 0; i < config->site_count; i++) {
        int more = snprintf(out + written, size - (size_t)written,
                            "known_site %d %s\n", config->sites[i].ppm,
                            config->sites[i].label);
        if (more < 0 || (size_t)(written + more) >= size)
            return -1;
        written += more;
    }
    for (i = 0; i < config->unknown_count; i++) {
        int more = snprintf(out + written, size - (size_t)written, "%s\n",
                            config->unknown[i]);
        if (more < 0 || (size_t)(written + more) >= size)
            return -1;
        written += more;
    }
    return written;
}

int config_path(char *out, size_t size) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    int written;

    if (!out)
        return -1;
    if (xdg && *xdg)
        written = snprintf(out, size, "%s/sdrprobe/config", xdg);
    else if (home && *home)
        written = snprintf(out, size, "%s/.config/sdrprobe/config", home);
    else
        return -1;
    return (written < 0 || (size_t)written >= size) ? -1 : 0;
}

int config_load(struct config *config) {
    char path[512], text[4096];
    size_t got;
    FILE *file;

    config_defaults(config);
    if (config_path(path, sizeof(path)) < 0)
        return 1;
    file = fopen(path, "rb");
    if (!file)
        return 1;                 /* no file yet is not a failure */
    got = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[got] = '\0';
    config_parse(text, config);
    return 0;
}

int config_save(const struct config *config) {
    char path[512], text[4096], *slash;
    FILE *file;
    int length;

    if (config_path(path, sizeof(path)) < 0) {
        fprintf(stderr, "No HOME or XDG_CONFIG_HOME; not saving settings\n");
        return -1;
    }
    length = config_format(config, text, sizeof(text));
    if (length < 0) {
        fprintf(stderr, "Settings do not fit in %zu bytes\n", sizeof(text));
        return -1;
    }
    slash = strrchr(path, '/');
    if (slash) {
        *slash = '\0';
        /* One level is enough in practice: ~/.config exists, and mkdir on an
           existing directory is not an error worth reporting. */
        if (mkdir(path, 0755) < 0 && errno != EEXIST) {
            char parent[512];
            char *up;
            snprintf(parent, sizeof(parent), "%s", path);
            up = strrchr(parent, '/');
            if (up) {
                *up = '\0';
                mkdir(parent, 0755);
            }
            if (mkdir(path, 0755) < 0 && errno != EEXIST) {
                fprintf(stderr, "Could not create %s: %s\n", path,
                        strerror(errno));
                return -1;
            }
        }
        *slash = '/';
    }
    file = fopen(path, "wb");
    if (!file) {
        fprintf(stderr, "Could not write %s: %s\n", path, strerror(errno));
        return -1;
    }
    fwrite(text, 1, (size_t)length, file);
    fclose(file);
    return 0;
}
