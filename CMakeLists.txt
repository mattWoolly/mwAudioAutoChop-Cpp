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

# Try system libsndfile first, fallback to fetching
find_package(PkgConfig QUIET)
pkg_check_modules(PkgConfig_sndfile QUIET sndfile)
if(PkgConfig_sndfile_FOUND)
    message(STATUS "Using system libsndfile")
    add_library(mwaac_sndfile INTERFACE)
    target_link_libraries(mwaac_sndfile INTERFACE ${PkgConfig_sndfile_LINK_LIBRARIES})
    target_include_directories(mwaac_sndfile INTERFACE ${PkgConfig_sndfile_INCLUDE_DIRS})
endif()

if(NOT TARGET SndFile::sndfile)
    # libsndfile - Audio file I/O via FetchContent
    FetchContent_Declare(
        libsndfile
        GIT_REPOSITORY https://github.com/libsndfile/libsndfile
        GIT_TAG        1.2.2
    )
endif()

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
if(NOT PkgConfig_sndfile_FOUND)
    set(libsndfile_BUILD_TESTS OFF CACHE BOOL "Disable libsndfile tests")
    FetchContent_MakeAvailable(libsndfile ftxui catch2)
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
if(PkgConfig_sndfile_FOUND)
    target_link_libraries(mwaac_core PUBLIC mwaac_sndfile)
else()
    target_link_libraries(mwaac_core PUBLIC SndFile::sndfile)
endif()

set(SOURCES
    src/main.cpp
)

# Executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

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

# Link core library and dependencies to main executable
if(PkgConfig_sndfile_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        mwaac_core
        mwaac_tui
        mwaac_sndfile
        ftxui::component
        ftxui::dom
    )
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE
        mwaac_core
        mwaac_tui
        SndFile::sndfile
        ftxui::component
        ftxui::dom
    )
endif()

# =============================================================================
# Tests
# =============================================================================

add_executable(test_basic tests/test_basic.cpp)
target_link_libraries(test_basic PRIVATE Catch2::Catch2WithMain)

add_executable(test_audio_file tests/test_audio_file.cpp)
target_link_libraries(test_audio_file PRIVATE mwaac_core Catch2::Catch2WithMain)

# Register tests with CTest
add_test(NAME test_basic COMMAND test_basic)
add_test(NAME test_audio_file COMMAND test_audio_file)

add_executable(test_audio_buffer tests/test_audio_buffer.cpp)
target_link_libraries(test_audio_buffer PRIVATE Catch2::Catch2WithMain mwaac_core)

add_test(NAME test_audio_buffer COMMAND test_audio_buffer)

add_executable(test_correlation tests/test_correlation.cpp)
target_link_libraries(test_correlation PRIVATE Catch2::Catch2WithMain mwaac_core)
add_test(NAME test_correlation COMMAND test_correlation)

add_executable(test_music_detection tests/test_music_detection.cpp)
target_link_libraries(test_music_detection PRIVATE Catch2::Catch2WithMain mwaac_core)
add_test(NAME test_music_detection COMMAND test_music_detection)

add_executable(test_lossless tests/test_lossless.cpp)
target_link_libraries(test_lossless PRIVATE Catch2::Catch2WithMain mwaac_core mwaac_sndfile)
add_test(NAME test_lossless COMMAND test_lossless)

add_executable(test_analysis tests/test_analysis.cpp)
target_link_libraries(test_analysis PRIVATE Catch2::Catch2WithMain mwaac_core)
add_test(NAME test_analysis COMMAND test_analysis)

add_executable(test_reference_mode tests/test_reference_mode.cpp)
target_link_libraries(test_reference_mode PRIVATE Catch2::Catch2WithMain mwaac_core)
add_test(NAME test_reference_mode COMMAND test_reference_mode)

add_executable(test_blind_mode tests/test_blind_mode.cpp)
target_link_libraries(test_blind_mode PRIVATE Catch2::Catch2WithMain mwaac_core)
add_test(NAME test_blind_mode COMMAND test_blind_mode)

add_executable(test_integration tests/test_integration.cpp)
target_link_libraries(test_integration PRIVATE Catch2::Catch2WithMain mwaac_core mwaac_sndfile)
add_test(NAME test_integration COMMAND test_integration)

# Installation rules
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
