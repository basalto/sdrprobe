CFLAGS?=-O2 -g -Wall -W $(shell pkg-config --cflags librtlsdr)
LDLIBS+=$(shell pkg-config --libs librtlsdr) -lm
CC?=gcc

SRC=src
TESTS=tests
VENDOR=vendor
BUILD=build

all: sdrprobe

DSP_SRC=$(SRC)/sdr_dsp.c $(SRC)/gsm_dsp.c $(SRC)/adsb_dsp.c
APP_SRC=$(SRC)/acquisition.c $(SRC)/options.c $(SRC)/view_scope.c $(SRC)/view_gsm.c \
	$(SRC)/view_adsb.c $(SRC)/view_survey.c $(SRC)/band_plan.c \
	$(SRC)/overlay_calibration.c $(SRC)/overlay_scan.c \
	$(SRC)/overlay_settings.c $(SRC)/overlay_help.c
APP_HDR=$(SRC)/options.h $(SRC)/gsm_layout.h $(SRC)/adsb_layout.h \
	$(SRC)/survey_layout.h $(SRC)/survey_window.h $(SRC)/chrome_layout.h \
	$(SRC)/band_plan.h $(SRC)/app.h $(SRC)/view.h
DSP_HDR=$(SRC)/sdr_dsp.h $(SRC)/gsm_dsp.h $(SRC)/adsb_dsp.h
GUI_SRC=$(SRC)/sdrgui_plot.c $(SRC)/sdrgui_scope.c \
	$(SRC)/sdrgui_decode.c $(SRC)/sdrgui_widgets.c
GUI_HDR=$(SRC)/sdrgui.h
RAYGUI_FLAGS=-I$(VENDOR) $(shell pkg-config --cflags raylib)

# The vendored raygui header is not -Wall -W clean; compile it in isolation.
# The one intermediate object lives under $(BUILD)/ to keep the root tidy.
$(BUILD)/raygui_impl.o: $(SRC)/raygui_impl.c $(VENDOR)/raygui.h
	@mkdir -p $(BUILD)
	$(CC) -O2 $(RAYGUI_FLAGS) -w -c $(SRC)/raygui_impl.c -o $@

sdrprobe: $(SRC)/sdrprobe.c $(APP_SRC) $(APP_HDR) $(DSP_SRC) $(DSP_HDR) \
		$(GUI_SRC) $(GUI_HDR) $(BUILD)/raygui_impl.o
	$(CC) $(CFLAGS) $(RAYGUI_FLAGS) -pthread \
		-o $@ $(SRC)/sdrprobe.c $(APP_SRC) $(DSP_SRC) $(GUI_SRC) \
		$(BUILD)/raygui_impl.o \
		$(LDFLAGS) $(LDLIBS) $(shell pkg-config --libs raylib) -pthread

# Per-technology hardware-free DSP checks. Each technology's checks build and
# run in isolation so they are easy to inspect and extend; check-dsp runs all.
# Test sources live in $(TESTS)/ and include the DSP headers from $(SRC)/.
check-sdr-dsp: $(TESTS)/sdr_dsp_test.c $(SRC)/sdr_dsp.c $(SRC)/sdr_dsp.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/sdr_dsp_test \
		$(TESTS)/sdr_dsp_test.c $(SRC)/sdr_dsp.c -lm
	./$(BUILD)/sdr_dsp_test

check-gsm-dsp: $(TESTS)/gsm_dsp_test.c $(SRC)/gsm_dsp.c $(SRC)/gsm_dsp.h \
		$(SRC)/sdr_dsp.c $(SRC)/sdr_dsp.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/gsm_dsp_test \
		$(TESTS)/gsm_dsp_test.c $(SRC)/gsm_dsp.c $(SRC)/sdr_dsp.c -lm
	./$(BUILD)/gsm_dsp_test

# The band plan is a table, not DSP: its own check, and the only one here that
# links nothing at all.
check-band-plan: $(TESTS)/band_plan_test.c $(SRC)/band_plan.c $(SRC)/band_plan.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/band_plan_test \
		$(TESTS)/band_plan_test.c $(SRC)/band_plan.c
	./$(BUILD)/band_plan_test

check-adsb-dsp: $(TESTS)/adsb_dsp_test.c $(SRC)/adsb_dsp.c $(SRC)/adsb_dsp.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/adsb_dsp_test \
		$(TESTS)/adsb_dsp_test.c $(SRC)/adsb_dsp.c -lm
	./$(BUILD)/adsb_dsp_test

# Layout check: the GSM and ADS-B views' rectangles and the window chrome,
# pinned at several window sizes. Needs raylib's headers for the Rectangle type but not
# the library -- both layouts are pure functions of the window size, which is
# what makes them testable without opening a window.
check-layout: $(TESTS)/layout_test.c $(SRC)/gsm_layout.h \
		$(SRC)/adsb_layout.h $(SRC)/chrome_layout.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(SRC) $(shell pkg-config --cflags raylib) \
		-o $(BUILD)/layout_test $(TESTS)/layout_test.c -lm
	./$(BUILD)/layout_test

# The band survey's window arithmetic: zoom, pan, and what Sweep would sweep.
# No raylib, no receiver, no window -- which is the point. Every one of these
# decisions previously had to be checked by building an instrumented binary and
# running it against the dongle, and two of them shipped wrong.
check-survey: $(TESTS)/survey_window_test.c $(SRC)/survey_window.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/survey_window_test \
		$(TESTS)/survey_window_test.c -lm
	./$(BUILD)/survey_window_test

check-dsp: check-sdr-dsp check-gsm-dsp check-adsb-dsp check-band-plan

# White-box diagnostic walk through the GSM SCH chain (not a unit test). It
# compiles gsm_dsp.c in (to reach its statics), so it links only sdr_dsp.c.
FILE ?= testfiles/gsm_arfcn_69.bin
probe-gsm-chain: scripts/gsm_chain_probe.c $(SRC)/gsm_dsp.c $(SRC)/gsm_dsp.h \
		$(SRC)/sdr_dsp.c $(SRC)/sdr_dsp.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/gsm_chain_probe \
		scripts/gsm_chain_probe.c $(SRC)/sdr_dsp.c -lm
	./$(BUILD)/gsm_chain_probe $(FILE)

# White-box diagnostic walk through the ADS-B Mode S decode chain.
FILE_ADSB ?= testfiles/adsb_modes1.bin
probe-adsb-chain: scripts/adsb_chain_probe.c $(SRC)/adsb_dsp.c $(SRC)/adsb_dsp.h \
		$(SRC)/sdr_dsp.c $(SRC)/sdr_dsp.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/adsb_chain_probe \
		scripts/adsb_chain_probe.c $(SRC)/sdr_dsp.c -lm
	./$(BUILD)/adsb_chain_probe $(FILE_ADSB)

# What the DSP costs per sample block, against the 65.5 ms one block covers.
# BENCH_ARCH=-march=native answers the SIMD question by measuring it: the
# default build has no -march, so the compiler targets the baseline ISA.
BENCH_ARCH ?=
bench-dsp: scripts/dsp_bench.c $(DSP_SRC) $(DSP_HDR)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(BENCH_ARCH) -I$(SRC) -o $(BUILD)/dsp_bench \
		scripts/dsp_bench.c $(DSP_SRC) -lm
	./$(BUILD)/dsp_bench

clean:
	rm -rf sdrprobe $(BUILD)

.PHONY: all check-sdr-dsp check-gsm-dsp check-adsb-dsp check-band-plan check-dsp check-layout check-survey probe-gsm-chain probe-adsb-chain bench-dsp clean
