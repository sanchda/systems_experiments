#!/bin/bash
binaries=$(find $(echo $PATH | tr ':' ' ') -type f -executable -exec sh -c 'file "$1" | grep -q ELF | grep -v -q "statically linked"' sh {} \; -print)

for binary in $binaries; do
  objdump -tT $binary | grep ' F ' | awk '{print $6}'
done | sort -u > symbols
