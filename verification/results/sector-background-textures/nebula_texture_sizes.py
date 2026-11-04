"""Dimensions and formats of the nebula background textures in the AP and FL installs (2026-11-03).
Run from the repository root: python3 verification/results/sector-background-textures/nebula_texture_sizes.py"""
import sys, struct, collections
from pathlib import Path
sys.path.insert(0, 'tools/analysis')
import sector_fog_census as sfc
for edition, root in (('ap', '/mnt/c/X3'), ('fl', '/mnt/c/X3FL')):
    a = sfc.Assets(Path(root), edition=edition)
    sizes = collections.Counter(); ex = []
    for key, entries in a.entries.items():
        p = entries[0]['path'].replace('\\', '/').lower()
        if 'nebula_' not in p or 'background' not in p: continue
        if p.endswith(('.bob', '.pbb', '.pbd', '.bod')): continue
        d = a.read_entry(entries[0])
        if d[:4] == b'DDS ':
            h, w = struct.unpack_from('<II', d, 12); fmt = d[84:88].decode('ascii', 'replace'); mips = struct.unpack_from('<I', d, 28)[0]
            sizes[(w, h, fmt, mips, p.rsplit('.',1)[-1])] += 1
        else:
            sizes[('non-dds', p.rsplit('.',1)[-1], d[:4])] += 1
        if len(ex) < 3: ex.append(p)
    print(edition, 'nebula background textures', sum(sizes.values()))
    for k, v in sizes.most_common(15): print('  ', v, k)
    print('  e.g.', ex)
