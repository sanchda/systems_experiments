// Micro-costs that explain the statsd numbers on this box.
//   syscall   getppid() round trip (kernel entry/exit incl. PTI/IBRS mitigations)
//   clock     clock_gettime(CLOCK_MONOTONIC) (vDSO)
//   udp_send  send() of 60 B on a connected loopback UDP socket, receiver never reads (drops after rcvbuf fills):
//             isolates the sender-side cost of the whole IP/UDP path
//   udp_drain recvmsg(MSG_DONTWAIT) of datagrams already queued (receiver-side cost per datagram)
//   pingpong  two threads, pipes, same CPU vs different CPUs: wakeup round-trip
#include "common.h"
#include <sys/syscall.h>

static double ns_per(uint64_t t0, uint64_t t1, long n) { return (double)(t1 - t0) / n; }

static int pfd[2][2];
static void *pong(void *a) {
  pin((int)(intptr_t)a); char c;
  for (;;) { if (read(pfd[0][0], &c, 1) != 1) return NULL; if (write(pfd[1][1], &c, 1) != 1) return NULL; }
}

static void pingpong(int c0, int c1) {
  if (pipe(pfd[0]) || pipe(pfd[1])) DIE("pipe");
  pthread_t t; pthread_create(&t, NULL, pong, (void *)(intptr_t)c1);
  pin(c0); char c = 'x'; long n = 50000;
  for (int i = 0; i < 2000; i++) { write(pfd[0][1], &c, 1); read(pfd[1][0], &c, 1); }
  uint64_t t0 = now(CLOCK_MONOTONIC);
  for (long i = 0; i < n; i++) { write(pfd[0][1], &c, 1); read(pfd[1][0], &c, 1); }
  uint64_t t1 = now(CLOCK_MONOTONIC);
  printf("pingpong cpu%d<->cpu%d  %.0f ns round trip\n", c0, c1, ns_per(t0, t1, n));
  close(pfd[0][1]); pthread_join(t, NULL); close(pfd[0][0]); close(pfd[1][0]); close(pfd[1][1]);
}

int main(void) {
  pin(1);
  long n = 2000000; uint64_t t0, t1;
  t0 = now(CLOCK_MONOTONIC); for (long i = 0; i < n; i++) syscall(SYS_getppid); t1 = now(CLOCK_MONOTONIC);
  printf("syscall   getppid       %.0f ns\n", ns_per(t0, t1, n));
  t0 = now(CLOCK_MONOTONIC); for (long i = 0; i < n; i++) now(CLOCK_MONOTONIC); t1 = now(CLOCK_MONOTONIC);
  printf("clock     clock_gettime %.0f ns\n", ns_per(t0, t1, n));

  addr_t a; socklen_t al = mkaddr(&a, 0, 0);
  int rx = socket(AF_INET, SOCK_DGRAM, 0), tx = socket(AF_INET, SOCK_DGRAM, 0);
  if (bind(rx, &a.sa, al)) DIE("bind");
  if (connect(tx, &a.sa, al)) DIE("connect");
  char buf[64]; memset(buf, 'a', sizeof buf);
  // rcvbuf holds ~221 datagrams; after that the kernel drops them in the rx path (still does most of the work)
  t0 = now(CLOCK_MONOTONIC); long ok = 0;
  for (long i = 0; i < 500000; i++) if (send(tx, buf, 60, MSG_DONTWAIT) == 60) ok++;
  t1 = now(CLOCK_MONOTONIC);
  printf("udp_send  60B (rx full/dropping)  %.0f ns per send\n", ns_per(t0, t1, 500000));
  // drain what was queued
  long got = 0; t0 = now(CLOCK_MONOTONIC);
  while (recv(rx, buf, sizeof buf, MSG_DONTWAIT) > 0) got++;
  t1 = now(CLOCK_MONOTONIC);
  printf("udp_drain recv %ld queued dgrams  %.0f ns per recv\n", got, ns_per(t0, t1, got ? got : 1));
  // send+recv alternating: no drops, no wakeups (nobody sleeping)
  t0 = now(CLOCK_MONOTONIC);
  for (long i = 0; i < 500000; i++) { send(tx, buf, 60, 0); recv(rx, buf, sizeof buf, 0); }
  t1 = now(CLOCK_MONOTONIC);
  printf("udp_loop  send+recv same thread   %.0f ns per pair\n", ns_per(t0, t1, 500000));

  pingpong(0, 0 + 0 == 0 ? 0 : 0); // same cpu (both pinned to cpu 0)
  pingpong(0, 1);
  return 0;
}
