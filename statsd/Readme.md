statsd over loopback: what the buffers actually do
===

Scratchpad backing the statsd post. Everything here is loopback, on one VM, with a server that does (almost) nothing.

```
./build.sh
./probe                                   # SO_REUSEPORT / bind behaviour per family
./sweep.sh "<server args>" "<client args>" rate...   # paced sweep, 3 trials per rate
./run_all.sh                              # the main matrix -> results/*.txt
./agg -H 16                               # tag normalization (section 6)
```

* `server.c`: recvmsg/recvmmsg loop; `-m` counts newlines; `-w` wait time via `SO_TIMESTAMPNS`; `-q k` samples `SO_MEMINFO`; drops from `SK_MEMINFO_DROPS`; `-R` reuseport group (UDP) or N paths (UDS); `-B` CBPF steer-by-cpu; `-T` threads per socket; `-S` stall.
* `client.c`: connected socket, ~60 B lines from a prebuilt pool, `-b` packs to N bytes, `-r` paces (busy-wait), `-c` threads (one socket/flow each), `-N` non-blocking (EAGAIN counted as loss).
* `sweep.sh`: server on cpu 2, clients from cpu 4 (siblings are 0-1, 2-3, 4-5, 6-7).
* `server -P`: busy-poll. `server -q 1 -o f`: per-recv series `t_ns rmem drops last_wait_ns thread_cpu_ns`, plus the server prints thread 0's schedstat (on-CPU, run-queue wait, slices). `client -L f`: pacer-late events.
* `jitter.c`: no-socket stall detector (`-m spin|sleep -C cpu -L n` background spinners). `debug/`: the gap-attribution scripts used in 3b.

Local config
---
i7-11800H, 8 vCPU (4c/8t) under WSL2, kernel 6.18.58.1-microsoft-standard-WSL2, gcc 11.4 `-O2`.
`rmem_default = wmem_default = 212992`, `rmem_max = 4194304`, `net.unix.max_dgram_qlen = 10`, `lo` MTU 65536.
Kernel source citations are upstream v6.18.

1. Wire format
---
`name:value|type|#tag:v,tag:v\n`. The generator produces lines like
`web.request.count:1|c|#env:prod,host:web-07,route:/v1/users`, mean 60.1 B with the newline.

2. A server that does nothing
---
One metric per `send()`, server `recvmsg` and discard. Two server variants: **blocking** (the natural server) and **busy-poll** (`-P`, `MSG_DONTWAIT` spin). They differ only in whether the server sleeps between datagrams, and that turns out to matter a lot below capacity (section 3b).

| rate (metrics/s) | loss, blocking (3 trials) | loss, busy-poll (3 trials) | p99 wait blocking / busy-poll |
|---|---|---|---|
| 250k | 1.6%, 0.01%, 0.01% | 2.95%*, 0.09%, 0 | 30-110 / 5-10 µs |
| 500k | 0.00-1.7% | 0.00-0.04% | 20-40 / 6-8 µs |
| 1M | 0-3.7% | 0.01-0.05% | 25-40 / 6-10 µs |
| 1.5M | 0.07-1.5% | 0.05-0.1% | ~100-120 / ~100-110 µs |
| 2M | 7-21% | 11-14% | ~130 µs |
| flat out (~2.2M offered) | 18-25%; delivers **~1.6-1.7M/s** | 21-25%; delivers ~1.72-1.74M/s | ~140 µs |

\* One outlier: a single multi-ms vCPU stall (3b).

Server capacity is ~1.7M datagrams/s either way. Below capacity, the blocking server's losses are scheduling stalls, not throughput limits. With busy-poll, the knee is clean: ≤0.1% loss through 1.5M/s.

Counting newlines (`-m`) changes nothing measurable here. memchr is ~1 ns/line and the cost is in the syscalls.

A single client thread tops out at ~410k sends/s (~2.4 µs per `send`). On loopback the sender's syscall runs the whole IP/UDP receive path *and* pays for waking the reader, so the client is as much the bottleneck as the server.

3. Watching it fill
---
**Capacity, measured.** Stall the server (`-S 1500`), fire non-blocking, count what survives:

| payload/datagram | datagrams that fit in 208 KiB | metrics that fit | rmem per datagram |
|---|---|---|---|
| 60 B (1 metric) | 221 | 221 | ~960 B (16x the payload) |
| 1432 B | 92 | ~2,100 | ~2.3 KiB |
| 8192 B | 13 | ~1,800 | ~16 KiB (rounds up to a 16 KiB alloc) |
| UDS, any size | **11** | 11 / 253 / 1,496 | (not charged to receiver) |

The UDP check is `rmem + size > rcvbuf` against truesize (`net/ipv4/udp.c:1723`), so the buffer holds 221 metrics, not 3,500. **Buffer in units of time = capacity / rate**: at 250k metrics/s that is 0.9 ms of slack. Any receiver stall longer than that drops metrics, even at a seventh of capacity (3b).

**Depth over time** (`-q 1` samples `SO_MEMINFO` every recv; series in `results/fill_udp_*.txt`, every 50th sample, columns `t_ns rmem` from before the series gained more columns):

| offered | queue empty | ≥90% full | p50 depth | p99 depth |
|---|---|---|---|---|
| 500k | 10% | 0.2% | ~8 dgrams | ~79 |
| 1M | 4% | 0.1% | ~10 | ~148 |
| 1.5M | 0% | 43% | ~194 | 221 (full) |
| 2M | 0% | 59% | ~207 | 221 (full) |

The queue is bimodal. Under capacity it is nearly empty with a hiccup tail. Over capacity it is pinned full and every metric waits about a full buffer's worth. (The getsockopt per recv slows the server, so the ceiling is ~1.25M in this run; that's an observer effect.)

**Wait time = full buffer / drain rate.** p99 wait at overload: UDP 1/dgram 221 / 1.67M/s ≈ 130 µs, measured 131-147 µs. UDP 8192: 13 / ~0.95M dgrams/s ≈ 14 µs, measured ~22 µs. UDS: 11 / 0.93M ≈ 12 µs, measured ~20 µs. So a deeper queue doesn't add throughput; it adds latency.

### 3b. Why a mostly idle server drops: it sleeps, and then it doesn't get the CPU back

The blocking server lost 0.01-3.7% at 250k-1M/s, sometimes *more* at 250k than at 1M. That looked like noise. It isn't noise and it isn't a counting error; it was tracked down as follows (tools: `jitter.c`, `debug/`).

1. **The drops are real kernel drops.** `SK_MEMINFO_DROPS` equals `/proc/net/snmp Udp: RcvbufErrors` (5031+197 = 5228). The per-recv series (`-q 1 -o`, columns `t rmem drops wait thread_cpu`) agrees with the final counter in every trial.
2. **Every drop burst follows a server gap.** That's a 1-34 ms stretch with no recv returning, during which rmem hits the 212160 cap, and the first datagram after it has waited the whole gap. Drops ≈ gap × rate − 221: a 4.16 ms gap gives 1040 − 221 ≈ 819 predicted vs 829 counted, and gaps of 22.3 + 6.5 ms give ≈6760 vs 6778.
3. **The client isn't the cause.** Several drop events had zero pacer-late sends in the preceding 5 ms.
4. **The server was off-CPU during the gaps.** Thread CPU time across a drop gap was ~0 µs, so this was not slow code. Drop trials showed 12-169 ms of run-queue wait (`/proc/thread-self/schedstat`), vs ~2 ms in clean trials. Some gaps had ~0 CPU *and* little run-queue wait. That fits a sleeping thread whose wakeup IPI wasn't serviced because vCPU 2 itself wasn't running (remote wakeups are enqueued only when the target CPU handles the IPI).
5. **It reproduces with no sockets.** `jitter -m spin -C 2`: one 33 ms gap in 5 s, all accounted for as run-queue wait. `jitter -m sleep` (100 µs naps, the "idle server" case): 2 wakeups >1 ms late with an idle box, **36 (57 ms total) with 4 busy-spinning threads on other vCPUs**, which is what the paced client does. A watcher catching CPU 2 stalls found the user's `nvim` and a Bun `HeapHelper` thread (this Claude Code session) running there. In other stalls (2-146 ms) nothing else in the guest was runnable on CPU 2, which points at Hyper-V descheduling the vCPU.
6. **Fixes, 5 trials each at 250k/s:**

| variant | drops per trial |
|---|---|
| baseline (blocking server, 4 spinning clients) | 25700, 10532, 3176, 15925, 9174 |
| 1 spinning client | 95257, 5783, 6896, 0, 10 |
| blocking server, `SO_RCVBUF` 4 MiB (~17 ms of slack) | **0, 0, 0, 0, 0** |
| busy-poll server | **0, 0, 0, 0, 0** (run-queue wait ~0.1 ms) |

Root cause: at low rates the receiver spends most of its time asleep in `recvmsg`. While it sleeps, the guest scheduler sees CPU 2 as idle and places other work there, and the vCPU halts and can be descheduled by the host. When data arrives, the server waits for the CPU, sometimes for longer than the buffer covers. A spinning server never gives up the CPU, so neither happens. A bigger buffer outlasts the stalls. Reducing pacer load alone doesn't fix it.

For the post: **a lightly loaded agent is more exposed than a busy one**, because the buffer is measured in milliseconds at the offered rate, and an idle receiver is the one that loses its CPU. The bare-metal analogue is anything that takes the agent off-CPU for a few ms: CFS quota throttling in a container (a 100 ms period!), GC pauses, a noisy neighbour on the same core. This environment exaggerates it (desktop VM, interactive processes sharing the CPU). The mechanism is general.

Measurement notes:
* `SIOCINQ` on UDP returns the size of the *next* datagram, not the queue. `SO_MEMINFO[SK_MEMINFO_RMEM_ALLOC]` is the right counter.
* `SO_TIMESTAMPNS` on UDP/loopback is stamped in `netif_rx` before the socket enqueue (softirq work is included). On UDS it is stamped at enqueue (`af_unix.c:2303`), and only if the receiver enabled it.

4. Batching
---
Same metrics, packed. Server counts newlines. Clients: 3 threads.

| config | blocking server: ≤~1% loss | busy-poll server: loss at the knee | flat-out delivered | datagrams/s at flat out |
|---|---|---|---|---|
| UDP 1 metric/dgram | ~1-1.5M/s (noisy, 3b) | **1.5M/s at ≤0.1%** | ~1.7M/s | ~1.7M |
| UDP 1432 B (~23 metrics) | ~20M/s | **20M/s at ≤0.08%**; 30M: ~2% | ~27-32M/s | ~1.3-1.5M |
| UDP 8192 B (~136 metrics) | ~40-80M/s | **40M/s at ≤0.09%; 80M/s at ~0.8%** | ~82-92M/s | ~0.9-1.2M |

(`results/poll_*.txt` are the busy-poll sweeps; the other files are blocking.)

Datagrams/s stays roughly constant (~1-1.5M) across sizes, so throughput scales with metrics per datagram: the win is syscalls and skbs, not bytes. 8192 is ~50x per-metric; 1432 is ~20x.

No fragmentation: `/proc/net/snmp` `Ip: FragCreates` and `ReasmReqds` stay 0 across 8192-byte and 60000-byte runs, because the loopback MTU is 65536. The 1432 folklore is about real NICs. Note that 1432 holds *more* metrics in the default buffer than 8192 does (2,100 vs 1,800), because of allocation rounding.

**What batching doesn't touch.** A real agent parses. `agg parse` (split name/value/type/tags) costs ~40 ns/line, so one core parses ~25M lines/s. Batched 8192 delivers 40-90M/s, so once you batch, parsing rather than syscalls is the bottleneck. The newline-counting server hides this because memchr is ~1 ns/line.

`recvmmsg` is the server-side mirror: `-M 64` averaged **28 datagrams per call** at flat out, loss went from 13.7% to 2.2%, and delivery reached ~1.9-2.1M/s. At that point the 4 client threads are the limit.

5. UDS is the same thing (it isn't)
---
Source (v6.18 `net/unix/af_unix.c`):
* The queue is capped by **count**: `unix_recvq_full_lockless` is `skb_queue_len > sk_max_ack_backlog` (`:288-291`). That limit is copied from `net.unix.max_dgram_qlen` when the socket is created (`:1122`), with default 10 (`:3832`). With `>`, you get **11** datagrams of any size. Changing the sysctl doesn't affect existing sockets.
* The skb is charged to the **sender** (`sock_alloc_send_pskb`, `:2153`), so the receiver's `rmem_alloc` stays 0 (every UDS row shows `rmem_pk 0`). The sender's `sndbuf` (wmem_default) is a second byte limit, and a datagram larger than `sndbuf - 32` is `EMSGSIZE` (`:2139`).
* A full queue **blocks** a blocking sender (`unix_wait_for_peer`, `:2272`) and returns **EAGAIN** to a non-blocking one (`:2289`). There is no drop counter on the receiver: `SK_MEMINFO_DROPS` stays 0, and the loss shows up only as EAGAIN at the client.
* The limit is skipped if the receiver is connected back to the sender (`unix_peer(other) != sk`, `:2270`). That's an edge case, but it exists.
* Waiters are exclusive (`prepare_to_wait_exclusive`, `:1630`), and each dequeue wakes one sender (`:2625`), so it isn't a thundering herd.

Measured (1 metric/dgram unless noted):

| config | result |
|---|---|
| non-blocking, 4 clients, blocking server | ~1% EAGAIN loss already at 250-500k/s; 8-10% at 1M; flat out delivers ~0.9M/s with 54% loss |
| same, busy-poll server | 0.03-0.13% at 250-500k; 0.6-1.1% at 1M; 17-21% at 1.5M; flat out ~0.9M/s with 54% loss |
| non-blocking + recvmmsg (blocking) | no better below 1M (the queue is still 11 deep); flat out ~1.3M/s |
| non-blocking, 8192 B | blocking: ~40M/s at 0.4%; busy-poll: 40M/s at ≤0.18%; 80M → 12-14% either way |
| blocking, 1 client | ~1.0M/s, no loss (the sender stalls instead); 13.6k voluntary context switches per 1M sends |
| **blocking, 4 clients** | **~85k/s**. 974k voluntary context switches per 1M sends: nearly every send sleeps and is woken |
| blocking 8192 B, 1 vs 3 clients | ~50M/s vs ~9.3M/s |

So yes, batching matters more for UDS. An 11-slot queue holds 11 metrics unbatched and ~1,500 at 8 KiB. In time units, 11 slots at 250k/s is **44 µs** of slack (vs 0.9 ms for UDP), so UDS is far more exposed to the receiver stalls in 3b. Even a busy-polling server loses 0.03-0.13% in *every* trial at 250-500k, where busy-poll UDP mostly loses 0-0.09% (one stall outlier aside). And "blocks instead of drops" means a stalled agent stalls the application, while contended blocking senders convoy on wakeups. That's the lie reveal: UDS is cheaper per datagram than UDP (1 client: ~1M/s vs ~0.4M/s) right up until the queue fills.

Not done (no root here): rerunning with `sysctl -w net.unix.max_dgram_qlen=512`.

6. Tag normalization
---
`agg`: 4 names × H hosts × 4 routes × 3 AZs, 4 tags per line in **random order**, ~72 B/line, 10M lines, flush every 100k lines. Counts sum, gauges keep the last value, histograms pass through (25% of lines). Keys are 64-bit hashes with no equality check, which is fine for a sketch.

| H (contexts) | key | ns/line | count+gauge lines per flush, in → out |
|---|---|---|---|
| 16 (768) | parse only | ~40 | |
| | raw tag string | ~89 | 75k → 13.8k (5.4x) |
| | sort tags, then hash | ~107 | 75k → **576 (130x)** |
| | sum of per-tag hashes | ~76 | 75k → 576 (130x) |
| 256 (12k) | raw / sort / sum | 100 / 112 / 82 | 1.2x / 8.1x / 8.1x |
| 4096 (196k) | raw / sort / sum | 98 / 118 / 87 | 1.0x / 2.3x / 2.3x |

* The collapse is the point. Without normalization, 4 tags in random order make up to 24 distinct keys per context, and the aggregation mostly evaporates.
* The cost is noise. Sort adds ~15-20 ns over raw; the order-independent sum is *cheaper* than raw, because the table is smaller and has fewer misses. Either is ~20-30x cheaper than the ~2.4 µs `send()` it saves.
* The collapse depends on cardinality per flush window. At 196k contexts and 75k lines per flush, it's only 2.3x.

7. SO_REUSEPORT
---
`./probe` on 6.18:
```
AF_INET/SOCK_DGRAM, reuseport=1:  bind #1 ok, bind #2 ok
AF_UNIX/SOCK_DGRAM, reuseport=0:  bind #2: Address already in use
AF_UNIX/SOCK_DGRAM, reuseport=1:  setsockopt(SO_REUSEPORT): Operation not supported; bind #2: Address already in use
```
`net/core/sock.c:1317`: `if (valbool && !sk_is_inet(sk)) ret = -EOPNOTSUPP;`, added by commit 5b0af621c3f6 "net: restrict SO_REUSEPORT to inet sockets" (merged for 6.13-rc6, Jan 2025; absent in v6.8-v6.10). Before that, `AF_UNIX` accepted it as a silent no-op. Stable backports weren't checked.

UDP, 4 reuseport sockets, per-socket datagram counts:

| flows | steering | per-socket split |
|---|---|---|
| 1 | hash | everything on one socket (3/3 trials) |
| 4 | hash | `500k,500k,0,1M` · `500k,1M,500k,0` · `0,500k,500k,1M` · `0,500k,0,1.5M` · `0,1.5M,0,500k` |
| 8 | hash | `1M,0,500k,500k` (still lumpy) |
| 4 (on cpus 4-7) | CBPF `cpu % 4` | `500k,496k,490k,496k` (even) |

With the hash, at least one socket sat idle in every 4-flow trial. On loopback, the "cpu the packet arrived on" is the sender's cpu, so CBPF steer-by-cpu means steer-by-sender-cpu.

Throughput: 4 reuseport sockets cut loss to ~0.1% (4x buffer, 4 readers), but the 4 senders then top out at ~1.2M/s (vs ~2M/s offered against one socket). On loopback the sender pays for waking the extra readers. CBPF ran at ~1% loss and ~1.1M/s.

UDS workarounds (4 clients, non-blocking unless noted):

| setup | 1M/s offered | flat out |
|---|---|---|
| 1 path, 1 thread | 8-10% loss | 0.93M/s |
| 1 path, 4 threads on one fd | **21% loss (worse)** | 1.08M/s |
| 4 paths, client i → path i%4 | 0.9% loss | **3.9M/s** (20% loss) |
| 4 paths, blocking | | 3.5-3.8M/s, no loss (vs 85k/s for 1 path) |

More readers on one fd don't help, because the queue is still 11 deep behind one lock. Doing reuseport by hand with N paths multiplies both the queue and the readers.

Results summary (orders of magnitude, this box, ≤~1% loss)
---

| | metrics/s | peak queue | p99 wait at overload |
|---|---|---|---|
| UDP, 1/dgram | 10^6 (busy-poll: 1.5M at ≤0.1%) | 221 metrics (208 KiB) | ~130 µs |
| UDP, 1/dgram, recvmmsg | 10^6 (client-bound ~2M) | 221 | ~100 µs |
| UDP, 1432 B | 10^7 | ~2.1k metrics | ~70 µs |
| UDP, 8192 B | 10^7.5-10^8 | ~1.8k metrics | ~22 µs |
| UDS, 1/dgram, non-blocking | 10^5.5 (busy-poll: 500k at ~0.1%) | 11 metrics | ~20 µs |
| UDS, 1/dgram, blocking, 4 senders | 10^5 (no loss, senders stall) | 11 | ~230-475 µs |
| UDS, 8192 B, non-blocking | 10^7.5 | ~1.5k metrics | ~35 µs |
| UDS, 4 paths | 10^6 (flat out 4M) | 4 × 11 | ~17 µs |
| agent parse, 1 core | ~2.5 × 10^7 | | |

Caveats
---
* WSL2/Hyper-V guest. Wakeups and IPIs cost VM exits, which magnifies anything wakeup-bound: single-client UDP, UDS blocking convoys, and the reuseport sender cap. Expect bare metal to move these numbers, maybe a lot, though not the order of magnitude between batched and unbatched.
* Low-rate loss with a blocking server is real and explained (3b): the idle receiver loses its CPU for 1-150 ms, both to other guest tasks placed on its "idle" CPU and to Hyper-V descheduling the halted vCPU. Busy-poll or a 4 MiB buffer removes it. Knees quoted from busy-poll sweeps are capacity; blocking-server numbers below capacity are scheduling-dominated, and trials disagree by up to a few %.
* Not explained: busy-poll runs still show occasional single outliers (2.95% at 250k in one trial). Spinning vCPUs can be preempted too (`jitter -m spin` saw a 33 ms gap), so these are probably the same host/guest stalls, but they weren't individually attributed.
* The paced client busy-spins one vCPU per thread. That measurably worsens idle-vCPU wake latency (3b, step 5). A sleeping pacer would be kinder but adds its own wake jitter.
* Loopback only. The sender runs the receive path, so "client-bound" and "server-bound" blur.
* The client memcpys prebuilt lines. A real client formats (snprintf, ~100+ ns), which would make single-thread batched client rates much lower than shown.
* Single host, default buffers. Raising `SO_RCVBUF` (up to `rmem_max` = 4 MiB) buys time, not throughput, and adds wait.
* Server CPUs: sweeps pin the server to cpu 2. Reuseport runs pin server threads to 0-3, which share a core with whatever lands on cpu 0.
