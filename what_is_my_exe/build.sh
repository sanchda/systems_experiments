#!/bin/bash
g++-10 -std=c++17 -Os -s -fno-asynchronous-unwind-tables -fno-unwind-tables -flto -fno-rtti -Wl,--gc-sections -Wl,--strip-all test.cpp -o test
