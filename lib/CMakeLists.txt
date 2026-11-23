# Native packages.
#
# Core packages are built as both an archive (linked into programs by nplc)
# and a shared object (linked into npli so define-c-primitive resolves at
# expansion time).  External packages are shared objects loaded through the
# FFI.  The package.meta files are the source of truth; this file wires the
# currently supported builds into CMake and writes the core manifest that
# nplc reads at link time.

set(NPL_NATIVE_DIR ${CMAKE_BINARY_DIR}/lib/npl/native)
file(MAKE_DIRECTORY ${NPL_NATIVE_DIR})

set(NPL_CORE_NATIVE_LIBS npl_time npl_unix)

add_library(npl_time_objects OBJECT
  ${CMAKE_CURRENT_SOURCE_DIR}/npl/time/native/time.c
)
target_include_directories(npl_time_objects PRIVATE ${CMAKE_SOURCE_DIR}/runtime)
set_target_properties(npl_time_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(npl_time STATIC $<TARGET_OBJECTS:npl_time_objects>)
add_library(npl_time_shared SHARED $<TARGET_OBJECTS:npl_time_objects>)
set_target_properties(npl_time PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY ${NPL_NATIVE_DIR})
set_target_properties(npl_time_shared PROPERTIES
  OUTPUT_NAME npl_time
  LIBRARY_OUTPUT_DIRECTORY ${NPL_NATIVE_DIR})
if(APPLE)
  # npli exports the runtime symbols from the executable; resolve them there.
  target_link_options(npl_time_shared PRIVATE -undefined dynamic_lookup)
endif()

add_library(npl_unix_objects OBJECT
  ${CMAKE_CURRENT_SOURCE_DIR}/npl/unix/native/unix.c
)
target_include_directories(npl_unix_objects PRIVATE ${CMAKE_SOURCE_DIR}/runtime)
set_target_properties(npl_unix_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(npl_unix STATIC $<TARGET_OBJECTS:npl_unix_objects>)
add_library(npl_unix_shared SHARED $<TARGET_OBJECTS:npl_unix_objects>)
set_target_properties(npl_unix PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY ${NPL_NATIVE_DIR})
set_target_properties(npl_unix_shared PROPERTIES
  OUTPUT_NAME npl_unix
  LIBRARY_OUTPUT_DIRECTORY ${NPL_NATIVE_DIR})
if(APPLE)
  target_link_options(npl_unix_shared PRIVATE -undefined dynamic_lookup)
endif()

# External sample: no runtime link, loaded with (npl native).
add_library(npl_demo_crc32 MODULE
  ${CMAKE_CURRENT_SOURCE_DIR}/npl/demo/crc32/native/crc32.c
)
target_include_directories(npl_demo_crc32 PRIVATE ${CMAKE_SOURCE_DIR}/runtime)
set_target_properties(npl_demo_crc32 PROPERTIES
  PREFIX "lib"
  OUTPUT_NAME npl_demo_crc32
  LIBRARY_OUTPUT_DIRECTORY ${NPL_NATIVE_DIR})

# nplc reads this beside libnpl_runtime.a and links every listed archive.
file(WRITE ${NPL_NATIVE_DIR}/core.meta
  "(core-native \"npl_time\")\n(core-native \"npl_unix\")\n")

install(TARGETS npl_time npl_time_shared npl_unix npl_unix_shared npl_demo_crc32
  ARCHIVE DESTINATION lib/npl/native
  LIBRARY DESTINATION lib/npl/native)
install(FILES ${NPL_NATIVE_DIR}/core.meta DESTINATION lib/npl/native)
