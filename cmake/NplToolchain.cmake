# Build nplc from the canonical SSA seed and npli with that nplc, and define
# the install layout that nplc and npli resolve relative to themselves:
#
#   <prefix>/bin/nplc, <prefix>/bin/npli
#   <prefix>/libexec/npl/qbe
#   <prefix>/lib/npl/libnpl_runtime.a
#   <prefix>/share/npl/lib/...        standard and (npl ...) libraries
#   <prefix>/share/npl/src/compiler/  frontend sources included by (npl internal frontend)
#   <prefix>/include/npl/npl.h        runtime ABI for C functions (docs/ffi.md)
#
# nplc and npli have no build-time paths compiled in except npli's REPL-STDLIB
# fallback, which install overrides by locating share/npl/lib first.

set(NPL_BIN_DIR ${CMAKE_BINARY_DIR}/bin)
set(NPLC_BIN ${NPL_BIN_DIR}/nplc${CMAKE_EXECUTABLE_SUFFIX})
set(NPLI_BIN ${NPL_BIN_DIR}/npli${CMAKE_EXECUTABLE_SUFFIX})
set(NPL_SEED ${CMAKE_SOURCE_DIR}/bootstrap/current.ssa)
set(NPL_RUNTIME_ARCHIVE ${CMAKE_BINARY_DIR}/runtime/libnpl_runtime.a)

if(WIN32)
  set(NPLC_LINK_LIBS -lm -Wl,-Bstatic -lpthread -Wl,-Bdynamic -lpsapi)
else()
  set(NPLC_LINK_LIBS -lm)
endif()

add_custom_command(
  OUTPUT ${NPLC_BIN}
  DEPENDS ${NPL_SEED} qbe_build npl_runtime
  COMMAND ${CMAKE_COMMAND} -E make_directory ${NPL_BIN_DIR}
  COMMAND ${QBE_BIN} -o ${CMAKE_BINARY_DIR}/nplc.s ${NPL_SEED}
  COMMAND ${CMAKE_C_COMPILER} -o ${NPLC_BIN} ${CMAKE_BINARY_DIR}/nplc.s
          ${NPL_RUNTIME_ARCHIVE} ${NPLC_LINK_LIBS}
  COMMENT "nplc: canonical SSA -> executable"
  VERBATIM)
add_custom_target(nplc ALL DEPENDS ${NPLC_BIN})

if(WIN32)
  file(GLOB_RECURSE NPLI_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/repl/*.npl
    ${CMAKE_SOURCE_DIR}/src/compiler/*.npl
    ${CMAKE_SOURCE_DIR}/lib/*.sld
    ${CMAKE_SOURCE_DIR}/lib/*.scm)

  # Mirror the install layout by copy on Windows (symlinks need elevation or
  # Developer Mode).  Runtime and core package DLLs are also placed in bin/
  # because Windows searches the executable directory for DLL dependencies.
  set(NPL_RUNTIME_SHARED ${CMAKE_BINARY_DIR}/lib/npl/libnpl_runtime.dll)
  set(NPL_RUNTIME_IMPLIB ${CMAKE_BINARY_DIR}/lib/npl/libnpl_runtime.dll.a)
  set(NPL_RUNTIME_SLOTS ${CMAKE_BINARY_DIR}/lib/npl/npl_runtime_slots.o)
  set(NPL_TREE_LINKS
    ${CMAKE_BINARY_DIR}/libexec/npl/qbe${CMAKE_EXECUTABLE_SUFFIX}
    ${CMAKE_BINARY_DIR}/lib/npl/libnpl_runtime.a
    ${NPL_RUNTIME_SHARED}
    ${NPL_RUNTIME_IMPLIB}
    ${NPL_RUNTIME_SLOTS}
    ${CMAKE_BINARY_DIR}/bin/libnpl_runtime.dll
    ${CMAKE_BINARY_DIR}/include/npl/npl.h
    ${CMAKE_BINARY_DIR}/share/npl/lib/scheme/base.sld)
  add_custom_command(
    OUTPUT ${NPL_TREE_LINKS}
    DEPENDS ${QBE_BIN} qbe_build npl_runtime npl_runtime_shared npl_runtime_win_slots
            ${NPLI_SOURCES}
    COMMAND ${CMAKE_COMMAND} -E make_directory
            ${CMAKE_BINARY_DIR}/bin ${CMAKE_BINARY_DIR}/libexec/npl
            ${CMAKE_BINARY_DIR}/lib/npl ${CMAKE_BINARY_DIR}/share/npl
            ${CMAKE_BINARY_DIR}/include/npl
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${QBE_BIN}
            ${CMAKE_BINARY_DIR}/libexec/npl/qbe${CMAKE_EXECUTABLE_SUFFIX}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${NPL_RUNTIME_ARCHIVE}
            ${CMAKE_BINARY_DIR}/lib/npl/libnpl_runtime.a
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/runtime/libnpl_runtime.dll
            ${NPL_RUNTIME_SHARED}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/runtime/libnpl_runtime.dll.a
            ${NPL_RUNTIME_IMPLIB}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/runtime/npl_runtime_slots.o
            ${NPL_RUNTIME_SLOTS}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/runtime/libnpl_runtime.dll
            ${CMAKE_BINARY_DIR}/bin/libnpl_runtime.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/runtime/npl.h
            ${CMAKE_BINARY_DIR}/include/npl/npl.h
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/lib
            ${CMAKE_BINARY_DIR}/share/npl/lib
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_BINARY_DIR}/share/npl/src
    COMMENT "build tree: install-layout copies (Windows)"
    VERBATIM)
  add_custom_target(npl_tree_layout ALL DEPENDS ${NPL_TREE_LINKS})
  add_dependencies(nplc npl_tree_layout)

  # Build npli.exe (REPL and script runner) on Windows against the shared
  # runtime DLL and core native package DLL so npli -l <ext>.dll and
  # (npl native) share one runtime instance (GC, roots, threads, symbols).
  set(NPLI_WIN_WRAPPER ${CMAKE_BINARY_DIR}/repl-win.npl)
  file(WRITE ${NPLI_WIN_WRAPPER}
    "(import (scheme base)\n"
    "        (scheme read)\n"
    "        (scheme write)\n"
    "        (scheme process-context)\n"
    "        (scheme file)\n"
    "        (npl process)\n"
    "        (scheme repl)\n"
    "        (scheme internal evaluator)\n"
    "        (npl internal repl-io)\n"
    "        (only (npl internal primitive) read-file))\n"
    "(define REPL-STDLIB \"${CMAKE_SOURCE_DIR}/lib\")\n"
    "(include \"${CMAKE_SOURCE_DIR}/src/repl/main.npl\")\n")
  add_custom_command(
    OUTPUT ${NPLI_BIN} ${CMAKE_BINARY_DIR}/bin/libnpl_time.dll
    DEPENDS ${NPLC_BIN} ${NPL_TREE_LINKS} ${NPLI_SOURCES}
            npl_runtime_shared npl_runtime_win_slots npl_time_shared
    COMMAND ${NPLC_BIN} --native=shared -c ${NPLI_WIN_WRAPPER}
            -o ${CMAKE_BINARY_DIR}/repl-win.o
    COMMAND ${CMAKE_C_COMPILER} -o ${NPLI_BIN} ${CMAKE_BINARY_DIR}/repl-win.o
            ${NPL_RUNTIME_SLOTS}
            ${NPL_RUNTIME_IMPLIB}
            -Wl,-u,npl_time_now_prim
            ${CMAKE_BINARY_DIR}/lib/npl/native/libnpl_time.dll.a
            ${NPLC_LINK_LIBS}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/lib/npl/native/libnpl_time.dll
            ${CMAKE_BINARY_DIR}/bin/libnpl_time.dll
    COMMENT "npli: nplc src/repl/main.npl (Windows shared DLL runtime)"
    VERBATIM)
  # Build-time define-c-struct layouts for core Windows native packages so
  # npli.exe can import (npl time) without invoking clang at runtime.
  set(NPL_STRUCT_CACHE ${CMAKE_BINARY_DIR}/lib/npl/native/c-struct-layouts.meta)
  set(NPL_STRUCT_CACHE_WIN_SRC ${CMAKE_BINARY_DIR}/c-struct-cache-win.npl)
  file(WRITE ${NPL_STRUCT_CACHE_WIN_SRC}
    "(import (scheme base)\n"
    "        (npl time))\n"
    "(define cache-ready #t)\n")
  add_custom_command(
    OUTPUT ${NPL_STRUCT_CACHE}
    DEPENDS ${NPLC_BIN} ${NPL_TREE_LINKS} ${CMAKE_SOURCE_DIR}/lib/npl/time.sld
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/lib/npl/native
    COMMAND ${NPLC_BIN} --emit=c-structs -o ${NPL_STRUCT_CACHE}
            ${NPL_STRUCT_CACHE_WIN_SRC}
    COMMENT "nplc: define-c-struct layout cache (Windows)"
    VERBATIM)
  add_custom_target(npl_c_struct_layouts ALL DEPENDS ${NPL_STRUCT_CACHE})

  add_custom_target(npli ALL DEPENDS ${NPLI_BIN} ${CMAKE_BINARY_DIR}/bin/libnpl_time.dll)
  add_dependencies(npli npl_time npl_time_shared npl_runtime_shared
                   npl_c_struct_layouts
                   npl_demo_crc32 npl_demo_crc32_static)
  if(TARGET npl_demo_zlib)
    add_dependencies(npli npl_demo_zlib npl_demo_zlib_static)
  endif()

  install(PROGRAMS ${NPLC_BIN} ${NPLI_BIN} DESTINATION bin)
  install(PROGRAMS ${QBE_BIN} DESTINATION libexec/npl)
  install(FILES ${NPL_RUNTIME_ARCHIVE} DESTINATION lib/npl)
  install(FILES ${NPL_STRUCT_CACHE} DESTINATION lib/npl/native)
  install(FILES ${CMAKE_SOURCE_DIR}/runtime/npl.h DESTINATION include/npl)
  install(DIRECTORY ${CMAKE_SOURCE_DIR}/lib/ DESTINATION share/npl/lib)
  install(DIRECTORY ${CMAKE_SOURCE_DIR}/src/compiler/ DESTINATION share/npl/src/compiler
          PATTERN tests EXCLUDE)
else()
# Build-time define-c-struct layouts.  The generator imports the core native
# packages once; later imports read this file instead of invoking the host C
# compiler.  Install it beside core.meta so an installed npli needs no cc.
set(NPL_STRUCT_CACHE ${CMAKE_BINARY_DIR}/lib/npl/native/c-struct-layouts.meta)
add_custom_command(
  OUTPUT ${NPL_STRUCT_CACHE}
  DEPENDS ${NPLC_BIN}
          ${CMAKE_SOURCE_DIR}/tools/c-struct-cache.npl
          ${CMAKE_SOURCE_DIR}/lib/npl/time.sld
          ${CMAKE_SOURCE_DIR}/lib/npl/unix.sld
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/lib/npl/native
  COMMAND ${NPLC_BIN} --emit=c-structs -o ${NPL_STRUCT_CACHE}
          ${CMAKE_SOURCE_DIR}/tools/c-struct-cache.npl
  COMMENT "nplc: define-c-struct layout cache"
  VERBATIM)
add_custom_target(npl_c_struct_layouts ALL DEPENDS ${NPL_STRUCT_CACHE})

# The build tree mirrors the install layout with links, so build/bin/nplc and
# build/bin/npli resolve tools and libraries exactly as an installation does.
set(NPL_RUNTIME_SHARED ${CMAKE_BINARY_DIR}/lib/npl/libnpl_runtime${CMAKE_SHARED_LIBRARY_SUFFIX})
set(NPL_TREE_LINKS
  ${CMAKE_BINARY_DIR}/libexec/npl/qbe
  ${CMAKE_BINARY_DIR}/lib/npl/libnpl_runtime.a
  ${NPL_RUNTIME_SHARED}
  ${CMAKE_BINARY_DIR}/share/npl/lib
  ${CMAKE_BINARY_DIR}/share/npl/src
  ${CMAKE_BINARY_DIR}/include/npl/npl.h)
add_custom_command(
  OUTPUT ${NPL_TREE_LINKS}
  DEPENDS qbe_build npl_runtime npl_runtime_shared
  COMMAND ${CMAKE_COMMAND} -E make_directory
          ${CMAKE_BINARY_DIR}/libexec/npl ${CMAKE_BINARY_DIR}/lib/npl
          ${CMAKE_BINARY_DIR}/share/npl ${CMAKE_BINARY_DIR}/include/npl
  COMMAND ${CMAKE_COMMAND} -E create_symlink ${QBE_BIN}
          ${CMAKE_BINARY_DIR}/libexec/npl/qbe
  COMMAND ${CMAKE_COMMAND} -E create_symlink ${NPL_RUNTIME_ARCHIVE}
          ${CMAKE_BINARY_DIR}/lib/npl/libnpl_runtime.a
  COMMAND ${CMAKE_COMMAND} -E create_symlink
          ${CMAKE_BINARY_DIR}/runtime/libnpl_runtime${CMAKE_SHARED_LIBRARY_SUFFIX}
          ${NPL_RUNTIME_SHARED}
  COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_SOURCE_DIR}/lib
          ${CMAKE_BINARY_DIR}/share/npl/lib
  COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_SOURCE_DIR}/src
          ${CMAKE_BINARY_DIR}/share/npl/src
  COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_SOURCE_DIR}/runtime/npl.h
          ${CMAKE_BINARY_DIR}/include/npl/npl.h
  COMMENT "build tree: install-layout links"
  VERBATIM)
add_custom_target(npl_tree_layout ALL DEPENDS ${NPL_TREE_LINKS})
add_dependencies(nplc npl_tree_layout)

# npli is compiled from NPL sources by nplc; rebuild when any of them change.
file(GLOB_RECURSE NPLI_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/src/repl/*.npl
  ${CMAKE_SOURCE_DIR}/src/compiler/*.npl
  ${CMAKE_SOURCE_DIR}/lib/*.sld
  ${CMAKE_SOURCE_DIR}/lib/*.scm)
add_custom_command(
  OUTPUT ${NPLI_BIN}
  DEPENDS ${NPLC_BIN} ${NPL_TREE_LINKS} ${NPLI_SOURCES} ${CMAKE_SOURCE_DIR}/tools/build_npli.sh
          npl_time_shared npl_unix_shared
  COMMAND ${CMAKE_COMMAND} -E env NPL_SKIP_NPLC_BUILD=1
          bash ${CMAKE_SOURCE_DIR}/tools/build_npli.sh ${NPLI_BIN}
  COMMENT "npli: nplc src/repl/main.npl"
  VERBATIM)
add_custom_target(npli ALL DEPENDS ${NPLI_BIN})
# The gate builds only nplc and npli, so npli also pulls in every native
# artifact that nplc's static or shared link modes need at test time.
add_dependencies(npli npl_time npl_unix npl_time_shared npl_unix_shared
                 npl_runtime_shared npl_c_struct_layouts
                 npl_demo_crc32 npl_demo_crc32_static)
if(TARGET npl_demo_zlib)
  add_dependencies(npli npl_demo_zlib npl_demo_zlib_static)
endif()

install(PROGRAMS ${NPLC_BIN} ${NPLI_BIN} DESTINATION bin)
install(PROGRAMS ${QBE_BIN} DESTINATION libexec/npl)
install(FILES ${NPL_RUNTIME_ARCHIVE} DESTINATION lib/npl)
install(FILES ${NPL_STRUCT_CACHE} DESTINATION lib/npl/native)
# The runtime ABI for C functions called from NPL (docs/ffi.md).
install(FILES ${CMAKE_SOURCE_DIR}/runtime/npl.h DESTINATION include/npl)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/lib/ DESTINATION share/npl/lib)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/src/compiler/ DESTINATION share/npl/src/compiler
        PATTERN tests EXCLUDE)
endif()
