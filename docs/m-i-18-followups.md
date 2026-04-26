# Mi-18 follow-ups

Stub entries surfaced during the Mi-18 mechanical `-Werror` cleanup. Each item
is a candidate for promotion to `BACKLOG.md` as its own backlog entry. The
fix-agent intentionally did NOT fold these into the Mi-18 PR per the strict
scope rules — Mi-18 is casts/annotations/pragma-suppression only.

Format: `Mi-18-FU-<n>: <one-line summary>` followed by a short paragraph.

---

## Mi-18-FU-1: `resample_linear` mixes `float` samples with `double` interpolant

`src/core/audio_buffer.cpp:52-76`. The linear interpolator computes `src_pos`,
`frac` in `double`, then implicitly downconverts to `float` per sample. The
Mi-18 cure is to mark the casts `static_cast<float>(...)`. The right fix is
either (a) keep the entire kernel in `float` (cheap, fine for analysis-grade
linear interp), or (b) promote `AudioBuffer::samples` to `double` for resample
intermediates and round at the end. Either way the choice is design-level, not
a spot cast.

## Mi-18-FU-2: channel-averaging integer divide loses precision

`src/core/audio_buffer.cpp:40`. `sum / info.channels` is a `float / int`
divide; the `int` divisor implicit-converts to `float`. Mi-18 cures with
`static_cast<float>(info.channels)`. The deeper question is whether
`info.channels > 16777216` (i.e. > 2^24, the float mantissa) ever matters —
clearly not for audio — so the cure is fine, but the loop body itself is a
candidate for `std::accumulate` cleanup at some later refactor pass.

## Mi-18-FU-3: `output_size` ratio cast loses size_t precision past 2^53

`src/core/audio_buffer.cpp:56`. `input.samples.size() * ratio` promotes
`size_t` to `double`, losing precision past 2^53. For reasonable audio
buffers (<< 2^53 samples) this is fine, but the cast is silent. Mi-18 leaves
it spot-cured. A real fix would compute the output size with integer
arithmetic where possible.

## Mi-18-FU-4a: `compute_rms_energy(sample_rate)` documented vestigial

`src/core/analysis.cpp:8-12`. Header comment in `analysis.hpp:14-19` says
"For reference, not used in basic RMS". Mi-18 marks `[[maybe_unused]]`. The
backlog item is whether to (a) keep the param for future ABI stability and
suppress the warning permanently, or (b) drop it once consumers are updated
— either way, a separate signature change.

## Mi-18-FU-4b: `compute_spectral_flatness(sample_rate)` is a TODO stub

`src/core/analysis.cpp:75-85`. Function body is `// TODO: Implement with FFT`
and returns a vector of `0.5f`. The `sample_rate` param will be needed when
the FFT is added; until then `[[maybe_unused]]`. **Real backlog item:
implement spectral flatness.**

## Mi-18-FU-4c: `estimate_noise_floor(window_seconds)` REAL Mi-7 SMOKE

`src/core/music_detection.cpp:11-32`. Header comment at `music_detection.hpp:20`
says `window_seconds` is the "Window for searching quietest region". The body
uses fixed 50ms frames and a 10th-percentile RMS over the *entire* signal —
`window_seconds` is dropped on the floor. This looks like a real defect: the
caller is supplying a 2.0-second window expecting a sliding-window quietest
region, but the implementation does a global percentile. Mi-18 silences with
`[[maybe_unused]]`; **real backlog item to actually consume `window_seconds`
or remove it from the API.**

## Mi-18-FU-5: pocketfft size→long-double conversion warning

`src/core/pocketfft_hdronly.h:372` produces a `-Wimplicit-int-float-conversion`
when included by our TUs. Per Mi-18 scope (vendored header is OFF LIMITS), the
cure is a `#pragma GCC diagnostic` wrapper at the include site in
`src/core/correlation.cpp`. A long-term cleaner answer would be to wrap
pocketfft in a small `mwaac_fft.hpp` shim that performs the suppression in one
place, so other TUs that may include pocketfft in the future need not repeat
the pragma.

## Mi-18-FU-6: Catch2 `-Wdouble-promotion` / `-Wimplicit-int-float-conversion`

Several Catch2 internal headers (`catch_decomposer.hpp`,
`catch_matchers_impl.hpp`) emit warnings under our flag set. These leak into
test TUs because Catch2's templates are instantiated in our code. Mi-18 cures
this with pragma suppression around the Catch2 includes in test TUs that
trigger the warning. A cleaner answer is a single `tests/test_main.hpp` that
includes Catch2 with the suppression once, and have all tests use it — but
that's a refactor, out of scope here.

## Mi-18-FU-6b: unused helpers in reference_mode.cpp

`src/modes/reference_mode.cpp:208 (measure_fade_in_samples)` and
`src/modes/reference_mode.cpp:433 (estimate_noise_floor_db)` are no-longer-
called. Mi-18 marks them `[[maybe_unused]]`. They're plausibly worth keeping
as future hooks for fade-in-aware alignment and adaptive vinyl noise-floor
detection — but as long as they're untested and unwired, they are dead.
Either re-introduce a use site or delete in a separate cleanup pass.

## Mi-18-FU-7: unused test helpers (`read_file_bytes`, etc.)

`tests/test_integration.cpp` contains `read_file_bytes` and a few other
helpers that are no longer referenced. Mi-18 silences with
`[[maybe_unused]]`, but a future pass should either re-introduce a use site
or delete them. Likely the latter.

## Mi-18-FU-8: `vinyl_info` test variable read but unused

`tests/test_integration.cpp:644` binds `vinyl_info` from a result.value() but
never reads it. Mi-18 marks it `[[maybe_unused]]`. The deeper question is
whether the test was supposed to assert on `vinyl_info` and was abandoned
mid-edit; worth a sanity-check pass.

