#include <iostream>
#include <string>
#include <array>

#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <x86intrin.h>

#define BUFFER_LEN 4096

struct Foo {
  std::string str;

  Foo(int n) {
    // Disable dynamic memory
    struct rlimit rl = {};
    setrlimit(RLIMIT_AS, &rl);
    if (MAP_FAILED != mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_ANON, -1, 0)) {
      std::cout << "<ERROR> mmap() succeeded (wrongly!)" << std::endl;
      exit(-1);
    }

    // Create some kind of dynamic string (and checksum)
    static char buf[BUFFER_LEN] = {};
    n = n < 1 ? BUFFER_LEN : n;
    n = n > BUFFER_LEN ? BUFFER_LEN : n;
    int sum = 0;
    for (int i = 0; i < n; i++) {
      char c = __rdtsc() % 26;
      buf[i] = 'a' + c;
      sum += c;
    } 
    buf[n] = '\0';
    str = std::string{buf};

    // And finally, we succeeded
    std::cout << "Successfully made string (" << std::to_string(sum) <<
                 ") of length " << std::to_string(n) << std::endl;
  };
};

#ifdef FAIL_CTOR
  #define ALLOC_MYFOO static Foo myfoo[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
#else
  #define ALLOC_MYFOO static Foo myfoo[] = {0}
#endif

#ifdef GLOBAL_ALLOC
ALLOC_MYFOO;
#endif

int foo() {
#ifndef GLOBAL_ALLOC
  try {
    ALLOC_MYFOO;
  } catch (...) {
    std::cout << "Oops\n";
    return -1;
  }
#endif
  std::cout << "Successfully called\n";
  return 0;
}

int main() {
  foo();
  foo();
}
