# liquid-dsp assessment for `sdrprobe`

## Recommendation

**Do not add liquid-dsp to this probe.** It materially removes the FFT kernel,
but not enough of the specified DSP pipeline to justify a second optional
package and a normalization adapter. More importantly, adoption would directly
contradict the current specification: DSP must be self-contained, external DSP
dependencies are out of scope, deterministic checks must link only `-lm`, and
raylib must remain the GUI target's only new package
([spec](./sdrprobe-implementation.md#makefile-integration)).

If that boundary is deliberately changed later, use liquid-dsp's **low-level
FFT plan plus `fft_shift`**, while retaining explicit local Hann coefficients,
coherent-gain normalization, linear averaging, per-window maxima, and the
`-120 dBFS` clamp. Avoid making `spgramcf` the normalization authority.

This assessment targets upstream release
[`v1.8.2`](https://github.com/jgaeddert/liquid-dsp/releases/tag/v1.8.2), released
2026-08-07; that release emphasizes reproducible builds and SIMD-selection
fixes, but does not change the application-level normalization contract.

## Does SIMD matter here?

liquid-dsp selects SIMD kernels (SSE/AVX/NEON) at build time, which is a fair
reason to prefer it *if* this program were short of CPU. It is not, and the
question is settled by measurement rather than argument: `make bench-dsp` times
every stage against the 65.5 ms of signal one 256 KB block covers, since that
is the interval the receiver delivers them at.

On an i5-8265U with AVX2, medians of three runs:

| Stage | per block | of the 65.5 ms budget |
| --- | ---: | ---: |
| byte → I/Q + magnitude | 0.22 ms | 0.3% |
| signal statistics | 1.4 ms | 2% |
| magnitude peak bins | 0.10 ms | 0.2% |
| DC removal | 0.24 ms | 0.4% |
| spectrum, 64 × 2048-point FFT | 4.2 ms | 6% |
| Mode S demodulation | 0.33 ms | 0.5% |
| survey peak search, 8192 bins | 0.52 ms | 0.8% |
| GSM SCH decode, all refinements | 20.7 ms | 32% |
| FCCH tone detection | 1.6 ms | 2.5% |

The Scope tab spends about 7 ms of every 65.5 ms; the GSM view, which adds the
SCH decode, about 28 ms. Live runs report zero overwritten blocks, which is the
same statement from the other side: the renderer keeps up with the receiver and
then waits.

Rebuilding with `-march=native`, which lets the compiler use this machine's
AVX2 throughout, moves nothing that matters: the FFT improves by roughly 10%
(4.2 → 3.8 ms, a saving of 0.4 ms per block), and the other stages land within
run-to-run noise or slightly worse — plausibly the clock behaviour of a U-series
part under wide vectors. `make bench-dsp BENCH_ARCH=-march=native` reproduces
it.

So SIMD is not a reason to adopt liquid-dsp here, and hand-vectorising the
local FFT would buy half a millisecond in sixty-five.

**What the measurement did find, and what came of it.** The largest Scope-path
cost was `sdr_dsp_signal_stats`, and almost all of it was one `qsort` of 131072
floats to recover two percentiles — an algorithmic cost, not a vectorisation
one. It now selects each rank in place instead, three-way partitioned because
magnitudes of 8-bit samples repeat in their thousands and a two-way split on
equal keys is quadratic on exactly that input. Built from the same source and
run back to back on the same machine:

| | per block |
| --- | ---: |
| sorting the block | 12.3 – 13.6 ms |
| selecting two ranks | 1.4 – 2.2 ms |

About nine times faster, and the values are bit-identical: selection returns
the element the sort would have placed at that rank, which `check-sdr-dsp`
verifies against a sorted reference over blocks of duplicates, sorted and
reversed input, all-equal input, and short blocks where the rank clamps bite.

So the one real cost in this program was worth about 11 ms a block, and SIMD
would not have found it. That is the shape of the answer to "should we
vectorise": measure first, and the thing you find is usually not a loop.

## API fit

| Requirement | Official API/evidence | Fit |
| --- | --- | --- |
| Complex FFT | `fft_create_plan`, `fft_execute`, `fft_destroy_plan`, and one-shot `fft_run` operate on single-precision complex arrays; input/output storage remains caller-owned ([public header](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/include/liquid.h#L2062-L2154)). The forward transform is unnormalized; upstream's round-trip test divides the inverse by `n` ([test](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/src/fft/tests/fft_transforms_autotest.c#L40-L60)). | **Strong.** Replaces the riskiest custom algorithm and reusable-plan lifecycle. It does not promise that a 2048-point plan is specifically radix-2; upstream currently selects mixed-radix even for powers of two ([selector](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/src/fft/src/fft_utilities.c#L42-L76)). |
| Hann window | `liquid_hann(i, n)` and `LIQUID_WINDOW_HANN` are public ([header](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/include/liquid.h#L7864-L7922)); the implementation is the symmetric `0.5 - 0.5 cos(2*pi*i/(n-1))` form ([source](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/src/math/src/windows.c#L245-L259)). | **Exact coefficient fit.** The application still needs a coefficient array, its sum, and multiplication unless `spgramcf` owns windowing. |
| FFT shift | Public in-place `fft_shift(x, n)` is documented as O(n) time/O(1) memory ([header](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/include/liquid.h#L2147-L2151)); upstream tests verify even-size half swapping ([test](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/src/fft/tests/fft_autotest.c#L26-L73)). | **Exact.** For power arrays, a local half-index mapping is equally small and avoids shifting complex output. |
| Streaming periodogram / Welch-like averaging | `spgramcf_create(nfft, window, window_len, delay)`, `write`, `set_alpha`, and FFT-shifted linear/dB getters provide streaming windowed periodograms and either infinite accumulation or exponential smoothing ([public API](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/include/liquid.h#L2188-L2312)). A configuration of `(2048, HANN, 2048, 2048)` gives the spec's non-overlapping transforms; the source accumulates squared magnitudes in linear space and divides by transform count before dB conversion ([source](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/src/fft/src/spgram.proto.c#L413-L480)). Upstream calls this a spectral periodogram rather than exposing a separately named Welch API. | **Partial.** It supplies Welch-like segmentation/windowing/linear averaging, but a whole-block result does not expose the greatest *individual* window needed for peak hold. Reset/read per 2048-sample window would recover that value but gives up most of the streaming abstraction. |
| PSD / dBFS normalization | `spgramcf` normalizes its window by `1/sqrt(sum(w^2))` ([source](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/src/fft/src/spgram.proto.c#L121-L152)), floors linear PSD at `1e-12`, FFT-shifts, then applies `10 log10` ([source](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/src/fft/src/spgram.proto.c#L444-L480)). | **Not the specified dBFS convention.** The spec divides complex FFT bins by `sum(w)`, making a bin-centered unit complex tone 0 dBFS. `spgramcf` instead makes that tone's bin power `(sum(w))^2/sum(w^2)`, about `1364.7` or `+31.35 dB` for a 2048-point symmetric Hann. A wrapper must multiply linear output by `sum(w^2)/(sum(w))^2`, then apply the application's floor. Calling the unadjusted output “dBFS” would be wrong. |
| Raw I/Q and magnitude/time bins | The public interface accepts floating-point real/complex samples; it does not define RTL-SDR unsigned interleaved byte conversion ([complex type and API](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/include/liquid.h#L624-L644), [FFT API](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/include/liquid.h#L2068-L2154)). | **Custom code remains.** Center bytes at 127.5, divide spectrum inputs by 127.5, calculate `sqrt(I^2+Q^2)`, compute min/mean/max, make pixel-width peak time bins, percentiles, deterministic scatter subsampling, and peak-hold decay locally. These are application semantics, not liquid-dsp abstractions. |

## Resource and thread contracts

- FFT plans bind caller-provided input/output pointers for their lifetime, and
  require explicit destruction; `fft_malloc` allocations must be paired with
  `fft_free` rather than ordinary `free`
  ([header](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/include/liquid.h#L2070-L2121)).
- `spgramcf` is an opaque mutable owner of its window buffer, FFT arrays/plan,
  coefficients, and PSD accumulator, all freed by `spgramcf_destroy`
  ([source](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/src/fft/src/spgram.proto.c#L38-L65),
  [destroy](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/src/fft/src/spgram.proto.c#L209-L225)).
  No public FFT/periodogram thread-safety guarantee was found. Keep each plan or
  periodogram confined to the spec's main/DSP thread; this needs no new locking.
- A liquid-dsp build can transparently use FFTW when found; upstream's default
  is to search for FFTW, while its internal FFT remains available
  ([README](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/README.rst#L46-L55),
  [build option](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/README.rst#L83-L108)).
  The internal backend selection explicitly switches periodogram plans and
  allocation/free functions to FFTW
  ([source](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/include/liquid.internal.h#L718-L741)).
  If FFTW is used, the public header warns that planner cleanup concerns static
  process-wide data and must occur only when no other code can create/use a
  plan ([header](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/include/liquid.h#L2156-L2161)).
- Upstream only documents explicit locking for its global logger, which has no
  mutex by default ([logging docs](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/doc/core/logging.rst#L399-L451)).
  Normal successful FFT calls need not log, but this is another reason not to
  infer library-wide thread safety from the presence of an optional threads
  build setting.

## Dependency and build impact

Upstream describes runtime requirements as `libc` and `libm`, with FFTW
optional ([README](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/README.rst#L46-L55)).
Its installed pkg-config template is named `liquid-dsp`, emits `-lliquid`, and
places `-lm` plus any detected FFTW in private link flags
([metadata](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/cmake/liquid-dsp.pc.in#L1-L15),
[generation](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/CMakeLists.txt#L749-L770)).
The optional target would therefore need, approximately:

```make
$(shell pkg-config --cflags liquid-dsp)
$(shell pkg-config --libs liquid-dsp)
```

The DSP test would also require liquid-dsp headers/library and could no longer
satisfy the current `-lm`-only contract. Static linking may additionally pull
the `Libs.private` backend selected when liquid-dsp itself was built.

Verified official package metadata is uneven:

- Arch Linux Extra publishes `liquid-dsp` 1.7.0 for x86_64, but its official
  file list contains the header and shared library and **no `.pc` file**
  ([package](https://archlinux.org/packages/extra/x86_64/liquid-dsp/),
  [files](https://archlinux.org/packages/extra/x86_64/liquid-dsp/files/)).
- Fedora 45 publishes `liquid-dsp-devel` 1.7.0, while Rawhide publishes 1.8.2;
  their official devel file lists likewise show the header and shared-library
  link, not pkg-config metadata
  ([Fedora 45](https://packages.fedoraproject.org/pkgs/liquid-dsp/liquid-dsp-devel/fedora-45.html),
  [Rawhide](https://packages.fedoraproject.org/pkgs/liquid-dsp/liquid-dsp-devel/fedora-rawhide.html)).
- Homebrew publishes 1.8.2 bottles for macOS and Linux and declares FFTW as a
  dependency ([formula metadata](https://formulae.brew.sh/formula/liquid-dsp)).
- Ubuntu's official package search currently returns no package named
  `liquid-dsp` in its listed suites
  ([search](https://packages.ubuntu.com/search?keywords=liquid-dsp&searchon=names&suite=all&section=all)).

Consequently, blindly using upstream's pkg-config name is not portable even
across distributions that package the library; a Makefile fallback or a
configure-time error would be extra project code.

## License

liquid-dsp is MIT-licensed: use, modification, distribution, sublicensing, and
sale are permitted, provided the copyright and permission notice accompany
copies or substantial portions
([LICENSE](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/LICENSE#L1-L19)).
That is permissive and does not impose a reciprocal license on this probe.
This repository is MIT-licensed (top-level `LICENSE`), which is compatible with
liquid-dsp's MIT terms; if liquid-dsp were vendored or statically linked, its
MIT copyright and permission notice would need to be retained alongside ours.

## Deterministic tests

The existing synthetic tests remain necessary: they define byte centering,
bin placement after shift, coherent-gain dBFS, linear-before-log averaging, and
odd/short-buffer safety. liquid-dsp's own tests verify FFT vectors, shift, and
periodogram noise behavior, but not this probe's 127.5-centered input or dBFS
definition ([FFT test](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/src/fft/tests/fft_transforms_autotest.c#L26-L65),
[periodogram test](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/src/fft/tests/spgram_autotest.c#L29-L89)).

Results can vary slightly by installed liquid-dsp version, SIMD build, and
internal-versus-FFTW backend because those are build-time choices
([options](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/CMakeLists.txt#L19-L38),
[backend wiring](https://github.com/jgaeddert/liquid-dsp/blob/v1.8.2/CMakeLists.txt#L642-L660)).
Tests should therefore use documented floating-point tolerances, check
`LIQUID_VERSION`/runtime compatibility if behavior depends on a version, and
never assert bit-identical spectra. This is less hermetic than compiling the
specified DSP source directly into the test binary.

## Approximate code tradeoff

These are implementation estimates for this fixed 2048-point pipeline, not
upstream line counts:

| Approach | Custom code removed | New/local code still required |
| --- | ---: | ---: |
| Low-level liquid FFT + shift | About **80-130 C LOC**: radix-2 bit reversal/butterflies, twiddle setup, and perhaps shift/window generation | About **45-75 C LOC** of plan/buffer lifecycle, Hann application/sum, coherent normalization, power average/max, floor, and error handling, plus pkg-config integration |
| `spgramcf`, reset/read per FFT window | About **110-160 C LOC**: FFT, Hann application, shift, and transform scheduling | About **45-80 C LOC** of object lifecycle, per-window reset/write/get loop, conversion by `sum(w^2)/(sum(w))^2`, average/max accumulation, floor, and errors |
| Fully custom specified module | No wrapper/dependency code | About **120-180 C LOC** for the small radix-2 spectrum core; all probe-specific conversion and display reductions are required in every approach |

Thus the realistic net saving is roughly **50-100 DSP LOC**, not an entire DSP
module. `spgramcf` saves slightly more mechanics but adds the highest semantic
risk: its output is plausibly labeled “PSD in dB” while being about 31.35 dB
away from this specification's unit-tone dBFS reference until corrected. For a
small probe whose tests are intended to lock down that exact convention, the
self-contained implementation is the clearer and more deterministic boundary.
