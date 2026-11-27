# QBE smoke test — verifies the full toolchain loop:
#   hand-written IL → qbe → asm → object → executable → runs and prints.

set(SMOKE_DIR ${CMAKE_BINARY_DIR}/smoke)

set(HELLO_SSA ${CMAKE_SOURCE_DIR}/tools/hello.ssa)
set(HELLO_S   ${SMOKE_DIR}/hello.s)
set(HELLO_O   ${SMOKE_DIR}/hello.o)
set(HELLO_BIN ${SMOKE_DIR}/hello${CMAKE_EXECUTABLE_SUFFIX})

if(WIN32)
  set(SMOKE_ASM_CMD ${CMAKE_C_COMPILER} -c -o ${HELLO_O} ${HELLO_S})
else()
  set(SMOKE_ASM_CMD ${AS_BIN} -o ${HELLO_O} ${HELLO_S})
endif()

add_custom_command(
  OUTPUT ${HELLO_BIN}
  DEPENDS ${HELLO_SSA} qbe_build
  COMMAND ${CMAKE_COMMAND} -E make_directory ${SMOKE_DIR}
  COMMAND ${QBE_BIN} -o ${HELLO_S} ${HELLO_SSA}
  COMMAND ${SMOKE_ASM_CMD}
  COMMAND ${CMAKE_C_COMPILER} -o ${HELLO_BIN} ${HELLO_O}
  COMMENT "Smoke: hello.ssa → exe"
  VERBATIM
)

add_custom_target(smoke DEPENDS ${HELLO_BIN})

# Running the binary is the assertion (bootstrap/stage0/tools/check_stage0*.sh use these
# targets); there is no CTest test.
add_custom_target(smoke_run
  COMMAND ${HELLO_BIN}
  DEPENDS smoke
  VERBATIM
)
