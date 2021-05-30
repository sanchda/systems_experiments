#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <x86intrin.h>

// Use like
// gcc -DUSE_REAL itimer_rate.c && ./a.out 1000

double count = 0.0;
void handler(int s) {
  (void)s;
  count += 1;
}

#define T __rdtsc
int main(int c, char** v) {
  suseconds_t usecs = 1000*100;
  if (c > 1)
    usecs = atoll(v[1]);
  signal(SIGPROF, handler);
  signal(SIGALRM, handler);
  signal(SIGVTALRM, handler);

  struct itimerval old;
#if defined(USE_REAL)
  setitimer(ITIMER_REAL, &(struct itimerval){.it_interval = {0, usecs}, .it_value = {0, usecs}}, &old);
#elif defined(USE_VIRT)
  setitimer(ITIMER_VIRTUAL, &(struct itimerval){.it_interval = {0, usecs}, .it_value = {0, usecs}}, &old);
#elif defined(USE_PROF)
  setitimer(ITIMER_PROF, &(struct itimerval){.it_interval = {0, usecs}, .it_value = {0, usecs}}, &old);
#else
  setitimer(ITIMER_PROF, &(struct itimerval){.it_interval = {0, usecs}, .it_value = {0, usecs}}, &old);
#endif
  u_int64_t t1 = T();
  u_int64_t t2 = T();
  u_int64_t cycles = 1e10;
  while (cycles > (t2 = T()) - t1)
    for(int i = 0; i < 1e3; i++);

  signal(SIGPROF, SIG_IGN);
  printf("Saw %d hits, or %f cycles/hit\n", (int)count, (t2-t1)/count);
}
