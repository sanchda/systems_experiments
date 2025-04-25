argv0
=====

Here are some small examples of a few things.
Mostly, the product of this exercise is a shared library you can LD_PRELOAD to print the argv0/argv1/exe of a process.
This is sometimes useful.

Here are some things I'd do to learn how this works.

```
./test
./test foo
LD_PRELOAD=$(pwd)/libargv.so

python3 -I test_python.py
./test_python.py
./test_shebang.sh foo
```
