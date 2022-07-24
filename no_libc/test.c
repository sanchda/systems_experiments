#include <asm/unistd.h>
#include <stdio.h>

int main() {
  printf("%d\n", __NR_execve);
  return 0;
}
