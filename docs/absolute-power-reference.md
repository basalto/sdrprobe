# An absolute power reference for `sdrprobe`

What it would take to turn this program's dBFS into dBm, on the two
receivers it will have. Written against ticket
`.scratch/device-model/issues/08-artifacts-and-calibration-are-per-device.md`,
which absorbed the question from ticket 06.

## Recommendation

**Keep dBFS. Neither device can be made to report dBm without a one-time
measurement against a known source, and on the board that was ordered that
measurement is the only path there is.**

The three facts that settle it:

1. **UHD has an absolute-power API and the B200 series is wired into it.**
   `multi_usrp::set_rx_power_reference(power_dbm)` and its siblings take and
   return dBm, and `b200_impl.cpp` builds a power calibration manager for every
   frontend. So the API is not the obstacle.
2. **UHD ships no power calibration data for the B2xx.** The only calibration
   blobs compiled into the library are the X410's ZBX daughterboard tables. The
   image manifest carries FPGA and firmware images and no calibration data at
   all. On a B210 out of the box, `has_rx_power_reference()` returns false and
   the setters throw.
3. **The B210's RX "gain in dB" is a gain-table index, not a calibrated gain.**
   UHD advertises `meta_range_t(0.0, 76.0, 1.0)` and writes the integer
   straight into the AD9361's gain-table index register, where one of three
   band-dependent tables decides what it means in dB. The AD9361's own RSSI
   readback carries the same caveat in UHD's source: "The result is in dB but
   not in absolute units. If absolute units are required a bench calibration
   should be done."

So the honest answer to "can `sdrprobe` report dBm on this hardware" is: **only
after a user-run calibration against a known source, stored per device, per
frequency and per gain.** UHD supplies the machinery for that -- a table
format, a database, a calibration utility with a manual-entry mode for people
without a VISA instrument -- and it is exactly the shape of the ppm calibration
this program already measures and stores.

The RTL-SDR half is shorter. `rtlsdr_get_tuner_gains()` returns a 29-entry
array compiled into librtlsdr, identical on every unit ever made, derived from
one measurement of one tuner at 928 MHz. There is no factory calibration, no
per-unit data, and no readback: `rtlsdr_get_tuner_gain()` returns the value the
caller last asked for.

**If the reading is ever added, it must name its reference point and sit beside
dBFS rather than replace it.** That is ticket 08's constraint and nothing found
here weakens it.

## Three quantities that are easy to confuse

The whole question turns on keeping these apart, so they come first.

**(i) Power at the receiver's antenna connector.** This is what 36.214 asks
for: RSRP is defined at the UE's antenna connector, so the antenna's gain is
not part of the quantity. It is a property of the receiver alone, and it is
reachable -- what stands between this program and it is a dBFS-to-dBm offset
for the converter at a given frequency and gain. That offset is what this
document is about.

**(ii) Field strength, or the transmitter's EIRP.** These need the antenna's
gain, its pattern, its match and its orientation, none of which this program
knows and none of which a converter calibration supplies. Out of reach, and
still out of reach after everything below is done.

**(iii) A handset's RSRP.** Not comparable with ours whatever we do. A
telescopic whip and a handset's internal antenna intercept different fractions
of the same field, so a correct dBm at *this* connector is a different number
from a correct dBm at *that* one. RSRQ stays the transferable quantity, because
it is a ratio of two powers through one chain and every fixed gain cancels out
of it.

A calibration gets us (i) and only (i). Presenting (i) as if it were (iii) is
the failure mode worth guarding against, and labelling a field "RSRP (dBm)"
invites exactly that.

## A. USRP B210 and UHD

### A1. The API, its units and its version

`uhd::usrp::multi_usrp` carries four RX methods and four TX mirrors of them
(`host/include/uhd/usrp/multi_usrp.hpp`, RX at lines 1341-1402):

```cpp
virtual bool has_rx_power_reference(const size_t chan = 0) = 0;
virtual void set_rx_power_reference(const double power_dbm,
                                    const size_t chan = 0) = 0;
virtual double get_rx_power_reference(const size_t chan = 0) = 0;
virtual meta_range_t get_rx_power_range(const size_t chan) = 0;
```

The unit is **dBm**, stated in the parameter name and in the doc comment. The
semantics are not "tell me the power of the signal": `set_rx_power_reference()`
sets the power level that a full-scale sample corresponds to, and UHD reaches
it by choosing a gain from the calibration table. The header is explicit about
the failure mode:

> Many devices either don't have a built-in reference power API, or they
> require calibration data for it to work. This means that it is not clear,
> even when the device type is known, if a device supports setting a power
> reference level. Use this method to query the availability of
> `set_rx_power_reference()` and `get_rx_power_reference()`, which will throw a
> `uhd::not_implemented_error` or `uhd::runtime_error` if they cannot be used.

The API arrived in **UHD 4.0.0.0**, whose release notes list "Add reference
power level API to multi_usrp and radio_control", "Add power cal manager", "Add
pwr_cal container" and, under b200, "Enable power calibration API". The files
carry a 2020 copyright. *(The exact release date is unverified -- the release
page renders "September 14" without a year and the GitHub API timed out.)*

`get_rx_power_range()` is worth noting for a UI: it returns the range available
**given the current frequency, gain profile and antenna**, and the header warns
it "may change frequently, so don't assume an immutable range".

### A2. Which devices ship factory power calibration, and the B210 does not

The B200 series manual page says the API is supported:

> The B200 series support the UHD power calibration API (see: Power Level
> Controls). The TX path and the two RX paths have their own calibration data,
> resulting in 6 sets of calibration data total for the B210, and 3 for all the
> others.

Supporting the API is not the same as having the data. UHD reads calibration
through `uhd::usrp::cal::database`, whose source enum is `NONE`, `ANY`, `RC`,
`FLASH`, `FILESYSTEM`, `USER` -- and whose own header says the class "only has
access to RC and FILESYSTEM type cal data", throwing `uhd::key_error` for
anything else. `RC` is the resource compiler: data hard-coded into libuhd.
`FLASH` (device EEPROM) is a documented storage class that this class cannot
reach.

**What is compiled in is enumerated in one file.** `host/lib/rc/CMakeLists.txt`
lists the entire set:

```
    cal/test.cal
    cal/x4xx_pwr_zbx_tx_0_tx+rx0.cal
    cal/x4xx_pwr_zbx_tx_1_tx+rx0.cal
    cal/x4xx_pwr_zbx_rx_0_tx+rx0.cal
    cal/x4xx_pwr_zbx_rx_1_tx+rx0.cal
    cal/x4xx_pwr_zbx_rx_0_rx1.cal
    cal/x4xx_pwr_zbx_rx_1_rx1.cal
    cal/zbx_dsa_tx.cal
    cal/zbx_dsa_rx.cal
```

One test file, the X410's ZBX daughterboard power tables, and its DSA tables.
**No `b2xx_pwr_*` entry exists**, so no B200, B210, B200mini, B205mini or
B206mini has hard-coded power calibration in UHD. This is also consistent with
the manual's framing of hard-coded data as "an average table for a given
device" carrying "much higher calibration error than specifically generated
calibration data" -- a family-wide table, never a per-serial one, since the
manual states that storing serial-specific data in the resource compiler is not
permitted.

Nor is there a downloadable package: `images/manifest.txt` has no line
containing "cal", and its B2xx entries are `b2xx_b210_fpga_default` and
`b2xx_common_fw_default` -- an FPGA bitstream and a firmware image.

The consequence, traced through the source: `has_rx_power_reference()` reaches
`pwr_cal_mgr::has_power_data()`, which calls `_load_cal_data()`, which asks
`cal::database::has_cal_data(key, serial)` and nothing else
(`host/lib/usrp/common/pwr_cal_mgr.cpp`). With no RC entry and no file on disk,
**`has_rx_power_reference()` is false on a fresh B210** and
`set_rx_power_reference()` throws "no cal data available!".

The keys and serial the B210 looks under are built in `b200_impl.cpp` around
line 990:

- the calibration serial is the motherboard EEPROM serial plus `#A` or `#B`
  for the frontend, e.g. `FFF1234#A`;
- the key is `b2xx_pwr_` + `rx`/`tx` + `_` + the sanitised antenna name, giving
  `b2xx_pwr_rx_rx2`, `b2xx_pwr_rx_tx_rx` and `b2xx_pwr_tx_tx_rx`. Two channels
  times three paths is the six sets the manual mentions.

Note a discrepancy worth knowing before grepping: the code *comment* above that
line, and the manual page, both write the key as `b2xx_power_cal_$dir_$ant`,
while the string actually built is `b2xx_pwr_`. The code is what runs.

### A3. `uhd_power_cal.py` versus `uhd_cal_rx_iq_balance` -- different things

These are routinely conflated and they correct unrelated defects.

**`uhd_cal_rx_iq_balance`, `uhd_cal_tx_iq_balance`, `uhd_cal_tx_dc_offset`**
are the daughterboard self-calibration utilities. The manual describes them as
minimising "RX IQ imbalance vs. LO frequency", "TX IQ imbalance vs. LO
frequency" and "TX DC offset vs. LO frequency". They produce **quadrature and
DC corrections, not an absolute power reference**, and they write to the same
cal directory, which is part of why the two get confused.

They also do not apply here. The manual lists the supported frontends as the
RFX, WBX, SBX, CBX, UBX and OBX daughterboards and the N320, and states that
"USRP E310, E320, N300, N310 and B200-Series use a dedicated RFIC which does
its own calibration". The AD9361 calibrates its own quadrature and DC offset in
hardware; there is nothing for these utilities to do on a B210.

**`uhd_power_cal.py`** (`host/python/uhd/utils/uhd_power_cal.py`) is the
absolute-power one, and the only one that produces a dBm reference. It walks
frequency and gain, records what the device reads against what a reference
instrument says, and writes a `pwr_cal` table. The manual states the equipment
requirement plainly: a calibrated power meter for TX, a calibrated signal
generator for RX, and "A calibrated USRP can thus be used to calibrate another
USRP".

There is a B200 calibrator class, so the B210 is a supported target
(`host/python/uhd/usrp/cal/usrp_calibrator.py`):

```python
class B200Calibrator(USRPCalibratorBase):
    mboard_ids = ("B200", "B210", "B200mini", "B205mini", "B206mini")
    default_rate = 5e6
    lo_offset = 10e6
```

It reads the frontend temperature sensor before starting, and the base class
defaults to a 10 dB gain step, a maximum input power of -20 dBm and a minimum
detectable signal of -70 dBm.

**The measurement device does not have to be programmable.**
`host/python/uhd/usrp/cal/meas_device.py` defines `ManualPowerMeter` and
`ManualPowerGenerator`, both with `key = 'manual'`, which prompt a person at
the terminal:

```
[RX] Set your signal generator to following frequency: {:.3f} MHz, then hit Enter.
[RX] Please enter the measured power in dBm:
```

The alternatives are a VISA instrument (`key = 'visa'`, via pyvisa), NI
RFSA/RFSG, and another USRP as the generator. So the workflow is reachable
by anyone with a signal generator whose output level is trustworthy, without
automation.

### A4. Producing a table by hand, and where it goes

Storage path, from `host/lib/utils/paths.cpp`:

```cpp
std::string uhd::get_cal_data_path(void)
{
    const std::string uhdcalib_path = get_env_var(UHD_CAL_DATA_PATH_VAR);
    if (not uhdcalib_path.empty()) {
        return uhdcalib_path;
    }
    const fs::path cal_data_path = get_xdg_data_home() / "uhd" / "cal";
    return cal_data_path.string();
}
```

So `$UHD_CAL_DATA_PATH` if set, otherwise `$XDG_DATA_HOME/uhd/cal`, which on a
Linux box without `XDG_DATA_HOME` set resolves to `~/.local/share/uhd/cal`.
Filenames are `key + "_" + serial + ".cal"` (`get_cal_path_fs()` in
`host/lib/cal/database.cpp`), giving for example
`b2xx_pwr_rx_rx2_FFF1234#A.cal`. Files over 10 MiB are refused as implausible.

**The schema**, from `host/include/uhd/cal/pwr_cal.hpp`, is exactly the shape a
converter reference needs:

- for each frequency, a map of gain (dB) to power (dBm), plus the minimum and
  maximum available power at that frequency;
- each such table tagged with the **temperature in Celsius** at which it was
  measured;
- lookup interpolates temperature by nearest data set first, then does a
  bilinear interpolation over frequency and gain;
- the interpolation "assume[s] a monotonic gain/power profile", with
  `get_gain_coerced()` provided for when it is not.

The temperature axis is itself a finding: Ettus considered level-versus-
temperature large enough to index the table by it.

The B210's `set_serial()` uses the EEPROM serial, so a hand-written table has
to be named for the device it was measured on. That is the right behaviour --
the whole point is that this number does not transfer between units.

The disclaimer on the manual page is worth quoting when deciding how much to
claim on screen: "USRPs are not factory-calibrated test and measurement
devices, but general purpose SDR devices."

### A5. What `get_rx_gain()` actually returns on a B210

Not a calibrated absolute gain. UHD advertises the range as

```cpp
static uhd::meta_range_t get_gain_range(const std::string& which)
{
    if (which[0] == 'R') {
        return uhd::meta_range_t(0.0, 76.0, 1.0);
    } else {
        return uhd::meta_range_t(0.0, 89.75, 0.25);
    }
}
```

(`host/lib/include/uhdlib/usrp/common/ad9361_ctrl.hpp`). The TX branch is a
genuine attenuator setting -- `ad9361_device_t::set_gain()` converts it to
0.25 dB attenuation steps and writes the attenuation word, and the comment says
so. The RX branch is not:

```cpp
if (direction == RX) {
    int gain_index = static_cast<int>(value);
    if (gain_index > 76) gain_index = 76;
    if (gain_index < 0)  gain_index = 0;
    ...
    _io_iface->poke8(0x109, gain_index);
```

with the function's own comment: "Note that the 'value' passed to this function
is the gain index for RX." The 0-76 range in steps of 1 is the index range, and
what each index is worth in dB is decided by whichever of three gain tables the
band selected -- `gain_table_sub_1300mhz`, `gain_table_1300mhz_to_4000mhz`,
`gain_table_4000mhz_to_6000mhz` in `ad9361_gain_tables.h`, reprogrammed on a
band change. Each is a 77-entry table of LNA, mixer and TIA settings, not dB
values.

So on a B210 the RX gain is nominal, per-band, and per-part. It is a
considerable improvement on the RTL's list -- monotonic, one step per index, in
a documented range -- but it is not the missing term, and the ticket 06 triage
was right to reach the same conclusion by a different route. **Any calibration
table must therefore be indexed by band as well as frequency**, because the
meaning of the gain setting itself changes at 1300 MHz and 4000 MHz.

*(Unverified: Analog Devices' claimed accuracy for the gain tables themselves.
The AD9361 datasheet specifies noise figure, gain range and step size; I did
not find a stated absolute gain accuracy per index, and I have not asserted
one.)*

### A6. The AD9361's internal RSSI

The AD9361 measures RSSI in hardware, and UHD exposes it: `ad936x_manager.cpp`
publishes `sensors/rssi` on every RX frontend, so `get_rx_sensor("rssi")` works
on a B210. The sensor's unit string is **"dB"**, and UHD's own comment above
the register read is the answer to the question:

> Read back the internal RSSI measurement data. The result is in dB but not in
> absolute units. If absolute units are required a bench calibration should be
> done. -0.25dB / bit 9bit resolution.

Analog Devices says the same thing in UG-570's RSSI section: "Note that the
RSSI value is not in absolute units. Equating the RSSI readback value to an
absolute power level (for example, in dBm) requires a factory calibration." The
manual's prescription is to "inject a signal into the antenna port of the
completed system and read the RSSI word", then "generate a correction factor
that equates the RSSI word to the injected signal level at the antenna port" --
which is the same one-time measurement against a known source arrived at above,
by a different road.

On accuracy, UG-570 states that after the gain step calibration "RSSI error
typically is within 2 dB of the expected value, which is satisfactory for most
applications", that the calibration "measures the actual gain steps to 0.25 dB
precision and creates error terms that are added to the calculated RSSI value",
and that "Each system runs this calibration as part of its factory test routine
so that RSSI is optimized for each unit". That last sentence is about the
*system integrator's* factory test, not Analog Devices', and there is no
evidence that any B210 -- genuine or clone -- has such a routine run on it.
*(Confidence: the UG-570 quotes above are from a mirrored copy of the manual on
manualslib rather than the analog.com PDF, which did not extract cleanly. The
substance is corroborated independently by UHD's source comment.)*

**RSSI is therefore not a shortcut.** It replaces one uncalibrated number
(dBFS) with another uncalibrated number (dB), needing the same bench
measurement to become dBm. Its one advantage is that it is measured in the RFIC
after the analogue chain, so it does not depend on knowing the gain table -- if
a correction is measured for it, that correction has one axis fewer.

## B. RTL-SDR

### B1. What `rtlsdr_get_tuner_gains()` returns

The unit is documented in `rtl-sdr.h`: "gains array of gain values. In tenths
of a dB, 115 means 11.5 dB." What is not documented there is where the values
come from. `librtlsdr.c` (lines 959-970) holds them as a compile-time constant:

```c
const int r82xx_gains[] = { 0, 9, 14, 27, 37, 77, 87, 125, 144, 157,
                             166, 197, 207, 229, 254, 280, 297, 328,
                             338, 364, 372, 386, 402, 421, 434, 439,
                             445, 480, 496 };
```

Twenty-nine values, returned for `RTLSDR_TUNER_R820T` and `R828D` alike.
Nothing is read from the device. **The list is identical on every dongle with
that tuner**, so it is nominal by construction -- it cannot express a
unit-to-unit difference, a frequency dependence or a temperature.

Its provenance is a comment in `tuner_r82xx.c` above the step tables the
setter walks:

```c
/* measured with a Racal 6103E GSM test set at 928 MHz with -60 dBm
 * input power, for raw results see:
 * http://steve-m.de/projects/rtl-sdr/gain_measurement/r820t/
 */
```

One tuner, one frequency, one input level, in 2013. The setter then walks LNA
and mixer steps alternately until the running total reaches the request, so an
arbitrary request is rounded to whatever combination the walk lands on -- which
is why `overlay_settings.c` already compares a requested gain against a
reported one.

And the readback is not a readback:

```c
int rtlsdr_get_tuner_gain(rtlsdr_dev_t *dev)
{
	if (!dev)
		return 0;

	return dev->gain;
}
```

`dev->gain` is set by `rtlsdr_set_tuner_gain()` to the value the caller passed,
on success. So `rtlsdr_get_tuner_gain()` reports the request, not the hardware.

### B2. Is there any factory or documented absolute calibration?

**No, and the negative is established rather than assumed.**

- The gain values are the compiled-in array above. There is no code path that
  reads a gain table, an offset or a level correction from the device.
- The RTL2832U's EEPROM is reachable (`rtlsdr_read_eeprom()` /
  `rtlsdr_write_eeprom()` are raw byte accessors at `EEPROM_ADDR 0xa0`), but
  librtlsdr interprets only the USB descriptor strings out of it -- the
  manufacturer, product and serial strings that
  `rtlsdr_get_device_usb_strings()` returns and that the tools'
  `manufact_check` / `product_check` compare against. No level or
  frequency-response field is read, written or defined.
- Rafael Micro has published no first-party datasheet for the R820T, R820T2 or
  R828D. `tuner_r82xx.c` states its own origin -- "This driver is a heavily
  modified version of the driver found in the Linux kernel" -- and the register
  layout in it is reverse-engineered rather than transcribed from a
  specification. There is consequently no manufacturer statement of absolute
  gain accuracy to cite either way. *(Confidence: high on librtlsdr's contents,
  which are verifiable; "no first-party datasheet exists" is an absence of
  evidence, so read it as "none is publicly available", not as a certainty.)*

The dongles are DVB-T receivers repurposed as SDRs. An absolute level reference
is not something their intended application ever needed.

### B3. Unit-to-unit spread, frequency dependence, temperature

Every number in this section is a community or academic measurement, not a
manufacturer figure, and is labelled as such.

**Frequency dependence, and the size of the gain-setting error.** Freitas et
al., *Implementation of a Spectrum Analyzer Using the Software-Defined Radio
Concept*, Journal of Microwaves, Optoelectronics and Electromagnetic
Applications 20(4), December 2021, calibrated one RTL-SDR R820T2 against an
Anritsu MS2034A at ten frequencies from 100 to 1000 MHz, at two gain settings.
Their Table I gives the correction to be applied to the dongle's reading:

| f (MHz) | correction at gain 0 dB | correction at gain 8 dB | difference |
| ---: | ---: | ---: | ---: |
| 100 | -14.03 | -0.06 | 13.97 |
| 200 | -13.38 | +0.96 | 14.34 |
| 300 | -17.95 | -1.87 | 16.08 |
| 400 | -16.77 | -2.48 | 14.29 |
| 500 | -14.65 | -0.60 | 14.05 |
| 600 | -16.04 | -3.21 | 12.83 |
| 700 | -20.49 | -5.32 | 15.17 |
| 800 | -20.48 | -6.96 | 13.52 |
| 900 | -17.18 | -3.42 | 13.76 |
| 1000 | -17.80 | -4.78 | 13.02 |

Two things follow, and they are of different strengths.

The **difference column is the strong one**, because both columns were taken
through the same cables on the same day, so the setup's losses cancel. Moving
the gain setting from nominally 0 dB to nominally 8 dB changed the actual
response by **12.8 to 16.1 dB**, varying by 3.3 dB across the band. The gain
setting is wrong by roughly a factor of two, and wrong by a frequency-dependent
amount. (librtlsdr's R820T list has no exactly 8.0 dB entry; 7.7 and 8.7 dB are
the neighbours, so the nominal step is one of those. Either way the discrepancy
is 5 dB or more.)

The **spread within one column is the weaker one**: about 7.1 dB at gain 0 and
7.9 dB at gain 8 across 100-1000 MHz. That is an *upper bound* on the dongle's
own frequency dependence, not a measurement of it, because the paper says the
correction also absorbs "the attenuation values added by the cables", and the
reference column itself moves from -63.22 to -66.22 dBm across the sweep. The
conclusion the paper draws is nevertheless the one that matters here: the
device is "not designed to perform RF measurements", so a calibration is
required, and it has to be per frequency.

**Unit-to-unit spread.** No measurement of a population of dongles was found in
a citable source. The argument that it must exist is structural rather than
empirical -- the gain table is a constant compiled into the driver, so any
per-part variation in the LNA, mixer or filter is unmodelled by construction --
and that argument should not be dressed up as a measurement. **Unverified: how
large the spread actually is.**

**Temperature.** Widely reported for *frequency*: rtl-sdr.com's freezer-and-
lamp testing recorded a control dongle drifting 14 ppm as it warmed, from 58 to
72 ppm, which is why the RTL-SDR Blog V3 fits a TCXO. That is the drift this
program's per-site ppm calibration already exists to chase. **For *level*, no
first-party or well-controlled measurement was found. Unverified.** The
strongest indirect evidence that it matters at all is that UHD's `pwr_cal`
container indexes its tables by temperature, which is a statement about
AD9361-based hardware and not about an R820T.

## C. The board that was ordered

TZT / HamGeek "upgraded USRP B210", AD9361BBCZ + XC7K325T.

### C1. What these boards are

**The FPGA claim is real and it is not the genuine part.** A genuine Ettus B210
uses a Xilinx Spartan-6 **XC6SLX150**, built with ISE 14.7; the B200 uses an
XC6SLX75. The advertised **XC7K325T** is a Kintex-7, a different family
requiring Vivado. The vendors say so themselves -- "using the newer K7 series
instead of the S6 series", "Adopting Vivado development".

So the board is a redesign around the same AD9361 and the same USB 3.0 host
interface, not a copy of the Ettus schematic.

**Does it run stock UHD?** The vendor's own instruction is the clearest
evidence available and it is a qualified yes:

> Before use, the usrp_b210_fpga.bin file in the computer needs to be replaced.

Both HamGeek and OpenSourceSDRLab carry that line, which is consistent with the
board being what it claims: the **host-side** UHD is stock, and the **FPGA
image** is not. That arrangement can work, because UHD loads the bitstream over
USB at open time and then checks the image rather than the silicon --
`b200_impl::check_fpga_compat()` peeks a signature register expecting
`0xACE0BA5E` and compares a compatibility number against
`B200_FPGA_COMPAT_NUM = 16`, and `check_fw_compat()` compares the FX3 firmware
major against `B200_FW_COMPAT_NUM_MAJOR = 8`. A vendor bitstream that
implements the same register interface and reports the same compat number
satisfies both. Nothing in the driver asks what part it is running on.

The practical consequences for ticket 07:

- **Stock UHD host code, vendor FPGA image.** The image is not in
  `images/manifest.txt`, so `uhd_images_downloader.py` will not fetch it and
  will happily overwrite it with the Ettus one. Whatever the vendor ships has
  to be kept, and a UHD upgrade that bumps `B200_FPGA_COMPAT_NUM` strands the
  board until the vendor issues a rebuilt image. That is a real maintenance
  liability and it belongs in ticket 07's assessment.
- **The reference clock is not the Ettus one either.** Both vendors state
  "Remove the GPSDO slot" and "Cannot access GPSDO module", offering instead an
  onboard GPS module plus 10 MHz and PPS inputs, with the GPS PPS taking
  priority over an external PPS when locked. Neither page states a TCXO
  stability figure. **Unverified: the reference oscillator's part and ppm.**
  Ticket 08's premise that "a TCXO barely drifts and a GPSDO does not drift at
  all" therefore cannot be assumed for this board until it is measured -- which
  is what that ticket already says about writing constants from datasheets.
- **Nothing on either page mentions calibration of any kind.** No power
  calibration, no IQ calibration, no per-unit data.

### C2. EEPROM, serial, and whether any factory calibration path is inherited

**The EEPROM must be programmed for the board to enumerate at all, and this is
provable from the driver.** `get_b200_product()` in `b200_impl.cpp` takes the
USB product ID first, and failing that reads `mb_eeprom["product"]`, throwing
`"B200 unknown product code: 0x%04x"` if it is empty or unrecognised. The
motherboard EEPROM serial is then read again for the calibration serial:

```cpp
const std::string cal_serial =
    _tree->access<mboard_eeprom_t>(mb_path / "eeprom").get()["serial"] + "#"
    + (dspno ? "B" : "A");
```

So a board that enumerates as a B210 under stock UHD necessarily has a
programmed product code and, for the calibration keys to be well-formed, a
serial. Whether the serial is **unique per unit** is a different question and I
could not establish it. If a batch shipped with one serial, then per-device
calibration files would collide -- two boards would read each other's table --
and `sdrprobe` would be storing a correction under an ambiguous name. **That is
worth checking with `uhd_find_devices` on arrival, against a second unit if one
is ever available.**

**No factory calibration path is inherited, because there is none to inherit.**
This is the one part of section C that needs no assumption about the clone: as
established in A2, UHD ships no B2xx power calibration for *any* B210,
genuine or otherwise. A clone cannot inherit what does not exist. The only
route to dBm on this board is the same as on a genuine one -- run the
measurement yourself.

The residual risk specific to the clone is that its RF front end is a redesign
("The RF front-end is consistent with the original version, adopting a
frequency division design, and the RF circuit has been optimized through
simulation"), so a table measured on a genuine B210 would not transfer to it
even if one existed. Since one has to be measured per unit anyway, this costs
nothing.

## What it would take for `sdrprobe` to report dBm

The offset wanted is one number per (device, frequency, gain, temperature):
**how many dBm at the antenna connector a full-scale sample corresponds to.**
Everything above says it has to be measured. What follows is the shape.

### The measurement

1. **Feed a known level in.** A signal generator with a trustworthy output
   level, connected to the antenna port by a cable whose loss at the test
   frequency is known and subtracted. There is no way around owning or
   borrowing this: without a calibrated source there is no reference, and a
   second uncalibrated receiver is not one.
2. **At each frequency and gain of interest, read the level `sdrprobe` already
   computes** -- the same dBFS the survey and `lte_reference_power()` report --
   and record `offset = P_in_dBm - level_dBFS`.
3. **Sweep the axes that move it.** Frequency, because section B3 measures
   several dB of variation across a band and section A5 shows the B210's gain
   table changes meaning at 1300 and 4000 MHz. Gain, because the setting is
   nominal on both devices. Temperature, because UHD's own table format has an
   axis for it -- at minimum, record the temperature the table was taken at and
   refuse to interpolate across a big change.
4. **Store it per device**, keyed by something that identifies the unit. This
   is the one structural difference from the existing calibration:
   `config_site_ppm()` is per *site* because a crystal's drift is chased where
   you are, whereas a converter offset is a property of the hardware and
   travels with it. It does not belong under the site key.

`uhd_power_cal.py --args type=b200 -d rx --meas-dev manual` does steps 1 to 3
for a B210 with a person at the keyboard, and writes a table UHD itself will
then use -- after which `has_rx_power_reference()` becomes true and
`set_rx_power_reference()` works. For an RTL-SDR there is no equivalent and the
table would be `sdrprobe`'s own.

### Two ways to consume it, and the recommendation

**Either** let UHD do it: call `set_rx_power_reference(0.0)` and read
`get_rx_power_reference()`, which makes UHD choose the gain and tells you what
full scale is worth. This is neat but it moves gain selection out of this
program's hands, and `.scratch/device-model/issues/06`'s "not in scope" note is
emphatic that a level moving under the measurement makes surveys incomparable.

**Or** -- recommended -- keep setting the gain here, read
`get_rx_power_reference()` purely as the dBFS-to-dBm offset at the current
tuning, and add it. That keeps the fixed-gain discipline and reduces UHD's
machinery to a lookup table, which is what it is. It also means the same field
in `device_profile` can be filled from a UHD table on a B210 and from a
hand-measured table on an RTL-SDR, with everything above that seam identical.

Either way, `has_rx_power_reference()` is the gate: when it is false there is
no offset and the program reports dBFS, exactly as today.

### What it must say on screen

Ticket 08 already fixes this and nothing found here changes it. Restated with
the reasons this document supplies:

- **Name the reference point.** "At this receiver's antenna connector" -- which
  is where 36.214 puts RSRP, and where the calibrated source was injected.
- **Beside dBFS, not instead of it.** dBFS is what compares two cells on one
  receiver (EARFCN 3625's two cells at -33.3 and -35.0 dBFS), and that
  comparison neither needs nor is improved by the offset.
- **Never presented as comparable with a handset's RSRP.** Section "Three
  quantities" is why. A correct dBm at this connector is still a different
  number from a correct dBm at a handset's.
- **Carry the uncertainty.** The best case here is a hand-run calibration
  against one generator; UG-570's 2 dB is what a *factory* routine achieves for
  RSSI. A reading whose error bar is unstated invites more confidence than it
  has earned, and `signal_findings.h` already sets the precedent that a
  measurement states what it does and does not reach.

## What could not be established

- **The exact UHD 4.0.0.0 release date.** The release page renders "September
  14" without a year; the GitHub API timed out. The source files carry a 2020
  copyright.
- **Analog Devices' claimed accuracy for the AD9361 RX gain tables** as
  distinct from the RSSI figure. Not found; not invented.
- **Unit-to-unit spread of RTL-SDR absolute level.** No citable population
  measurement found. The structural argument that it must exist is not a
  number.
- **Temperature dependence of RTL-SDR *level*** (as opposed to frequency).
  Nothing well-controlled found.
- **Whether the ordered board's EEPROM carries a unique serial.** Determinable
  in one command on arrival; guessing would be worse than the gap.
- **The reference oscillator fitted to the ordered board**, and its stability.
  Neither vendor page states it.
- **UG-570 quotations** are from a mirrored copy on manualslib; the analog.com
  PDF did not extract. Corroborated in substance by UHD's source comment.

## Sources

Primary, UHD (source at `github.com/EttusResearch/uhd`, master):

- `host/include/uhd/usrp/multi_usrp.hpp` -- the power reference API and its
  doc comments.
- `host/include/uhd/cal/database.hpp`, `host/lib/cal/database.cpp` -- the
  source enum, the RC and filesystem back ends, the `.cal` naming.
- `host/lib/rc/CMakeLists.txt` -- the complete list of calibration blobs
  compiled into libuhd.
- `host/include/uhd/cal/pwr_cal.hpp` -- the power table schema.
- `host/lib/usrp/common/pwr_cal_mgr.cpp` -- how `has_power_data()` is decided.
- `host/lib/usrp/b200/b200_impl.cpp`, `b200_impl.hpp` -- the B210's cal keys
  and serial, the EEPROM product lookup, the firmware and FPGA compat checks.
- `host/lib/usrp/common/ad9361_driver/ad9361_device.cpp`,
  `ad9361_gain_tables.h`, `host/lib/include/uhdlib/usrp/common/ad9361_ctrl.hpp`,
  `host/lib/usrp/common/ad936x_manager.cpp` -- the RX gain index, the three
  band tables, the RSSI sensor.
- `host/lib/utils/paths.cpp` -- `get_cal_data_path()`.
- `host/python/uhd/utils/uhd_power_cal.py`,
  `host/python/uhd/usrp/cal/usrp_calibrator.py`,
  `host/python/uhd/usrp/cal/meas_device.py` -- the calibration workflow, the
  B200 calibrator, the manual measurement devices.
- `images/manifest.txt` -- no calibration data distributed.

Primary, Ettus documentation:

- Power Level Controls: <https://files.ettus.com/manual/page_power.html> and
  <https://uhd.readthedocs.io/en/uhd-4.9/page_power.html>
- Device Calibration and Frontend Correction:
  <https://files.ettus.com/manual/page_calibration.html>
- B200/B210 device manual: <https://files.ettus.com/manual/page_usrp_b200.html>
- `multi_usrp` reference:
  <https://files.ettus.com/manual/classuhd_1_1usrp_1_1multi__usrp.html>
- `cal::database` reference:
  <https://files.ettus.com/manual/classuhd_1_1usrp_1_1cal_1_1database.html>
- UHD 4.0.0.0 release notes:
  <https://github.com/EttusResearch/uhd/releases/tag/v4.0.0.0>
- USRP B210 product page (Spartan-6 XC6SLX150):
  <https://www.ettus.com/all-products/ub210-kit/>
- B200/B210 knowledge base: <https://kb.ettus.com/B200/B210/B200mini/B205mini/B206mini>

Primary, Analog Devices:

- AD9361 Reference Manual UG-570, RSSI sections. Consulted via
  <https://www.manualslib.com/manual/1071572/Analog-Devices-Ad9361.html?page=50>
  and `?page=51`; the first-party PDF is at
  <https://ez.analog.com/cfs-file/__key/communityserver-discussions-components-files/323/AD9361_5F00_Reference_5F00_Manual_5F00_UG_2D00_570.pdf>
- AD9361 datasheet:
  <https://www.analog.com/media/en/technical-documentation/data-sheets/ad9361.pdf>

Primary, librtlsdr (source at `github.com/osmocom/rtl-sdr`, master; header also
at `/usr/include/rtl-sdr.h` on this machine):

- `include/rtl-sdr.h` -- the tenths-of-a-dB unit and the gain API contract.
- `src/librtlsdr.c` -- `r82xx_gains[]`, `rtlsdr_get_tuner_gain()`, the EEPROM
  accessors.
- `src/tuner_r82xx.c` -- the LNA/mixer/VGA step tables and the Racal 6103E
  provenance comment.

Academic and community, labelled as such in the text:

- P. V. A. Freitas et al., "Implementation of a Spectrum Analyzer Using the
  Software-Defined Radio Concept", *Journal of Microwaves, Optoelectronics and
  Electromagnetic Applications*, 20(4), December 2021, pp. 801-811. DOI
  10.1590/2179-10742021v20i4254767.
  <https://www.scielo.br/j/jmoea/a/Hy9nk6f3WM6gms4QnWP8X6c/>
- rtl-sdr.com on thermal frequency drift:
  <https://www.rtl-sdr.com/tag/thermal-drift/>
- Vendor pages for the ordered board:
  <https://www.hgeek.com/products/hamgeek-upgraded-usrp-b210-ad9361bbcz-xc7k325t-sdr-development-board-usb3-0-type-c-56mhz-supports-pps-10mhz-input>
  and
  <https://opensourcesdrlab.com/products/opensourcesdrlab-usrp-b210-ad9361bbcz-xc7k325t-sdr-development-board-usb30-type-c-56mhz-support-pps-10mhz-input>
- usrp-users list, on locating cal files and `has_tx_power_reference()` as the
  loaded/not-loaded test:
  <https://www.mail-archive.com/usrp-users@lists.ettus.com/msg18471.html>
