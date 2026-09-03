#include "check.h"

#include "row_list.h"

/*
 * The candidate list's rows and its scroll.
 *
 * The list had no scroll: it drew the rows that fitted and stopped, so a sweep
 * that found sixty candidates left forty-three unreachable, and the keyboard
 * walk looked dead because it was moving a selection that had left the drawn
 * rows. Every rule that keeps a selection on screen is here rather than in the
 * draw call, so it can be checked without a window (ADR-0012).
 */

static const struct row_list_metrics SURVEY = SURVEY_LIST_METRICS;

static Rectangle panel(float height) {
    Rectangle r = { 80.0f, 680.0f, 740.0f, height };
    return r;
}

static void test_how_many_rows_fit(void) {
    /* The panel the survey actually draws: 440 px holds seventeen rows. */
    check_int("a 440 px panel holds 16 rows",
              row_list_rows(panel(440.0f), SURVEY), 16);
    check_int("a taller one holds more", row_list_rows(panel(660.0f), SURVEY), 26);
    /* A panel too short for its own caption holds none rather than a negative
       number, which would index backwards off the array. */
    check_int("a panel shorter than its caption holds none",
              row_list_rows(panel(40.0f), SURVEY), 0);
    check_int("and one of no height at all",
              row_list_rows(panel(0.0f), SURVEY), 0);
}

static void test_how_far_it_scrolls(void) {
    check_int("a list that fits does not scroll",
              row_list_max_scroll(10, 17), 0);
    check_int("nor one exactly filling the panel",
              row_list_max_scroll(17, 17), 0);
    /* The last candidate lands on the last row, not past it: a list that
       scrolls into empty space below is how a reader concludes the rest was
       lost. */
    check_int("sixty candidates in seventeen rows scroll to 43",
              row_list_max_scroll(60, 17), 43);
    check_int("an empty list does not scroll",
              row_list_max_scroll(0, 17), 0);

    check_int("scrolling past the end is clamped",
              row_list_clamp_scroll(99, 60, 17), 43);
    check_int("and above the start",
              row_list_clamp_scroll(-5, 60, 17), 0);
    check_int("a scroll in range is left alone",
              row_list_clamp_scroll(20, 60, 17), 20);
    /* A list that shrank under a scroll -- a narrower zoom, fewer candidates
       in view -- comes back rather than showing a panel of nothing. */
    check_int("a shrunken list pulls its scroll back",
              row_list_clamp_scroll(43, 20, 17), 3);
}

static void test_the_selection_stays_on_screen(void) {
    int rows = 17;   /* pinned, so the arithmetic is read here not measured */

    /* Stepping down inside the window moves nothing. */
    check_int("a rank already on screen does not scroll",
              row_list_scroll_to(0, 5, 60, rows), 0);
    check_int("nor the last row on screen",
              row_list_scroll_to(0, 16, 60, rows), 0);
    /* One past it scrolls by exactly one row, so a walk down a long list
       creeps rather than jumping the selection to the middle every press. */
    check_int("one past the last row scrolls by one",
              row_list_scroll_to(0, 17, 60, rows), 1);
    check_int("and again", row_list_scroll_to(1, 18, 60, rows), 2);
    /* Walking back up does the same at the other edge. */
    check_int("above the first row scrolls back by one",
              row_list_scroll_to(10, 9, 60, rows), 9);
    check_int("a rank inside the window on the way up stays",
              row_list_scroll_to(10, 20, 60, rows), 10);

    /*
     * A rank nowhere near the window: clicking a peak in the chart picks by
     * frequency, and the loudest thing in a band can be fortieth in the list.
     * It lands against the edge it came from and no further.
     */
    check_int("a far rank below lands on the last row",
              row_list_scroll_to(0, 45, 60, rows), 29);
    check_int("a far rank above lands on the first",
              row_list_scroll_to(40, 2, 60, rows), 2);
    /* The very last candidate is reachable, and does not scroll past its
       own end. */
    check_int("the last candidate sits at the maximum scroll",
              row_list_scroll_to(0, 59, 60, rows), 43);

    check_int("no selection just clamps",
              row_list_scroll_to(99, -1, 60, rows), 43);
    check_int("and a panel with no rows scrolls nowhere",
              row_list_scroll_to(5, 3, 60, 0), 0);

    /*
     * The property the whole thing exists for: after following a rank, that
     * rank is on screen. Over every combination of scroll and rank, because
     * a list that hides the row it just selected is the bug being fixed.
     */
    {
        int count = 60, scroll, rank;
        for (scroll = 0; scroll <= 60; scroll++)
        for (rank = 0; rank < count; rank++) {
            int now = row_list_scroll_to(scroll, rank, count, rows);
            check_msg(rank >= now && rank < now + rows,
                      "rank %d from scroll %d landed at %d, off screen\n",
                      rank, scroll, now);
            check_msg(now >= 0 && now <= row_list_max_scroll(count, rows),
                      "rank %d from scroll %d gave scroll %d, out of range\n",
                      rank, scroll, now);
        }
    }
}

/*
 * The pointer and the draw must agree about which candidate a row is. They
 * used to be two copies of the same two literals, which is exactly how a list
 * selects a different row from the one under the cursor.
 */
static void test_which_row_the_pointer_is_over(void) {
    Rectangle rect = panel(440.0f);
    int rows = row_list_rows(rect, SURVEY);
    Vector2 p;

    p.x = rect.x + 40.0f;

    p.y = row_list_row_y(rect, SURVEY, 0) + 4.0f;
    check_int("the first row, unscrolled",
              row_list_rank_at(rect, SURVEY, 0, 60, rows, p), 0);
    p.y = row_list_row_y(rect, SURVEY, 3) + 4.0f;
    check_int("the fourth row", row_list_rank_at(rect, SURVEY, 0, 60, rows, p), 3);
    /* Scrolled, the same row is a different candidate -- the whole point. */
    check_int("the fourth row scrolled by ten",
              row_list_rank_at(rect, SURVEY, 10, 60, rows, p), 13);

    /* The caption is not a row. Clicking "Candidates (60)" used to select the
       first one, because the hit test divided a negative number. */
    p.y = rect.y + 6.0f;
    check_int("the caption selects nothing",
              row_list_rank_at(rect, SURVEY, 0, 60, rows, p), -1);

    /* Nothing outside the panel, and no row past the end of the list. */
    p.y = rect.y - 5.0f;
    check_int("above the panel", row_list_rank_at(rect, SURVEY, 0, 60, rows, p), -1);
    p.y = rect.y + rect.height + 5.0f;
    check_int("below it", row_list_rank_at(rect, SURVEY, 0, 60, rows, p), -1);
    p.x = rect.x - 5.0f;
    p.y = row_list_row_y(rect, SURVEY, 2) + 4.0f;
    check_int("beside it", row_list_rank_at(rect, SURVEY, 0, 60, rows, p), -1);

    p.x = rect.x + 40.0f;
    p.y = row_list_row_y(rect, SURVEY, 9) + 4.0f;
    check_int("an empty row past a short list",
              row_list_rank_at(rect, SURVEY, 0, 5, rows, p), -1);
    /* Scrolled to the end of a shorter list, the rows past its last
       candidate are empty and select nothing: 43 + 9 is 52, and there are
       only 50. */
    check_int("and past the end of a scrolled one",
              row_list_rank_at(rect, SURVEY, 43, 50, rows, p), -1);
    check_int("while the rows before it still answer",
              row_list_rank_at(rect, SURVEY, 43, 50, rows,
                                  (Vector2){ rect.x + 40.0f,
                                             row_list_row_y(rect, SURVEY, 3) +
                                                 4.0f }), 46);

    /* Every drawn row maps back to the candidate drawn there. */
    {
        int row;
        for (row = 0; row < rows; row++) {
            Vector2 q;
            q.x = rect.x + 40.0f;
            q.y = row_list_row_y(rect, SURVEY, row) + SURVEY.row_h / 2.0f;
            check_msg(row_list_rank_at(rect, SURVEY, 7, 60, rows, q) == 7 + row,
                      "row %d drawn as candidate %d reads back as %d\n", row,
                      7 + row, row_list_rank_at(rect, SURVEY, 7, 60, rows, q));
        }
    }
}

int main(void) {
    test_how_many_rows_fit();
    test_how_far_it_scrolls();
    test_the_selection_stays_on_screen();
    test_which_row_the_pointer_is_over();

    return check_report("list rows, scroll and hit test");
}
