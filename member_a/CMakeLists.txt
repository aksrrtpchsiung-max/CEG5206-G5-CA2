cmake_minimum_required(VERSION 3.16)
project(ceg5206_member_a LANGUAGES CXX)

# Contract only. Algorithm implementations will be added in A-01 onward.
add_library(ceg5206_contracts INTERFACE)
target_include_directories(ceg5206_contracts INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(ceg5206_contracts INTERFACE cxx_std_17)
