#define _GNU_SOURCE
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <poll.h>

int main() {
  int fd = memfd_create("Wow, this is stupid", MFD_CLOEXEC);
  ftruncate(fd, 4096);
  unsigned char* region = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, fd, 0);

  struct pollfd pfd = {.fd = fd, .events = POLLOUT | POLLNVAL | POLLHUP};

  // We can read over and over again, but ultimately need to clear the bit
  if (1 == poll(&pfd, 1, 0) && (pfd.revents & POLLOUT)) {
    printf("poll thinks I can read lol\n");
    pfd.revents = 0;
  } else {
    printf("Hey, this is a mistake!  Poll is supposed to have an opinion!\n");
    return -1;
  }

  if (1 == poll(&pfd, 1, 0) && (pfd.revents & POLLOUT)) {
    printf("I didn't do anything and poll _still_ thinks I can read!\n");
    pfd.revents = 0;
  } else {
    printf("Hey, this is a mistake!  Poll is supposed to have an opinion!\n");
    return -1;
  }

  // But what if we just slurped the data out of the buffer?
  char buf[4096];
  read(fd, &buf, 1);
  if (1 == poll(&pfd, 1, 0) && (pfd.revents & POLLOUT)) {
    printf("poll shouldn't think this, oops\n");
    pfd.revents = 0;
  } else {
    printf("Hey, this is a mistake!  Poll is supposed to have an opinion!\n");
    return -1;
  }


//  region[0] = 11;
  if (1 == poll(&pfd, 1, 0))
    printf("Correctly saw that one file descriptor was ready\n");
  pfd.revents = 0;
  munmap(region, 4096);
  close(fd);
  if (1 == poll(&pfd, 1, 0))
    printf("Correctly saw that the file descriptor closed\n");
  return 0;
}
