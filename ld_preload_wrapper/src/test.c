#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main() {
  setenv("HELLO", "WORLD", 1);
  if (fork()) {
    setenv("TYPE", "PARENT", 1);
  } else {
    setenv("TYPE", "CHILD", 1);
    usleep(1000);

    // If the environment variable is set, then dump out
    char *no_repeat = getenv("NOREPEAT");
    if (no_repeat) {
      return 0;
    }

    // Otherwise, exec myself again
    setenv("NOREPEAT", "1", 1);
    execl("/proc/self/exe", "/proc/self/exe", NULL);
  }
  return 1;
}
