#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/audio_buffer.hpp"

TEST_CASE("AudioBuffer duration calculation", "[audio]") {
    mwaac::AudioBuffer buf;
    buf.sample_rate = 44100;
    buf.samples.resize(44100);  // 1 second
    
    REQUIRE_THAT(buf.duration_seconds(), 
                 Catch::Matchers::WithinAbs(1.0, 0.001));
}

TEST_CASE("Resampling doubles sample count when doubling rate", "[audio]") {
    mwaac::AudioBuffer input;
    input.sample_rate = 22050;
    input.samples.resize(22050);  // 1 second at 22050
    
    auto output = mwaac::resample_linear(input, 44100);
    
    REQUIRE(output.sample_rate == 44100);
    REQUIRE(output.samples.size() == 44100);  // Still 1 second
}