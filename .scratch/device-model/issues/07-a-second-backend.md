# 07 - A second backend behind the acquisition worker

Status: needs-info -- **the device is now chosen** (2026-09-08): a TZT /
HamGeek "upgrade USRP B210", AD9361BBCZ + XC7K325T. That answers the driver
question (UHD) and raises a new one, below.
Blocked by: 02, 03, 04

The driver, at last. Everything above is device-shaped arithmetic; this is the
device.

## What is waiting on information

**Which device.** The spec does not depend on it and this ticket does:

- a real Ettus B210 means UHD, whose C API (`uhd.h`) is usable from C but
  whose library is C++;
- a LimeSDR means LimeSuite; a bladeRF means libbladeRF;
- an ADALM-Pluto is USB 2.0 and caps near 20 MHz, which reopens SIB1 but not
  comfortably, and would not reopen NR at 30 kHz spacing with margin;
- SoapySDR covers all of them behind one C API and is the hedge, at the cost
  of a layer that normalises away the facts ticket 02 exists to keep.

## Shape, whichever it is

`acquisition.h` currently `#include`s `<rtl-sdr.h>`, so the seam is in the
header rather than behind it. A worker gains a third variant beside
`receiver_worker` and `file_worker`. The pull model matters: librtlsdr pushes
fixed buffers through `rtlsdr_read_async`; UHD's `recv()` is a pull with
metadata, including **sample timestamps and overflow flags**, neither of which
librtlsdr can provide. The slot stays a single overwriteable slot (ADR-0002);
an overflow flag is a thing to *report*, not a reason to queue.

An ADR is owed here, because "device I/O is not a DSP library" is a reading of
ADR-0003 that should be written down rather than assumed.

## Not in scope

- Two channels. The contract must not preclude MIMO; this does not build it.
- Transmitting.


## The board that was ordered, and what it means for this ticket

`docs/absolute-power-reference.md` looked into it. Two findings.

**It runs stock host UHD with a vendor-supplied FPGA image.** A genuine B210 is
a Spartan-6 XC6SLX150 built with ISE 14.7; an XC7K325T is a Kintex-7 and needs
Vivado. Both vendors say the `usrp_b210_fpga.bin` in the UHD image directory
must be replaced with theirs before use. That works because UHD's
`check_fpga_compat()` reads a `0xACE0BA5E` signature and a compatibility number
out of the image and never inspects the silicon.

So the backend is **UHD**, and the host side is ordinary. The liability is the
image:

- `uhd_images_downloader.py` will **overwrite the vendor image**, because it is
  not in UHD's manifest. Back it up before the first run of that tool.
- A UHD release that bumps the FPGA compatibility number **strands the board**
  until the vendor rebuilds. That is a pinning question this ticket has to
  answer: which UHD version this program is developed against, and what happens
  when the distribution moves.

**The clock is a question mark.** The GPSDO slot is removed and no TCXO figure
is published. Ticket 08 and `device_profile.ppm_drifts` assume a TCXO "barely
drifts"; for this board that is **unverified** and has to be measured rather
than assumed -- which the existing calibration path can do on arrival, against
GSM ARFCN 113 or an LTE cell as it already does.

## To settle on arrival

The research predicts several things. This is how to make the board answer
rather than trusting the prediction.

**Do not guess the calibration directory -- ask UHD for it.** The research said
`$XDG_DATA_HOME/uhd/cal`; UHD's own comment in `cal/database.cpp` says
`$XDG_DATA_HOME/uhd/cal_data`; a question to the research agent had assumed
`~/.uhd/cal`. Three answers, and `get_cal_data_path()` is a public function
that settles it, so **nothing should ever hardcode this path** -- not this
ticket, not `sdrprobe`. It reads `$UHD_CAL_DATA_PATH` when set, which is a
fourth possibility. Print it:

```sh
uhd_find_devices                       # does it enumerate, and with what serial
uhd_config_info --images-dir --print-all
python3 -c 'import uhd; print(uhd.get_cal_data_path())'
ls -la "$(python3 -c 'import uhd; print(uhd.get_cal_data_path())')"
```

**Whether power calibration data exists**, which is the prediction ticket 08
rests on:

```sh
python3 -c '
import uhd
u = uhd.usrp.MultiUSRP("type=b200")
for ch in (0, 1):
    print(ch, "rx:", u.has_rx_power_reference(ch),
             "tx:", u.has_tx_power_reference(ch))
'
```

`False` everywhere is the prediction. `has_*_power_reference()` is the right
probe rather than looking for the file, because it tests the whole load path.

The property tree gives the same answer without Python, and gives it as a
**positive** confirmation rather than an absence. Verified in
`cal/pwr_cal_mgr.cpp`: `ref_power/key` and `ref_power/serial` are created
unconditionally, then `if (!has_power_data()) return;` guards `ref_power/value`
and `ref_power/range`. So key-and-serial-but-no-value means the manager was
built and found nothing, which is exactly the state predicted:

```sh
uhd_usrp_probe --tree | grep -i ref_power
```

**Whether the EEPROM serial is unique per unit.** It matters more than it
looks: the calibration key is `mb_eeprom["serial"] + "#A"` and the filename is
`key + "_" + serial + ".cal"`, so a batch shipped with one serial would have
two boards silently loading each other's calibration table -- and `sdrprobe`
storing its own correction under the same key would inherit that. One board
cannot prove uniqueness; `uhd_find_devices` records what this one says, and a
second unit would settle it.

**Whether the clock drifts.** `device_profile.ppm_drifts` is `1` for the RTL
because a crystal drifts. For this board it is unverified -- no TCXO figure is
published and the GPSDO slot is removed -- so measure it with the calibration
path this program already has, against ARFCN 113 or an LTE cell, on two
sessions far enough apart to see drift if there is any.


## The FPGA image: back it up before running the downloader once

The board ships a vendor `usrp_b210_fpga.bin` because the silicon is a
Kintex-7 where a genuine B210 is a Spartan-6. UHD does not care --
`check_fpga_compat()` (`b200_impl.cpp:1212`) checks a `0xACE0BA5E` signature
and a major compat number and never inspects the part -- but `uhd_images_downloader`
does not know the vendor image exists.

**It does not merely overwrite it.** `update_target()` calls
`delete_from_inv()`, which `os.remove()`s every file recorded in
`inventory.json` for that target, then unpacks the Ettus archive over the
directory. No prompt. `-n` shows what it would touch.

```sh
IMG=$(uhd_config_info --images-dir | sed 's/.*: //')   # typically /usr/share/uhd/images
sudo cp -a "$IMG/usrp_b210_fpga.bin" ~/hw/b210-vendor-fpga.bin
```

**Better: keep the image out of that directory entirely.** The B200 driver
takes an `fpga` device argument, verified at `b200_impl.cpp:464`:

```c
std::string b200_fpga_image = find_image_path(
    device_addr.has_key("fpga") ? device_addr["fpga"] : default_file_name);
```

```sh
uhd_usrp_probe --args "type=b200,fpga=/home/rjdinis/hw/b210-vendor-fpga.bin"
```

That makes the downloader harmless, and it is what `sdrprobe` should pass when
it opens the device rather than relying on whatever is in the shared directory.

**One more reason to expect needing it.** At `b200_impl.cpp:423`, if the
EEPROM's product ID is not one UHD recognises, the constructor rethrows --
*unless* `fpga` was given, in which case it carries on with the product named
`"B200?"`. A clone with an unrecognised product ID would therefore **only work
at all with `fpga=` specified**. Whether this one is recognised is unverified;
if `uhd_find_devices` fails on arrival, this is the first thing to try rather
than a sign the board is dead.

**Identifying which image is loaded** is weaker than it looks:

```sh
uhd_usrp_probe --string /mboards/0/fpga_version     # expect "16.<minor>"
```

The major number is 16 whoever built it -- a vendor image reports 16
deliberately, or it would not load. Only the minor could differ and whether it
does is unverified. **`sha256sum` against the backup is the reliable
discriminator.** The consolation is that getting it wrong is loud: an Ettus
Spartan-6 bitstream loaded into a Kintex-7 fails at open rather than running
subtly wrong.

## Pinning, which this ticket still has to decide

A UHD release that bumps `B200_FPGA_COMPAT_NUM` strands the board until the
vendor rebuilds. So: which UHD version is `sdrprobe` developed against, and
what happens when the distribution moves past it? Unanswered, and it is the
part of this ticket that is genuinely a decision rather than a fact.


## Built 2026-09-08: the seam, both existing backends, and the build path

`src/device_backend.h` is the vtable the spec deferred until there was a
second backend to satisfy it. Fourteen entries -- open, close, frequency and
rate both ways, ppm, gain, flush, stream, stop -- with wrappers so a caller
writes `device_set_frequency_hz(&s, hz)` and a **NULL entry is a refusal in
one place instead of a crash in ninety**.

`flush` earns its place as an entry rather than an implementation detail:
`rtlsdr_reset_buffer()` appears thirteen times in this program and is the most
driver-specific thing in it. A backend with no pipeline returns 0, which is
truthful rather than a stub -- it lets every caller keep one
retune-then-flush path.

Two implementations exist now, which is what stops this being a pass-through
before the hardware lands:

- `src/backend_rtlsdr.c`, the nineteen distinct `rtlsdr_` calls. **Intended to
  become the only file that includes `<rtl-sdr.h>`** -- see What is left.
- `src/backend_capture.c`, which is mostly refusals, and the refusals are the
  content: a recording holds one tuning at one rate with one gain baked into
  its samples. It reads the sidecar for the container and rounds the file's
  length to whole pairs *of that container*. It deliberately does **not** pace
  -- pacing is the acquisition layer's, because that is what
  `acquisition_set_lossless()` turns off to get the same answer twice.

`check-device-backend`, 66 checks, adds a third and a fourth: a **fake** that
records what it was asked and can be made to fail on demand, and a **sparse**
backend that implements nothing, so the NULL-entry paths are reachable. It
asserts the property the receiver lease depends on -- **a refused retune moves
nothing** -- and that a closed session refuses rather than crashes. Verified by
mutation: making the capture backend accept a retune fails it twice by name.

### The build path, proven both ways

`HAVE_UHD` is a Makefile switch. `make` builds without UHD, which is the state
on this machine and the state of anyone this repository is handed to -- there
is no CI here, so `make check` has to run on a machine that never installed a
C++ SDR framework.

**It defaults to 0 rather than auto-detecting, and that is deliberate.** The
adapter is not written; auto-detecting would break the build for anyone who
happens to have UHD installed. `make HAVE_UHD=1` opts in and currently fails
with an `#error` naming this ticket, which is a better answer than a link
error. The pkg-config probe to switch to is written out in the Makefile
comment.

### Why `backend_uhd.c` contains no UHD

Because none of it could be compiled here, and this spec has spent its time
correcting exactly that kind of artifact -- an antenna-gain explanation that
was wrong, a full scale that agreed with nothing, a block size right only by
luck. What the file carries instead is the obligation list, beside the code
that will have to satisfy it: the `fpga=` argument a clone may need to
enumerate at all, `SAMPLE_FORMAT_S16` with a full scale that has to come from
somewhere real, `GAIN_MODEL_RANGE` whose unit is an **index** and not dB, a
`flush` that is a truthful no-op, and `ppm_drifts` that must be measured.

## What is left

**The app is not ported onto the seam yet.** `<rtl-sdr.h>` still reaches
`acquisition.h`, `app.h` and `sdrprobe.c`, and `app->dev` is still an
`rtlsdr_dev_t *`. That is 72 call sites and it is the next commit, not a
smaller job than the seam itself. Until it lands, the two backends are
compiled and unused, which is the one thing about this commit that is not
finished.

The order that keeps it reviewable: `app->dev` becomes a `struct
device_session`, then `open_receiver` and `open_capture`, then
`retune_receiver*` and the acquisition worker, then `overlay_settings.c`'s
eighteen gain calls last, because those are the ones ticket 06 rewrites anyway.
