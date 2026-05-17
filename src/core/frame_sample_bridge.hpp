#pragma once
//
// M-6 / M-MUSIC-DETECT-FRAME-SAMPLE-BRIDGE: phantom-typed sample/frame
// index types for the frame×hop_length bridge sites in `src/core/` and
// `src/modes/`.
//
// This header is shared between callers in `core/` (currently
// `music_detection.cpp`) and callers in `modes/` (currently
// `blind_mode.cpp`). It lives in `core/` rather than `modes/` because
// the project's intended include-graph layering puts `core/` below
// `modes/` (one vestigial exception exists at `src/core/drift_model.cpp:2`
// which includes `modes/reference_mode.hpp`; that include appears
// unused and is filed for cleanup, not cited as precedent). It is
// NOT included from any PUBLIC header (`blind_mode.hpp`,
// `music_detection.hpp` keep their raw `size_t` / `int64_t` parameter
// types unchanged) — adoption is internal to each TU that crosses the
// frame↔sample boundary.
//
// History.
//  - M-6 (PR #52) introduced the types and bridge scoped to blind-mode
//    internals at `src/modes/blind_mode_indices.hpp`. The user-authorized
//    "scoped" cure choice deliberately limited M-6 to one TU pending
//    sibling sweep results.
//  - M-MUSIC-DETECT-FRAME-SAMPLE-BRIDGE (PR #53) hoisted the header
//    from `modes/` to `core/` to enable adoption from
//    `core/music_detection.cpp` without the architectural inversion.
//    Bridge types unchanged; only the include path moved.
//
// Why it exists. Multiple callers in the codebase convert frame indices
// to sample positions via multiplication by a per-frame hop length (or
// frame size). The two indexing systems were both raw `size_t` /
// `int64_t` and the conversion was an untagged arithmetic expression
// — any change that accidentally passed a frame variable where a sample
// variable was expected (or vice versa) would compile silently and
// produce wrong-by-a-factor offsets. The phantom-typed `SampleIdx` and
// `FrameIdx` are opaque structs with no implicit conversions, no
// arithmetic, no comparisons; the ONLY way to cross between them is
// the `frame_to_sample` bridge below.
//
// Scope. The bridge currently serves the RMS-frame ↔ sample boundary
// (blind_mode and music_detection share the same hop_length semantics).
// A SECOND bridge for envelope-frame ↔ sample (different unit definition,
// different hop) is filed as M-REF-FRAME-SAMPLE-BRIDGE; it will likely
// introduce parameterized types (EnvFrameIdx<…>) rather than reuse
// FrameIdx directly, since the units differ.
//
// Compile-time tests at the bottom of this header verify the types
// reject implicit construction, mutual conversion, and raw-int
// conversion — i.e., the cure cannot silently regress without a
// static_assert failure at TU build time.

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace mwaac::detail {

// Frame index into the RMS frame vector produced by compute_rms_energy
// (frame size = 50 ms, hop = 12.5 ms at analysis_sr). Opaque construction
// only; no arithmetic, no implicit conversion.
struct FrameIdx {
    std::size_t value;
    explicit constexpr FrameIdx(std::size_t v) noexcept : value(v) {}
};

// Sample index into the AudioBuffer::samples vector at native rate.
// Opaque construction only; no arithmetic, no implicit conversion.
struct SampleIdx {
    std::int64_t value;
    explicit constexpr SampleIdx(std::int64_t v) noexcept : value(v) {}
};

// The bridge: convert a frame index to a sample index via the frame
// hop length. This is the ONLY supported FrameIdx → SampleIdx
// conversion path; constructing a SampleIdx from a FrameIdx by any
// other means (e.g. `SampleIdx{f.value}`) compiles but is semantically
// wrong (loses the hop-length multiplication). Reviewers must reject
// such constructions; the bridge function name makes the intent
// explicit at the callsite.
[[nodiscard]] inline constexpr SampleIdx frame_to_sample(
    FrameIdx f, int hop_length) noexcept
{
    return SampleIdx{static_cast<std::int64_t>(f.value) *
                     static_cast<std::int64_t>(hop_length)};
}

// Compile-time contract tests. Verify the types are NOT implicitly
// constructible from each other (which would defeat the cure) and NOT
// implicitly convertible from raw integers (which would let untagged
// arithmetic slip through). If a future change relaxes these
// guarantees (e.g. removes `explicit` on a constructor), the build
// fails here with a named static_assert rather than silently regressing
// the M-6 cure.
static_assert(!std::is_constructible_v<SampleIdx, FrameIdx>,
              "M-6 invariant: SampleIdx must not be constructible from "
              "FrameIdx — use frame_to_sample(f, hop_length) instead.");
static_assert(!std::is_constructible_v<FrameIdx, SampleIdx>,
              "M-6 invariant: FrameIdx must not be constructible from "
              "SampleIdx — the inverse direction is not supported.");
static_assert(!std::is_convertible_v<std::int64_t, SampleIdx>,
              "M-6 invariant: SampleIdx must require explicit construction "
              "from int64_t to prevent untagged-int leakage.");
static_assert(!std::is_convertible_v<std::size_t, FrameIdx>,
              "M-6 invariant: FrameIdx must require explicit construction "
              "from size_t to prevent untagged-int leakage.");
static_assert(!std::is_convertible_v<SampleIdx, std::int64_t>,
              "M-6 invariant: SampleIdx must require explicit .value access "
              "to extract the raw int (no implicit decay).");
static_assert(!std::is_convertible_v<FrameIdx, std::size_t>,
              "M-6 invariant: FrameIdx must require explicit .value access "
              "to extract the raw int (no implicit decay).");

} // namespace mwaac::detail
