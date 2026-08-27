# runtime/CMakeLists.txt — NPL runtime library

set(RUNTIME_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_library(npl_runtime STATIC
  ${RUNTIME_SRC_DIR}/alloc.c
  ${RUNTIME_SRC_DIR}/gc.c
  ${RUNTIME_SRC_DIR}/simd_kernels.c
  ${RUNTIME_SRC_DIR}/simd_kernels_scalar.c
)

target_include_directories(npl_runtime PUBLIC ${RUNTIME_SRC_DIR})
target_compile_options(npl_runtime PRIVATE
  -std=c99 -Wall -Wextra -Wpedantic
  -Wno-unused-parameter
)

# The scalar reference kernels must measure a true sequential loop:
# disable the compiler's auto-vectorizer for them only.
set_source_files_properties(${RUNTIME_SRC_DIR}/simd_kernels_scalar.c
  PROPERTIES COMPILE_OPTIONS "-O2;-fno-vectorize;-fno-slp-vectorize")
# Intrinsics + bench get -O2 so call overhead doesn't distort timing.
set_source_files_properties(${RUNTIME_SRC_DIR}/simd_kernels.c
  PROPERTIES COMPILE_OPTIONS "-O2")

set_target_properties(npl_runtime PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/runtime
)

add_executable(gc_stress
  ${RUNTIME_SRC_DIR}/gc_stress.c
)
target_include_directories(gc_stress PRIVATE ${RUNTIME_SRC_DIR})
target_link_libraries(gc_stress PRIVATE npl_runtime)
target_compile_options(gc_stress PRIVATE
  -std=c99 -Wall -Wextra -Wpedantic -Wno-unused-parameter -O2)
set_target_properties(gc_stress PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/runtime)

# ---- Phase 9 acceptance harness (correctness + >=3x speedup) ----
add_executable(simd_bench
  ${RUNTIME_SRC_DIR}/simd_bench.c
)
target_include_directories(simd_bench PRIVATE ${RUNTIME_SRC_DIR})
target_link_libraries(simd_bench PRIVATE npl_runtime)
target_compile_options(simd_bench PRIVATE
  -std=c99 -Wall -Wextra -Wpedantic -Wno-unused-parameter -O2)
set_target_properties(simd_bench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/runtime)
