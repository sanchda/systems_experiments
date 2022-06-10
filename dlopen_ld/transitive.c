// cc -shared -fPIC -L. -o libtransitive.so -lfoo
extern int foo();
int baz() { return foo(); }
