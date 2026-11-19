#!/bin/bash
# Remaining experiments for the 8-vCPU re-run. Output: results_8vcpu/<name>.txt
cd "$(dirname "$0")"; mkdir -p results_8vcpu; R=results_8vcpu
run() { n=$1; shift; echo "## $n  server[$1] client[$2]" | tee $R/$n.txt; bash ./sweep.sh "$@" | tee -a $R/$n.txt; }
M=1000000
# busy-poll capacity sweeps
run poll_udp_1_discard "-P"      "-c 4"              250000 500000 $M 1500000 2000000 0
run poll_udp_1432_count "-P -m"  "-b 1432 -c 3"      5000000 10000000 20000000 30000000 0
run poll_udp_8192_count "-P -m"  "-b 8192 -c 3"      10000000 20000000 40000000 80000000 0
run poll_uds_1_nb       "-P -u"  "-u -N -c 4"        250000 500000 $M 1500000 0
run poll_uds_8192_nb    "-P -u -m" "-u -N -b 8192 -c 3" 10000000 20000000 40000000 80000000 0
# reuseport / multi-path
run udp_1_reuse4_hash  "-R 4 -C 0"    "-c 4"          $M 1500000 2000000 0
run udp_1_reuse4_cbpf  "-R 4 -B -C 0" "-c 4"          $M 1500000 2000000 0
run uds_1_nb_path4     "-u -R 4 -C 0" "-u -N -c 4 -P 4" 500000 $M 1500000 0
run uds_1_nb_thr4      "-u -T 4 -C 0" "-u -N -c 4"    500000 $M 1500000 0
run uds_1_block_path4  "-u -R 4 -C 0" "-u -c 4 -P 4"  0
# 3b fixes: 5 trials at 250k/s
export TRIALS=5
run 3b_baseline_block  ""   "-c 4" 250000
run 3b_one_client      ""   "-c 1" 250000
run 3b_rcvbuf_4MiB     "-s 4194304" "-c 4" 250000
run 3b_busypoll        "-P" "-c 4" 250000
unset TRIALS
# buffer capacity: stall server, non-blocking client
{ for spec in "udp 60 1" "udp 1432 1432" "udp 8192 8192" "uds 60 1" "uds 1432 1432" "uds 8192 8192"; do set -- $spec
  fam=$1; sz=$2; b=$3; su=""; cu=""; [ $fam = uds ] && { su=-u; cu=-u; }
  ./server -C 2 $su -m -S 1500 -i 300 > /tmp/f.srv 2>/dev/null & sp=$!; sleep 0.3
  ./client -C 4 $cu -N -b $b -c 1 -n 20000 > /tmp/f.cli; wait $sp
  echo "== $fam ${sz}B"; cat /tmp/f.cli /tmp/f.srv; done; } 2>&1 | tee $R/fill_capacity.txt
# queue-depth series
for r in 500000 1000000 1500000 2000000; do
  ./server -C 2 -i 300 -q 1 -o /tmp/fill.series > /tmp/f.srv 2>/dev/null & sp=$!; sleep 0.3
  ./client -C 4 -r $r -n $((r*3)) -c 4 > /tmp/f.cli; wait $sp
  awk 'NR%50==1{print $1,$2}' /tmp/fill.series > $R/fill_udp_$r.txt; cat /tmp/f.srv >> $R/fill_srv_$r.txt
done
# scheduler jitter without sockets
{ echo "# spin cpu2"; for i in 1 2 3; do ./jitter -m spin -C 2 -s 5; done
  echo "# sleep cpu2, no load"; for i in 1 2 3; do ./jitter -m sleep -C 2 -s 5; done
  echo "# sleep cpu2, 4 spinners"; for i in 1 2 3; do ./jitter -m sleep -C 2 -s 5 -L 4; done; } 2>&1 | tee $R/jitter.txt
# aggregation
for H in 16 256 4096; do echo "## H=$H"; ./agg -H $H; ./agg -H $H; done 2>&1 | tee $R/agg.txt
# micro costs
for i in 1 2 3; do ./hwbench; done 2>&1 | tee $R/hw_microbench.txt
echo ALLDONE
