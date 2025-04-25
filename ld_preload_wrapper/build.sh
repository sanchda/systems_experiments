#!/bin/bash
# consolidate GCC args into common env var
export CFLAGS="-Os -Wall -Wextra"
rm -rf build && mkdir -p build && cd build
gcc -shared -fPIC -o env ../src/env.c -ldl && \
ld -r -o env.o -z noexecstack --format=binary env && \
gcc -o peekenv -I../include ../src/peekenv.c env.o && \
gcc -o test ../src/test.c
