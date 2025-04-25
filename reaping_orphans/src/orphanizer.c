#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

void donothing_handler(int signum) {
  return;
}

pid_t make_zombie() {
  pid_t pid = fork();
  if (!pid) {
    exit(0);
  }
  return pid;
}

pid make_detached_orphan() {
  // In general, a user may have a custom SIGCHLD handler, and they may have some logic in that
  // handler which only counts the number of expired processes without checking that they are
  // "target" children.  I think the only way around this is to initialize outside of the multi-
  // threaded phase of an application.

  // First, store the current SIGCHLD handler and replace it with SIG_IGN
  struct sigaction old_action;
  struct sigaction new_action;
  sigemptyset(&new_action.sa_mask);
  new_action.sa_handler = SIG_IGN;
  if (sigaction(SIGCHLD, &new_action, &old_action) < 0) {
    return -1;
  }

  // Fork and exit the parent process

}

char get_status(pid_t pid) {
    // Return the status of the child process by reading /proc/pid/stat
    char path_buf[1024];
    sprintf(path_buf, "/proc/%d/stat", pid);
    FILE *fp = fopen(path_buf, "r");
    if (fp == NULL) {
        return 'E';
    }

    // This pattern will break for processes with spaces in the name, but we don't do that here
    char status;
    fscanf(fp, "%*d %*s %c", &status);
    fclose(fp);
    return status;
}


__attribute__((constructor))
void setup_orphanizer() {
  signal(SIGCHLD, donothing_handler);
}
