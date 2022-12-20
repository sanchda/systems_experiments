#include <sched.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define SZ (1024*4096) // 4 megs
#define I_SZ (SZ - sizeof(size_t))
#define ITER (1e2)

// Silly time
#if defined(__x86_64__)
#include <x86intrin.h>
#define tick() __rdtsc()
#elif defined(__aarch64__)
inline uint64_t tick_impl() {
  uint64_t x;
  asm volatile("mrs %0, cntvct_el0" : "=r"(x));
  return x;
}
#define tick() tick_impl()
#else
#error Architecture not supported
#endif

void print(const char* msg1, const char* msg2, double num) {
  if (!msg1 || !msg2 || !*msg1 || !*msg2)
    return;
  printf("%s%s%*.3f\n",msg1, msg2, 11, num);
}

double r_baseline[3] = {0};
double w_baseline[3] = {0};
typedef enum TYPES{COW, PRIVATE}TYPES;

void do_work(const char* msg, unsigned char *buf, int type) {
  // Do linear read
  long long unsigned start_time = tick();
  volatile size_t *accum = (size_t*)buf; // use same storage for accumulation
  buf += sizeof(size_t);
  for (size_t i = 0; i < I_SZ; i++) {
    buf[i] = (unsigned char)i;
  }
  for (int i = 0; i < ITER; i++) {
    for (size_t j = 0; j < I_SZ; j++) {
      *accum += buf[j];
    }
  }
  uint64_t ticks = tick() - start_time;
  if (!r_baseline[type])
    r_baseline[type] = ticks;
  print(msg, "read,  ", ticks / r_baseline[type]);

  // Do linear writes
  start_time = tick();
  for (int i = 0; i < ITER; i++) {
    for (size_t j = 0; j < I_SZ; j++) {
      buf[j] = *accum;
    }
  }
  ticks = tick() - start_time;
  if (!w_baseline[type])
    w_baseline[type] = ticks;
  print(msg, "write, ",ticks / w_baseline[type]);
}

int main() {
  unsigned char *cow_region = mmap(0, SZ, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0); // malloc, explicitly
  unsigned char *private_region = mmap(0, SZ, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

  // Setup a shared barrier to coordinate processes for later tests
  // Setup simple coordination using GCC intrinsics
  unsigned long *sem = mmap(0, sizeof(*sem), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
  *sem = 0;

  // Do work in the parent process
  do_work("parent, cow,     ", cow_region, COW);
  do_work("parent, private, ", private_region, PRIVATE);

  // Do work in child process
  if (fork()) {
    wait(NULL);
  } else {
    do_work("child,  cow,     ", cow_region, COW);
    return 0;
  }
  if (fork()) {
    wait(NULL);
  } else {
    do_work("child,  private, ", private_region, PRIVATE);
    return 0;
  }

  // Do work in both (with a new child to reset the CoW counters etc)
  // This is a little tricky.  I'm using `sem` as a poor-man's shared-memory semaphore in a way that will almost
  // certainly cause consistency issues if dropped into arbitrary code.  Be careful out there!
  if (fork()) {
    __sync_add_and_fetch(sem, 1);
    while (2 != *sem)
      sched_yield();
    do_work("Psync,  cow,     ", cow_region, COW);

    __sync_add_and_fetch(sem, 1);
    while (4 != *sem)
      sched_yield();
    do_work("Psync,  private, ", private_region, PRIVATE);
    wait(NULL);
  } else {
    __sync_add_and_fetch(sem, 1);
    while (2 != *sem)
      sched_yield();
    do_work("Csync,  cow,     ", cow_region, COW);
    __sync_add_and_fetch(sem, 1);
    while (4 != *sem)
      sched_yield();
    do_work("Csync,  private, ", private_region, PRIVATE);
    return 0;
  }
  fflush(stdout);
}
