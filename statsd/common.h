// Shared bits for the statsd experiments. Terse on purpose.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define DIE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, ": %s\n", strerror(errno)); exit(1); } while (0)
#define PORT 8125
#define PATHFMT "/tmp/statsd.%d.sock"

static inline uint64_t now(clockid_t c) {
  struct timespec t; clock_gettime(c, &t);
  return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec;
}

static inline void pin(int cpu) {
  if (cpu < 0) return;
  cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpu, &s);
  if (sched_setaffinity(0, sizeof s, &s)) DIE("pin %d", cpu);
}

typedef union { struct sockaddr sa; struct sockaddr_in in; struct sockaddr_un un; } addr_t;

static inline socklen_t mkaddr(addr_t *a, int uds, int idx) {
  memset(a, 0, sizeof *a);
  if (uds) {
    a->un.sun_family = AF_UNIX;
    snprintf(a->un.sun_path, sizeof a->un.sun_path, PATHFMT, idx);
    return sizeof a->un;
  }
  a->in.sin_family = AF_INET;
  a->in.sin_port = htons(PORT);
  a->in.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  return sizeof a->in;
}

// ~60 byte metric lines, e.g. "web.request.count:1|c|#env:prod,host:web-07,route:/v1/users\n"
static const char *NAMES[] = {"web.request.count", "web.request.bytes", "db.query.latency", "cache.hit.ratio"};
static const char *TYPES[] = {"c", "c", "h", "g"};
static const char *ROUTES[] = {"/v1/users", "/v1/order", "/v1/items", "/v2/carts"};

static inline int metric(char *b, uint64_t i) {
  return sprintf(b, "%s:%u|%s|#env:prod,host:web-%02u,route:%s\n",
                 NAMES[i % 4], (unsigned)(i % 97), TYPES[i % 4], (unsigned)(i / 4 % 16), ROUTES[i / 64 % 4]);
}

// Tiny log-linear histogram (16 sub-buckets per power of two). Values in ns.
#define HB 1024
typedef struct { uint64_t n, max, b[HB]; } hist_t;
static inline int hidx(uint64_t v) {
  if (v < 16) return (int)v;
  int m = 63 - __builtin_clzll(v);
  return (m - 3) * 16 + (int)((v >> (m - 4)) & 15);
}
static inline uint64_t hval(int i) { return i < 16 ? (uint64_t)i : (16ull + (i & 15)) << (i / 16 - 1); }
static inline void hadd(hist_t *h, uint64_t v) { h->b[hidx(v)]++; h->n++; if (v > h->max) h->max = v; }
static inline void hmerge(hist_t *d, const hist_t *s) {
  for (int i = 0; i < HB; i++) d->b[i] += s->b[i];
  d->n += s->n; if (s->max > d->max) d->max = s->max;
}
static inline uint64_t hpct(const hist_t *h, double p) {
  uint64_t want = (uint64_t)(p * h->n), c = 0;
  for (int i = 0; i < HB; i++) if ((c += h->b[i]) > want) return hval(i);
  return h->max;
}
