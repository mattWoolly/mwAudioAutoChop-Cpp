# tests/fixtures/waveext/CMakeLists.txt
#
# FIXTURE-WAVEEXT — produces 24-bit WAVE_FORMAT_EXTENSIBLE WAV files at
# build time. We deliberately roll the bytes by hand (no libsndfile) so
# the format-tag word is exactly 0xFFFE; libsndfile's writer emits format
# tag 0x0001 for stereo PCM and we need to exercise the EXTENSIBLE parse
# path (M-3) and the EXTENSIBLE round-trip path (NEW-WAVEEXT-WRITE).
#
# Output directory layout:
#   ${CMAKE_BINARY_DIR}/fixtures/waveext/
#       pcm_24bit_stereo.wav
#       pcm_24bit_5ch.wav
#       pcm_extensible_unsupported_subformat.wav
#       manifest.txt
#
# The dependent test binaries (test_audio_file, test_lossless) consume
# this path through the MWAAC_FIXTURE_WAVEEXT_DIR compile definition that
# we set on those targets here, after they have been declared by the
# top-level CMakeLists.

set(MWAAC_FIXTURE_WAVEEXT_OUTPUT_DIR "${CMAKE_BINARY_DIR}/fixtures/waveext")

add_executable(gen_waveext_fixture generate.cpp)
mwaac_apply_flags(gen_waveext_fixture)

set(MWAAC_FIXTURE_WAVEEXT_OUTPUTS
    "${MWAAC_FIXTURE_WAVEEXT_OUTPUT_DIR}/pcm_24bit_stereo.wav"
    "${MWAAC_FIXTURE_WAVEEXT_OUTPUT_DIR}/pcm_24bit_5ch.wav"
    "${MWAAC_FIXTURE_WAVEEXT_OUTPUT_DIR}/pcm_extensible_unsupported_subformat.wav"
    "${MWAAC_FIXTURE_WAVEEXT_OUTPUT_DIR}/manifest.txt"
)

add_custom_command(
    OUTPUT ${MWAAC_FIXTURE_WAVEEXT_OUTPUTS}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MWAAC_FIXTURE_WAVEEXT_OUTPUT_DIR}"
    COMMAND $<TARGET_FILE:gen_waveext_fixture> "${MWAAC_FIXTURE_WAVEEXT_OUTPUT_DIR}"
    DEPENDS gen_waveext_fixture
    COMMENT "Generating WAVE_FORMAT_EXTENSIBLE fixtures (FIXTURE-WAVEEXT)"
    VERBATIM
)

add_custom_target(waveext_fixture
    DEPENDS ${MWAAC_FIXTURE_WAVEEXT_OUTPUTS}
)

# Wire the fixture path and target dependency into the consuming tests.
# These targets are declared in the top-level CMakeLists.txt before
# add_subdirectory(tests/fixtures); guard with TARGET to keep this fixture
# subdirectory drop-in.
foreach(consumer test_audio_file test_lossless)
    if(TARGET ${consumer})
        target_compile_definitions(${consumer} PRIVATE
            MWAAC_FIXTURE_WAVEEXT_DIR="${MWAAC_FIXTURE_WAVEEXT_OUTPUT_DIR}"
        )
        add_dependencies(${consumer} waveext_fixture)
    endif()
endforeach()
