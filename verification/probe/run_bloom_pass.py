#!/usr/bin/env python3
"""Actual BloomPass GPU/state/failure fixture. Run through wine_lock.py.

--build-only expands authored sources, writes deterministic inputs and links
production BloomPass, but never executes Wine or publishes a GPU pass.
"""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import bloom_pass_fixture_build as builder
import bottle
import run_bloom_filter as filtering
from game_guard import game_running
ROOT = builder.ROOT
sys.path.insert(0, str(ROOT/'verification/analysis'))
import test_bloom_composition as oracle
sys.path.insert(0, str(ROOT/'tools/analysis'))
import agx_reference as agx_ref
ref = filtering.ref
NAMES = ('quad_vs', *(f'bloom_{name}_ps' for name in filtering.KERNELS),
         'bloom_agx_ps', 'taa_sharpen_ps', 'hdr_writeback_ps')
# Fixed before first execution. Compare final 8-bit RGB to independent double
# AgX + nine-tap bloom + RCAS, without FP16 intermediate quantization (fused
# ideal). Three codes cover FP16 storage, shader arithmetic, sampler and UNORM
# conversion jointly for this moderate bounded corpus, not a universal bound.
MAX_CODE_ERROR = 3
# The case repeated after the native Reset: pinned to the odd-geometry,
# generic-extraction authored case (9x7, decode none), matching kResetCase in
# bloom_pass_fixture.cpp, so appended cases cannot move it silently.
RESET_CASE_INDEX = 35


def make_cases():
    cases = []
    for mode in ref.DECODE_MODES:
        for kind in ('constant', 'structured'):
            w, h = (8, 6) if kind == 'constant' else (9, 7)
            image = [[(2., .5, .125, .375) if kind == 'constant' else
                      ((x % 4 + 1)/4, (y % 3 + 1)/8, (1 + ((x+y) % 5))/16, .375)
                      for x in range(w)] for y in range(h)]
            for strength in (0., .5):
                for sharp in (0., .75):
                    cases.append(dict(mode=mode, kind=kind, width=w, height=h, levels=3,
                                      strength=strength, sharp=sharp, threshold=0., exposure=1.,
                                      authored_glow_gain=0., highlight_gain=.05, scatter=.7,
                                      source_clamp=0., image=image))
    # Six bounded authored configurations, each paired as final strength 0/1
    # to exercise the runtime F10 contribution gate against the same input.
    configurations = (
        ('gamma2.2', 8, 6, 0., .1), ('gamma2.2', 9, 7, 1.5, .2),
        ('srgb', 8, 6, 2., .1), ('srgb', 9, 7, 0., .2),
        ('none', 8, 6, 1.5, .1), ('none', 9, 7, 2., .2),
    )
    for mode, w, h, ev, gain in configurations:
        image = []
        for y in range(h):
            row = []
            for x in range(w):
                if (x, y) == (w // 2, h // 2):
                    pixel = (.18, .04, .01, 1.)
                elif (x, y) == (0, 0):
                    pixel = (2., .5, .125, 0.)
                else:
                    alpha = (0., .5, 1.)[(x + 2 * y) % 3]
                    if y == 0 and x == 1: alpha = math.nan
                    if y == 0 and x == 2: alpha = -math.inf
                    if y == 0 and x == 3: alpha = math.inf
                    pixel = ((x % 5 + 1) / 5, (y % 4 + 1) / 6,
                             (1 + (x + y) % 6) / 12, alpha)
                row.append(pixel)
            image.append(row)
        for strength in (0., 1.):
            cases.append(dict(mode=mode, kind='authored', width=w, height=h, levels=3,
                              strength=strength, sharp=0., threshold=1., exposure=2 ** ev,
                              authored_glow_gain=gain, highlight_gain=.05, scatter=.7,
                              source_clamp=0., image=image))
    cases.extend(clamp_cases())
    cases.extend(dither_cases())
    return cases


# X3M_HDR_DITHER (docs/verification/hdr-scene-path.md, "Display dither"): the
# constant 8x6 gamma2.2 image with bloom, c8.z = 1/255, unsharpened (the direct
# A8R8G8B8 candidate draw dithers) and sharpened (the FP16 staging draw gets 0,
# taa_sharpen applies c23.w). The oracle adds the same static pattern.
def dither_cases():
    image = [[(2., .5, .125, .375) for x in range(8)] for y in range(6)]
    return [dict(mode='gamma2.2', kind='dither', width=8, height=6, levels=3, strength=.5, sharp=sharp,
                 threshold=0., exposure=1., authored_glow_gain=0., highlight_gain=.05, scatter=.7,
                 source_clamp=0., dither=agx_ref.DITHER_AMPLITUDE, image=image) for sharp in (0., .75)]


# The live compositor constants (capture.cpp: authored glow .375, highlight
# .05, scatter .65, threshold 1, knee .5, five levels, EV ceiling +1.3) at the
# bolt geometry of docs/architecture/bloom-falloff.md: a 6x40 bar with alpha 0,
# so only the thresholded highlight term feeds the pyramid. No other case pins
# these constants, five effective levels, or the source clamp.
CLAMP_WIDTH, CLAMP_HEIGHT, CLAMP_BAR = 64, 40, (29, 35)
CLAMP_EXPOSURE = 2 ** 1.3


def clamp_cases():
    def bar(code, alpha):
        return [[(code, code, code, alpha) if CLAMP_BAR[0] <= x < CLAMP_BAR[1] else (0., 0., 0., alpha)
                 for x in range(CLAMP_WIDTH)] for _ in range(CLAMP_HEIGHT)]
    # A code-1 source is at the clamp, so 1.0 must reproduce the unbounded feed
    # exactly; the code-5 source is 42x brighter decoded and must fall back onto
    # the code-1 result at clamp 1, with clamp 2 strictly between. Alpha 0 is the
    # thresholded highlight lane (the additive bolt); alpha 1 is the authored
    # glow lane (a * 0.375), which the clamp must bound identically.
    configurations = (('clamp_ref_none', 1., 0., 0.), ('clamp_ref_one', 1., 1., 0.),
                      ('clamp_hot_none', 5., 0., 0.), ('clamp_hot_one', 5., 1., 0.),
                      ('clamp_hot_two', 5., 2., 0.),
                      ('clamp_authored_ref_none', 1., 0., 1.), ('clamp_authored_ref_one', 1., 1., 1.),
                      ('clamp_authored_hot_none', 5., 0., 1.), ('clamp_authored_hot_one', 5., 1., 1.))
    return [dict(mode='gamma2.2', kind='clamp', label=label, width=CLAMP_WIDTH, height=CLAMP_HEIGHT,
                 levels=5, strength=1., sharp=0., threshold=1., exposure=CLAMP_EXPOSURE,
                 authored_glow_gain=.375, highlight_gain=.05, scatter=.65, source_clamp=clamp,
                 alpha=alpha, image=bar(code, alpha))
            for label, code, clamp, alpha in configurations]


def write_cases(cases, path):
    with path.open('wb') as f:
        f.write(b'X3BP0004' + struct.pack('<I',len(cases)))
        for c in cases:
            f.write(struct.pack('<4I9f',c['width'],c['height'],ref.DECODE_MODES.index(c['mode']),
                                c['levels'],c['strength'],c['sharp'],c['threshold'],c['exposure'],
                                c['authored_glow_gain'],c['highlight_gain'],c['scatter'],
                                c['source_clamp'],c.get('dither',0.)))
            for row in c['image']:
                for p in row:
                    f.write(struct.pack('<4e',*p))


def expected(c):
    p = ref.Params(levels=c['levels'], threshold=c['threshold'], strength=c['strength'],
                   scatter=c['scatter'], authored_glow_gain=c['authored_glow_gain'],
                   highlight_gain=c['highlight_gain'])
    # The source clamp bounds the extraction feed only; the displayed scene in
    # composition() keeps its unbounded HDR value.
    bloom = ref.bloom(c['image'],p,exposure=c['exposure'],clamp_max=c['source_clamp'],mode=c['mode'])
    display = [[oracle.composition(e,b,c['strength'],exposure=c['exposure'],mode=c['mode'])
                for e,b in zip(row,blur)] for row,blur in zip(c['image'],bloom)]
    if not c['sharp']:
        out=[[p[:3] for p in row] for row in display]
    else:
        w,h=c['width'],c['height']
        def get(x,y): return display[min(h-1,max(0,y))][min(w-1,max(0,x))]
        out=[[oracle.rcas_cross([get(x,y-1),get(x-1,y),get(x,y),get(x+1,y),get(x,y+1)],
                                2**(-2*(1-c['sharp']))) for x in range(w)] for y in range(h)]
    if c.get('dither',0.):
        # The static display dither of the final 8-bit write, once, after RCAS.
        out=[[tuple(agx_ref.dither_display(v,x,y,c['dither']) for v in p[:3]) for x,p in enumerate(row)]
             for y,row in enumerate(out)]
    return out


def compare(c,path,baseline_path):
    data=path.read_bytes(); baseline=baseline_path.read_bytes()
    if len(data)!=c['width']*c['height']*4 or len(baseline)!=len(data):
        raise ValueError('Readback length mismatch')
    errors=[]; alpha_errors=0
    for i,p in enumerate(p for row in expected(c) for p in row):
        b,g,r,a=data[i*4:i*4+4]
        alpha_errors+=a!=baseline[i*4+3]
        errors.extend(abs(actual-oracle.code8(wanted)) for actual,wanted in zip((r,g,b),p))
    result=dict(passed=max(errors)<=MAX_CODE_ERROR and alpha_errors==0,
                max_code_error=max(errors), mean_code_error=sum(errors)/len(errors),
                channels=len(errors), alpha_errors=alpha_errors, sha256=filtering.digest(path),
                original_sha256=filtering.digest(baseline_path))
    if c.get('dither',0.):
        # The pattern is present: the constant image does not store one code per channel,
        # and the mean signed error against the undithered oracle stays near zero.
        plain=[p for row in expected(dict(c,dither=0.)) for p in row]
        signed=[]; distinct=[set(),set(),set()]
        for i,p in enumerate(plain):
            b,g,r,a=data[i*4:i*4+4]
            for k,(actual,wanted) in enumerate(zip((r,g,b),p)):
                signed.append(actual-255.*wanted); distinct[k].add(actual)
        result.update(dither=c['dither'], mean_signed_error_vs_undithered=sum(signed)/len(signed),
                      codes_per_channel=[len(s) for s in distinct],
                      max_code_error_vs_undithered=max(abs(v) for v in signed))
        result['passed']=result['passed'] and max(len(s) for s in distinct)>1
    return result


# The clamp identities hold where the displayed scene is identical between two
# clamp cases, i.e. on the black background: inside the bar the scene keeps its
# unbounded HDR code by design and the two images must differ.
CLAMP_CODE_TOLERANCE = 1


def _rgb_codes(path,pixels):
    data=path.read_bytes()
    if len(data)!=pixels*4: raise ValueError('Readback length mismatch')
    return [(data[i*4+2],data[i*4+1],data[i*4]) for i in range(pixels)]


def clamp_relations(cases,directory):
    """Cross-case source-clamp identities on the unclamped background."""
    index={c['label']:i for i,c in enumerate(cases) if c.get('label')}
    lanes={'highlight':'clamp_','authored':'clamp_authored_'}
    wanted={prefix+name for prefix in lanes.values() for name in ('ref_none','ref_one','hot_none','hot_one')}
    wanted.add('clamp_hot_two')
    if set(index)!=wanted: raise ValueError('Missing source-clamp cases')
    reference=cases[index['clamp_ref_none']]
    pixels=reference['width']*reference['height']
    # Outside the bar every clamp case of a lane shares the same displayed
    # scene, so the whole difference there is the bloom feed. The bar itself
    # carries the unclamped HDR code and must differ between the two sources.
    background=[y*reference['width']+x for y in range(reference['height'])
                for x in range(reference['width']) if not CLAMP_BAR[0] <= x < CLAMP_BAR[1]]
    if len(background)!=pixels-reference['height']*(CLAMP_BAR[1]-CLAMP_BAR[0]):
        raise ValueError('Clamp background region mismatch')
    codes={name:_rgb_codes(directory/f'case_{i}.bgra8',pixels) for name,i in index.items()}
    def delta(a,b,region):
        return max(abs(x-y) for i in region for x,y in zip(codes[a][i],codes[b][i]))
    everywhere=range(pixels)
    result={}
    for lane,prefix in lanes.items():
        identical=codes[prefix+'ref_none']==codes[prefix+'ref_one']
        clamped=delta(prefix+'hot_one',prefix+'ref_none',background)
        effect=delta(prefix+'hot_none',prefix+'hot_one',background)
        result[lane]=dict(identity_bit_identical=identical,
                          clamped_vs_native_max_code=clamped,
                          clamp_effect_max_code=effect,
                          # Informational: the bar itself must still differ,
                          # since the clamp never touches the displayed scene.
                          whole_image_max_code=delta(prefix+'hot_one',prefix+'ref_none',everywhere),
                          passed=identical and clamped<=CLAMP_CODE_TOLERANCE and effect>MAX_CODE_ERROR)
    # Only the highlight lane carries the intermediate clamp 2.0 case.
    bracket=max(max(codes['clamp_hot_one'][i][ch]-codes['clamp_hot_two'][i][ch],
                    codes['clamp_hot_two'][i][ch]-codes['clamp_hot_none'][i][ch])
                for i in background for ch in range(3))
    result['clamp_two_bracket_violation_code']=bracket
    result['background_pixels']=len(background)
    result['pixels']=pixels
    result['passed']=bracket<=CLAMP_CODE_TOLERANCE and all(result[lane]['passed'] for lane in lanes)
    if not result['passed']: raise RuntimeError(f'Source-clamp relations failed: {result}')
    return result


def validate_log(text,cases,returncode):
    lines=text.splitlines()
    terminal=[line for line in lines if line.startswith('RESULT ')]
    if returncode or terminal!=[f'RESULT PASS cases={len(cases)} controls=16 checks={len(cases)+16} reset=1 reset_cases=1'] \
            or not lines or lines[-1]!=terminal[0] or any('FAIL' in line for line in lines):
        raise ValueError('Missing, duplicate, failed or nonterminal fixture result')
    neutral=re.findall(r'^FILL label=neutral requested=6b193957 mismatches=(\d+) first=(\d+) observed=([0-9a-f]{8})$',text,re.MULTILINE)
    if neutral!=[('0','4294967295','6b193957')]:
        raise ValueError('Missing/duplicate/failed neutral ColorFill identity control')
    controls=[int(m.group(1)) for line in lines if (m:=re.fullmatch(r'CONTROL test=(\d+) pass=1',line))]
    if controls!=list(range(16)):
        raise ValueError('Missing/duplicate/unordered fault controls')
    records=[tuple(map(int,m.groups())) for line in lines
             if (m:=re.fullmatch(r'CASE index=(\d+) width=(\d+) height=(\d+) checks=(\d+) pass=1',line))]
    expected_records=[(i,c['width'],c['height'],16 if i==0 else 1) for i,c in enumerate(cases)]
    if records!=expected_records:
        raise ValueError('Missing/duplicate/incorrect image cases')
    npatch=re.findall(r'^NPATCH accepted=([01]) hr=([0-9a-f]{8})$',text,re.MULTILINE)
    adaptive=re.findall(r'^ADAPTIVE accepted=([01]) hr=([0-9a-f]{8})$',text,re.MULTILINE)
    draws=re.findall(r'^NPATCH_DRAWS checked=(\d+) pass=1$',text,re.MULTILINE)
    if len(npatch)!=1 or len(adaptive)!=1 or len(draws)!=1 or int(draws[0])<=0:
        raise ValueError('Missing/duplicate NPatch capability or injected-draw assertion')
    reset=[line for line in lines if line.startswith('RESET_CASE ')]
    last=cases[RESET_CASE_INDEX]
    if reset!=[f"RESET_CASE index={RESET_CASE_INDEX} width={last['width']} height={last['height']} checks=1 pass=1"]:
        raise ValueError('Missing/duplicate/incorrect post-Reset case')
    if any(line.startswith(('CASE ','CONTROL ')) and not re.fullmatch(
            r'(?:CONTROL test=\d+ pass=1|CASE index=\d+ width=\d+ height=\d+ checks=\d+ pass=1)',line) for line in lines):
        raise ValueError('Malformed fixture record')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only',action='store_true')
    parser.add_argument('--output-dir',type=Path)
    args=parser.parse_args()
    directory=args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='x3-bloom-pass-'))
    directory.mkdir(parents=True,exist_ok=True)
    if any(directory.iterdir()): raise ValueError('Output directory must be empty')
    sources={Path(__file__).resolve(),Path(builder.__file__),Path(filtering.__file__),Path(oracle.__file__),
             Path(bottle.__file__),ROOT/'verification/probe/game_guard.py',ROOT/'verification/probe/wine_lock.py',
             ROOT/'verification/probe/bloom_pass_fixture.cpp',ROOT/'src/renderer/bloom_pass.cpp',
             ROOT/'src/renderer/bloom_pass.h',ROOT/'src/renderer/quad_vertex_program.h',
             ROOT/'src/renderer/quad_vertex_program_inc.h',ROOT/'src/temporal/bloom.h',
             ROOT/'src/temporal/agx.h',ROOT/'src/temporal/sharpen.h',
             ROOT/'tools/analysis/bloom_reference.py',ROOT/'tools/analysis/agx_reference.py',filtering.GENERATOR}
    expanded={}
    for name in NAMES:
        path=ROOT/f'src/temporal/{name}.hlsl'
        text,includes=filtering.generator.expand_includes(path)
        sources.update([path,*includes])
        path=directory/(name+'.hlsl');path.write_text(text);expanded[str(path)]=filtering.digest(path)
    before={str(p):filtering.digest(p) for p in sorted(sources)}
    exe,command=builder.build(directory)
    corpus=make_cases();bundle=directory/'cases.bin';write_cases(corpus,bundle)
    tracked=before|expanded|{str(exe):filtering.digest(exe),str(bundle):filtering.digest(bundle)}
    report=dict(schema=1,passed=False,phase='built',bottle=bottle.describe(),build_command=command,
        gpu_execution_verified=False,native_windows_runtime_verified=False,game_launched=False,
        renderer_integration_verified=False,installed_dll_changed=False,
        scope='actual standalone BloomPass state/failure/reset and bounded final-image differential',
        untested=['game bridge and CPU/cache ownership','lost device','non-null depth','two devices and wrong thread',
                  'per-setter driver failures and partial StretchRect writes','game FPS and capture mutex timing',
                  'native Windows runtime','game acceptance'],
        max_code_error=MAX_CODE_ERROR,cases=[{k:v for k,v in c.items() if k!='image'} for c in corpus],
        retained=str(directory),inputs_before=tracked,images=[])
    summary=directory/'summary.json'
    def save(): summary.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    save()
    if args.build_only:
        report['inputs_unchanged']=all(filtering.digest(p)==sha for p,sha in tracked.items())
        save()
        if not report['inputs_unchanged']: raise RuntimeError('Sources changed during build')
        print(json.dumps(dict(build_only=True,gpu_execution_verified=False,summary=str(summary))))
        return
    filtering.require_runner_lock()
    if game_running(): raise RuntimeError('Game running; fixture postponed')
    compiler=bottle.game_dir()/'d3dx9_37.dll'
    runtime=[compiler,*[bottle.bottle_dir()/'drive_c/windows/system32'/n for n in ('d3d9.dll','wined3d.dll')]]
    tracked.update({str(p.resolve()):filtering.digest(p) for p in runtime if p.exists()})
    report['inputs_before']=tracked.copy()
    win=lambda p:'Z:'+str(p)
    command=[bottle.WINE,*bottle.wine_args(),'--dll','d3d9=b','--workdir',str(directory),str(exe),win(compiler),win(directory)]
    report.update(phase='running',command=command,exit_code=None);save()
    try:
        with (directory/'fixture.txt').open('w') as out,(directory/'wine.log').open('w') as err:
            result=subprocess.run(command,stdout=out,stderr=err,timeout=180)
        report.update(exit_code=result.returncode,stdout_sha256=filtering.digest(directory/'fixture.txt'),
                      stderr_sha256=filtering.digest(directory/'wine.log'))
        log=(directory/'fixture.txt').read_text(errors='replace')
        validate_log(log,corpus,result.returncode)
        modules={}
        for name,path in re.findall(r'^MODULE name=(\S+) path=(.+)$',log,re.MULTILINE):
            if name in modules: raise RuntimeError('Duplicate loaded module')
            host=filtering.host_module_path(path.strip())
            if str(host) not in tracked: raise RuntimeError('Loaded runtime was not fingerprinted before execution: '+str(host))
            modules[name]=dict(path=path.strip(),host_path=str(host),sha256=filtering.digest(host))
        if not {'d3d9.dll','d3dx9_37.dll'}<=set(modules): raise RuntimeError('Missing loaded runtime identity')
        report['loaded_modules']=modules
        report['hostile_npatch_verified']='NPATCH accepted=1 ' in log
        if not report['hostile_npatch_verified']: report['untested'].append('hostile nonzero NPatch state rejected by backend')
        report['hostile_adaptive_verified']='ADAPTIVE accepted=1 ' in log
        if not report['hostile_adaptive_verified']: report['untested'].append('hostile adaptive tessellation state rejected by backend')
        for i,c in enumerate(corpus): report['images'].append(dict(index=i,**compare(c,directory/f'case_{i}.bgra8',directory/f'c{i}_t0_original.bgra8')))
        reset_index=RESET_CASE_INDEX
        report['reset_image']=compare(corpus[reset_index],directory/'reset_case.bgra8',
                                      directory/f'reset_c{reset_index}_t0_original.bgra8')
        if not report['reset_image']['passed']: raise RuntimeError('Post-Reset independent image mismatch')
        report['compiled_shaders']={name:filtering.digest(directory/(name+'.cso')) for name in NAMES}
        if not all(x['passed'] for x in report['images']): raise RuntimeError('Independent image oracle mismatch')
        report['clamp_relations']=clamp_relations(corpus,directory)
        report.update(passed=True,phase='complete',gpu_execution_verified=True)
    except Exception as error:
        report.update(passed=False,phase='failed',error=str(error))
        raise
    finally:
        log=(directory/'fixture.txt').read_text(errors='replace')
        report['fill_diagnostics']=[dict(label=label,requested=requested,mismatches=int(count),first=int(first),observed=observed)
            for label,requested,count,first,observed in re.findall(
                r'^FILL label=(\S+) requested=([0-9a-f]{8}) mismatches=(\d+) first=(\d+) observed=([0-9a-f]{8})$',log,re.MULTILINE)]
        report['readbacks']={p.name:filtering.digest(p) for p in sorted(directory.glob('*.bgra8'))}
        report['stdout_sha256']=filtering.digest(directory/'fixture.txt')
        report['stderr_sha256']=filtering.digest(directory/'wine.log')
        report['inputs_after']={p:filtering.digest(p) for p in tracked}
        report['inputs_unchanged']=report['inputs_after']==tracked
        if not report['inputs_unchanged']: report.update(passed=False,phase='failed',gpu_execution_verified=False,error='Inputs changed during run')
        save();(bottle.results_dir(ROOT)/'bloom-pass-summary.json').write_bytes(summary.read_bytes())
        print(json.dumps(dict(passed=report['passed'],phase=report['phase'],summary=str(summary))))
        if not report['inputs_unchanged']: raise RuntimeError('Inputs changed during run')

if __name__=='__main__': main()
