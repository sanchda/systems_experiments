#!/bin/bash
# tid comm utime+stime(ticks) last_cpu
for f in /proc/[0-9]*/task/[0-9]*/stat; do
  read -r l < "$f" 2>/dev/null || continue
  tid=${l%% *}; rest=${l##*) }; comm=${l#*(}; comm=${comm%%)*}
  set -- $rest; echo "$tid ${comm// /_} $(( ${12} + ${13} )) ${37}"
done
