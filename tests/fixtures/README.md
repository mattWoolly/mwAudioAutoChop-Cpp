# Test Fixtures

Reproducible test inputs for the integration suite. Every fixture in this
tree is generated from a script checked into the same directory; binary
artifacts must not be committed without an accompanying regenerator and a
SHA-256 manifest.

## Contract for a fixture subdirectory

Each `tests/fixtures/<id>/` provides:

1. `CMakeLists.txt` — declares either an executable that *generates* the
   fixture at build time, or a list of pre-checked-in artifacts (with
   accompanying SHA-256 verification). Connected via `add_subdirectory()`
   from `tests/fixtures/CMakeLists.txt`.

2. `README.md` — names the invariants this fixture exercises by ID
   (e.g. "FIXTURE-REF supports `analyze_reference_mode` correctness tests
   for invariants INV-REF-1 and INV-REF-2"). Lists the backlog items that
   depend on the fixture.

3. **Generator code or hand-crafted artifacts**, plus a manifest.

Fixtures that produce >100 KB of binary output should generate at build
time, not commit blobs. Fixtures whose value is in being byte-precise
(e.g. malformed-header corpus) commit small handcrafted blobs alongside
a `Makefile` or shell snippet showing how each was constructed.

## Existing fixtures

| ID | Path | Exercises | Backlog items |
|----|------|-----------|---------------|
| _none yet_ | | | FIXTURE-REF, FIXTURE-RF64, FIXTURE-WAVEEXT, FIXTURE-MALFORMED |

This table is updated as fixtures land.

## Why a CMake target instead of a Python/shell generator

C++ test code already links libsndfile, std::filesystem, and the project's
own `audio_file.cpp`. Generators written in C++ can produce fixtures that
exercise edge cases the project's own writer is supposed to support
(e.g., RF64 sparse files, WAVE_FORMAT_EXTENSIBLE). Avoiding a second
language for fixture build means the toolchain is identical.
