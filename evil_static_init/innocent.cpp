#include <iostream>
#ifdef EVIL_DYNLOAD
#include <dlfcn.h>
#endif

int main(int n, char **V) {
  V++; n--;
#ifdef EVIL_DYNLOAD
  dlopen("./libevil.so", RTLD_NOW);
#endif
  while (n--)
    std::cout << *V++ <<std::endl;
  return 0;
}
