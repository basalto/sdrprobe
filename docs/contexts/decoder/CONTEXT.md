# Decoder

Turns an acquired sample stream into decoded messages. This context begins where
the Probe context ends: at the recovery of bits from the signal. It owns the
vocabulary of demodulation and message parsing across technologies — Mode S /
ADS-B aircraft messages and the GSM Synchronisation Channel — and says nothing
about how samples were acquired or how signal quality is judged.

## Language

**Mode S frame**:
One complete Mode S transmission recovered from the sample stream: a preamble
followed by either 56 or 112 data bits.
_Avoid_: Packet, sample block, burst

**Preamble**:
The fixed pulse pattern at the start of a Mode S frame that marks where a frame
begins and anchors bit timing.
_Avoid_: Sync word, header, sample marker

**Pulse-position bit**:
One Mode S data bit recovered by comparing signal magnitude in the first versus
second half of a fixed interval; energy-first is a one, energy-second is a zero.
_Avoid_: Symbol, I/Q pair, sample

**Downlink format**:
The 5-bit type code at the start of a Mode S frame that determines its length
and how its bits are interpreted.
_Avoid_: Message type, opcode, DF number

**Extended squitter**:
The 112-bit downlink-format-17/18 Mode S frame that carries ADS-B data an
aircraft broadcasts unprompted.
_Avoid_: ADS-B packet, long frame

**Message CRC**:
The 24-bit parity trailing every Mode S frame, checked to accept or reject a
frame; a nonzero remainder means the frame is dropped.
_Avoid_: Checksum, hash, FCS

**ICAO address**:
The 24-bit identifier of the transmitting aircraft, used as the key that groups
messages from the same aircraft.
_Avoid_: Tail number, callsign, aircraft id

**Callsign**:
The flight identification string carried in an identification extended squitter.
_Avoid_: Registration, tail number, ICAO address

**Barometric altitude**:
The pressure altitude decoded from an airborne-position extended squitter.
_Avoid_: Height, elevation, GPS altitude

**Ground velocity**:
The aircraft's speed and heading over the ground decoded from a velocity
extended squitter.
_Avoid_: Airspeed, groundspeed vector

**CPR position**:
A latitude/longitude fix decoded from the Compact Position Reporting fields of
airborne-position frames.
_Avoid_: GPS fix, coordinate, waypoint

**Even/odd frame**:
The two CPR encodings an aircraft alternates between; one of each, close in
time, is required to decode an unambiguous global position.
_Avoid_: Frame parity, phase

**Position pairing cache**:
The minimal per-ICAO memory of the most recent even and odd frame, kept only
long enough to decode a position; it is not a tracked aircraft record.
_Avoid_: Aircraft table, track, state store

**Decoded message**:
A validated Mode S frame parsed into its meaningful fields (ICAO address,
downlink format, and any decoded callsign / altitude / velocity / position).
_Avoid_: Signal activity, detection, raw frame

**Message log**:
The bounded, newest-first list of recently decoded messages shown to the user;
older entries fall off as new ones arrive.
_Avoid_: Queue, history buffer, aircraft table

**Frame trace**:
The intermediate demodulation data kept for one recovered Mode S frame so the
decode can be seen: where the frame was found, how far each pulse-position bit
stood from its decision boundary, and the magnitudes behind both.
_Avoid_: Capture, recording, snapshot

**Preamble score**:
How well the magnitude pattern at one sample offset matches the four-pulse Mode
S preamble; the evidence behind accepting a frame, expressed as a number.
_Avoid_: Correlation (the GSM training-sequence term), coherence (a Probe term
for tone-likeness)

**Decision margin**:
How far a pulse-position bit's two half-interval magnitudes stood apart,
relative to their sum; the confidence behind one hard bit decision.
_Avoid_: Soft symbol, SNR, amplitude

**Decode funnel**:
The counts of what each stage of demodulation accepted and passed on —
preambles, extended-squitter-shaped frames, CRC failures, decoded messages —
reported so that an empty message log distinguishes a silent band from frames
that are arriving and failing.
_Avoid_: Yield, error rate, statistics

## GSM synchronisation

**SCH**:
The GSM Synchronisation Channel: a modulated, coded burst on the BCCH carrier
that a receiver decodes to obtain the cell's identity code and timing.
_Avoid_: Sync burst, FCCH

**Extended training sequence**:
The fixed 64-bit pattern in the middle of the SCH burst used to locate the burst
and anchor bit timing before decoding.
_Avoid_: Preamble, midamble, pilot

**BSIC**:
The 6-bit Base Station Identity Code carried in the SCH, made of the Network
Colour Code and the Base station Colour Code.
_Avoid_: Cell id, station code

**NCC**:
The 3-bit Network Colour Code, the upper half of the BSIC, distinguishing
neighbouring operators' cells.
_Avoid_: Operator id

**BCC**:
The 3-bit Base station Colour Code, the lower half of the BSIC, distinguishing a
cell from its same-frequency neighbours.
_Avoid_: Sector id

**Reduced frame number**:
The compressed TDMA frame number (T1, T2, T3) carried in the SCH, from which the
full frame number is reconstructed.
_Avoid_: Timestamp, frame counter

**Frame-number continuity check**:
The single comparison kept between consecutive SCH decodes — that T1 has not
moved by more than one, which it cannot over a few seconds. It flags a decode
that cannot be right; it never substitutes a value of its own.
_Avoid_: Lock, tracker, clock, scheduler

**Burst analysis chart**:
A time-series visualization of intermediate decoder metrics (correlation, soft bits, phase) for a single recovered burst.
_Avoid_: Oscilloscope, logic analyzer

**Timing correlation landscape**:
A plot showing the training-sequence match score across a window of samples, revealing where the decoder locked onto the burst.
_Avoid_: Sync graph, peak finder

**Soft symbol magnitude**:
The absolute confidence value of a demodulated symbol before hard bit decisions or Viterbi decoding are applied.
_Avoid_: Power, amplitude, SNR

**Differential phase trajectory**:
The accumulated symbol-to-symbol phase change across a burst, illustrating the underlying GMSK modulation.
_Avoid_: FM waveform, phase drift

**Primary synchronisation signal**:
The Zadoff-Chu sequence on the central 62 subcarriers of the last symbol of an
LTE half-frame, which a receiver finds first; it carries N_ID_2 and fixes the
symbol timing.
_Avoid_: PSS burst, LTE preamble, pilot

**Secondary synchronisation signal**:
The plus-or-minus-one sequence in the symbol before it, an interleaved pair of
length-31 m-sequences; it carries N_ID_1, says which half-frame this is, and so
fixes the frame boundary.
_Avoid_: SSS burst, sync word, training sequence

**Physical cell identity**:
The number 3 * N_ID_1 + N_ID_2, from 0 to 503, that the two synchronisation
signals together name. It identifies a cell to the physical layer and seeds
every scrambling sequence the cell uses.
_Avoid_: Cell ID, PCI number, base station identity

**Cell search**:
Finding the two synchronisation signals in a sample block and reading a cell
identity, a frame boundary, a cyclic-prefix length and a frequency offset out
of them. It decodes no message.
_Avoid_: Cell scan, acquisition, LTE lock

**Cyclic prefix**:
The copy of a symbol's tail placed in front of it, nine or ten samples under
the normal arrangement and thirty-two under the extended one. Which of the two
a cell uses is read from where the secondary sequence sits.
_Avoid_: Guard interval, header, preamble

**Cell-specific reference signal**:
The known symbols a cell transmits on every sixth subcarrier of certain
symbols, from which the channel is measured before anything is demodulated.
_Avoid_: Pilot tone, training sequence, sync signal

**Master Information Block**:
The 24-bit message the broadcast channel carries: downlink bandwidth, PHICH
configuration, and the eight high bits of the system frame number. The lowest
two bits of that frame number are not in the message -- they are which quarter
of the 40 ms period the transmission occupied.
_Avoid_: MIB packet, System Information, broadcast block

**Antenna-port count**:
Whether a cell transmits on one, two or four ports, recovered from which of
three masks the broadcast channel's parity fits. It changes how the resource
elements are combined and is not known before the message decodes.
_Avoid_: MIMO order, antenna number, transmit mode

**Space-frequency block code**:
The Alamouti pairing across two neighbouring resource elements by which two or
four antenna ports carry the broadcast channel; undoing it recovers both
symbols with the interference between them cancelled.
_Avoid_: MIMO decode, diversity combining, beamforming
