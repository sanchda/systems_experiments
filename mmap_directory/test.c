#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>


int main() {
  // Make the directory.  There's no problem if it can't be made because it exists
  mkdir("./foo", 0777);
  close(open("./foo/a", O_CREAT, 0777));
  close(open("./foo/b", O_CREAT, 0777));

  // Open the directory
  int fd = openat(AT_FDCWD, "foo/", O_RDONLY | O_CLOEXEC | O_DIRECTORY);
  unsigned char *buf = mmap(0, 4096, PROT_READ | PROT_EXEC, MAP_SHARED, fd, 0);

  // lolno
  if (buf == MAP_FAILED)
    printf("You obviously can't mmap() directory (%s)\n", strerror(errno))
}
