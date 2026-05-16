cmake_minimum_required(VERSION 3.20)
project(qjsp VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ERROR_ON_WARNING "Treat warnings as errors" OFF)
option(BUILD_TESTS "Build tests" ON)

# Set compiler warning flags based on compiler
if(MSVC)
  # MSVC warning flags
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
  if(ERROR_ON_WARNING)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /WX")
  endif()
else()
  # GCC/Clang warning flags
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wshadow -Wconversion") 
  if(ERROR_ON_WARNING)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
  endif()
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

if(BUILD_TESTS)
  include(GoogleBenchAndTest)
  enable_testing()
  add_subdirectory(tests)
endif()

include(RE2)

add_library(qjsp_headers INTERFACE)
target_include_directories(qjsp_headers INTERFACE
  ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(qjsp_headers INTERFACE)
add_library(qjsp::headers ALIAS qjsp_headers)

add_library(qjsp
  src/qjsp/engine.cpp
  src/qjsp/value.cpp
  src/qjsp/string.cpp
  src/qjsp/object.cpp
  src/qjsp/array.cpp
  src/qjsp/function.cpp
  src/qjsp/primitive.cpp
  src/qjsp/lexer.cpp
  src/qjsp/lexer2.cpp
  src/qjsp/parser2_expr.cpp
  src/qjsp/parser2_primary.cpp
  src/qjsp/parser2_stmt.cpp
  src/qjsp/parser2_fn.cpp
  src/qjsp/unicode_id.cpp
  src/qjsp/varref.cpp
  src/qjsp/reg_parser_emit.cpp
  src/qjsp/reg_parser_expr.cpp
  src/qjsp/reg_parser_stmt.cpp
  src/qjsp/binding.cpp
  src/qjsp/ast_emit.cpp
  src/qjsp/eval_ast.cpp
  src/qjsp/reg_parser_lower.cpp
  src/qjsp/reg_interpreter.cpp
  src/qjsp/regexp.cpp
)
target_link_libraries(qjsp PUBLIC qjsp::headers re2::re2)
add_library(qjsp::qjsp ALIAS qjsp)

add_executable(_clangd_tu src/_clangd_tu.cpp)
target_link_libraries(_clangd_tu PRIVATE qjsp::headers)

add_executable(perf_test tools/perf_test.cpp)
target_link_libraries(perf_test PRIVATE qjsp::qjsp)
