#!/bin/bash
for a in `seq 1 5`; do
  echo
  echo "==================== Testing alignment $((2**a))"
  clang++-13 -std=c++17 test.cpp -fnew-alignment=$((2**a)) -o test
  ./test
done
