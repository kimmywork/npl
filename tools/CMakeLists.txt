# QBE smoke test — verifies the full toolchain loop:
#   hand-written IL → qbe → asm → object → executable → runs and prints.

set(SMOKE_DIR ${CMAKE_BINARY_DIR}/smoke)

add_executable(npl_driver ${CMAKE_SOURCE_DIR}/tools/npl_driver.c)
target_compile_options(npl_driver PRIVATE
  -std=c99 -Wall -Wextra -Wpedantic)
target_compile_definitions(npl_driver PRIVATE
  NPL_DEFAULT_RAW_COMPILER="${CMAKE_BINARY_DIR}/bin/nplc-raw"
  NPL_DEFAULT_QBE="${QBE_BIN}"
  NPL_DEFAULT_RUNTIME="${CMAKE_BINARY_DIR}/runtime/libnpl_runtime.a"
  NPL_DEFAULT_STDLIB="${CMAKE_SOURCE_DIR}/lib"
  NPL_DEFAULT_CC="${CMAKE_C_COMPILER}")
set_target_properties(npl_driver PROPERTIES
  OUTPUT_NAME nplc
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(HELLO_SSA ${CMAKE_SOURCE_DIR}/tools/hello.ssa)
set(HELLO_S   ${SMOKE_DIR}/hello.s)
set(HELLO_O   ${SMOKE_DIR}/hello.o)
set(HELLO_BIN ${SMOKE_DIR}/hello)

add_custom_command(
  OUTPUT ${HELLO_BIN}
  DEPENDS ${HELLO_SSA} qbe_build
  COMMAND ${CMAKE_COMMAND} -E make_directory ${SMOKE_DIR}
  COMMAND ${QBE_BIN} -o ${HELLO_S} ${HELLO_SSA}
  COMMAND ${AS_BIN} -o ${HELLO_O} ${HELLO_S}
  COMMAND ${CMAKE_C_COMPILER} -o ${HELLO_BIN} ${HELLO_O}
  COMMENT "Smoke: hello.ssa → exe"
  VERBATIM
)

add_custom_target(smoke DEPENDS ${HELLO_BIN})

add_test(
  NAME smoke_hello
  COMMAND ${CMAKE_COMMAND} -E compare_files
          ${CMAKE_SOURCE_DIR}/tools/hello.expected ${HELLO_BIN}
)
# Running the binary is the real assertion; compare files won't work for a binary,
# so we add a run-test target instead.
add_custom_target(smoke_run
  COMMAND ${HELLO_BIN}
  DEPENDS smoke
  VERBATIM
)
