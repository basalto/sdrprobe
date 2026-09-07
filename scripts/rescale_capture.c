/*
 * rescale_capture -- an 8-bit capture into a 16-bit container, exactly.
 *
 * `.scratch/device-model/issues/01-a-format-change-moves-no-answer.md` is the
 * ticket. Everything in that spec is a change to how samples are represented,
 * and the only way to know such a change is harmless is to make it against a
 * corpus whose right answers are already pinned. testfiles/ is that corpus and
 * it is 8-bit; this writes the same signal in the container a 12-bit device
 * delivers, so the two can be compared through the format layer.
 *
 * The scaling is `(byte - 127.5) * 16`:
 *
 *   - times sixteen rather than a shift that fills the container, because that
 *     is what an AD9361 actually delivers -- twelve bits sitting in sixteen --
 *     and a scale that filled the container would hide the overflow this is
 *     meant to find;
 *   - exact and lossless, because `byte - 127.5` is a multiple of 0.5 and
 *     sixteen times it is a multiple of 8, comfortably inside int16. So a
 *     disagreement downstream is a bug in the format layer and never rounding.
 *
 * **Full scale of the result is 2040.0, not 2047.5**, and the difference is
 * load-bearing rather than pedantic. 127.5 x 16 is 2040; a real 12-bit ADC
 * rails at 2047.5. This file's samples came from an 8-bit part, so 2040 is
 * what its full scale *is*, and normalising by it reproduces the 8-bit floats
 * bit for bit -- all 256 values, no tolerance needed. Normalising by 2047.5
 * instead misses every one of them, by up to 3.7e-3. A sidecar claiming 2047.5
 * would be a statement about a device this data never went through.
 *
 * Output is signed 16-bit little-endian interleaved I/Q, written byte by byte
 * so the file does not depend on this machine's endianness.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bytes of input per pass. Captures here run to twelve megabytes and there is
   no reason to hold one whole. */
#define CHUNK_BYTES 65536

/* The scaling, and the full scale it produces. Both are quoted in the sidecar,
   so a reader of the generated file needs nothing but the file. */
#define RESCALE_GAIN 16
#define RESCALE_FULL_SCALE 2040.0

/* Swap a trailing .bin for .json, or append .json when there is no extension
   to replace. The same rule acquisition.c uses when it writes a sidecar. */
static void sidecar_path(const char *bin_path, char *out, size_t out_size) {
    size_t n = strlen(bin_path);
    if (n > 4 && strcmp(bin_path + n - 4, ".bin") == 0)
        snprintf(out, out_size, "%.*s.json", (int)(n - 4), bin_path);
    else
        snprintf(out, out_size, "%s.json", bin_path);
}

/* Whether a sidecar line carries exactly this key. The closing quote is part
   of the match, so "bytes" does not also match "bytes_per_pair". */
static int line_has_key(const char *line, const char *key) {
    const char *p = line;
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p != '"')
        return 0;
    p++;
    size_t n = strlen(key);
    return strncmp(p, key, n) == 0 && p[n] == '"';
}

/*
 * Brace and bracket nesting a line adds, ignoring anything inside a string.
 * The sidecars carry a "notes" array spanning several lines, so a copier that
 * treated every line as a field would put a comma inside it.
 */
static int depth_delta(const char *line) {
    int delta = 0, in_string = 0;
    for (const char *p = line; *p; p++) {
        if (in_string) {
            if (*p == '\\' && p[1])
                p++;
            else if (*p == '"')
                in_string = 0;
            continue;
        }
        if (*p == '"')
            in_string = 1;
        else if (*p == '{' || *p == '[')
            delta++;
        else if (*p == '}' || *p == ']')
            delta--;
    }
    return delta;
}

/* Strip a trailing newline in place, so the caller decides where newlines go. */
static void chomp(char *line) {
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
        line[--n] = '\0';
}

/* Drop a trailing comma. The source separates its own fields; this copier
   supplies the separator instead, so the one that arrives is one too many. */
static void strip_trailing_comma(char *line) {
    size_t n = strlen(line);
    while (n && (line[n - 1] == ' ' || line[n - 1] == '\t'))
        n--;
    if (n && line[n - 1] == ',')
        line[n - 1] = '\0';
}

/*
 * The new sidecar: the fields that changed, then every field of the source
 * that did not.
 *
 * Copying the remainder is what keeps the tuning honest. Reading
 * testfiles/gsm_arfcn_69.bin correctly depends on knowing it was tuned 400 kHz
 * below the channel, and a rescaled capture that lost that would decode to
 * nothing for a reason having nothing to do with its format. duration_seconds
 * and sample_rate_hz survive unchanged too, and should: the pair count and the
 * rate are exactly what they were.
 *
 * provenance, format and bytes are replaced rather than copied -- each is a
 * statement about the file, and all three are now false of it.
 */
static int write_sidecar(const char *in_bin, const char *out_bin,
                         unsigned long long out_bytes) {
    char in_json[4096], out_json[4096];
    sidecar_path(in_bin, in_json, sizeof(in_json));
    sidecar_path(out_bin, out_json, sizeof(out_json));

    FILE *out = fopen(out_json, "w");
    if (!out) {
        fprintf(stderr, "rescale_capture: cannot write %s: %s\n", out_json,
                strerror(errno));
        return -1;
    }

    fprintf(out, "{\n");
    fprintf(out, "  \"provenance\": \"generated by scripts/rescale_capture "
                 "from %s; not committed, rebuilt by check-sample-format\",\n",
            in_bin);
    fprintf(out, "  \"format\": \"signed 16-bit little-endian interleaved "
                 "I/Q, 0 = zero\",\n");
    fprintf(out, "  \"full_scale\": %.1f,\n", RESCALE_FULL_SCALE);
    fprintf(out, "  \"bytes_per_pair\": 4,\n");
    fprintf(out, "  \"rescale\": \"(byte - 127.5) * %d, exact and lossless; "
                 "full scale is 127.5 * %d and not a 12-bit part's 2047.5\",\n",
            RESCALE_GAIN, RESCALE_GAIN);
    fprintf(out, "  \"derived_from\": \"%s\",\n", in_bin);
    /* Our own last field, so it carries no comma; the copier below opens each
       of its top-level lines with one. */
    fprintf(out, "  \"bytes\": %llu", out_bytes);

    FILE *in = fopen(in_json, "r");
    if (in) {
        char line[8192];
        int seen_open = 0, depth = 0, skipping = 0;
        while (fgets(line, sizeof(line), in)) {
            chomp(line);
            const char *p = line;
            while (*p == ' ' || *p == '\t')
                p++;
            if (!seen_open) {
                if (*p == '{')
                    seen_open = 1;
                continue;
            }
            int delta = depth_delta(line);
            /* A line that returns to the top level ends a field, so its
               separator is the one to drop. Lines inside an array keep
               theirs -- those separate its elements, not its fields. */
            if (depth + delta <= 0)
                strip_trailing_comma(line);
            if (depth == 0) {
                /* A top-level line: either a field of its own, or the closing
                   brace, which is not ours to copy. */
                if (*p == '}' && delta < 0)
                    break;
                skipping = line_has_key(line, "provenance") ||
                           line_has_key(line, "format") ||
                           line_has_key(line, "bytes");
                if (!skipping)
                    fprintf(out, ",\n%s", line);
            } else if (!skipping) {
                /* Inside an array or a nested object: verbatim, no comma. */
                fprintf(out, "\n%s", line);
            }
            depth += delta;
            if (depth <= 0) {
                depth = 0;
                skipping = 0;
            }
        }
        fclose(in);
    } else {
        fprintf(stderr, "rescale_capture: no sidecar at %s; the generated one "
                        "carries format only\n",
                in_json);
    }

    fprintf(out, "\n}\n");
    if (fclose(out) != 0) {
        fprintf(stderr, "rescale_capture: cannot close %s: %s\n", out_json,
                strerror(errno));
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: rescale_capture <in.bin> <out.bin>\n"
                        "  8-bit unsigned interleaved I/Q in, signed 16-bit "
                        "little-endian out.\n");
        return 2;
    }
    const char *in_path = argv[1];
    const char *out_path = argv[2];

    FILE *in = fopen(in_path, "rb");
    if (!in) {
        fprintf(stderr, "rescale_capture: cannot read %s: %s\n", in_path,
                strerror(errno));
        return 1;
    }
    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "rescale_capture: cannot write %s: %s\n", out_path,
                strerror(errno));
        fclose(in);
        return 1;
    }

    uint8_t src[CHUNK_BYTES];
    uint8_t dst[2 * CHUNK_BYTES];
    unsigned long long in_bytes = 0, out_bytes = 0;
    size_t got;

    while ((got = fread(src, 1, sizeof(src), in)) > 0) {
        for (size_t n = 0; n < got; n++) {
            /* (byte - 127.5) * 16 without touching a float: the doubled value
               (2*byte - 255) is an integer, and eight times that is the same
               number. Nothing here can round. */
            int v = (2 * (int)src[n] - 255) * (RESCALE_GAIN / 2);
            dst[2 * n] = (uint8_t)(v & 0xff);
            dst[2 * n + 1] = (uint8_t)((v >> 8) & 0xff);
        }
        if (fwrite(dst, 1, 2 * got, out) != 2 * got) {
            fprintf(stderr, "rescale_capture: short write to %s: %s\n",
                    out_path, strerror(errno));
            fclose(in);
            fclose(out);
            return 1;
        }
        in_bytes += got;
        out_bytes += 2 * got;
    }

    int read_failed = ferror(in);
    fclose(in);
    if (read_failed || fclose(out) != 0) {
        fprintf(stderr, "rescale_capture: I/O error on %s\n", in_path);
        return 1;
    }
    if (in_bytes % 2 != 0)
        fprintf(stderr, "rescale_capture: %s has an odd byte count (%llu); "
                        "the last I has no Q\n",
                in_path, in_bytes);

    if (write_sidecar(in_path, out_path, out_bytes) != 0)
        return 1;

    printf("%s -> %s  %llu bytes -> %llu, full scale %.1f\n", in_path,
           out_path, in_bytes, out_bytes, RESCALE_FULL_SCALE);
    return 0;
}
