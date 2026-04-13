// Test that dependencies are properly linked
#include <sndfile.h>
#include <ftxui/component/component.hpp>
#include <catch2/catch_test_macros.hpp>

// This file is not compiled into the main binary
// It's just to verify headers are accessible