CFLAGS?=-O2 -g -Wall -W $(shell pkg-config --cflags librtlsdr)
LDLIBS+=$(shell pkg-config --libs librtlsdr) -lm
CC?=gcc

# Commands are hidden so `make check` reads as a report rather than a wall of
# compiler lines. V=1 shows them again, which is what you want when a build
# fails rather than a check.
V?=0
Q_0=@
Q=$(Q_$(V))

SRC=src
TESTS=tests
VENDOR=vendor
BUILD=build

all: sdrprobe

DSP_SRC=$(SRC)/sdr_dsp.c $(SRC)/gsm_dsp.c $(SRC)/gsm_bcch.c $(SRC)/adsb_dsp.c \
	$(SRC)/lte_dsp.c $(SRC)/lte_mib.c
APP_SRC=$(SRC)/acquisition.c $(SRC)/options.c $(SRC)/view_scope.c $(SRC)/view_gsm.c \
	$(SRC)/view_adsb.c $(SRC)/view_lte.c $(SRC)/view_survey.c \
	$(SRC)/band_plan.c \
	$(SRC)/overlay_calibration.c $(SRC)/overlay_scan.c \
	$(SRC)/overlay_settings.c $(SRC)/overlay_help.c \
	$(SRC)/survey_report.c
APP_HDR=$(SRC)/options.h $(SRC)/gsm_layout.h $(SRC)/adsb_layout.h \
	$(SRC)/lte_layout.h \
	$(SRC)/survey_layout.h $(SRC)/survey_window.h $(SRC)/survey_sweep.h \
	$(SRC)/survey_suspect.h $(SRC)/chrome_layout.h \
	$(SRC)/band_plan.h $(SRC)/calibration_gate.h $(SRC)/scan_plan.h \
	$(SRC)/adsb_analysis.h $(SRC)/gsm_continuity.h $(SRC)/input_route.h $(SRC)/app.h $(SRC)/view.h
DSP_HDR=$(SRC)/sdr_dsp.h $(SRC)/gsm_dsp.h $(SRC)/gsm_bcch.h $(SRC)/adsb_dsp.h \
	$(SRC)/lte_dsp.h $(SRC)/lte_mib.h $(SRC)/lte_gold.h $(SRC)/lte_scan.h
GUI_SRC=$(SRC)/sdrgui_plot.c $(SRC)/sdrgui_scope.c \
	$(SRC)/sdrgui_decode.c $(SRC)/sdrgui_widgets.c
GUI_HDR=$(SRC)/sdrgui.h $(SRC)/sdrgui_geometry.h
RAYGUI_FLAGS=-I$(VENDOR) $(shell pkg-config --cflags raylib)

# The vendored raygui header is not -Wall -W clean; compile it in isolation.
# The one intermediate object lives under $(BUILD)/ to keep the root tidy.
$(BUILD)/raygui_impl.o: $(SRC)/raygui_impl.c $(VENDOR)/raygui.h
	@mkdir -p $(BUILD)
	$(Q)printf '  cc  %s\n' $@
	$(Q)$(CC) -O2 $(RAYGUI_FLAGS) -w -c $(SRC)/raygui_impl.c -o $@

sdrprobe: $(SRC)/sdrprobe.c $(APP_SRC) $(APP_HDR) $(DSP_SRC) $(DSP_HDR) \
		$(GUI_SRC) $(GUI_HDR) $(BUILD)/raygui_impl.o
	$(Q)printf '  cc  %s\n' $@
	$(Q)$(CC) $(CFLAGS) $(RAYGUI_FLAGS) -pthread \
		-o $@ $(SRC)/sdrprobe.c $(APP_SRC) $(DSP_SRC) $(GUI_SRC) \
		$(BUILD)/raygui_impl.o \
		$(LDFLAGS) $(LDLIBS) $(shell pkg-config --libs raylib) -pthread

# Per-technology hardware-free DSP checks. Each technology's checks build and
# run in isolation so they are easy to inspect and extend; check-dsp runs all.
# Test sources live in $(TESTS)/ and include the DSP headers from $(SRC)/.
check-sdr-dsp: $(TESTS)/sdr_dsp_test.c $(TESTS)/check.h $(SRC)/sdr_dsp.c $(SRC)/sdr_dsp.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/sdr_dsp_test \
		$(TESTS)/sdr_dsp_test.c $(SRC)/sdr_dsp.c -lm
	$(Q)./$(BUILD)/sdr_dsp_test

check-gsm-dsp: $(TESTS)/gsm_dsp_test.c $(TESTS)/check.h $(SRC)/gsm_dsp.c $(SRC)/gsm_dsp.h \
		$(SRC)/sdr_dsp.c $(SRC)/sdr_dsp.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/gsm_dsp_test \
		$(TESTS)/gsm_dsp_test.c $(SRC)/gsm_dsp.c $(SRC)/sdr_dsp.c -lm
	$(Q)./$(BUILD)/gsm_dsp_test

# The band plan is a table, not DSP: its own check, and the only one here that
# links nothing at all.
check-band-plan: $(TESTS)/band_plan_test.c $(TESTS)/check.h $(SRC)/band_plan.c $(SRC)/band_plan.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/band_plan_test \
		$(TESTS)/band_plan_test.c $(SRC)/band_plan.c
	$(Q)./$(BUILD)/band_plan_test

check-adsb-dsp: $(TESTS)/adsb_dsp_test.c $(TESTS)/check.h $(SRC)/adsb_dsp.c $(SRC)/adsb_dsp.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/adsb_dsp_test \
		$(TESTS)/adsb_dsp_test.c $(SRC)/adsb_dsp.c -lm
	$(Q)./$(BUILD)/adsb_dsp_test

# The LTE cell search: the channel map, the three sequences the standard
# fixes, and a whole frame synthesised here and read back. The frame is what
# makes it worth running -- every mapping the plugin uses is written out a
# second time and independently, so agreement means something.
check-lte-dsp: $(TESTS)/lte_dsp_test.c $(TESTS)/check.h $(SRC)/lte_dsp.c \
		$(SRC)/lte_dsp.h $(SRC)/lte_gold.h testfiles/lte_b20_pci32.bin
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/lte_dsp_test \
		$(TESTS)/lte_dsp_test.c $(SRC)/lte_dsp.c -lm
	$(Q)./$(BUILD)/lte_dsp_test

# And the Decoder side of LTE: 480 soft bits to a Master Information Block.
# Scrambling, rate matching, a tail-biting trellis and a masked parity, each
# pushed on in both directions. No samples, no receiver.
check-lte-mib: $(TESTS)/lte_mib_test.c $(TESTS)/check.h $(SRC)/lte_mib.c \
		$(SRC)/lte_mib.h $(SRC)/lte_gold.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/lte_mib_test \
		$(TESTS)/lte_mib_test.c $(SRC)/lte_mib.c -lm
	$(Q)./$(BUILD)/lte_mib_test

# The LTE band scan's order: every channel of a band named exactly once, and
# the likely carrier centres named first. Links lte_dsp.c for the band table.
check-lte-scan: $(TESTS)/lte_scan_test.c $(TESTS)/check.h $(SRC)/lte_scan.h \
		$(SRC)/lte_dsp.c $(SRC)/lte_dsp.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/lte_scan_test \
		$(TESTS)/lte_scan_test.c $(SRC)/lte_dsp.c -lm
	$(Q)./$(BUILD)/lte_scan_test

# Layout check: the GSM and ADS-B views' rectangles and the window chrome,
# pinned at several window sizes. Needs raylib's headers for the Rectangle type but not
# the library -- both layouts are pure functions of the window size, which is
# what makes them testable without opening a window.
check-layout: $(TESTS)/layout_test.c $(TESTS)/check.h $(SRC)/gsm_layout.h \
		$(SRC)/adsb_layout.h $(SRC)/chrome_layout.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) $(shell pkg-config --cflags raylib) \
		-o $(BUILD)/layout_test $(TESTS)/layout_test.c -lm
	$(Q)./$(BUILD)/layout_test

# Command-line parsing: every flag, every rejection. Pure text in, options
# out, so it links nothing at all.
check-options: $(TESTS)/options_test.c $(TESTS)/check.h $(SRC)/options.c $(SRC)/options.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/options_test \
		$(TESTS)/options_test.c $(SRC)/options.c -lm
	$(Q)./$(BUILD)/options_test

# Whole paths through the built program, over the captures in testfiles/:
# decode, record, and the flags that reach them. Needs the binary and about ten
# seconds; needs no receiver and nobody watching.
check-pipelines: sdrprobe $(TESTS)/pipelines.sh
	@$(TESTS)/pipelines.sh

# When a frequency correction may be trusted (ADR-0004). Pure arithmetic, so
# the rule can be checked clause by clause without a receiver.
check-calibration: $(TESTS)/calibration_gate_test.c $(TESTS)/check.h $(SRC)/calibration_gate.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/calibration_gate_test \
		$(TESTS)/calibration_gate_test.c -lm
	$(Q)./$(BUILD)/calibration_gate_test

# Which control a key press reaches: the frame loop's precedence chain, as a
# function of flags rather than a chain of IsKeyPressed calls.
check-input: $(TESTS)/input_route_test.c $(TESTS)/check.h $(SRC)/input_route.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/input_route_test \
		$(TESTS)/input_route_test.c -lm
	$(Q)./$(BUILD)/input_route_test

# Chart geometry: where the plot sits inside a chart, and which bar the
# pointer is over. Needs raylib's headers for Rectangle but not the library.
check-geometry: $(TESTS)/sdrgui_geometry_test.c $(TESTS)/check.h \
		$(SRC)/sdrgui_geometry.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) $(shell pkg-config --cflags raylib) \
		-o $(BUILD)/sdrgui_geometry_test \
		$(TESTS)/sdrgui_geometry_test.c -lm
	$(Q)./$(BUILD)/sdrgui_geometry_test

# The BCCH: four bursts to a System Information message. The Decoder context's
# side of GSM -- interleaving, the Fire code, the convolutional code, and what
# the message says. No samples, no receiver.
check-gsm-bcch: $(TESTS)/gsm_bcch_test.c $(TESTS)/check.h $(SRC)/gsm_bcch.c \
		$(SRC)/gsm_bcch.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/gsm_bcch_test \
		$(TESTS)/gsm_bcch_test.c $(SRC)/gsm_bcch.c -lm
	$(Q)./$(BUILD)/gsm_bcch_test

# Whether consecutive SCH decodes hang together: the hyperframe wrap, the
# elapsed time a frame number is judged against, and a BSIC that changes.
check-gsm-continuity: $(TESTS)/gsm_continuity_test.c $(TESTS)/check.h \
		$(SRC)/gsm_continuity.h $(SRC)/input_route.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/gsm_continuity_test \
		$(TESTS)/gsm_continuity_test.c -lm
	$(Q)./$(BUILD)/gsm_continuity_test

# What the ADS-B view decides: whether Mode S could be there, which frame the
# analysis charts describe, the message log, and the funnel counters.
check-adsb-analysis: $(TESTS)/adsb_analysis_test.c $(TESTS)/check.h \
		$(SRC)/adsb_analysis.h $(SRC)/gsm_continuity.h $(SRC)/input_route.h $(SRC)/adsb_dsp.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/adsb_analysis_test \
		$(TESTS)/adsb_analysis_test.c -lm
	$(Q)./$(BUILD)/adsb_analysis_test

# The GSM 900 band scan: how the downlink is covered, and which channel the
# operator is handed at the end. That single ARFCN is the scan's whole output.
check-scan: $(TESTS)/scan_plan_test.c $(TESTS)/check.h $(SRC)/scan_plan.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/scan_plan_test \
		$(TESTS)/scan_plan_test.c -lm
	$(Q)./$(BUILD)/scan_plan_test

# The handoff between the acquisition thread and the renderer: the
# overwriteable slot (ADR-0002), the lossless mode scripted playback needs, and
# the file worker driven against a real capture. Links librtlsdr for the device
# type only -- it never opens one.
check-acquisition: $(TESTS)/acquisition_test.c $(TESTS)/check.h \
		$(SRC)/acquisition.c $(SRC)/acquisition.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -pthread -o $(BUILD)/acquisition_test \
		$(TESTS)/acquisition_test.c $(SRC)/acquisition.c \
		$(shell pkg-config --libs librtlsdr) -lm -pthread
	$(Q)./$(BUILD)/acquisition_test

# Which candidates the survey should warn about: the receiver's own reference
# comb, and the DC offset at each step centre. The check is built from a real
# sweep taken with the antenna disconnected.
check-suspect: $(TESTS)/survey_suspect_test.c $(TESTS)/check.h \
		$(SRC)/survey_suspect.h $(SRC)/survey_sweep.h $(SRC)/sdr_dsp.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/survey_suspect_test \
		$(TESTS)/survey_suspect_test.c -lm
	$(Q)./$(BUILD)/survey_suspect_test

# The sweep itself: the step plan, the fold, and what measuring a candidate
# adds up to. None of it is visible when it is wrong -- a gap between steps
# hides whatever transmits in it and the chart looks right -- so the arithmetic
# is the only place it can be caught.
check-survey-sweep: $(TESTS)/survey_sweep_test.c $(TESTS)/check.h \
		$(SRC)/survey_sweep.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/survey_sweep_test \
		$(TESTS)/survey_sweep_test.c -lm
	$(Q)./$(BUILD)/survey_sweep_test

# The band survey's window arithmetic: zoom, pan, and what Sweep would sweep.
# No raylib, no receiver, no window -- which is the point. Every one of these
# decisions previously had to be checked by building an instrumented binary and
# running it against the dongle, and two of them shipped wrong.
check-survey: $(TESTS)/survey_window_test.c $(TESTS)/check.h $(SRC)/survey_window.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/survey_window_test \
		$(TESTS)/survey_window_test.c -lm
	$(Q)./$(BUILD)/survey_window_test

# One command that says whether the tree is sound, for agents and for people.
# ADR-0012: every decision must be reachable by a check that needs no window,
# no receiver and nobody watching -- and reaching them has to be one step, or
# it will not be done.
#
# Each suite prints one line saying what it covers and how much it proved, and
# appends its counts to CHECK_TALLY so the total below is real rather than a
# claim. Sub-makes rather than prerequisites, so the sections stay in order.
CHECK_UNITS=check-sdr-dsp check-gsm-dsp check-adsb-dsp check-lte-dsp \
	check-lte-mib check-lte-scan check-band-plan \
	check-options check-survey check-survey-sweep check-suspect \
	check-calibration \
	check-layout check-acquisition check-scan check-adsb-analysis \
	check-gsm-continuity check-gsm-bcch check-geometry check-input
TALLY=$(BUILD)/check-tally

check: sdrprobe
	@mkdir -p $(BUILD)
	@rm -f $(TALLY)
	@printf '\nsdrprobe checks -- no window, no receiver, nobody watching\n'
	@printf '\nunits\n'
	@CHECK_TALLY=$(TALLY) $(MAKE) --no-print-directory $(CHECK_UNITS)
	@printf '\npipelines -- the built program over testfiles/\n'
	@CHECK_TALLY=$(TALLY) $(MAKE) --no-print-directory check-pipelines
	@awk '{checks += $$1; bad += $$2} END { printf \
		"\n%d checks in %d suites, no failures\n\n", checks, NR}' $(TALLY)

check-dsp: check-sdr-dsp check-gsm-dsp check-adsb-dsp check-lte-dsp \
	check-lte-mib check-band-plan

# White-box diagnostic walk through the GSM SCH chain (not a unit test). It
# compiles gsm_dsp.c in (to reach its statics), so it links only sdr_dsp.c.
FILE ?= testfiles/gsm_arfcn_69.bin
probe-gsm-chain: scripts/gsm_chain_probe.c $(SRC)/gsm_dsp.c $(SRC)/gsm_dsp.h \
		$(SRC)/sdr_dsp.c $(SRC)/sdr_dsp.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/gsm_chain_probe \
		scripts/gsm_chain_probe.c $(SRC)/sdr_dsp.c -lm
	$(Q)./$(BUILD)/gsm_chain_probe $(FILE)

# White-box diagnostic walk through the ADS-B Mode S decode chain.
FILE_ADSB ?= testfiles/adsb_modes1.bin
probe-adsb-chain: scripts/adsb_chain_probe.c $(SRC)/adsb_dsp.c $(SRC)/adsb_dsp.h \
		$(SRC)/sdr_dsp.c $(SRC)/sdr_dsp.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/adsb_chain_probe \
		scripts/adsb_chain_probe.c $(SRC)/sdr_dsp.c -lm
	$(Q)./$(BUILD)/adsb_chain_probe $(FILE_ADSB)

# White-box diagnostic walk through the LTE cell search and broadcast channel.
FILE_LTE ?= testfiles/lte_b20_pci32.bin
probe-lte-chain: scripts/lte_chain_probe.c $(SRC)/lte_dsp.c $(SRC)/lte_dsp.h \
		$(SRC)/lte_mib.c $(SRC)/lte_mib.h $(SRC)/lte_gold.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/lte_chain_probe \
		scripts/lte_chain_probe.c $(SRC)/lte_mib.c -lm
	$(Q)./$(BUILD)/lte_chain_probe $(FILE_LTE)

# What the DSP costs per sample block, against the 65.5 ms one block covers.
# BENCH_ARCH=-march=native answers the SIMD question by measuring it: the
# default build has no -march, so the compiler targets the baseline ISA.
BENCH_ARCH ?=
bench-dsp: scripts/dsp_bench.c $(DSP_SRC) $(DSP_HDR)
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) $(BENCH_ARCH) -I$(SRC) -o $(BUILD)/dsp_bench \
		scripts/dsp_bench.c $(DSP_SRC) -lm
	$(Q)./$(BUILD)/dsp_bench

# Point git at the version-controlled hooks in scripts/hooks/, so `git push`
# runs `make check` first. One setting, and the hook itself stays in the repo
# where it can be read and changed like anything else.
hooks:
	$(Q)git config core.hooksPath scripts/hooks
	@printf '  %-34s %s\n' "pre-push" \
		"installed; git push --no-verify skips it"

clean:
	rm -rf sdrprobe $(BUILD)

.PHONY: all check hooks check-lte-dsp check-lte-mib check-lte-scan check-gsm-bcch check-suspect check-input check-geometry check-gsm-continuity check-adsb-analysis check-scan check-acquisition check-survey-sweep check-options check-calibration check-pipelines check-sdr-dsp check-gsm-dsp check-adsb-dsp check-band-plan check-dsp check-layout check-survey probe-gsm-chain probe-adsb-chain probe-lte-chain bench-dsp clean
