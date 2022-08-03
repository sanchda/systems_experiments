#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MSIZE 4097

int main() {
  printf("cat /proc/%d/maps\n", getpid());
  int fd1 = (unlink("/tmp/foo"), open("/tmp/foo", O_RDWR | O_CREAT)); ftruncate(fd1, MSIZE);
  int fd2 = (unlink("/tmp/bar"), open("/tmp/bar", O_RDWR | O_CREAT)); ftruncate(fd2, MSIZE);
  void *region1 = mmap(NULL, MSIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd1, 0);
  void *region2 = mmap(NULL, MSIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd2, 0);

  printf("Region 1: %p\n", region1);
  printf("Region 2: %p\n", region2);
  usleep(1410065407);
  return 0;
}
