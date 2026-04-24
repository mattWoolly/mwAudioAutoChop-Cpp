# FIXTURE-RF64

Hand-written RF64 corpus for testing parser edge cases and the C-3 / M-4
fixes.

## What it produces

`gen_rf64_fixture` (built from `generate.cpp`) writes three files into
`${CMAKE_BINARY_DIR}/fixtures/rf64/`:

| File | Layout | Purpose |
|------|--------|---------|
| `rf64_ds64_first.wav` | RF64 + ds64 + fmt + data | Canonical RF64 layout, the one libsndfile and most tools emit. Logical data size is just over 4 GiB. |
| `rf64_ds64_after.wav` | RF64 + fmt + data + ds64 | EBU-tolerated but unusual layout; trips M-4 in the current chunk walker. |
| `manifest.txt`        | KEY=VALUE | Payload region offsets and seeds, consumed by tests. |

Both `.wav` files are sparse — the on-disk footprint is a few KiB despite a
logical size of 4 GiB + 64 bytes. APFS, HFS+, ext4, btrfs, and xfs all
sparsify the `ftruncate(fd, BIG)` + `pwrite(small_region)` pattern this
generator uses.

Inside the sparse data region, two payload windows of 256 frames each
(1024 bytes per window) hold deterministic LCG-seeded bytes:

- Region A at data offset `0x1000` (4 KiB into the data region).
- Region B at data offset `0x100000000` (4 GiB exactly into the data region).

Region B is past the uint32 cliff on purpose: the round-trip in C-3 cannot
reach it without 64-bit-clean offset arithmetic.

## Invariants exercised

- **INV-RF64-1** — `parse_wav_header` recovers the correct `data_offset`
  and `data_size` for an RF64 file whose `ds64` chunk appears immediately
  after `WAVE`.
- **INV-RF64-2** — `parse_wav_header` recovers the same correct fields for
  an RF64 file whose `ds64` chunk appears AFTER the `data` chunk.
  *Currently violated by the production code; M-4 is the fix.*
- **INV-RF64-3** — `write_track` on an RF64 source produces output whose
  named payload region is byte-identical (SHA-256 equal) to the source's
  same payload region. *Currently violated; C-3 is the fix.*

## Backlog items dependent on this fixture

- **C-3** — RF64 output silently truncated above 4 GiB. The round-trip
  test will go from `[!shouldfail]` to passing once C-3 lands.
- **M-4** — RF64 data placeholder confuses chunk walker. The
  ds64-after-data parsing test will go from `[!shouldfail]` to passing
  once M-4 lands.

## Forbidden short-cuts (and why)

- **Don't switch the generator to libsndfile.** libsndfile picks its own
  RF64 layout and never emits the ds64-after-data variant. Using it would
  defeat the M-4 trigger entirely.
- **Don't commit the .wav files into the repo.** They are 4 GiB +
  logically; even sparse, they would bloat clones the moment a single
  filesystem in the toolchain doesn't preserve sparseness (e.g., during
  zip extraction in CI). Generating at build time costs ~50 ms.
- **Don't widen the LCG seed namespace silently.** Tests pin the seeds
  by value in `manifest.txt`. Changing seeds without updating the test
  expected values means the round-trip "passes" trivially against zero
  bytes.

## Determinism

The generator is deterministic: same `generate.cpp` ⇒ byte-identical
output. Seeds are fixed constants. There is no `std::random_device`,
`time(nullptr)`, or similar. Re-running the generator overwrites prior
output (`O_TRUNC`), so a corrupt prior run never lingers.
