#!/bin/bash
mkdir -p /tmp/my_jail/bin
mkdir -p /tmp/my_jail/lib/x86_64-linux-gnu
mkdir -p /tmp/my_jail/lib64
cp /lib/x86_64-linux-gnu/libgcc_s.so.1 /tmp/my_jail/lib/x86_64-linux-gnu/libgcc_s.so.1
cp /lib/x86_64-linux-gnu/libpthread.so.0 /tmp/my_jail/lib/x86_64-linux-gnu/libpthread.so.0
cp /lib/x86_64-linux-gnu/libc.so.6 /tmp/my_jail/lib/x86_64-linux-gnu/libc.so.6
cp /lib64/ld-linux-x86-64.so.2 /tmp/my_jail/lib64/ld-linux-x86-64.so.2
cargo build --bin hello &&
cargo build --bin run &&
sudo cp target/debug/hello /tmp/my_jail/bin/hello &&
sudo strace -f -o strace.out -s 250000 -v ./target/debug/run
