#!/bin/bash
BUILD_DIR=target
find src include -type f \( -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.cxx" -o -name "*.cc" -o -name "*.c++" \) -exec clang-format -i {} +
mkdir -p ${BUILD_DIR} && cd ${BUILD_DIR}
cmake ..
cmake --build .
