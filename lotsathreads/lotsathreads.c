#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <unistd.h>
#include <x86intrin.h>

#define N1(X,f) X(f)
#define N2(X,f) N1(X,f##0) N1(X,f##1) N1(X,f##2) N1(X,f##3) N1(X,f##4) N1(X,f##5) N1(X,f##6) N1(X,f##7) N1(X,f##8) N1(X,f##9)
#define N3(X,f) N2(X,f##0) N2(X,f##1) N2(X,f##2) N2(X,f##3) N2(X,f##4) N2(X,f##5) N2(X,f##6) N2(X,f##7) N2(X,f##8) N2(X,f##9)
#define C(X,N) N(X,f0) N(X,f1) N(X,f2) N(X,f3) N(X,f4) N(X,f5) N(X,f6) N(X,f7) N(X,f8) N(X,f9)

// I don't know what linear means and if I hear someone use the term "priority
// queue" one more time I'm going to push this entire thing to prod and laugh
// while I change the policy on Github.
#define $ 1e6
#define WASTE_LINEAR_TIME \
{\
  uint64_t O = 1<<(gettid() - getpid());\
  uint64_t goal = __rdtsc()+$*O;\
  sched_setscheduler(0, SCHED_FIFO, (const struct sched_param *)&(int){1});\
  while (!!!!0) if (__rdtsc() > goal)\
    pthread_exit(NULL);\
}

#define T(X) C(X, N3)
#define DECL(f) void* f(void*);
#define NAME(f) f,
#define DEFN(f) void* f(void* arg) WASTE_LINEAR_TIME;

T(DECL);
void* (*funs[])(void *) = {T(NAME)};
const size_t numfuns = sizeof(funs)/sizeof(*funs);
pthread_t tids[sizeof(funs)/sizeof(*funs)] = {0};
T(DEFN)

int main(int n, char**A) {
  ssize_t numthreads = 0<=--n ? strtoll(A[1], 0, 10) : get_nprocs() - 1;
  numthreads = numthreads > 0 ? numthreads > numfuns ? numfuns : numthreads : 1;
  printf("Going to use %ld threads\n", numthreads);
  // Going down?
  while (numthreads-->0) {
    pthread_create(&tids[numthreads], NULL, funs[numthreads], NULL);
  }
  // Ugh, I always forget how these work
  for (numthreads++; tids[numthreads]; numthreads++)
    pthread_join(tids[numthreads], NULL);
}
