// Does this CPU disappear? No sockets involved.
//   spin:  read CLOCK_MONOTONIC in a tight loop; any gap is time this thread didn't run.
//          If the guest's own accounting says we were on-CPU the whole time (cpu/wall ~ 100%,
//          no runqueue wait, no involuntary switches), the missing time was taken by the host.
//   sleep: clock_nanosleep 100us repeatedly; overshoot is wakeup latency of an idle vCPU.
//   -C cpu  -s secs  -m spin|sleep  -L n background spinner threads on cpus 4.. (simulates the pacer)
#include "common.h"
#include <sys/resource.h>

static atomic_int stop;
static void *burn(void *a) { pin((int)(intptr_t)a); while (!atomic_load_explicit(&stop, memory_order_relaxed)) ; return NULL; }

int main(int argc, char **argv) {
  int cpu = 2, secs = 5, sleepm = 0, load = 0;
  for (int o; (o = getopt(argc, argv, "C:s:m:L:")) != -1;) switch (o) {
    case 'C': cpu = atoi(optarg); break; case 's': secs = atoi(optarg); break;
    case 'm': sleepm = !strcmp(optarg, "sleep"); break; case 'L': load = atoi(optarg); break;
    default: return 2;
  }
  pthread_t th[8];
  for (int i = 0; i < load; i++) pthread_create(&th[i], NULL, burn, (void *)(intptr_t)(4 + i));
  pin(cpu);
  static hist_t h; uint64_t over1ms = 0, sum1ms = 0;
  uint64_t w0 = now(CLOCK_MONOTONIC), c0 = now(CLOCK_THREAD_CPUTIME_ID), end = w0 + secs * 1000000000ull, prev = w0;
  while (prev < end) {
    if (sleepm) {
      struct timespec ts = {0, 100000}; clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
      uint64_t t = now(CLOCK_MONOTONIC), late = t - prev > 100000 ? t - prev - 100000 : 0;
      hadd(&h, late); if (late > 1000000) { over1ms++; sum1ms += late; }
      prev = now(CLOCK_MONOTONIC);
    } else {
      uint64_t t = now(CLOCK_MONOTONIC), g = t - prev;
      if (g > 20000) { hadd(&h, g); if (g > 1000000) { over1ms++; sum1ms += g; } }
      prev = t;
    }
  }
  double frac = (double)(now(CLOCK_THREAD_CPUTIME_ID) - c0) / (now(CLOCK_MONOTONIC) - w0);
  struct rusage ru; getrusage(RUSAGE_THREAD, &ru);
  unsigned long long ss[3] = {0}; FILE *f = fopen("/proc/thread-self/schedstat", "r");
  if (f) { if (fscanf(f, "%llu %llu %llu", &ss[0], &ss[1], &ss[2]) != 3) ss[1] = 0; fclose(f); }
  atomic_store(&stop, 1);
  for (int i = 0; i < load; i++) pthread_join(th[i], NULL);
  printf("%s cpu=%d load=%d secs=%d events(>20us gap or any wake)=%lu p99=%.0fus max=%.0fus  >1ms: %lu events, %.1f ms total",
         sleepm ? "sleep" : "spin ", cpu, load, secs, h.n, hpct(&h, .99) / 1e3, h.max / 1e3, over1ms, sum1ms / 1e6);
  if (!sleepm) printf(" | guest view: cpu/wall %.1f%%, runq wait %.2f ms, involuntary csw %ld", 100 * frac, ss[1] / 1e6, ru.ru_nivcsw);
  printf("\n");
  return 0;
}
