#!/bin/sh
set -e
cd "$(dirname "$0")"
CF="-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -pthread"
for p in server client probe agg jitter; do [ -f $p.c ] && cc $CF -o $p $p.c; done
