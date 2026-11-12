cmake_minimum_required(VERSION 3.20)
# Keep in sync with VERSION; the release version is informational.
project(npl VERSION 0.1.1 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Keep explicit Debug/custom configurations; default builds run the compiler too.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Default generator and compiler (Apple clang on this host)
if(NOT CMAKE_C_COMPILER)
  set(CMAKE_C_COMPILER /usr/bin/clang)
endif()

option(NPL_TARGET "target arch: arm64|amd64|rv64" "arm64")
option(NPL_BOOTSTRAP_STAGE "bootstrap stage to build" 0)

# QBE binary (already built in-tree)
set(QBE_SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/qbe-1.3)
set(QBE_BIN ${CMAKE_BINARY_DIR}/qbe/qbe)

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
add_subdirectory(bootstrap/runtime)
add_subdirectory(stage0)
include(cmake/NplToolchain.cmake)
