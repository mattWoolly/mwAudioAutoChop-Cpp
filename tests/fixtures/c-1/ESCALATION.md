# C-1 escalation notes

Bugs found in `src/core/audio_file.cpp` while fixing C-1 (encode_float80
buffer overrun). Mostly out of C-1's stated scope, but listed here for
follow-up triage. The orchestrator should decide which (if any) need
their own backlog entries.

- `build_aiff_header` previously emitted `numSampleFrames` as a 10-byte
  float80 even though the AIFF 1.3 spec defines it as a 4-byte big-endian
  u32 (and the same function declares `comm_size = 18`, which only
  balances when `numSampleFrames` is u32). This caused libsndfile to
  reject every AIFF file the program wrote. **Fixed inline as part of
  C-1**, because the C-1 round-trip test required a libsndfile-readable
  AIFF; without the fix the new round-trip test could not be written
  honestly. Worth a follow-up audit-only entry to confirm no other
  callers depend on the broken layout.

- `parse_aiff_header` (audio_file.cpp:340) does not decode `numSampleFrames`
  or `sampleRate` from the COMM chunk — it just leaves those fields zero
  and "lets libsndfile validate later". This is Mi-1 and intentionally
  out of scope.

- `encode_float80`'s clamping behaviour for sub-/over-flow values silently
  pins the exponent and writes a denormal-shaped pattern. Acceptable for
  AIFF sample rates (44.1k–192k all fit comfortably) but worth a unit
  test if the helper is ever reused.
