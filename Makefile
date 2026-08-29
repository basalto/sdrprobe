CFLAGS?=-O2 -g -Wall -W $(shell pkg-config --cflags librtlsdr)
LDLIBS+=$(shell pkg-config --libs librtlsdr) -lm
CC?=gcc
PROGNAME=rtl_init

SRC=src
TESTS=tests
VENDOR=vendor

all: $(PROGNAME) rtl_tui

$(PROGNAME): $(SRC)/rtl_init.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

rtl_tui: $(SRC)/rtl_tui.c $(SRC)/tui.c $(SRC)/tui.h
	$(CC) $(CFLAGS) -o $@ $(SRC)/rtl_tui.c $(SRC)/tui.c $(LDFLAGS) $(LDLIBS)

DSP_SRC=$(SRC)/sdr_dsp.c $(SRC)/gsm_dsp.c
DSP_HDR=$(SRC)/sdr_dsp.h $(SRC)/gsm_dsp.h
GUI_SRC=$(SRC)/sdrgui.c
GUI_HDR=$(SRC)/sdrgui.h
RAYGUI_FLAGS=-I$(VENDOR) $(shell pkg-config --cflags raylib)

# The vendored raygui header is not -Wall -W clean; compile it in isolation.
raygui_impl.o: $(SRC)/raygui_impl.c $(VENDOR)/raygui.h
	$(CC) -O2 $(RAYGUI_FLAGS) -w -c $(SRC)/raygui_impl.c -o $@

rtl_raylib: $(SRC)/rtl_raylib.c $(DSP_SRC) $(DSP_HDR) $(GUI_SRC) $(GUI_HDR) \
		raygui_impl.o
	$(CC) $(CFLAGS) $(RAYGUI_FLAGS) -pthread \
		-o $@ $(SRC)/rtl_raylib.c $(DSP_SRC) $(GUI_SRC) raygui_impl.o \
		$(LDFLAGS) $(LDLIBS) $(shell pkg-config --libs raylib) -pthread

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
	rm -f $(PROGNAME) rtl_tui rtl_raylib sdr_dsp_test gsm_dsp_test raygui_impl.o

.PHONY: all check-sdr-dsp check-gsm-dsp check-dsp check-raylib-dsp clean
