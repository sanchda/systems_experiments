uprobe vs syscall hook
===
Two ways to get an app event into eBPF:

* **uprobe**: app calls `hook(seq, task_id)`, a `noinline` `nop; ret`.  uprobe on it.
* **syscall**: app calls `prctl(MAGIC, seq, task_id, 0, 0)`.  `MAGIC` is nowhere near a real `PR_*`, so the kernel returns `EINVAL` and nothing changes.  eBPF hangs off `sys_enter_prctl`, filters on `MAGIC`.

Same BPF body everywhere: `@n = count(); @x = sum(seq ^ task_id)` (per-cpu maps, no ringbuf, no consumer).
Attached with bpftrace since this box has no clang/libbpf.

```
./run.sh                      # builds, re-execs under sudo, appends to out.tsv
N=2000000 R=3 THREADS="1 4" ./run.sh
./summarize.py out.tsv
```

Arms
---
| arm | site | attached |
|---|---|---|
| A0 | nothing (timing loop only) | – |
| U0 | `hook()` | – |
| U1 | `hook()` | `uprobe:drv:hook` |
| S0 | `prctl(MAGIC,…)` | – |
| S1 | `prctl(MAGIC,…)` | `tracepoint:syscalls:sys_enter_prctl` |
| S2 | `prctl(MAGIC,…)` | `fentry:__x64_sys_prctl` |

Experiments
---
* **sweep**: N hooks per thread, K=50 xorshift+store iters of fake work between hooks, T pinned threads.
* **hostile**: same, plus a sibling thread spinning `mmap`/`munmap` (an `mmap_lock` writer).  uprobes on 6.8 take `mmap_lock` for read on every hit.
* **bystander**: separate `getppid()` loop with each probe attached.  Any `syscalls:*` tracepoint sets `SYSCALL_TRACEPOINT` work on *every* task; this checks what that costs everyone else.
* **bpfstats**: `kernel.bpf_stats_enabled=1` pass to split prog body from dispatch.  Separate pass because stats themselves add clock reads.

Columns (summarize.py, all deltas vs A0 at same T)
---
* `sys ns/hook`: system-wide busy time (`/proc/stat` user+nice+sys+irq+softirq) per hook.  Capacity.
* `app ns/hook`: `getrusage` of the driver per hook.
* `net mean ns`, `p50..max`: rdtsc-bracketed time the app is blocked in the hook.  Latency.
* `collateral ns`: slowdown of the fake work *between* hooks (cache/TLB/predictor pollution).  Capacity loss the bracket misses.
* `Mhook/s/thr`: per-thread throughput including work.

Caveats
---
* GCP KVM guest, no PMU: no cycle counters, no host visibility.  "Cycles" are TSC-time.  Neither int3 nor `syscall` should VM-exit, so host-side cost should be ~0 but this can't confirm it.
* `/proc/stat` is tick-sampled and includes whatever else runs on the box; baseline is A0 at the same T, no background subtraction.  Use big N and reps.
* AMD guest rdtsc is quantized (~10ns steps); percentiles are coarse, means are fine.  Percentiles past the 65536-cycle histogram print `inf`; see `over`.
* THREADS stops at 8: cpu i and i+8 are SMT siblings, and T=16 also oversubscribes once falco wakes up.
* Kernel 6.8: pre lockless-uprobe (6.10–6.12) and pre uprobe-nop5 optimization.  Newer kernels should look better for uprobes.
* Only one flavor of each: bpftrace's perf-event uprobe (not `uprobe_multi`), perf syscall tracepoint (not raw_tp/`tp_btf`), fentry.
* `uprobes_treelock` is global on 6.8, so uprobe contention depends on the machine-wide hit rate, not one app's.
* K=50 work on a 32KB buffer (48KB L1d) is a toy; it can't show realistic cache pollution.

Results
---
6.8.0-1073-gcp, 16 vCPU AMD EPYC 9B45 (KVM), N=2M, K=50, R=3, arm order shuffled per rep, medians.  Raw: `out.tsv`.

**Falco was running** and dominates the syscall side.  It runs `prctl_x` on prctl's `sys_exit`
synchronously, inside the timing bracket, for every arm that calls prctl (S0 included): 2M prctls
cost falco ~20 ticks, 2M getppid cost it 0.  Its userspace consumer then saturates at 1.0 CPU from T=4 up,
so S-arm `sys ns/hook` there is a lower bound (falco falls behind), not a per-event cost.
It also voids the bystander test: falco's tracepoints already have `SYSCALL_TRACEPOINT` on everywhere.

So compare *attach increments* (probed site minus unprobed site), which falco cancels out of:

```
net mean ns       T=1    T=2    T=4    T=8
U0  hook()          2      2      2      2
U1  + uprobe      232    275    339   1177   (p99 at T=8: 6µs, p99.9 11µs)
S0  prctl()       155    201    217    188   (mostly falco; getppid is ~70)
S1  + tp           78     64     39     47   over S0
S2  + fentry       12     14     14     18   over S0
falco consumer   ~0.5   ~0.7   ~1.0   ~1.0   CPUs, S arms only
```

* Cost of attaching: uprobe ~230ns, syscall tracepoint ~40–80ns, fentry ~15ns.  On this box the
  totals happen to tie at T=1 (~235ns) only because falco inflates the bare prctl.
* Idle site: `hook()` is free; a prctl site always pays a syscall plus whatever already traces syscalls.
* Scaling: uprobe degrades from T=4 and is 5x worse at T=8.  Profile at T=8: 34% `native_queued_spin_lock_slowpath`
  via `find_uprobe` (`uprobes_treelock`), 15% `mmap_lock` `down_read`/`up_read`.  Syscall increments stay flat.
* Hostile mmap/munmap sibling: every arm's tail grows from munmap TLB shootdowns (A0 p99.9 30→530ns;
  S1/S2 p99 ~3x).  On top of that the uprobe worker sleeps on `mmap_lock` (mean 232→890ns, p99.9 6µs).
  Capacity columns there are meaningless: the spinner's CPU scales with run length.
* bpf_stats body cost (38/39/29ns) is inflated by the stats clock reads, which exceed S2's whole
  increment; read it as an upper bound, not a body/dispatch split.
* Collateral slowdown of the work between hooks: ≤1.5ns with this toy workload.
* Blocked time ≈ app CPU per hook in the sweep; not in hostile (the uprobe worker sleeps).
