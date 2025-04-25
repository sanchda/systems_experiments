#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main() {
  pid_t pid = fork();
  char buf[1024];
  int n;
  while (1) {
    n = read(STDIN_FILENO, buf, sizeof(buf));
    if (pid) {
      write(STDOUT_FILENO, "parent: ", 8);
    } else {
      write(STDOUT_FILENO, "child: ", 7);
    }
    write(STDOUT_FILENO, buf, n);
  }

  return 0;
}
