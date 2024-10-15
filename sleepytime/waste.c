#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>

int main() {
  for (int i = 0; i < 7; i++)
    if (!fork())
      break;
  printf("Process %d\n", getpid());
  while (true) {
    volatile size_t _ = 0;
    _ = _ + 1;
  }
  return 0;
}
