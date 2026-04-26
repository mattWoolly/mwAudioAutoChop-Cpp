---
name: Tier 1 + Tier 2 walk-and-merge plan
description: Step-by-step sequence for rebasing and merging PRs #23-#29 onto post-Mi-18 main, with predicted conflicts, halt rules, and local-verification commands. Hand to the rebase fix-agent on dispatch so the shape is fixed up-front.
type: project
---

# Walk #23–#29 onto post-Mi-18 main: rebase, verify, merge

**Generated:** 2026-04-26 while waiting on PR #30 (Mi-18) macOS CI to complete.

**Trigger:** This plan executes only after PR #30 (Mi-18) has merged to main and main's CI is fully green on every job. If main is not green, halt and re-evaluate.

## Why pre-stage this

The user's standing instruction: "make the next dispatch unambiguous instead of discovering shape mid-flight" (same pattern as `docs/m14-scope.md`). Each rebase + merge is a serialized step; predicting conflicts and codifying halt rules in advance prevents the fix-agent from improvising on contact.

## Universal rules (apply to every step)

### CI green definition — the post-rebase gate

A PR's CI is **green for merge purposes** when **all three** hold:

1. Every `build / *` and `sanitizers (asan+ubsan)` job's compile step succeeds. This is the strict build-clean signal Mi-18 was dispatched to produce.
2. The set of failing tests on the PR is a **strict subset of `docs/known-failing-tests.md`** — same test name, same line, same failure mode. New failures that don't appear on the known-failing list are regressions.
3. **No test passes on main and fails on the PR.** Run main's most recent test pass before merging the PR; diff. If a test that's PASS on main is FAIL on the PR, halt and surface — that's the regression case the gate exists for.

`clang-tidy` red is out of scope for the merge gate per the standing Mi-18 mandate (it tracks style rules separately under N-1..N-12 / Mi-18-FU-*). Do **not** wait for clang-tidy green on this walk.

When a PR cures a known-failing entry, the merging commit must also move the corresponding entry in `docs/known-failing-tests.md` from Active to Resolved. This keeps the gate-definition file accurate as the queue progresses.

### Halt rules — non-negotiable

1. **Pending CI is not actionable.** Treat pending the same as red. Wait for completion before merging.
2. **A test failure that is NOT on `docs/known-failing-tests.md` halts the merge.** This is the operative regression check; it replaces the previous "documented pre-existing" gloss, which was ambiguous when main itself couldn't compile. The known-failing doc is now the authoritative pre-existing list.
3. **Conflicts that aren't mechanical halt.** "Mechanical" means: each side's intent is preserved by textual interleaving (e.g. two casts at different lines in the same function). "Non-mechanical" means: each side's intent is incompatible (e.g. one rewrites a function the other casts inside). Non-mechanical conflicts halt and surface — the fix-agent does not get to pick winners on its own authority.
4. **Force-push requires `--force-with-lease`, never `--force`.** Rebases rewrite branch history; `--force-with-lease` protects against overwriting concurrent pushes.
5. **Do not merge anything if main itself goes red between steps.** A red main mid-walk halts the entire walk until main is investigated. "Red main" means main violates the CI green definition above — not just "any job has a red dot."
6. **A new test failure on the PR that becomes a known-failing entry after the fact requires explicit user authorization.** If you discover the failure is acceptable (e.g. it's the side-effect of a fixture landing earlier in the walk), update the known-failing doc as a separate orchestrator-paperwork commit on main first, then proceed with the merge. Do not retroactively rationalize regressions into the known-failing list inside the merging PR itself.

### Per-PR loop

For each PR in order:

```bash
# 1. Sync main and confirm it is green.
git fetch origin
git checkout main
git pull --ff-only origin main
gh run list --branch main --limit 1 --json conclusion,databaseId
# Expect: conclusion = "success". If "failure", halt.

# 2. Rebase the PR branch onto current main.
WORKTREE=/tmp/mwaac-rebase-$PR_NUM
BRANCH=remediation/<slug>
git worktree add $WORKTREE $BRANCH
cd $WORKTREE
git rebase origin/main

# 3. If conflicts: resolve per the per-PR notes below. If non-mechanical, halt.

# 4. Local verification before push.
cmake -B build -DCMAKE_BUILD_TYPE=Release -DMWAAC_WERROR=ON
cmake --build build 2>&1 | tee build.log
# Expect: 0 errors, 0 warnings.

cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug -DMWAAC_WERROR=ON -DMWAAC_SANITIZE=ON
cmake --build build-debug 2>&1 | tee build-debug.log
# Expect: 0 errors, 0 warnings.

ctest --test-dir build --output-on-failure | tee tests.log
ctest --test-dir build-debug --output-on-failure | tee tests-debug.log
# Expect: only pre-existing failures (compare against the pre-merge main baseline).

# 5. Push (force-with-lease, since rebase rewrote history).
git push --force-with-lease origin $BRANCH

# 6. Wait for fresh CI on the PR.
gh pr checks $PR_NUM --watch
# When all jobs complete: check conclusion. If not all "success", halt.

# 7. Merge.
gh pr merge $PR_NUM --merge --delete-branch
# Use --merge (not --squash or --rebase) to preserve the per-commit narrative
# the fix-agent and audit-agent established. Per-commit subjects are
# load-bearing for Phase 4 reconciliation.

# 8. Clean up the worktree.
cd /Users/mwoolly/projects/mwAudioAutoChop-C-
git worktree remove $WORKTREE
```

### Local-verification baseline tracking

The **authoritative baseline** is `docs/known-failing-tests.md` (Active section). The local `ctest` run at each step compares against this file, not against an empirically-captured snapshot. This pulls the baseline definition into reviewable code rather than letting it live as untracked output in `/tmp`.

```bash
git checkout main
git pull --ff-only origin main
cmake -B build -DCMAKE_BUILD_TYPE=Release -DMWAAC_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure 2>&1 | tee /tmp/main-tests-current.txt

# Diff actual failures vs the doc's Active section.
# If anything in /tmp/main-tests-current.txt isn't covered by the doc, halt.
```

After each PR merges, two things happen in parallel:
1. The PR's known-failing entries move to Resolved in `docs/known-failing-tests.md` (must be part of the merging commit or a single-line follow-up commit on main, never deferred).
2. The local baseline is implicitly updated by virtue of the doc change. The next rebase reads the post-merge doc.

The expected per-PR delta is documented under each PR's section below — verify it against the doc movement.

---

## Tier 1 — fixture PRs (independent of each other)

Tier 1 PRs are independent. Merge order can be any of #23 / #24 / #25 / #26 — they do not conflict with each other in source files (each lives under its own `tests/fixtures/<slug>/` subdirectory, and the parent `tests/fixtures/CMakeLists.txt` uses `file(GLOB)` auto-discovery so no per-fixture edits cascade up). However, recommend merging in numeric order for narrative clarity.

### PR #23 — FIXTURE-REF

- **Predicted conflicts.** None against post-Mi-18 main. The fixture lives under `tests/fixtures/ref_v1/`. The only mainline-touching change is un-SKIP-ing three test cases in `tests/test_integration.cpp`, and Mi-18 touched test_integration only for cast-cures at `phase` / `vinyl_info` sites which are in different test cases.
- **Verify.** After merge, the three previously-SKIP'd reference-mode test cases should now run and pass. If they SKIP, the fixture isn't being discovered — halt.
- **Test-baseline delta:** tests that previously emitted SKIP now PASS. **Negative delta on the failure set is good.**

### PR #24 — FIXTURE-RF64

- **Predicted conflicts.** Possibly minor in `tests/test_lossless.cpp` — both Mi-18 (`read_file_bytes` `[[maybe_unused]]` at line 17) and #24 (RF64 round-trip test) edit this file. Mechanical: re-apply Mi-18's `[[maybe_unused]]` annotation if the rebase displaces it.
- **Verify.** New tests `Lossless: RF64 round-trip preserves sample bytes`, `RF64 header parsing: ds64 before data`, `RF64 header parsing: ds64 after data` should run and pass.
- **Test-baseline delta:** new PASS-ing tests added to the suite. Failure set unchanged or shrinking (FIXTURE-RF64 doesn't fix any prior tests).

### PR #25 — FIXTURE-WAVEEXT

- **Predicted conflicts.** None expected against post-Mi-18 main; lives entirely under `tests/fixtures/waveext/`. `tests/test_integration.cpp`'s NEW-WAVEEXT-WRITE test reference (line 691 area) was Mi-18-touched only for surrounding casts.
- **Verify.** New WAVE_FORMAT_EXTENSIBLE 24-bit fixture loads. Note: the *write* path for WAVE_FORMAT_EXTENSIBLE 24-bit is broken (NEW-WAVEEXT-WRITE / M-3); FIXTURE-WAVEEXT is read-only by design until M-3 lands. The new tests should expect the write to fail in a controlled way, not pass it.
- **Test-baseline delta:** new tests added; `tests/test_integration.cpp:691`'s `export_result.has_value()` may now run more often but should fail in the same documented pattern.

### PR #26 — FIXTURE-MALFORMED

- **Predicted conflicts.** Possibly trivial in `tests/test_audio_file.cpp` — Mi-18 added `[[maybe_unused]]` on `create_test_wav` at line 10. #26 adds parser-hardening test cases that compile-time-include the malformed-fixture manifest. Different parts of the file. Re-apply mechanically if displaced.
- **Verify.** The 14 hand-crafted malformed blobs all load and produce one of `InvalidFormat` / `UnsupportedFormat` (per `docs/deviations.md`'s `FIXTURE-MALFORMED` deviation). New parser-hardening tests should pass.
- **Test-baseline delta:** new PASS-ing tests added.

After Tier 1 lands, regenerate the test baseline. Expected: test count grows; failure count stays flat or shrinks (FIXTURE-REF un-SKIPs three previously-passing-by-virtue-of-being-skipped cases that now actually run; if they regressed, halt).

---

## Tier 2 — sequential, halts on red at every step

Tier 2 PRs are NOT independent. They all touch `src/core/audio_file.cpp` and/or `audio_file.hpp` in overlapping regions. Strict serial; each rebase happens against the previous merge's main.

### PR #27 — C-1 (encode_float80 + AIFF round-trip)

- **Predicted conflicts.** Minor against post-Mi-18 main. C-1 touches `encode_float80` (function rewrite; far from anything Mi-18 cured), `build_aiff_header` (the `numSampleFrames` field-type fix; in the same function as Mi-18's `bytes_per_frame` `[[maybe_unused]]` at line 619 — but `bytes_per_frame` is in `write_track`, not `build_aiff_header`, so different functions), and `tests/test_lossless.cpp` (six-rate AIFF round-trip — Mi-18's edit there was `read_file_bytes` `[[maybe_unused]]`, distant). Expect mechanical resolution.
- **Resolution policy if conflicts arise.** Take both sides: C-1's structural changes plus Mi-18's casts/annotations on whichever lines survive. If C-1's diff fully replaces a Mi-18-touched line, drop Mi-18's annotation on that line (no longer applicable).
- **Verify locally.** Build green Release+WERROR. Build green Debug+WERROR+SANITIZE. The 6-rate AIFF round-trip test passes (44.1, 48, 88.2, 96, 176.4, 192 kHz). ASan does NOT trip on the previously-stack-smash-prone `encode_float80`.
- **CI signal expected.** Linux Build green; macOS Build green; sanitizers Build+Test green; clang-tidy may still red on pre-existing nits (Mi-18's mandate excluded clang-tidy). Test failures: shrinking — the disabled AIFF tests now pass.
- **Halt rule specific to C-1.** If any of the 6-rate AIFF round-trip tests fails on either Linux or macOS, halt — the inline-scope fix on `build_aiff_header` (`numSampleFrames` u32) is what makes libsndfile read the output, and if it's broken on a particular platform's libsndfile build, audit-2's verification was insufficient.

### PR #28 — M-16 (atomic write_track via temp-sibling + rename)

- **Predicted conflicts.** Likely in `src/core/audio_file.cpp` against post-#27 main. M-16 rewrites `write_track`'s body. C-1's `build_aiff_header` is called from `write_track`. The two PRs touched adjacent or interleaved regions. Likely mechanical — preserve C-1's call to the corrected `build_aiff_header`, wrap it in M-16's temp-sibling+rename idiom.
- **Resolution policy.** M-16's `make_temp_sibling_path` + `std::filesystem::rename` idiom is the structure; C-1's payload-writing logic (the AIFF/WAV header byte stream) is the body called from inside that structure. If a conflict puts C-1's `build_aiff_header` outside M-16's transactional region, that's a logic conflict — halt.
- **Verify locally.** The M-16 audit-1-rejected concurrent-stress test (≥8 threads, 54-char filename, 6400 calls) must still produce 0 corrupt files. ASan must not trip on the temp-sibling rename path.
- **CI signal expected.** Linux Build green; macOS Build green; sanitizers Build+Test green. Test failures: roughly flat vs post-#27 main; M-16 adds the atomic-write regression test which should pass.
- **Halt rule specific to M-16.** If the concurrent-stress regression test produces ANY corrupt files (size below WAV header size at the target path), halt — that was the audit-1 failure mode and it must stay cured through the rebase.

### PR #29 — C-2 (Expected precondition guards + main.cpp call-site fixes)

- **Predicted conflicts.** Likely in `src/main.cpp` against post-#28 main. Mi-18 added casts at several main.cpp call sites. C-2 rewrote all three CLI branches (reference, blind, tui) with the canonical `if (!opened) return 1; auto& audio_file = opened.value();` pattern. The casts Mi-18 added may be inside or adjacent to the rewritten regions.
- **Resolution policy.** **Take C-2's structural rewrite as the structure; re-apply Mi-18's casts on the lines that survive.** The casts are the cheaper change to redo; the structural rewrite carries the M-15 subsumption. If a Mi-18 cast was on a line C-2 deleted entirely (e.g. `audio_file.value().info().frames` cast that's no longer needed because `audio_file` is now a guaranteed-valid reference), drop the cast — it became unreachable. **This is the conflict most likely to require careful judgment**; if anything looks ambiguous, halt and surface.
- `audio_file.hpp` should be a clean rebase — Mi-18 didn't touch it and M-16 didn't either.
- `tests/test_audio_file.cpp` may have a tiny conflict: Mi-18 added `[[maybe_unused]]` on `create_test_wav` at line 10; C-2 added the new `Expected: value()-on-error abort` death-test scaffolding. Different parts of the file; mechanical.
- **Verify locally.** The new C-2 death tests must pass under the standard build AND under sanitizers. The subprocess test (`main: failed AudioFile::open exits cleanly`) must pass — this is the M-15 subsumption proof. `MWAAC_TEST_BINARY_PATH` wiring in `CMakeLists.txt` must survive (it's how the subprocess test finds the main binary).
- **CI signal expected.** Linux Build green; macOS Build green; sanitizers Build+Test green; the death tests must not trip ASan/UBSan in unexpected ways.
- **Halt rule specific to C-2.** After merge, run `grep '\.value()' src/main.cpp` on post-#29 main and confirm 0 unguarded `.value()` calls remain. This is the M-15 subsumption invariant; if it regresses through the rebase, halt and surface — the M-15 status in BACKLOG.md depends on it.

---

## Post-walk verification

After PR #29 merges, **before declaring Tier 2 complete and dispatching M-14**:

1. Capture the post-#29 test baseline:
   ```bash
   git checkout main && git pull --ff-only origin main
   cmake -B build -DCMAKE_BUILD_TYPE=Release -DMWAAC_WERROR=ON
   cmake --build build
   ctest --test-dir build --output-on-failure | tee /tmp/post-tier2-tests.txt
   ```
2. Verify the cumulative test-failure delta:
   - **Should be PASSING that previously failed/skipped:** AIFF round-trip tests (Tier 2 C-1), reference-mode tests (Tier 1 FIXTURE-REF), RF64 round-trip tests (Tier 1 FIXTURE-RF64), atomic-write regression test (M-16), Expected death tests (C-2), main subprocess test (C-2).
   - **Still failing (out of Tier 1+2 scope):** NEW-BLIND-GAP, NEW-WAVEEXT-WRITE (M-3), Mi-1 (parse_aiff_header sample_rate decode), and any test that depends on later Tier 4+ items.
3. Verify M-15 subsumption: `grep '\.value()' src/main.cpp` returns only guarded usages.
4. Update `BACKLOG.md` to mark each merged item `[x] closed` with PR links.
5. Push the orchestrator paperwork commit on main.
6. **Then dispatch M-14** with `docs/m14-scope.md` as input (per the existing M-14 task #27).

## What this plan does NOT cover

- Tier 4+ items (parser hardening, algorithmic correctness). Separate plan when reached.
- M-14 dispatch mechanics. Covered by `docs/m14-scope.md`.
- Mi-18's defensive-cleanup follow-up `MI18-FOLLOWUP-BLIND-ITER`. That's a ≤ 5-line cast and rides along with whichever Tier 4+ blind-mode work picks it up.
- `MI18-FU-1..FU-8` from `docs/m-i-18-followups.md` (introduced by Mi-18 PR #30 and now on main). Each is a separate small backlog item; promote to `BACKLOG.md` in the Phase 4 reconciliation pass.

## Halt-and-surface protocol

When any halt rule triggers, the rebase fix-agent must:

1. Stop the walk immediately. Do not continue to the next PR.
2. Capture the failure state: which PR, which step (rebase / verify / push / CI / merge), the relevant logs, the conflicting files (if any), the failing tests (if any).
3. Surface to the orchestrator with: (a) what halt rule triggered, (b) the failure state, (c) the candidate paths forward (resolve & retry / abandon this PR / split into a sub-PR / dispatch a fix-agent for the conflict).
4. **Do not pick a path on the agent's own authority.** The orchestrator (and ultimately the user) decides.
