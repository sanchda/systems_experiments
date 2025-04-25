#include <unistd.h>
#include <sys/mman.h>
#include <stdio.h>

int main() {
  unsigned char *region;
  for (int i = 0; i < 1024; i++) {
    region = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  }
  printf("Done allocating on process %d\n", getpid());
  char buf[] = {0};
  read(0, buf, 1);
}
