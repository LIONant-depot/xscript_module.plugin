cmake_minimum_required(VERSION 3.10)
project(xscript_module_compiler LANGUAGES C CXX)
set(TARGET_PROJECT "xscript_module_compiler")
set(COMPONENT_REGISTRY "")

include(FetchContent)
FetchContent_Declare(
  xcmake_tools
  GIT_REPOSITORY https://github.com/LIONant-depot/xcmake_tools.git
  GIT_TAG main
  GIT_SHALLOW TRUE
  SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/xcmake_tools"
)
FetchContent_MakeAvailable(xcmake_tools)
include(${CMAKE_BINARY_DIR}/_deps/xcmake_tools/Common.cmake)

add_executable(${TARGET_PROJECT}
  "source/Compiler/main.cpp"
  "source/Compiler/xscript_module_compiler.cpp"
  "source/Compiler/xscript_module_compiler.h"
)

source_group("" FILES
  "source/Compiler/main.cpp"
)

# Allow access to other plugins
target_include_directories(${TARGET_PROJECT} PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/../../>)

add_subdirectory("build/dependency" "${CMAKE_CURRENT_BINARY_DIR}/xscript_module_compiler")

ProcessComponents()
