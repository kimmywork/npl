# runtime/CMakeLists.txt — NPL runtime library

set(RUNTIME_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_library(npl_runtime STATIC
  ${RUNTIME_SRC_DIR}/alloc.c
  ${RUNTIME_SRC_DIR}/control.c
  ${RUNTIME_SRC_DIR}/gc.c
  ${RUNTIME_SRC_DIR}/io.c
  ${RUNTIME_SRC_DIR}/lexer.c
  ${RUNTIME_SRC_DIR}/value.c
)

target_include_directories(npl_runtime PUBLIC ${RUNTIME_SRC_DIR})
target_compile_options(npl_runtime PRIVATE
  -std=c99 -Wall -Wextra -Wpedantic
  -Wno-unused-parameter
)

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
