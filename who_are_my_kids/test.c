#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


void list_kids()
{
  char path[256];
  snprintf(path, sizeof(path), "/proc/self/task/%d/children", getpid());
  FILE *f = fopen(path, "r");
  if (!f) {
    perror("fopen");
    return;
  }

  char buf[256];
  while (fgets(buf, sizeof(buf), f)) {
    int pid = atoi(buf);
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/exe", pid);
    char exe[256];
    ssize_t len = readlink(path, exe, sizeof(exe));
    if (len < 0) {
      perror("readlink");
      continue;
    }
    exe[len] = '\0';
    printf("%s\n", exe);
  }
}

int main()
{
  if (fork())
    list_kids();
  else
    sleep(1);
  return 0;
}
