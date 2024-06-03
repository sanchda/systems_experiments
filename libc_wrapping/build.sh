#!/bin/bash
# First, build the library
gcc -Os -DLIBRARY_MODE -o libpeekenv.so -shared -fPIC peekenv.c -ldl
ld -r -o "./libpeekenv.so.o" -z noexecstack --format=binary "./libpeekenv.so"
gcc -DLIB_NAME="libpeekenv_so" -o peekenv peekenv.c libpeekenv.so.o
