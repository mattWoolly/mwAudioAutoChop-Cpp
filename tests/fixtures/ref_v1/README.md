# ref_v1 — Synthetic vinyl rip with distinctive per-track envelopes

This fixture supplies the inputs for the reference-mode integration tests
that previously SKIP'd against `FIXTURE-REF` in `BACKLOG.md`.

## Why this exists

Reference-mode alignment combines waveform cross-correlation with an RMS
envelope cross-correlation pass (see `src/modes/reference_mode.cpp` —
`compute_rms_envelope`, `envelope_refine_start`). A constant-amplitude tone
has a flat envelope: every shift correlates equally well, so the envelope
peak is ambiguous and refinement either fails or wanders. The previous
tones-in-noise test fixture exhibited exactly that pathology, so the three
reference-mode integration cases stayed SKIP'd.

This fixture replaces the tones with band-limited pink noise *amplitude
modulated* by a per-track curve whose RMS envelope is unique among the
three tracks:

| Track | Envelope shape | Distinguishing feature |
|-------|---------------|------------------------|
| 1     | 1.5 s cosine fade-in, 8.5 s sustain at 0 dB, 2.0 s cosine fade-out | Smooth trapezoid |
| 2     | Instant onset, 4 s sustain, 0.3 s ramp down to -18 dB, 0.3 s recovery, sustain | Sharp drop in the middle |
| 3     | 1 Hz amplitude oscillation between 0 dB and -12 dB across the whole track | Periodic modulation |

The three envelopes have no overlap in their cross-correlation peaks; the
correct alignment is the global peak of `compute_rms_envelope` against the
vinyl rip.

## Layout

The generator binary `gen_ref_fixture_v1` writes, under `<out_dir>`:

- `vinyl.wav` — concatenated rip:
  `[1.5 s lead-in noise][T1][3.5 s gap][T2][3.5 s gap][T3]`
  Mono, 44100 Hz, PCM_16.
- `refs/01_track1.wav`, `02_track2.wav`, `03_track3.wav` — the per-track
  reference audio (no lead-in, no gap), same encoding.
- `manifest.txt` — flat `KEY=VALUE` ground truth: `track<N>_start_sample`,
  `track<N>_end_sample`, plus `sample_rate` and `num_tracks`.

The generator is deterministic: a fixed `kMasterSeed` drives all RNGs and
no wall-clock or filesystem state enters the output. Re-running it from a
clean build produces a byte-identical `vinyl.wav`.

The fixture is built once, on demand, by the CMake target
`ref_fixture_v1`. It is not a member of `ALL`, so a default build does
not pay for it. Tests that depend on it add the target as a build
dependency (see `tests/fixtures/ref_v1/CMakeLists.txt`).

## Invariants exercised

- **INV-REF-1** — *Position fidelity.* For every reference track in the
  fixture, `analyze_reference_mode` reports a `split_points[i].start_sample`
  within ±N samples of the manifest's `track<i>_start_sample`. The tolerance
  N is declared as a named constant in `tests/test_integration.cpp`
  (currently 1 ms at the analysis sample rate).
- **INV-REF-2** — *Lossless byte identity.* Given a reference-mode
  detection on this fixture, exporting the first track via `write_track`
  produces a file whose sample-data region is byte-identical to the
  source vinyl's sample bytes over the same `[start_sample, end_sample]`
  range. (PCM_16 mono — raw sample bytes are well-defined and do not
  depend on header alignment.)

## Backlog items

| ID | What |
|----|------|
| FIXTURE-REF | Defines and ships this fixture. |
| (consumes)  | The three `[integration][reference]` cases in
                `tests/test_integration.cpp` un-SKIP'd alongside the fixture. |

If a future invariant lands here, append it to the table above and to
`docs/invariants.md` (handled by the docs-agent).
