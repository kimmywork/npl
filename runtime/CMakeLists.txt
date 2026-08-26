# runtime/CMakeLists.txt — NPL runtime library

set(RUNTIME_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_library(npl_runtime STATIC
  ${RUNTIME_SRC_DIR}/alloc.c
  ${RUNTIME_SRC_DIR}/gc.c
)

target_include_directories(npl_runtime PUBLIC ${RUNTIME_SRC_DIR})
target_compile_options(npl_runtime PRIVATE
  -std=c99 -Wall -Wextra -Wpedantic
  -Wno-unused-parameter
)

set_target_properties(npl_runtime PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/runtime
)
