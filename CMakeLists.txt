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

# Warning and sanitizer harness.
#
# MWAAC_WERROR defaults ON because PROJECT_SPEC.md's quality gate demands
# warning-free builds. Clearing it is only for local investigative builds;
# CI must keep it ON.
#
# MWAAC_SANITIZE turns on AddressSanitizer + UndefinedBehaviorSanitizer.
# Keep OFF in Release, ON in the dedicated CI sanitizer job (Debug build).
# The two sanitizers compose cleanly; together they catch the UB paths the
# Knuth-level review flagged (reinterpret_cast into wrong-type storage,
# stack buffer overflow in encode_float80, division-by-zero in ZCR on
# one-sample frames, etc.).
#
# The warning set below is scoped to OUR targets only (see mwaac_apply_flags
# helper and its uses after the FetchContent_MakeAvailable calls).
# Applying -Werror to third-party code via add_compile_options would fail
# to build FTXUI / libsndfile / Catch2, which legitimately produce warnings
# under -Wconversion / -Wold-style-cast.
option(MWAAC_WERROR "Treat warnings as errors" ON)
option(MWAAC_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

if(MSVC)
    set(MWAAC_WARNINGS /W4 /permissive-)
    if(MWAAC_WERROR)
        list(APPEND MWAAC_WARNINGS /WX)
    endif()
else()
    set(MWAAC_WARNINGS
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wold-style-cast
        -Wcast-align
        -Wnon-virtual-dtor
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
    )
    if(MWAAC_WERROR)
        list(APPEND MWAAC_WARNINGS -Werror)
    endif()
endif()

# Sanitizers go on every target we build (our code AND tests). Third-party
# code compiled by FetchContent does not need them — the sanitizer runtime
# is whole-program. Putting these in add_compile_options (directory scope)
# is correct here because the runtime needs to be consistent across all TUs.
if(MWAAC_SANITIZE)
    if(MSVC)
        message(WARNING "MWAAC_SANITIZE is not supported on MSVC; ignoring")
    else()
        message(STATUS "Sanitizers enabled: address, undefined")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# Apply the warning set to one of our targets. Use after the target is
# declared. Keep this as a function so every target goes through one
# check-point.
function(mwaac_apply_flags target)
    target_compile_options(${target} PRIVATE ${MWAAC_WARNINGS})
endfunction()

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

# Try system libsndfile first, fallback to fetching.
#
# IMPORTED_TARGET is important: it creates PkgConfig::mwaac_sndfile_pc carrying
# full -L, -I, and -Wl,-rpath flags from pkg-config. Using the bare
# ${..._LIBRARIES} variable loses the library dir, producing `-lsndfile` with
# no `-L` — which fails to link unless libsndfile happens to live in the
# default system search path.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(mwaac_sndfile_pc QUIET IMPORTED_TARGET sndfile)
endif()
if(mwaac_sndfile_pc_FOUND)
    message(STATUS "Using system libsndfile (pkg-config)")
    add_library(mwaac_sndfile INTERFACE)
    target_link_libraries(mwaac_sndfile INTERFACE PkgConfig::mwaac_sndfile_pc)
else()
    # libsndfile - Audio file I/O via FetchContent.
    FetchContent_Declare(
        libsndfile
        GIT_REPOSITORY https://github.com/libsndfile/libsndfile
        GIT_TAG        1.2.2
    )
endif()

# FFT is provided by vendored pocketfft_hdronly.h (BSD-3, Martin Reinecke).
# See src/core/pocketfft_hdronly.h; no linkage required. An earlier iteration
# of this project used FFTW3 — keep that out of the dependency graph.

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

# Fetch and configure all dependencies.
# Disable tests for dependency projects to avoid running their test suite.
set(BUILD_TESTING OFF CACHE BOOL "Disable dependency tests")
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "Disable FTXUI tests")
if(NOT mwaac_sndfile_pc_FOUND)
    set(libsndfile_BUILD_TESTS OFF CACHE BOOL "Disable libsndfile tests")
    FetchContent_MakeAvailable(libsndfile ftxui catch2)
    # FetchContent'd libsndfile exports as SndFile::sndfile. Normalize so the
    # rest of CMakeLists.txt always links `mwaac_sndfile`.
    add_library(mwaac_sndfile INTERFACE)
    target_link_libraries(mwaac_sndfile INTERFACE SndFile::sndfile)
else()
    FetchContent_MakeAvailable(ftxui catch2)
endif()

# =============================================================================
# Source files
# =============================================================================

# Core library source files
set(CORE_SOURCES
    src/core/drift_model.cpp
    src/core/audio_buffer.cpp
    src/core/audio_file.cpp
    src/core/correlation.cpp
    src/core/analysis.cpp
    src/core/music_detection.cpp
)

# Reference mode sources
set(REFERENCE_SOURCES
    src/modes/reference_mode.cpp
    src/modes/blind_mode.cpp
    src/modes/reaper_export.cpp
)

# TUI sources
set(TUI_SOURCES
    src/tui/waveform.cpp
    src/tui/app.cpp
)

# Core library
add_library(mwaac_core STATIC ${CORE_SOURCES} ${REFERENCE_SOURCES})
target_include_directories(mwaac_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)
target_compile_features(mwaac_core PUBLIC cxx_std_20)
target_link_libraries(mwaac_core PUBLIC mwaac_sndfile)
mwaac_apply_flags(mwaac_core)

set(SOURCES
    src/main.cpp
)

# Executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
mwaac_apply_flags(${PROJECT_NAME})

# TUI library
add_library(mwaac_tui STATIC ${TUI_SOURCES})
target_include_directories(mwaac_tui PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(mwaac_tui PRIVATE
    mwaac_core
    ftxui::component
    ftxui::dom
)
mwaac_apply_flags(mwaac_tui)

# Link core library and dependencies to main executable.
# mwaac_sndfile is a single normalized target name for libsndfile regardless
# of whether pkg-config or FetchContent supplied the dependency.
target_link_libraries(${PROJECT_NAME} PRIVATE
    mwaac_core
    mwaac_tui
    ftxui::component
    ftxui::dom
)

# =============================================================================
# Tests
# =============================================================================

# Test targets. Factored through a helper so every test binary goes
# through the same warning-and-sanitizer flags as production code.
function(mwaac_add_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE Catch2::Catch2WithMain ${ARGN})
    mwaac_apply_flags(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

mwaac_add_test(test_basic)
mwaac_add_test(test_audio_file        mwaac_core)
mwaac_add_test(test_audio_buffer      mwaac_core)
mwaac_add_test(test_correlation       mwaac_core)
mwaac_add_test(test_music_detection   mwaac_core)
mwaac_add_test(test_lossless          mwaac_core mwaac_sndfile)
mwaac_add_test(test_analysis          mwaac_core)
mwaac_add_test(test_reference_mode    mwaac_core)
mwaac_add_test(test_blind_mode        mwaac_core)
mwaac_add_test(test_integration       mwaac_core mwaac_sndfile)

# Reproducible test fixtures. Each subdirectory of tests/fixtures/ is a
# self-contained fixture project — see tests/fixtures/README.md.
add_subdirectory(tests/fixtures)

# Installation rules
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
