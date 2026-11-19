#!/bin/bash
# Long-run noise study (8-vCPU box). DUR s per trial (default 30), TRIALS (default 10).
# Server on CPU 2, clients from CPU 4 (same as sweep.sh). Per trial: summary row + per-receive series (-q 16 -o)
# reduced to per-second drops and the largest gap between receives. Output: results_8vcpu_long/
cd "$(dirname "$0")"; R=results_8vcpu_long; DUR=${DUR:-30}; TRIALS=${TRIALS:-10}
point() { # name "<server args>" "<client args>" rate
  name=$1 S=$2 C=$3 r=$4
  n=$(awk -v r=$r -v d=$DUR 'BEGIN{printf "%d", r?r*d:d*1600000}')   # flat out: ~DUR s at ~1.6M/s
  out=$R/$name.txt; echo "## $name server[$S] client[$C] rate=$r dur=${DUR}s trials=$TRIALS" | tee $out
  printf "%-5s %10s %10s %8s %9s %10s %9s  %s\n" trial sent/s deliv/s drop% p99_us maxgap_ms secs persec_drops | tee -a $out
  for t in $(seq $TRIALS); do
    ./server -C 2 -i 300 -w -q 16 -o /tmp/long.series $S 2>/dev/null > /tmp/long.srv & sp=$!
    sleep 0.3
    ./client -C 4 -r $r -n $n $C > /tmp/long.cli
    wait $sp
    python3 -I - $t /tmp/long.cli /tmp/long.srv /tmp/long.series <<'PY' | tee -a $out
import sys
t,cli,srv,ser=sys.argv[1:5]
def kv(f,p):
    for l in open(f):
        if l.startswith(p): return dict(x.split('=',1) for x in l.split()[1:] if '=' in x)
c=kv(cli,'cli'); s=kv(srv,'srv')
cm=float(c['metrics']); cd=float(c['dgrams']); ce=float(c['eagain_metrics']); cs=float(c['secs'])
mpd=cm/cd if cd else 1; tot=cm+ce; lost=float(s['drops'])*mpd+ce
S=[tuple(map(int,l.split())) for l in open(ser)]
gap=max((S[i][0]-S[i-1][0] for i in range(1,len(S))),default=0)/1e6
bins={}; prev=0
for row in S:
    b=(row[0]-S[0][0])//10**9; bins[b]=row[2]
ps=[]; last=0
for b in range(max(bins)+1):
    v=bins.get(b,last); ps.append(v-last); last=v
print("%-5s %10.0f %10.0f %8.3f %9s %10.2f %9.1f  %s"%(t,tot/cs,float(s['dgrams'])*mpd/cs,100*lost/tot,s['wait_p99_us'],gap,cs,",".join(map(str,ps))))
PY
  done
}
# UDP, 1 metric/datagram, 4 paced client threads
for mode in block poll; do P=""; [ $mode = poll ] && P="-P"
  for r in 250000 500000 1000000 0; do point udp1_${mode}_$([ $r = 0 ] && echo max || echo $r) "$P" "-c 4" $r; done
done
# 3b variants at 250k/s
point udp1_block_250000_1client ""             "-c 1" 250000
point udp1_block_250000_rcvbuf  "-s 4194304"   "-c 4" 250000
echo ALLDONE
