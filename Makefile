CFLAGS?=-O2 -g -Wall -W $(shell pkg-config --cflags librtlsdr)
LDLIBS+=$(shell pkg-config --libs librtlsdr) -lm
CC?=gcc

SRC=src
TESTS=tests
VENDOR=vendor
BUILD=build

all: sdrprobe

DSP_SRC=$(SRC)/sdr_dsp.c $(SRC)/gsm_dsp.c $(SRC)/adsb_dsp.c
DSP_HDR=$(SRC)/sdr_dsp.h $(SRC)/gsm_dsp.h $(SRC)/adsb_dsp.h
GUI_SRC=$(SRC)/sdrgui.c
GUI_HDR=$(SRC)/sdrgui.h
RAYGUI_FLAGS=-I$(VENDOR) $(shell pkg-config --cflags raylib)

# The vendored raygui header is not -Wall -W clean; compile it in isolation.
# The one intermediate object lives under $(BUILD)/ to keep the root tidy.
$(BUILD)/raygui_impl.o: $(SRC)/raygui_impl.c $(VENDOR)/raygui.h
	@mkdir -p $(BUILD)
	$(CC) -O2 $(RAYGUI_FLAGS) -w -c $(SRC)/raygui_impl.c -o $@

sdrprobe: $(SRC)/sdrprobe.c $(DSP_SRC) $(DSP_HDR) $(GUI_SRC) $(GUI_HDR) \
		$(BUILD)/raygui_impl.o
	$(CC) $(CFLAGS) $(RAYGUI_FLAGS) -pthread \
		-o $@ $(SRC)/sdrprobe.c $(DSP_SRC) $(GUI_SRC) $(BUILD)/raygui_impl.o \
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

check-adsb-dsp: $(TESTS)/adsb_dsp_test.c $(SRC)/adsb_dsp.c $(SRC)/adsb_dsp.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/adsb_dsp_test \
		$(TESTS)/adsb_dsp_test.c $(SRC)/adsb_dsp.c -lm
	./$(BUILD)/adsb_dsp_test

check-dsp: check-sdr-dsp check-gsm-dsp check-adsb-dsp

# White-box diagnostic walk through the GSM SCH chain (not a unit test). It
# compiles gsm_dsp.c in (to reach its statics), so it links only sdr_dsp.c.
FILE ?= testfiles/gsm_arfcn_69.bin
probe-gsm-chain: scripts/gsm_chain_probe.c $(SRC)/gsm_dsp.c $(SRC)/gsm_dsp.h \
		$(SRC)/sdr_dsp.c $(SRC)/sdr_dsp.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(SRC) -o $(BUILD)/gsm_chain_probe \
		scripts/gsm_chain_probe.c $(SRC)/sdr_dsp.c -lm
	./$(BUILD)/gsm_chain_probe $(FILE)

clean:
	rm -rf sdrprobe $(BUILD)

.PHONY: all check-sdr-dsp check-gsm-dsp check-adsb-dsp check-dsp probe-gsm-chain clean
