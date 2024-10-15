#!/bin/bash
gcc -fPIC -shared -o liborphanizer.so src/orphanizer.c
gcc -L. -o call_orphanizer src/call_orphanizer.c -lorphanizer -Wl,-rpath,'$ORIGIN'
