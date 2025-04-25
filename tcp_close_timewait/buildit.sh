#!/bin/bash
for i in `find . -name '*.c' -type f -exec basename {} \;`; do echo Building ${i%%.*}; gcc $i -o ${i%%.*}; done
