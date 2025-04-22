#!/bin/bash
BUILD_DIR=target
clang-format -i src/*.cpp
rm -rf ${BUILD_DIR} && mkdir ${BUILD_DIR} && cd ${BUILD_DIR}
cmake ..
cmake --build .
ln -sf ${BUILD_DIR}/compile_commands.json ../compile_commands.json
