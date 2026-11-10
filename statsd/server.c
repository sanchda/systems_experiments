// A statsd server that does (almost) nothing.
//   -u        unix datagram instead of UDP 127.0.0.1:8125
//   -m        count newlines (metrics) instead of pure discard
//   -R n      n sockets: UDP -> SO_REUSEPORT group, UDS -> n paths
//   -T t      t threads blocking on each socket
//   -B        UDP only: attach CBPF that steers by cpu % n
//   -M v      recvmmsg with vlen v (default: recvmsg, one per syscall)
//   -s bytes  SO_RCVBUF
//   -S ms     stall this long before the first read (fill experiment)
//   -w        record wait time = recv time - SO_TIMESTAMPNS enqueue time
//   -q k      sample SO_MEMINFO rmem_alloc every k reads (-o file: dump series)
//   -C cpu    pin thread i to cpu+i
//   -i ms     exit after this long without traffic (default 1000)
//   -P        busy-poll (MSG_DONTWAIT spin) instead of blocking
// Series (-o) rows: t_ns rmem_alloc sk_drops last_wait_ns thread_cpu_ns
#include "common.h"
#include <linux/filter.h>
#include <linux/sock_diag.h>

#define BUF 65536
#define MAXV 64

static int poll_, uds, count, nsock = 1, nthr = 1, cbpf, vlen = 1, rcvbuf, stall, wait_ts, qk, cpu0 = -1, idle = 1000;
static const char *qout;
static atomic_int stop;

typedef struct {
  int fd, sock, cpu;
  pthread_t t;
  _Atomic uint64_t dg;
  uint64_t bytes, metrics, calls, qmax, qsum, qn;
  uint64_t *qs; size_t qcap, qlen, lastwait;
  unsigned long long ss[3]; // /proc/thread-self/schedstat at exit: on-cpu ns, runqueue-wait ns, timeslices // (t, rmem, drops, wait) series
  hist_t wait;
} thr_t;

static void tsig(int s) { (void)s; atomic_store(&stop, 1); }

static uint64_t meminfo(int fd, int k) {
  uint32_t m[SK_MEMINFO_VARS] = {0}; socklen_t l = sizeof m;
  return getsockopt(fd, SOL_SOCKET, SO_MEMINFO, m, &l) ? 0 : m[k];
}

static void handle(thr_t *t, struct msghdr *h, char *b, size_t n, uint64_t rt) {
  t->bytes += n;
  if (count) for (char *p = b, *e = b + n; (p = memchr(p, '\n', e - p)); p++) t->metrics++;
  for (struct cmsghdr *c = CMSG_FIRSTHDR(h); c; c = CMSG_NXTHDR(h, c)) {
    if (c->cmsg_level != SOL_SOCKET) continue;
    if (c->cmsg_type == SO_TIMESTAMPNS) {
      struct timespec ts; memcpy(&ts, CMSG_DATA(c), sizeof ts);
      uint64_t et = (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
      hadd(&t->wait, t->lastwait = rt > et ? rt - et : 0);
    }
  }
}

static void sample_q(thr_t *t, uint64_t t0) {
  uint32_t m[SK_MEMINFO_VARS] = {0}; socklen_t l = sizeof m;
  getsockopt(t->fd, SOL_SOCKET, SO_MEMINFO, m, &l);
  uint64_t r = m[SK_MEMINFO_RMEM_ALLOC];
  if (r > t->qmax) t->qmax = r;
  t->qsum += r; t->qn++;
  if (t->qs && t->qlen + 5 <= t->qcap) {
    uint64_t *q = t->qs + t->qlen; t->qlen += 5;
    q[0] = now(CLOCK_MONOTONIC); // absolute, to line up with client -L
    q[1] = r; q[2] = m[SK_MEMINFO_DROPS]; q[3] = t->lastwait; q[4] = now(CLOCK_THREAD_CPUTIME_ID);
  }
}

static void *run(void *arg) {
  thr_t *t = arg;
  pin(t->cpu);
  char (*bufs)[BUF] = malloc(MAXV * BUF), (*cms)[256] = malloc(MAXV * 256);
  struct iovec iov[MAXV]; struct mmsghdr mm[MAXV];
  for (int i = 0; i < MAXV; i++) {
    iov[i] = (struct iovec){bufs[i], BUF};
    mm[i].msg_hdr = (struct msghdr){.msg_iov = &iov[i], .msg_iovlen = 1};
  }
  uint64_t t0 = now(CLOCK_MONOTONIC);
  if (stall) {
    usleep(stall * 1000);
    if (qk) sample_q(t, t0); // depth at the moment we finally look
  }
  while (!atomic_load_explicit(&stop, memory_order_relaxed)) {
    for (int i = 0; i < vlen; i++) {
      mm[i].msg_hdr.msg_control = cms[i]; mm[i].msg_hdr.msg_controllen = sizeof cms[i];
    }
    int n;
    if (vlen == 1) {
      ssize_t r = recvmsg(t->fd, &mm[0].msg_hdr, poll_ ? MSG_DONTWAIT : 0);
      n = r < 0 ? -1 : 1; if (r >= 0) mm[0].msg_len = r;
    } else n = recvmmsg(t->fd, mm, vlen, poll_ ? MSG_DONTWAIT : MSG_WAITFORONE, NULL);
    if (n < 0) {
      if (errno == EAGAIN || errno == EINTR) continue;
      DIE("recv");
    }
    uint64_t rt = wait_ts ? now(CLOCK_REALTIME) : 0;
    t->calls++;
    for (int i = 0; i < n; i++) handle(t, &mm[i].msg_hdr, bufs[i], mm[i].msg_len, rt);
    atomic_fetch_add_explicit(&t->dg, n, memory_order_relaxed);
    if (qk && t->calls % qk == 0) sample_q(t, t0);
  }
  FILE *f = fopen("/proc/thread-self/schedstat", "r");
  if (f) { if (fscanf(f, "%llu %llu %llu", &t->ss[0], &t->ss[1], &t->ss[2]) != 3) t->ss[1] = 0; fclose(f); }
  return NULL;
}

static int mksock(int idx) {
  addr_t a; socklen_t al = mkaddr(&a, uds, idx);
  int fd = socket(uds ? AF_UNIX : AF_INET, SOCK_DGRAM, 0), one = 1;
  if (fd < 0) DIE("socket");
  if (uds) unlink(a.un.sun_path);
  else if (nsock > 1 && setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one)) DIE("SO_REUSEPORT");
  if (rcvbuf && setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf)) DIE("SO_RCVBUF");
  if (wait_ts && setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPNS, &one, sizeof one)) DIE("SO_TIMESTAMPNS");
  struct timeval tv = {0, 100000};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  if (bind(fd, &a.sa, al)) DIE("bind");
  return fd;
}

int main(int argc, char **argv) {
  for (int o; (o = getopt(argc, argv, "umR:T:BM:s:S:wq:o:C:i:P")) != -1;) switch (o) {
    case 'u': uds = 1; break;            case 'm': count = 1; break;
    case 'R': nsock = atoi(optarg); break; case 'T': nthr = atoi(optarg); break;
    case 'B': cbpf = 1; break;           case 'M': vlen = atoi(optarg); break;
    case 's': rcvbuf = atoi(optarg); break; case 'S': stall = atoi(optarg); break;
    case 'w': wait_ts = 1; break;        case 'q': qk = atoi(optarg); break;
    case 'o': qout = optarg; break;      case 'C': cpu0 = atoi(optarg); break;
    case 'i': idle = atoi(optarg); break; case 'P': poll_ = 1; break;
    default: return 2;
  }
  if (vlen < 1 || vlen > MAXV) vlen = MAXV;
  signal(SIGINT, tsig); signal(SIGTERM, tsig);
  int fds[64], nt = nsock * nthr;
  for (int i = 0; i < nsock; i++) fds[i] = mksock(i);
  if (cbpf && !uds) {
    struct sock_filter f[] = {
      {BPF_LD | BPF_W | BPF_ABS, 0, 0, SKF_AD_OFF + SKF_AD_CPU},
      {BPF_ALU | BPF_MOD | BPF_K, 0, 0, (uint32_t)nsock},
      {BPF_RET | BPF_A, 0, 0, 0}};
    struct sock_fprog p = {3, f};
    if (setsockopt(fds[0], SOL_SOCKET, SO_ATTACH_REUSEPORT_CBPF, &p, sizeof p)) DIE("CBPF");
  }
  thr_t *T = calloc(nt, sizeof *T);
  for (int i = 0; i < nt; i++) {
    T[i].sock = i % nsock; T[i].fd = fds[T[i].sock]; T[i].cpu = cpu0 < 0 ? -1 : cpu0 + i;
    if (qout && i == 0) T[i].qs = malloc((T[i].qcap = 1 << 24) * 8);
    pthread_create(&T[i].t, NULL, run, &T[i]);
  }
  fprintf(stderr, "ready\n");
  // Exit once traffic has started and then gone quiet for `idle` ms.
  uint64_t last = 0, quiet = 0, first = 0, lastrx = 0;
  while (!atomic_load(&stop)) {
    usleep(10000);
    uint64_t s = 0;
    for (int i = 0; i < nt; i++) s += atomic_load_explicit(&T[i].dg, memory_order_relaxed);
    uint64_t tn = now(CLOCK_MONOTONIC);
    if (s && !first) first = tn;
    if (s != last) { last = s; quiet = 0; lastrx = tn; }
    else if (s && (quiet += 10) >= (uint64_t)idle) break;
  }
  atomic_store(&stop, 1);
  for (int i = 0; i < nt; i++) pthread_join(T[i].t, NULL);

  uint64_t dg = 0, by = 0, me = 0, ca = 0, qm = 0;
  uint64_t dropsock[64] = {0}, dgsock[64] = {0};
  hist_t *w = calloc(1, sizeof *w);
  for (int i = 0; i < nt; i++) {
    dg += T[i].dg; by += T[i].bytes; me += T[i].metrics; ca += T[i].calls;
    if (T[i].qmax > qm) qm = T[i].qmax;
    dgsock[T[i].sock] += T[i].dg;
    hmerge(w, &T[i].wait);
  }
  uint64_t drops = 0;
  for (int i = 0; i < nsock; i++) drops += dropsock[i] = meminfo(fds[i], SK_MEMINFO_DROPS);
  double secs = first && lastrx > first ? (lastrx - first) / 1e9 : 0;
  printf("srv dgrams=%lu bytes=%lu metrics=%lu calls=%lu drops=%lu secs=%.3f dgram_per_s=%.0f",
         dg, by, me, ca, drops, secs, secs ? dg / secs : 0);
  if (count) printf(" metric_per_s=%.0f", secs ? me / secs : 0);
  if (qk) printf(" rmem_peak=%lu rmem_mean=%.0f", qm, T[0].qn ? (double)T[0].qsum / T[0].qn : 0);
  if (wait_ts) printf(" wait_p50_us=%.1f wait_p99_us=%.1f wait_max_us=%.1f",
                      hpct(w, .5) / 1e3, hpct(w, .99) / 1e3, w->max / 1e3);
  printf(" t0_oncpu_ms=%.1f t0_runq_wait_ms=%.2f t0_slices=%llu", T[0].ss[0] / 1e6, T[0].ss[1] / 1e6, T[0].ss[2]);
  if (nsock > 1) { printf(" per_sock="); for (int i = 0; i < nsock; i++) printf("%s%lu", i ? "," : "", dgsock[i]); }
  printf("\n");
  if (qout && T[0].qs) {
    FILE *f = fopen(qout, "w");
    for (size_t i = 0; i < T[0].qlen; i += 5)
      fprintf(f, "%lu %lu %lu %lu %lu\n", T[0].qs[i], T[0].qs[i + 1], T[0].qs[i + 2], T[0].qs[i + 3], T[0].qs[i + 4]);
    fclose(f);
  }
  if (uds) for (int i = 0; i < nsock; i++) { addr_t a; mkaddr(&a, 1, i); unlink(a.un.sun_path); }
  return 0;
}
