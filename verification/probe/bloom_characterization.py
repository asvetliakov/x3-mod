"""Independent FP16-store/sampler controls for the bloom fixture, not production.

Candidate models are fixed before held-out evaluation. The nearest-up result
mode and one-ULP 2D envelope were added after inspecting training residuals;
the unchanged held-outs then validated them. Model identification uses only
calibration ramps/endpoints; held-out controls cannot select a model.
No bloom readback participates. A non-unique or unrecognized result rejects
characterization instead of widening the bloom tolerance.
"""
import hashlib
import math
from pathlib import Path
import random
import struct

PHASE_WIDTH = 1024
PHASE_HEIGHT = 4
TRAIN_PHASES = 2048
TRAIN_STORES = 192
TRAIN_ENDPOINTS = 2


def f32(v):
    return struct.unpack('<f', struct.pack('<f', v))[0]


def from_half(bits):
    return struct.unpack('<e', struct.pack('<H', bits))[0]


def fp16(v, rounding='nearest_even', flush_subnormal=False):
    bits = struct.unpack('<H', struct.pack('<e', v))[0]
    nearest = from_half(bits)
    if rounding == 'toward_zero' and abs(from_half(bits)) > abs(v) and (bits & 0x7fff):
        bits -= 1
    elif rounding == 'nearest_up' and nearest < v:
        upper_bits = bits + (1 if v >= 0 else -1)
        upper = from_half(upper_bits)
        if v-nearest == upper-v:
            bits = upper_bits
    elif rounding not in ('nearest_even', 'toward_zero', 'nearest_up'):
        raise ValueError('unsupported FP16 rounding')
    if flush_subnormal and (bits & 0x7c00) == 0:
        bits &= 0x8000
    return from_half(bits)


def make_inputs():
    # Quarter/tie/three-quarter points around exact half values across the
    # FP16 range, with positive/negative/exact-neighbor channels. Shuffle
    # deterministic recipes to distribute magnitudes across train/holdout.
    recipes = [(bits, fraction) for bits in (0,1,2,15,511,1023,1024,1025,2047,
                4096,8192,12288,15360,16384,20480,24576,28672,30718)
               for fraction in (.125,.25,.5,.75,.875)]
    rng = random.Random(93015)
    rng.shuffle(recipes)
    stores = []
    for i in range(256):
        bits, fraction = recipes[i % len(recipes)]
        lo, hi = from_half(bits), from_half(bits + 1)
        value = f32(lo + (hi-lo) * fraction)
        stores.append((value, -value, lo, hi))
    # Calibration phases include exact fixed-point ties and nearby values;
    # held-out phases use independent seeds, boundaries and denser fractions.
    phases = []
    for i in range(TRAIN_PHASES):
        if i < 1024:
            phase = i / 1023
        elif i < 1792:
            tie = ((i - 1024) // 3 + .5) / 256
            phase = tie + (-1,0,1)[(i - 1024) % 3] / 65536
        else:
            phase = rng.random()
        phases.append((f32((phase+.5)/2), .5, 0., 0.))
    held = random.Random(190315)
    for i in range(PHASE_WIDTH * PHASE_HEIGHT - TRAIN_PHASES):
        phase = i / 1535 if i < 1536 else held.uniform(-.1, 1.1)
        phases.append((f32((phase+.5)/2), .5, 0., 0.))
    endpoints = [
        [(0.,0.,16.,0.), (1.,65504.,17.,2**-13)],
        [(0.,1000.,.1,2**-10), (1.,4000.,.2,2**-9)],
        [(0.,12.5,.5,32752.), (1.,13.75,2.,65504.)],
        [(0.,2**-16,2048.,.03125), (1.,2**-12,3072.,.0625)],
    ]
    endpoints = [[tuple(fp16(v) for v in pixel) for pixel in pair] for pair in endpoints]
    phases2d = []
    rng2d = random.Random(93016)
    for i in range(PHASE_WIDTH * PHASE_HEIGHT):
        if i < TRAIN_PHASES:
            x,y = (i%64)/63,(i//64)/31
        else:
            x,y = rng2d.uniform(-.1,1.1),rng2d.uniform(-.1,1.1)
        phases2d.append((f32((x+.5)/2),f32((y+.5)/2),0.,0.))
    endpoints2d = [
        [(0.,0.,16.,.03125),(1.,65504.,17.,.5),(0.,64.,25.,1.),(1.,32000.,3.,16.)],
        [(0.,.1,2048.,0.),(0.,.2,1000.,1.),(1.,.001,32768.,2.),(1.,2.,16384.,4.)],
        [(0.,12.5,.5,32752.),(.25,13.75,2.,65504.),(.75,1000.,32.,2048.),(1.,.125,12.,64.)],
        [(1.,2**-16,2048.,.03125),(.5,2**-12,3072.,.0625),(.75,2**-14,4096.,.0078125),(0.,2**-10,65504.,.125)],
    ]
    endpoints2d = [[tuple(fp16(v) for v in pixel) for pixel in quad] for quad in endpoints2d]
    return dict(stores=stores, phases=phases, endpoints=endpoints,
                phases2d=phases2d,endpoints2d=endpoints2d)


def write_inputs(path):
    inputs = make_inputs()
    with Path(path).open('wb') as file:
        file.write(b'X3BCH002' + struct.pack('<4I', len(inputs['stores']), PHASE_WIDTH,
                                           PHASE_HEIGHT, len(inputs['endpoints'])))
        for name in ('stores', 'phases'):
            for pixel in inputs[name]:
                file.write(struct.pack('<4f', *pixel))
        for pair in inputs['endpoints']:
            for pixel in pair:
                file.write(struct.pack('<4e', *pixel))
        for pixel in inputs['phases2d']:
            file.write(struct.pack('<4f',*pixel))
        for quad in inputs['endpoints2d']:
            for pixel in quad:
                file.write(struct.pack('<4e',*pixel))


def read_pixels(path, code, count):
    data = Path(path).read_bytes()
    stride = struct.calcsize('<4' + code)
    if len(data) != count * stride:
        raise ValueError('characterization readback size mismatch: ' + str(path))
    pixels = list(struct.iter_unpack('<4' + code, data))
    if not all(math.isfinite(v) for pixel in pixels for v in pixel):
        raise ValueError('nonfinite characterization output')
    return pixels


def phase_weight(u, model):
    phase = min(max(f32(f32(u * 2) - .5), 0.), 1.)
    bits = model['fraction_bits']
    if bits:
        value = phase * (1 << bits)
        mode = model['fraction_rounding']
        integer = math.floor(value) if mode == 'toward_zero' else round(value) if mode == 'nearest_even' else math.floor(value+.5)
        phase = integer / (1 << bits)
    return phase


def sample_pair(pair, u, model):
    weight = phase_weight(u, model)
    values = [f32(a*(1-weight)+b*weight) for a,b in zip(*pair)]
    if model['result_precision'] != 'float32':
        return tuple(fp16(v, model['result_precision'], model['flush_subnormal']) for v in values)
    return tuple(values)


def sample_quad(quad,u,v,model,path):
    x,y = phase_weight(u,model),phase_weight(v,model)
    def rounded(values):
        if model['result_precision']=='float32':
            return tuple(f32(v) for v in values)
        return tuple(fp16(f32(v),model['result_precision'],model['flush_subnormal']) for v in values)
    if path=='single':
        return rounded(a*(1-x)*(1-y)+b*x*(1-y)+c*(1-x)*y+d*x*y for a,b,c,d in zip(*quad))
    if path=='horizontal_then_vertical':
        top=rounded(a*(1-x)+b*x for a,b in zip(quad[0],quad[1]))
        bottom=rounded(a*(1-x)+b*x for a,b in zip(quad[2],quad[3]))
        return rounded(a*(1-y)+b*y for a,b in zip(top,bottom))
    if path=='vertical_then_horizontal':
        left=rounded(a*(1-y)+b*y for a,b in zip(quad[0],quad[2]))
        right=rounded(a*(1-y)+b*y for a,b in zip(quad[1],quad[3]))
        return rounded(a*(1-x)+b*x for a,b in zip(left,right))
    raise ValueError('unknown bilinear interpolation path')


def models():
    for bits in (0, *range(4,13)):
        for fraction in (('exact',) if bits == 0 else ('toward_zero','nearest_even','nearest_up')):
            for precision in ('float32','nearest_even','nearest_up','toward_zero'):
                # A filtered FP16 output model can independently flush tiny
                # subnormals; full-float output has no FP16 subnormal domain.
                for flush in ((False,) if precision == 'float32' else (False, True)):
                    yield dict(fraction_bits=bits, fraction_rounding=fraction,
                               result_precision=precision, flush_subnormal=flush)


def sample_matches(actual, expected):
    # Independent FP32-target controls use a strict float32 arithmetic allowance,
    # far below one FP16 ULP. This is not the bloom .002+.003*v tolerance.
    return all(abs(a-b) <= max(2**-25, abs(b)*2**-21) for a,b in zip(actual,expected))


def half_ulp(value):
    """Conservative adjacent finite FP16 spacing, including powers/subnormals."""
    bits=struct.unpack('<H',struct.pack('<e',abs(value)))[0]
    center=from_half(bits)
    gaps=[]
    if bits:
        gaps.append(center-from_half(bits-1))
    if bits<0x7bff:
        gaps.append(from_half(bits+1)-center)
    return max(gaps)


def analyze(directory, cases, max_width, max_height):
    directory = Path(directory)
    inputs = make_inputs()
    stores = read_pixels(directory / 'characterization_store.rgba16f', 'e', len(inputs['stores']))
    store_candidates = []
    for rounding in ('nearest_even', 'toward_zero'):
        for flush in (False, True):
            if all(actual == tuple(fp16(v, rounding, flush) for v in pixel)
                   for actual,pixel in zip(stores[:TRAIN_STORES], inputs['stores'][:TRAIN_STORES])):
                store_candidates.append(dict(rounding=rounding, flush_subnormal=flush))
    report = dict(passed=False, calibration_uses_bloom_outputs=False,
                  store_candidates=store_candidates, sampler_candidates=[],
                  train_phases=TRAIN_PHASES, train_stores=TRAIN_STORES, train_endpoints=TRAIN_ENDPOINTS)
    if len(store_candidates) != 1:
        report['error'] = 'Store calibration did not identify exactly one model'
        return report
    store = store_candidates[0]
    report['store_model'] = store
    store_holdout = all(actual == tuple(fp16(v, **store) for v in pixel)
                       for actual,pixel in zip(stores[TRAIN_STORES:], inputs['stores'][TRAIN_STORES:]))
    report['store_heldout_passed'] = store_holdout
    samples = []
    for i,pair in enumerate(inputs['endpoints']):
        point = read_pixels(directory / f'characterization_point_{i}.rgba32f', 'f', 2)
        if point != pair:
            report['error'] = 'Point-sampled endpoint twin did not preserve exact uploaded FP16 values'
            return report
        samples.append(read_pixels(directory / f'characterization_sample_{i}.rgba32f', 'f', len(inputs['phases'])))
    for model in models():
        if all(sample_matches(samples[e][i], sample_pair(inputs['endpoints'][e], phase[0], model))
               for e in range(TRAIN_ENDPOINTS)
               for i,phase in enumerate(inputs['phases'][:TRAIN_PHASES])):
            report['sampler_candidates'].append(model)
    if len(report['sampler_candidates']) != 1:
        report['error'] = 'Sampler calibration did not identify exactly one model'
        return report
    model = report['sampler_candidates'][0]
    report['sampler_model'] = model
    heldout_failures = []
    heldout_count = 0
    for e,pair in enumerate(inputs['endpoints']):
        start = TRAIN_PHASES if e < TRAIN_ENDPOINTS else 0
        for i in range(start, len(inputs['phases'])):
            heldout_count += 1
            expected = sample_pair(pair, inputs['phases'][i][0], model)
            if not sample_matches(samples[e][i], expected):
                heldout_failures.append(dict(endpoint=e, phase=i, actual=samples[e][i], expected=expected))
    report['sampler_heldout_count'] = heldout_count
    report['sampler_heldout_failure_count'] = len(heldout_failures)
    report['sampler_heldout_failures'] = heldout_failures[:8]
    samples2d=[]
    for i,quad in enumerate(inputs['endpoints2d']):
        if read_pixels(directory/f'characterization_point2d_{i}.rgba32f','f',4)!=quad:
            report['error']='2D point twin did not preserve uploaded FP16 values'
            return report
        samples2d.append(read_pixels(directory/f'characterization_sample2d_{i}.rgba32f','f',len(inputs['phases2d'])))
    paths=[]
    for path in ('single','horizontal_then_vertical','vertical_then_horizontal'):
        if all(sample_matches(samples2d[e][i],sample_quad(inputs['endpoints2d'][e],phase[0],phase[1],model,path))
               for e in range(TRAIN_ENDPOINTS) for i,phase in enumerate(inputs['phases2d'][:TRAIN_PHASES])):
            paths.append(path)
    report['sampler2d_candidates']=paths
    # Exact internal 2D arithmetic is intentionally not guessed. Following
    # training-only inspection of the rejected initial 2D run, qualify the
    # natural one-FP16-ULP envelope around the single final-round operator.
    # Held-out controls cannot select or widen this bound. Report chronology.
    report['sampler2d_contract']='quantized bilinear within one FP16 ULP of single final round'
    report['sampler2d_exact_path_identified']=len(paths)==1
    report['sampler2d_envelope_origin']='introduced after initial 2D training residual inspection; held-out controls unchanged'
    report['sampler2d_path']=paths[0] if len(paths)==1 else None
    heldout2d=[]
    train2d=[]
    max_train_ulps=0.
    max_heldout_ulps=0.
    for e,quad in enumerate(inputs['endpoints2d']):
        for i in range(len(inputs['phases2d'])):
            u,v=inputs['phases2d'][i][:2]
            expected=sample_quad(quad,u,v,model,'single')
            training=e<TRAIN_ENDPOINTS and i<TRAIN_PHASES
            error_ulps=max(abs(a-b)/half_ulp(b) for a,b in zip(samples2d[e][i],expected))
            if training: max_train_ulps=max(max_train_ulps,error_ulps)
            else: max_heldout_ulps=max(max_heldout_ulps,error_ulps)
            if error_ulps>1:
                (train2d if training else heldout2d).append(dict(endpoint=e,phase=i,actual=samples2d[e][i],expected=expected,error_ulps=error_ulps))
    report['sampler2d_train_failure_count']=len(train2d)
    report['sampler2d_max_train_error_ulps']=max_train_ulps
    report['sampler2d_max_heldout_error_ulps']=max_heldout_ulps
    report['sampler2d_heldout_failure_count']=len(heldout2d)
    report['sampler2d_heldout_failures']=heldout2d[:8]
    sizes = set()
    # Avoid importing the fixture runner (which imports this module).
    for case in cases:
        width, height = len(case['image'][0]), len(case['image'])
        if width > max_width or height > max_height:
            continue
        sizes.add((width,height))
        for _ in range(case['params'].levels):
            width,height = (width+1)//2,(height+1)//2
            sizes.add((width,height))
            if width == height == 1:
                break
    uv_reports = []
    for width,height in sorted(sizes):
        path = directory / f'characterization_uv_{width}x{height}.rgba32f'
        pixels = read_pixels(path, 'f', width*height)
        max_error = max(max(abs(p[0]*width-(i%width+.5)),abs(p[1]*height-(i//width+.5)))
                        for i,p in enumerate(pixels))
        valid = max_error <= .01 and all(p[2:] == (0.,1.) for p in pixels)
        uv_reports.append(dict(width=width,height=height,max_pixel_center_error=max_error,passed=valid,
                               file=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    report['uv_maps'] = uv_reports
    report['passed'] = store_holdout and not heldout_failures and not train2d and not heldout2d and all(p['passed'] for p in uv_reports)
    if not report['passed']:
        report['error'] = 'Independent held-out controls or UV map failed'
    return report
