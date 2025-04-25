#define _GNU_SOURCE

#include <stdio.h>
#include <sys/auxv.h>
#include <unistd.h>
#include <sys/mman.h>

int main() {
  unsigned long vdso_map = getauxval(AT_SYSINFO_EHDR);
  int fd = memfd_create("vdso", MFD_CLOEXEC);
  char cmd[1024] = {0};

  int n = 0;
  while (-1 < write(fd, (unsigned char *)vdso_map, 4096))
    vdso_map += 4096*++n;

  // Since we control this application, we know 3 is the number of the memfd
  snprintf(cmd, 1024, "readelf -h /proc/%d/fd/%d", getpid(), fd);
  FILE *res = popen(cmd, "re");
  char line[128] = {0};
  while (fgets(line, 128, res) != NULL)
    printf("%s", line);
  fclose(res);

  // Let's get the symbols too!
  snprintf(cmd, 1024, "readelf -sW /proc/%d/fd/%d", getpid(), fd);
  res = popen(cmd, "re");
  while (fgets(line, 128, res) != NULL)
    printf("%s", line);
  fclose(res);
}
