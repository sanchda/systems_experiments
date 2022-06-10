#!/bin/bash
for i in `find . -name '*.c' -type f -printf '%f\n'`; do echo Building ${i%%.*}; gcc $i -o ${i%%.*}; done
