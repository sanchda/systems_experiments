#define _GNU_SOURCE

#include <stdio.h>
#include <dirent.h>
#include <dlfcn.h>
#include <sys/types.h>

DIR *opendir(const char *name) {
  printf("HELLO I OPENED <%s>\n", name);
  DIR *(*real_fun)(const char *) = dlsym(RTLD_NEXT, "opendir");
  return real_fun(name);
}
