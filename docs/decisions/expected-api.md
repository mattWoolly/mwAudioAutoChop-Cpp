# Decision — `Expected<T,E>` API shape

- **Decided in.** C-2 (the `Expected::value()` precondition fix).
- **Status.** Carried out in M-14. The placement-new layout has been replaced
  by `std::variant<T, E>`; the public API surface is unchanged.
- **Carried out by.** M-14 (contract unification with the load-specific
  result wrapper, plus storage rewrite).

## Decision

Keep the existing placement-new-in-aligned-storage layout for `Expected<T,E>`
through the C-2 fix. Add precondition checks (`assert` in Debug,
`std::terminate` in Release) on `value()` and `error()`. **Do not** migrate to
a `std::variant<T, E>`-backed implementation in C-2.

The migration to `std::variant` (or to `std::expected` once we move to C++23)
is M-14's responsibility.

## Reasoning

C-2 is scoped to *signalling* the UB in the existing type, not to redesigning
it. The two viable shapes considered:

1. **Status quo + precondition check** (chosen). Minimal diff: ~12 lines in
   `audio_file.hpp`, plus call-site guards in `main.cpp`. Strictly a superset
   of correct behaviour: every previously-broken access becomes a noisy
   abort, every previously-correct access continues to compile and run with
   identical semantics. Audit cost is bounded.

2. **Migrate to `std::variant<T, E>`** (rejected for C-2; deferred to M-14).
   Removes `reinterpret_cast` from `Expected` entirely, fixes the latent
   `[basic.life]/8` / `std::launder` standard-conformance concern, and
   gives us copy/move/destruction for free. But it (a) blows up the C-2
   diff into the territory M-14 is meant to own, (b) front-runs M-14's own
   audit pass on the unified contract API, and (c) couples C-2's land
   sequence to a much larger redesign. M-14 is the right place for this.

## What this decision leaves on the table

The placement-new + `reinterpret_cast` pattern is technically UB per
`[basic.life]/8` unless `std::launder` is used on the return path. The C-2
precondition fix narrows the *behavioural* hazard (no UB on errored value
access) but does **not** resolve the standard-conformance concern. M-14
will eliminate it by replacing the storage. Until M-14 lands, the
precondition-checked accessors are good-citizen on every compiler we
target (libc++ and libstdc++ on Clang/GCC), but the codebase carries a
known latent issue documented here.

## Audit override

Audit pass on C-2 will challenge this decision. If the audit decides that
variant migration is required at C-2 (e.g. because UBSan flags the
`reinterpret_cast` paths under `-fsanitize=vptr` or because the latent
`[basic.life]` concern is judged unacceptable to carry until M-14), this
document is rewritten and M-14 is folded into C-2. Default position
otherwise: the precondition-check version lands at C-2; M-14 follows.

## M-14 update — variant migration completed

M-14 carried out path #2 above. The `Expected<T,E>` storage now uses
`std::variant<T, E>` directly (alternative 0 = T, alternative 1 = E).
The accessors keep the same names and reference categories; the
precondition-check macro from C-2 is preserved on `value()` / `error()`,
so the four C-2 death tests (`tests/test_audio_file.cpp`
TEST_CASEs `"Expected: value() on errored Expected aborts the
process"`, `"Expected: error() on value-bearing Expected aborts the
process"`, `"Expected: documented contract — has_value() gates
value()"`, and `"main: failed AudioFile::open exits cleanly (no
crash)"`) all pass unchanged.

Merge of the load-specific error enum into `AudioError` was done via
option (a) in `docs/m14-scope.md`: `AudioError::ResampleError` was
added so the unified taxonomy is a strict superset of the previous
load-specific values. No information is lost.

Thread-safety and moved-from semantics are now documented in the
`Expected<T,E>` class comment in `src/core/audio_file.hpp`.
