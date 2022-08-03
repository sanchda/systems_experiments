#define _GNU_SOURCE

#include <dlfcn.h>

int main() {
  dlsym(RTLD_NEXT, "malloc");
  return 0;
}
