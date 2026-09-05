# A cell that synchronises better than the one that works, and never says anything

Band 8 has an LTE cell at EARFCN 3475 (927.5 MHz) that the chain finds in
almost every block and that has never produced a Master Information Block.

```
$ ./sdrprobe --headless --lte-chain --lte-chain-band 8 --lte-chain-seconds 15
lte-chain earfcn 3475 carrier_hz 927500000 rate 1920000 ppm 0
chain 1 pss 0.918 0.360 n_id_2 0 timing 3329 offset_hz -30126 integer -2
chain 1 sss 0.829 0.417 n_id_1 110 pci 330 cp normal half_frame 0
lte-chain-summary blocks 219 cells 216 parity 0 messages 0
```

216 cells in 219 blocks and not one message. For comparison the band 20 cell
that works decodes 168 messages in 175 blocks, and it synchronises *less*
strongly: PSS 0.87 and SSS 0.76 against this one's 0.918 and 0.829.

`CLAUDE.md` says what that shape means: "SSS without parity is the broadcast
channel". This is the funnel doing its job and naming a stage, and nobody has
looked at the stage.

## It reproduces offline

Recorded 2026-09-05, three seconds at 1.92 MS/s. `captures/` is gitignored, so
re-record it with

```
./sdrprobe --headless --earfcn 3475 --sample-rate 1920000 \
    --technology lte --record-seconds 3
make probe-lte-chain FILE_LTE=captures/<the file>
```

which gives `12 blocks, 11 with a cell, 0 with a message` -- the same behaviour
as on air, so this can be worked on without the receiver.

## What is already ruled out

- **The CRS shift.** PCI 330 mod 6 is 0, and the band 20 cell that works is
  PCI 28, shift 4 -- so "the extraction is only ever exercised at one shift"
  was the obvious first guess. It is wrong: `tests/lte_dsp_test.c` builds
  synthetic buffers at PCI 0, 101, 227, 310 and 503, which is shifts 0, 5, 5, 4
  and 5, and the extraction decodes all of them.
- **The soft bits are not empty.** `probe-lte-chain` reports mean |soft| of
  3.1, 5.3 and 6.5 for one, two and four ports, so the broadcast channel's
  resource elements are being read and equalised into something with
  magnitude. It is the parity that never fits, not the extraction that returns
  nothing.
- **The frequency offset is not unusual.** -32.8 kHz here, -2 whole
  subcarriers, against -28.0 kHz and -2 subcarriers on the capture that works.

## What has not been looked at

- **The cyclic prefix.** Reported `normal` in every block. If it were extended
  and misread, PSS and SSS would still correlate -- they are in the last
  symbols of their slots -- while every broadcast-channel symbol would be
  sampled in the wrong place, and the failure would be exactly this
  systematic. Nothing has checked the margin of that decision on this signal.
- **The primary sequence's margin.** On the capture the peak is 0.583 against
  a runner-up of 0.479, where the band 20 capture is 0.87 against much less.
  A correct identity found on a thin margin is still a correct identity, but
  it is worth knowing whether the timing that comes with it is good enough for
  four symbols of broadcast channel.
