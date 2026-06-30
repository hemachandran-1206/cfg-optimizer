cmake_minimum_required(VERSION 3.16)
project(CFGExtractor)

set(CMAKE_CXX_STANDARD 17)

find_package(LLVM REQUIRED CONFIG
  PATHS /usr/lib/llvm-21/lib/cmake/llvm
  NO_DEFAULT_PATH)

find_package(Clang REQUIRED CONFIG
  PATHS /usr/lib/llvm-21/lib/cmake/clang
  NO_DEFAULT_PATH)

include_directories(${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_executable(cfg_extractor cfg_extractor.cpp)

target_link_libraries(cfg_extractor
  clangTooling
  clangFrontend
  clangAnalysis
  clangAST
  clangBasic
  clangSema
  clangParse
  clangLex
  clangSerialization
  clangToolingCore
  LLVMSupport
  LLVMCore
)
