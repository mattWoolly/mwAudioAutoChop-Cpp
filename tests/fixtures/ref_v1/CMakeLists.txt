# tests/fixtures/ref_v1/CMakeLists.txt
#
# Builds the gen_ref_fixture_v1 generator and exposes a custom target
# `ref_fixture_v1` that runs it once. The generator writes its output under
# ${CMAKE_BINARY_DIR}/fixtures/ref_v1/. Tests that depend on this fixture
# add `ref_fixture_v1` to their target dependencies; ctest then sees a
# linear order and the fixture is generated exactly once even when ctest
# runs in parallel.
#
# The generator is NOT auto-run on every build (no add_custom_command at
# build-all scope), keeping the default build cheap.

set(REF_FIXTURE_V1_OUT_DIR ${CMAKE_BINARY_DIR}/fixtures/ref_v1)

add_executable(gen_ref_fixture_v1 generate.cpp)
target_link_libraries(gen_ref_fixture_v1 PRIVATE mwaac_sndfile)
target_compile_features(gen_ref_fixture_v1 PRIVATE cxx_std_20)
mwaac_apply_flags(gen_ref_fixture_v1)

# Custom target driving the generator. The OUTPUT-list pattern would let
# CMake's dependency tracker re-run only when needed, but the cost of
# regenerating is small and a stamp file would still need plumbing — for
# now, the ALL-disabled custom target is simplest. Tests opt in via
# add_dependencies().
set(REF_FIXTURE_V1_STAMP ${REF_FIXTURE_V1_OUT_DIR}/.stamp)

add_custom_command(
    OUTPUT ${REF_FIXTURE_V1_STAMP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${REF_FIXTURE_V1_OUT_DIR}
    COMMAND $<TARGET_FILE:gen_ref_fixture_v1> ${REF_FIXTURE_V1_OUT_DIR}
    COMMAND ${CMAKE_COMMAND} -E touch ${REF_FIXTURE_V1_STAMP}
    DEPENDS gen_ref_fixture_v1
    COMMENT "Generating ref_v1 fixture in ${REF_FIXTURE_V1_OUT_DIR}"
    VERBATIM
)

add_custom_target(ref_fixture_v1 DEPENDS ${REF_FIXTURE_V1_STAMP})

# Expose the fixture path to the rest of the build via a cache variable so
# the test executables can link it through a preprocessor define.
set(MWAAC_REF_FIXTURE_V1_DIR ${REF_FIXTURE_V1_OUT_DIR}
    CACHE INTERNAL "Output directory for the ref_v1 fixture")

# Wire the fixture to the integration test binary. This is done from the
# fixture's CMakeLists (rather than the top-level one) so a fix-agent who
# adds a fixture only edits its own subtree. test_integration is declared
# at top level in tests/CMakeLists.txt before tests/fixtures is added, so
# the target already exists here.
if(TARGET test_integration)
    add_dependencies(test_integration ref_fixture_v1)
    target_compile_definitions(test_integration PRIVATE
        MWAAC_REF_FIXTURE_V1_DIR="${REF_FIXTURE_V1_OUT_DIR}")
endif()
