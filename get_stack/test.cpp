#include <pthread.h>
#include <unistd.h>
#include <cstddef>
#include <cstring>
#include <iostream>

static const uintptr_t get_argc() {
  // The trick here is that we can get the user environment
  // through a global, and the args are just before it.
  void **args = reinterpret_cast<void**>(environ);
  int argc = 0;

  // Find argc.  It's only sort of argc, since we're off by one, but whatever.
  // This limit is actually higher
  for (int i = 2; i < 25; i++) {
    if (i - 2 == reinterpret_cast<intptr_t>(args[-i])) {
      argc = i - 1;
      break;
    }
  }
  return (uintptr_t)&args[argc];
}

static __attribute__((noinline)) const std::byte *get_stack_end_address() {
  void *stack_addr;
  size_t stack_size;
  pthread_attr_t attrs;
  pthread_getattr_np(pthread_self(), &attrs);
  pthread_attr_getstack(&attrs, &stack_addr, &stack_size);
  pthread_attr_destroy(&attrs);
  return static_cast<std::byte *>(stack_addr) + stack_size;
}

void bar() {
  long long buf[1024];
  memset(buf, 0, sizeof(buf));
  const std::byte *stack_end = get_stack_end_address();
  printf("Stack end is 0x%p\n", stack_end);
  char foo;
  printf("Foo is 0x%p\n", &foo);
  printf("Argc is 0x%lx\n", get_argc());
  printf("Argc is 0x%lx bytes short\n", ((uintptr_t)get_stack_end_address()) - get_argc());
  printf("Environ is 0x%lx bytes short\n", ((uintptr_t)get_stack_end_address()) - (uintptr_t)environ);
}

void foo() {
  return bar();
}

int main() {
  foo();
  return 0;
}
