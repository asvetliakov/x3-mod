#!/usr/bin/env python3
"""Summarise the local X3Flag12cScan.java TSV (kept untracked): counts per access kind for non-stack
bases, and every site whose immediate, byte test or sign idiom can involve bit 31 of +0x12c.
Usage: classify_scan.py scan12c.tsv"""
import sys, re
rows = [l.rstrip('\n').split('\t') for l in open(sys.argv[1])]
rows = [r for r in rows if 'ESP' not in r[3]]
kinds = {}
for r in rows: kinds[r[0]] = kinds.get(r[0], 0) + 1
print('non-stack sites', len(rows), kinds, 'functions', len({r[1] for r in rows}))
def imm(s):
    m = re.search(r',(0x[0-9a-f]+)$', s); return int(m.group(1), 16) if m else None
for k, fn, a, ins, tr in rows:
    v = imm(ins); why = None
    if k == 'read' and (v is None or v & 0x80000000 or ('+ 0x12f' in ins and v & 0x80)):
        why = 'direct test, mask %s' % ('register' if v is None else hex(v))
    if k == 'write' and ins.startswith(('OR', 'MOV', 'BTS')) and v is not None and v & 0x80000000:
        why = 'immediate sets bit 31'
    if k == 'write' and ins.startswith('AND') and v is not None and not v & 0x80000000:
        why = 'immediate clears bit 31'
    if k == 'write' and v is None:
        why = 'register-operand write (source traced by hand)'
    if k == 'load':
        t = tr.split(' ; ')[1:9]
        sign = [x for x in t if re.search(r'\b(JS|JNS|JL|JGE|SHR|SAR|BT|ROL)\b', x) or re.search(r'0x8[0-9a-f]{7}', x)]
        if sign: why = 'load then ' + ' | '.join(x.split(' ', 1)[1] for x in sign)
    if why: print('%s %s %-42s %s' % (fn, a, ins.split(' ptr ')[0] + ' ' + ins.split(']')[-1] if ']' in ins else ins, why))
