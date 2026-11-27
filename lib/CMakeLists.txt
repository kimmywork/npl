# Native packages.
#
# Every native package is built in both artifact forms: an archive for
# nplc static linking and a shared object for npli and nplc --native=shared.
# Core packages are linked automatically when imported; external packages use
# the same native-package declaration and metadata, so both forms work.
# native.meta is the index the driver reads; core.meta stays for older
# installs and diagnostics.

set(NPL_NATIVE_DIR ${CMAKE_BINARY_DIR}/lib/npl/native)
file(MAKE_DIRECTORY ${NPL_NATIVE_DIR})

if(WIN32)
  set(NPL_CORE_NATIVE_LIBS npl_time)
else()
  set(NPL_CORE_NATIVE_LIBS npl_time npl_unix)
endif()

file(WRITE ${NPL_NATIVE_DIR}/native.meta "(native-packages\n")
macro(npl_native_meta name tier)
  set(_npl_system "")
  foreach(_npl_lib ${ARGN})
    string(APPEND _npl_system " \"${_npl_lib}\"")
  endforeach()
  file(APPEND ${NPL_NATIVE_DIR}/native.meta
    "  (native-package \"${name}\" ${tier} \"lib${name}${CMAKE_STATIC_LIBRARY_SUFFIX}\" \"lib${name}${CMAKE_SHARED_LIBRARY_SUFFIX}\" (${_npl_system}))\n")
endmacro()

# Core packages may call runtime helpers; their shared form depends on the
# one shared runtime rather than exporting a second copy.
function(npl_core_shared name)
  set_target_properties(${name}_shared PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME ${name}
    LIBRARY_OUTPUT_DIRECTORY ${NPL_NATIVE_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${NPL_NATIVE_DIR}
    ARCHIVE_OUTPUT_DIRECTORY ${NPL_NATIVE_DIR})
  target_link_libraries(${name}_shared PRIVATE npl_runtime_shared)
  if(WIN32)
    target_link_options(${name}_shared PRIVATE -Wl,--export-all-symbols)
  elseif(APPLE)
    set_target_properties(${name}_shared PROPERTIES
      BUILD_RPATH "${CMAKE_BINARY_DIR}/runtime"
      INSTALL_RPATH "@loader_path/..")
  else()
    set_target_properties(${name}_shared PROPERTIES
      BUILD_RPATH "${CMAKE_BINARY_DIR}/runtime"
      INSTALL_RPATH "$ORIGIN/..")
  endif()
endfunction()

add_library(npl_time_objects OBJECT
  ${CMAKE_CURRENT_SOURCE_DIR}/npl/time/native/time.c
)
target_include_directories(npl_time_objects PRIVATE ${CMAKE_SOURCE_DIR}/runtime)
set_target_properties(npl_time_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(npl_time STATIC $<TARGET_OBJECTS:npl_time_objects>)
set_target_properties(npl_time PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY ${NPL_NATIVE_DIR})
add_library(npl_time_shared SHARED $<TARGET_OBJECTS:npl_time_objects>)
npl_core_shared(npl_time)
npl_native_meta(npl_time core)

if(NOT WIN32)
  add_library(npl_unix_objects OBJECT
    ${CMAKE_CURRENT_SOURCE_DIR}/npl/unix/native/unix.c
  )
  target_include_directories(npl_unix_objects PRIVATE ${CMAKE_SOURCE_DIR}/runtime)
  set_target_properties(npl_unix_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)

  add_library(npl_unix STATIC $<TARGET_OBJECTS:npl_unix_objects>)
  add_library(npl_unix_shared SHARED $<TARGET_OBJECTS:npl_unix_objects>)
  set_target_properties(npl_unix PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${NPL_NATIVE_DIR})
  npl_core_shared(npl_unix)
  npl_native_meta(npl_unix core)
endif()

# External samples: no runtime link.  Their shared objects can be loaded by
# (npl native), or linked by nplc --native=shared; their archives let
# nplc --native=static produce a self-contained executable.
add_library(npl_demo_crc32_objects OBJECT
  ${CMAKE_CURRENT_SOURCE_DIR}/npl/demo/crc32/native/crc32.c
)
target_include_directories(npl_demo_crc32_objects PRIVATE ${CMAKE_SOURCE_DIR}/runtime)
set_target_properties(npl_demo_crc32_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(npl_demo_crc32_static STATIC $<TARGET_OBJECTS:npl_demo_crc32_objects>)
set_target_properties(npl_demo_crc32_static PROPERTIES
  PREFIX "lib"
  OUTPUT_NAME npl_demo_crc32
  ARCHIVE_OUTPUT_DIRECTORY ${NPL_NATIVE_DIR})
add_library(npl_demo_crc32 SHARED $<TARGET_OBJECTS:npl_demo_crc32_objects>)
set_target_properties(npl_demo_crc32 PROPERTIES
  PREFIX "lib"
  OUTPUT_NAME npl_demo_crc32
  LIBRARY_OUTPUT_DIRECTORY ${NPL_NATIVE_DIR}
  RUNTIME_OUTPUT_DIRECTORY ${NPL_NATIVE_DIR}
  ARCHIVE_OUTPUT_DIRECTORY ${NPL_NATIVE_DIR})
if(WIN32)
  target_link_libraries(npl_demo_crc32 PRIVATE npl_runtime_shared)
  target_link_options(npl_demo_crc32 PRIVATE -Wl,--export-all-symbols)
endif()
npl_native_meta(npl_demo_crc32 external)

# External zlib sample.  zlib is optional: when the development files are
# not present, only this package is skipped.
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
  add_library(npl_demo_zlib_objects OBJECT
    ${CMAKE_CURRENT_SOURCE_DIR}/npl/demo/zlib/native/zlib.c
  )
  target_include_directories(npl_demo_zlib_objects PRIVATE ${CMAKE_SOURCE_DIR}/runtime)
  set_target_properties(npl_demo_zlib_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)

  add_library(npl_demo_zlib_static STATIC $<TARGET_OBJECTS:npl_demo_zlib_objects>)
  target_link_libraries(npl_demo_zlib_static PUBLIC ZLIB::ZLIB)
  set_target_properties(npl_demo_zlib_static PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME npl_demo_zlib
    ARCHIVE_OUTPUT_DIRECTORY ${NPL_NATIVE_DIR})
  add_library(npl_demo_zlib SHARED $<TARGET_OBJECTS:npl_demo_zlib_objects>)
  target_link_libraries(npl_demo_zlib PRIVATE ZLIB::ZLIB)
  if(WIN32)
    target_link_libraries(npl_demo_zlib PRIVATE npl_runtime_shared)
    target_link_options(npl_demo_zlib PRIVATE -Wl,--export-all-symbols)
  endif()
  set_target_properties(npl_demo_zlib PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME npl_demo_zlib
    LIBRARY_OUTPUT_DIRECTORY ${NPL_NATIVE_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${NPL_NATIVE_DIR}
    ARCHIVE_OUTPUT_DIRECTORY ${NPL_NATIVE_DIR})
  install(TARGETS npl_demo_zlib npl_demo_zlib_static
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib/npl/native
    LIBRARY DESTINATION lib/npl/native)
  npl_native_meta(npl_demo_zlib external z)
endif()

file(APPEND ${NPL_NATIVE_DIR}/native.meta ")\n")

# nplc reads these beside libnpl_runtime.a.
if(WIN32)
  file(WRITE ${NPL_NATIVE_DIR}/core.meta "(core-native \"npl_time\")\n")
  install(TARGETS npl_time npl_time_shared npl_demo_crc32 npl_demo_crc32_static
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib/npl/native
    LIBRARY DESTINATION lib/npl/native)
else()
  file(WRITE ${NPL_NATIVE_DIR}/core.meta
    "(core-native \"npl_time\")\n(core-native \"npl_unix\")\n")
  install(TARGETS npl_time npl_time_shared npl_unix npl_unix_shared
          npl_demo_crc32 npl_demo_crc32_static
    ARCHIVE DESTINATION lib/npl/native
    LIBRARY DESTINATION lib/npl/native)
endif()
install(FILES ${NPL_NATIVE_DIR}/core.meta ${NPL_NATIVE_DIR}/native.meta
  DESTINATION lib/npl/native)
