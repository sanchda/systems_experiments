#define _GNU_SOURCE
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

int main() {
  int fd = open("/host/proc/1/ns/pid", O_RDONLY);
  if (-1 == fd) {
    printf("Failed to open PID ns\n");
    return -1;
  }
  if (setns(fd, CLONE_NEWPID)) {
    printf("Failed to acquire PID ns\n");
    return -1;
  }
  return 0;
}
