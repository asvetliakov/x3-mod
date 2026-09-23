import sys, math, random, importlib.util
import numpy as np
sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[4] / 'tools/analysis'))
import lod_atlas as new
spec = importlib.util.spec_from_file_location('old', sys.argv[1]); old = importlib.util.module_from_spec(spec); spec.loader.exec_module(old)
rng = random.Random(20260923)
ident = []; worst = 0.0; n = 0; bad = []; sums = 0.0
def case(k0, k1, scale, origin, content, lo, span, src_n):
    global worst, n, sums
    a = old.level_weights(k0, k1, scale, origin, content, lo, span, src_n)
    b = new.level_weights(k0, k1, scale, origin, content, lo, span, src_n)
    n += 1
    if a.shape != b.shape: bad.append(('shape', k0,k1,scale,origin,content,lo,span,src_n)); return
    d = float(np.abs(a - b).max()) if a.size else 0.0
    worst = max(worst, d)
    rs = float(np.abs(b.sum(1) - 1).max()) if b.size else 0.0
    sums = max(sums, rs)
    ident.append(d == 0.0)
    if d > 1e-5: bad.append((d, k0,k1,scale,origin,content,lo,span,src_n))
for _ in range(600):
    src_n = rng.choice([1,2,3,4,7,8,16,31,64,128,256,512,1024])
    content = rng.choice([1,2,5,16,33,64,100,256,1000,2048])
    scale = rng.choice([1,2,4,8,16,32,64,128])
    origin = rng.randint(0, 200)
    k0 = rng.randint(0, 5); k1 = k0 + rng.randint(0, max(1, (content + 2*8)//scale + 2))
    lo = rng.choice([0.0, 0.5, -0.25, rng.uniform(-20, 20), -13.6, 1e-9, 0.999999])
    span = rng.choice([1.0, 0.5, rng.uniform(1e-4, 3), rng.uniform(3, 60), 1e-6, 2.0])
    if content * 0 == 0 and span * src_n * scale / content > 5e4: span = 1.0
    case(k0, k1, scale, origin, content, lo, span, src_n)
# edge cases: exact integer boundaries, negative lo, huge span modest size
for src_n in (1, 2, 4, 64):
    for lo in (0.0, 1.0, -1.0, 0.25):
        for span in (1.0, 2.0, 0.25, 10.0):
            case(0, 16, 1, 0, 16, lo, span, src_n); case(0, 4, 4, 2, 16, lo, span, src_n)
print(f'cases {n} shape/threshold failures {len(bad)} max|old-new| {worst:.3e} max|rowsum-1| {sums:.3e}')
for b in bad[:5]: print('  bad', b)
print('bit-identical cases', sum(ident), 'of', len(ident))
