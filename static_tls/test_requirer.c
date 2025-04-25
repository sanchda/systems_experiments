// we get `foo` from libtest.so
extern void foo();

extern void bar() {
  foo();
}
