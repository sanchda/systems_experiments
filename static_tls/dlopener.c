#include <dlfcn.h>
#include <stdio.h>
#ifndef TARGET
#   error "TARGET is not defined"
#endif
#define STRINGIFY(x) #x
#define STR(x) STRINGIFY(x)

int main() {
  const char *target = "./"STR(TARGET);
  void *handle = dlopen(target, RTLD_LAZY);
  if (!handle) {
    fprintf(stderr, "[%s] dlopen failed: %s\n", target, dlerror());
    return 1;
  }

  // Get the function pointer
  void (*foo)(void) = dlsym(handle, "foo");
  if (!foo) {
    fprintf(stderr, "dlsym failed: %s\n", dlerror());
    return 1;
  }
  foo();

  return 0;
}

