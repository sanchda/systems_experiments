#!/bin/bash
gcc -Og -g -shared -fPIC -o libmain.so main.c -ldl -nostartfiles -Wl,-e,_start
gcc -Og -g -o caller caller.c -L. -lmain -Wl,-rpath=.
