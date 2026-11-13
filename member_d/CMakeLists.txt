cmake_minimum_required(VERSION 3.16)
project(ceg5206_member_d LANGUAGES CXX)

if(NOT TARGET c_contract_adapter)
  add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../member_c" member_c)
endif()

# Reuse production libraries. Only C's diagnostic variant needs recompilation.
add_library(d_runtime STATIC src/allocation_tracker.cpp ../member_b/bench/edge_major.cpp)
target_include_directories(d_runtime PUBLIC include)
target_compile_definitions(d_runtime PUBLIC D_ALLOCATION_TRACKER)
target_link_libraries(d_runtime PUBLIC ceg5206_contracts PRIVATE ceg5206_b_policy)

function(add_benchmark name scheduler)
  add_executable(${name} src/benchmark.cpp)
  target_compile_definitions(${name} PRIVATE D_ENABLE_A D_ENABLE_B D_ENABLE_C D_ENABLE_EDGE_MAJOR)
  target_link_libraries(${name} PRIVATE ceg5206_a ${scheduler} d_runtime ceg5206_b_policy)
endfunction()
add_benchmark(benchmark c_contract_adapter)

option(D_BUILD_PROFILE "Build the separate instrumented benchmark" ON)
if(D_BUILD_PROFILE)
  add_library(c_contract_profile STATIC ../member_c/src/contract_adapter.cpp)
  target_compile_definitions(c_contract_profile PUBLIC CEG5206_D_DIAGNOSTICS)
  target_include_directories(c_contract_profile PRIVATE include)
  target_link_libraries(c_contract_profile PUBLIC ceg5206_b OpenMP::OpenMP_CXX
    PRIVATE ceg5206_b_policy)
  add_benchmark(benchmark_profile c_contract_profile)
endif()

include(CTest)
if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_executable(test_measurement tests/test_measurement.cpp)
  target_include_directories(test_measurement PRIVATE include)
  target_link_libraries(test_measurement PRIVATE ceg5206_contracts)
  add_test(NAME measurement COMMAND test_measurement)

  add_executable(test_allocation tests/test_allocation.cpp)
  target_link_libraries(test_allocation PRIVATE d_runtime)
  add_test(NAME allocation COMMAND test_allocation)

  # Deliberately unavailable/fixture-only binaries test the runner's failure paths.
  add_executable(test_unlinked_runner src/benchmark.cpp)
  target_include_directories(test_unlinked_runner PRIVATE include)
  target_link_libraries(test_unlinked_runner PRIVATE ceg5206_contracts)
  add_executable(test_fixture_runner src/benchmark.cpp tests/fake_a.cpp)
  target_include_directories(test_fixture_runner PRIVATE include)
  target_compile_definitions(test_fixture_runner PRIVATE D_ENABLE_A D_TEST_ONLY)
  target_link_libraries(test_fixture_runner PRIVATE ceg5206_contracts)
  add_test(NAME runner_python COMMAND "${CMAKE_COMMAND}" -E env
    "D_UNLINKED_BINARY=$<TARGET_FILE:test_unlinked_runner>"
    "D_FIXTURE_BINARY=$<TARGET_FILE:test_fixture_runner>"
    "PYTHONDONTWRITEBYTECODE=1"
    "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p "test_*.py")
endif()
