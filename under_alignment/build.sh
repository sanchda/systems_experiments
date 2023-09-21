#!/bin/bash
for a in `seq 1 6`; do
  echo
  echo "==================== Testing alignment $((2**a))"
  clang++-13 -std=c++17 test.cpp -fnew-alignment=$((2**a)) -o test
  # tcmalloc will align to 8 bytes--can we go lower?
#  clang++-13 -std=c++17 test.cpp -fnew-alignment=$((2**a)) -ltcmalloc -o test

  # TBH, I don't know whether this envvar is respected.
  MALLOC_ALIGNMENT=$((2**a)) ./test
done
