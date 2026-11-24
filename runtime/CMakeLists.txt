# runtime/CMakeLists.txt — NPL runtime library

set(RUNTIME_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_library(npl_runtime_objects OBJECT
  ${RUNTIME_SRC_DIR}/alloc.c
  ${RUNTIME_SRC_DIR}/clock.c
  ${RUNTIME_SRC_DIR}/complex.c
  ${RUNTIME_SRC_DIR}/control.c
  ${RUNTIME_SRC_DIR}/error.c
  ${RUNTIME_SRC_DIR}/ffi.c
  ${RUNTIME_SRC_DIR}/ffi-memory.c
  ${RUNTIME_SRC_DIR}/gc.c
  ${RUNTIME_SRC_DIR}/hashtable.c
  ${RUNTIME_SRC_DIR}/inexact.c
  ${RUNTIME_SRC_DIR}/io.c
  ${RUNTIME_SRC_DIR}/lexer.c
  ${RUNTIME_SRC_DIR}/number_read.c
  ${RUNTIME_SRC_DIR}/numeric.c
  ${RUNTIME_SRC_DIR}/platform.c
  ${RUNTIME_SRC_DIR}/port.c
  ${RUNTIME_SRC_DIR}/port-ready.c
  ${RUNTIME_SRC_DIR}/read.c
  ${RUNTIME_SRC_DIR}/stack.c
  ${RUNTIME_SRC_DIR}/string.c
  ${RUNTIME_SRC_DIR}/symbol.c
  ${RUNTIME_SRC_DIR}/trampoline.c
  ${RUNTIME_SRC_DIR}/unicode.c
  ${RUNTIME_SRC_DIR}/value.c
  ${RUNTIME_SRC_DIR}/write.c
)

target_include_directories(npl_runtime_objects PUBLIC ${RUNTIME_SRC_DIR})
target_compile_options(npl_runtime_objects PRIVATE
  -std=c99 -Wall -Wextra -Wpedantic
  -Wno-unused-parameter
)
set_target_properties(npl_runtime_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(npl_runtime STATIC $<TARGET_OBJECTS:npl_runtime_objects>)
target_include_directories(npl_runtime PUBLIC ${RUNTIME_SRC_DIR})
# libm (inexact math); a separate library on Linux, part of libSystem on macOS.
target_link_libraries(npl_runtime PUBLIC m)
set_target_properties(npl_runtime PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/runtime
)

# Dynamic runtime for npli and nplc --native=shared.  It is one shared
# runtime instance: native package shared objects depend on it instead of
# each carrying a copy of the runtime.
add_library(npl_runtime_shared_objects OBJECT
  ${RUNTIME_SRC_DIR}/alloc.c
  ${RUNTIME_SRC_DIR}/clock.c
  ${RUNTIME_SRC_DIR}/complex.c
  ${RUNTIME_SRC_DIR}/control.c
  ${RUNTIME_SRC_DIR}/error.c
  ${RUNTIME_SRC_DIR}/ffi.c
  ${RUNTIME_SRC_DIR}/ffi-memory.c
  ${RUNTIME_SRC_DIR}/gc.c
  ${RUNTIME_SRC_DIR}/hashtable.c
  ${RUNTIME_SRC_DIR}/inexact.c
  ${RUNTIME_SRC_DIR}/io.c
  ${RUNTIME_SRC_DIR}/lexer.c
  ${RUNTIME_SRC_DIR}/number_read.c
  ${RUNTIME_SRC_DIR}/numeric.c
  ${RUNTIME_SRC_DIR}/platform.c
  ${RUNTIME_SRC_DIR}/port.c
  ${RUNTIME_SRC_DIR}/port-ready.c
  ${RUNTIME_SRC_DIR}/read.c
  ${RUNTIME_SRC_DIR}/stack.c
  ${RUNTIME_SRC_DIR}/string.c
  ${RUNTIME_SRC_DIR}/symbol.c
  ${RUNTIME_SRC_DIR}/trampoline.c
  ${RUNTIME_SRC_DIR}/unicode.c
  ${RUNTIME_SRC_DIR}/value.c
  ${RUNTIME_SRC_DIR}/write.c
)
target_include_directories(npl_runtime_shared_objects PUBLIC ${RUNTIME_SRC_DIR})
target_compile_options(npl_runtime_shared_objects PRIVATE
  -std=c99 -Wall -Wextra -Wpedantic
  -Wno-unused-parameter
)
target_compile_definitions(npl_runtime_shared_objects PRIVATE NPL_RUNTIME_SHARED)
set_target_properties(npl_runtime_shared_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(npl_runtime_shared SHARED $<TARGET_OBJECTS:npl_runtime_shared_objects>)
target_include_directories(npl_runtime_shared PUBLIC ${RUNTIME_SRC_DIR})
target_link_libraries(npl_runtime_shared PUBLIC m)
if(APPLE)
  # npl_stack_limit is defined by the dynamic executable so the generated
  # code can use a direct data reference; the shared runtime resolves it.
  target_link_options(npl_runtime_shared PRIVATE -undefined dynamic_lookup)
endif()
set_target_properties(npl_runtime_shared PROPERTIES
  OUTPUT_NAME npl_runtime
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/runtime
  INSTALL_NAME_DIR "@rpath"
)
if(APPLE)
  set_target_properties(npl_runtime_shared PROPERTIES
    INSTALL_RPATH "@loader_path")
else()
  set_target_properties(npl_runtime_shared PROPERTIES
    INSTALL_RPATH "$ORIGIN")
endif()
install(TARGETS npl_runtime_shared LIBRARY DESTINATION lib/npl)

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
