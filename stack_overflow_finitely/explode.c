#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>

// sigsegv signal handler
// interesting things might happen if ulimit -s is set to a small value
void sigsegv_handler(int signum) {
  static int count = 1;
  printf("Caught signal %d times\n", count++);
  fflush(stdout);

  // Allocate something big on the stack
  char buf[1000000];
  printf("Allocated 1MB on the stack\n");
  fflush(stdout);

  // If we're here, we're done
  exit(1);
}

int main(int argc, char *argv[]) {
  if (argc > 1) {
    int stack_size = atoi(argv[1]);
    if (stack_size < 0) {
      printf("Usage: %s [stack_size]\n", argv[0]);
      return 1;
    }
    printf("Setting stack size to %d\n", stack_size);
    struct rlimit rlim = {.rlim_cur = stack_size, .rlim_max = stack_size};
    setrlimit(RLIMIT_STACK, &rlim);
  }

  // Register the signal handler with SA_NODEFER
  struct sigaction sa = {.sa_handler = sigsegv_handler, .sa_flags = SA_NODEFER};
  sigaction(SIGSEGV, &sa, NULL);

  // Cause a segfault
  int *p = 0;
  *p = 0;

  return 0;
}
