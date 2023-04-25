#!/bin/bash
g++-10 -std=c++20 -shared -fPIC evil.cpp -o libevil.so
g++-10 -std=c++20 innocent.cpp -o innocent_01                                                     # Listens politely
g++-10 -std=c++20 -L. innocent.cpp -Wl,-rpath='$ORIGIN' -Wl,--no-as-needed -levil -o innocent_02  # Does not
g++-10 -std=c++20 -DEVIL_DYNLOAD innocent.cpp -o innocent_03 -ldl                                 # Does not
