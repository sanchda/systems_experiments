#!/bin/bash
gcc -nostdlib main.c
gcc -fPIC -shared -o lolopenat.so lolopenat.c -ldl
