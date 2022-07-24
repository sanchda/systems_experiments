// cc main.c -ldl
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

int main() {

  int (*foo)(void) = dlopen("libfoo.so", RTLD_LAZY);
  if (!foo)
    printf("Hey, couldn't get foo without `LD_PRELOAD`\n");
  else
    printf("Hey, I _could_ get foo without `LD_PRELOAD`\n");

  setenv("LD_LIBRARY_PATH", ".", 1);
  foo = dlopen("libfoo.so", RTLD_LAZY);
  if (!foo)
    printf("Hey, couldn't get foo _with_ `LD_PRELOAD`\n");
  else
    printf("Hey, I _could_ get foo with `LD_PRELOAD`\n");
  return 0;
}
