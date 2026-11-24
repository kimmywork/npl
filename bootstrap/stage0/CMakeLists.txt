# stage0/CMakeLists.txt — NPL stage0 compiler (C99)

set(STAGE0_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(nplc0
  ${STAGE0_SRC_DIR}/main.c
  ${STAGE0_SRC_DIR}/reader.c
  ${STAGE0_SRC_DIR}/ast.c
  ${STAGE0_SRC_DIR}/symtab.c
  ${STAGE0_SRC_DIR}/qbe_gen.c
  ${STAGE0_SRC_DIR}/desugar.c
)

target_include_directories(nplc0 PRIVATE ${STAGE0_SRC_DIR})
target_compile_options(nplc0 PRIVATE
  -std=c99 -Wall -Wextra -Wpedantic
  -Wno-unused-parameter
)

# Pass QBE path to the compiler
target_compile_definitions(nplc0 PRIVATE
  QBE_PATH="${QBE_BIN}"
  AS_BIN="${AS_BIN}"
  CC_BIN="${CMAKE_C_COMPILER}"
  RUNTIME_LIB_PATH="${CMAKE_BINARY_DIR}/bootstrap-runtime/libnpl_runtime_frozen.a -lm"
)

# stage0 is frozen with the bootstrap chain: the programs it compiles link
# against the frozen runtime (bootstrap/runtime), not the current one.
add_dependencies(nplc0 npl_runtime_frozen)

set_target_properties(nplc0 PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/stage0
)

# ---- reader tests ----
# Run nplc0 on test .npl files and check output
set(STAGE0_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests)

# Test: simple number
add_custom_target(reader_test_number
  COMMAND nplc0 --print-ast ${STAGE0_TEST_DIR}/test_number.npl
  DEPENDS nplc0
  COMMENT "Reader test: number"
  VERBATIM
)

# Test: symbol
add_custom_target(reader_test_symbol
  COMMAND nplc0 --print-ast ${STAGE0_TEST_DIR}/test_symbol.npl
  DEPENDS nplc0
  COMMENT "Reader test: symbol"
  VERBATIM
)

# Test: nested list
add_custom_target(reader_test_nested
  COMMAND nplc0 --print-ast ${STAGE0_TEST_DIR}/test_nested.npl
  DEPENDS nplc0
  COMMENT "Reader test: nested list"
  VERBATIM
)

# Test: special forms
add_custom_target(reader_test_special
  COMMAND nplc0 --print-ast ${STAGE0_TEST_DIR}/test_special.npl
  DEPENDS nplc0
  COMMENT "Reader test: special forms"
  VERBATIM
)

# Test: quote
add_custom_target(reader_test_quote
  COMMAND nplc0 --print-ast ${STAGE0_TEST_DIR}/test_quote.npl
  DEPENDS nplc0
  COMMENT "Reader test: quote"
  VERBATIM
)

# Test: function define
add_custom_target(reader_test_function
  COMMAND nplc0 --print-ast ${STAGE0_TEST_DIR}/test_function.npl
  DEPENDS nplc0
  COMMENT "Reader test: function define"
  VERBATIM
)

# Test: string
add_custom_target(reader_test_string
  COMMAND nplc0 --print-ast ${STAGE0_TEST_DIR}/test_string.npl
  DEPENDS nplc0
  COMMENT "Reader test: string"
  VERBATIM
)

# Run all reader tests
add_custom_target(reader_tests
  DEPENDS
    reader_test_number
    reader_test_symbol
    reader_test_nested
    reader_test_special
    reader_test_quote
    reader_test_function
    reader_test_string
  COMMENT "Running all reader tests"
)
