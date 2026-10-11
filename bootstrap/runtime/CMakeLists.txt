# Frozen runtime for the permanent bootstrap chain: the runtime of commit
# 43d2a14, copied verbatim. bootstrap/bootstrap.ssa and stage0 (nplc0) emit
# code for this runtime's object layout and entry points, so they are built
# and checked against it; the current runtime (runtime/) is free to change.
# Do not edit these files.

set(FROZEN_RUNTIME_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_library(npl_runtime_frozen STATIC
  ${FROZEN_RUNTIME_DIR}/alloc.c
  ${FROZEN_RUNTIME_DIR}/clock.c
  ${FROZEN_RUNTIME_DIR}/complex.c
  ${FROZEN_RUNTIME_DIR}/control.c
  ${FROZEN_RUNTIME_DIR}/error.c
  ${FROZEN_RUNTIME_DIR}/gc.c
  ${FROZEN_RUNTIME_DIR}/inexact.c
  ${FROZEN_RUNTIME_DIR}/io.c
  ${FROZEN_RUNTIME_DIR}/lexer.c
  ${FROZEN_RUNTIME_DIR}/numeric.c
  ${FROZEN_RUNTIME_DIR}/symbol.c
  ${FROZEN_RUNTIME_DIR}/value.c
)
target_include_directories(npl_runtime_frozen PUBLIC ${FROZEN_RUNTIME_DIR})
target_link_libraries(npl_runtime_frozen PUBLIC m)
target_compile_options(npl_runtime_frozen PRIVATE
  -std=c99 -Wall -Wextra -Wpedantic -Wno-unused-parameter)
set_target_properties(npl_runtime_frozen PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bootstrap-runtime)
