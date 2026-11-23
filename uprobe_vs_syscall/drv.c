// Driver for uprobe vs syscall-tracepoint hook overhead.
// Each thread: N x { work(K); t0; hook(seq, tid); t1 }.  One TSV line out.
#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

#define MAGIC 0x7eb9f00d // far outside any PR_* the kernel knows; returns EINVAL
#define HB (1 << 16)     // latency histogram, 1 bucket per TSC cycle
#define WB 4096          // 32KB scratch, ~L1d, so pollution shows up in work time

// Probe site.  Compiled with -fcf-protection=none so the first insn is a bare
// nop, which x86 uprobes emulate rather than single-step out of line.
__attribute__((noinline)) void hook(uint64_t seq, uint64_t tid) { asm volatile("nop" ::: "memory"); }

typedef struct {
  int id, cpu;
  uint64_t total, hooked, sink;
  uint32_t *h;
  uint64_t over, max;
} targ;

static char mode = 'n';
static int T = 1, hostile = 0;
static uint64_t N = 1000000, K = 50;
static pthread_barrier_t bar;
static volatile int stop;

static inline uint64_t t_beg(void) { _mm_lfence(); uint64_t t = __rdtsc(); _mm_lfence(); return t; }
static inline uint64_t t_end(void) { unsigned a; uint64_t t = __rdtscp(&a); _mm_lfence(); return t; }

static void pin(int cpu) {
  cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpu, &s);
  pthread_setaffinity_np(pthread_self(), sizeof(s), &s);
}

static uint64_t now_ns(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

static double tsc_hz(void) {
  uint64_t n0 = now_ns(), c0 = __rdtsc();
  while (now_ns() - n0 < 200000000ull);
  return (__rdtsc() - c0) * 1e9 / (now_ns() - n0);
}

// System-wide busy time (excludes idle, iowait, steal, guest)
static uint64_t busy_ns(void) {
  uint64_t u, n, s, i, w, irq, sirq;
  FILE *f = fopen("/proc/stat", "r");
  if (!f || fscanf(f, "cpu %lu %lu %lu %lu %lu %lu %lu", &u, &n, &s, &i, &w, &irq, &sirq) != 7) exit(1);
  fclose(f);
  return (u + n + s + irq + sirq) * (1000000000ull / sysconf(_SC_CLK_TCK));
}

static uint64_t self_ns(void) {
  struct rusage r; getrusage(RUSAGE_SELF, &r);
  return (r.ru_utime.tv_sec + r.ru_stime.tv_sec) * 1000000000ull + (r.ru_utime.tv_usec + r.ru_stime.tv_usec) * 1000ull;
}

static inline uint64_t work(uint64_t x, uint64_t *buf) {
  for (uint64_t k = 0; k < K; k++) {
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    buf[x & (WB - 1)] += x;
  }
  return x;
}

static void *run(void *p) {
  targ *a = p;
  uint64_t *buf = calloc(WB, sizeof(*buf)), x = 0x9e3779b97f4a7c15ull * (a->id + 1), hooked = 0;
  pin(a->cpu);
  pthread_barrier_wait(&bar);
  uint64_t start = __rdtsc();
  for (uint64_t i = 0; i < N; i++) {
    x = work(x, buf);
    uint64_t t0 = t_beg();
    switch (mode) {
    case 'u': hook(i, a->id); break;
    case 's': prctl(MAGIC, i, a->id, 0, 0); break;
    case 'b': syscall(SYS_getppid); break;
    }
    uint64_t d = t_end() - t0;
    hooked += d;
    if (d < HB) a->h[d]++; else a->over++;
    if (d > a->max) a->max = d;
  }
  a->total = __rdtsc() - start;
  a->hooked = hooked;
  a->sink = x + buf[x & (WB - 1)];
  return 0;
}

static void *mmapper(void *p) {
  pin(*(int *)p);
  while (!stop) {
    char *m = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    *(volatile char *)m = 1;
    munmap(m, 4096);
  }
  return 0;
}

static double pct(uint64_t *h, uint64_t tot, double q) {
  uint64_t want = q * tot, c = 0;
  for (int i = 0; i < HB; i++) if ((c += h[i]) > want) return i;
  return INFINITY; // past histogram range; see `over`
}

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "header")) {
    printf("mode\tT\tN\tK\thostile\thooks\twall_ns\tapp_cpu_ns\tsys_busy_ns\t"
           "hook_mean_ns\thook_p50_ns\thook_p90_ns\thook_p99_ns\thook_p999_ns\thook_max_ns\t"
           "work_ns\thooks_per_s_per_thr\tover\n");
    return 0;
  }
  for (int c; (c = getopt(argc, argv, "m:t:n:k:h")) != -1;) switch (c) {
    case 'm': mode = *optarg; break;
    case 't': T = atoi(optarg); break;
    case 'n': N = strtoull(optarg, 0, 0); break;
    case 'k': K = strtoull(optarg, 0, 0); break;
    case 'h': hostile = 1; break;
    default: return 1;
    }
  if (mode == 's' && (prctl(MAGIC, 0, 0, 0, 0) != -1 || errno != EINVAL)) {
    fprintf(stderr, "MAGIC prctl did not EINVAL\n");
    return 1;
  }

  int ncpu = sysconf(_SC_NPROCESSORS_ONLN), hcpu = T < ncpu ? T : ncpu - 1;
  double hz = tsc_hz(), ns = 1e9 / hz;
  targ *a = calloc(T, sizeof(*a));
  pthread_t *th = calloc(T, sizeof(*th)), ht;
  pthread_barrier_init(&bar, 0, T + 1);
  for (int i = 0; i < T; i++) {
    a[i] = (targ){.id = i, .cpu = i % ncpu, .h = calloc(HB, sizeof(uint32_t))};
    pthread_create(&th[i], 0, run, &a[i]);
  }
  if (hostile) pthread_create(&ht, 0, mmapper, &hcpu);

  uint64_t b0 = busy_ns(), s0 = self_ns(), w0 = now_ns();
  pthread_barrier_wait(&bar);
  for (int i = 0; i < T; i++) pthread_join(th[i], 0);
  uint64_t w1 = now_ns(), s1 = self_ns(), b1 = busy_ns();
  stop = 1;
  if (hostile) pthread_join(ht, 0);

  uint64_t *h = calloc(HB, sizeof(*h)), tot = 0, hooked = 0, over = 0, max = 0, sink = 0;
  for (int i = 0; i < T; i++) {
    for (int j = 0; j < HB; j++) h[j] += a[i].h[j];
    tot += a[i].total; hooked += a[i].hooked; over += a[i].over; sink += a[i].sink;
    if (a[i].max > max) max = a[i].max;
  }
  uint64_t n = N * T;
  printf("%c\t%d\t%lu\t%lu\t%d\t%lu\t%lu\t%lu\t%lu\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.0f\t%lu\n",
         mode, T, N, K, hostile, mode == 'n' || mode == 'b' ? 0 : n, w1 - w0, s1 - s0, b1 - b0,
         hooked * ns / n, pct(h, n, .5) * ns, pct(h, n, .9) * ns, pct(h, n, .99) * ns, pct(h, n, .999) * ns,
         max * ns, (tot - hooked) * ns / n, N / (tot * ns / T / 1e9), over);
  return sink == 42; // keep work live
}
