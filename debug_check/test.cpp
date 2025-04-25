static inline int foo() {
  return 0;
}

static inline int bar(int x, int y) {
  return x + y;
}

static inline int baz(int x, int y) {
  auto tmp_foo = foo();
  auto tmp_bar = bar(x, y);
  return tmp_foo + tmp_bar;
}

int main() {
  auto tmp_baz = baz(1, 2);
  return tmp_baz;
}
