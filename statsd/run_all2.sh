#!/bin/bash
# 2-vCPU version of run_all.sh. Server on cpu0, all client threads on cpu1. Output: results_2vcpu/<name>.txt
# Rates are scaled down: one client CPU can't offer more than a few hundred k sends/s.
cd "$(dirname "$0")"; mkdir -p results_2vcpu
run() { n=$1; shift; echo "## $n  server[$1] client[$2]" | tee results_2vcpu/$n.txt; ./sweep2.sh "$@" | tee -a results_2vcpu/$n.txt; }
export MAXN=1000000
run udp_1_block      ""         "-c 1"               50000 100000 200000 300000 0
run udp_1_poll       "-P"       "-c 1"               50000 100000 200000 300000 0
run udp_1_count      "-m"       "-c 1"               100000 200000 0
run udp_1_mmsg       "-M 64"    "-c 1"               100000 200000 300000 0
MAXN=60000000
run udp_1432_block   "-m"       "-b 1432 -c 1"       2000000 5000000 10000000 20000000 0
run udp_1432_poll    "-m -P"    "-b 1432 -c 1"       2000000 5000000 10000000 20000000 0
MAXN=150000000
run udp_8192_block   "-m"       "-b 8192 -c 1"       5000000 10000000 20000000 40000000 0
run udp_8192_poll    "-m -P"    "-b 8192 -c 1"       5000000 10000000 20000000 40000000 0
MAXN=1000000
run uds_1_nb_block   "-u"       "-u -N -c 1"         50000 100000 200000 300000 0
run uds_1_nb_poll    "-u -P"    "-u -N -c 1"         50000 100000 200000 300000 0
run uds_1_nb_mmsg    "-u -M 64" "-u -N -c 1"         100000 200000 300000 0
MAXN=60000000
run uds_8192_nb_block "-u -m"   "-u -N -b 8192 -c 1" 5000000 10000000 20000000 40000000 0
run uds_8192_nb_poll  "-u -m -P" "-u -N -b 8192 -c 1" 5000000 10000000 20000000 40000000 0
MAXN=500000
run uds_1_blk_c1     "-u"       "-u -c 1"            0
run uds_1_blk_c4     "-u"       "-u -c 4"            0
MAXN=40000000
run uds_8192_blk_c1  "-u -m"    "-u -b 8192 -c 1"    0
run uds_8192_blk_c3  "-u -m"    "-u -b 8192 -c 3"    0
echo ALLDONE
