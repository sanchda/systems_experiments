#!/bin/bash
set -e

# Keep it tidy, I guess
BUILD_DIR=target
export CMAKE_BUILD_PARALLEL_LEVEL=4 # tuned to this this machine

# Make code pretty
clang-format -i src/*.cpp

# Build main project
mkdir -p ${BUILD_DIR}
cmake -GNinja -S. -B${BUILD_DIR}
cmake --build ${BUILD_DIR}
