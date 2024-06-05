#define _GNU_SOURCE
#include <dlfcn.h>
#include <unistd.h>
#include <sys/syscall.h>

// It's not a problem in this particular application, but as someone who writes these wrappers a lot,
// it can be unexpected to discover how often libc calls into itself.  Recursion can be a problem.
// So, let's implement as much as possible without libc!
static inline size_t strlen(const char *s) {
  size_t len = 0;
  while (*s++) {
    len++;
  }
  return len;
}

// Inline assembly function for a syscall with 3 arguments
// libc maintains that the first arg is the syscall number, which forces it to
// reshuffle the arguments around.  We just pass it last here.
inline static long syscall_3(long arg1, long arg2, long arg3, long number) {
    long result;
#if defined(__x86_64__)
    asm volatile (
        "syscall\n"            // Invoke syscall
        : "=a" (result)        // Output operand (result in rax)
        : "D" (arg1), "S" (arg2), "d" (arg3), "a" (number)  // Input operands
        : "rcx", "r11", "memory"  // Clobbered registers
    );
#elif defined(__aarch64__)
    asm volatile (
        "svc 0\n"              // Invoke syscall
        : "=r" (result)        // Output operand (result in x0)
        : "r" (arg1), "r" (arg2), "r" (arg3), "r" (number)  // Input operands in x0, x1, x2, x8 respectively
        : "memory"  // Clobbered memory
    );
#else
#error "Unsupported architecture"
#endif
    return result;
}

// Now the wrapper for writev
struct iovec {
  void  *iov_base;
  size_t iov_len;
};

inline static ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
  return syscall_3(fd, (long)iov, iovcnt, SYS_writev);
}

// Helper function to print an environment key-value pair
static void print_env_kv(const char *msg, const char *name, const char *value) {
  if (!name || !*name || !value || !*value) {
    return;
  }

  if (!msg || !*msg) {
    msg = "[]";
  }

  struct iovec iov[] = {
    [0] = {.iov_base = (void *)msg,   .iov_len = strlen(msg)},
    [1] = {.iov_base = " ",           .iov_len = 1},
    [2] = {.iov_base = (void *)name,  .iov_len = strlen(name)},
    [3] = {.iov_base = "=",           .iov_len = 1},
    [4] = {.iov_base = (void *)value, .iov_len = strlen(value)},
    [5] = {.iov_base = "\n",          .iov_len = 1}
  };

  int _ = writev(1, iov, sizeof(iov) / sizeof(iov[0]));
  (void)_;
}

// Helper for more generic prints
static void print_val(const char *msg, const char *value) {
  if (!value || !*value) {
    return;
  }

  if (!msg || !*msg) {
    msg = "[]";
  }

  struct iovec iov[] = {
    [0] = {.iov_base = (void *)msg,   .iov_len = strlen(msg)},
    [1] = {.iov_base = " ",           .iov_len = 1},
    [2] = {.iov_base = (void *)value, .iov_len = strlen(value)},
    [3] = {.iov_base = "\n",          .iov_len = 1}
  };

  int _ = writev(1, iov, sizeof(iov) / sizeof(iov[0]));
  (void)_;
}

// Defaults
static int (*real_setenv)(const char *, const char *, int) = NULL;
static char *(*real_getenv)(const char *) = NULL;
static int (*real_unsetenv)(const char *) = NULL;
static int (*real_putenv)(char *) = NULL;
static void (*real_clearenv)(void) = NULL;

static __attribute__((constructor)) void init() {
  real_setenv = dlsym(RTLD_NEXT, "setenv");
  real_getenv = dlsym(RTLD_NEXT, "getenv");
  real_unsetenv = dlsym(RTLD_NEXT, "unsetenv");
  real_putenv = dlsym(RTLD_NEXT, "putenv");
  real_clearenv = dlsym(RTLD_NEXT, "clearenv");
}

// Overrides
int setenv(const char *name, const char *value, int overwrite) {
  print_env_kv(overwrite ? "[O]" : "[N]", name, value);
  return real_setenv(name, value, overwrite);
}

char *getenv(const char *name) {
  char *value = real_getenv(name);
  print_env_kv("[G]", name, value);
  return value;
}

int unsetenv(const char *name) {
  print_val("[U]", name);
  return real_unsetenv(name);
}

int putenv(char *string) {
  print_val("[P]", string);
  return real_putenv(string);
}

void clearenv(void) {
  print_val("[C]", "---");
  return real_clearenv();
}
