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
