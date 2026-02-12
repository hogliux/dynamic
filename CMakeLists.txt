cmake_minimum_required(VERSION 3.10)

project(dynamic)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_FLAGS_RELEASE -Ofast)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Set a default build type if none was specified
set(default_build_type "Release")
if(EXISTS "${CMAKE_SOURCE_DIR}/.git")
  set(default_build_type "Debug")
endif()

if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
  set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE
      STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

#increase warning level
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-missing-field-initializers -Wshadow-all -Wshorten-64-to-32 -Wstrict-aliasing -Wuninitialized -Wunused-parameter -Wsign-compare -Wint-conversion -Wconditional-uninitialized -Wreorder -Wconstant-conversion -Wunused-private-field -Wbool-conversion -Wextra-semi -Wno-ignored-qualifiers -Wunreachable-code")

  # clang on Linux/android has a bug where it can't link with -Wl,--no-undefined when sanitizers are enabled :-(
  if (NOT (CMAKE_SYSTEM_NAME MATCHES Linux))
    string(APPEND CMAKE_SHARED_LINKER_FLAGS " -Wl,--no-undefined")
  endif()
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-missing-field-initializers -Wshadow -Wstrict-aliasing -Wuninitialized -Wunused-parameter -Wsign-compare -Wreorder -Wno-ignored-qualifiers -Wunreachable-code")
  string(APPEND CMAKE_SHARED_LINKER_FLAGS " -Wl,--no-undefined")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
endif()

# Boost.PFR (vendored headers)
set(PFR_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/3rdparty/pfr/include)

add_library(dynamic STATIC dynamic.cpp CxxUtilities.hpp dynamic.hpp dynamic_detail.hpp dynamic.tpp)
target_include_directories(dynamic PUBLIC ${PFR_INCLUDE_DIR})

add_executable(example main.cpp dynamic.cpp CxxUtilities.hpp dynamic.hpp dynamic_detail.hpp dynamic.tpp)
target_include_directories(example PRIVATE ${PFR_INCLUDE_DIR})

# Unit tests
enable_testing()
add_executable(dynamic_test dynamic_test.cpp dynamic.cpp CxxUtilities.hpp dynamic.hpp dynamic_detail.hpp dynamic.tpp)
target_include_directories(dynamic_test PRIVATE ${CMAKE_SOURCE_DIR} ${PFR_INCLUDE_DIR})
add_test(NAME dynamic_test COMMAND dynamic_test)
