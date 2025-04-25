#!/bin/bash
gcc -I. test.c -o test

# Now build a library
gcc -shared -fPIC lib.c -o libargv.so
