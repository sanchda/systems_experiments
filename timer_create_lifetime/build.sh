#!/bin/bash
gcc -fsanitize=address test.c -lrt
