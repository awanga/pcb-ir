# SPDX-License-Identifier: Apache-2.0
find_package(Catch2 REQUIRED)
include(Catch)

add_executable(pcbir_tests
  smoke_test.cpp
  core_test.cpp
  board_serialize_test.cpp
  board_file_view_test.cpp
  format_error_test.cpp
  geometry/point_test.cpp
  geometry/segment_arc_test.cpp
  geometry/bezier_test.cpp
  geometry/contour_test.cpp
  geometry/polygon_test.cpp
  geometry/path_test.cpp
  geometry/rotate_test.cpp
  geometry/entities_test.cpp
  geometry/serialize_test.cpp
  geometry/boolean_test.cpp
  geometry/spatial_index_test.cpp
  geometry/diagnostics_test.cpp
  connectivity/entities_test.cpp
  connectivity/serialize_test.cpp
  connectivity/diagnostics_test.cpp
  stackup/entities_test.cpp
  stackup/serialize_test.cpp
  stackup/diagnostics_test.cpp
)
target_link_libraries(pcbir_tests PRIVATE pcbir_core Catch2::Catch2WithMain pcbir_warnings)

catch_discover_tests(pcbir_tests)

add_test(
  NAME public_header_hygiene
  COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_public_header_hygiene.sh
)

# Stable C ABI (docs/rfcs/0001-stable-c-abi.md): a genuine C11 smoke test,
# proving pcbir.h is self-contained, valid C, and that the scoped-in MVP
# surface works end to end.
add_executable(pcbir_c_abi_generate_fixture ${CMAKE_SOURCE_DIR}/tools/c_abi/generate_fixture.cpp)
target_link_libraries(pcbir_c_abi_generate_fixture PRIVATE pcbir_core pcbir_warnings)

add_executable(pcbir_c_abi_smoke_test c_abi/smoke_test.c)
set_target_properties(pcbir_c_abi_smoke_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
)
target_link_libraries(pcbir_c_abi_smoke_test PRIVATE pcbir_c_abi pcbir_warnings)

set(PCBIR_C_ABI_FIXTURE "${CMAKE_BINARY_DIR}/c_abi_fixture.pcbir")

add_test(
  NAME c_abi_generate_fixture
  COMMAND pcbir_c_abi_generate_fixture "${PCBIR_C_ABI_FIXTURE}"
)
set_tests_properties(c_abi_generate_fixture PROPERTIES FIXTURES_SETUP c_abi_fixture)

add_test(
  NAME c_abi_smoke_test
  COMMAND pcbir_c_abi_smoke_test "${PCBIR_C_ABI_FIXTURE}"
)
set_tests_properties(c_abi_smoke_test PROPERTIES FIXTURES_REQUIRED c_abi_fixture)

# Cheap, precise header-hygiene gate, independent of whether the smoke test
# happens to exercise every declaration: pcbir.h must compile as
# self-contained C11 on its own. cl.exe understands none of GCC/Clang's
# -std=/-fsyntax-only/-x c/-I spelling, so MSVC needs its own flag set.
if(MSVC)
  add_test(
    NAME c_abi_header_is_valid_c
    COMMAND ${CMAKE_C_COMPILER} /std:c11 /TC /Zs
            /I${CMAKE_SOURCE_DIR}/include /I${CMAKE_BINARY_DIR}/generated
            ${CMAKE_SOURCE_DIR}/include/pcbir/pcbir.h
  )
else()
  add_test(
    NAME c_abi_header_is_valid_c
    COMMAND ${CMAKE_C_COMPILER} -std=c11 -fsyntax-only -x c
            -I${CMAKE_SOURCE_DIR}/include -I${CMAKE_BINARY_DIR}/generated
            ${CMAKE_SOURCE_DIR}/include/pcbir/pcbir.h
  )
endif()
