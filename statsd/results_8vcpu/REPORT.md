# statsd experiments re-run on an 8-vCPU KVM guest (AMD EPYC-Rome)

Scripts: `../run_all8.sh` (= `run_all.sh` writing to `results_8vcpu/`), `../run_rest8.sh` (busy-poll sweeps, reuseport/paths,
3b fixes, fill capacity, jitter, agg, hwbench). Binaries built with `build.sh` (gcc, `-O2`). `debug/who.c` run for 30 s.
Original layout kept: server on CPU 2, clients from CPU 4 (CPUs 0-7 are separate cores, no SMT). 3 trials x 3 s per point unless noted.
Raw per-point tables are the other `*.txt` files here; `run_all.log` / `run_rest.log` are the console logs.

## Environment
| | this box | Readme box | 2-vCPU box |
|---|---|---|---|
| CPU | AMD EPYC-Rome, 8 vCPUs, no SMT | i7-11800H, 8 vCPUs (4C/8T) | Xeon Skylake-SP, 2 vCPUs |
| Virtualization | KVM | WSL2 / Hyper-V | KVM |
| Kernel | 6.8.0-138 (Ubuntu) | 6.18.58 | 6.8.0-138 |
| RAM | 15 GiB | n/a | 3.7 GiB |
| `rmem_max` | 212992 | 4194304 | 212992 |
| `net.unix.max_dgram_qlen` | 512 | 10 | 512 |
| privileges | non-root, no sudo | n/a | non-root |

Micro-costs (`hw_microbench.txt`, 3 runs): `getppid` ~90-100 ns, `clock_gettime` ~32 ns, `send()` 60 B ~1.55-1.66 us,
`recv()` ~0.6-0.67 us, send+recv ~1.9-2.2 us, pipe ping-pong same CPU ~3.7-4.5 us / cross-CPU ~25-26 us.
(2-vCPU box: 860 ns / 2.9 us / 1.5 us / 45 us.) No PTI-style syscall penalty is visible here.

## What reproduced
* **Buffer capacity** (`fill_capacity.txt`): UDP 222 / 93 / 13 datagrams for 60 / 1432 / 8192 B (2139 / 1768 metrics for the latter two), exactly as on both other boxes.
  UDS held 278 / 93 / 13 datagrams (qlen 512; 2139 and 1768 metrics for the larger sizes).
* **Batching** is what scales metrics/s; datagrams/s is ~0.9-1.1M regardless of size at overload. Busy-poll, <=~0.1% loss:
  UDP 1432 B 10M/s (20M/s: 9-15%), UDP 8192 B 20M/s (40M/s: 0.1-0.9%).
* **Aggregation** ratios identical: 5.4x raw vs 130x sorted/summed (H=16), 1.2x vs 8.1x, 1.0x vs 2.3x. Cost per line: parse ~63-80 ns, raw ~180-240, sort ~190-255, sum ~130-180 ns (sum is cheaper than both raw and sort at every H).
* **UDS `SO_REUSEPORT`** returns EOPNOTSUPP (`probe.txt`), same as the 2-vCPU box (also 6.8).
* **UDS blocking, 1 client vs 4 clients**: ~0.84-0.95M/s vs ~0.18M/s. The four-client collapse (Readme: 85k/s) appears here, though milder.
  4 paths cure it: ~2.9-3.1M/s, no loss.
* **UDS multi-path** non-blocking: 4 paths 0.0-0.3% loss at 1.5M/s, flat out 2.0-3.0M/s delivered at ~40% loss. A single fd with 4 server threads is worse (1M/s offered: 51-59% loss).
* **reuseport CBPF vs hash** (4 sockets): CBPF 0.03-0.24% loss to 2M offered; hash is lumpier (0.002-24.5%).
  Neither beats ~1.2-1.4M/s offered since the 4 senders get slower.

## What differed
* **UDP single-socket capacity is ~1.0M/s delivered here** (busy-poll flat out 0.93-0.98M/s delivered at 41-45% loss; blocking ~0.9-1.08M/s), versus ~1.7M/s on the Readme box.
  1M/s offered already loses 6-13%; 500k/s is clean (<=0.06%).
* **The 3b blocking-server puzzle did not reproduce.** At 250k/s the blocking server lost 0.002-0.052% (5 trials, `3b_baseline_block.txt`); Readme's baseline lost 3k-26k datagrams per trial.
  1 client: 0 loss. `-s 4194304` (capped at 2x rmem_max, ~2x slack): 0 loss. Busy-poll: 0 loss. Blocking vs busy-poll p99 wait: ~30 us vs ~8-12 us.
  Blocking-vs-busy-poll loss below capacity is indistinguishable in the main sweeps (e.g. 500k/s: 0.000-0.063% vs 0.000%).
* **Scheduler jitter is small here** (`jitter.txt`): spinning thread on CPU 2 max gap 268-718 us, nothing >1 ms in 5 s; sleeper woke >1 ms late 3-5 times per 5 s (4.4-12.7 ms total),
  and with 4 spinners 0-4 times. The 2-vCPU box had 11-16 events. `debug/who.c` produced no stale-heartbeat events in 30 s.
* UDS non-blocking single-fd loses more than on the Readme box: 1M/s offered loses 41-45% (Readme: 8-10%), and busy-poll is no better (45-54%). Delivery tops out around 0.5-0.8M/s. I did not investigate why.
* `recvmmsg` (`-M 64`): delivery up to ~1.2-1.8M/s, loss at 1.5M/s offered 3-9.5% (vs 35% for `recvmsg`), but the 4 client threads become the limit.
* Throughput of the sender side: unlike the 2-vCPU box, 4 client threads on separate CPUs can offer ~1.4-1.7M/s, so server-bound capacity is measurable.

## Caveats
* Single run of each configuration, 3 x 3 s per point; ranges are min-max and some points are very noisy (UDP 1432 B at 30M/s: 5.9-38% loss).
* The 4 MiB `SO_RCVBUF` test is capped by `rmem_max` (as on the 2-vCPU box), so it only tested ~2x slack, not 17 ms.
* No root: `sysctl` experiments (e.g. `max_dgram_qlen`) not possible.
* Reuseport runs pin server threads to CPUs 0-3, as in the original; the client starts at CPU 4.
* `fill_udp_*.txt` are every 50th sample of `-q 1 -o` series (columns `t_ns rmem`). I did not tabulate depth percentiles.
* `debug/analyze.py`/`gaps.py` were not run: the idle-server drop bursts they analyze did not occur here.
* Only `run_all.sh`'s own `sweep.sh` was used, so the server CPU is 2 and clients start on 4 throughout.
