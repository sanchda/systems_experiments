// cc main.c -ldl
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

int main() {

//  fprintf(stderr, "[foo]: I'm about to open libfoo.so\n");
  void* handle;
//  if (!dlopen("./libfoo.so", RTLD_GLOBAL | RTLD_LAZY)) {
//    fprintf(stderr, "[foo]: Failed to do the first step (%s)\n", dlerror());
//    return -1;
//  }

  setenv("LD_LIBRARY_PATH", ".", 1);
  fprintf(stderr, "[foo]: I'm about to open libtransitive.so\n");
  if (!(handle = dlopen("libtransitive.so", RTLD_LAZY))) {
    fprintf(stderr, "[foo] Well, you failed (%s)\n", dlerror());
    return -1;
  }

  int (*baz)(void) = dlsym(handle, "baz");

  printf("Hey, the value of baz is %d\n", baz());
  return 0;
}
