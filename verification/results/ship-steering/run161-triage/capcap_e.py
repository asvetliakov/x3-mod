"""Run161 Q1: E-verdict rows (non-member pairs only; member pairs log no E row in stage A) where A and B are both capitals (caps.txt),
by verdict and (A_order, B_order); plus the session counters e_suppressed_ctl / e_pass_ctl (member-pair E calls, last summary row).
Usage: python3 -I capcap_e.py rows.txt"""
import sys, re, collections as C
caps = {l.split()[0]: l.split()[1] for l in open('caps.txt')}
c = C.Counter(); co = C.Counter(); last = None
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_avoid_e_verdict '):
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        if d['A'] in caps and d['B'] in caps: c[d['verdict']] += 1; co[(d['A_order'], d['B_order'], d['verdict'])] += 1
    elif l.startswith('ship_steering_summary '): last = l
print('capital-capital E rows (non-member pairs) by verdict:', dict(c))
print('by (A_order,B_order,verdict):', co.most_common(12))
d = dict(re.findall(r'(\w+)=(\S+)', last))
print('member-pair E calls (cumulative counters): e_suppressed_ctl', d['e_suppressed_ctl'], 'e_pass_ctl', d['e_pass_ctl'],
      'share passed %.1f%%' % (100 * int(d['e_pass_ctl']) / (int(d['e_pass_ctl']) + int(d['e_suppressed_ctl']))))
