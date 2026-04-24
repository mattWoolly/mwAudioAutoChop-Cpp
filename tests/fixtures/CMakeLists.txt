# tests/fixtures/CMakeLists.txt
#
# Auto-discovers every immediate subdirectory and treats it as a fixture
# project. Each <id>/ contributes its own CMakeLists.txt — see
# tests/fixtures/README.md for the contract.
#
# We use a glob over directories rather than an explicit list so the
# parallel fixture agents (FIXTURE-REF, FIXTURE-RF64, FIXTURE-WAVEEXT,
# FIXTURE-MALFORMED) don't all need to edit this file. The standard CMake
# caveat about file(GLOB) — that a freshly-added file is not picked up
# without re-running cmake — is acceptable here: adding a fixture is an
# explicit deliberate action that ships in a PR alongside a re-configure.

file(GLOB MWAAC_FIXTURE_DIRS LIST_DIRECTORIES true RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
     ${CMAKE_CURRENT_SOURCE_DIR}/*)

foreach(dir IN LISTS MWAAC_FIXTURE_DIRS)
    set(full ${CMAKE_CURRENT_SOURCE_DIR}/${dir})
    if(IS_DIRECTORY ${full} AND EXISTS ${full}/CMakeLists.txt)
        add_subdirectory(${dir})
    endif()
endforeach()
