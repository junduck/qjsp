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

# On Windows, we might need to find Boost differently
if(WIN32)
  # Try CONFIG mode first (preferred for vcpkg or pre-built Boost)
  find_package(Boost CONFIG QUIET)
  if(NOT Boost_FOUND)
    # Fall back to MODULE mode
    find_package(Boost REQUIRED)
  endif()
else()
  find_package(Boost CONFIG REQUIRED)
endif()


list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

if(BUILD_TESTS)
  include(GoogleBenchAndTest)
  enable_testing()
  add_subdirectory(tests)
endif()

add_executable(_clangd_tu src/_clangd_tu.cpp)
