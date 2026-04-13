cmake_minimum_required(VERSION 3.20)
project(mwAudioAutoChop VERSION 0.1.0 LANGUAGES CXX)

# C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type handling
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# Compiler warnings
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Enable testing
enable_testing()

# =============================================================================
# Dependency Management
# =============================================================================

include(FetchContent)

# libsndfile - Audio file I/O
FetchContent_Declare(
    libsndfile
    GIT_REPOSITORY https://github.com/libsndfile/libsndfile
    GIT_TAG        1.2.2
)

# FFTW3 - FFT operations
# TODO: FFTW3 is complex to build from source. Prefer system install.
# For now, check if system-installed first with find_package
find_package(FFTW3 QUIET)
if(NOT FFTW3_FOUND)
    message(STATUS "FFTW3 not found. Please install system-wide or add support later.")
    # TODO: Add FetchContent build for FFTW3 when ready
endif()

# FTXUI - Terminal UI
FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG        v5.0.0
)

# Catch2 - Testing framework
FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2
    GIT_TAG        v3.5.2
)

# Fetch and configure all dependencies
# Disable tests for dependency projects to avoid running their test suite
set(BUILD_TESTING OFF CACHE BOOL "Disable dependency tests")
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "Disable FTXUI tests")
set(libsndfile_BUILD_TESTS OFF CACHE BOOL "Disable libsndfile tests")
FetchContent_MakeAvailable(libsndfile ftxui catch2)

# =============================================================================
# Source files
# =============================================================================

set(SOURCES
    src/main.cpp
)

# Executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Link dependencies to main executable
target_link_libraries(${PROJECT_NAME} PRIVATE
    SndFile::sndfile
    ftxui::component
    ftxui::dom
)

# =============================================================================
# Tests
# =============================================================================

add_executable(test_basic tests/test_basic.cpp)
target_link_libraries(test_basic PRIVATE Catch2 Catch2WithMain)

# Register tests with CTest
add_test(NAME test_basic COMMAND test_basic)

# Installation rules
install(TARGETS ${PROJECT_NAME} DESTINATION bin)