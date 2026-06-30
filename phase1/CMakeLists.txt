cmake_minimum_required(VERSION 3.10)
project(cfg_extractor)

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")

include_directories(${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_executable(cfg_extractor cfg_extractor.cpp)

target_link_libraries(cfg_extractor
  clangTooling
  clangFrontend
  clangFrontendTool
  clangSerialization
  clangDriver
  clangParse
  clangSema
  clangAnalysis
  clangAST
  clangASTMatchers
  clangEdit
  clangLex
  clangBasic
  clangRewrite
  LLVMSupport
)

set_target_properties(cfg_extractor PROPERTIES
  CXX_STANDARD 17
)
