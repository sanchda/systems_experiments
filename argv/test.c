#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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

int main(int n, char** v) {
  printf("Reading my commandline from /proc/self/cmdline.\n");
  read_cmdline();

  // Anonymize args, then print them.
  for (int i = 0; i < n; i++)
    for (int j = strlen(v[i]); j; j--)
      v[i][j-1] = '*';
  printf("I scrubbed my argv.  I'll read the sanitized output for you.\n");
  for (int i = 0; i < n; i++)
    printf("%s ", v[i]);
  printf("\n");

  // Print the commandline again to see if the kernel-detected strategy works
  printf("I'm going to read my commandline from procfs again.\n");
  read_cmdline();
  return 0;
}
