#!/home/ubuntu/.pyenv/versions/3.5.10/bin/python3
#YMMV
import sys
import os

if __name__ == '__main__':
    print("[py] arg len: ", len(sys.argv))
    print("[py] argv0: ", sys.argv[0])
    print("[py] argv0 basename: ", os.path.basename(sys.argv[0]))
    if len(sys.argv) > 1:
      print("[py] argv1: ", sys.argv[1])
