# statsd experiments re-run on a 2-vCPU KVM guest

Reproduction of the experiments in `../Readme.md`, on very different hardware. Raw output is in this directory;
scripts are `../sweep2.sh`, `../run_all2.sh`, `../hwbench.c` (the committed scripts are unchanged).
`invalid_bad_rates/` holds a first matrix run that had a shell bug in the rate list (`5$M` expanded to `51000000`), so
every "paced" point was actually flat-out. It is kept only for transparency and should not be used.

## 1. Hardware / environment

| | this box | Readme box |
|---|---|---|
| CPU | Intel Xeon (Skylake-SP, 2.3 GHz-class), **2 vCPUs, no SMT** | i7-11800H, 8 vCPUs (4C/8T) |
| Virtualization | KVM guest (kvm-clock), PTI + IBRS mitigations active | WSL2 / Hyper-V |
| RAM | 3.7 GiB | n/a |
| Kernel / gcc | 6.8.0-138-generic (Ubuntu) / 13.3 | 6.18.58 WSL2 / 11.4 |
| `rmem_default` / `rmem_max` | 212992 / 212992 | 212992 / 4194304 |
| `net.unix.max_dgram_qlen` | **512** | 10 |
| privileges | non-root, no sudo (sysctls unchanged) | n/a |

CPU layout used: server pinned to CPU 0, client threads all on CPU 1 (`-C 1 -Z 0`). Anything that needed more CPUs
(the Readme's server on CPU 2, clients on 4-7) was scaled down; see caveats.

Measured micro-costs (`hwbench`, `hw_microbench.txt`, 3 runs):

| operation | cost |
|---|---|
| `getppid()` syscall | ~860 ns |
| `clock_gettime` (vDSO) | ~33 ns |
| `send()` 60 B UDP over loopback, receiver full | ~2.9 us |
| `recv()` 60 B UDP, already queued | ~1.5 us |
| send+recv pair, same thread | ~4.0-4.3 us |
| pipe ping-pong, same CPU / across CPUs | ~8 us / **~45 us** round trip |

Scheduler jitter with no sockets (`jitter`): spinning thread saw gaps up to 2.9 ms (CPU 1) and none > 79 us on CPU 0
in 5 s. A 100 us sleeper on an idle CPU 0 woke >1 ms late 11-16 times per 5 s (max 2.3-4.7 ms), with or without a
spinner on the other vCPU (the loaded runs ranged 9-30 events, so the effect of load is not established).

## 2. Throughput (sections 2 and 4 of the Readme)

All per-point results are 3 trials, 3 s each (`udp_*.txt`, `uds_*.txt`). Ranges below are min-max.

**UDP, 1 metric/datagram, one client thread.** Offered load is limited by the single client CPU
(~220k/s blocking server, ~290k/s busy-poll server flat out). The server is *not* saturated, so no server capacity
figure can be taken from these runs.

| offered | loss, blocking | loss, busy-poll | p99 wait blocking / busy-poll |
|---|---|---|---|
| 50k | 0-0.016% | 0% | 0.6-1.5 ms / 10-15 us |
| 100k | 0.06-0.16% | 0-0.002% | ~1.0-1.2 ms / 11-13 us |
| 200k | 0.5-1.8% | 0-0.19% | 0.75-1.0 ms / 12-24 us |
| 300k (client delivers ~210-260k) | 1.5-3.6% | 0.05-0.18% | ~0.9-1.1 ms / 26-43 us |
| flat out | 1.9-2.3% (~215-222k/s) | 0.06-1.0% (~266-293k/s) | ~0.95 ms / 140-490 us |

- Counting newlines (`-m`) made no clear difference. `recvmmsg` (`-M 64`) lost 0-1.1% at 100-300k/s, vs 0.5-3.6% for
  `recvmsg` at the same rates, and the flat-out rate was about the same (~216-232k/s).
- Blocking vs busy-poll at low rates reproduces the Readme's pattern: the blocking server loses packets at rates far
  below its capability, the spinning one mostly doesn't.

**Batching** (one client thread; busy-poll columns are the capacity-like numbers):

| config | blocking, <=~1% loss | busy-poll | flat-out delivered (busy-poll) | datagrams/s |
|---|---|---|---|---|
| UDP 1 metric | ~100k | ~300k | ~0.27-0.29M/s | ~0.27-0.30M |
| UDP 1432 B | ~2M (0.1-0.7%); 5M = 1.7-4.6% | 5M at <=0.08%; 10M+ ~0.3-0.8% | **~6.0-6.1M/s** | ~265k |
| UDP 8192 B | ~10M (0.8-1.3%) | 20M at 0.2-0.6% | **~20-21.5M/s** | ~150-160k |
| UDS 8192 B, non-blocking | ~10M (0.6-1.8%) | 10M at <=0.25%; 20M at 0.2-1.1% | ~26-27M/s (8.5% loss) | ~195k |

The Readme's pattern holds: datagrams/s stays in the same ballpark regardless of size, so metrics/s scales with
metrics per datagram. Absolute numbers are 3-5x lower than the Readme's.

**UDS.** Blocking senders see no loss, as in the Readme, but the "convoy" collapse does not appear:

| config | result here | Readme |
|---|---|---|
| blocking, 1 client | ~340-380k/s | ~1.0M/s |
| blocking, 4 clients (same CPU) | ~290-320k/s | **~85k/s** |
| blocking 8192 B, 1 / 3 clients | ~22M/s / 18-25M/s | ~50M/s / ~9.3M/s |
| non-blocking, 1 metric, blocking server | 0-0.02% to 100k/s; 0.4-1.8% at 200k; 2.5-5% at 300k | ~1% already at 250-500k |
| same, busy-poll server | <=0.5% up to 300k; flat out 16-21% loss, ~0.4M/s | 0.03-0.13% at 250-500k, flat out ~0.9M/s |

The queue-depth difference (`max_dgram_qlen` 512 vs 10) is the obvious candidate for the absence of the collapse, but
the client threads here also share a single CPU, so this run cannot separate the two causes.

## 3. Buffer capacity (`fill_capacity.txt`, 3 trials each, identical every time)

Server stalled 1.5 s, non-blocking client floods it, count what survives.

| payload | UDP datagrams / metrics fit | Readme UDP | UDS datagrams / metrics fit |
|---|---|---|---|
| 60 B | 222 / 222 | 221 | **278** / 278 |
| 1432 B | 93 / 2139 | 92 / ~2100 | 93 / 2139 |
| 8192 B | 13 / 1768 | 13 / ~1800 | 13 / 1768 |

UDP reproduces essentially exactly (~16x truesize overhead for tiny datagrams). UDS no longer holds 11: it fits
278 / 93 / 13, i.e. the same byte-based limit as UDP for the larger sizes, and a bit more for 60 B. That is
consistent with the count limit (512) no longer binding and the sender's send buffer (212992) being the limit, but I did
not confirm that against kernel 6.8 source.

I did not repeat the per-receive queue-depth time series (`fill_udp_*.txt`) or the wait ~ buffer / drain-rate fit.

## 4. The "idle server drops metrics" puzzle (section 3b)

5 trials, blocking server, 200k/s paced, `-q 1 -o` per-receive log + client `-L` log (`3b_gap_analysis.txt`):

- **Suspect 1, miscounting: ruled out.** `SK_MEMINFO_DROPS` equals the system-wide `RcvbufErrors` delta in every trial
  (e.g. 10621 = 10621, 31071 = 31071).
- **Gaps explain drops.** Drops come in bursts after 2-5 ms stretches with essentially no server CPU
  (17-44 us of thread CPU during a multi-ms gap), and `drops ~ gap x rate - 222` holds: 4317 us -> 641 predicted,
  641 counted; 2194 us -> 217 predicted, 215 counted.
- **Suspect 2, bursty client: NOT ruled out here.** Unlike the Readme, the client logged ~100k late sends per trial
  and stalls of 12-17 ms, so I cannot show the client was steady. On one shared client CPU this is plausible but unproven.
- **Suspect 4, scheduler:** server run-queue wait was 21-116 ms per trial.
- Observer effect: with `-q 1` drops were 2-5% vs 0.5-1.8% in the plain sweep at the same rate (the Readme makes the
  same point).
- Not done: `debug/who.c` (identify what runs on the stolen CPU). It assumes spare CPUs 0 and 2 and I did not adapt it.

Fix tests, 200k/s, 5 trials each (`3b_fixes.txt`):

| variant | loss per trial |
|---|---|
| blocking | 0.47, 0.56, 0.99, 1.26, 3.0% |
| busy-poll | 0.006, 0.014, 0.0, 0.077, **0.58%** |
| blocking, `SO_RCVBUF` request 4 MiB | 0.30, 0.39, 0.65, 0.93, 1.4% |

Busy-poll helps a lot but is not perfectly clean (one 0.58% outlier, like the Readme's single outlier). The big-buffer
test is **not equivalent** to the Readme's: `rmem_max` is 212992 here, so the request was capped and only doubled
the buffer (rmem peak 426240), about 2.2 ms of slack, less than the 2-5 ms stalls. Consistent with the
explanation, but not a confirmation. Waits roughly doubled (p99 1.2-2.0 ms), the Readme's "bigger buffer = more wait" point.

## 5. Aggregation (section 6, `agg.txt`, 2 runs each)

The reduction ratios are identical to the Readme (deterministic): raw 5.4x vs sort/sum 130x at H=16; 1.2x vs 8.1x at
H=256; 1.0x vs 2.3x at H=4096. Costs are 2.5-3x higher per line:

| H | parse | raw | sort | sum (ns/line) |
|---|---|---|---|---|
| 16 | 108-122 | 205-217 | 220-227 | 174-186 |
| 256 | 107-115 | 271-279 | 263-273 | 205-214 |
| 4096 | 111-121 | 287-292 | 292-309 | 241-248 |

Here parsing alone is ~110-120 ns/line (38-56% of the per-line cost, depending on H), so one core parses ~8-9M
lines/s (vs the Readme's ~25M/s). Order-independent sum is still cheapest; the Readme's "sort costs 15-20 ns over
raw" is not consistently reproduced (here sort minus raw ranges from -8 to +22 ns across runs).

## 6. SO_REUSEPORT (section 7)

`probe` (`hw_probe.txt`): UDP reuseport ok; UDS `SO_REUSEPORT` -> `EOPNOTSUPP`, second bind `EADDRINUSE`. The Readme says
6.8-6.10 silently accepted it; **Ubuntu's 6.8.0-138 rejects it**, so the change appears to have been backported.

Per-socket split, 4 sockets, 100k/s, 3 trials, server unpinned (`reuseport_split.txt`):

| flows | steering | split |
|---|---|---|
| 1 | hash | all on one socket (3/3) |
| 2 | hash | 2 of 4 sockets used (3/3) |
| 4 | hash | 2-3 of 4 used, e.g. 75k,0,75k,149k / 150k,0,150k,0 |
| 8 | hash | lumpy, e.g. 37k,0,146k,112k |
| 2 (cpu 0,1) | CBPF `cpu % 4` | even over sockets 0,1; sockets 2,3 unused (only 2 CPUs exist) |
| 2 (cpu 0,1) | CBPF `cpu % 2` (-R 2) | 150k,150k (even) |

This matches the Readme: hash steering is lumpy with few flows; CBPF is even but bounded by CPU count.
I did NOT reproduce the throughput comparison (4 reuseport sockets vs 1): it needs more CPUs than exist here.

UDS multi-path (unpinned, 3 trials, `uds_paths.txt`):

| setup | delivered |
|---|---|
| non-blocking, 1 path, 1 client | 320-450k/s, 17-31% loss |
| non-blocking, 1 path, 4 clients | 160-220k/s, 76-79% loss |
| non-blocking, 1 path, 4 clients, 4 server threads | ~340-350k/s, 28-34% loss |
| non-blocking, 4 paths, 4 clients | ~255-272k/s, 70-71% loss |
| blocking, 1 path, 4 clients | ~225-230k/s, no loss |
| blocking, 4 paths, 4 clients | ~525k/s, no loss |

Only the blocking comparison (4 paths ~2.3x one path) is a clean signal. The non-blocking flat-out rows are dominated by
4 clients and the server fighting over 2 CPUs and should not be read as a statement about UDS itself.

## 7. Summary: how this box differs

1. **It is far slower per operation**: ~860 ns syscalls and ~1.5 us `recv`, so every rate is several times lower than
   the Readme's. With one client CPU the unbatched UDP/UDS tests are client-bound, so server capacity was not measured.
2. **The stall problem is present** (>1 ms wakeup overshoot 11-16x/5 s, 2-5 ms server gaps), so a blocking server loses
   packets at rates well below its capability, and busy-polling mostly fixes it. The stalls are shorter than the
   Readme's (<=5 ms vs up to 146 ms), but the one-client-CPU setup also stalls, so client-side causes are not excluded.
3. **UDP buffer arithmetic is hardware independent**: 222/93/13 datagrams, ~16x truesize, same as the Readme.
4. **UDS depends on sysctl and kernel**: `max_dgram_qlen=512` and a 6.8 kernel that rejects UDS `SO_REUSEPORT`
   change the numbers and one of the Readme's claims about 6.8.
5. **Aggregation results are CPU-bound, not system-bound**: same ratios, 2.5-3x slower.

## Caveats

- 2 vCPUs: server and clients cannot be placed on separate physical cores with SMT siblings apart, as the Readme does;
  paced tests use one client thread. Multi-client tests (`-c 3/4`) share one CPU.
- Not reproduced: queue-depth time series, `debug/who.c`, reuseport throughput comparison, `sysctl` experiments (no root),
  4 MiB `SO_RCVBUF`.
- Single run of each configuration set (3 trials per point); variation between trials is large for blocking servers.
- Explanations for the 2-5 ms gaps (host vCPU descheduling vs guest scheduling) were not separated here.
