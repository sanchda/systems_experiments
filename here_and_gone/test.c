#include <stdio.h>
#include <unistd.h>

int main() {
  char exe_path[1024];
  ssize_t len;

  if ((len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1)) == -1) {
    perror("readlink");
    return 1;
  }
  exe_path[len] = '\0';

  printf("Attempting to unlink: %s\n", exe_path);
  if (unlink(exe_path) == -1) {
    perror("unlink");
    return 1;
  }

  printf("Successfully unlinked: %s\n", exe_path);
  for (volatile size_t i = 0; i < 1e9; i++);
  return 0;
}
