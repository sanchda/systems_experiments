# no_libc

Sometimes you just want to spawn a process, but you don't want to assume the 
calling process has libc (in order to emulate the behavior of runc).
