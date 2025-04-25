#!/bin/bash
gcc-11 -Wextra -Wall -Wpedantic -g -O3 -march=native -fanalyzer -std=gnu11 test.c -o test
