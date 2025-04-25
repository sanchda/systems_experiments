#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

extern char** environ;
void scrub_args() {
  // The trick here is that we can get the user environment
  // through a global, and the args are just before it.  We
  // check procfs to determine the number of args, then
  // backtrack, replacing the elements
  char **args = environ;
  char buf[1024] = {0};
  int fd = open("/proc/self/cmdline", O_RDONLY);
  int n = 0;
  int argc = 0;
  while (0 < (n=read(fd, buf, sizeof(buf))))
    for (;n;n--)
      if (!buf[n]) argc++;
  for(;argc > 1;argc--)
    memset(args[-argc], '?', strlen(args[-argc]));
  close(fd);
}

void read_cmdline() {
  char buf[1024] = {0};
  int fd = open("/proc/self/cmdline", O_RDONLY);
  int n = 0;
  while (0 < (n=read(fd, buf, sizeof(buf)))) {
    // Replace '\0' with ' '
    for (int i = 0; i < n; i++)
      if (!buf[i])
        buf[i] = ' ';
    write(1, buf, n);
    memset(buf, 0, n);
  }
  write(1, "\n", 1);
  fflush(stdout);
  close(fd);
}




#include <cstdio>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

const char interp[] __attribute__((section(".interp"))) = "/lib64/ld-linux-x86-64.so.2";

void start() {
    puts("Starting");
    if (fork()==0) {
        execl("./libfoo.so", "libfoo.so", NULL);
    }
}

extern "C" {
void mymain() {
    puts("Main from lib");
    read_cmdline();
    puts("OK, done.\n");
    scrub_args();
    read_cmdline();

    exit(0);
}
}
