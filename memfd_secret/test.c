// Example application that uses a direct syscall to `memfd_secret()`
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

// Page align
#define PAGE_SZ 4096 // Not PAGE_SIZE because we don't want to conflict
#define ALIGN(x, a) (typeof(x)) (((uintptr_t)(x) + ((uintptr_t)(a) - 1)) & ~((uintptr_t)(a) - 1))
#define PAGE_ALIGN(x) ALIGN(x, PAGE_SZ) // Round "up" (down?) to the nearest page
#define PAGE_TRUNC(x) (PAGE_ALIGN(x) - PAGE_SZ) // Gets the top of the containing page

void protect_argv(int argc, char **argv) {
  int fd = syscall(SYS_memfd_secret, 0);
  if (fd < 0) perror("memfd secret"), exit(1);

  // Figure out how many pages are spanned by argv members.  Strongly assume that this is a contiguous
  // range, which is usually a reasonable assumption.
  unsigned char *first_page = PAGE_TRUNC(argv[0]);
  unsigned char *last_page = PAGE_TRUNC(argv[argc - 1] + strlen(argv[argc - 1]));
  size_t num_pages = (last_page - first_page) / PAGE_SZ;
  num_pages++;

  // Ftruncate, etc
  if (ftruncate(fd, num_pages * PAGE_SZ) < 0) perror("ftruncate"), exit(1);
  unsigned char *copy = mmap(NULL, num_pages * PAGE_SZ, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

  // Copy the memory
  memcpy(copy, first_page, num_pages * PAGE_SZ);

  // Now, MAP_FIXED to over-write the range with a mapping from the secret memory
  unsigned char *secret = mmap(first_page, num_pages * PAGE_SZ, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
  close(fd); // not needed
  munmap(copy, num_pages * PAGE_SZ); // not needed
}

void print_validation_info(int argc, char **argv) {
  // Print out the args
  for (int i = 0; i < argc; i++) {
    printf("argv[%d] = %s\n", i, argv[i]);
  }

  // Print out /proc/self/cmdline one page at a time (it may be longer)
  printf("Proc cmdline:\n");
  unsigned char buffer[PAGE_SZ];
  ssize_t read_sz;
  int cfd = open("/proc/self/cmdline", O_RDONLY);

  while (1) {
    read_sz = read(cfd, buffer, PAGE_SZ);
    if (read_sz == -1 && errno == EINTR) continue;
    else if (read_sz <= 0) break;

    for (int i = 0; i < PAGE_SZ; ++i) {
      if (buffer[i] == 0) buffer[i] = ' ';
    }

    ssize_t write_sz = 0;
    while (write_sz < read_sz) {
      ssize_t written = write(1, buffer + write_sz, read_sz - write_sz);
      if (written == -1 && errno == EINTR) continue;
      else if (written <= 0) break;
      write_sz += written;
    }
  }
  printf("\n");
  close(cfd);

  // Pause (scanf) to allow for inspection.  Also print PID
  printf("PID: %d\n", getpid());
  printf("Press enter to exit\n");
  scanf("%*c");
}

int main(int argc, char **argv) {
  print_validation_info(argc, argv);
  protect_argv(argc, argv);
  printf("Protected argv\n");
  print_validation_info(argc, argv);
  return 0;
}
