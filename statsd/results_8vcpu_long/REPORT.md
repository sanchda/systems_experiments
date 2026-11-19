# Long-run noise study, 8-vCPU KVM guest

Follow-up to `../results_8vcpu/REPORT.md`, whose 3 s x 3 trial points were too short to see rare stalls.
Here: **30 s per trial, 10 trials per point**, UDP 1 metric/datagram, 4 paced client threads (CPUs 4-7), server on CPU 2,
`-q 16 -o` series reduced to per-second drop counts (the last column of each trial row in the `udp1_*.txt` files) and the largest
gap between sampled receives (every 16th receive, so resolution ~64 us at 250k/s). Script: `../run_long8.sh`. `summary.txt` is the table below.
Raw per-receive series were not kept (tens of MB per trial).
Points were run one configuration after another (not interleaved), so slow drift on the host would show up as a config effect.

| config | deliv/s (median) | loss median | loss IQR | loss max | trials with 0 loss | seconds with any drop | max receive gap (median / max) |
|---|---|---|---|---|---|---|---|
| 250k, blocking | 249.8k | **0.080%** | 0.039-0.125 | 0.193 | 0/10 | 126/302 (42%) | 3.4 / 4.7 ms |
| 250k, busy-poll | 250.0k | **0.002%** | 0.000-0.013 | 0.025 | 4/10 | 14/303 (5%) | 1.2 / 3.6 ms |
| 250k, blocking, 1 client | 250.0k | 0.016% | 0.007-0.033 | 0.048 | 0/10 | 51/301 (17%) | 2.0 / 9.1 ms |
| 250k, blocking, `-s 4194304` (capped to 2x) | 250.0k | 0.014% | 0.005-0.033 | 0.048 | 0/10 | 49/300 (16%) | 3.2 / 3.7 ms |
| 500k, blocking | 499.8k | 0.040% | 0.022-0.056 | 0.112 | 0/10 | 103/302 (34%) | 2.9 / 3.6 ms |
| 500k, busy-poll | 500.0k | 0.005% | 0.000-0.011 | 0.039 | 3/10 | 34/305 (11%) | 1.5 / 4.1 ms |
| 1M offered, blocking | 934k | 6.6% | 5.4-8.5 | 11.2 | 0/10 | 300/309 | 0.9 / 2.1 ms |
| 1M offered, busy-poll | 906k | 9.3% | 7.3-10.2 | 16.9 | 0/10 | 300/310 | 1.2 / 1.8 ms |
| flat out (~1.5-2M offered), blocking | 996k | 38.5% | 36.6-41.4 | 53.0 | 0/10 | 284/291 | 1.2 / 3.1 ms |
| flat out, busy-poll | 938k | 39.7% | 36.6-43.9 | 53.8 | 0/10 | 295/303 | 1.5 / 2.8 ms |

## What the longer runs show
* **The idle-server drops of Readme 3b do exist on this box, but are small.** A blocking server at 250k/s lost data in all 10 trials
  and in 42% of seconds; the busy-poll server lost nothing in 4 of 10 trials and dropped in 5% of seconds. Median loss differs ~40x
  (0.080% vs 0.002%). The earlier 3 s runs (<=0.06%, busy-poll 0) could not distinguish these, so "did not reproduce" in
  `../results_8vcpu/REPORT.md` was too strong; it has been amended.
* **Drops are bursty.** Per-second counts are mostly 0 with occasional bursts of 60-2000 datagrams (e.g. trial 3: `...2031, ..., 2088...`),
  as the Readme's gap model predicts. Magnitude is 20-50x below the Readme box's 1.6-3.7%, consistent with the stalls being 1-5 ms here and
  1-34 ms there.
* **More slack helps a lot, one fewer client helps about as much:** 2x buffer and 1 client both cut median loss ~5x (0.08 -> 0.014-0.016%) but neither removes it.
  These were separate configs in sequence, so a config effect is not separated from host drift.
* **Above ~0.93M/s the server is overloaded and blocking vs busy-poll is not the story.** At 1M offered, blocking delivered 934k with 6.6% loss and busy-poll
  906k with 9.3% (IQRs overlap partly); flat out both deliver ~0.94-1.0M/s. Capacity here is ~0.9-1.0M/s delivered, versus ~1.7M/s on the laptop.
  Flat-out trials ran 24-32 s because the client rate varies (1.5-2.0M/s); they are not equal-duration.
* **Noise profile:** between-trial spread at 250k/s is about 3x (IQR 0.039-0.125%) even with 300 s of data per config; per-config medians over 10 trials are
  usable, single trials are not. Overload points vary 36-54% flat-out because the offered load itself varies.

## Caveats / not explained
* The 5 trials at 250k/s in the earlier 3 s run had 0.002-0.052% loss (mean ~0.027%); the 30 s run's median is 0.080%. Either the 3 s points undercount stalls or
  the host was noisier during this run; I did not separate them. Interleaving configs and repeating at different times would.
* `maxgap_ms` is the largest gap between *sampled* receives and does not show what caused it. I did not run `jitter` concurrently or record scheduler stats per trial,
  so attributing the stalls to the hypervisor (vs guest scheduling) is not established here.
* Only UDP, 1 metric/datagram. Batched and UDS points were not repeated at this length.

## Open question: why is there any loss at 250k/s?
The buffer holds 222 datagrams = ~0.9 ms at 250k/s, so any pause in the server's reading longer than that drops data. Observed receive gaps
(blocking, 250k/s): median worst gap 3.4 ms, max 4.7 ms. What stops the server for milliseconds is **not established**. Hypotheses, most likely first:
1. Host-level vCPU stalls (descheduled vCPU, or a late wakeup of a halted vCPU). Consistent with: busy-poll cuts loss ~40x but not to zero;
   `jitter -m sleep` saw >1 ms late wakeups 3-5 times per 5 s with no sockets. Not confirmed: `/proc/stat` steal is 0 on all CPUs, but KVM guests often don't report steal, so this is no evidence either way.
2. Guest-side noise on CPU 2 (ticks, RCU, kworkers, ksoftirqd, other processes placed on the idle CPU).
3. Client load: 1 client instead of 4 cut median loss ~5x, via host contention or softirq/wakeup work. Not separated.
Ruled out earlier (Readme 3b): counting error, a bursty client being the main cause.

Planned tests, not yet run:
* Keep per-trial server scheduler stats (`t0_oncpu_ms`, `t0_runq_wait_ms`, slices), which the long run discarded. Mostly run-queue wait suggests guest scheduling; neither running nor waiting suggests a late wakeup (host).
* Spin-mode `jitter` on several CPUs at once: gaps aligned across CPUs suggest a whole-guest host event.
* Diff `/proc/interrupts` and `/proc/softirqs` for CPU 2 across a run.
* Move the server to another CPU and see whether loss follows the CPU.
* Interleave configurations and repeat at another time, to separate config effects from host drift (the 3 s runs averaged ~0.027% vs 0.080% in the 30 s run).
* Optional: per-thread sequence numbers and send timestamps in the payload (behind a new flag), to get the exact lost ranges and burst windows. Gives loss shape, not cause.
