"""Run 80 A (run305): per-draw rows of the ODS parts (usc_dock_e_*) in chosen burst frames. Streaming, read-only.
Usage: python3 ods_burst_draws.py LOG FRAMES(comma) [BODY_SUBSTR=usc_dock_e]
Per frame prints each ODS draw: body lod prims ps vb/ib s0..s5 (identity:WxH:levels) and the PS float registers that
differ between the draws (rows for the first frame of each burst); then per burst whether frames 2..8 repeat frame 1's (body,lod,prims,ps,textures)."""
import sys, re, struct
from collections import defaultdict
KV = re.compile(r'(\w+)=(\S+)')
log, frames = sys.argv[1], set(sys.argv[2].split(','))
sub = sys.argv[3] if len(sys.argv) > 3 else 'usc_dock_e'
def f4(bits):
    return tuple(round(struct.unpack('<f', bytes.fromhex(b)[::-1])[0], 3) for b in bits.split(',')[:4])
model_body, draws, cur = {}, [], None
with open(log, errors='replace') as fh:
    for line in fh:
        if line.startswith('cull_census device'):
            d = dict(KV.findall(line))
            if d.get('body', '-') != '-': model_body[d['model']] = d['body'].rsplit('\\', 1)[-1]
            continue
        if line.startswith('draw device'):
            d = dict(KV.findall(line))
            cur = None
            if d['frame'] in frames:
                cur = dict(frame=int(d['frame']), index=int(d['index']), prims=d['primitives'], ps=d['ps'][:8], tex={}, lv={},
                           psf={}, psb={}, st={}); draws.append(cur)
            continue
        if cur is None: continue
        t = line[:16]
        if t.startswith('object_context'):
            d = dict(KV.findall(line)); cur['model'] = d['model']; cur['lod'] = int(d['lod'], 16); cur['node'] = d['node']
        elif t.startswith('texture stage'):
            d = dict(KV.findall(line)); cur['tex'][int(d['stage'])] = d['identity']; cur['lv'][int(d['stage'])] = d.get('levels')
        elif t.startswith('texture_desc'):
            d = dict(KV.findall(line)); s = int(d['stage'])
            cur['tex'][s] = f"{cur['tex'].get(s)}:{d['w']}x{d['h']}:L{cur['lv'].get(s)}"
        elif t.startswith('constant kind=ps'):
            d = dict(KV.findall(line))
            if d['type'] == 'f': cur['psf'][int(d['reg'])] = f4(d['bits'])
            elif d['type'] == 'b': cur['psb'][int(d['reg'])] = d['values']
        elif t.startswith('state id='):
            d = dict(KV.findall(line))
            if d['id'] in ('15', '19', '20', '24', '27'): cur['st'][d['id']] = d['value']
        elif t.startswith('stream slot=0'):
            cur['vb'] = dict(KV.findall(line))['identity']
        elif t.startswith('indices'):
            cur['ib'] = dict(KV.findall(line))['identity']
        elif t.startswith('object_bounds'):
            cur['at'] = dict(KV.findall(line)).get('alpha_tested')
sel = [d for d in draws if sub in model_body.get(d.get('model'), '')]
regs = sorted({r for d in sel for r in d['psf']})
vary = [r for r in regs if len({d['psf'].get(r) for d in sel}) > 1]
sig = lambda d: (model_body[d['model']], d['lod'], d['prims'], d['ps'], tuple(sorted(d['tex'].items())), tuple(sorted(d['psf'].items())))
byf = defaultdict(list)
for d in sel: byf[d['frame']].append(d)
print('ps float regs varying across the printed draws:', vary)
fl = sorted(byf)
for f in fl:
    print(f'== frame {f} ods_draws={len(byf[f])}')
    if f - 1 in byf: continue  # draws printed for the first frame of each burst only
    for d in byf[f]:
        st = ' '.join(f's{s}={d["tex"].get(s, "-")}' for s in range(7))
        pc = ' '.join(f'c{r}={d["psf"].get(r)}' for r in vary)
        print(f'  #{d["index"]} {model_body[d["model"]]} lod={d["lod"]} prims={d["prims"]} ps={d["ps"]} vb={d.get("vb")} ib={d.get("ib")}'
              f' at={d.get("at")} rs(15at,19src,20dst,24ref,27ab)={d["st"]} {st} {pc} b0={d["psb"].get(0)} b1={d["psb"].get(1)}')
for first in fl:
    rest = [f for f in fl if first < f <= first + 7]
    if not rest or first - 1 in fl: continue
    same = all([sig(x) for x in byf[f]] == [sig(x) for x in byf[first]] for f in rest)
    print(f'burst {first}: frames {len(rest)+1}, all frames repeat frame {first} draws (body,lod,prims,ps,tex,psf): {same}')
