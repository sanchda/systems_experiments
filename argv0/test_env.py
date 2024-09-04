#!/usr/bin/env python3
import sys

if __name__ == '__main__':
    print("[py] argv0: ", sys.argv[0])
    if len(sys.argv) > 1:
      print("[py] argv1: ", sys.argv[1])
