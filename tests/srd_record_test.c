#include "check.h"
#include "srd_record.h"

#include <math.h>

/*
 * The SRD record-duration field: what a typed string may mean, checked
 * without a window, a receiver, or a click on Record.
 */

static void check_ok(const char *text, double expect) {
    double out = -999.0;
    int rc = srd_record_seconds(text, &out);
    check_msg(rc == 0 && fabs(out - expect) < 1e-9,
              "srd_record_seconds(\"%s\") = rc %d out %.6f, expected 0/%.6f\n",
              text ? text : "(null)", rc, out, expect);
}

static void check_refused(const char *text) {
    double out = -999.0;
    int rc = srd_record_seconds(text, &out);
    check_msg(rc < 0 && out == -999.0,
              "srd_record_seconds(\"%s\") = rc %d out %.6f, expected refusal "
              "with *out untouched\n",
              text ? text : "(null)", rc, out);
}

static void test_default(void) {
    check_ok(NULL, SRD_RECORD_SECONDS_DEFAULT);
    check_ok("", SRD_RECORD_SECONDS_DEFAULT);
}

static void test_plain_values(void) {
    check_ok("2", 2.0);
    check_ok("0.5", 0.5);
    check_ok("2.5", 2.5);
    check_ok("30", 30.0);
    check_ok("0.1", 0.1);
}

static void test_bounds(void) {
    /* Exactly at each bound is accepted; one step past is refused. */
    check_ok("0.1", SRD_RECORD_SECONDS_MIN);
    check_ok("30", SRD_RECORD_SECONDS_MAX);
    check_refused("0.09");
    check_refused("30.1");
    check_refused("0");
    check_refused("31");
    check_refused("-1");
}

static void test_malformed(void) {
    check_refused("abc");
    check_refused("2.5.1");
    check_refused("2s");
    check_refused("2 ");
    check_refused("nan");
    check_refused("inf");
}

static void test_null_out(void) {
    check_msg(srd_record_seconds("2", NULL) < 0,
              "srd_record_seconds with a NULL out must refuse\n");
}

int main(void) {
    test_default();
    test_plain_values();
    test_bounds();
    test_malformed();
    test_null_out();
    return check_report("the SRD record-duration field: range and refusals");
}
