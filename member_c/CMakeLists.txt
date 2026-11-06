cmake_minimum_required(VERSION 3.16)

project(ceg5206_group5_part_c LANGUAGES CXX)

find_package(Threads REQUIRED)

add_library(c_scheduler STATIC
    src/c_scheduler.cpp
)

target_include_directories(c_scheduler
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(c_scheduler PUBLIC cxx_std_17)
target_link_libraries(c_scheduler PUBLIC Threads::Threads)

if(MSVC)
    target_compile_options(c_scheduler PRIVATE /W4 /permissive-)
else()
    target_compile_options(c_scheduler PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_executable(c_scheduler_tests
    tests/test_scheduler.cpp
)

target_link_libraries(c_scheduler_tests PRIVATE c_scheduler)

enable_testing()
add_test(NAME c_scheduler_tests COMMAND c_scheduler_tests)

# Shared A/B/C contract integration. The default path targets the user-provided
# GitHub snapshot next to this directory and can be overridden by consumers.
set(CEG5206_CONTRACT_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../member_a/include"
    CACHE PATH "Directory containing ceg5206/contracts.hpp")

if(EXISTS "${CEG5206_CONTRACT_INCLUDE_DIR}/ceg5206/contracts.hpp")
    find_package(OpenMP)

    add_library(c_contract_adapter STATIC src/contract_adapter.cpp)
    target_include_directories(c_contract_adapter PUBLIC
        "${CEG5206_CONTRACT_INCLUDE_DIR}")
    target_compile_features(c_contract_adapter PUBLIC cxx_std_17)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(c_contract_adapter PUBLIC OpenMP::OpenMP_CXX)
    endif()
    if(MSVC)
        target_compile_options(c_contract_adapter PRIVATE /W4 /permissive- /fp:strict)
    else()
        target_compile_options(c_contract_adapter PRIVATE
            -Wall -Wextra -Wpedantic -fno-fast-math -ffp-contract=off)
    endif()

    option(C_BUILD_PORTABLE_CONTRACT_TESTS
        "Build x64 validation with a test-only portable B ABI shim" ON)
    if(C_BUILD_PORTABLE_CONTRACT_TESTS)
        add_library(c_portable_b_shim STATIC tests/portable_b_shim.cpp)
        target_include_directories(c_portable_b_shim PUBLIC
            "${CEG5206_CONTRACT_INCLUDE_DIR}")
        target_compile_features(c_portable_b_shim PUBLIC cxx_std_17)

        add_executable(c_contract_tests tests/test_contract_adapter.cpp)
        target_link_libraries(c_contract_tests PRIVATE
            c_contract_adapter c_portable_b_shim)
        add_test(NAME c_contract_tests COMMAND c_contract_tests)

        add_executable(c_contract_benchmark bench/contract_benchmark.cpp)
        target_link_libraries(c_contract_benchmark PRIVATE
            c_contract_adapter c_portable_b_shim)
    endif()

    option(C_BUILD_REAL_B_INTEGRATION
        "Build contract tests/benchmark against Member B's real AArch64 NEON source" OFF)
    set(CEG5206_B_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/../member_b/src/simd.cpp"
        CACHE FILEPATH "Path to Member B's production SIMD source")
    if(C_BUILD_REAL_B_INTEGRATION)
        if(NOT OpenMP_CXX_FOUND)
            message(FATAL_ERROR "Real multithreaded C integration requires OpenMP")
        endif()
        add_library(c_real_b STATIC "${CEG5206_B_SOURCE}")
        target_include_directories(c_real_b PUBLIC "${CEG5206_CONTRACT_INCLUDE_DIR}")
        target_compile_features(c_real_b PUBLIC cxx_std_17)
        if(MSVC)
            message(FATAL_ERROR "Member B requires AArch64 NEON with Clang or GCC")
        else()
            target_compile_options(c_real_b PRIVATE
                -O3 -Wall -Wextra -Wpedantic -fno-fast-math -ffp-contract=off)
        endif()

        add_executable(c_contract_tests_real_b tests/test_contract_adapter.cpp)
        target_link_libraries(c_contract_tests_real_b PRIVATE
            c_contract_adapter c_real_b)
        add_test(NAME c_contract_tests_real_b COMMAND c_contract_tests_real_b)

        add_executable(c_contract_benchmark_real_b bench/contract_benchmark.cpp)
        target_link_libraries(c_contract_benchmark_real_b PRIVATE
            c_contract_adapter c_real_b)
    endif()
endif()
