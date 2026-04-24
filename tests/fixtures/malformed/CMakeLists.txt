# tests/fixtures/malformed/CMakeLists.txt
#
# FIXTURE-MALFORMED — see README.md in this directory.
#
# Builds a tiny standalone generator (`malformed_fixture_gen`) that writes
# the 14+ hand-crafted malformed audio blobs documented in README.md /
# manifest.txt into ${CMAKE_CURRENT_BINARY_DIR}. Wraps the run as a
# custom target named `malformed_fixture` so the audio_file test target
# can DEPEND on it.
#
# We do NOT link the generator against mwaac_core: the blobs are
# malformed by definition, and producing them via the very parser we are
# trying to test would be circular. The generator only depends on the
# C++ standard library.
#
# Output paths and the manifest source path are exposed to the test by
# `target_compile_definitions` from the parent CMakeLists.txt. The
# parent reads ${MWAAC_MALFORMED_FIXTURE_DIR} / ${MWAAC_MALFORMED_FIXTURE_MANIFEST}
# from the cache variables we set below.

# Generator executable. Plain C++ — no project deps. We do NOT call
# mwaac_apply_flags() because this generator must build cleanly even on
# the warnings-as-errors build, AND because it is intentionally
# decoupled from the project's audio code.
add_executable(malformed_fixture_gen generate.cpp)
target_compile_features(malformed_fixture_gen PRIVATE cxx_std_20)

# Where the blobs land at build time.
set(MWAAC_MALFORMED_FIXTURE_OUTPUT_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/blobs"
    CACHE INTERNAL
    "FIXTURE-MALFORMED: directory containing generated malformed blobs"
)

# Manifest source is checked in next to generate.cpp.
set(MWAAC_MALFORMED_FIXTURE_MANIFEST
    "${CMAKE_CURRENT_SOURCE_DIR}/manifest.txt"
    CACHE INTERNAL
    "FIXTURE-MALFORMED: path to manifest.txt"
)

# A stamp file lets us hook the generator into a normal CMake dependency
# graph: any target that depends on the stamp will rebuild the blobs if
# either the generator or the manifest changes. The stamp is touched
# whenever the generator successfully runs.
set(_stamp "${CMAKE_CURRENT_BINARY_DIR}/.malformed_fixture.stamp")

add_custom_command(
    OUTPUT  ${_stamp}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MWAAC_MALFORMED_FIXTURE_OUTPUT_DIR}
    COMMAND $<TARGET_FILE:malformed_fixture_gen> ${MWAAC_MALFORMED_FIXTURE_OUTPUT_DIR}
    COMMAND ${CMAKE_COMMAND} -E touch ${_stamp}
    DEPENDS malformed_fixture_gen
            ${CMAKE_CURRENT_SOURCE_DIR}/generate.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/manifest.txt
    COMMENT "FIXTURE-MALFORMED: generating malformed audio corpus"
    VERBATIM
)

# Public-facing target the test depends on. Convention follows the
# fixture README at tests/fixtures/README.md: the target name is
# `<fixture>_fixture` and it is the single hook point for downstream
# tests.
add_custom_target(malformed_fixture ALL DEPENDS ${_stamp})
