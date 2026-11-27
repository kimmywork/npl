cmake_minimum_required(VERSION 3.20)
# Keep in sync with VERSION; the release version is informational.
project(npl VERSION 0.1.1 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The runtime and frozen stage0 use POSIX functions such as strdup. Under
# strict C99 on glibc they need an explicit feature-test macro; Darwin
# exposes them (and MAP_ANON) by default. This keeps the build portable
# without changing the C language level or the frozen bootstrap sources.
if(NOT APPLE AND NOT WIN32)
  add_compile_definitions(_DEFAULT_SOURCE)
endif()

# Keep explicit Debug/custom configurations; default builds run the compiler too.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Default generator and compiler (Apple clang on Darwin; auto-discovered on WIN32)
if(NOT CMAKE_C_COMPILER AND NOT WIN32)
  set(CMAKE_C_COMPILER /usr/bin/clang)
endif()

option(NPL_TARGET "target arch: arm64|amd64|rv64" "arm64")
option(NPL_BOOTSTRAP_STAGE "bootstrap stage to build" 0)

# QBE binary (built in-tree)
set(QBE_SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/qbe-1.3)
set(QBE_BIN ${CMAKE_BINARY_DIR}/qbe/qbe${CMAKE_EXECUTABLE_SUFFIX})

if(WIN32)
  # Build vendored QBE directly via CMake on Windows (no GNU make/uname needed).
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/qbe)
  file(WRITE ${CMAKE_BINARY_DIR}/qbe/config.h "#define Deftgt T_amd64_win\n")
  add_executable(qbe_native
    ${QBE_SOURCE_DIR}/main.c
    ${QBE_SOURCE_DIR}/util.c
    ${QBE_SOURCE_DIR}/parse.c
    ${QBE_SOURCE_DIR}/abi.c
    ${QBE_SOURCE_DIR}/cfg.c
    ${QBE_SOURCE_DIR}/mem.c
    ${QBE_SOURCE_DIR}/ssa.c
    ${QBE_SOURCE_DIR}/alias.c
    ${QBE_SOURCE_DIR}/load.c
    ${QBE_SOURCE_DIR}/copy.c
    ${QBE_SOURCE_DIR}/fold.c
    ${QBE_SOURCE_DIR}/gvn.c
    ${QBE_SOURCE_DIR}/gcm.c
    ${QBE_SOURCE_DIR}/simpl.c
    ${QBE_SOURCE_DIR}/ifopt.c
    ${QBE_SOURCE_DIR}/live.c
    ${QBE_SOURCE_DIR}/spill.c
    ${QBE_SOURCE_DIR}/rega.c
    ${QBE_SOURCE_DIR}/emit.c
    ${QBE_SOURCE_DIR}/amd64/targ.c
    ${QBE_SOURCE_DIR}/amd64/sysv.c
    ${QBE_SOURCE_DIR}/amd64/isel.c
    ${QBE_SOURCE_DIR}/amd64/emit.c
    ${QBE_SOURCE_DIR}/amd64/winabi.c
    ${QBE_SOURCE_DIR}/arm64/targ.c
    ${QBE_SOURCE_DIR}/arm64/abi.c
    ${QBE_SOURCE_DIR}/arm64/isel.c
    ${QBE_SOURCE_DIR}/arm64/emit.c
    ${QBE_SOURCE_DIR}/rv64/targ.c
    ${QBE_SOURCE_DIR}/rv64/abi.c
    ${QBE_SOURCE_DIR}/rv64/isel.c
    ${QBE_SOURCE_DIR}/rv64/emit.c
  )
  target_include_directories(qbe_native PRIVATE ${CMAKE_BINARY_DIR}/qbe ${QBE_SOURCE_DIR})
  target_compile_definitions(qbe_native PRIVATE Deftgt=T_amd64_win)
  target_compile_options(qbe_native PRIVATE -std=c99)
  set_target_properties(qbe_native PROPERTIES
    OUTPUT_NAME qbe
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qbe
  )
  add_custom_target(qbe_build DEPENDS qbe_native)
else()
  # Build QBE as an external project (plain make, no dependency on cmake)
  add_custom_command(
    OUTPUT ${QBE_BIN}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/qbe
    COMMAND make -C ${QBE_SOURCE_DIR} -j4
    COMMAND ${CMAKE_COMMAND} -E copy ${QBE_SOURCE_DIR}/qbe ${QBE_BIN}
    WORKING_DIRECTORY ${QBE_SOURCE_DIR}
    COMMENT "Building QBE"
    VERBATIM
  )
  add_custom_target(qbe_build DEPENDS ${QBE_BIN})
endif()

# Discover the assembler/linker from clang to keep the pipeline native
set(AS_BIN /usr/bin/as)
set(LD_BIN /usr/bin/ld)

message(STATUS "NPL configuration:")
message(STATUS "  C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "  Target:     ${NPL_TARGET}")
message(STATUS "  QBE:        ${QBE_BIN}")

# Tests are driven by tools/run_tests.sh, not CTest (docs/testing.md).
add_subdirectory(tools)
add_subdirectory(runtime)
add_subdirectory(lib)
if(NOT WIN32)
  add_subdirectory(bootstrap/runtime)
  add_subdirectory(bootstrap/stage0 stage0)
endif()
include(cmake/NplToolchain.cmake)
