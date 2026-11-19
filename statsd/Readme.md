statsd
===

Many of us rely on statsd as a standard, uncelebrated, invisible piece of kit.

Let's take a peek behind the curtain.


## Usage
There are a couple of tools here:
```
./build.sh
./probe                                   # SO_REUSEPORT / bind behaviour per family
./sweep.sh "<server args>" "<client args>" rate...   # paced sweep, 3 trials per rate
./run_all.sh                              # the main matrix -> results/*.txt
./agg -H 16                               # tag normalization (section 6)
```

* `server.c` is the receiver. It sits in a `recvmsg()` loop (or `recvmmsg()` with `-M`) and mostly throws the data away. The flags make it do a little more work, or collect more evidence:
  * `-m` counts newlines, so we know how many metrics arrived and not just how many datagrams.
  * `-w` measures how long each datagram waited in the queue, using `SO_TIMESTAMPNS`.
  * `-q k` samples the socket's memory use (`SO_MEMINFO`) every k receives. Drop counts come from `SK_MEMINFO_DROPS`.
  * `-P` spins on `recvmsg(MSG_DONTWAIT)` instead of blocking. The tables call this "busy-poll"; it has nothing to do with `poll(2)`.
  * `-R n` opens n sockets: a `SO_REUSEPORT` group for UDP, or n separate paths for UDS. `-B` spreads UDP packets across that group by CPU, using a classic BPF program. `-T` sets the number of threads per socket.
  * `-S ms` stalls before reading, so we can watch the buffer fill up.
  * `-q 1 -o file` writes one line per receive (`t_ns rmem drops last_wait_ns thread_cpu_ns`). The server also prints thread 0's scheduler stats when it exits: time on CPU, time spent waiting to run, and number of slices.
* `client.c` is the sender. It uses a connected socket and sends ~60-byte lines from a prebuilt pool.
  * `-b N` packs lines into datagrams of up to N bytes.
  * `-r` paces sends to a target rate by busy-waiting.
  * `-c` runs several threads, each with its own socket.
  * `-N` makes sends non-blocking. Every `EAGAIN` counts as a lost metric.
  * `-L file` logs every time the pacer fell behind schedule.
* `sweep.sh` runs the server on CPU 2 and the clients from CPU 4 up. Hyperthread siblings on this box are 0-1, 2-3, 4-5 and 6-7, so the server and clients never share a physical core.
* `jitter.c` measures scheduling stalls with no sockets involved at all. `-m spin` or `-m sleep` picks what it does, `-C` picks the CPU, and `-L n` adds n busy-spinning threads in the background.
* `debug/` has the scripts used to track down the gaps in section 3b.

Local config
---
Everything ran on a laptop: an i7-11800H with 8 vCPUs (4 cores, 8 threads) under WSL2, kernel 6.18.58.1-microsoft-standard-WSL2, built with gcc 11.4 at `-O2`.

The network defaults were left alone: `rmem_default = wmem_default = 212992`, `rmem_max = 4194304`, `net.unix.max_dgram_qlen = 10`, and the loopback MTU is 65536.

Wherever kernel source is cited, it's upstream v6.18.

1. Wire format
---
A statsd metric is one line of text: `name:value|type|#tag:v,tag:v\n`. The generator produces lines like
`web.request.count:1|c|#env:prod,host:web-07,route:/v1/users`, which average 60.1 bytes including the newline.

2. statsd load generation
---
For this example, we're going to focus on the single-client case.
This isn't quite accurate to reality for a few reasons (e.g., kubernetes), but maybe I'll tackle that in a separate experiment.
As such, we won't get into `select()`/`poll()`/`epoll()`.

1. emit just one metric per `send()` at a time
2. server will `recvmsg()` the message, then just throw it away
3. the server can either be blocking (sit on `recvmsg()`) or a hot poll (use `MSG_DONTWAIT`)

"Rate" is what the client tried to send. "Loss" is metrics that were sent but never received. "p99 wait" is how long a datagram sat in the socket queue before the server picked it up.

| rate (metrics/s) | loss, blocking (3 trials) | loss, busy-poll (3 trials) | p99 wait blocking / busy-poll |
|---|---|---|---|
| 250k | 1.6%, 0.01%, 0.01% | 2.95%*, 0.09%, 0 | 30-110 / 5-10 µs |
| 500k | 0.00-1.7% | 0.00-0.04% | 20-40 / 6-8 µs |
| 1M | 0-3.7% | 0.01-0.05% | 25-40 / 6-10 µs |
| 1.5M | 0.07-1.5% | 0.05-0.1% | ~100-120 / ~100-110 µs |
| 2M | 7-21% | 11-14% | ~130 µs |
| flat out (~2.2M offered) | 18-25%; delivers **~1.6-1.7M/s** | 21-25%; delivers ~1.72-1.74M/s | ~140 µs |

\* One outlier, caused by a single multi-millisecond vCPU stall (see 3b).

What this tells us:
* The server tops out at around 1.7 million datagrams per second, whether it blocks or spins. That's its real capacity.
* The spinning server behaves the way you'd hope: 0.1% loss or less all the way up to 1.5M/s, then a clean knee.
* The blocking server doesn't, and it shouldn't be losing anything. At 250k/s it's running at about a seventh of its capacity, yet one trial lost 1.6% while the next two lost 0.01%. At 1M/s it lost anywhere from 0 to 3.7%. Loss that is worse at a *lower* rate, and changes from run to run, doesn't look like a server that's too slow.

That last point was a puzzle, and section 3b is the story of chasing it down. Spoiler: it turned out to be a quirk of benchmarking inside WSL2, not something a statsd server on real hardware should see. Keep that in mind when reading the blocking-server numbers in this document.

Counting newlines (`-m`) made no measurable difference. `memchr` costs about 1 ns per line, and the real time goes into syscalls.

The client is a bottleneck too. A single client thread can only call `send()` about 410k times a second (~2.4 µs each). On loopback there's no NIC in the middle: the sender's `send()` runs the whole IP/UDP receive path itself, *and* pays to wake up the reader. So the client is as much of a bottleneck as the server.

3. Watching it fill
---
**How much does the buffer actually hold?** To find out, stall the server (`-S 1500`), have the client fire non-blocking sends at it, and count what survives once the server starts reading again.

| payload/datagram | datagrams that fit in 208 KiB | metrics that fit | rmem per datagram |
|---|---|---|---|
| 60 B (1 metric) | 221 | 221 | ~960 B (16x the payload) |
| 1432 B | 92 | ~2,100 | ~2.3 KiB |
| 8192 B | 13 | ~1,800 | ~16 KiB (rounds up to a 16 KiB alloc) |
| UDS, any size | **11** | 11 / 253 / 1,496 | (not charged to receiver) |

The surprise is the first row. The default buffer is 208 KiB, which naively fits about 3,500 sixty-byte metrics. It actually fits 221. That's because the kernel doesn't charge the socket for your payload; it charges for the whole kernel buffer (the skb's "truesize"), which for a tiny datagram is about 16x the payload. The check is `rmem + size > rcvbuf` (`net/ipv4/udp.c:1723`).

UDS behaves completely differently, and gets its own section (5).

A more useful way to think about the buffer is in units of time: **capacity / rate**. At 250k metrics/s, 221 metrics is 0.9 ms of slack. If the receiver stops reading for longer than that, metrics get dropped, even though it's running at a seventh of its capacity. Keep this number in mind; it's the key to the puzzle in 3b.

**How full does it get in practice?** Next, watch the queue depth while the server is running (`-q 1` samples `SO_MEMINFO` on every receive). The series are in `results/fill_udp_*.txt`, keeping every 50th sample. Those files only have `t_ns rmem` columns, because they were recorded before the other columns were added.

| offered | queue empty | ≥90% full | p50 depth | p99 depth |
|---|---|---|---|---|
| 500k | 10% | 0.2% | ~8 dgrams | ~79 |
| 1M | 4% | 0.1% | ~10 | ~148 |
| 1.5M | 0% | 43% | ~194 | 221 (full) |
| 2M | 0% | 59% | ~207 | 221 (full) |

The queue is basically in one of two states. Below capacity it's nearly empty, with the occasional spike. Above capacity it's pinned full, and every metric waits about as long as it takes to drain a full buffer.

One caveat: calling `getsockopt` on every receive slows the server down, so in this run the ceiling was ~1.25M/s rather than ~1.7M/s. Measuring changed the thing being measured.

**How long do metrics wait?** Under overload, that gives a simple rule: **wait ≈ full buffer / drain rate**. It holds up well:
* UDP, 1 metric per datagram: 221 / 1.67M/s ≈ 130 µs predicted, 131-147 µs measured.
* UDP, 8192 B: 13 / ~0.95M datagrams/s ≈ 14 µs predicted, ~22 µs measured.
* UDS: 11 / 0.93M ≈ 12 µs predicted, ~20 µs measured.

So a deeper queue doesn't make the server any faster. It just makes metrics wait longer.

### 3b. A puzzle: why does an idle server drop metrics?

Here's the problem. A server that can handle 1.7M datagrams/s should lose nothing at 250k/s. It has nothing to do 85% of the time, and a 221-slot buffer to absorb hiccups. Yet the blocking server in section 2 lost 0.01-3.7% at 250k-1M/s, sometimes lost *more* at 250k than at 1M, and gave different answers on every run. The spinning server, doing the same work, was clean.

The easy move is to call that noise, average a few more trials, and move on. But "noise" isn't an explanation, and the thing a statsd user actually cares about is whether their idle-ish agent is quietly throwing away metrics. So we went looking. Each step below is a suspect we checked and either ruled out or kept, and the scripts for each are in `debug/` and `jitter.c`.

**Suspect 1: we're counting wrong.** The client counts what it sent, the server counts what it received, and the difference is "loss". Maybe that arithmetic is off: a send that silently failed, a race at shutdown, an off-by-one. To check, we compared three independent counts: the socket's own drop counter (`SK_MEMINFO_DROPS`), the system-wide `Udp: RcvbufErrors` in `/proc/net/snmp`, and a per-receive log from the server (`-q 1 -o`) that records the drop counter on every `recvmsg()`. They all agreed (for example, `SK_MEMINFO_DROPS` and `RcvbufErrors` both came to 5031 + 197 = 5228), in every trial. **Ruled out: the kernel really is dropping these datagrams because the receive buffer was full.**

**Clue: the drops come in bursts, and each burst follows a gap.** That same per-receive log shows *when* drops happen, and they aren't spread evenly. They arrive in bursts, and every burst comes right after a gap: a 1-34 ms stretch where no `recvmsg()` returns at all. During the gap the buffer fills to its 212160-byte cap, and the first datagram after the gap has been waiting the whole time. The numbers line up almost exactly with "the buffer filled, and everything after that was dropped": drops ≈ gap × rate − 221. A 4.16 ms gap predicts 1040 − 221 ≈ 819 drops, and 829 were counted. Two gaps of 22.3 and 6.5 ms predict ≈6760, and 6778 were counted. So the question changes from "why are we dropping?" to "why does the server stop reading for milliseconds at a time?"

**Suspect 2: the client is bursty.** The client paces itself by busy-waiting. If it falls behind and then catches up in a rush, the server would see a burst well above the average rate, and that could overflow the buffer. The client already logs every time it falls behind schedule (`-L`), so `debug/analyze.py` lines up each drop burst with the client's late events in the 5 ms before it. Several drop bursts had *no* late sends at all before them. **Ruled out: the client was sending steadily; the server just wasn't reading.**

**Suspect 3: the server is doing something slow.** A page fault, a slow syscall, some rare path in the code. If so, the server thread would be burning CPU during the gap. The per-receive log records the thread's own CPU time (`CLOCK_THREAD_CPUTIME_ID`), and `debug/gaps.py` checks how much of it was used during each gap. The answer was about 0 µs. **Ruled out: the server wasn't running slow code during the gaps. It wasn't running at all.**

**Suspect 4: the scheduler.** If the thread wasn't running, either it was waiting for a CPU, or it was asleep and nobody woke it. Linux tracks the first in `/proc/thread-self/schedstat`: time on CPU, and time spent runnable but waiting in the run queue. The server prints that at exit. Trials with drops spent 12-169 ms waiting in the run queue, compared with ~2 ms for clean trials. That's most of the answer: the server was ready to run, and didn't get a CPU.

But not all of it. Some gaps had almost no CPU time *and* little run-queue wait. A thread that is neither running nor waiting to run is asleep, so in those gaps the wakeup itself was late. When a datagram arrives on another CPU and needs to wake the server on CPU 2, the kernel sends CPU 2 an interrupt (an IPI), and the thread only joins the run queue once CPU 2 handles it. If CPU 2 isn't running at all, that wakeup sits there. That's not something a normal machine does, but a virtual machine can: vCPU 2 is just a thread on the Windows host, and Hyper-V can take it off the physical CPU.

**Confirming it without sockets.** If this is really the scheduler, sockets shouldn't matter. `jitter.c` takes them out of the picture:
* `jitter -m spin -C 2` pins one thread to CPU 2 and reads the clock in a tight loop. Any jump in the clock is time it didn't run. It saw one 33 ms gap in 5 s, all of it run-queue wait.
* `jitter -m sleep` mimics the idle server by napping 100 µs at a time and measuring how late each wakeup is. On an otherwise idle box, 2 wakeups were more than 1 ms late. With 4 busy-spinning threads on other vCPUs (`-L 4`, which is what the paced client looks like), **36 were late, adding up to 57 ms**.

So the stalls exist with no networking at all, and they get worse when other vCPUs are busy.

**Who's on CPU 2?** The last question is what takes CPU 2 away. `debug/who.c` keeps a heartbeat thread spinning on CPU 2, and a watcher on CPU 0 that, whenever the heartbeat goes stale, lists every runnable task whose last CPU was 2. Some stalls caught `nvim` and a Bun `HeapHelper` thread (from the Claude Code session running these experiments). The guest scheduler saw CPU 2 as idle while the server slept, and put other work there. In other stalls (2-146 ms), nothing else in the guest was runnable on CPU 2 at all, which leaves Hyper-V descheduling the vCPU as the explanation.

**Testing the explanation.** If that's right, two things should fix it: a server that never sleeps (so it never gives up CPU 2), or a buffer big enough to outlast the stalls. And making the client lighter should help less, since the stalls happen even on an idle box. 5 trials each at 250k/s:

| variant | drops per trial |
|---|---|
| baseline (blocking server, 4 spinning clients) | 25700, 10532, 3176, 15925, 9174 |
| 1 spinning client | 95257, 5783, 6896, 0, 10 |
| blocking server, `SO_RCVBUF` 4 MiB (~17 ms of slack) | **0, 0, 0, 0, 0** |
| busy-poll server | **0, 0, 0, 0, 0** (run-queue wait ~0.1 ms) |

Both fixes work, and a lighter client alone doesn't.

**The answer.** At low rates, the blocking server spends most of its time asleep in `recvmsg()`. While it sleeps, the guest scheduler treats CPU 2 as free and runs other work there, and the idle vCPU halts, which lets Hyper-V take it away entirely. When data arrives, the server has to wait to get a CPU back, sometimes for longer than its 0.9 ms of buffer. A spinning server never lets go of its CPU, so neither thing happens.

**This is a property of the test box, not of statsd.** It's a desktop VM, sharing its CPUs with an editor, a coding agent and Windows itself, with a hypervisor that's free to deschedule idle vCPUs. On a real server with a quiet core, a blocking receiver at a seventh of its capacity should lose nothing, and the blocking and busy-poll columns in section 2 should look alike below the knee. I'll confirm that on real hardware in a follow-up. Until then, treat the blocking-server loss figures below capacity in this document as WSL2 artifacts. The capacity figures (from the busy-poll runs) don't have this problem.

That said, the underlying lesson does carry over. The buffer only covers a few milliseconds at the offered rate, and anything that takes the agent off its CPU for longer than that will drop metrics, however lightly loaded it is. On real hardware, the usual suspects are CFS quota throttling in a container (with a 100 ms period!), GC pauses, or a noisy neighbour on the same core. WSL2 just makes those stalls happen all the time.

A couple of measurement gotchas:
* `SIOCINQ` on a UDP socket returns the size of the *next* datagram, not how much is queued. To get the queue size, use `SO_MEMINFO[SK_MEMINFO_RMEM_ALLOC]`.
* `SO_TIMESTAMPNS` means different things on the two families. On UDP over loopback, the timestamp is taken in `netif_rx`, before the packet reaches the socket, so softirq processing counts as wait time. On UDS it's taken when the datagram is queued (`af_unix.c:2303`), and only if the receiver turned timestamps on.

4. Batching
---
Section 2 sent one metric per datagram. Real clients usually pack several lines into each datagram, so here the same metrics are packed up to a target size. The server counts newlines, so we're counting metrics rather than datagrams, and there are 3 client threads.

| config | blocking server: ≤~1% loss | busy-poll server: loss at the knee | flat-out delivered | datagrams/s at flat out |
|---|---|---|---|---|
| UDP 1 metric/dgram | ~1-1.5M/s (noisy; WSL2 stalls, see 3b) | **1.5M/s at ≤0.1%** | ~1.7M/s | ~1.7M |
| UDP 1432 B (~23 metrics) | ~20M/s | **20M/s at ≤0.08%**; 30M: ~2% | ~27-32M/s | ~1.3-1.5M |
| UDP 8192 B (~136 metrics) | ~40-80M/s | **40M/s at ≤0.09%; 80M/s at ~0.8%** | ~82-92M/s | ~0.9-1.2M |

(`results/poll_*.txt` are the busy-poll sweeps; the other files are from the blocking server.)

The number of datagrams per second stays roughly the same (~1-1.5M) whatever their size, so metrics per second scales with how many metrics fit in each datagram. Batching wins by making fewer syscalls and allocating fewer skbs; the bytes themselves are nearly free. Per metric, 8192-byte datagrams are ~50x faster, and 1432-byte ones ~20x.

You may have heard that statsd datagrams should stay at or under 1432 bytes to avoid IP fragmentation. That advice is about real NICs with a 1500-byte MTU. Loopback's MTU is 65536, and `/proc/net/snmp` confirms nothing was fragmented: `Ip: FragCreates` and `ReasmReqds` stayed at 0, even with 8192- and 60000-byte datagrams. One fun detail: 1432-byte datagrams fit *more* metrics in the default buffer than 8192-byte ones (2,100 vs 1,800), because of how allocations round up.

**What batching doesn't fix.** A real agent has to parse every line. `agg parse` (splitting out name, value, type and tags) costs ~40 ns per line, so one core can parse ~25M lines/s. Batched 8192-byte datagrams deliver 40-90M/s, so once you batch, parsing replaces syscalls as the bottleneck. Our newline-counting server hides this, because `memchr` costs about 1 ns per line.

`recvmmsg()` is batching on the server side: one syscall receives many datagrams. With `-M 64` it averaged **28 datagrams per call** at flat out, loss dropped from 13.7% to 2.2%, and delivery reached ~1.9-2.1M/s. At that point, the 4 client threads become the limit.

5. UDS is the same thing (it isn't)
---
It's tempting to think of a Unix domain socket as UDP without the network stack. The kernel source (v6.18 `net/unix/af_unix.c`) says otherwise:
* **The queue is limited by count, not bytes.** `unix_recvq_full_lockless` checks `skb_queue_len > sk_max_ack_backlog` (`:288-291`). That limit is copied from `net.unix.max_dgram_qlen` when the socket is created (`:1122`), and it defaults to 10 (`:3832`). Because the check is `>`, the queue holds **11** datagrams, whatever their size. Changing the sysctl later doesn't affect sockets that already exist.
* **Memory is charged to the sender, not the receiver.** The skb is allocated with `sock_alloc_send_pskb` (`:2153`), so the receiver's `rmem_alloc` stays at 0 (every UDS row in the results shows `rmem_pk 0`). The sender's `sndbuf` (from `wmem_default`) is a second limit, in bytes: a datagram larger than `sndbuf - 32` fails with `EMSGSIZE` (`:2139`).
* **A full queue makes the sender wait or fail, instead of dropping.** A blocking sender goes to sleep (`unix_wait_for_peer`, `:2272`), and a non-blocking one gets `EAGAIN` (`:2289`). The receiver has no drop counter (`SK_MEMINFO_DROPS` stays 0), so the only sign of loss is `EAGAIN` at the client.
* **There's an exception to the limit.** It's skipped if the receiver is connected back to the sender (`unix_peer(other) != sk`, `:2270`). That's an edge case, but it exists.
* **No thundering herd.** Waiting senders are exclusive waiters (`prepare_to_wait_exclusive`, `:1630`), and each dequeue wakes only one of them (`:2625`).

Here's how that plays out (1 metric per datagram unless noted):

| config | result |
|---|---|
| non-blocking, 4 clients, blocking server | ~1% EAGAIN loss already at 250-500k/s; 8-10% at 1M; flat out delivers ~0.9M/s with 54% loss |
| same, busy-poll server | 0.03-0.13% at 250-500k; 0.6-1.1% at 1M; 17-21% at 1.5M; flat out ~0.9M/s with 54% loss |
| non-blocking + recvmmsg (blocking) | no better below 1M (the queue is still 11 deep); flat out ~1.3M/s |
| non-blocking, 8192 B | blocking: ~40M/s at 0.4%; busy-poll: 40M/s at ≤0.18%; 80M → 12-14% either way |
| blocking, 1 client | ~1.0M/s, no loss (the sender stalls instead); 13.6k voluntary context switches per 1M sends |
| **blocking, 4 clients** | **~85k/s**. 974k voluntary context switches per 1M sends: nearly every send sleeps and is woken |
| blocking 8192 B, 1 vs 3 clients | ~50M/s vs ~9.3M/s |

So batching matters even more for UDS. An 11-slot queue holds 11 metrics unbatched, but ~1,500 at 8 KiB. In time terms, 11 slots at 250k/s is **44 µs** of slack, compared with 0.9 ms for UDP, so UDS is far more exposed to stalls like the ones in 3b. On this box, even a busy-polling server loses 0.03-0.13% in *every* trial at 250-500k/s, where busy-poll UDP mostly loses 0-0.09% (apart from the one stall outlier). How much of that UDS loss survives on real hardware is one of the things the follow-up should answer.

"Blocks instead of drops" also cuts both ways. If the agent stalls, the application sending metrics stalls with it, and several blocking senders end up convoying on wakeups (~85k/s with 4 senders, vs ~1M/s with one). That's the catch with UDS: it's cheaper per datagram than UDP (one client: ~1M/s vs ~0.4M/s), right up until the queue fills.

Not tried, because it needs root: rerunning with `sysctl -w net.unix.max_dgram_qlen=512`.

6. Tag normalization
---
statsd agents aggregate: lines with the same name and tags get combined before being flushed upstream. That only works if "the same tags" is detected reliably, and clients don't promise to send tags in the same order. `agg` measures how much that matters.

The setup is 4 metric names × H hosts × 4 routes × 3 AZs, with 4 tags per line in **random order**, ~72 bytes per line, 10M lines, flushing every 100k lines. Counters are summed, gauges keep their last value, and histograms (25% of lines) pass through untouched. Keys are 64-bit hashes with no equality check, which is good enough for an experiment.

There are three ways to build the key: hash the raw tag string as-is, sort the tags and then hash, or hash each tag separately and add the hashes up (which doesn't care about order). A "context" is one unique combination of name and tags.

| H (contexts) | key | ns/line | count+gauge lines per flush, in → out |
|---|---|---|---|
| 16 (768) | parse only | ~40 | |
| | raw tag string | ~89 | 75k → 13.8k (5.4x) |
| | sort tags, then hash | ~107 | 75k → **576 (130x)** |
| | sum of per-tag hashes | ~76 | 75k → 576 (130x) |
| 256 (12k) | raw / sort / sum | 100 / 112 / 82 | 1.2x / 8.1x / 8.1x |
| 4096 (196k) | raw / sort / sum | 98 / 118 / 87 | 1.0x / 2.3x / 2.3x |

* **Normalization is what makes aggregation work.** With 4 tags in random order, one context can show up as up to 24 different keys, and most of the aggregation evaporates (5.4x instead of 130x).
* **It's cheap.** Sorting adds ~15-20 ns per line over the raw key. The order-independent sum is actually *cheaper* than raw, because the table ends up smaller and has fewer cache misses. Either way, it's ~20-30x cheaper than the ~2.4 µs `send()` it saves.
* **How much it helps depends on how many contexts show up per flush window.** With 196k contexts and 75k lines per flush, most contexts appear only once per window, so there's not much to combine: just 2.3x.

7. SO_REUSEPORT
---
`SO_REUSEPORT` lets several sockets bind the same address, and the kernel spreads incoming datagrams across them. It's the usual way to scale a UDP receiver across threads. `./probe` checks which socket families allow it on 6.18:
```
AF_INET/SOCK_DGRAM, reuseport=1:  bind #1 ok, bind #2 ok
AF_UNIX/SOCK_DGRAM, reuseport=0:  bind #2: Address already in use
AF_UNIX/SOCK_DGRAM, reuseport=1:  setsockopt(SO_REUSEPORT): Operation not supported; bind #2: Address already in use
```
UDS doesn't. `net/core/sock.c:1317` rejects it outright: `if (valbool && !sk_is_inet(sk)) ret = -EOPNOTSUPP;`. That check came in with commit 5b0af621c3f6 "net: restrict SO_REUSEPORT to inet sockets", merged for 6.13-rc6 in January 2025 (v6.8-v6.10 don't have it). Before that, `AF_UNIX` accepted the option and silently ignored it. I haven't checked which stable kernels got the backport.

For UDP, here's how datagrams were split across 4 reuseport sockets:

| flows | steering | per-socket split |
|---|---|---|
| 1 | hash | everything on one socket (3/3 trials) |
| 4 | hash | `500k,500k,0,1M` · `500k,1M,500k,0` · `0,500k,500k,1M` · `0,500k,0,1.5M` · `0,1.5M,0,500k` |
| 8 | hash | `1M,0,500k,500k` (still lumpy) |
| 4 (on cpus 4-7) | CBPF `cpu % 4` | `500k,496k,490k,496k` (even) |

With the default hash steering, at least one socket sat idle in every 4-flow trial. The kernel hashes each flow to a socket, and with only a handful of flows, collisions are likely. Steering by CPU with a CBPF program spreads them evenly. On loopback, though, "the CPU the packet arrived on" is the sender's CPU, so steering by CPU really means steering by whichever CPU the sender ran on.

As for throughput: 4 reuseport sockets cut loss to ~0.1% (4x the buffer, and 4 readers), but the 4 senders then top out at ~1.2M/s, compared with ~2M/s offered against a single socket. On loopback, the sender pays for waking the extra readers. CBPF steering ran at ~1% loss and ~1.1M/s.

For UDS, the workaround is to do reuseport by hand: open several paths and spread the clients across them. 4 clients, non-blocking unless noted:

| setup | 1M/s offered | flat out |
|---|---|---|
| 1 path, 1 thread | 8-10% loss | 0.93M/s |
| 1 path, 4 threads on one fd | **21% loss (worse)** | 1.08M/s |
| 4 paths, client i → path i%4 | 0.9% loss | **3.9M/s** (20% loss) |
| 4 paths, blocking | | 3.5-3.8M/s, no loss (vs 85k/s for 1 path) |

Adding more readers to one fd doesn't help; it makes things worse. The queue is still 11 deep, and now four threads are fighting over its lock. Using N paths multiplies both the queue and the readers, and that's what scales.

Results summary
---
Orders of magnitude on this box, at ≤~1% loss:

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
* **This is a WSL2/Hyper-V guest.** Wakeups and IPIs cost VM exits, which magnifies anything that depends on wakeups: single-client UDP, UDS blocking convoys, and the reuseport sender cap. Expect bare metal to move these numbers, maybe a lot, though probably not the order-of-magnitude gap between batched and unbatched.
* **Loss below capacity is a WSL2 artifact** (3b). The idle blocking receiver loses its CPU for 1-150 ms, both to other guest tasks placed on its "idle" CPU and to Hyper-V descheduling the halted vCPU. Busy-polling or a 4 MiB buffer gets rid of it. The capacity limits quoted here come from the busy-poll sweeps and should hold up. The blocking-server loss figures below capacity are dominated by these stalls, disagree between trials by up to a few percent, and shouldn't be expected on a real server with a quiet core. A follow-up on real hardware will check that.
* **Not explained:** busy-poll runs still show the occasional single outlier (2.95% at 250k in one trial). Spinning vCPUs can be preempted too (`jitter -m spin` saw a 33 ms gap), so these are probably the same host/guest stalls, but they weren't individually tracked down.
* **The paced client busy-spins one vCPU per thread.** That measurably hurts wakeup latency on the idle vCPU (3b, "Confirming it without sockets"). A sleeping pacer would be kinder, but it adds its own wakeup jitter.
* **Loopback only.** The sender runs the receive path, so "client-bound" and "server-bound" blur together.
* **The client copies prebuilt lines.** A real client formats each line (snprintf, ~100+ ns), which would make single-thread batched client rates much lower than shown here.
* **Single host, default buffers.** Raising `SO_RCVBUF` (up to `rmem_max` = 4 MiB) buys time, not throughput, and adds wait.
* **CPU placement.** Sweeps pin the server to CPU 2. Reuseport runs pin server threads to CPUs 0-3, which share a core with whatever lands on CPU 0.


Re-run on a 2-vCPU KVM guest
---
The experiments above were repeated on very different hardware to see what carries over. Full tables, raw output and caveats: `results_2vcpu/REPORT.md`. Scripts: `sweep2.sh`, `run_all2.sh` (server on CPU 0, clients on CPU 1) and `hwbench.c` (micro-costs).

| | this box | box above |
|---|---|---|
| CPU | Xeon Skylake-SP, 2 vCPUs, no SMT, 3.7 GiB | i7-11800H, 8 vCPUs |
| Virtualization | KVM, PTI + IBRS mitigations | WSL2 / Hyper-V |
| Kernel | 6.8.0-138 (Ubuntu) | 6.18.58 |
| `rmem_max` | 212992 | 4194304 |
| `net.unix.max_dgram_qlen` | **512** | 10 |

Measured costs: bare syscall ~860 ns, `send()` of 60 B ~2.9 us, `recv()` ~1.5 us, cross-CPU wakeup round trip ~45 us (same CPU ~8 us). A 100 us sleeper woke more than 1 ms late 11-16 times per 5 s on an idle CPU.

**What reproduced**
* UDP buffer capacity is hardware independent: 222 / 93 / 13 datagrams for 60 / 1432 / 8192 B (above: 221 / 92 / 13).
* Blocking vs busy-poll: at 200k/s the blocking server lost 0.47-3.0% (p99 wait ~0.8-1.6 ms), busy-poll 0.006-0.58% (p99 ~10-20 us).
* Idle-server drops: the socket drop counter equals `RcvbufErrors` in all 5 trials, and drops arrive in bursts after 2-5 ms gaps with ~0 server CPU. `drops ~ gap x rate - 222` holds (4317 us gap: 641 predicted, 641 counted).
* Batching: UDP 1432 B reaches ~6M metrics/s and 8192 B ~20M/s at <=~0.6% loss with a busy-poll server, from ~0.29M/s unbatched.
* Aggregation ratios are identical (5.4x raw vs 130x sorted or summed at H=16; 1.2x vs 8.1x; 1.0x vs 2.3x). Cost per line is 2.5-3x higher (parse ~110 ns).
* `SO_REUSEPORT`: hash steering is lumpy (1 flow always lands on 1 socket, 2-8 flows leave sockets idle), CPU steering is even but limited by the number of CPUs.
* UDS blocking senders never lose data. Four paths delivered ~525k/s against ~227k/s for one.

**What differed**
* Everything is 3-5x slower, and one client CPU cannot saturate the server, so the one-metric tests here are client-bound (~220k/s blocking, ~290k/s busy-poll). No server capacity figure came out of them.
* UDS held 278 datagrams at 60 B, not 11, consistent with `max_dgram_qlen=512` (byte-limit explanation not checked against 6.8 source). The four-sender blocking collapse (~85k/s above) did not appear (~290k/s), but the clients shared one CPU, so the cause is not separated.
* UDS `SO_REUSEPORT` returns `EOPNOTSUPP` on this 6.8 kernel, which contradicts the note above that 6.8-6.10 accept it silently. Ubuntu probably backported the change.
* The client logged ~100k late sends per trial with stalls up to 17 ms, so a bursty client is **not** ruled out here, unlike above.
* The 4 MiB `SO_RCVBUF` test could not be repeated (`rmem_max` caps it; the request only doubled the buffer to ~2.2 ms of slack, and loss only fell to 0.3-1.4%).

**Not reproduced:** the queue-depth time series, `debug/who.c`, the 4-socket reuseport throughput comparison, and the `sysctl` experiment (no root). Non-blocking UDS flat-out runs with several clients are not meaningful on 2 CPUs.


Re-run on an 8-vCPU KVM guest (AMD EPYC-Rome)
---
Same experiments on 8 non-SMT vCPUs (kernel 6.8.0-138, `rmem_max` 212992, `max_dgram_qlen` 512, non-root), using the original CPU layout (server on CPU 2, clients from CPU 4). Full tables and caveats: `results_8vcpu/REPORT.md`. Scripts: `run_all8.sh`, `run_rest8.sh`.

* **Reproduced:** buffer capacity (222 / 93 / 13 datagrams), batching scaling (busy-poll: 1432 B ~10M/s and 8192 B ~20M/s at <=~0.1% loss), aggregation ratios, UDS `SO_REUSEPORT` EOPNOTSUPP, UDS 4-path scaling, the blocking-UDS multi-client slowdown (~0.18M/s vs ~0.9M/s with one client).
* **Differed:** single-socket UDP capacity is ~1.0M/s delivered (Readme box: ~1.7M/s). The idle-server drops of section 3b are far smaller: in 3 s runs the blocking server lost <=0.06% at 250k/s, but in 30 s x 10 trials (`results_8vcpu_long/`) it loses data in every trial (median 0.08%, bursty, 42% of seconds with drops) versus 0.002% busy-poll. That is 20-50x below the laptop's 1.6-3.7%. `jitter` saw few >1 ms stalls and `debug/who.c` none.
