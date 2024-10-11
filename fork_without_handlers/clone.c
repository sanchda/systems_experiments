#include <stdio.h>
#include <sys/syscall.h>
#include <linux/sched.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <spawn.h>


// clone syscall
int clone(int flags, void *stack, int *parent_tid, int *child_tid, unsigned long tls) {
  return syscall(
      SYS_clone,
      (long)flags,
      (long)stack,
      (long)parent_tid,
#if defined(__x86_64__)
      (long)child_tid,
      (long)tls
#else
      (long)tls,
      (long)child_tid
#endif
  );
}

// implement fork without atexit handlers
pid_t clone_like_fork() {
  // This will call atexit handlers
  pid_t ptid = 0; // whatever
  pid_t ctid = 0;
  clone(
      CLONE_CHILD_SETTID|SIGCHLD,
      NULL,
      &ptid,
      &ctid,
      0
  );
  return ctid;
}

pid_t clone_like_vfork() {
  pid_t ptid = 0; // whatever
  pid_t ctid = 0;
  clone(
      CLONE_VM|CLONE_VFORK|SIGCHLD,
      NULL,
      &ptid,
      &ctid,
      0
  );
  return ctid;
}

void cause_a_horrible_segmentation_fault_to_occur() {
  printf("Process %d has hit an atfork handler\n", getpid());
  int *p = 0;
  *p = 0;
}

char *argv0;
pid_t try_spawn() {
  // Re-execute myself with no args
  pid_t pid;
  char *argv[] = { argv0, NULL };
  char *envp[] = { NULL };

  // Get the current /proc/self/exe
  char path[4096];
  char link[4096];
  snprintf(link, sizeof(link), "/proc/%d/exe", getpid());
  ssize_t len = readlink(link, path, sizeof(path));
  if (len < 0) {
    perror("readlink");
    return -1;
  }
  posix_spawn(&pid, path, NULL, NULL, argv, envp);
  return pid;
}

void print_help() {
  printf("Usage: %s [fork|vfork|clone_fork|clone_vfork|spawn]\n", argv0);
}

int main(int argc, char **argv) {
  // Process the command line arguments
  argv0 = argv[0];
  // Set the atfork handlers to cause a segfault
  pthread_atfork(
    cause_a_horrible_segmentation_fault_to_occur,
    cause_a_horrible_segmentation_fault_to_occur,
    cause_a_horrible_segmentation_fault_to_occur
  );

  // Use the appropriate spawn method
  pid_t pid;
  if (argc > 1) {
    if (strcasecmp(argv[1], "fork") == 0) {
      pid = fork();
    } else if (strcasecmp(argv[1], "vfork") == 0) {
      pid = vfork();
    } else if (strcasecmp(argv[1], "clone_fork") == 0) {
      pid = clone_like_fork();
    } else if (strcasecmp(argv[1], "clone_vfork") == 0) {
      pid = clone_like_vfork();
    } else if (strcasecmp(argv[1], "spawn") == 0) {
      pid = try_spawn();
    } else if (strcasecmp(argv[1], "help") == 0) {
      print_help();
      return 1;
    }
  }

  printf("Clean exit for %d\n", getpid());
  return 0;
}
