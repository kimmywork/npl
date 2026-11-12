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
set(NPLC_BIN ${NPL_BIN_DIR}/nplc)
set(NPLI_BIN ${NPL_BIN_DIR}/npli)
set(NPL_SEED ${CMAKE_SOURCE_DIR}/bootstrap/current.ssa)
set(NPL_RUNTIME_ARCHIVE ${CMAKE_BINARY_DIR}/runtime/libnpl_runtime.a)

add_custom_command(
  OUTPUT ${NPLC_BIN}
  DEPENDS ${NPL_SEED} qbe_build npl_runtime
  COMMAND ${CMAKE_COMMAND} -E make_directory ${NPL_BIN_DIR}
  COMMAND ${QBE_BIN} -o ${CMAKE_BINARY_DIR}/nplc.s ${NPL_SEED}
  COMMAND ${CMAKE_C_COMPILER} -o ${NPLC_BIN} ${CMAKE_BINARY_DIR}/nplc.s
          ${NPL_RUNTIME_ARCHIVE} -lm
  COMMENT "nplc: canonical SSA -> executable"
  VERBATIM)
add_custom_target(nplc ALL DEPENDS ${NPLC_BIN})

# The build tree mirrors the install layout with links, so build/bin/nplc and
# build/bin/npli resolve tools and libraries exactly as an installation does.
set(NPL_TREE_LINKS
  ${CMAKE_BINARY_DIR}/libexec/npl/qbe
  ${CMAKE_BINARY_DIR}/lib/npl/libnpl_runtime.a
  ${CMAKE_BINARY_DIR}/share/npl/lib
  ${CMAKE_BINARY_DIR}/share/npl/src
  ${CMAKE_BINARY_DIR}/include/npl/npl.h)
add_custom_command(
  OUTPUT ${NPL_TREE_LINKS}
  DEPENDS qbe_build npl_runtime
  COMMAND ${CMAKE_COMMAND} -E make_directory
          ${CMAKE_BINARY_DIR}/libexec/npl ${CMAKE_BINARY_DIR}/lib/npl
          ${CMAKE_BINARY_DIR}/share/npl ${CMAKE_BINARY_DIR}/include/npl
  COMMAND ${CMAKE_COMMAND} -E create_symlink ${QBE_BIN}
          ${CMAKE_BINARY_DIR}/libexec/npl/qbe
  COMMAND ${CMAKE_COMMAND} -E create_symlink ${NPL_RUNTIME_ARCHIVE}
          ${CMAKE_BINARY_DIR}/lib/npl/libnpl_runtime.a
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
  COMMAND ${CMAKE_COMMAND} -E env NPL_SKIP_NPLC_BUILD=1
          bash ${CMAKE_SOURCE_DIR}/tools/build_npli.sh ${NPLI_BIN}
  COMMENT "npli: nplc src/repl/main.npl"
  VERBATIM)
add_custom_target(npli ALL DEPENDS ${NPLI_BIN})

install(PROGRAMS ${NPLC_BIN} ${NPLI_BIN} DESTINATION bin)
install(PROGRAMS ${QBE_BIN} DESTINATION libexec/npl)
install(FILES ${NPL_RUNTIME_ARCHIVE} DESTINATION lib/npl)
# The runtime ABI for C functions called from NPL (docs/ffi.md).
install(FILES ${CMAKE_SOURCE_DIR}/runtime/npl.h DESTINATION include/npl)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/lib/ DESTINATION share/npl/lib)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/src/compiler/ DESTINATION share/npl/src/compiler
        PATTERN tests EXCLUDE)
