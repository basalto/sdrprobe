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

**Frame-number lock**:
The minimal running memory of the SCH frame number, used to vote a constant T1
and predict the next burst's number; it is not a scheduler or a clock.
_Avoid_: Clock, timer, scheduler

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
