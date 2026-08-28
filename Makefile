CFLAGS?=-O2 -g -Wall -W $(shell pkg-config --cflags librtlsdr)
LDLIBS+=$(shell pkg-config --libs librtlsdr) -lm
CC?=gcc
PROGNAME=rtl_init

SRC=src
TESTS=tests

all: $(PROGNAME) rtl_tui

$(PROGNAME): $(SRC)/rtl_init.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

rtl_tui: $(SRC)/rtl_tui.c $(SRC)/tui.c $(SRC)/tui.h
	$(CC) $(CFLAGS) -o $@ $(SRC)/rtl_tui.c $(SRC)/tui.c $(LDFLAGS) $(LDLIBS)

DSP_SRC=$(SRC)/sdr_dsp.c $(SRC)/gsm_dsp.c
DSP_HDR=$(SRC)/sdr_dsp.h $(SRC)/gsm_dsp.h

rtl_raylib: $(SRC)/rtl_raylib.c $(DSP_SRC) $(DSP_HDR)
	$(CC) $(CFLAGS) $(shell pkg-config --cflags raylib) -pthread \
		-o $@ $(SRC)/rtl_raylib.c $(DSP_SRC) $(LDFLAGS) $(LDLIBS) \
		$(shell pkg-config --libs raylib) -pthread

# Per-technology hardware-free DSP checks. Each technology's checks build and
# run in isolation so they are easy to inspect and extend; check-dsp runs all.
# Test sources live in $(TESTS)/ and include the DSP headers from $(SRC)/.
check-sdr-dsp: $(TESTS)/sdr_dsp_test.c $(SRC)/sdr_dsp.c $(SRC)/sdr_dsp.h
	$(CC) $(CFLAGS) -I$(SRC) -o sdr_dsp_test \
		$(TESTS)/sdr_dsp_test.c $(SRC)/sdr_dsp.c -lm
	./sdr_dsp_test

check-gsm-dsp: $(TESTS)/gsm_dsp_test.c $(SRC)/gsm_dsp.c $(SRC)/gsm_dsp.h \
		$(SRC)/sdr_dsp.c $(SRC)/sdr_dsp.h
	$(CC) $(CFLAGS) -I$(SRC) -o gsm_dsp_test \
		$(TESTS)/gsm_dsp_test.c $(SRC)/gsm_dsp.c $(SRC)/sdr_dsp.c -lm
	./gsm_dsp_test

check-dsp: check-sdr-dsp check-gsm-dsp

# Backwards-compatible alias.
check-raylib-dsp: check-dsp

clean:
	rm -f $(PROGNAME) rtl_tui rtl_raylib sdr_dsp_test gsm_dsp_test

.PHONY: all check-sdr-dsp check-gsm-dsp check-dsp check-raylib-dsp clean
