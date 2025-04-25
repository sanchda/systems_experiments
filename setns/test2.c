#define _GNU_SOURCE
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main() {
  char pid_str[sizeof("42949672960")] = {};
  if (-1 != readlink("/proc/self", pid_str, sizeof(pid_str)) &&
      getpid() == strtol(pid_str, NULL, 10)) {
    printf("Did it!\n");
  }
  return 0;
}
