#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${1:-$ROOT/member_d/build/team}"
OMP_PREFIX="${OMP_PREFIX:-/opt/homebrew/opt/libomp}"
OMP_ARGS=()
if [[ "$(uname -s)" == Darwin ]]; then
  OMP_ARGS=("-DOpenMP_CXX_FLAGS=-Xpreprocessor -fopenmp -I$OMP_PREFIX/include" -DOpenMP_CXX_LIB_NAMES=omp "-DOpenMP_omp_LIBRARY=$OMP_PREFIX/lib/libomp.dylib")
fi
cmake -S "$ROOT/member_d" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
 -DD_ENABLE_A=ON "-DD_A_SOURCES=$ROOT/member_a/src/validation.cpp;$ROOT/member_a/src/csr.cpp;$ROOT/member_a/src/generator.cpp;$ROOT/member_a/src/oracle.cpp;$ROOT/member_a/src/chunk_audit.cpp" \
 "-DD_SCALAR_SOURCES=$ROOT/member_a/src/scalar.cpp" \
 -DD_ENABLE_B=ON "-DD_B_SOURCES=$ROOT/member_b/src/simd.cpp" \
 -DD_ENABLE_C=ON "-DD_C_SOURCES=$ROOT/member_c/src/contract_adapter.cpp" \
 -DD_USE_OPENMP=ON "${OMP_ARGS[@]}"
cmake --build "$BUILD" -j4
ctest --test-dir "$BUILD" --output-on-failure
