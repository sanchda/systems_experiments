// What does the kernel say about SO_REUSEPORT and shared binds, per family?
#include "common.h"
#include <sys/utsname.h>

static int sock(int uds, int reuse) {
  int fd = socket(uds ? AF_UNIX : AF_INET, SOCK_DGRAM, 0), one = 1;
  if (reuse) {
    int r = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
    printf("  setsockopt(SO_REUSEPORT) on %s: %s\n", uds ? "AF_UNIX" : "AF_INET", r ? strerror(errno) : "ok");
  }
  return fd;
}

static void two_binds(int uds, int reuse) {
  printf("%s, reuseport=%d:\n", uds ? "AF_UNIX/SOCK_DGRAM" : "AF_INET/SOCK_DGRAM", reuse);
  addr_t a; socklen_t al = mkaddr(&a, uds, 99);
  if (uds) unlink(a.un.sun_path); else a.in.sin_port = htons(PORT + 74); // stay off the benchmark port
  int f1 = sock(uds, reuse), f2 = sock(uds, reuse);
  int r1 = bind(f1, &a.sa, al); int e1 = errno;
  int r2 = bind(f2, &a.sa, al); int e2 = errno;
  printf("  bind #1: %s\n  bind #2: %s\n", r1 ? strerror(e1) : "ok", r2 ? strerror(e2) : "ok");
  close(f1); close(f2);
  if (uds) unlink(a.un.sun_path);
}

int main(void) {
  struct utsname u; uname(&u);
  printf("kernel %s\n", u.release);
  two_binds(0, 0); two_binds(0, 1); two_binds(1, 0); two_binds(1, 1);
  FILE *f = fopen("/proc/sys/net/unix/max_dgram_qlen", "r"); int q = -1;
  if (f) { if (fscanf(f, "%d", &q) != 1) q = -1; fclose(f); }
  printf("net.unix.max_dgram_qlen = %d\n", q);
  return 0;
}
