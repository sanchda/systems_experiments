#include <sys/mman.h>
#include <sys/prctl.h>
#include <stdio.h>
#include <errno.h>

int main() {
  const size_t sz = 4096;
  void *p = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED) {
    perror("mmap");
    return 1;
  }
  prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, p, sz, "test");
  munmap(p, sz);
  return 0;
}
