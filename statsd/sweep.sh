#!/bin/bash
# usage: sweep.sh "<server args>" "<client args>" rate...   (rate 0 = flat out)
# env: DUR seconds per point (default 3), TRIALS (default 3), MAXN metrics for flat out (default 2e7)
# Server pinned to cpu 2, clients from cpu 4. Prints one row per rate.
cd "$(dirname "$0")"
S=$1 C=$2; shift 2
printf "%-9s %10s %10s %8s %8s %9s %9s\n" target sent/s deliv/s drop% dg/s p99_us rmem_pk
for r in "$@"; do for t in $(seq ${TRIALS:-3}); do
  n=$(awk -v r=$r -v d=${DUR:-3} -v m=${MAXN:-2e7} 'BEGIN{printf "%d", r?r*d:m}')
  ./server -C 2 -i 300 -w -q 16 $S 2>/dev/null > /tmp/srv.out & sp=$!
  sleep 0.3
  ./client -C 4 -r $r -n $n $C > /tmp/cli.out
  wait $sp
  awk -v r=$r '
    function g(k,  i,a){for(i=1;i<=NF;i++){split($i,a,"=");if(a[1]==k)return a[2]}return 0}
    /^cli/{cm=g("metrics");cd=g("dgrams");ce=g("eagain_metrics");cs=g("secs")}
    /^srv/{sd=g("dgrams");dr=g("drops");p99=g("wait_p99_us");rp=g("rmem_peak")}
    END{mpd=cd?cm/cd:1; tot=cm+ce; lost=dr*mpd+ce; deliv=sd*mpd;
        printf "%-9s %10.0f %10.0f %8.3f %8.0f %9s %9s\n", r?r:"max", tot/cs, deliv/cs, 100*lost/tot, cd/cs, p99, rp}' /tmp/cli.out /tmp/srv.out
done; done
