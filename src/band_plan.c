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
 *
 * The source is ANACOM's Quadro Nacional de Atribuicao de Frequencias, the
 * 2010/2011 edition of 20 June 2012, whose table gives a band's allocated
 * services and its principal national applications side by side. Entries added
 * from it name the band edges the QNAF itself uses, to the kilohertz, rather
 * than rounding to something tidier -- a table that disagrees with its source
 * about where a band begins is worse than one that says nothing.
 *
 * "Faixa condicionada" marks a great deal of 230-400 MHz there. The QNAF's
 * legend for it is not reproduced in what this table was built from, so the
 * entries below say what the allocation *is* -- fixed and mobile -- and do not
 * guess at what the condition is. That is the rule for the whole table: an
 * allocation, never an identification, and a gap in preference to a guess.
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
    /* QNAF: FIXO and MOVEL through 30-47 MHz, the national application named
       as SMT private land-mobile networks. */
    { 30000000.0,  47000000.0, "Land mobile", "private networks",
      BAND_PLAN_NONE },
    /* QNAF: RADIODIFUSAO, band I television, channels 2-4 -- and analogue
       transmission ceased here by 26 April 2012 under RCM 26/2009. The
       allocation stands; what is heard in it is not television. */
    { 47000000.0,  50000000.0, "VHF band I television", "analogue, off since 2012",
      BAND_PLAN_NONE },
    { 50000000.0,  52000000.0, "6 m amateur", NULL, BAND_PLAN_NONE },
    { 52000000.0,  68000000.0, "VHF band I television", "analogue, off since 2012",
      BAND_PLAN_NONE },
    /* QNAF: FIXO, MOVEL except aeronautical mobile; the 80 MHz plan. */
    { 68000000.0,  74800000.0, "Land mobile", "private networks, 80 MHz plan",
      BAND_PLAN_NONE },
    /* QNAF: RADIONAVEGACAO AERONAUTICA -- the ILS marker beacons. Four
       hundred kilohertz wide, and worth the entry: it is the last thing in
       the sweep the table could not name. */
    { 74800000.0,  75200000.0, "Aeronautical radionavigation", "ILS markers",
      BAND_PLAN_NONE },
    /* QNAF: FIXO, MOVEL except aeronautical mobile; the 80 MHz plan again,
       running up to the broadcast band. */
    { 75200000.0,  76000000.0, "Land mobile", "private networks, 80 MHz plan",
      BAND_PLAN_NONE },
    { 76000000.0,  87500000.0, "VHF band II (Japan/OIRT edge)", NULL,
      BAND_PLAN_NONE },
    { 87500000.0, 108000000.0, "FM broadcast", "200 kHz raster, wideband FM",
      BAND_PLAN_FM },
    { 108000000.0, 117975000.0, "Aeronautical navigation", "VOR, ILS",
      BAND_PLAN_NONE },
    { 117975000.0, 137000000.0, "VHF airband", "25 kHz channels, AM",
      BAND_PLAN_NONE },
    { 137000000.0, 138000000.0, "Weather satellite downlink", "NOAA APT",
      BAND_PLAN_NONE },
    { 144000000.0, 146000000.0, "2 m amateur", NULL, BAND_PLAN_NONE },
    { 146000000.0, 148000000.0, "2 m amateur (Region 2)", NULL,
      BAND_PLAN_NONE },
    /* QNAF: FIXO and MOVEL either side of a mobile-satellite sliver. */
    { 148000000.0, 149900000.0, "Land mobile", NULL, BAND_PLAN_NONE },
    { 149900000.0, 150050000.0, "Mobile-satellite uplink", NULL,
      BAND_PLAN_NONE },
    { 150050000.0, 156000000.0, "Land mobile", NULL, BAND_PLAN_NONE },
    { 156000000.0, 162050000.0, "Marine VHF", "AIS at 161.975/162.025",
      BAND_PLAN_NONE },
    /* QNAF: FIXO, MOVEL except aeronautical mobile; the 160 MHz plan, with
       local paging at 169.175 MHz and AIS receive channels at 161.975 and
       162.025 MHz inside it. */
    { 162050000.0, 174000000.0, "Land mobile", "private networks, 160 MHz plan",
      BAND_PLAN_NONE },
    { 174000000.0, 230000000.0, "VHF band III / DAB+", "blocks 5A-12D",
      BAND_PLAN_NONE },
    { 230000000.0, 240000000.0, "VHF band III (upper)", NULL, BAND_PLAN_NONE },
    /*
     * QNAF: FIXO and MOVEL in every row from 230 to 328.6 MHz and again from
     * 335.4, each marked "faixa condicionada". Split only where the QNAF
     * itself puts a different service in the middle of it.
     */
    { 240000000.0, 328600000.0, "Fixed and mobile", "conditioned band",
      BAND_PLAN_NONE },
    /* QNAF: RADIONAVEGACAO AERONAUTICA -- the ILS glide path. */
    { 328600000.0, 335400000.0, "Aeronautical radionavigation", "ILS glide path",
      BAND_PLAN_NONE },
    { 335400000.0, 380000000.0, "Fixed and mobile", "conditioned band",
      BAND_PLAN_NONE },
    { 380000000.0, 400000000.0, "TETRA", "SIRESP emergency services",
      BAND_PLAN_NONE },
    /* QNAF: AUXILIARES DE METEOROLOGIA, the application named as radiosondes. */
    { 403000000.0, 406000000.0, "Meteorological aids", "radiosondes",
      BAND_PLAN_NONE },
    /* QNAF: MOVEL POR SATELITE (Terra-espaco) -- COSPAS-SARSAT beacons. */
    { 406000000.0, 406100000.0, "Emergency beacons", "COSPAS-SARSAT",
      BAND_PLAN_NONE },
    /* QNAF: FIXO, the application named as monovias -- fixed point-to-point
       links -- continuously from 406.1 to 430 MHz. */
    { 406100000.0, 430000000.0, "Fixed links", "point to point",
      BAND_PLAN_NONE },
    { 430000000.0, 440000000.0, "70 cm amateur / 433 ISM", "overlapping uses",
      BAND_PLAN_NONE },
    /* QNAF: FIXO, MOVEL except aeronautical mobile; the 440-450 MHz plan,
       with PMR446 and TETRA direct-mode channels inside it. */
    { 440000000.0, 446000000.0, "Land mobile", "private networks, 440 MHz plan",
      BAND_PLAN_NONE },
    { 446000000.0, 446200000.0, "PMR446", "12.5 kHz channels, FM",
      BAND_PLAN_NONE },
    { 446200000.0, 460000000.0, "Land mobile", "private networks, 440 MHz plan",
      BAND_PLAN_NONE },
    /* QNAF: FIXO and MOVEL; the 450 MHz plan, with public land mobile at
       465.80625-467.45 MHz and local paging above it. */
    { 460000000.0, 470000000.0, "Land mobile", "private networks, 450 MHz plan",
      BAND_PLAN_NONE },
    { 470000000.0, 694000000.0, "UHF television", "DVB-T, 8 MHz channels",
      BAND_PLAN_NONE },
    /* Neither allocated to television nor to the 700 MHz band: the guard
       between them, left by the APT700 arrangement the 2020 clearance
       adopted. Named so that a survey can say "nothing is allocated here"
       rather than "the table does not know". */
    { 694000000.0, 703000000.0, "700 MHz guard band", NULL, BAND_PLAN_NONE },
    { 703000000.0, 733000000.0, "LTE band 28 uplink", "700 MHz, cleared 2020",
      BAND_PLAN_NONE },
    { 733000000.0, 758000000.0, "700 MHz centre gap", "duplex gap",
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
    /* QNAF: MOVEL POR SATELITE (Terra-espaco) in every row from 1610 to
       1660.5 MHz -- GMPCS handsets, maritime distress, land mobile satellite.
       All of it is the uplink; the downlinks are elsewhere. */
    { 1610000000.0, 1660500000.0, "Mobile-satellite uplink", "GMPCS, GMDSS",
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
