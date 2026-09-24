# Q7: row kinds (first token) present in run304/run305 but in none of run299/run300/run302/run303, and vice versa
for r in 299 300 302 303 304 305; do LC_ALL=C awk '{c[$1]++} END{for(k in c) print k, c[k]}' /tmp/x3-bottleX3-run$r/session-*.log | sort > /tmp/claude-501/rt_$r.txt; done
cut -d' ' -f1 /tmp/claude-501/rt_{299,300,302,303}.txt | sort -u > /tmp/claude-501/rt_old.txt
for r in 304 305; do echo "new in run$r:"; join -v1 /tmp/claude-501/rt_$r.txt /tmp/claude-501/rt_old.txt; done
cut -d' ' -f1 /tmp/claude-501/rt_{304,305}.txt | sort -u > /tmp/claude-501/rt_new.txt; echo "in run300 not in 304/305:"; join -v1 /tmp/claude-501/rt_300.txt /tmp/claude-501/rt_new.txt
