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

add_library(qjsp_headers INTERFACE)
target_include_directories(qjsp_headers INTERFACE
  ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(qjsp_headers INTERFACE)
add_library(qjsp::headers ALIAS qjsp_headers)

add_library(qjsp
  src/qjsp/runtime.cpp
  src/qjsp/context.cpp
  src/qjsp/string.cpp
  src/qjsp/shape.cpp
  src/qjsp/object.cpp
  src/qjsp/lexer.cpp
  src/qjsp/varref.cpp
  src/qjsp/reg_parser_emit.cpp
  src/qjsp/reg_parser_expr.cpp
  src/qjsp/reg_parser_stmt.cpp
  src/qjsp/reg_parser_lower.cpp
  src/qjsp/reg_interpreter.cpp
)
target_link_libraries(qjsp PUBLIC qjsp::headers)
add_library(qjsp::qjsp ALIAS qjsp)

add_executable(_clangd_tu src/_clangd_tu.cpp)
target_link_libraries(_clangd_tu PRIVATE qjsp::headers)
