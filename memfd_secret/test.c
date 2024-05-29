// Example application that uses a direct syscall to `memfd_secret()`

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/types.h>

// Page align
#define PAGE_SZ 4096 // Not PAGE_SIZE because we don't want to conflict
#define ALIGN(x, a) (typeof(x)) (((uintptr_t)(x) + ((uintptr_t)(a) - 1)) & ~((uintptr_t)(a) - 1))
#define PAGE_ALIGN(x) ALIGN(x, PAGE_SZ)
#define PAGE_TRUNC(x) (PAGE_ALIGN(x) - PAGE_SZ)

int main(int argc, char **argv) {
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

  // Temporary copy of the memory
  unsigned char *copy = malloc(num_pages * PAGE_SZ);
  if (!copy) perror("malloc"), exit(1);

  // Copy the memory
  printf("Copying %zu pages from %p to %p\n", num_pages, first_page, copy);
  memcpy(copy, first_page, num_pages * PAGE_SZ);

  // Now, MAP_FIXED to over-write the range with a mapping from the secret memory
  unsigned char *secret = mmap(first_page, num_pages * PAGE_SZ, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
  close(fd); // not needed
  memcpy(secret, copy, num_pages * PAGE_SZ);

  // Validation stuff
  // Print out the args
  for (int i = 0; i < argc; i++) {
    printf("argv[%d] = %s\n", i, argv[i]);
  }

  // Print out /proc/self/cmdline one page at a time (it may be longer)
  FILE *cmdline = fopen("/proc/self/cmdline", "r");
  if (!cmdline) {
    perror("fopen");
    return 1;
  }
  char buf[PAGE_SZ];
  while (fread(buf, 1, PAGE_SZ, cmdline) > 0) {
    printf("%s", buf);
  }
  printf("\n");
  fclose(cmdline);

  // Pause (scanf) to allow for inspection.  Also print PID
  printf("PID: %d\n", getpid());
  printf("Press enter to exit\n");
  scanf("%*c");


  return 0;
}
