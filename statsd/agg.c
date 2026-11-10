// Client-side aggregation: collapse lines that name the same context.
// Counts sum, gauges keep the last value, histograms pass through untouched.
// Tags arrive in random order, so "same context" needs a canonical key:
//   raw   hash the tag string as written (no normalization)
//   sort  sort the tags, then hash
//   sum   order-independent: sum of mixed per-tag hashes
//   parse parse only, no table (baseline cost)
//   -n lines (default 1e7)  -f flush every f lines (default 1e5)  -H hosts (default 16)
#include "common.h"

typedef struct { const char *p; int n; } sl_t;
typedef struct { uint64_t k; double v; uint32_t n; char t; } ent_t;
#define CAP (1 << 18) // > 2x the flush interval, so the table never fills
static ent_t tab[CAP];
static uint32_t occ[CAP], used; // occupied slots, so a flush clears only what it used

static inline uint64_t fnv(const char *p, int n, uint64_t h) {
  for (int i = 0; i < n; i++) h = (h ^ (uint8_t)p[i]) * 0x100000001b3ull;
  return h;
}
static inline uint64_t mix(uint64_t x) {
  x ^= x >> 33; x *= 0xff51afd7ed558ccdull; x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ull; return x ^ (x >> 33);
}

// name:value|type|#t1,t2,...
static int parse(const char *s, const char *e, sl_t *name, double *v, char *t, sl_t *tagstr, sl_t *tags) {
  const char *c = memchr(s, ':', e - s), *b = memchr(c, '|', e - c);
  *name = (sl_t){s, (int)(c - s)};
  *v = strtod(c + 1, NULL);
  *t = b[1];
  const char *h = memchr(b + 1, '#', e - b - 1);
  int nt = 0;
  *tagstr = (sl_t){h ? h + 1 : e, h ? (int)(e - h - 1) : 0};
  for (const char *p = tagstr->p; p < e; nt++) {
    const char *q = memchr(p, ',', e - p); if (!q) q = e;
    tags[nt] = (sl_t){p, (int)(q - p)}; p = q + 1;
  }
  return nt;
}

static inline int slcmp(sl_t a, sl_t b) {
  int r = memcmp(a.p, b.p, a.n < b.n ? a.n : b.n);
  return r ? r : a.n - b.n;
}

static void put(uint64_t k, double v, char t) {
  for (uint64_t i = k & (CAP - 1);; i = (i + 1) & (CAP - 1)) {
    ent_t *e = &tab[i];
    if (!e->n) { *e = (ent_t){k, v, 1, t}; occ[used++] = i; return; }
    if (e->k == k) { e->v = t == 'c' ? e->v + v : v; e->n++; return; }
  }
}

void clear(void) { for (uint32_t i = 0; i < used; i++) tab[occ[i]].n = 0; used = 0; }

int main(int argc, char **argv) {
  uint64_t N = 10000000, F = 100000; int H = 16;
  for (int o; (o = getopt(argc, argv, "n:f:H:")) != -1;) switch (o) {
    case 'n': N = strtod(optarg, 0); break; case 'f': F = strtod(optarg, 0); break;
    case 'H': H = atoi(optarg); break; default: return 2;
  }
  // Pre-generate lines with shuffled tag order: 4 names x H hosts x 4 routes x 3 az.
  static const char *nm[] = {"web.request.count", "web.request.bytes", "db.query.latency", "cache.size"};
  static const char *ty[] = {"c", "c", "h", "g"};
  static const char *az[] = {"us-east-1a", "us-east-1b", "us-east-1c"};
  char *buf = malloc(N * 96), *p = buf; uint32_t *off = malloc((N + 1) * sizeof *off);
  uint64_t rng = 88172645463325252ull, bytes = 0;
  if (F > CAP / 2) F = CAP / 2;
  for (uint64_t i = 0; i < N; i++) {
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    char tg[4][32]; int k = 0;
    sprintf(tg[k++], "env:prod"); sprintf(tg[k++], "host:web-%02d", (int)(rng % H));
    sprintf(tg[k++], "route:%s", ROUTES[(rng >> 8) % 4]); sprintf(tg[k++], "az:%s", az[(rng >> 16) % 3]);
    for (int j = 3; j > 0; j--) { int r = (rng >> (24 + 4 * j)) % (j + 1); char x[32]; memcpy(x, tg[j], 32); memcpy(tg[j], tg[r], 32); memcpy(tg[r], x, 32); }
    int n = (rng >> 48) % 4;
    off[i] = p - buf;
    p += sprintf(p, "%s:%d|%s|#%s,%s,%s,%s", nm[n], (int)(rng >> 56) % 100, ty[n], tg[0], tg[1], tg[2], tg[3]);
  }
  off[N] = p - buf; bytes = p - buf;
  printf("lines=%lu avg_line=%.1f flush_every=%lu contexts<=%d\n", N, (double)bytes / N, F, 4 * H * 4 * 3);

  const char *modes[] = {"parse", "raw", "sort", "sum"};
  for (int m = 0; m < 4; m++) {
    memset(tab, 0, sizeof tab); used = 0;
    uint64_t cg_in = 0, cg_out = 0, pass = 0, flushes = 0; volatile double sink = 0;
    uint64_t t0 = now(CLOCK_MONOTONIC);
    for (uint64_t i = 0; i < N; i++) {
      if (i && i % F == 0) { cg_out += used; flushes++; clear(); }
      sl_t name, ts, tags[16]; double v; char t;
      int nt = parse(buf + off[i], buf + off[i + 1], &name, &v, &t, &ts, tags);
      if (m == 0) { sink += v + nt; continue; }
      if (t != 'c' && t != 'g') { pass++; continue; } // histograms: ship as-is
      cg_in++;
      uint64_t h = fnv(name.p, name.n, 0xcbf29ce484222325ull) ^ (uint8_t)t;
      if (m == 1) h = fnv(ts.p, ts.n, h);
      else if (m == 2) {
        for (int a = 1; a < nt; a++) { sl_t x = tags[a]; int b = a; for (; b && slcmp(tags[b - 1], x) > 0; b--) tags[b] = tags[b - 1]; tags[b] = x; }
        for (int a = 0; a < nt; a++) h = fnv(tags[a].p, tags[a].n, h ^ ',');
      } else {
        uint64_t s = 0;
        for (int a = 0; a < nt; a++) s += mix(fnv(tags[a].p, tags[a].n, 0xcbf29ce484222325ull));
        h = mix(h ^ s);
      }
      put(h, v, t);
    }
    cg_out += used; flushes++; clear();
    double ns = (double)(now(CLOCK_MONOTONIC) - t0) / N;
    if (m == 0) printf("%-5s %6.1f ns/line\n", modes[m], ns);
    else printf("%-5s %6.1f ns/line  count+gauge lines %lu -> %lu per flush (%.1fx)  total out %.1f%% of in\n",
                modes[m], ns, cg_in / flushes, cg_out / flushes, (double)cg_in / cg_out, 100.0 * (cg_out + pass) / N);
  }
  return 0;
}
