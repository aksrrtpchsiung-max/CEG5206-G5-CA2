cmake_minimum_required(VERSION 3.16)
project(ceg5206_member_c LANGUAGES CXX)

if(NOT TARGET ceg5206_b)
  add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../member_b" member_b)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/OpenMP.cmake")
add_library(c_contract_adapter STATIC src/contract_adapter.cpp)
target_link_libraries(c_contract_adapter PUBLIC ceg5206_b OpenMP::OpenMP_CXX
  PRIVATE ceg5206_b_policy)

include(CTest)
if(BUILD_TESTING)
  add_executable(c_contract_tests tests/test_contract_adapter.cpp)
  target_link_libraries(c_contract_tests PRIVATE c_contract_adapter ceg5206_b_policy)
  add_test(NAME c_contract_tests COMMAND c_contract_tests)
endif()

option(C_BUILD_BENCHMARK "Build the standalone scheduling benchmark" OFF)
if(C_BUILD_BENCHMARK)
  add_executable(c_contract_benchmark bench/contract_benchmark.cpp)
  target_link_libraries(c_contract_benchmark PRIVATE c_contract_adapter ceg5206_b_policy)
endif()
