#!/bin/bash

echo "This is what success looks like."
g++ test.cc -o success && ./success

echo "This is what uncatchable global failure looks like."
g++ test.cc -DFAIL_CTOR -DGLOBAL_ALLOC -o fail_global && ./fail_global

echo "This is whath a caught failure looks like."
g++ test.cc -DFAIL_CTOR -o fail_fun && ./fail_fun
