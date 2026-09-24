# row kinds (first token) in run308 not in run304 and vice versa
for r in 304 308; do LC_ALL=C awk '{c[$1]++} END{for(k in c) print k, c[k]}' /tmp/x3-bottleX3-run$r/session-*.log | LC_ALL=C sort > /tmp/claude-501/rt_$r.txt; done
echo "new in run308:"; LC_ALL=C join -v1 /tmp/claude-501/rt_308.txt /tmp/claude-501/rt_304.txt
echo "in run304 not in run308:"; LC_ALL=C join -v2 /tmp/claude-501/rt_308.txt /tmp/claude-501/rt_304.txt
