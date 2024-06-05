// Throw an error on non-Linux platforms
#ifndef __linux__
#error "Linux only"
#else

#include "inject_library.h"

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// The linker defines these symbols to the start and end of the binary, as well
extern char _binary_env_start[];
extern char _binary_env_end[];

int main(int argc, char *argv[]) {
  // Need at least one arg
  if (argc < 2) {
    fprintf(stderr, "Usage: peekenv <command> [args...]\n");
    return -1;
  }

  // Extract the library from the binary and inject into LD_PRELOAD
  add_to_ld_preload(_binary_env_start, _binary_env_end);

  // execve the command and shift the args
  int ret = execvp(argv[1], argv + 1);
  fprintf(stderr, "Cannot execvp\n");
  return ret;
}
#endif
