#!/bin/bash
gcc -ggdb3 -O0 -Wall -Wextra -Werror -fanalyzer test.c -o test
scan-build clang -ggdb3 -O0 -Wall -Wextra -Werror test.c -o test
