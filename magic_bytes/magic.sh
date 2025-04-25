#!/bin/bash
cp $(which ls) .
readelf -h ./ls | head -n 2
cat ls.magic | xxd -r - ./ls
./ls
readelf -h ./ls | head -n 2
