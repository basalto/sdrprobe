# Dump1090 Architecture

Dump1090 is a single-binary, two-thread Mode S / ADS-B decoder. It reads 2 MHz
8-bit I/Q samples (from an RTLSDR device, a file, or stdin), demodulates Mode S
messages, tracks aircraft, and serves results over a terminal UI, an HTTP/JSON
interface, and three TCP ports. All logic lives in `dump1090.c`; `anet.c`
provides small non-blocking TCP helpers (from Redis).

## Mode S / ADS-B message primer

This section summarizes the parts of the Mode S downlink format that
Dump1090 implements, as decoded by `decodeModesMessage()`.

### Physical layer

- Carrier: **1090 MHz**, modulation is **PPM** (pulse position modulation).
- Sampling: **2 MS/s**, 8-bit unsigned I/Q — one sample = 0.5 µs.
- Every message starts with an **8 µs preamble**: four 0.5 µs pulses at
  0, 1.0, 3.5 and 4.5 µs. At 2 MS/s the preamble occupies 16 samples:

```
time (µs):  0   0.5  1.0  1.5  ...  3.5  4.0  4.5  5.0
sample:     0   1    2    3        7    8    9    10
            ┌──┐     ┌──┐        ┌──┐      ┌──┐
            │  │     │  │        │  │      │  │
         ───┘  └─────┘  └────────┘  └──────┘  └──────
            high low   high low    high low  high low
```

`detectModeS()` finds this pattern with ratio tests on the first 10 samples
(spikes at indices 0, 2, 7, 9; low energy between and after them).

- After the preamble, each **bit is Manchester-encoded** over 1 µs (two
  samples): high-then-low = 1, low-then-high = 0. Equal magnitudes mean a
  demodulation error.

### Message lengths and Downlink Formats

Messages are either **56 bits** (short) or **112 bits** (long). The first
5 bits of every message are the **Downlink Format (DF)** number, which
determines the length (`modesMessageLenByType()`):

| Length  | DF |
|---------|-----|
| 56 bit  | 0, 4, 5, 11 (and everything else) |
| 112 bit | 16, 17, 18, 19, 20, 21 |

```
Long message (112 bits, e.g. DF17):

bit:  0     4 5      7 8              31 32             87 88        111
      ┌───────┬────────┬──────────────────┬───────────────┬────────────┐
      │  DF   │  CA    │  ICAO address    │   ME field    │   CRC/PI   │
      │ 5 bit │ 3 bit  │     24 bit       │    56 bit     │   24 bit   │
      └───────┴────────┴──────────────────┴───────────────┴────────────┘

Short message (56 bits, e.g. DF4):

bit:  0     4 5      7 8                                 32           55
      ┌───────┬────────┬──────────────────────────────────┬────────────┐
      │  DF   │  FS    │   altitude / identity fields     │  CRC xor   │
      │ 5 bit │ 3 bit  │                                  │  ICAO (AP) │
      └───────┴────────┴──────────────────────────────────┴────────────┘
```

### CRC: two flavors

The last 24 bits are a CRC over the message, but with two different
meanings:

- **DF11/17/18**: plain CRC. Syndrome == 0 means valid. This is what the
  syndrome-table bit-error correction (`fixBitErrors()`) operates on —
  up to 1 flipped bit (2 with `--aggressive`) can be located and fixed.
- **DF0/4/5/16/20/21**: the CRC is XORed with the aircraft's ICAO address
  (the "AP" field). `bruteForceAP()` recovers it by trying recently seen
  ICAO addresses from the `icao_cache` ring buffer: if XORing the syndrome
  with a cached address yields zero, the message is valid and the address
  is known.

DF11 is special: a small non-zero CRC residual (< 80) with a recently seen
ICAO is accepted as an Interrogator Identifier (IID).

### DF17/DF18 extended squitter (ADS-B)

The 56-bit **ME field** carries ADS-B data; its first 5 bits are the
**type code** (`metype`), selecting the payload layout:

| metype | Payload | Decoded into |
|--------|---------|--------------|
| 1–4   | Aircraft identification | 8-char callsign (6-bit AIS charset) |
| 5–8   | Surface position | ground speed (movement), ground track, CPR lat/lon |
| 9–18  | Airborne position | altitude (AC12), CPR lat/lon |
| 19    | Airborne velocity | E/W + N/S speed → velocity & heading, or heading directly (subtype 3/4) |

**CPR position encoding**: positions are sent as 17-bit raw lat/lon in
alternating **even** and **odd** frames (`fflag` bit). Neither frame alone
is sufficient — `decodeCPR()` needs an even/odd pair received within
10 seconds to resolve the position globally:

```
ME field, airborne position (metype 9-18):

bit:  32     36 37  38 39  40 41    42 ...           87
      ┌────────┬─────┬────┬────┬────┬──────────────────┐
      │ metype │ SS  │ NIC│ ALT│ T F│  CPR lat/lon     │
      │ 5 bit  │2 bit│    │12b │ 1+1│  (17+17 bit)     │
      └────────┴─────┴────┴────┴────┴──────────────────┘
                               T=tflag, F=fflag (even/odd)
```

Airborne CPR covers 360° and is self-contained; **surface CPR** (metype
5–8) covers only 90° of longitude and needs a reference position within
45 NM to disambiguate — which is why Dump1090 maintains the running
`ref_lat/ref_lon` average of airborne positions.

### The squawk (identity) field

In DF5/DF21 the 13-bit identity field uses **Gillham code**: bits are
interleaved as `C1-A1-C2-A2-C4-A4-0-B1-D1-B2-D2-B4-D4` — four octal digits
A/B/C/D, decoded by `decodeModesMessage()` into a base-10 number that
*looks* like the octal squawk (e.g. squawk 7500 = hijack is stored as the
integer 7500).



## High-level data flow

```
                    ┌────────────────────────────────────────────────────┐
                    │                     main thread                    │
                    │                                                    │
 I/Q sources        │   computeMagnitudeVector()                         │
 ┌─────────────┐    │     I/Q bytes -> magnitude (0..255) via maglut     │
 │ RTLSDR      │    │             │                                      │
 │ (librtlsdr) │    │             ▼                                      │
 └──────┬──────┘    │   detectModeS()                                    │
        │           │     preamble detect -> bit decode ->               │
 ┌──────┴──────┐    │     phase-correction retry -> CRC check/fix        │
 │ --ifile     │    │             │                                      │
 │ file/stdin  │    │             ▼                                      │
 └──────┬──────┘    │   decodeModesMessage()  (DF fields, CRC syndrome,  │
        │           │                        bit-error fixing, AP brute  │
        │           │                        force, DF17/18 extraction)  │
        │           │             │                                      │
        │           │             ▼                                      │
        │           │   useModesMessage() ───┬───────────────────┐       │
        │           │                        │                   │       │
        │           │                        ▼                   ▼       │
        │           │            interactiveReceiveData()   modesSend*   │
        │           │            (aircraft table update,    (raw TCP     │
        │           │             CPR position decode,       out, SBS    │
        │           │             reference position avg)    out)        │
        │           └────────────────────────────────────────────────────┘
        │                                    ▲
        │            Modes.data (256 KB buffer + overlap),
        │            data_mutex / data_cond, data_ready flag
        │                                    │
        ▼                                    │
 ┌────────────────────────────────────────────────────┐
 │              reader thread                         │
 │  rtlsdrCallback()  OR  readDataFromFile()          │
 │  fills Modes.data, sets data_ready, signals cond   │
 └────────────────────────────────────────────────────┘
```

## Threads

There are exactly two threads, created in `main()`:

1. **Reader thread** (`readerThreadEntryPoint`)
   - With a device: runs `rtlsdr_read_async()`, which invokes
     `rtlsdrCallback()` per USB block.
   - With `--ifile`: runs `readDataFromFile()`, a blocking read loop
     (`--loop` seeks back to start; `--interactive` throttles with `usleep`
     to approximate the real 2 MHz rate).
   - Both write into the shared buffer `Modes.data` (256 KB), preserving the
     tail of the previous block so messages split across block boundaries
     still decode. Sets `Modes.data_ready` and signals `data_cond`.

2. **Main thread**
   - Waits on `data_cond`, calls `computeMagnitudeVector()` (I/Q →
     magnitude via the precomputed `maglut` sqrt(I²+Q²) lookup), clears
     `data_ready`, *unlocks the mutex* so acquisition continues, then runs
     `detectModeS()` and `backgroundTasks()` outside the lock.
   - In `--net-only` mode there is no reader thread at all; the main loop
     is just `backgroundTasks()` + `modesWaitReadableClients()` (a plain
     `select()` over listeners and client fds).

## Signal decoding pipeline (main thread, per buffer)

`detectModeS()` scans the magnitude vector sample by sample:

1. **Preamble detection** — cheap ratio tests on the 10 samples of the
   8 µs Mode S preamble (spikes at 0, 2, 7, 9; low energy between and
   after). Failures `continue` immediately.
2. **Bit demodulation** — for each of 112 bits, compare the two 0.5 µs
   samples (Manchester-style: first > second → 1, else 0; equal magnitude
   = error marker).
3. **Noise filter** — average |low−high| delta across the message must
   clear a threshold, otherwise discard.
4. **Phase-correction retry** — if decoding failed, rewind one sample,
   run `applyPhaseCorrection()` (estimates early/late sampling from
   preamble energy leakage and rescales samples), and demodulate again.
5. **CRC + error fixing** (`decodeModesMessage()`)
   - `modesChecksum()` computes the syndrome; 0 means valid.
   - DF11/17/18 with bad CRC: `fixBitErrors()` looks the syndrome up in a
     precomputed table (`modesInitErrorInfo()` at startup) to fix 1 bit,
     or 2 bits with `--aggressive`. Disabled by `--no-fix`.
   - DF0/4/5/16/20/21 (checksum XORed with ICAO address):
     `bruteForceAP()` tries recently seen ICAO addresses from the
     `icao_cache` ring buffer to recover the address.
6. **Dispatch** (`useModesMessage()`)
   - CRC-valid messages (unless `--no-crc-check`) fan out to:
     - `interactiveReceiveData()` — aircraft tracking (below), when
       `--interactive` or anyone is consuming HTTP/SBS.
     - `displayModesMessage()` — plain stdout dump (non-interactive).
     - `modesSendRawOutput()` — `*HEX...;\n` to all raw-output clients.

## Aircraft tracking & CPR position decoding

`interactiveReceiveData()` maintains a singly linked list `Modes.aircrafts`
of `struct aircraft` keyed by ICAO address. Per message type it updates:

- DF0/4/20 → altitude.
- DF17/18 metype 1–4 → flight callsign.
- DF17/18 metype 9–18 → airborne position: stores raw even/odd CPR frames;
  when an even/odd pair is ≤10 s apart, `decodeCPR()` (global CPR) yields
  lat/lon. Each newly decoded position incrementally updates the global
  reference position `Modes.ref_lat/ref_lon` (running average).
- DF17/18 metype 5–8 → surface position: `decodeCPRSurface()` uses the
  reference position to resolve CPR ambiguity (90° grid). Skipped until at
  least one airborne position has been decoded.
- DF17/18 metype 19 → velocity/heading.

Stale aircraft (no message for `--interactive-ttl` seconds, default 60) are
pruned by `interactiveRemoveStaleAircrafts()` from `backgroundTasks()`.

## Networking (all single-threaded, non-blocking)

Enabled by `--net`; four listening sockets (`modesInitNet()`), defined in
`modesNetServices[]`:

| Port  | Service | Direction | Handler |
|-------|---------|-----------|---------|
| 30002 | Raw output   | server → clients | `modesSendRawOutput()` (`*HEX;` lines) |
| 30001 | Raw input    | client → server  | `decodeHexMessage()` — hex frames fed back into `decodeModesMessage()`/`useModesMessage()`, so they re-broadcast to 30002 like local traffic |
| 30003 | SBS-1 output | server → clients | `modesSendSBSOutput()` (BaseStation `MSG,...` CSV) |
| 8080  | HTTP         | request/response | `handleHTTPRequest()` — `/` serves `gmap.html`, `/data.json` serves `aircraftsToJson()` |

`backgroundTasks()` (called a few times per second from the main loop, and
continuously in `--net-only` mode) does all network housekeeping:
`modesAcceptClients()` drains pending accepts on all four listeners, and
`modesReadFromClients()` non-blockingly reads each client, splitting on
`\n` (raw input) or `\r\n\r\n` (HTTP). Writes are best-effort: a short
`write()` drops the client. There is no event library — just `select()` in
net-only mode and polling elsewhere.

The browser map (`gmap.html`) polls `/data.json` via AJAX; the JSON is
regenerated per request from the aircraft linked list.

## Shared state

Everything lives in the global `struct Modes` (config, buffers, aircraft
list, client array, statistics, stats counters). The only synchronization
is `data_mutex`/`data_cond` guarding the I/Q buffer and `data_ready`
between the reader and main threads. The aircraft list and client table are
touched only by the main thread, so they need no locking.

## The shared I/Q buffer in detail

### Layout

`Modes.data` is a byte buffer of size
`Modes.data_len = MODES_DATA_LEN + (MODES_FULL_LEN-1)*4`:

- `MODES_DATA_LEN` = 256 KB — one block of raw interleaved I/Q samples
  (`I0 Q0 I1 Q1 ...`, unsigned 8-bit, 127 = zero signal).
- `(MODES_FULL_LEN-1)*4` ≈ 476 bytes — an **overlap head**. A worst-case
  message spans `MODES_FULL_LEN` = 120 µs (8 µs preamble + 112 bit periods);
  at 2 MS/s each I/Q pair is one sample, so a message starting on the very
  last sample of a block needs 239 samples = 478 bytes of lookahead.
  Reserving `(120-1)*4` at the front guarantees any boundary-crossing
  message fits.

```
Modes.data
┌──────────────────────┬────────────────────────────────┐
│ overlap: 476 bytes   │   new block: 256 KB of I/Q     │
│ (tail of prev block) │   written here                 │
└──────────────────────┴────────────────────────────────┘
 0                  476                            data_len
```

### How a block is written

Both writers perform the same two-step copy under `data_mutex`:

`rtlsdrCallback()` (device path, invoked by librtlsdr per USB block):

```c
pthread_mutex_lock(&Modes.data_mutex);
/* 1. Slide the unprocessed tail of the previous buffer to offset 0. */
memcpy(Modes.data, Modes.data+MODES_DATA_LEN, (MODES_FULL_LEN-1)*4);
/* 2. Append the fresh samples right after the overlap. */
memcpy(Modes.data+(MODES_FULL_LEN-1)*4, buf, len);
Modes.data_ready = 1;
pthread_cond_signal(&Modes.data_cond);
pthread_mutex_unlock(&Modes.data_mutex);
```

`readDataFromFile()` (`--ifile` path) does the identical slide-then-fill,
but loops on `read()` until the 256 KB block is complete — padding with
127 ("no signal") at EOF, or seeking to 0 with `--loop`. Unlike the device
callback it also *blocks* on `data_cond` while the previous block is still
unconsumed (librtlsdr has its own internal buffering, so the callback just
overwrites).

### Producer/consumer handshake

A strict single-slot handoff built on two fields guarded by `data_mutex`:
the `data_ready` flag and the `data_cond` condition variable. The flag, not
the signal, carries the state; both sides re-check it after waking
(Mesa-monitor style, immune to spurious wakeups).

Reader thread (file mode):

```c
while(1) {
    if (Modes.data_ready)
        pthread_cond_wait(&data_cond, &data_mutex);  /* wait until consumed */
    ... slide overlap, fill buffer ...
    Modes.data_ready = 1;
    pthread_cond_signal(&data_cond);                 /* wake decoder */
}
```

Main thread:

```c
pthread_mutex_lock(&Modes.data_mutex);
while(1) {
    if (!Modes.data_ready) {
        pthread_cond_wait(&data_cond, &data_mutex);  /* wait for data */
        continue;
    }
    computeMagnitudeVector();      /* I/Q -> magnitude, still under lock */
    Modes.data_ready = 0;          /* mark consumed */
    pthread_cond_signal(&data_cond);          /* wake reader */
    pthread_mutex_unlock(&data_mutex);        /* release! */
    detectModeS(...);              /* expensive scan, lock-free */
    backgroundTasks();             /* net accept/read, screen refresh */
    pthread_mutex_lock(&data_mutex);
}
```

### Design points

1. **Overlap carry-over**: `detectModeS()` only scans
   `j < mlen - MODES_FULL_LEN*2`, so the last ~239 samples of each block are
   never examined in place. They're copied to the front of the next buffer,
   where a message starting there is seen whole. Nothing is lost at block
   boundaries.

2. **Narrow lock scope**: the mutex is held only while copying bytes and
   computing the magnitude vector (a fast LUT pass). The expensive part —
   `detectModeS()` scanning ~128K samples — runs with the lock **released**.

   This works because `detectModeS()` operates on `Modes.magnitude`, a
   *separate* `uint16_t` buffer (one magnitude per I/Q pair, produced by
   `computeMagnitudeVector()` via `maglut`). The main thread can safely
   unlock: the raw I/Q buffer is free for the reader to refill while the
   decoder works on its private magnitude snapshot.

3. **Initialization**: `modesInit()` fills `Modes.data` with 127
   (zero-signal midpoint) so the overlap region is sane on the first block.

## Initialization sequence (`main()`)

1. `modesInitConfig()` → defaults; parse CLI args.
2. `modesInit()` → allocate buffers, build `maglut`, `modesInitErrorInfo()`
   (syndrome table), init mutex/cond.
3. Open data source: `modesInitRTLSDR()` or `--ifile` file/stdin.
4. `modesInitNet()` if `--net`.
5. If `--net-only`: serve-only loop. Otherwise spawn reader thread and enter
   the decode loop.
6. On EOF (`--ifile` without `--loop`): print `--stats` counters and exit.

## Error-correction tables

`modesInitErrorInfo()` precomputes the CRC syndrome of every single-bit
error (and every two-bit error for DF17 under `--aggressive`-style fixing)
into `bitErrorTable[]`, so `fixBitErrors()` is a table lookup rather than
brute force at runtime.
