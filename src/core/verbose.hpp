#pragma once

#include <iostream>
#include <chrono>
#include <string>

namespace mwaac {

// Global verbose flag - set by CLI
inline bool g_verbose = false;

// Start timing for a named operation
inline std::chrono::steady_clock::time_point g_timer_start;

// Verbose output to stderr (so stdout remains clean for structured output)
inline void verbose(const std::string& msg) {
    if (g_verbose) {
        std::cerr << msg << "\n";
    }
}

// Verbose section header
inline void verbose_section(const std::string& title) {
    if (g_verbose) {
        std::cerr << "\n=== " << title << " ===\n";
    }
}

// Verbose timing helper
class VerboseTimer {
public:
    VerboseTimer(const std::string& name) 
        : name_(name)
        , start_(std::chrono::steady_clock::now()) 
    {}
    
    ~VerboseTimer() {
        if (g_verbose) {
            auto end = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start_).count();
            std::cerr << "  [TIMING] " << name_ << ": " << duration << "ms\n";
        }
    }
    
private:
    std::string name_;
    std::chrono::steady_clock::time_point start_;
};

// Set verbose mode
inline void set_verbose(bool v) {
    g_verbose = v;
}

// Get verbose mode
inline bool is_verbose() {
    return g_verbose;
}

} // namespace mwaac