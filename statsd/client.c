// A statsd client: generates ~60 byte metrics and ships them.
//   -u        unix datagram instead of UDP
//   -n N      total metrics (default 1e6)
//   -b bytes  pack metrics into datagrams up to this size (default: one metric per datagram)
//   -r rate   target metrics/s across all threads (default: flat out)
//   -c k      k threads, each with its own socket (= its own flow / source port)
//   -P p      UDS: thread i sends to path i % p
//   -N        non-blocking sends (EAGAIN = drop at the client)
//   -C cpu    pin thread i to cpu+i (-c with -Z z: cpu+i*z)
//   -q        sample SIOCOUTQ (bytes charged to this socket) peak
//   -L file   log pacer stalls: sends issued >50us after their scheduled time (t_abs thread late_ns)
#include "common.h"
#include <sys/ioctl.h>
#include <linux/sockios.h>

static int uds, batch, nthr = 1, npath = 1, nb, cpu0 = -1, stride = 1, sampleq;
static uint64_t total = 1000000;
static double rate;
static const char *latef;
#define POOL 4096
static char pool[POOL][96];
static int plen[POOL];

typedef struct { int id; pthread_t t; uint64_t sent, dg, eagain, outq, ns, latemax, nlate, *ev; size_t nev; } thr_t;
#define MAXEV (1 << 20)

static void *run(void *arg) {
  thr_t *t = arg;
  pin(cpu0 < 0 ? -1 : cpu0 + t->id * stride);
  addr_t a; socklen_t al = mkaddr(&a, uds, t->id % npath);
  int fd = socket(uds ? AF_UNIX : AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) DIE("socket");
  if (connect(fd, &a.sa, al)) DIE("connect");
  uint64_t n = total / nthr, k = 0, t0 = now(CLOCK_MONOTONIC);
  double per = rate > 0 ? 1e9 * nthr / rate : 0;
  static _Thread_local char buf[65536];
  int cap = batch ? batch : 1;
  while (k < n) {
    int len = 0; uint64_t k0 = k;
    do {
      int i = (t->id * 7919 + k) % POOL;
      if (len && len + plen[i] > cap) break;
      memcpy(buf + len, pool[i], plen[i]); len += plen[i]; k++;
    } while (k < n && batch);
    if (per) {
      uint64_t due = t0 + (uint64_t)(per * k), tn;
      while ((tn = now(CLOCK_MONOTONIC)) < due) ;
      uint64_t late = tn - due;
      if (late > t->latemax) t->latemax = late;
      if (late > 50000) { t->nlate++; if (t->ev && t->nev < MAXEV) { t->ev[2 * t->nev] = tn; t->ev[2 * t->nev++ + 1] = late; } }
    }
    if (send(fd, buf, len, nb ? MSG_DONTWAIT : 0) < 0) {
      if (errno == EAGAIN) { t->eagain += k - k0; continue; } // metrics lost
      if (errno == ECONNREFUSED) { t->eagain += k - k0; continue; } // UDP: no listener yet
      DIE("send");
    }
    t->sent += k - k0; t->dg++;
    if (sampleq && (t->dg & 63) == 0) {
      int q; if (!ioctl(fd, SIOCOUTQ, &q) && (uint64_t)q > t->outq) t->outq = q;
    }
  }
  t->ns = now(CLOCK_MONOTONIC) - t0;
  close(fd);
  return NULL;
}

int main(int argc, char **argv) {
  for (int o; (o = getopt(argc, argv, "un:b:r:c:P:NC:Z:qL:")) != -1;) switch (o) {
    case 'u': uds = 1; break;              case 'n': total = strtod(optarg, 0); break;
    case 'b': batch = atoi(optarg); break; case 'r': rate = strtod(optarg, 0); break;
    case 'c': nthr = atoi(optarg); break;  case 'P': npath = atoi(optarg); break;
    case 'N': nb = 1; break;               case 'C': cpu0 = atoi(optarg); break;
    case 'Z': stride = atoi(optarg); break; case 'q': sampleq = 1; break;
    case 'L': latef = optarg; break;
    default: return 2;
  }
  uint64_t lsum = 0;
  for (int i = 0; i < POOL; i++) lsum += plen[i] = metric(pool[i], i);
  thr_t *T = calloc(nthr, sizeof *T);
  for (int i = 0; i < nthr; i++) { T[i].id = i; if (latef) T[i].ev = malloc(MAXEV * 16); pthread_create(&T[i].t, NULL, run, &T[i]); }
  uint64_t s = 0, d = 0, e = 0, q = 0, ns = 0, lm = 0, nl = 0;
  for (int i = 0; i < nthr; i++) {
    pthread_join(T[i].t, NULL);
    s += T[i].sent; d += T[i].dg; e += T[i].eagain;
    if (T[i].outq > q) q = T[i].outq;
    if (T[i].ns > ns) ns = T[i].ns;
    if (T[i].latemax > lm) lm = T[i].latemax;
    nl += T[i].nlate;
  }
  if (latef) {
    FILE *f = fopen(latef, "w");
    for (int i = 0; i < nthr; i++) for (size_t j = 0; j < T[i].nev; j++) fprintf(f, "%lu %d %lu\n", T[i].ev[2 * j], i, T[i].ev[2 * j + 1]);
    fclose(f);
  }
  printf("cli metrics=%lu dgrams=%lu eagain_metrics=%lu secs=%.3f metric_per_s=%.0f dgram_per_s=%.0f avg_line=%.1f",
         s, d, e, ns / 1e9, s / (ns / 1e9), d / (ns / 1e9), (double)lsum / POOL);
  if (sampleq) printf(" outq_peak=%lu", q);
  if (rate > 0) printf(" pace_late_max_us=%.1f sends_late_50us=%lu", lm / 1e3, nl);
  printf("\n");
  return 0;
}
