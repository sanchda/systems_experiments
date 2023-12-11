#!/bin/bash
gcc -O3 test.c -o test
gcc -O3 -static test.c -o test_noglibc
