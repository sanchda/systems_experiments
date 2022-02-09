#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

extern char** environ;
void scrub_args() {
  // The trick here is that we can get the user environment
  // through a global, and the args are just before it.
  char **args = environ;
  int argc = 0;

  // Find argc.  It's only sort of argc, since we're off by one, but whatever.
  // This limit is actually higher
  for (int i = 2; i < 25; i++) {
    if (i - 2 == (unsigned int)args[-i]) {
      argc = i - 1;
      break;
    }
  }
  for(;argc > 1;argc--)
    memset(args[-argc], '?', strlen(args[-argc]));
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
