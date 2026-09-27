#ifndef TEXT_WRAP_H
#define TEXT_WRAP_H

#include <stddef.h>

/*
 * Breaking a line of text into several, at spaces where there are any.
 *
 * Written for radio text, which is up to sixty-four characters and arrives
 * where a panel a third of the window wide has room for about forty. Handing
 * that to a component that ellipsises produced "Cultura em antena2.rtp..."
 * -- a message truncated exactly where the useful part of it begins.
 *
 * How wide a line may be is a question about a font and belongs to the caller,
 * which has one. Where the breaks go given that width is arithmetic, and is
 * here so it can be checked without a window (ADR-0012).
 */

struct text_wrap_line {
    int start;    /* index into the text */
    int length;   /* characters, with no trailing space */
};

/*
 * Fill `lines` with up to `capacity` of them and return how many were used.
 *
 * Breaks at the last space that fits. A word longer than a whole line is
 * broken mid-word rather than dropped or allowed to overrun -- a URL with no
 * spaces in it is exactly the case this has to survive, and radio text is
 * full of them.
 */
static inline int text_wrap(const char *text, int columns,
                            struct text_wrap_line *lines, int capacity) {
    int at = 0, used = 0, length;

    if (!text || !lines || columns <= 0 || capacity <= 0)
        return 0;
    length = 0;
    while (text[length])
        length++;

    while (at < length && used < capacity) {
        int remaining = length - at;
        int take = remaining < columns ? remaining : columns;
        int brk = take;

        if (take == columns && at + take < length) {
            int k;
            /* The last space that fits, if there is one. */
            for (k = take; k > 0; k--)
                if (text[at + k - 1] == ' ') {
                    brk = k;
                    break;
                }
            /* A word wider than the line: break it rather than overrun. */
            if (brk == take && text[at + take] != ' ') {
                int space = 0;
                for (k = 0; k < take; k++)
                    if (text[at + k] == ' ')
                        space = 1;
                if (!space)
                    brk = take;
            }
        }
        lines[used].start = at;
        {
            int end = brk;
            while (end > 0 && text[at + end - 1] == ' ')
                end--;
            lines[used].length = end;
        }
        used++;
        at += brk;
        /* Swallow the spaces a break landed on, so the next line does not
           start with them. */
        while (at < length && text[at] == ' ')
            at++;
    }
    return used;
}

#endif
