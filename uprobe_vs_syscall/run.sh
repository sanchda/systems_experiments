#!/bin/bash
# Runs every arm, appends rows to $OUT.  Needs root for bpftrace.
set -e
cd "$(dirname "$0")"
if [ $EUID -ne 0 ]; then
  gcc -O2 -fcf-protection=none -pthread -o drv drv.c
  objdump --no-show-raw-insn --disassemble=hook drv | grep -A2 '<hook>:'
  exec sudo -E "$0" "$@"
fi

N=${N:-1000000}
K=${K:-50}
R=${R:-3}
THREADS=${THREADS:-"1 2 4 8"} # <= physical cores (cpu i, i+8 are SMT siblings here)
OUT=${OUT:-out.tsv}
MAGIC=0x7eb9f00d
BODY='@n = count(); @x = sum'

declare -A BT=(
  [U1]="uprobe:$PWD/drv:hook { $BODY(arg0 ^ arg1); }"
  [S1]="tracepoint:syscalls:sys_enter_prctl /args.option == $MAGIC/ { $BODY(args.arg2 ^ args.arg3); }"
  [S2]="fentry:vmlinux:__x64_sys_prctl /args.regs->di == $MAGIC/ { $BODY(args.regs->si ^ args.regs->dx); }"
)
declare -A MODE=([A0]=n [U0]=u [U1]=u [S0]=s [S1]=s [S2]=s)

BTPID=
attach() {
  [ -n "${BT[$1]}" ] || return 0
  bpftrace -e "BEGIN { printf(\"ready\n\"); } ${BT[$1]}" >bt.log 2>&1 &
  BTPID=$!
  until grep -q ready bt.log; do kill -0 $BTPID || { cat bt.log; exit 1; }; sleep 0.1; done
}

# bpf_stats run_time_ns / run_cnt across bpftrace's progs (BEGIN has cnt 1, harmless)
bpf_ns() {
  bpftool prog show -j | jq -r --argjson p $BTPID \
    '[.[] | select(.pids[]?.pid == $p) | select(.run_cnt > 1)] | if length == 0 then "-" else ((map(.run_time_ns) | add) / (map(.run_cnt) | add)) end'
}

# sets BTN; not called in a subshell so BTPID reset sticks
detach() {
  BTN=0
  [ -n "$BTPID" ] || return 0
  kill -INT $BTPID; wait $BTPID || true; BTPID=
  BTN=$(awk '/^@n:/ {print $2}' bt.log); rm -f bt.log
}

# exp arm rep drv-args...   (arm names the probe attached; drv mode comes from args)
one() {
  local exp=$1 arm=$2 rep=$3 row stat=- want; shift 3
  attach $arm
  row=$(./drv "$@")
  [ "$STATS" = 1 ] && stat=$(bpf_ns)
  detach; want=$(cut -f6 <<<"$row")
  [ "$(cut -f1 <<<"$row")" = s ] && want=$((want + 1)) # drv's startup EINVAL check also hits
  if [ "$exp" != bystander ] && [ -n "${BT[$arm]}" ] && [ "$BTN" != "$want" ]; then
    echo "WARN $exp $arm: bpf saw $BTN, drv made $want" >&2
  fi
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$exp" "$arm" "$rep" "$row" "${BTN:-0}" "$stat" | tee -a $OUT
}

[ -s $OUT ] || printf 'exp\tarm\trep\t%s\tbpf_n\tbpf_ns\n' "$(./drv header)" >$OUT
sysctl -qw kernel.bpf_stats_enabled=0

for rep in $(seq $R); do
  # 1. main sweep: capacity, latency, collateral, throughput vs threads
  for t in $THREADS; do
    for arm in $(shuf -e A0 U0 U1 S0 S1 S2); do
      one sweep $arm $rep -m ${MODE[$arm]} -t $t -n $N -k $K
    done
  done
  # 2. hostile: sibling thread hammering mmap/munmap (mmap_lock writer)
  for t in 1 4; do
    for arm in $(shuf -e A0 U1 S1 S2); do
      one hostile $arm $rep -m ${MODE[$arm]} -t $t -n $N -k $K -h
    done
  done
  # 3. bystander: unrelated getppid() loop while each probe is attached
  for att in $(shuf -e A0 U1 S1 S2); do
    one bystander $att $rep -m b -t 1 -n $((N * 4)) -k 0
  done
done

# 4. bpf_stats: cost of the BPF body alone (stats add ~2 clock reads per run, so separate pass)
sysctl -qw kernel.bpf_stats_enabled=1
for arm in U1 S1 S2; do STATS=1 one bpfstats $arm 1 -m ${MODE[$arm]} -t 1 -n $N -k $K; done
sysctl -qw kernel.bpf_stats_enabled=0
