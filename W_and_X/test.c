#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

typedef struct {
  uintptr_t start;
  uintptr_t end;
  off_t offset;
  char filename[4096];
} Mapping;

bool get_mapping(Mapping *mapping) {
  FILE *fp = fopen("/proc/self/maps", "r");
  if (!fp) {
    perror("Failed to open /proc/self/maps for some reason");
    return false;
  }

  char line[4096];
  while(fgets(line, sizeof(line), fp)) {
    char perms[5];
    int r = sscanf(line, "%lx-%lx %4s %lx %*x:%*x %*d %s",
                         &mapping->start,
                         &mapping->end,
                         perms,
                         &mapping->offset,
                         mapping->filename);
    if (perms[2] == 'x') {
      fclose(fp);
      return true;
    }
  }

  fclose(fp);
  perror("Failed to find executable mapping");
  return false;
}

int main() {
    Mapping mapping = {0};
    if (!get_mapping(&mapping))
      return -1;

    // Compute the number of pages
    size_t pages = (mapping.end - mapping.start + 4095) / 4096;
    size_t size = 4096*pages;

    // Create anonymous file using memfd_create
    int fd = syscall(SYS_memfd_create, "jitsegment", 0);
    ftruncate(fd, size);

    // Map the file with read-write permissions and copy the executable pages there
    void *rw = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (rw == MAP_FAILED) {
        perror("mmap rw");
        return 1;
    }
    memcpy(rw, (void *)mapping.start, size);

    // Map the file with read-execute permissions, but map OVER the executable
    void *rx = mmap((void *)mapping.start, size, PROT_READ | PROT_EXEC, MAP_SHARED | MAP_FIXED, fd, 0);
    if (rx == MAP_FAILED) {
        perror("mmap rx");
        return 1;
    }

    // Now rw can be modified to see changes in rx
    printf("I'm about to segfault\n"); fflush(stdout);
    memset(rw, 0, size);
    return 0;
}
