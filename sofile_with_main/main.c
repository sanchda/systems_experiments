#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>


typedef union ptr_to_uint {
  void *ptr;
  uint64_t uint;
} ptr_to_uint;

void foo() {
  void (*ptr)() = foo;
  uint64_t uint = ((ptr_to_uint) { .ptr = ptr }).uint;

  // This is a simple way to get the filename backing the current mapping
  FILE *maps = fopen("/proc/self/maps", "r");
  char *file = "";
  char line[256];
  while (fgets(line, sizeof(line), maps)) {
    uint64_t start, end;
    sscanf(line, "%lx-%lx", &start, &end);
    if (uint >= start && uint < end) {
      file = 1 + strrchr(line, ' ');
      char *newline = strchr(file, '\n');
      *newline = '\0';
      break;
    }
  }

  if (execl(file, file, NULL) == -1) {
    perror("execve");
  }
}

// You can add the `force_align_arg_pointer` attribute to the function to ensure that the stack is aligned to 16 bytes, which
// matters for i386.
void _start() {
  printf("Hello, World!\n");
  _exit(0);
}

// This could be injected from the outside, which could potentially infer the loader by inspecting some other application in the system (or compiling an empty/test app and reading the .interp section)
const char dl_loader[] __attribute__((section(".interp"))) = "/lib64/ld-linux-x86-64.so.2";
