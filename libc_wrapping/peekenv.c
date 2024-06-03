#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef LIBRARY_MODE
#include <dlfcn.h>

static void print(const char *msg, const char *name, const char *value) {
  if (!name || !*name || !value || !*value) {
    return;
  }
  int _ = 0;
  if (msg && *msg) {
    _ = write(1, msg, strlen(msg));
  }
  _ = write(1, name, strlen(name));
  _ = write(1, "=", 1);
  _ = write(1, value, strlen(value));
  _ = write(1, "\n", 1);
  (void)_;
}


int (*real_setenv)(const char *, const char *, int) = NULL;
char *(*real_getenv)(const char *) = NULL;
__attribute__((constructor)) void init() {
  real_setenv = dlsym(RTLD_NEXT, "setenv");
  real_getenv = dlsym(RTLD_NEXT, "getenv");
}

int setenv(const char *name, const char *value, int overwrite) {
    print(overwrite ? "[O]" : "[N]", name, value);
    return real_setenv(name, value, overwrite);
}

char *getenv(const char *name) {
    char *value = real_getenv(name);
    print("[G]", name, value);
    return value;
}
#else

// The linker defines these symbols to the start and end of the binary, as well
extern char _binary___libpeekenv_so_start[];
extern char _binary___libpeekenv_so_end[];

#include <stdio.h>
#include <sys/mman.h>
int main(int argc, char *argv[]) {

  // Need at least one arg
  if (argc < 2) {
    const char *msg = "Usage: peekenv <command> [args...]\n";
    write(2, msg, strlen(msg));
    return 1;
  }

  // Setup the library
  size_t sz = _binary___libpeekenv_so_end - _binary___libpeekenv_so_start;
  int fd = memfd_create("peekenv", 0);
  if (-1 == fd) {
    const char *msg = "Cannot create memfd\n";
    write(2, msg, strlen(msg));
    return 1;
  }
  if (-1 == ftruncate(fd, sz)) {
    const char *msg = "Cannot truncate memfd\n";
    write(2, msg, strlen(msg));
    return 1;
  }
  unsigned char *addr = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (MAP_FAILED == addr) {
    const char *msg = "Cannot mmap memfd\n";
    write(2, msg, strlen(msg));
    return 1;
  }
  memcpy(addr, _binary___libpeekenv_so_start, sz);

  // Finally, format the path to the fd for use in LD_PRELOAD
  char fd_path[4096];
  snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", fd);

  // Setup the environment.  We have to add to LD_PRELOAD
  // 1. if it's set, the append
  // 2. if it's not set, just set it
  char *ld_preload = getenv("LD_PRELOAD");
  if (ld_preload) {
    char new_ld_preload[4096];
    size_t n = snprintf(new_ld_preload, sizeof(new_ld_preload), "%s:%s", ld_preload, fd_path);
    if (n >= sizeof(new_ld_preload)) {
      const char *msg = "LD_PRELOAD too long\n";
      write(2, msg, strlen(msg));
      return 1;
    }
    setenv("LD_PRELOAD", new_ld_preload, 1);
  } else {
    setenv("LD_PRELOAD", fd_path, 0);
  }

  // Finally, execve the command and shift the args
  int ret = execvp(argv[1], argv + 1);
  {
    const char *msg = "Cannot execvp\n";
    write(2, msg, strlen(msg));
  }
  return ret;
}
#endif
