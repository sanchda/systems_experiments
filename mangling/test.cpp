template <typename T>
T foo(const T& lhs, const T& rhs) {
    return lhs < rhs ? lhs : rhs;
}

int main() {
  foo(1.0, 2.0);
  foo(1, 2);
  foo(1ull, 2ull);
  return 0;
}
