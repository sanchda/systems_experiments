# usage: gaps.py <server -o series>: classify recv gaps >400us by thread CPU time
import sys
S=[tuple(map(int,l.split())) for l in open(sys.argv[1])]
rows=[]
for i in range(1,len(S)):
    g=S[i][0]-S[i-1][0]
    if g>400_000:
        rows.append((g, S[i][4]-S[i-1][4], S[i][2]-S[i-1][2], S[i][3]))
rows.sort(reverse=True)
print(f"  gaps>400us: {len(rows)}; total drops {S[-1][2]}")
for g,c,d,w in rows[:8]:
    print(f"    gap {g/1e3:7.0f}us  thread_cpu_during_gap {c/1e3:7.0f}us  drops_in_gap {d:5d}  wait_after {w/1e3:6.0f}us")
