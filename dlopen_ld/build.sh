#!/bin/bash
cc -shared -fPIC -o libfoo.so foo.c
cc -shared -fPIC -L. -Ldeps -o libtransitive.so transitive.c -lfoo -Wl,-rpath,'$ORIGIN/deps'
cc main.c -ldl
