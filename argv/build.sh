#!/bin/bash
gcc -c lib.c -o lib.o
gcc mutate_argv.c lib.o -o mutate_argv
gcc environ_addr.c -o environ_addr
