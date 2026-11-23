#!/usr/bin/env python3
# Median over reps, deltas vs A0 at same (exp, T).  ./summarize.py [out.tsv]
import csv, statistics as st, sys
from collections import defaultdict

rows = defaultdict(list)
for r in csv.DictReader(open(sys.argv[1] if len(sys.argv) > 1 else "out.tsv"), delimiter="\t"):
    rows[(r["exp"], r["arm"], int(r["T"]))].append(r)

def med(k, f):
    v = [float(r[f]) for r in rows[k] if r[f] != "-"]
    return st.median(v) if v else float("nan")

def table(title, hdr, lines):
    print(f"\n## {title}\n")
    print("| " + " | ".join(hdr) + " |\n|" + "---|" * len(hdr))
    for l in lines: print("| " + " | ".join(l) + " |")

def fmt(c, d=1): return f"{c:.{d}f}"

for exp in ("sweep", "hostile"):
    out = []
    for k in sorted({k for k in rows if k[0] == exp and k[1] != "A0"}, key=lambda k: (k[2], k[1])):
        b = (exp, "A0", k[2])
        m = lambda c: med(k, c)
        d = lambda c: m(c) - med(b, c)
        out.append([k[1], str(k[2]), fmt(d("sys_busy_ns") / m("hooks")), fmt(d("app_cpu_ns") / m("hooks")),
                    fmt(d("hook_mean_ns")), *(fmt(m(c), 0) for c in ("hook_p50_ns", "hook_p99_ns", "hook_p999_ns", "hook_max_ns")),
                    fmt(d("work_ns")), fmt(m("hooks_per_s_per_thr") / 1e6, 2)])
    table(exp, ["arm", "T", "sys ns/hook", "app ns/hook", "net mean ns", "p50", "p99", "p99.9", "max",
                "collateral ns", "Mhook/s/thr"], out)

by = {a: med(("bystander", a, 1), "hook_mean_ns") for a in ["A0", "U1", "S1", "S2"]}
table("bystander getppid()", ["attached", "ns/call", "delta ns"],
      [[a, f"{v:.1f}", f"{v - by['A0']:+.1f}"] for a, v in by.items()])
table("bpf_stats (prog body only, T=1)", ["arm", "ns/run"],
      [[a, fmt(med(("bpfstats", a, 1), "bpf_ns"))] for a in ["U1", "S1", "S2"]])
