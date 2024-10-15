#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

int main() {
  void *handle;
  double (*cosine)(double);
  char *error;

  handle = dlopen("libm.so.6", RTLD_LAZY);
  if (!handle) {
      fprintf(stderr, "%s\n", dlerror());
      exit(EXIT_FAILURE);
  }

  dlerror();    /* Clear any existing error */

  cosine = (double (*)(double)) dlsym(handle, "cos");

  error = dlerror();
  if (error != NULL) {
      fprintf(stderr, "%s\n", error);
      exit(EXIT_FAILURE);
  }

  printf("%f\n", (*cosine)(2.0));
  dlclose(handle);
  exit(EXIT_SUCCESS);
}
