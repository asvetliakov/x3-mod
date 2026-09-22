"""Count object_context rows (drawn nodes) whose node flags +0x12c carry 0x40000. Usage: LOG"""
import sys,re,collections
pat=re.compile(r' node=(\S+) .* model=(\S+) lod=(\S+) flags12c=(\S+) flags130=(\S+)')
c=collections.Counter(); mods=collections.Counter(); nodes=set(); allnodes=set()
with open(sys.argv[1], errors='replace') as fh:
    for line in fh:
        if not line.startswith('object_context '): continue
        m=pat.search(line)
        if not m: continue
        c['rows']+=1; allnodes.add(m.group(1))
        f=int(m.group(4),16)
        if f & 0x40000: c['flag40000']+=1; mods[m.group(2)]+=1; nodes.add(m.group(1))
        if int(m.group(5),16) & 0x100000: c['f130_100000']+=1
print(dict(c), 'distinct nodes', len(allnodes), 'flagged nodes', len(nodes))
print(mods.most_common(20))
