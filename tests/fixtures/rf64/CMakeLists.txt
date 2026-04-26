# tests/fixtures/rf64/CMakeLists.txt
#
# FIXTURE-RF64. See README.md in this directory for the contract this
# fixture satisfies and the backlog items that depend on it.
#
# We intentionally do NOT link against mwaac_core, libsndfile, or anything
# else. The whole fixture lives in raw POSIX I/O. That keeps the generator
# small (a few hundred lines) and, more importantly, decouples the fixture
# from the production code we are about to fix — the generator stays
# byte-identical across changes to src/.

add_executable(gen_rf64_fixture generate.cpp)

# Honor the project's warning set, but skip -Werror — this generator uses
# POSIX-only headers that draw a few harmless warnings on some toolchains
# (e.g., -Wold-style-cast on macros from <fcntl.h>). The failure mode for
# a fixture generator is "doesn't run / produces wrong bytes", which the
# tests catch directly; warnings here would not improve safety.
target_compile_features(gen_rf64_fixture PRIVATE cxx_std_20)

# Where the fixture writes to. Tests locate it via the same path through
# a compile definition on the test target — see the top-level CMakeLists.
set(MWAAC_RF64_FIXTURE_DIR "${CMAKE_BINARY_DIR}/fixtures/rf64" CACHE INTERNAL
    "Directory holding generated FIXTURE-RF64 files")

# Generator outputs. Listed explicitly so CMake can re-run the generator
# when generate.cpp changes (otherwise an OUTPUT-less custom_command would
# rebuild every configure).
set(_rf64_outputs
    "${MWAAC_RF64_FIXTURE_DIR}/rf64_ds64_first.wav"
    "${MWAAC_RF64_FIXTURE_DIR}/rf64_ds64_after.wav"
    "${MWAAC_RF64_FIXTURE_DIR}/manifest.txt"
)

add_custom_command(
    OUTPUT ${_rf64_outputs}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MWAAC_RF64_FIXTURE_DIR}"
    COMMAND $<TARGET_FILE:gen_rf64_fixture> "${MWAAC_RF64_FIXTURE_DIR}"
    DEPENDS gen_rf64_fixture
    COMMENT "Generating FIXTURE-RF64 (sparse RF64 files >4 GiB)"
    VERBATIM
)

# A named target test code can list as a dependency. CMake's documented
# pattern for "build artefact via custom_command, expose as a target" is
# add_custom_target depending on the OUTPUT list.
add_custom_target(rf64_fixture ALL DEPENDS ${_rf64_outputs})

# Wire the dependent test target from inside this fixture's directory so
# the parent CMakeLists.txt doesn't need to be edited per-fixture. This
# directory is processed via tests/fixtures/CMakeLists.txt's glob over
# subdirectories, which happens AFTER mwaac_add_test(test_lossless ...)
# in the top-level file — so test_lossless exists as a target by the time
# we get here.
#
# The TARGET guard keeps this safe if test wiring is later refactored
# (e.g., test_lossless is renamed or split). A missing target gracefully
# becomes a no-op rather than a configure-time error.
if(TARGET test_lossless)
    add_dependencies(test_lossless rf64_fixture)
    target_compile_definitions(test_lossless PRIVATE
        "MWAAC_RF64_FIXTURE_DIR=\"${MWAAC_RF64_FIXTURE_DIR}\"")
endif()
