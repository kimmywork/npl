# runtime/CMakeLists.txt — NPL runtime library

set(RUNTIME_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_library(npl_runtime STATIC
  ${RUNTIME_SRC_DIR}/alloc.c
  ${RUNTIME_SRC_DIR}/clock.c
  ${RUNTIME_SRC_DIR}/complex.c
  ${RUNTIME_SRC_DIR}/control.c
  ${RUNTIME_SRC_DIR}/error.c
  ${RUNTIME_SRC_DIR}/gc.c
  ${RUNTIME_SRC_DIR}/inexact.c
  ${RUNTIME_SRC_DIR}/io.c
  ${RUNTIME_SRC_DIR}/lexer.c
  ${RUNTIME_SRC_DIR}/numeric.c
  ${RUNTIME_SRC_DIR}/port.c
  ${RUNTIME_SRC_DIR}/read.c
  ${RUNTIME_SRC_DIR}/string.c
  ${RUNTIME_SRC_DIR}/symbol.c
  ${RUNTIME_SRC_DIR}/value.c
  ${RUNTIME_SRC_DIR}/write.c
)

target_include_directories(npl_runtime PUBLIC ${RUNTIME_SRC_DIR})
# libm (inexact math); a separate library on Linux, part of libSystem on macOS.
target_link_libraries(npl_runtime PUBLIC m)
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

add_executable(numeric_stress
  ${RUNTIME_SRC_DIR}/numeric_stress.c
)
target_include_directories(numeric_stress PRIVATE ${RUNTIME_SRC_DIR})
target_link_libraries(numeric_stress PRIVATE npl_runtime)
target_compile_options(numeric_stress PRIVATE
  -std=c99 -Wall -Wextra -Wpedantic -Wno-unused-parameter -O2)
set_target_properties(numeric_stress PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/runtime)
