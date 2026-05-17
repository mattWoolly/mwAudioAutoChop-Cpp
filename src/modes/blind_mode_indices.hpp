#pragma once
//
// M-6: scoped phantom-typed sample/frame index types for blind mode.
//
// This header is INTERNAL to the blind-mode TU (blind_mode.cpp) and its
// unit tests (test_blind_mode.cpp). It is NOT included from
// `modes/blind_mode.hpp` and is NOT part of any public API surface; the
// public functions (analyze_blind_mode, detect_gaps, score_gap) keep
// their raw `size_t` / `int64_t` parameter types unchanged so callers
// outside the blind-mode TU need not adopt the typed bridge.
//
// Why it exists. detect_gaps returns frame indices; score_gap takes
// sample indices; the bridge between them is a multiplication by
// hop_length at the analyze_blind_mode call site. Pre-M-6 those two
// indexing systems were both `size_t` and the conversion was an
// untagged arithmetic expression — any future change that accidentally
// passed `gap.first` (a frame index) where a sample index was expected
// (or vice versa) would compile silently and produce wrong-by-a-factor-
// of-hop_length sample offsets. M-6 introduces `SampleIdx` and
// `FrameIdx` as opaque structs with no implicit conversions, no
// arithmetic, no comparisons; the ONLY way to cross between them is
// the `frame_to_sample` bridge below.
//
// Why scoped (not project-wide). The sample-vs-frame confusion only
// arises at this one bridge site in the codebase. A project-wide
// SampleIndex / FrameIndex API would require updating ~20+ callsites
// across all modes for a localized concern; the BACKLOG M-6 entry
// explicitly allows the "or at minimum" minimum-blast option, and the
// user-authorized middle-ground for this dispatch is "tagged types
// scoped to blind_mode internals" — compile-time safety where it
// matters, no project-wide API churn.
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
