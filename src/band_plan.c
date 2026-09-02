#include "band_plan.h"

#include <stddef.h>

/*
 * Ordered by frequency, non-overlapping. Coverage is what an RTL-SDR can hear
 * and someone might plausibly point this program at; a gap means the table has
 * nothing useful to say, not that the spectrum is empty there.
 *
 * The allocations are Portugal's, which is to say ITU Region 1 as ANACOM
 * applies it. Where a European allocation differs from the wider Region 1 one
 * the Portuguese arrangement wins -- the 700 MHz band cleared for mobile in
 * 2020 is the visible example, and it is why UHF television stops at 694 MHz
 * here rather than at 790.
 */
static const struct band_plan_entry entries[] = {
    {   148500.0,    283500.0, "Long-wave broadcast", "AM", BAND_PLAN_NONE },
    {   526500.0,   1606500.0, "Medium-wave broadcast", "AM", BAND_PLAN_NONE },
    {  2300000.0,   2498000.0, "120 m broadcast", NULL, BAND_PLAN_NONE },
    {  3200000.0,   3400000.0, "90 m broadcast", NULL, BAND_PLAN_NONE },
    {  3500000.0,   3800000.0, "80 m amateur", NULL, BAND_PLAN_NONE },
    {  5900000.0,   6200000.0, "49 m broadcast", NULL, BAND_PLAN_NONE },
    {  7000000.0,   7200000.0, "40 m amateur", NULL, BAND_PLAN_NONE },
    {  9400000.0,   9900000.0, "31 m broadcast", NULL, BAND_PLAN_NONE },
    { 11600000.0,  12100000.0, "25 m broadcast", NULL, BAND_PLAN_NONE },
    { 13570000.0,  13870000.0, "22 m broadcast", NULL, BAND_PLAN_NONE },
    { 14000000.0,  14350000.0, "20 m amateur", NULL, BAND_PLAN_NONE },
    { 15100000.0,  15800000.0, "19 m broadcast", NULL, BAND_PLAN_NONE },
    { 17480000.0,  17900000.0, "16 m broadcast", NULL, BAND_PLAN_NONE },
    { 21000000.0,  21450000.0, "15 m amateur", NULL, BAND_PLAN_NONE },
    { 26965000.0,  27405000.0, "CB", "27 MHz, FM/AM/SSB", BAND_PLAN_NONE },
    { 28000000.0,  29700000.0, "10 m amateur", NULL, BAND_PLAN_NONE },
    { 50000000.0,  52000000.0, "6 m amateur", NULL, BAND_PLAN_NONE },
    { 76000000.0,  87500000.0, "VHF band II (Japan/OIRT edge)", NULL,
      BAND_PLAN_NONE },
    { 87500000.0, 108000000.0, "FM broadcast", "200 kHz raster, wideband FM",
      BAND_PLAN_NONE },
    { 108000000.0, 117975000.0, "Aeronautical navigation", "VOR, ILS",
      BAND_PLAN_NONE },
    { 117975000.0, 137000000.0, "VHF airband", "25 kHz channels, AM",
      BAND_PLAN_NONE },
    { 137000000.0, 138000000.0, "Weather satellite downlink", "NOAA APT",
      BAND_PLAN_NONE },
    { 144000000.0, 146000000.0, "2 m amateur", NULL, BAND_PLAN_NONE },
    { 146000000.0, 148000000.0, "2 m amateur (Region 2)", NULL,
      BAND_PLAN_NONE },
    { 156000000.0, 162050000.0, "Marine VHF", "AIS at 161.975/162.025",
      BAND_PLAN_NONE },
    { 174000000.0, 230000000.0, "VHF band III / DAB+", "blocks 5A-12D",
      BAND_PLAN_NONE },
    { 230000000.0, 240000000.0, "VHF band III (upper)", NULL, BAND_PLAN_NONE },
    { 380000000.0, 400000000.0, "TETRA", "SIRESP emergency services",
      BAND_PLAN_NONE },
    { 430000000.0, 440000000.0, "70 cm amateur / 433 ISM", "overlapping uses",
      BAND_PLAN_NONE },
    { 446000000.0, 446200000.0, "PMR446", "12.5 kHz channels, FM",
      BAND_PLAN_NONE },
    { 470000000.0, 694000000.0, "UHF television", "DVB-T, 8 MHz channels",
      BAND_PLAN_NONE },
    { 703000000.0, 733000000.0, "LTE band 28 uplink", "700 MHz, cleared 2020",
      BAND_PLAN_NONE },
    { 758000000.0, 788000000.0, "LTE band 28 downlink", "700 MHz",
      BAND_PLAN_LTE },
    { 791000000.0, 821000000.0, "LTE band 20 downlink", "800 MHz",
      BAND_PLAN_LTE },
    { 832000000.0, 862000000.0, "LTE band 20 uplink", "800 MHz",
      BAND_PLAN_NONE },
    { 863000000.0, 870000000.0, "868 MHz ISM", "meters, sensors, LoRa",
      BAND_PLAN_NONE },
    { 880000000.0, 915000000.0, "GSM 900 / LTE B8 uplink",
      "handsets transmit here", BAND_PLAN_NONE },
    /* Two technologies share this allocation, and the table names one
       decoder. GSM wins it because the GSM view has a band scan that finds a
       channel on its own, where LTE needs a carrier centre it cannot guess;
       pointing someone at the more useful screen is the whole purpose of the
       field. */
    { 925000000.0, 960000000.0, "GSM 900 / LTE B8 downlink",
      "200 kHz channels, ARFCN 1-124", BAND_PLAN_GSM },
    { 1090000000.0, 1090500000.0, "Mode S / ADS-B", "1090 MHz extended squitter",
      BAND_PLAN_ADSB },
    { 1164000000.0, 1215000000.0, "GNSS L5 / E5", "GPS L5, Galileo E5",
      BAND_PLAN_NONE },
    { 1227000000.0, 1230000000.0, "GPS L2", NULL, BAND_PLAN_NONE },
    { 1240000000.0, 1300000000.0, "23 cm amateur", NULL, BAND_PLAN_NONE },
    { 1452000000.0, 1492000000.0, "L-band (LTE B32 downlink)",
      "supplementary downlink", BAND_PLAN_NONE },
    { 1525000000.0, 1559000000.0, "Inmarsat / AERO downlink", NULL,
      BAND_PLAN_NONE },
    { 1559000000.0, 1610000000.0, "GNSS L1 / E1", "GPS L1 at 1575.42 MHz",
      BAND_PLAN_NONE },
    { 1710000000.0, 1785000000.0, "GSM 1800 / LTE B3 uplink", "1800 MHz",
      BAND_PLAN_NONE }
};

const struct band_plan_entry *band_plan_lookup(double hz) {
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++)
        if (hz >= entries[i].lower_hz && hz < entries[i].upper_hz)
            return &entries[i];
    return NULL;
}

int band_plan_entry_count(void) {
    return (int)(sizeof(entries) / sizeof(entries[0]));
}

const struct band_plan_entry *band_plan_entry_at(int index) {
    if (index < 0 || index >= band_plan_entry_count())
        return NULL;
    return &entries[index];
}
