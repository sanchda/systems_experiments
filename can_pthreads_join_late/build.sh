#!/bin/bash
echo "Main"
gcc -g -Wall -Wextra -o main main.c -ldl

echo "libfoo"
gcc -g -Wall -Wextra -o libfoo.so foo.c -shared -fPIC -lpthread
