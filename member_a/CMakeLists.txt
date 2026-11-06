cmake_minimum_required(VERSION 3.16)
project(ceg5206_member_a LANGUAGES CXX)

add_library(ceg5206_contracts INTERFACE)
target_include_directories(ceg5206_contracts INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(ceg5206_contracts INTERFACE cxx_std_17)

add_library(ceg5206_a STATIC
    src/validation.cpp src/csr.cpp src/scalar.cpp src/oracle.cpp
    src/generator.cpp src/chunk_audit.cpp)
target_link_libraries(ceg5206_a PUBLIC ceg5206_contracts)
if(MSVC)
    target_compile_options(ceg5206_a PRIVATE /fp:strict /W4)
    # scalar.cpp uses MSVC's documented loop(no_vector) on the accumulation loop.
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ceg5206_a PRIVATE -Wall -Wextra -Wpedantic -fno-fast-math -ffp-contract=off)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set_source_files_properties(src/scalar.cpp PROPERTIES COMPILE_OPTIONS "-fno-tree-vectorize")
    else()
        set_source_files_properties(src/scalar.cpp PROPERTIES COMPILE_OPTIONS "-fno-vectorize;-fno-slp-vectorize")
    endif()
else()
    message(FATAL_ERROR "Configure explicit FP and scalar vectorization flags for this compiler")
endif()

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
