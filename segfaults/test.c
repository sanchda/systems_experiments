int bar(int n) {
  if (n == 1) {
    *((int*)0) = 0; // Segfault, maybe?
    return 0;
  }
  return bar((n&1 ? 3*n+1 : n)/2);
}

void foo() {
  bar(27);
}

int main() {
  foo();
}
