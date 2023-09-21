#!/bin/bash
for a in `seq 1 6`; do
  echo
  echo "==================== Testing alignment $((2**a))"
  clang++-13 -std=c++17 test.cpp -fnew-alignment=$((2**a)) -o test
  MALLOC_ALIGNMENT=$((2**a)) ./test
done
