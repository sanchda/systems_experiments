#include <sys/mman.h>
#include <stdio.h>
#include <unistd.h>

int main() {
  void *buf = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB, -1, 0);
  if (buf == MAP_FAILED) {
    printf("lolfail\n");
    return -1;
  }
  char io_buf[100] = {0};
  printf("PID: %d\n", getpid());
  read(0, io_buf, 1);
  return 0;
}
