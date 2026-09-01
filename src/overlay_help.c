#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "view.h"
#include "sdrgui.h"

/*
 * The help overlay: what each chart plots and how to read it, reachable with
 * `h` from every view and from the calibration and scan overlays.
 *
 * The text lives here as one table of topics rather than beside each view,
 * because a reader arrives with a question ("what is this chart telling me?")
 * and wants the answer next to the neighbouring answers, not scattered across
 * five files. Opening it picks the topic for the screen underneath, so the
 * common case is one keypress.
 *
 * It says nothing a reader can already see on the screen. Every figure in it
 * -- window sizes, thresholds, decay rates -- is one a caller can check
 * against the constants in acquisition.h, sdr_dsp.h and gsm_dsp.h, and should
 * be rechecked when those change.
 */

enum help_topic {
    HELP_OVERVIEW,
    HELP_MAGNITUDE,
    HELP_SPECTRUM,
    HELP_WATERFALL,
    HELP_SCATTER,
    HELP_SURVEY,
    HELP_QUALITY,
    HELP_SCAN,
    HELP_BURST,
    HELP_CONSTELLATION,
    HELP_ADSB,
    HELP_ADSB_ANALYSIS,
    HELP_CALIBRATION,
    HELP_TOPIC_COUNT
};

struct help_page {
    const char *entry;   /* sidebar label */
    const char *title;   /* heading above the body */
    const char *body;
};

static const struct help_page help_pages[HELP_TOPIC_COUNT] = {
{
    "Overview & keys",
    "Reading the display",
    "sdrprobe shows one sample block at a time: 256 KB, 131072 I/Q pairs, "
    "65.5 ms of signal at 2 MS/s. A block that arrives while the previous one "
    "is still being drawn overwrites it rather than queueing behind it, so "
    "every chart shows the freshest signal instead of a backlog; the HUD "
    "counts the blocks that were overwritten.\n"
    "\n"
    "Two tabs. Scope charts the signal itself and stops at signal statistics: "
    "nothing there has been demodulated, and nothing there is a message. "
    "Decode is where bits become messages -- GSM synchronisation bursts and "
    "Mode S / ADS-B frames.\n"
    "\n"
    "Every chart draws inside its own rectangle: caption at the top left, "
    "axis labels in the left gutter, and a summary line under the plot "
    "repeating the axis range and the settings behind it. Hover over a plot "
    "and a readout box follows the pointer with the value under it.\n"
    "\n"
    "Two units run through all of it. dBFS is decibels relative to the "
    "digitizer's full scale, so 0 dBFS is the strongest signal the receiver "
    "can represent and every reading is negative; a quiet band sits around "
    "-80 to -100 dBFS. Sample units are raw magnitudes from the 8-bit I/Q "
    "pairs, where 180.3 is full scale.\n"
    "\n"
    "Keys\n"
    "1-5   Scope tab: magnitude, spectrum, I/Q scatter, waterfall, survey\n"
    "1-2   Decode tab: GSM, ADS-B\n"
    "Up/Down   stretch or compress the active chart's scale\n"
    "s   Settings: centre frequency, gain, PPM, DC-spike filter\n"
    "c   Calibration\n"
    "h   this help\n"
    "+ - 0   band survey: zoom in, out, and back to the whole sweep\n"
    "Left/Right   band survey: walk the zoomed window\n"
    "q   quit, from any screen but the Settings panel\n"
    "Esc   leave the current screen; quit from the Scope tab\n"
    "\n"
    "In here: Up/Down or the mouse wheel scrolls, Left/Right changes topic, "
    "h or Esc closes."
},
{
    "Magnitude",
    "Magnitude view (Scope, key 1)",
    "What it plots: the magnitude of each I/Q pair -- signal strength with the "
    "phase discarded -- against time within one sample block. The left edge is "
    "the start of the block and the right edge is its end; the label under the "
    "right corner gives the span in milliseconds.\n"
    "\n"
    "One pixel column is one time bin, and its height is the PEAK magnitude in "
    "that bin, not the average. That is deliberate. A Mode S frame lasts 120 "
    "us, far less than a column covers, and an average would bury it in the "
    "noise either side of it; a peak keeps it. So a spike that reaches the top "
    "of a column really happened, and the flat band along the bottom is the "
    "noise floor.\n"
    "\n"
    "Reading it: a quiet band is a fuzzy horizontal stripe of even thickness. "
    "Isolated vertical spikes are bursts -- aircraft transmissions at 1090 "
    "MHz, GSM bursts on a downlink channel. Regular spikes at an even spacing "
    "are a periodic transmitter. A trace pinned flat against the top of the "
    "axis is not a strong signal, it is clipping: give gain back in Settings "
    "and read it again.\n"
    "\n"
    "The line under the plot gives the block's absolute min, mean and max "
    "alongside the axis range. Up/Down moves the top of the axis only -- zero "
    "stays at the bottom -- so a weak signal can be stretched without changing "
    "anything that was measured."
},
{
    "Spectrum",
    "Spectrum view (Scope, key 2)",
    "What it plots: signal power across the sampled bandwidth. The x axis is "
    "frequency in MHz with the receiver's centre frequency in the middle and a "
    "span of one sample rate (2 MHz at 2 MS/s); the y axis is dBFS, topping "
    "out at +6.\n"
    "\n"
    "How it is made: the block is cut into 64 non-overlapping 2048-pair "
    "windows, each Hann-windowed and transformed by a 2048-point FFT, and the "
    "traces are the average across those windows -- the caption says how many "
    "went in. Each bin is sample_rate/2048 wide, 977 Hz at 2 MS/s, which is "
    "the finest frequency detail this view can show.\n"
    "\n"
    "Two traces. The brighter one is that average. The fainter one is peak "
    "hold: the highest level seen at each frequency, decaying 20 dB per "
    "second, so a burst stays visible for a moment after it has gone. A "
    "carrier that is always on draws the two together; a bursty signal shows "
    "as peak hold standing well above the average.\n"
    "\n"
    "Reading it: a narrow spike is a carrier. A flat-topped block is a "
    "modulated channel, and its width is the channel's -- a GSM channel is 200 "
    "kHz wide. The rough floor between them is noise. A spike exactly at the "
    "centre frequency is usually the receiver's own DC offset rather than "
    "anything on the air; the DC-spike filter in Settings removes it.\n"
    "\n"
    "Up/Down moves the bottom of the dBFS axis in 10 dB steps while the top "
    "stays put, which is how to give a weak signal more of the plot."
},
{
    "Waterfall",
    "Waterfall view (Scope, key 4)",
    "What it plots: the same spectrum as the spectrum view, one row per block, "
    "stacked over time. The newest row is at the top and older rows scroll "
    "down; the left gutter labels rows by age in seconds, and the line under "
    "the plot gives the visible history span. Colour is power -- dark blue at "
    "the bottom of the scale, through blue and purple, to orange and white at "
    "the top.\n"
    "\n"
    "Reading it: a vertical stripe is a carrier that stays on. Short dashes "
    "are bursts, and their spacing is the transmitter's timing. A stripe that "
    "leans sideways is a frequency that is moving -- the transmitter's, or the "
    "receiver's own crystal warming up. Brightening spread across the whole "
    "width is the noise floor rising, not a signal.\n"
    "\n"
    "Up/Down moves the bottom of the colour scale, which matters more here "
    "than on any other chart. Raise it and only the strongest activity keeps "
    "its colour, which is how a weak carrier is picked out of a busy band; "
    "lower it and the noise floor lights up.\n"
    "\n"
    "The GSM view and the calibration overlay draw this same waterfall with "
    "its x axis labelled by ARFCN instead of frequency, zoomed to the channel "
    "being inspected."
},
{
    "I/Q scatter",
    "I/Q scatter view (Scope, key 3)",
    "What it plots: one dot per I/Q pair, in-phase on the x axis against "
    "quadrature on the y axis, both normalised to full scale (+/-1.0). Up to "
    "4096 pairs are taken from each block, and one second of blocks is kept "
    "with the older dots fading, so what you see is the recent distribution of "
    "the signal rather than one instant.\n"
    "\n"
    "It is not a constellation. Nothing here has been demodulated and no point "
    "carries a symbol decision -- this is the raw distribution of acquired "
    "samples. The Decode tab's GSM view has a real constellation; that topic "
    "is next but one.\n"
    "\n"
    "Reading it by shape:\n"
    "A round blob centred on the origin is noise alone.\n"
    "A filled ring is a constant-envelope signal -- FM, GMSK, anything whose "
    "phase moves while its amplitude does not.\n"
    "A blob sitting off the origin is a DC offset in the receiver.\n"
    "A line or a cross is a strong carrier beating against the tuning.\n"
    "Dots crowded onto a square edge, or a hard rim with nothing beyond it, "
    "is clipping.\n"
    "\n"
    "Dots further from the origin are drawn brighter, so the outline of the "
    "shape stands out against the noise filling it. Up/Down changes the axis "
    "range; the line under the plot gives the current limit and how many "
    "points the latest block contributed."
},
{
    "Band survey",
    "Band survey (Scope, key 5)",
    "Every other view shows the 2 MHz the receiver is tuned to. This one sweeps "
    "a range you choose and shows what is on it, which is the question you "
    "start with when you do not already know where to look.\n"
    "\n"
    "How it sweeps: the receiver is retuned in steps of 1.6 MHz -- the usable "
    "middle of its 2 MHz span, the edges being where the tuner's response rolls "
    "off -- and every block that arrives while it sits there is folded into the "
    "chart, keeping the highest level seen in each bin. The whole tuner, 24 to "
    "1766 MHz, is about a thousand steps. The bin width in use is on the header "
    "line while it sweeps: a narrow range gets finer bins than a wide one.\n"
    "\n"
    "Dwell is how long it sits on each step, and it decides what kind of "
    "signal the sweep can see at all. At the default tenth of a second a step "
    "catches whatever happens to be transmitting at that instant, which is the "
    "wrong tool for anything bursty -- a channel that keys up for a moment "
    "every few seconds is simply absent from most steps. Raise the dwell and "
    "each step listens longer, and because the fold keeps the peak, a burst "
    "anywhere inside the dwell leaves its mark. The cost is linear: at one "
    "second a step, the whole tuner is about twenty minutes, so dwell long "
    "over a band and briefly over everything.\n"
    "\n"
    "Sweep sweeps what the chart is showing. Zoomed out, that is the range in "
    "the fields. Zoomed in, it is the window -- so after finding something and "
    "zooming to it, pressing Sweep again surveys just that span, with the "
    "dwell now in the field and finer bins, because the bin width follows the "
    "span. Typing a range moves the chart to it, so the fields and the window "
    "never disagree about what Sweep will do.\n"
    "\n"
    "Sweeping a zoomed window narrows the swept range to it, which throws away "
    "everything outside: zooming out afterwards has nothing wider to show.\n"
    "\n"
    "Reset zoom backs out one level at a time, which is the way out of that. "
    "Zoomed in, it shows the whole sweep. Already showing all of it after a "
    "narrowed sweep, it puts the earlier survey back -- the measurements, the "
    "candidates and the range it covered, exactly as they were, because a copy "
    "is kept before the region replaces them and restoring it costs nothing. "
    "With nothing to go back to, it sets the tuner's full span in the fields. "
    "It never starts a sweep by itself: a full sweep is minutes, and a button "
    "press should not commit you to that silently.\n"
    "\n"
    "Candidates are the green ticks above the trace: peaks standing at least "
    "8 dB above the noise either side of them, judged by how far you would have "
    "to descend to reach anything higher. That measure -- not simply height "
    "above a floor -- is what stops the shoulder of a strong carrier being "
    "reported as a signal of its own, and what keeps a weak carrier beside a "
    "strong one from being swallowed. They are called candidates because that "
    "is all they are: something is radiating there.\n"
    "\n"
    "The shaded regions behind the trace are the band plan: what the spectrum "
    "there is allocated to in Portugal, named where there is room for the name "
    "and always in the cursor readout. They are drawn from the same table the "
    "detail panel quotes, and they claim exactly as much: this is what the "
    "region is for, not what is transmitting in it.\n"
    "\n"
    "The chart holds as much as you swept, which can be 1.7 GHz in which a "
    "200 kHz carrier is a fifth of a pixel wide. Drag a rectangle across the "
    "chart to zoom into exactly that span; the width is shown while you drag, "
    "and a press that does not move is still a click, so dragging a zoom and "
    "clicking a candidate share one button. + and - zoom, Left and Right walk "
    "the window, 0 goes back to the whole sweep, and the wheel zooms while the "
    "pointer is over the chart.\n"
    "\n"
    "Zooming re-draws the same measurements larger rather than re-sweeping, so "
    "it costs nothing and changes nothing. The level axis follows what is on "
    "screen, so a quiet band beside a loud one is not left flat on the floor, "
    "and the candidate list narrows to the window, so zooming into a band "
    "lists what is in that band rather than what is loudest elsewhere. Zooming "
    "in keeps the selected candidate in view, and selecting one the window has "
    "scrolled past brings the window to it.\n"
    "\n"
    "Selecting one, by clicking it or with Up and Down, retunes to it and "
    "measures it for two seconds. The receiver is deliberately parked 300 kHz "
    "off the frequency, so the carrier is never measured on top of the "
    "receiver's own DC spike.\n"
    "\n"
    "What the panel then reports:\n"
    "peak power and how far it stands above the local floor;\n"
    "occupied bandwidth, between the points where it falls a stated number of "
    "dB below its peak -- the drop is held clear of the noise, and the figure "
    "used is printed beside the width;\n"
    "duty, the fraction of blocks the carrier was actually up in. Continuous is "
    "a broadcast; intermittent or bursty is traffic, and one GSM channel can be "
    "either depending on whether anyone is talking;\n"
    "stability, how far the measured centre wandered. A kilohertz is a stable "
    "carrier; tens of kilohertz is something hopping, drifting, or too weak to "
    "measure well.\n"
    "\n"
    "Scan this frequency, on the candidate panel, sweeps a few megahertz "
    "centred on the one selected. That is the drill-down the survey exists "
    "for: the wide sweep says something is at 943.2 MHz, and the short sweep "
    "says what its neighbourhood looks like, in bins as fine as the FFT "
    "allows rather than the hundreds of kilohertz a full-tuner sweep can "
    "afford. Reset zoom comes back afterwards, because the survey it replaced "
    "is kept.\n"
    "\n"
    "Open waterfall tunes to the candidate and switches to view 4, so you can "
    "watch it over time: whether a carrier is steady, whether bursts repeat on "
    "a pattern, whether it drifts. The tuning puts it 300 kHz off centre for "
    "the same reason the measurement does, and the waterfall's history is "
    "cleared first, because rows drawn at another frequency say nothing about "
    "this one. That tuning stays when you leave, unlike the survey's own "
    "retuning, which is put back.\n"
    "\n"
    "The band plan line names the service allocated to that frequency, and "
    "carries the words 'a frequency lookup, not a detection' because that is "
    "exactly what it is. Nothing has been demodulated. A carrier inside the GSM "
    "downlink allocation is a carrier inside an allocation -- it could be an "
    "interferer, a harmonic, or a neighbour's amplifier leaking. When the "
    "allocation has a decoder in this program, the button offers to point it "
    "there, which is an invitation to go and find out rather than an answer."
},
{
    "Signal quality",
    "The signal-quality line",
    "Where: under the header on the Scope tab, and again above the charts in "
    "the GSM view. One line, all of it computed from a single sample block, "
    "reporting whether the receiver is set up well for what it is hearing.\n"
    "\n"
    "noise (p10) -- the 10th-percentile magnitude of the block: the noise "
    "floor.\n"
    "signal (p99.5) -- the 99.5th-percentile magnitude: the strong end, taken "
    "high enough to be real activity and low enough not to be one outlier.\n"
    "estimated SNR -- the ratio between those two, in dB. A diagnostic figure "
    "for setting gain, not a decode confidence: a strong interferer raises it "
    "just as a wanted signal does.\n"
    "clipping -- the percentage of samples that reached the digitizer's rail.\n"
    "headroom -- how far the strongest sample stayed below full scale.\n"
    "\n"
    "The colour is the verdict. Green is healthy. Amber is low headroom: "
    "clipping has begun, or less than 3 dB is left. Red is clipping: 0.1% or "
    "more of the samples clipped, or under 1 dB of headroom.\n"
    "\n"
    "Red matters more than it looks. A clipped sample is not merely loud, it "
    "is wrong, and it spreads energy across the whole spectrum -- so the "
    "spectrum, the waterfall and every decode above it are reading distorted "
    "data. Lower the gain in Settings until the line is green, then judge the "
    "signal. Gain starts at the supported step nearest 30 dB rather than the "
    "tuner's maximum for exactly this reason."
},
{
    "GSM channel scan",
    "Channel power scan chart",
    "Where: the bottom left of the Decode tab's GSM view, and the full-screen "
    "scan reached from Calibration.\n"
    "\n"
    "What it plots: one bar per GSM 900 downlink channel, ARFCN 1 to 124 left "
    "to right, 200 kHz apart. Bar height is that channel's average power in "
    "dBFS. The receiver only sees 2 MHz at a time, so the whole band cannot be "
    "measured at once: a scan is a sweep of retuning steps, and the header "
    "counts them off. A channel with no bar was never measured.\n"
    "\n"
    "Green bars are BCCH carriers. Green means that at that step the FCCH tone "
    "was found on the channel with a coherence of 0.85 or better. Coherence "
    "measures how tone-like the channel looked -- near 1 for a pure tone, near "
    "0 for modulation or noise -- so a green bar is a channel carrying the "
    "continuously transmitted reference that calibration and SCH decoding both "
    "need. Grey bars have power but no reference tone.\n"
    "\n"
    "Hover a bar for its ARFCN, frequency and power. Click one to select it: "
    "in the GSM view that retunes and opens the burst charts on it; in the "
    "calibration scan it fills in the channel and starts measuring.\n"
    "\n"
    "Choose the strongest green bar, not the tallest bar. A traffic channel "
    "can be far louder than the BCCH beside it and is useless here -- it "
    "carries no reference tone, so there is nothing to lock to."
},
{
    "GSM burst charts",
    "Burst analysis charts",
    "Where: the Decode tab's GSM view with a channel selected and the View "
    "button set to Burst. All three show the inside of one Synchronisation "
    "Channel (SCH) decode, so they stay empty until a burst is found.\n"
    "\n"
    "Timing Correlation Landscape. The differentially demodulated burst slid "
    "against the 64-bit extended training sequence the SCH carries, one "
    "correlation value per candidate offset. One sharp peak means the burst "
    "was located confidently and everything downstream starts from the right "
    "sample. A flat noisy landscape, or several peaks of similar height, means "
    "it was not, and any decode reported above it deserves no trust. The peak "
    "height is the match figure printed in the SCH line.\n"
    "\n"
    "Soft Symbol Magnitudes. One bar per demodulated bit: how far that bit "
    "landed from the decision boundary, measured against the 90th-percentile "
    "bit of the same burst. So 1.0 is a typical strong bit here and an outlier "
    "reaches past it -- the scale is the burst's own. That is deliberate: the "
    "raw magnitudes follow the signal level, tens of thousands on a strong "
    "capture and thousandths on a weak live channel, and an absolute axis left "
    "the panel empty while the burst decoded perfectly well. The question this "
    "chart answers is which bits were weak within one burst, not how loud the "
    "channel was. A run of short bars marks where the burst faded or was "
    "interfered with -- the stretch the Viterbi decoder had to bridge, and "
    "where a parity failure comes from when one comes.\n"
    "\n"
    "Differential Phase Trajectory. The accumulated phase of the burst, symbol "
    "by symbol. GMSK advances the phase by about +/-pi/2 per symbol, so a "
    "clean burst is a staircase of even steps. A steady slope running through "
    "the whole burst is a residual frequency offset -- the carrier is not "
    "where the receiver believes it is, which calibration or the FnCFO toggle "
    "addresses. A trajectory that wanders or doubles back is noise winning.\n"
    "\n"
    "Above the charts, the SCH line reports what was recovered: the BSIC (NCC "
    "and BCC), the frame number as T1/T2/T3, and the training-sequence match. "
    "[T1 JUMPED] means two consecutive decodes disagree by more than one T1, "
    "which cannot happen seconds apart -- the decode is wrong even though its "
    "parity passed.\n"
    "\n"
    "The Filter, FnCFO and Trellis buttons switch decode refinements on and "
    "off, and these three charts are where their effect shows."
},
{
    "GSM constellation",
    "SCH symbol constellation",
    "Where: the bottom right of the GSM view. This one is a constellation, "
    "unlike the Scope tab's I/Q scatter: every point is a symbol the decoder "
    "made a decision on, and the colour is that decision.\n"
    "\n"
    "Two buttons change what is plotted.\n"
    "\n"
    "Derot off plots the differential product conj(previous) x current, which "
    "is what the bit decision is actually taken from: two clusters, left and "
    "right. Derot on plots the derotated symbol sample instead -- a BPSK-like "
    "pair of clusters -- coloured by the reconstructed channel bit.\n"
    "\n"
    "Amp on keeps each point's amplitude, scaled so that the 90th-percentile "
    "magnitude reaches the edge of the box, with genuine outliers pinned at "
    "the rim rather than drawn outside it. (Scaling by the largest sample "
    "instead would put that one point on the edge every frame by construction "
    "and squeeze everything else inward.) Amp off projects every point onto "
    "the unit circle, leaving only its angle.\n"
    "\n"
    "Reading it: two tight, well-separated clusters is a clean burst. A smear "
    "filling the gap between them is noise. A cloud rotated off the axes is a "
    "frequency or phase offset. Points spread evenly around the circle mean "
    "there was no usable burst at all, whatever the decode line says."
},
{
    "ADS-B log",
    "Decoded message log",
    "Where: the Decode tab's ADS-B view. Not a chart -- a table of decoded "
    "messages, newest first. It fills only when the receiver is near 1090 MHz "
    "at 2 MS/s or better; when it is not, the view says so and offers a "
    "Retune button.\n"
    "\n"
    "Columns: TIME is the local clock when the frame decoded; ICAO is the "
    "transmitting aircraft's 24-bit address, the key that groups its messages; "
    "TYPE is the kind of message; DECODED MESSAGE is the fields recovered from "
    "it; RAW is the frame's own bytes in hex. The newest rows are highlighted "
    "until fresher ones arrive.\n"
    "\n"
    "TYPE is ID for an identification frame carrying a callsign, POS for an "
    "airborne position, VEL for a velocity, and MSG for a frame whose downlink "
    "format and type code are shown but whose contents this decoder does not "
    "parse.\n"
    "\n"
    "A position needs two frames. Aircraft alternate between even and odd CPR "
    "encodings, and one of each, close together in time, is required before a "
    "latitude and longitude can be resolved unambiguously -- so a POS row may "
    "read 'alt 37000 ft (awaiting even frame)' until its partner arrives. The "
    "counters in the header keep the two apart: frames decoded, and positions "
    "resolved.\n"
    "\n"
    "Every row here passed its 24-bit CRC. There is no error correction: a "
    "frame with a bad remainder is dropped rather than repaired, so the log "
    "shows what arrived intact, not a best guess at what was sent. What it "
    "dropped is what the funnel line and the next topic's charts are for."
},
{
    "ADS-B analysis",
    "ADS-B frame analysis charts",
    "Where: the Decode tab's ADS-B view with View: Analysis. Three charts of one "
    "Mode S frame, a bit-decision scatter, and the log kept beside them.\n"
    "\n"
    "Which frame: the most recent attempt -- a preamble that was accepted and "
    "produced a DF17/18-shaped frame -- whether or not its CRC passed. A frame "
    "that failed is the one worth looking at, so it is not the one thrown away. "
    "The caption says which, and a failed frame shows no ICAO: those bits are "
    "not an address, they are noise that landed in the address field. Hold last "
    "good pins the last frame that passed, to compare against.\n"
    "\n"
    "The funnel line above the charts is the quickest answer to a log that "
    "stays empty. Preambles accepted, then how many were squitter-shaped, then "
    "how many failed their CRC, then how many decoded -- totals first, then the "
    "latest block. No preambles at all is a silent band, an antenna, or the "
    "wrong tuning. Preambles with no decodes behind them is a signal you are "
    "receiving badly, and the line turns amber to say so.\n"
    "\n"
    "Preamble Score Landscape. The preamble match score at each sample offset "
    "either side of the frame: the mean of the four pulse samples over the mean "
    "of the twelve quiet ones. One peak standing clear of the field is a "
    "confident lock. A peak with a near-equal runner-up beside it means the "
    "frame could have been sliced half a microsecond off, and every bit after "
    "it inherits that.\n"
    "\n"
    "Pulse-Position Bit Confidence. One bar per bit: how far its two half "
    "intervals stood apart, over their sum. A Mode S bit is energy in the first "
    "half for a one and the second half for a zero, so 1.0 is a bit with all "
    "its energy on one side and 0 is a coin toss. Tall even bars are a clean "
    "frame; a dip partway through is where the aircraft's signal faded or "
    "another transmitter sat on top of it, and that is where a CRC failure "
    "comes from.\n"
    "\n"
    "Frame Magnitude Envelope. The frame as the receiver saw it, divided by the "
    "preamble's own level, so 1.0 is a full pulse whatever the gain was. The "
    "first sixteen samples are the preamble -- pulses at 0, 2, 7 and 9 -- and "
    "the rest is two samples per bit. Pulses that do not reach 1.0 are a weak "
    "frame; a flat top across many samples is clipping, and clipping breaks the "
    "comparison the bit decisions are made on.\n"
    "\n"
    "Bit decisions. Every bit as a point: sideways is its signed margin, so "
    "left is a zero and right is a one, and up is how much energy the bit "
    "carried relative to the preamble. Two tight clusters at the left and right "
    "edges is a clean frame. Points crowding the centre line decoded by luck. "
    "Vertical spread means the amplitude moved during the frame. It is not a "
    "constellation: Mode S is pulse-position, with no modulated symbols and no "
    "phase in it.\n"
    "\n"
    "Every frame in the log passed its CRC, so the log alone cannot show you a "
    "marginal signal. These charts can: a frame whose bits sit near the centre "
    "of the scatter decoded this time and will not next time."
},
{
    "Calibration",
    "Calibration and drift",
    "Where: the Calibration button, or c, from any view.\n"
    "\n"
    "The problem it solves: the receiver's crystal is off by a few parts per "
    "million and its error moves with temperature, so every frequency it "
    "reports is off in proportion -- about 1 kHz at 1090 MHz for each 1 PPM. "
    "Calibration measures that error against a transmitter whose frequency is "
    "known exactly, and suggests the correction that cancels it.\n"
    "\n"
    "The chart is the waterfall again, zoomed to +/-250 kHz around the channel "
    "being measured and labelled by ARFCN. Two vertical markers cross it: "
    "green 'expected' at the frequency the chosen ARFCN must be on, and amber "
    "'measured' at the carrier actually found. The gap between them is the "
    "receiver's error, and the lines above the chart put a number on it in kHz "
    "and in PPM.\n"
    "\n"
    "What gets measured: the FCCH tone when one is found -- a pure tone 67.708 "
    "kHz above the BCCH carrier, precise and unbiased -- and otherwise the "
    "power centroid of the channel as a fallback. The status line names the "
    "source, and only an FCCH-backed lock is trusted enough to turn the health "
    "circle green.\n"
    "\n"
    "Apply PPM unlocks only once the correction has settled: the standard "
    "error of the recent residuals has to fall below a threshold, over "
    "residuals that all came from the same source. Wait for it rather than "
    "working around it -- centroid residuals mixed in with FCCH residuals are "
    "exactly the mistake that gate exists to catch.\n"
    "\n"
    "The GSM cal circle at the top right of every screen: grey means no "
    "FCCH-backed calibration, green means calibrated and verified, amber means "
    "a re-check is running (it retunes the receiver for a few seconds, so the "
    "live view pauses), and red means the correction has drifted more than 2 "
    "PPM and should be taken again. The periodic re-check stays off until "
    "'Auto GSM drift check' is ticked in Settings."
}
};

/*
 * One derivation of the overlay's rectangles, used by both the input and the
 * draw pass -- the same reason chrome_layout.h exists. The topic list is a
 * fixed-height column on the left; the body takes the rest and scrolls.
 */
struct help_layout {
    Rectangle panel;
    Rectangle entry[HELP_TOPIC_COUNT];
    Rectangle body;
    Rectangle close;
};

static struct help_layout help_layout_now(void) {
    struct help_layout l;
    float width = (float)GetScreenWidth();
    float height = (float)GetScreenHeight();
    const float entry_h = 28.0f;
    const float entry_gap = 3.0f;
    float sidebar_w = 196.0f;
    float content_y;

    l.panel = (Rectangle){ 34.0f, 34.0f, width - 68.0f, height - 68.0f };
    if (l.panel.width < 320.0f)
        l.panel.width = 320.0f;
    if (l.panel.height < 220.0f)
        l.panel.height = 220.0f;

    l.close = (Rectangle){ l.panel.x + l.panel.width - 104.0f,
                           l.panel.y + 16.0f, 84.0f, 30.0f };
    content_y = l.panel.y + 86.0f;
    for (int i = 0; i < HELP_TOPIC_COUNT; i++)
        l.entry[i] = (Rectangle){ l.panel.x + 20.0f,
                                  content_y + (float)i * (entry_h + entry_gap),
                                  sidebar_w, entry_h };
    l.body = (Rectangle){ l.panel.x + 20.0f + sidebar_w + 26.0f, content_y,
                          l.panel.width - sidebar_w - 72.0f,
                          l.panel.height - 86.0f - 44.0f };
    if (l.body.width < 120.0f)
        l.body.width = 120.0f;
    if (l.body.height < 60.0f)
        l.body.height = 60.0f;
    return l;
}

/* The topic that answers the question the current screen raises. */
static int help_topic_for_screen(const struct app *app) {
    if (app->calibration_open)
        return app->scan_open ? HELP_SCAN : HELP_CALIBRATION;
    if (app->tab == TAB_DECODE) {
        if (app->decode == DECODE_ADSB)
            return app->adsb.analysis_mode ? HELP_ADSB_ANALYSIS : HELP_ADSB;
        if (app->scan_selected_arfcn > 0 && app->gsm_analysis_mode)
            return HELP_BURST;
        return HELP_SCAN;
    }
    if (app->view == VIEW_SURVEY)
        return HELP_SURVEY;
    if (app->view == VIEW_SPECTRUM)
        return HELP_SPECTRUM;
    if (app->view == VIEW_SCATTER)
        return HELP_SCATTER;
    if (app->view == VIEW_WATERFALL)
        return HELP_WATERFALL;
    return HELP_MAGNITUDE;
}

void open_help(struct app *app) {
    app->help.topic = help_topic_for_screen(app);
    app->help.scroll = 0.0f;
    app->help.content_height = 0.0f;
    app->help.open = 1;
}

void close_help(struct app *app) {
    app->help.open = 0;
}

static void help_select(struct app *app, int topic) {
    if (topic < 0)
        topic = HELP_TOPIC_COUNT - 1;
    if (topic >= HELP_TOPIC_COUNT)
        topic = 0;
    app->help.topic = topic;
    app->help.scroll = 0.0f;
}

void handle_help_input(struct app *app) {
    struct help_layout l = help_layout_now();
    const int body_size = 17;
    const int body_gap = 6;
    float limit;

    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_H) ||
        clicked(l.close)) {
        close_help(app);
        return;
    }
    for (int i = 0; i < HELP_TOPIC_COUNT; i++)
        if (clicked(l.entry[i]))
            help_select(app, i);
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT))
        help_select(app, app->help.topic + 1);
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT))
        help_select(app, app->help.topic - 1);

    app->help.scroll -= GetMouseWheelMove() * 48.0f;
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN))
        app->help.scroll += 40.0f;
    if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP))
        app->help.scroll -= 40.0f;
    if (IsKeyPressed(KEY_PAGE_DOWN))
        app->help.scroll += l.body.height - 40.0f;
    if (IsKeyPressed(KEY_PAGE_UP))
        app->help.scroll -= l.body.height - 40.0f;

    /* Measure the wrapped body at this window width so the scroll can be
       clamped before anything is drawn with it. */
    app->help.content_height = sdrgui_text_block(
        l.body, help_pages[app->help.topic].body, body_size, body_gap,
        BLANK, 0);
    limit = app->help.content_height - l.body.height;
    if (limit < 0.0f)
        limit = 0.0f;
    if (app->help.scroll > limit)
        app->help.scroll = limit;
    if (app->help.scroll < 0.0f)
        app->help.scroll = 0.0f;
}

void draw_help(const struct app *app) {
    struct help_layout l = help_layout_now();
    const struct help_page *page = &help_pages[app->help.topic];
    const int body_size = 17;
    const int body_gap = 6;
    Rectangle body = l.body;

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                  (Color){ 0, 0, 0, 195 });
    DrawRectangleRec(l.panel, (Color){ 12, 20, 29, 255 });
    DrawRectangleLinesEx(l.panel, 2.0f, (Color){ 111, 139, 154, 255 });

    DrawText("Reading the charts", (int)l.panel.x + 20, (int)l.panel.y + 18,
             24, (Color){ 235, 242, 246, 255 });
    draw_button(l.close, "Close", 0);

    for (int i = 0; i < HELP_TOPIC_COUNT; i++)
        draw_button(l.entry[i], help_pages[i].entry, i == app->help.topic);

    DrawText(page->title, (int)body.x, (int)body.y - 30, 20,
             (Color){ 149, 205, 232, 255 });
    DrawLine((int)body.x, (int)body.y - 6, (int)(body.x + body.width),
             (int)body.y - 6, (Color){ 54, 72, 86, 255 });

    BeginScissorMode((int)body.x, (int)body.y, (int)body.width,
                     (int)body.height);
    body.y -= app->help.scroll;
    sdrgui_text_block(body, page->body, body_size, body_gap,
                      (Color){ 199, 214, 224, 255 }, 1);
    EndScissorMode();

    /* A scrollbar only when there is something below the fold. */
    if (app->help.content_height > l.body.height) {
        float track_h = l.body.height;
        float thumb_h = track_h * (l.body.height / app->help.content_height);
        float travel = track_h - thumb_h;
        float progress = app->help.scroll /
                         (app->help.content_height - l.body.height);
        float x = l.body.x + l.body.width + 8.0f;
        if (thumb_h < 24.0f)
            thumb_h = 24.0f;
        DrawRectangle((int)x, (int)l.body.y, 4, (int)track_h,
                      (Color){ 34, 47, 58, 255 });
        DrawRectangle((int)x, (int)(l.body.y + travel * progress), 4,
                      (int)thumb_h, (Color){ 111, 139, 154, 255 });
    }

    DrawText("Up/Down or wheel scroll   Left/Right topic   h or Esc close",
             (int)l.body.x, (int)(l.panel.y + l.panel.height - 32), 16,
             (Color){ 151, 174, 188, 255 });
}
