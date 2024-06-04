#!/bin/bash
# First, build the library
gcc -Os -g -Wall -Wextra -o env -shared -fPIC src/env.c -ldl
ld -r -o "./env.o" -z noexecstack --format=binary "./env"
gcc -Os -g -Wall -Wextra -o peekenv -Iinclude src/peekenv.c env.o
gcc -Os -g -o test src/test.c
