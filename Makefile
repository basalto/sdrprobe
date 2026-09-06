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

DSP_SRC=$(SRC)/signal_probe.c $(SRC)/sdr_dsp.c $(SRC)/gsm_dsp.c $(SRC)/gsm_bcch.c $(SRC)/adsb_dsp.c \
	$(SRC)/lte_dsp.c $(SRC)/lte_mib.c $(SRC)/fm_dsp.c $(SRC)/rds.c \
	$(SRC)/tetra_dsp.c $(SRC)/tetra_sync.c
APP_SRC=$(SRC)/acquisition.c $(SRC)/options.c $(SRC)/chart_window.c $(SRC)/config.c $(SRC)/site_history.c $(SRC)/view_scope.c $(SRC)/view_gsm.c \
	$(SRC)/view_adsb.c $(SRC)/view_lte.c $(SRC)/view_fm.c $(SRC)/view_tetra.c \
	$(SRC)/view_survey.c \
	$(SRC)/band_plan.c \
	$(SRC)/overlay_calibration.c $(SRC)/overlay_scan.c \
	$(SRC)/overlay_settings.c $(SRC)/overlay_help.c \
	$(SRC)/survey_report.c $(SRC)/survey_store.c $(SRC)/debug_log.c
APP_HDR=$(SRC)/options.h $(SRC)/config.h $(SRC)/calibration_layout.h $(SRC)/survey_carrier.h $(SRC)/survey_confirm.h $(SRC)/site_history.h $(SRC)/survey_store.h $(SRC)/gsm_layout.h $(SRC)/adsb_layout.h $(SRC)/tetra_layout.h \
	$(SRC)/lte_layout.h $(SRC)/fm_layout.h \
	$(SRC)/survey_layout.h $(SRC)/freq_window.h $(SRC)/survey_sweep.h \
	$(SRC)/survey_suspect.h $(SRC)/chrome_layout.h \
	$(SRC)/band_plan.h $(SRC)/calibration_gate.h $(SRC)/scan_plan.h \
	$(SRC)/adsb_analysis.h $(SRC)/gsm_continuity.h $(SRC)/input_route.h $(SRC)/debug_log.h $(SRC)/app.h $(SRC)/view.h \
	$(SRC)/version.h \
	$(SRC)/panel_rows.h $(SRC)/lte_stats.h $(SRC)/lte_confirm.h \
	$(SRC)/lte_findings.h \
	$(SRC)/chart_window.h $(SRC)/help_layout.h $(SRC)/scan_layout.h \
	$(SRC)/scope_layout.h $(SRC)/settings_layout.h
DSP_HDR=$(SRC)/signal_probe.h $(SRC)/sdr_dsp.h $(SRC)/gsm_dsp.h $(SRC)/gsm_bcch.h $(SRC)/adsb_dsp.h \
	$(SRC)/lte_dsp.h $(SRC)/lte_mib.h $(SRC)/lte_gold.h $(SRC)/lte_scan.h \
	$(SRC)/fm_dsp.h $(SRC)/rds.h $(SRC)/tetra_dsp.h $(SRC)/tetra_sync.h
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
# FM broadcast: the discriminator and the 19 kHz pilot every other rate in the
# multiplex is derived from. Probe side, links libm only.
# RDS: the block code, the search that finds group boundaries without a
# preamble, and what a station says about itself. Decoder side; links fm_dsp
# only to reach the real capture.
check-rds: $(TESTS)/rds_test.c $(TESTS)/check.h $(SRC)/rds.c $(SRC)/rds.h \
		$(SRC)/fm_dsp.c $(SRC)/fm_dsp.h testfiles/fm_rds_tsf.bin
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/rds_test \
		$(TESTS)/rds_test.c $(SRC)/rds.c $(SRC)/fm_dsp.c -lm
	$(Q)./$(BUILD)/rds_test

# Band II's scan: the 100 kHz raster, that the coarse sweep covers the band
# with no gap, and what the two passes cost.
check-fm-scan: $(TESTS)/fm_scan_test.c $(TESTS)/check.h $(SRC)/fm_scan.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/fm_scan_test \
		$(TESTS)/fm_scan_test.c -lm
	$(Q)./$(BUILD)/fm_scan_test

check-fm-dsp: $(TESTS)/fm_dsp_test.c $(TESTS)/check.h $(SRC)/fm_dsp.c \
		$(SRC)/fm_dsp.h testfiles/fm_rds_tsf.bin
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/fm_dsp_test \
		$(TESTS)/fm_dsp_test.c $(SRC)/fm_dsp.c -lm
	$(Q)./$(BUILD)/fm_dsp_test

# Which allocations the survey offers to sweep, what range each means, and
# the dwell that comes with it. Reads the band plan, links no receiver.
check-survey-bands: $(TESTS)/survey_bands_test.c $(TESTS)/check.h \
		$(SRC)/survey_bands.h $(SRC)/band_plan.c $(SRC)/band_plan.h \
		$(SRC)/survey_sweep.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/survey_bands_test \
		$(TESTS)/survey_bands_test.c $(SRC)/band_plan.c -lm
	$(Q)./$(BUILD)/survey_bands_test

check-band-plan: $(TESTS)/band_plan_test.c $(TESTS)/check.h $(SRC)/band_plan.c $(SRC)/band_plan.h $(SRC)/band_plan_view.h
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
		$(SRC)/lte_dsp.h $(SRC)/lte_gold.h $(SRC)/lte_mib.c $(SRC)/lte_mib.h \
		testfiles/lte_b20_pci28.bin
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/lte_dsp_test \
		$(TESTS)/lte_dsp_test.c $(SRC)/lte_dsp.c $(SRC)/lte_mib.c -lm
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
		$(SRC)/adsb_layout.h $(SRC)/chrome_layout.h $(SRC)/lte_layout.h \
		$(SRC)/survey_layout.h $(SRC)/calibration_layout.h \
		$(SRC)/fm_layout.h $(SRC)/row_list.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) $(shell pkg-config --cflags raylib) \
		-o $(BUILD)/layout_test $(TESTS)/layout_test.c -lm
	$(Q)./$(BUILD)/layout_test

# Command-line parsing: every flag, every rejection. Pure text in, options
# out, so it links nothing at all.
# The antenna and site that persist between runs. Text in, text out.
check-config: $(TESTS)/config_test.c $(TESTS)/check.h $(SRC)/config.c \
		$(SRC)/config.h $(SRC)/sdr_dsp.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/config_test \
		$(TESTS)/config_test.c $(SRC)/config.c -lm
	$(Q)./$(BUILD)/config_test

# Naming a saved sweep, and escaping what goes in it. The write itself needs a
# receiver and a directory; these two do not, and they are where it goes wrong.
check-survey-store: $(TESTS)/survey_store_test.c $(TESTS)/check.h \
		$(SRC)/survey_store.c $(SRC)/survey_store.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) $(shell pkg-config --cflags raylib) \
		$(shell pkg-config --cflags librtlsdr) \
		-o $(BUILD)/survey_store_test $(TESTS)/survey_store_test.c \
		$(SRC)/survey_store.c $(SRC)/sdr_dsp.c $(SRC)/band_plan.c -lm
	$(Q)./$(BUILD)/survey_store_test

# What a site has heard before, and how a sweep is judged against it.
check-site-history: $(TESTS)/site_history_test.c $(TESTS)/check.h \
		$(SRC)/site_history.c $(SRC)/site_history.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/site_history_test \
		$(TESTS)/site_history_test.c $(SRC)/site_history.c -lm
	$(Q)./$(BUILD)/site_history_test

# Asking again about what a sweep called new or missing.
check-survey-confirm: $(TESTS)/survey_confirm_test.c $(TESTS)/check.h \
		$(SRC)/survey_confirm.h $(SRC)/survey_sweep.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/survey_confirm_test \
		$(TESTS)/survey_confirm_test.c -lm
	$(Q)./$(BUILD)/survey_confirm_test

# Local maxima to signals: one carrier has several, and reporting each is how
# one station becomes five things to remember.
check-survey-carrier: $(TESTS)/survey_carrier_test.c $(TESTS)/check.h \
		$(SRC)/survey_carrier.h $(SRC)/sdr_dsp.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/survey_carrier_test \
		$(TESTS)/survey_carrier_test.c -lm
	$(Q)./$(BUILD)/survey_carrier_test

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
# The candidate list: how many rows fit, which one the pointer is over, and
# how far the list is scrolled. Needs raylib's headers for Rectangle, not the
# library.
# Where a line of text breaks when a panel is narrower than it. Pure
# arithmetic; how wide a line may be is the caller's font question.
check-text-wrap: $(TESTS)/text_wrap_test.c $(TESTS)/check.h $(SRC)/text_wrap.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/text_wrap_test \
		$(TESTS)/text_wrap_test.c -lm
	$(Q)./$(BUILD)/text_wrap_test

check-row-list: $(TESTS)/row_list_test.c $(TESTS)/check.h \
		$(SRC)/row_list.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) $(shell pkg-config --cflags raylib) \
		-o $(BUILD)/row_list_test $(TESTS)/row_list_test.c -lm
	$(Q)./$(BUILD)/row_list_test

# The debug log's decisions: what a key is called, what a screen is called,
# and whether a screen changed. It is believed when nothing else can be, so a
# mislabelled line is worse than no line.
check-debug-log: $(TESTS)/debug_log_test.c $(TESTS)/check.h \
		$(SRC)/debug_log.c $(SRC)/debug_log.h $(SRC)/input_route.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/debug_log_test \
		$(TESTS)/debug_log_test.c $(SRC)/debug_log.c -lm
	$(Q)./$(BUILD)/debug_log_test

check-input: $(TESTS)/input_route_test.c $(TESTS)/check.h $(SRC)/input_route.h \
		$(SRC)/calibration_nav.h
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
check-signal-probe: $(TESTS)/signal_probe_test.c $(TESTS)/check.h \
		$(SRC)/signal_probe.c $(SRC)/signal_probe.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/signal_probe_test \
		$(TESTS)/signal_probe_test.c $(SRC)/signal_probe.c -lm
	$(Q)./$(BUILD)/signal_probe_test

check-lte-findings: $(TESTS)/lte_findings_test.c $(TESTS)/check.h \
		$(SRC)/lte_findings.h $(SRC)/lte_stats.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/lte_findings_test \
		$(TESTS)/lte_findings_test.c -lm
	$(Q)./$(BUILD)/lte_findings_test

check-lte-stats: $(TESTS)/lte_stats_test.c $(TESTS)/check.h \
		$(SRC)/lte_stats.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/lte_stats_test \
		$(TESTS)/lte_stats_test.c -lm
	$(Q)./$(BUILD)/lte_stats_test

check-lte-confirm: $(TESTS)/lte_confirm_test.c $(TESTS)/check.h \
		$(SRC)/lte_confirm.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/lte_confirm_test \
		$(TESTS)/lte_confirm_test.c -lm
	$(Q)./$(BUILD)/lte_confirm_test

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
check-freq-window: $(TESTS)/freq_window_test.c $(TESTS)/check.h \
		$(SRC)/freq_window.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/freq_window_test \
		$(TESTS)/freq_window_test.c -lm
	$(Q)./$(BUILD)/freq_window_test

# One command that says whether the tree is sound, for agents and for people.
# ADR-0012: every decision must be reachable by a check that needs no window,
# no receiver and nobody watching -- and reaching them has to be one step, or
# it will not be done.
#
# Each suite prints one line saying what it covers and how much it proved, and
# appends its counts to CHECK_TALLY so the total below is real rather than a
# claim. Sub-makes rather than prerequisites, so the sections stay in order.
CHECK_UNITS=check-config check-survey-carrier check-survey-confirm check-site-history check-survey-store check-sdr-dsp check-gsm-dsp check-adsb-dsp check-lte-dsp \
	check-lte-mib check-lte-scan check-band-plan \
	check-options check-freq-window check-survey-sweep check-suspect \
	check-calibration \
	check-layout check-acquisition check-scan check-adsb-analysis \
	check-fm-dsp check-fm-scan check-rds check-debug-log \
	check-row-list check-survey-bands check-text-wrap \
	check-gsm-continuity check-gsm-bcch check-geometry check-input \
	check-lte-turbo check-lte-transport check-lte-confirm check-lte-stats check-lte-findings check-tetra-dsp check-tetra-sync
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

# The checks that cover what changed, and a count of what was skipped. The
# full suite is the gate on push; this is what to run while working, because
# a suite takes under a second and all of them take the better part of a
# minute. FILES overrides what git thinks changed.
check-touched:
	$(Q)python3 scripts/check_touched.py $(FILES)

# White-box diagnostic walk through the GSM SCH chain (not a unit test). It
# compiles gsm_dsp.c in (to reach its statics), so it links only sdr_dsp.c.
FILE ?= testfiles/gsm_arfcn_69.bin
FILE_FM_FILTER ?= testfiles/fm_rds_tsf.bin
RATE_FM_FILTER ?= 2048000

# Rectangular against shaped biphase filter, over the same samples at a sweep
# of added noise. Answers whether the theoretical decibel is worth having.
probe-fm-filter: scripts/fm_filter_probe.c $(SRC)/fm_dsp.c $(SRC)/fm_dsp.h \
		$(SRC)/rds.c $(SRC)/rds.h $(SRC)/sdr_dsp.c $(SRC)/sdr_dsp.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/fm_filter_probe \
		scripts/fm_filter_probe.c $(SRC)/fm_dsp.c $(SRC)/rds.c \
		$(SRC)/sdr_dsp.c -lm
	$(Q)./$(BUILD)/fm_filter_probe $(FILE_FM_FILTER) $(RATE_FM_FILTER)

check-lte-turbo: $(TESTS)/lte_turbo_test.c $(TESTS)/check.h \
		$(SRC)/lte_turbo.c $(SRC)/lte_turbo.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/lte_turbo_test \
		$(TESTS)/lte_turbo_test.c $(SRC)/lte_turbo.c -lm
	$(Q)./$(BUILD)/lte_turbo_test

check-tetra-dsp: $(TESTS)/tetra_dsp_test.c $(TESTS)/check.h \
		$(SRC)/tetra_dsp.c $(SRC)/tetra_dsp.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/tetra_dsp_test \
		$(TESTS)/tetra_dsp_test.c $(SRC)/tetra_dsp.c -lm
	$(Q)./$(BUILD)/tetra_dsp_test

check-tetra-sync: $(TESTS)/tetra_sync_test.c $(TESTS)/check.h \
		$(SRC)/tetra_sync.c $(SRC)/tetra_sync.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/tetra_sync_test \
		$(TESTS)/tetra_sync_test.c $(SRC)/tetra_sync.c -lm
	$(Q)./$(BUILD)/tetra_sync_test

check-lte-transport: $(TESTS)/lte_transport_test.c $(TESTS)/check.h \
		$(SRC)/lte_transport.c $(SRC)/lte_transport.h \
		$(SRC)/lte_turbo.c $(SRC)/lte_turbo.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/lte_transport_test \
		$(TESTS)/lte_transport_test.c $(SRC)/lte_transport.c \
		$(SRC)/lte_turbo.c -lm
	$(Q)./$(BUILD)/lte_transport_test

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
FILE_LTE ?= testfiles/lte_b20_pci28.bin
FILE_NBIOT ?= captures/nbiot.bin
probe-nbiot: scripts/nbiot_gate.c $(SRC)/lte_dsp.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/nbiot_gate \
		scripts/nbiot_gate.c -lm
	$(Q)./$(BUILD)/nbiot_gate $(FILE_NBIOT)

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
# What technology is a capture, and on what grid? Two model-free measurements
# and a conclusion, for a signal nothing here can demodulate.
FILE_PERIODICITY?=testfiles/lte_b20_pci28.bin
RATE_PERIODICITY?=1920000
# Where a survey's noise floor reaches, so the candidate threshold can be
# chosen rather than inherited (ADR-0013). Pure noise through the real
# transform and the real fold; a survey of nothing should report nothing.
DRAWS ?= 6
probe-survey-threshold: scripts/survey_threshold_probe.c $(SRC)/sdr_dsp.c \
		$(SRC)/sdr_dsp.h $(SRC)/survey_sweep.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/survey_threshold_probe \
		scripts/survey_threshold_probe.c $(SRC)/sdr_dsp.c -lm
	$(Q)./$(BUILD)/survey_threshold_probe $(DRAWS)

probe-periodicity: scripts/signal_periodicity.c
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -o $(BUILD)/signal_periodicity \
		scripts/signal_periodicity.c -lm
	$(Q)./$(BUILD)/signal_periodicity $(FILE_PERIODICITY) $(RATE_PERIODICITY)

# Every screen the program has, rendered to look at. A change that draws is not
# finished until somebody has seen it: check-layout compares rectangles and
# cannot see two panels drawing into the same one, a band button offering a
# band the receiver cannot reach, or a field that says N/A. All three shipped.
SCREEN_DIR?=$(BUILD)/screens
SCREEN_W?=1500
SCREEN_H?=950
# Every screen, or the ones named: make screens NAMES="calibration-2g gsm".
# A change touches a screen or two; rendering the other ten costs a minute to
# learn nothing.
screens: sdrprobe
	@mkdir -p $(SCREEN_DIR)
	$(Q)NAMES="$(NAMES)" sh scripts/screens.sh $(SCREEN_DIR) $(SCREEN_W) $(SCREEN_H)

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

.PHONY: all check hooks check-signal-probe check-lte-findings check-lte-stats check-lte-confirm check-config check-survey-carrier check-survey-confirm check-site-history check-survey-store check-lte-dsp check-lte-mib check-lte-scan check-gsm-bcch check-suspect check-input check-geometry check-gsm-continuity check-adsb-analysis check-scan check-acquisition check-survey-sweep check-options check-calibration check-pipelines check-sdr-dsp check-gsm-dsp check-adsb-dsp check-band-plan check-dsp check-layout check-freq-window probe-gsm-chain probe-adsb-chain probe-lte-chain probe-nbiot probe-periodicity probe-survey-threshold bench-dsp screens clean
