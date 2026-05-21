#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <variant>

namespace mwaac {

// Evidence value can be numeric or string
using EvidenceValue = std::variant<double, std::string>;

struct SplitPoint {
    int64_t start_sample{0};    // First sample of track (0-indexed)
    int64_t end_sample{0};      // Last sample of track (inclusive)
    double confidence{0.0};     // 0.0 to 1.0
    // Descriptive provenance from the algorithmic detection pipelines
    // (set at marker construction by `src/modes/blind_mode.cpp` and
    // `src/modes/reference_mode.cpp`); cleared by TUI boundary-shift
    // nudges in `src/tui/app_handlers.cpp` (Mi-NUDGE-EVIDENCE-STALENESS).
    // Otherwise persistent through the marker's lifetime.
    std::map<std::string, EvidenceValue> evidence;
    std::string source;         // "reference" or "blind"
    
    [[nodiscard]] int64_t duration_samples() const noexcept {
        return end_sample - start_sample + 1;
    }
};

} // namespace mwaac