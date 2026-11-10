// Usage: cc -O2 -pthread -o who who.c && ./who 30
// Spinner on CPU 2 publishes a heartbeat; watcher on CPU 0 sees it go stale and lists
// tasks in state R whose last cpu is 2 (fields 3 and 39 of /proc/*/task/*/stat).
#define _GNU_SOURCE
#include <dirent.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
static _Atomic uint64_t hb; static atomic_int stop; static int spin_tid;
static uint64_t ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000000ull + t.tv_nsec; }
static void pin(int c) { cpu_set_t s; CPU_ZERO(&s); CPU_SET(c, &s); sched_setaffinity(0, sizeof s, &s); }
static void *spin(void *a) { pin(2); spin_tid = syscall(SYS_gettid); while (!stop) atomic_store_explicit(&hb, ns(), memory_order_relaxed); return a; }
static void scan(uint64_t stale) {
  DIR *p = opendir("/proc"); struct dirent *d;
  printf("stale %.1f ms, running on cpu2:", stale / 1e6);
  while ((d = readdir(p))) {
    if (d->d_name[0] < '1' || d->d_name[0] > '9') continue;
    char path[300]; snprintf(path, sizeof path, "/proc/%s/task", d->d_name);
    DIR *t = opendir(path); if (!t) continue; struct dirent *e;
    while ((e = readdir(t))) {
      if (e->d_name[0] == '.') continue;
      char sp[600], buf[1024]; snprintf(sp, sizeof sp, "%s/%s/stat", path, e->d_name);
      FILE *f = fopen(sp, "r"); if (!f) continue;
      size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[n] = 0;
      char *rp = strrchr(buf, ')'); if (!rp) continue;
      char st; int fld = 3, cpu = -1; char *q = rp + 2; st = *q;
      for (char *tok = strtok(q, " "); tok; tok = strtok(NULL, " "), fld++) if (fld == 39) { cpu = atoi(tok); break; }
      if (st == 'R' && cpu == 2 && atoi(e->d_name) != spin_tid) {
        char *lp = strchr(buf, '('); *rp = 0; printf("  [%s %s pid=%s]", e->d_name, lp + 1, d->d_name);
      }
    }
    closedir(t);
  }
  closedir(p); printf("\n"); fflush(stdout);
}
int main(int argc, char **argv) {
  int secs = argc > 1 ? atoi(argv[1]) : 20;
  pthread_t t; pthread_create(&t, NULL, spin, NULL); pin(0); usleep(10000);
  uint64_t end = ns() + secs * 1000000000ull, last_report = 0;
  while (ns() < end) {
    uint64_t h = atomic_load(&hb), n = ns(); int64_t s = (int64_t)(n - h);
    if (s > 2000000 && n - last_report > 5000000) { scan(s); last_report = ns(); }
  }
  stop = 1; pthread_join(t, NULL); return 0;
}
