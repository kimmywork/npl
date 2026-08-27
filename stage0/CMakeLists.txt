set(STAGE0_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(nplc0
  ${STAGE0_SRC_DIR}/main.c
  ${STAGE0_SRC_DIR}/reader.c
  ${STAGE0_SRC_DIR}/ast.c
)

target_include_directories(nplc0 PRIVATE ${STAGE0_SRC_DIR})
target_compile_options(nplc0 PRIVATE -std=c99 -Wall -Wextra -Wpedantic)
set_target_properties(nplc0 PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/stage0
)
