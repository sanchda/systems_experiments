#include <x86intrin.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define T(x) t0=__rdtsc(); clock_gettime(x, &ts); printf("%s: %lld\n", #x, __rdtsc() - t0)

int main() {
  struct timespec ts = {0};
  uint64_t t0 = 0;

//  T(CLOCK_REALTIME);
//  T(CLOCK_REALTIME_COARSE);
//  T(CLOCK_MONOTONIC);
//  T(CLOCK_MONOTONIC_COARSE);
//  T(CLOCK_MONOTONIC_RAW);
//  T(CLOCK_BOOTTIME);
  T(CLOCK_PROCESS_CPUTIME_ID);
  T(CLOCK_THREAD_CPUTIME_ID);

}
