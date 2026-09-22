"""Drawn rows (object_context) of the pilot bodies by LOD and node+0x130 & 0x100000. Usage: LOG"""
import sys, re, collections
pat = re.compile(r' model=(\S+) lod=(\S+) flags12c=(\S+) flags130=(\S+)')
pilot = {'00004f72': 'argon_TL', '00004f75': 'argon_M1', '00004f76': 'argon_M2', '000053b8': 'military_outpost_middleb'}
c = collections.Counter()
with open(sys.argv[1], errors='replace') as fh:
    for line in fh:
        if not line.startswith('object_context '): continue
        m = pat.search(line)
        if m and m.group(1) in pilot:
            c[(pilot[m.group(1)], int(m.group(2), 16), bool(int(m.group(4), 16) & 0x100000))] += 1
for k, v in sorted(c.items()): print(v, *k)
