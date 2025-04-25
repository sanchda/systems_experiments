#!/bin/bash
mkdir -p build
sudo mkdir -p /noexec_lib
sudo mount --bind -o noexec,nodev,nosuid build /noexec_lib

# build lib.c as a shared library
gcc -shared -fPIC -o build/lib.so lib.c

# Now build main as an executable with a dynamic dependency on lib.so, but set RPATH to point to /noexec_lib
gcc -o main main.c -Lbuild -l:lib.so -Wl,-rpath,/noexec_lib

# Run the test
./main

# Might as well unmount?
sudo umount /noexec_lib
