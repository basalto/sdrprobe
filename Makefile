# -O3 rather than -O2, and the reason is one stage.
#
# Measured three times each, alternating so a drifting machine cannot fake it:
# the GSM SCH decode goes from 20.4/23.1/21.7 ms a block to 14.5/15.8/15.7,
# about 30% off the largest single stage in the program -- 33% of a 65.5 ms
# block down to 23%. Everything else moves within noise: the LTE cell search
# 9.75 to 10.03 ms, every cell on a carrier 20.07 to 20.56, the spectrum's 64
# transforms 4.44 to 4.55.
#
# Nothing was over budget at -O2, so this buys no capability today. What it
# buys is headroom, and headroom is not free here: ADR-0002 has a slow
# renderer *drop* blocks rather than lag, so a machine slower than this one
# loses decodes rather than falling behind, and the biggest stage is where
# that starts.
#
# No -ffast-math, and the whole suite passes at -O3 -- 16750 checks including
# every real-capture invariant: BSIC 59, LTE cell 32, TETRA colour codes 17
# and 32, RDS 0x8343, six ADS-B frames with a position. `make bench-dsp` is
# how to re-measure, and CFLAGS is overridable for anyone who would rather not.
CFLAGS?=-O3 -g -Wall -W $(shell pkg-config --cflags librtlsdr)
CFLAGS+=$(UHD_CFLAGS)
LDLIBS+=$(shell pkg-config --libs librtlsdr) $(UHD_LIBS) -lm
CC?=gcc

# UHD is optional, and the build works either way.
#
# A B210-class device is on order and its driver is a large C++ library. The
# repository has no CI, so `make check` on a machine that has never installed
# UHD is the gate for everyone; requiring it would make the gate unrunnable
# there. So: detect it, and compile the backend in only when it is found.
# `device_backend_uhd()` returns NULL in a build without it, which is why
# callers ask rather than testing a macro -- a binary built without UHD
# refuses a UHD device with a sentence instead of failing to link.
#
# **It defaults to 0 today, deliberately.** The adapter itself is not written
# -- the board has not arrived and C++ against an API nobody here can compile
# is exactly the plausible-and-wrong artifact this spec keeps producing -- so
# auto-detecting would break the build for anyone who happens to have UHD
# installed. Flip the default to the pkg-config probe on the line below when
# `backend_uhd.c` exists and compiles:
#
#     HAVE_UHD?=$(shell pkg-config --exists uhd && echo 1 || echo 0)
#
# `make HAVE_UHD=1` opts in meanwhile, which is how the adapter will be
# developed once there is something to develop against.
HAVE_UHD?=0
ifeq ($(HAVE_UHD),1)
UHD_CFLAGS=$(shell pkg-config --cflags uhd) -DHAVE_UHD=1
UHD_LIBS=$(shell pkg-config --libs uhd)
UHD_SRC=
else
UHD_CFLAGS=
UHD_LIBS=
UHD_SRC=
endif

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

DSP_SRC=$(SRC)/gsm_session.c $(SRC)/tetra_session.c $(SRC)/lte_session.c $(SRC)/adsb_session.c $(SRC)/fm_session.c $(SRC)/signal_probe.c $(SRC)/sdr_dsp.c $(SRC)/gsm_dsp.c $(SRC)/gsm_bcch.c $(SRC)/adsb_dsp.c \
	$(SRC)/lte_dsp.c $(SRC)/lte_mib.c $(SRC)/fm_dsp.c $(SRC)/rds.c \
	$(SRC)/tetra_dsp.c $(SRC)/tetra_sync.c
APP_SRC=$(SRC)/installation.c $(SRC)/backend_rtlsdr.c $(SRC)/backend_capture.c \
	$(SRC)/backend_uhd.c \
	$(SRC)/acquisition.c $(SRC)/options.c $(SRC)/chart_window.c $(SRC)/config.c $(SRC)/site_history.c $(SRC)/survey_record.c $(SRC)/lte_chain_analysis.c $(SRC)/signal_frame.c $(SRC)/receiver_runtime.c $(SRC)/view_scope.c $(SRC)/view_gsm.c \
	$(SRC)/view_adsb.c $(SRC)/view_lte.c $(SRC)/view_fm.c $(SRC)/view_tetra.c \
	$(SRC)/view_survey.c \
	$(SRC)/band_plan.c \
	$(SRC)/overlay_calibration.c $(SRC)/overlay_scan.c \
	$(SRC)/overlay_settings.c $(SRC)/overlay_help.c \
	$(SRC)/survey_report.c $(SRC)/survey_store.c $(SRC)/survey_session.c \
	$(SRC)/debug_log.c
APP_HDR=$(SRC)/options.h $(SRC)/config.h $(SRC)/reading_origin.h $(SRC)/clock_chain.h $(SRC)/lte_chain_analysis.h $(SRC)/calibration_layout.h $(SRC)/survey_carrier.h $(SRC)/survey_confirm.h $(SRC)/site_history.h $(SRC)/survey_store.h $(SRC)/survey_record.h $(SRC)/signal_frame.h $(SRC)/receiver_runtime.h $(SRC)/gsm_layout.h $(SRC)/adsb_layout.h $(SRC)/tetra_layout.h \
	$(SRC)/lte_layout.h $(SRC)/fm_layout.h \
	$(SRC)/survey_layout.h $(SRC)/freq_window.h $(SRC)/survey_sweep.h \
	$(SRC)/survey_session.h \
	$(SRC)/survey_suspect.h $(SRC)/reading_origin.h $(SRC)/clock_chain.h $(SRC)/chrome_layout.h \
	$(SRC)/band_plan.h $(SRC)/calibration_gate.h $(SRC)/scan_plan.h \
	$(SRC)/adsb_analysis.h $(SRC)/gsm_continuity.h $(SRC)/input_route.h $(SRC)/debug_log.h \
	$(SRC)/receiver_lease.h $(SRC)/device_profile.h \
	$(SRC)/installation.h \
	$(SRC)/capture_sidecar.h $(SRC)/device_backend.h \
	$(SRC)/app.h $(SRC)/view.h \
	$(SRC)/version.h \
	$(SRC)/panel_rows.h $(SRC)/lte_stats.h $(SRC)/lte_confirm.h \
	$(SRC)/lte_findings.h \
	$(SRC)/chart_window.h $(SRC)/help_layout.h $(SRC)/scan_layout.h \
	$(SRC)/scope_layout.h $(SRC)/settings_layout.h
DSP_HDR=$(SRC)/gsm_session.h $(SRC)/tetra_session.h $(SRC)/lte_session.h $(SRC)/adsb_session.h $(SRC)/fm_session.h $(SRC)/device_profile.h $(SRC)/signal_probe.h $(SRC)/signal_findings.h $(SRC)/sdr_dsp.h $(SRC)/gsm_dsp.h $(SRC)/gsm_bcch.h $(SRC)/adsb_dsp.h \
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
check-receiver-runtime: $(TESTS)/receiver_runtime_test.c $(TESTS)/check.h \
		$(TESTS)/fake_backend.c $(TESTS)/fake_backend.h \
		$(SRC)/receiver_runtime.c $(SRC)/receiver_runtime.h \
		$(SRC)/device_backend.h $(SRC)/device_profile.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -I$(TESTS) \
		-o $(BUILD)/receiver_runtime_test \
		$(TESTS)/receiver_runtime_test.c $(TESTS)/fake_backend.c \
		$(SRC)/receiver_runtime.c -lm
	$(Q)./$(BUILD)/receiver_runtime_test

check-signal-frame: $(TESTS)/signal_frame_test.c $(TESTS)/check.h \
		$(SRC)/signal_frame.c $(SRC)/signal_frame.h \
		$(SRC)/sdr_dsp.c $(SRC)/sdr_dsp.h $(SRC)/device_profile.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/signal_frame_test \
		$(TESTS)/signal_frame_test.c $(SRC)/signal_frame.c \
		$(SRC)/sdr_dsp.c -lm
	$(Q)./$(BUILD)/signal_frame_test

check-sdr-dsp: $(TESTS)/sdr_dsp_test.c $(TESTS)/check.h $(SRC)/sdr_dsp.c $(SRC)/sdr_dsp.h \
		$(SRC)/device_profile.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/sdr_dsp_test \
		$(TESTS)/sdr_dsp_test.c $(SRC)/sdr_dsp.c -lm
	$(Q)./$(BUILD)/sdr_dsp_test

check-gsm-dsp: $(TESTS)/gsm_dsp_test.c $(TESTS)/check.h $(SRC)/gsm_dsp.c $(SRC)/gsm_dsp.h \
		$(SRC)/sdr_dsp.c $(SRC)/sdr_dsp.h \
		$(SRC)/device_profile.h
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
		$(SRC)/fm_dsp.c $(SRC)/fm_dsp.h testfiles/fm_rds_tsf.bin \
		$(SRC)/device_profile.h
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
		$(SRC)/fm_dsp.h testfiles/fm_rds_tsf.bin \
		$(SRC)/device_profile.h
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
		$(TESTS)/band_plan_test.c $(SRC)/band_plan.c -lm
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
		testfiles/lte_b20_pci28.bin \
		$(SRC)/device_profile.h
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
		$(SRC)/lte_dsp.c $(SRC)/lte_dsp.h \
		$(SRC)/device_profile.h
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
# The antenna and site that persist between runs. Mostly text in, text out --
# and one test that writes files, because ADR-0022's amended promise is that a
# legacy site-only history is never opened, which nothing pure can assert. It
# runs in a temporary directory of its own and never touches surveys/.
check-installation: $(TESTS)/installation_test.c $(TESTS)/check.h \
		$(SRC)/installation.h $(SRC)/installation.c $(SRC)/config.c \
		$(SRC)/site_history.h $(SRC)/site_history.c
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/installation_test \
		$(TESTS)/installation_test.c $(SRC)/installation.c \
		$(SRC)/config.c $(SRC)/site_history.c -lm
	$(Q)./$(BUILD)/installation_test

check-config: $(TESTS)/config_test.c $(TESTS)/check.h $(SRC)/config.c \
		$(SRC)/config.h $(SRC)/sdr_dsp.h \
		$(SRC)/device_profile.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/config_test \
		$(TESTS)/config_test.c $(SRC)/config.c -lm
	$(Q)./$(BUILD)/config_test

# Naming a saved sweep, and escaping what goes in it. The write itself needs a
# receiver and a directory; these two do not, and they are where it goes wrong.
check-survey-record: $(TESTS)/survey_record_test.c $(TESTS)/check.h \
		$(SRC)/survey_record.c $(SRC)/survey_record.h \
		$(SRC)/survey_carrier.h $(SRC)/survey_confirm.h \
		$(SRC)/survey_suspect.h $(SRC)/reading_origin.h $(SRC)/clock_chain.h $(SRC)/survey_sweep.h \
		$(SRC)/band_plan.c $(SRC)/band_plan.h \
		$(SRC)/sdr_dsp.c $(SRC)/sdr_dsp.h $(SRC)/installation.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/survey_record_test \
		$(TESTS)/survey_record_test.c $(SRC)/survey_record.c \
		$(SRC)/band_plan.c $(SRC)/sdr_dsp.c -lm
	$(Q)./$(BUILD)/survey_record_test

check-survey-store: $(TESTS)/survey_store_test.c $(TESTS)/check.h \
		$(SRC)/survey_store.c $(SRC)/survey_store.h \
		$(SRC)/survey_record.c $(SRC)/survey_record.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) \
		-o $(BUILD)/survey_store_test $(TESTS)/survey_store_test.c \
		$(SRC)/survey_store.c $(SRC)/survey_record.c $(SRC)/sdr_dsp.c \
		$(SRC)/band_plan.c -lm
	$(Q)./$(BUILD)/survey_store_test

# One LTE chain walk, over both committed captures. No window, no receiver.
check-lte-chain-analysis: $(TESTS)/lte_chain_analysis_test.c $(TESTS)/check.h \
		$(SRC)/lte_chain_analysis.c $(SRC)/lte_chain_analysis.h \
		$(SRC)/lte_confirm.h $(SRC)/lte_stats.h $(SRC)/lte_session.c \
		$(SRC)/lte_session.h $(SRC)/lte_dsp.c $(SRC)/lte_dsp.h \
		$(SRC)/lte_mib.c $(SRC)/lte_mib.h $(SRC)/sdr_dsp.c \
		$(SRC)/device_profile.h testfiles/lte_b20_pci28.bin \
		testfiles/lte_b8_pci330_4port.bin
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/lte_chain_analysis_test \
		$(TESTS)/lte_chain_analysis_test.c $(SRC)/lte_chain_analysis.c \
		$(SRC)/lte_session.c $(SRC)/lte_dsp.c $(SRC)/lte_mib.c \
		$(SRC)/sdr_dsp.c -lm
	$(Q)./$(BUILD)/lte_chain_analysis_test

# A clock family in octaves rather than harmonics: f, 2f, 4f and never 3f.
check-clock-chain: $(TESTS)/clock_chain_test.c $(TESTS)/check.h \
		$(SRC)/clock_chain.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/clock_chain_test \
		$(TESTS)/clock_chain_test.c -lm
	$(Q)./$(BUILD)/clock_chain_test

# Whose oscillator a reading belongs to: three numbers, no receiver.
check-reading-origin: $(TESTS)/reading_origin_test.c $(TESTS)/check.h \
		$(SRC)/reading_origin.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/reading_origin_test \
		$(TESTS)/reading_origin_test.c -lm
	$(Q)./$(BUILD)/reading_origin_test

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
		$(SRC)/survey_carrier.h $(SRC)/sdr_dsp.h \
		$(SRC)/device_profile.h
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
check-pipelines: sdrprobe $(TESTS)/pipelines.sh $(FORMAT16)
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
		$(SRC)/sdrgui_geometry.h $(SRC)/sdrgui.h $(SRC)/survey_suspect.h $(SRC)/reading_origin.h $(SRC)/clock_chain.h
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
check-fm-session: $(TESTS)/fm_session_test.c $(TESTS)/check.h \
		$(SRC)/fm_session.c $(SRC)/fm_session.h $(SRC)/fm_dsp.c \
		$(SRC)/rds.c $(SRC)/fm_scan.h $(SRC)/sdr_dsp.c \
		$(SRC)/device_profile.h testfiles/fm_rds_tsf.bin
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/fm_session_test \
		$(TESTS)/fm_session_test.c $(SRC)/fm_session.c $(SRC)/fm_dsp.c \
		$(SRC)/rds.c $(SRC)/sdr_dsp.c -lm
	$(Q)./$(BUILD)/fm_session_test

check-adsb-session: $(TESTS)/adsb_session_test.c $(TESTS)/check.h \
		$(SRC)/adsb_session.c $(SRC)/adsb_session.h $(SRC)/adsb_dsp.c \
		$(SRC)/adsb_analysis.h $(SRC)/sdr_dsp.c $(SRC)/device_profile.h \
		testfiles/adsb_cpr_pair.bin
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/adsb_session_test \
		$(TESTS)/adsb_session_test.c $(SRC)/adsb_session.c \
		$(SRC)/adsb_dsp.c $(SRC)/sdr_dsp.c -lm
	$(Q)./$(BUILD)/adsb_session_test

check-lte-session: $(TESTS)/lte_session_test.c $(TESTS)/check.h \
		$(SRC)/lte_session.c $(SRC)/lte_session.h $(SRC)/lte_dsp.c \
		$(SRC)/lte_mib.c $(SRC)/lte_stats.h $(SRC)/sdr_dsp.c \
		$(SRC)/device_profile.h testfiles/lte_b20_pci28.bin
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/lte_session_test \
		$(TESTS)/lte_session_test.c $(SRC)/lte_session.c $(SRC)/lte_dsp.c \
		$(SRC)/lte_mib.c $(SRC)/sdr_dsp.c -lm
	$(Q)./$(BUILD)/lte_session_test

check-tetra-session: $(TESTS)/tetra_session_test.c $(TESTS)/check.h \
		$(SRC)/tetra_session.c $(SRC)/tetra_session.h $(SRC)/tetra_dsp.c \
		$(SRC)/tetra_sync.c $(SRC)/sdr_dsp.c $(SRC)/signal_probe.c \
		$(SRC)/device_profile.h \
		testfiles/tetra_cc17.bin testfiles/tetra_cc32.bin
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/tetra_session_test \
		$(TESTS)/tetra_session_test.c $(SRC)/tetra_session.c \
		$(SRC)/tetra_dsp.c $(SRC)/tetra_sync.c $(SRC)/sdr_dsp.c \
		$(SRC)/signal_probe.c -lm
	$(Q)./$(BUILD)/tetra_session_test

check-gsm-session: $(TESTS)/gsm_session_test.c $(TESTS)/check.h \
		$(SRC)/gsm_session.c $(SRC)/gsm_session.h $(SRC)/gsm_dsp.c \
		$(SRC)/gsm_bcch.c $(SRC)/sdr_dsp.c $(SRC)/device_profile.h \
		testfiles/gsm_arfcn_69.bin testfiles/gsm_arfcn_113.bin
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/gsm_session_test \
		$(TESTS)/gsm_session_test.c $(SRC)/gsm_session.c \
		$(SRC)/gsm_dsp.c $(SRC)/gsm_bcch.c $(SRC)/sdr_dsp.c -lm
	$(Q)./$(BUILD)/gsm_session_test

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
		$(SRC)/acquisition.c $(SRC)/acquisition.h \
		$(SRC)/device_profile.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -pthread -o $(BUILD)/acquisition_test \
		$(TESTS)/acquisition_test.c $(SRC)/acquisition.c \
		$(shell pkg-config --libs librtlsdr) -lm -pthread
	$(Q)./$(BUILD)/acquisition_test

# Which candidates the survey should warn about: the receiver's own reference
# comb, and the DC offset at each step centre. The check is built from a real
# sweep taken with the antenna disconnected.
check-signal-findings: $(TESTS)/signal_findings_test.c $(TESTS)/check.h \
		$(SRC)/signal_findings.h $(SRC)/signal_probe.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/signal_findings_test \
		$(TESTS)/signal_findings_test.c -lm
	$(Q)./$(BUILD)/signal_findings_test

# The 16-bit corpus, and the gate every later device-model ticket is measured
# against. `.scratch/device-model/issues/01-a-format-change-moves-no-answer.md`.
#
# testfiles/ is 8-bit and its answers are pinned; build/testfiles16/ is the
# same signal in the container a 12-bit device delivers, generated here and
# never committed. The check asserts the two arrive as bit-identical floats,
# which settles it for the whole program: there is exactly one byte-to-float
# seam (sdr_dsp_convert_iq, called at sdrprobe.c:311) and everything past it
# takes floats.
FORMAT_CAPTURES=gsm_arfcn_69 gsm_arfcn_113 adsb_cpr_pair lte_b20_pci28 \
	tetra_cc17 fm_rds_tsf
FORMAT16=$(patsubst %,$(BUILD)/testfiles16/%.bin,$(FORMAT_CAPTURES))

$(BUILD)/rescale_capture: scripts/rescale_capture.c
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -o $@ scripts/rescale_capture.c

$(BUILD)/testfiles16/%.bin: testfiles/%.bin testfiles/%.json \
		$(BUILD)/rescale_capture
	@mkdir -p $(BUILD)/testfiles16
	$(Q)./$(BUILD)/rescale_capture $< $@ >/dev/null

# One capture by hand, for a capture outside the corpus above.
rescale-capture: $(BUILD)/rescale_capture
	$(Q)./$(BUILD)/rescale_capture $(FILE_RESCALE) $(OUT_RESCALE)

# Threading a new parameter through a function with dozens of call sites, which
# this repository keeps needing: a device profile through sdr_dsp's three
# functions across six files, a full scale through LTE's three, a reference
# clock through survey_suspect's five. It was a scratch script three times.
#
#     make add-argument FILE=src/foo.c FUNC=bar INDEX=1 VALUE='&app->source'
#     make add-argument FILE=--self-test
#
# Read the diff afterwards: it is a text tool and rewrites a prototype the same
# way it rewrites a call.
add-argument:
	$(Q)python3 scripts/add_argument.py $(FILE) $(FUNC) $(INDEX) $(VALUE)

check-add-argument: scripts/add_argument.py
	$(Q)CHECK_TALLY=$(CHECK_TALLY) python3 scripts/add_argument.py --self-test

check-device-backend: $(TESTS)/device_backend_test.c $(TESTS)/check.h \
		$(SRC)/device_backend.h $(SRC)/device_profile.h \
		$(SRC)/capture_sidecar.h $(SRC)/backend_capture.c $(FORMAT16)
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/device_backend_test \
		$(TESTS)/device_backend_test.c $(SRC)/backend_capture.c -lm
	$(Q)./$(BUILD)/device_backend_test

check-capture-sidecar: $(TESTS)/capture_sidecar_test.c $(TESTS)/check.h \
		$(SRC)/capture_sidecar.h $(SRC)/device_profile.h $(FORMAT16)
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/capture_sidecar_test \
		$(TESTS)/capture_sidecar_test.c -lm
	$(Q)./$(BUILD)/capture_sidecar_test

check-device-profile: $(TESTS)/device_profile_test.c $(TESTS)/check.h \
		$(SRC)/device_profile.h $(SRC)/survey_bands.h \
		$(SRC)/survey_sweep.h $(SRC)/survey_suspect.h $(SRC)/reading_origin.h $(SRC)/clock_chain.h $(SRC)/band_plan.h \
		$(SRC)/sdr_dsp.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/device_profile_test \
		$(TESTS)/device_profile_test.c -lm
	$(Q)./$(BUILD)/device_profile_test

check-sample-format: $(TESTS)/sample_format_test.c $(TESTS)/check.h \
		$(SRC)/sdr_dsp.c $(SRC)/sdr_dsp.h $(FORMAT16) \
		$(SRC)/device_profile.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/sample_format_test \
		$(TESTS)/sample_format_test.c $(SRC)/sdr_dsp.c -lm
	$(Q)./$(BUILD)/sample_format_test

check-signal-probe: $(TESTS)/signal_probe_test.c $(TESTS)/check.h \
		$(SRC)/signal_probe.c $(SRC)/signal_probe.h \
		testfiles/carrier_75000_bare.bin
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
		$(SRC)/survey_suspect.h $(SRC)/reading_origin.h $(SRC)/clock_chain.h $(SRC)/survey_sweep.h $(SRC)/sdr_dsp.h \
		$(SRC)/band_plan.c $(SRC)/band_plan.h \
		$(SRC)/device_profile.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/survey_suspect_test \
		$(TESTS)/survey_suspect_test.c $(SRC)/band_plan.c -lm
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

# The survey's own state machine: which block is stale, when a step is over,
# what a confirmation pass asks about and concludes, what a watch reports.
# Every one of those used to be reachable only by running the program against
# a dongle and clicking (ADR-0012).
check-survey-session: $(TESTS)/survey_session_test.c $(TESTS)/check.h \
		$(SRC)/survey_session.c $(SRC)/survey_session.h \
		$(SRC)/survey_sweep.h $(SRC)/survey_carrier.h \
		$(SRC)/survey_confirm.h $(SRC)/survey_suspect.h $(SRC)/reading_origin.h $(SRC)/clock_chain.h \
		$(SRC)/site_history.c $(SRC)/site_history.h \
		$(SRC)/band_plan.c $(SRC)/band_plan.h \
		$(SRC)/signal_probe.c $(SRC)/sdr_dsp.c \
		testfiles/gsm_arfcn_69.bin testfiles/adsb_cpr_pair.bin
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/survey_session_test \
		$(TESTS)/survey_session_test.c $(SRC)/survey_session.c \
		$(SRC)/site_history.c $(SRC)/band_plan.c $(SRC)/signal_probe.c \
		$(SRC)/sdr_dsp.c -lm
	$(Q)./$(BUILD)/survey_session_test

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
# Who borrowed the receiver's tuning, and in what order they give it back.
# Nine views each restored their own frequency and no rule connected them, so
# an out-of-order return was expressible and silent. Plain integers here, so
# the ordering is reachable without a receiver or a window (ADR-0012).
check-receiver-lease: $(TESTS)/receiver_lease_test.c $(TESTS)/check.h \
		$(SRC)/receiver_lease.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/receiver_lease_test \
		$(TESTS)/receiver_lease_test.c -lm
	$(Q)./$(BUILD)/receiver_lease_test

# The order is the schedule, and it is measured.
#
# `make -j` starts targets in the order they appear here, so the longest one
# has to go first or its tail is added to the end of the run instead of
# overlapping it. Measured serially, the units are 166 s of work of which
# `check-signal-probe` alone is 54 -- and with it late in the list the units
# phase took **74 s** against a floor of 54, so twenty of those seconds were
# the pole starting last. Longest-processing-time-first is the whole of the
# fix.
#
# Re-measure after adding a slow suite:
#
#   for r in $(CHECK_UNITS); do /usr/bin/time -f "%e $$r" $(MAKE) $$r; done
#
CHECK_UNITS=check-signal-probe check-signal-frame check-receiver-runtime check-tetra-session check-lte-dsp \
	check-fm-dsp check-lte-mib check-gsm-session check-fm-session \
	check-lte-session check-survey-session check-gsm-dsp check-rds \
	check-lte-scan check-tetra-dsp check-layout check-adsb-session \
	check-sdr-dsp check-sample-format check-survey-store check-survey-record \
	check-lte-transport check-lte-turbo check-options check-tetra-sync \
	check-gsm-bcch check-adsb-dsp check-suspect check-acquisition check-config \
	check-signal-findings check-device-backend check-site-history \
	check-installation check-calibration check-freq-window check-device-profile \
	check-survey-carrier check-scan check-survey-sweep check-survey-bands \
	check-lte-confirm check-lte-findings check-text-wrap check-capture-sidecar \
	check-band-plan check-debug-log check-adsb-analysis check-input \
	check-geometry check-fm-scan check-row-list check-survey-confirm \
	check-gsm-continuity check-receiver-lease check-lte-stats \
	check-reading-origin check-clock-chain check-lte-chain-analysis \
	check-add-argument TALLY=$(BUILD)/check-tally

TALLY=$(BUILD)/check-tally

# How many suites at once.
#
# The units share nothing: each compiles its own binary from its own sources
# and runs it, with no fixture, no port and no temporary path in common. The
# two things they do share are handled rather than assumed.
#
# **The terminal** -- `--output-sync=target` buffers each suite's output and
# emits it whole, so the report still reads as a report. Without it the lines
# interleave mid-word and a failure can be sawn in half, which is worse than
# slow.
#
# **The tally** -- each suite appends one short line through `CHECK_TALLY`,
# with a single `fopen("a")`/`fprintf`/`fclose`, so it reaches the kernel as
# one `write()` under `O_APPEND` and the kernel serialises those for a regular
# file. And the failure mode is visible rather than silent: the summary counts
# suites as **lines** in that file, so a lost or torn line comes out as a
# wrong suite count in the last line of the report.
#
# Serial, this gate was **242 s** on an eight-core machine -- and 242 s again
# with nothing changed, because every `check-*` is a phony name, so make
# rebuilds all 56 binaries every run and `sdr_dsp.c` alone is compiled fifteen
# times. Parallelism does not fix that; it divides it.
#
# **Half the cores, not all of them, and that is measured.** These suites
# stream large float arrays and saturate memory bandwidth long before they run
# out of cores, so past a point another job makes every running job slower.
# The units phase on this eight-core machine, twice each where it mattered:
#
#   -j2  88 s     -j4  66 s, 66 s     -j8  72 s, 72 s     -j16  78 s
#   -j3  71 s     -j5  68 s           -j12 75 s
#
# So `nproc` is the wrong default and `nproc/2` is about right. Override it if
# a machine says otherwise -- `make CHECK_JOBS=8 check` -- and re-measure
# rather than assuming, because the shape of that curve is a property of the
# memory subsystem and not of this Makefile.
CHECK_JOBS?=$(shell echo $$(( $$(nproc 2>/dev/null || echo 4) / 2 )) )
ifeq ($(CHECK_JOBS),0)
CHECK_JOBS=1
endif

# `check-pipelines` is in the pool rather than after it, and first in it.
#
# It was a phase of its own, run serially once the units were done, so its
# 29 s was added to the end of the run: 66 + 29 = 95. As one more job among
# the units it overlaps them and the pair takes **70 s**. It goes first for
# the same reason the slowest unit does -- it is the second-longest thing
# here, and a long job started late is a tail nothing can hide.
#
# What that cost is the two section headings. They said which kind of check
# was which, and with the pool interleaving them they would have been a
# promise the order does not keep -- which is worse than not having them.
# Every suite still says what it covers on its own line.
check: sdrprobe
	@mkdir -p $(BUILD)
	@rm -f $(TALLY)
	@printf '\nsdrprobe checks -- no window, no receiver, nobody watching\n\n'
	@CHECK_TALLY=$(TALLY) $(MAKE) --no-print-directory -j$(CHECK_JOBS) \
		--output-sync=target check-pipelines $(CHECK_UNITS)
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
		$(SRC)/tetra_dsp.c $(SRC)/tetra_dsp.h \
		$(SRC)/signal_probe.c $(SRC)/signal_probe.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/tetra_dsp_test \
		$(TESTS)/tetra_dsp_test.c $(SRC)/tetra_dsp.c \
		$(SRC)/signal_probe.c -lm
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
		$(SRC)/lte_mib.c $(SRC)/lte_mib.h $(SRC)/lte_gold.h \
		$(SRC)/lte_chain_analysis.c $(SRC)/lte_chain_analysis.h \
		$(SRC)/lte_session.c $(SRC)/lte_session.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/lte_chain_probe \
		scripts/lte_chain_probe.c $(SRC)/lte_chain_analysis.c \
		$(SRC)/lte_session.c $(SRC)/lte_mib.c -lm
	$(Q)./$(BUILD)/lte_chain_probe $(FILE_LTE)

# Where the two-cell fixture stops separating two cells, and whether that is a
# property of the fixture or of one draw of its interfering traffic. Built
# from the check's own translation unit, with the module compiled in rather
# than linked, so it reaches the per-root scores -- and so it cannot drift
# from the conditions the suite runs under, which is how an earlier standalone
# harness came to report zero cells at every level.
#   MODE_TWO_CELL=--seeds      the rate over many draws of the traffic
#   MODE_TWO_CELL=--fixture    which stage of the fixture two compilers differ at
#   MODE_TWO_CELL=--diagnose   what a copy of the fixture leaves out
MODE_TWO_CELL ?= --seeds
probe-two-cell: $(TESTS)/lte_dsp_test.c $(TESTS)/two_cell_sweep.inc \
		$(TESTS)/check.h $(SRC)/lte_dsp.c $(SRC)/lte_dsp.h \
		$(SRC)/lte_mib.c $(SRC)/lte_mib.h $(SRC)/lte_gold.h \
		$(SRC)/device_profile.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -Wno-unused-function -I$(SRC) -I$(TESTS) \
		-DLTE_TWO_CELL_SWEEP -o $(BUILD)/two_cell_sweep \
		$(TESTS)/lte_dsp_test.c $(SRC)/lte_mib.c -lm
	$(Q)./$(BUILD)/two_cell_sweep $(MODE_TWO_CELL) $(SEEDS_TWO_CELL)

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

# What signal_probe says about a capture, at a signal and at its controls.
# The shape is signal-against-controls because a measurement at one frequency
# is a number and the same measurement where nothing should be is what makes
# it evidence.
FILE_SIGNAL?=testfiles/carrier_75000_bare.bin
RATE_SIGNAL?=2000000
AT_SIGNAL?=300000
CONTROLS_SIGNAL?=-200000,600000
CHANNEL_SIGNAL?=20000
SEARCH_SIGNAL?=0
GUARD_SIGNAL?=150000
# How much of the capture to use. The answer depends on it: a fixed-frequency
# mix cannot follow a drifting carrier, so the standing fraction falls as the
# observation lengthens.
PAIRS_SIGNAL?=0
probe-signal: scripts/signal_report.c $(SRC)/signal_probe.c \
		$(SRC)/signal_probe.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/signal_report \
		scripts/signal_report.c $(SRC)/signal_probe.c -lm
	$(Q)./$(BUILD)/signal_report $(FILE_SIGNAL) $(RATE_SIGNAL) \
		$(AT_SIGNAL) $(CONTROLS_SIGNAL) $(CHANNEL_SIGNAL) \
		$(SEARCH_SIGNAL) $(GUARD_SIGNAL) $(PAIRS_SIGNAL)

probe-periodicity: scripts/signal_periodicity.c $(SRC)/signal_probe.c \
		$(SRC)/signal_probe.h
	@mkdir -p $(BUILD)
	$(Q)$(CC) $(CFLAGS) -o $(BUILD)/signal_periodicity \
		-I$(SRC) scripts/signal_periodicity.c $(SRC)/signal_probe.c -lm
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

.PHONY: all check hooks check-survey-session check-signal-probe check-signal-findings check-lte-findings check-lte-stats check-lte-confirm check-config check-survey-carrier check-survey-confirm check-site-history check-survey-store check-lte-dsp check-lte-mib check-lte-scan check-gsm-bcch check-suspect check-input check-geometry check-gsm-continuity check-adsb-analysis check-scan check-acquisition check-survey-sweep check-options check-calibration check-pipelines check-sdr-dsp check-gsm-dsp check-adsb-dsp check-band-plan check-dsp check-layout check-freq-window probe-gsm-chain probe-adsb-chain probe-lte-chain probe-nbiot probe-two-cell probe-signal probe-periodicity probe-survey-threshold bench-dsp screens rescale-capture check-sample-format check-device-profile check-capture-sidecar check-device-backend check-add-argument check-gsm-session check-tetra-session check-lte-session check-adsb-session check-fm-session add-argument clean
