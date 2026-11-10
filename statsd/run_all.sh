#!/bin/bash
# Full throughput matrix. Output: results/<name>.txt
cd "$(dirname "$0")"
run() { n=$1; shift; echo "## $n  server[$1] client[$2]" | tee results/$n.txt; ./sweep.sh "$@" | tee -a results/$n.txt; }
M=1000000
run udp_1_discard   ""        "-c 4"              250000 500000 $M 1500000 2000000 0
run udp_1_count     "-m"      "-c 4"              250000 500000 $M 1500000 2000000 0
run udp_1_mmsg      "-M 64"   "-c 4"              500000 $M 1500000 2000000 0
run udp_1432_count  "-m"      "-b 1432 -c 3"      5000000 10000000 20000000 30000000 0
run udp_8192_count  "-m"      "-b 8192 -c 3"      10000000 20000000 40000000 80000000 0
run uds_1_nb        "-u"      "-u -N -c 4"        250000 500000 $M 1500000 0
run uds_1_nb_mmsg   "-u -M 64" "-u -N -c 4"       250000 500000 $M 1500000 0
run uds_8192_nb     "-u -m"   "-u -N -b 8192 -c 3" 10000000 20000000 40000000 80000000 0
run uds_1_block_c1  "-u"      "-u -c 1"           0
run uds_1_block_c4  "-u"      "-u -c 4"           0
run uds_8192_block_c1 "-u -m" "-u -b 8192 -c 1"   0
run uds_8192_block_c3 "-u -m" "-u -b 8192 -c 3"   0
