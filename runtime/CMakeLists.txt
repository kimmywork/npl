# runtime/CMakeLists.txt — NPL runtime library

set(RUNTIME_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_library(npl_runtime_objects OBJECT
  ${RUNTIME_SRC_DIR}/alloc.c
  ${RUNTIME_SRC_DIR}/clock.c
  ${RUNTIME_SRC_DIR}/complex.c
  ${RUNTIME_SRC_DIR}/control.c
  ${RUNTIME_SRC_DIR}/dynamic-context.c
  ${RUNTIME_SRC_DIR}/coordinator.c
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
  ${RUNTIME_SRC_DIR}/read-buffer.c
  ${RUNTIME_SRC_DIR}/shared-lock.c
  ${RUNTIME_SRC_DIR}/stack.c
  ${RUNTIME_SRC_DIR}/string.c
  ${RUNTIME_SRC_DIR}/symbol.c
  ${RUNTIME_SRC_DIR}/thread.c
  ${RUNTIME_SRC_DIR}/thread-sched.c
  ${RUNTIME_SRC_DIR}/thread-lifecycle.c
  ${RUNTIME_SRC_DIR}/thread-start-root.c
  ${RUNTIME_SRC_DIR}/trampoline.c
  ${RUNTIME_SRC_DIR}/unicode.c
  ${RUNTIME_SRC_DIR}/value.c
  ${RUNTIME_SRC_DIR}/virtual-thread.c
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
  ${RUNTIME_SRC_DIR}/dynamic-context.c
  ${RUNTIME_SRC_DIR}/coordinator.c
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
  ${RUNTIME_SRC_DIR}/read-buffer.c
  ${RUNTIME_SRC_DIR}/shared-lock.c
  ${RUNTIME_SRC_DIR}/stack.c
  ${RUNTIME_SRC_DIR}/string.c
  ${RUNTIME_SRC_DIR}/symbol.c
  ${RUNTIME_SRC_DIR}/thread.c
  ${RUNTIME_SRC_DIR}/thread-sched.c
  ${RUNTIME_SRC_DIR}/thread-lifecycle.c
  ${RUNTIME_SRC_DIR}/thread-start-root.c
  ${RUNTIME_SRC_DIR}/trampoline.c
  ${RUNTIME_SRC_DIR}/unicode.c
  ${RUNTIME_SRC_DIR}/value.c
  ${RUNTIME_SRC_DIR}/virtual-thread.c
  ${RUNTIME_SRC_DIR}/write.c
)
target_include_directories(npl_runtime_shared_objects PUBLIC ${RUNTIME_SRC_DIR})
target_compile_options(npl_runtime_shared_objects PRIVATE
  -std=c99 -Wall -Wextra -Wpedantic
  -Wno-unused-parameter
)
# The old NPL_RUNTIME_SHARED C macro selected who defined npl_stack_limit /
# npl_opaque_zero.  The executable now owns both slots in every link mode
# (program.npl), so the runtime needs no C-side link-mode split.
set_target_properties(npl_runtime_shared_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(npl_runtime_shared SHARED $<TARGET_OBJECTS:npl_runtime_shared_objects>)
target_include_directories(npl_runtime_shared PUBLIC ${RUNTIME_SRC_DIR})
target_link_libraries(npl_runtime_shared PUBLIC m)
if(APPLE)
  # The generated executable owns the npl_stack_limit TLS slot and the
  # npl_opaque_zero data slot (program.npl emits them).  The shared runtime
  # declares both extern and resolves them through the loader; every native
  # package reaches the same slots, never independent copies.
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

# C-only harnesses have no generated IL, so they must supply the two data
# slots the generated executable normally defines.  Do NOT link this into
# npl_runtime itself: generated programs define the same symbols.
add_executable(gc_stress
  ${RUNTIME_SRC_DIR}/gc_stress.c
  ${RUNTIME_SRC_DIR}/standalone-slots.c
)
target_include_directories(gc_stress PRIVATE ${RUNTIME_SRC_DIR})
target_link_libraries(gc_stress PRIVATE npl_runtime)
target_compile_options(gc_stress PRIVATE
  -std=c99 -Wall -Wextra -Wpedantic -Wno-unused-parameter -O2)
set_target_properties(gc_stress PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/runtime)

add_executable(numeric_stress
  ${RUNTIME_SRC_DIR}/numeric_stress.c
  ${RUNTIME_SRC_DIR}/standalone-slots.c
)
target_include_directories(numeric_stress PRIVATE ${RUNTIME_SRC_DIR})
target_link_libraries(numeric_stress PRIVATE npl_runtime)
target_compile_options(numeric_stress PRIVATE
  -std=c99 -Wall -Wextra -Wpedantic -Wno-unused-parameter -O2)
set_target_properties(numeric_stress PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/runtime)

# T1 acceptance: native TLS current context, per-thread stack limits and
# dynamic state, explicit attach/detach, and GC visibility of all roots.
add_executable(thread_test
  ${RUNTIME_SRC_DIR}/thread_test.c
  ${RUNTIME_SRC_DIR}/standalone-slots.c
)
target_include_directories(thread_test PRIVATE ${RUNTIME_SRC_DIR})
target_link_libraries(thread_test PRIVATE npl_runtime)
target_compile_options(thread_test PRIVATE
  -std=c99 -Wall -Wextra -Wpedantic -Wno-unused-parameter -O2)
set_target_properties(thread_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/runtime)

# T2 acceptance: SRFI 18 heap objects, predicates, time, exceptions, and
# GC visibility of the rooted current-thread object.
add_executable(thread_object_test
  ${RUNTIME_SRC_DIR}/thread_object_test.c
  ${RUNTIME_SRC_DIR}/standalone-slots.c
)
target_include_directories(thread_object_test PRIVATE ${RUNTIME_SRC_DIR})
target_link_libraries(thread_object_test PRIVATE npl_runtime)
target_compile_options(thread_object_test PRIVATE
  -std=c99 -Wall -Wextra -Wpedantic -Wno-unused-parameter -O2)
set_target_properties(thread_object_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/runtime)

# T2 acceptance: 1:1 pthread start/join, timeout, cooperative termination,
# mutex and condition-variable operations.
add_executable(thread_lifecycle_test
  ${RUNTIME_SRC_DIR}/thread_lifecycle_test.c
  ${RUNTIME_SRC_DIR}/standalone-slots.c
)
target_include_directories(thread_lifecycle_test PRIVATE ${RUNTIME_SRC_DIR})
target_link_libraries(thread_lifecycle_test PRIVATE npl_runtime)
target_compile_options(thread_lifecycle_test PRIVATE
  -std=c99 -Wall -Wextra -Wpedantic -Wno-unused-parameter -O2)
set_target_properties(thread_lifecycle_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/runtime)

# T1.3 acceptance: a blocking libc call publishes its context for GC.
add_executable(blocking_io_test
  ${RUNTIME_SRC_DIR}/blocking_io_test.c
  ${RUNTIME_SRC_DIR}/standalone-slots.c
)
target_include_directories(blocking_io_test PRIVATE ${RUNTIME_SRC_DIR})
target_link_libraries(blocking_io_test PRIVATE npl_runtime)
target_compile_options(blocking_io_test PRIVATE
  -std=c99 -Wall -Wextra -Wpedantic -Wno-unused-parameter -O2)
set_target_properties(blocking_io_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/runtime)

# T1.3 acceptance: shared-object lock under concurrent mutation + GC.
add_executable(shared_lock_test
  ${RUNTIME_SRC_DIR}/shared_lock_test.c
  ${RUNTIME_SRC_DIR}/standalone-slots.c
)
target_include_directories(shared_lock_test PRIVATE ${RUNTIME_SRC_DIR})
target_link_libraries(shared_lock_test PRIVATE npl_runtime)
target_compile_options(shared_lock_test PRIVATE
  -std=c99 -Wall -Wextra -Wpedantic -Wno-unused-parameter -O2)
set_target_properties(shared_lock_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/runtime)

# T1.3 acceptance: multiple mutators, world-stop park/resume, scanning of
# parked threads' C stacks and register snapshots, cancellation, blocking.
add_executable(safepoint_test
  ${RUNTIME_SRC_DIR}/safepoint_test.c
  ${RUNTIME_SRC_DIR}/standalone-slots.c
)
target_include_directories(safepoint_test PRIVATE ${RUNTIME_SRC_DIR})
target_link_libraries(safepoint_test PRIVATE npl_runtime)
target_compile_options(safepoint_test PRIVATE
  -std=c99 -Wall -Wextra -Wpedantic -Wno-unused-parameter -O2)
set_target_properties(safepoint_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/runtime)

# T2b acceptance: one-shot scheduler continuation, N virtual threads on one
# carrier, dynamic-state isolation and GC survival of suspended tokens.
add_executable(virtual_thread_test
  ${RUNTIME_SRC_DIR}/virtual_thread_test.c
  ${RUNTIME_SRC_DIR}/standalone-slots.c
)
target_include_directories(virtual_thread_test PRIVATE ${RUNTIME_SRC_DIR})
target_link_libraries(virtual_thread_test PRIVATE npl_runtime)
target_compile_options(virtual_thread_test PRIVATE
  -std=c99 -Wall -Wextra -Wpedantic -Wno-unused-parameter -O2)
set_target_properties(virtual_thread_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/runtime)
