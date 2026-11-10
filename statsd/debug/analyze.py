# usage: analyze.py <server -o series> <client -L file>  (series must be the 5-column format)
import sys
srv, cli = sys.argv[1], sys.argv[2]
S = [tuple(map(int, l.split())) for l in open(srv)]
L = [tuple(map(int, l.split())) for l in open(cli)]
# server-side gaps between consecutive recv returns
gaps = sorted(((S[i][0]-S[i-1][0], i) for i in range(1, len(S))), reverse=True)
print(f"  srv samples={len(S)} final_drops={S[-1][2]} top srv gaps (us): {[round(g/1e3) for g,_ in gaps[:5]]}")
print(f"  client late events (>50us) = {len(L)}, top (us): {sorted([round(x[2]/1e3) for x in L], reverse=True)[:5]}")
# each drop burst: first sample where drops increased
prev = S[0][2]; bursts = []
for i, r in enumerate(S):
    if r[2] > prev: bursts.append((i, r[2]-prev)); prev = r[2]
for i, nd in bursts[:6]:
    t = S[i][0]
    # largest server gap in the 3ms before
    lo = i
    while lo > 0 and t - S[lo][0] < 3_000_000: lo -= 1
    g = max(((S[j][0]-S[j-1][0], j) for j in range(lo+1, i+1)), default=(0, i))
    cl = [x for x in L if t - 5_000_000 < x[0] <= t + 200_000]
    print(f"  drop +{nd} at t={(t-S[0][0])/1e6:.1f}ms rmem={S[i][1]} wait={S[i][3]/1e3:.0f}us | max srv gap in prior 3ms: {g[0]/1e3:.0f}us "
          f"ending {(t-S[g[1]][0])/1e3:.0f}us before | client late events within 5ms before: {len(cl)} max {max([x[2] for x in cl], default=0)/1e3:.0f}us")
