#!/bin/bash
LD_PRELOAD=/lib/x86_64-linux-gnu/libSegFault.so ltrace -x '*' -n 2 ./give_a_fault
