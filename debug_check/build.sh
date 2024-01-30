#!/bin/bash
g++-10 -g3 -Og -Wall -Wextra -Werror -std=c++20 -o test test.cpp -gsplit-dwarf
