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
ref = filtering.ref
NAMES = ('quad_vs', *(f'bloom_{name}_ps' for name in filtering.KERNELS),
         'bloom_agx_ps', 'taa_sharpen_ps', 'hdr_writeback_ps')
# Fixed before first execution. Compare final 8-bit RGB to independent double
# AgX + nine-tap bloom + RCAS, without FP16 intermediate quantization (fused
# ideal). Three codes cover FP16 storage, shader arithmetic, sampler and UNORM
# conversion jointly for this moderate bounded corpus, not a universal bound.
MAX_CODE_ERROR = 3


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
                    cases.append(dict(mode=mode, kind=kind, width=w, height=h,
                                      strength=strength, sharp=sharp, threshold=0., exposure=1.,
                                      authored_glow_gain=0., highlight_gain=.05, image=image))
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
            cases.append(dict(mode=mode, kind='authored', width=w, height=h,
                              strength=strength, sharp=0., threshold=1., exposure=2 ** ev,
                              authored_glow_gain=gain, highlight_gain=.05, image=image))
    return cases


def write_cases(cases, path):
    with path.open('wb') as f:
        f.write(b'X3BP0002' + struct.pack('<I',len(cases)))
        for c in cases:
            f.write(struct.pack('<3I6f',c['width'],c['height'],ref.DECODE_MODES.index(c['mode']),
                                c['strength'],c['sharp'],c['threshold'],c['exposure'],
                                c['authored_glow_gain'],c['highlight_gain']))
            for row in c['image']:
                for p in row:
                    f.write(struct.pack('<4e',*p))


def expected(c):
    p = ref.Params(levels=3, threshold=c['threshold'], strength=c['strength'],
                   authored_glow_gain=c['authored_glow_gain'], highlight_gain=c['highlight_gain'])
    bloom = ref.bloom(c['image'],p,exposure=c['exposure'],mode=c['mode'])
    display = [[oracle.composition(e,b,c['strength'],exposure=c['exposure'],mode=c['mode'])
                for e,b in zip(row,blur)] for row,blur in zip(c['image'],bloom)]
    if not c['sharp']:
        return [[p[:3] for p in row] for row in display]
    w,h=c['width'],c['height']
    def get(x,y): return display[min(h-1,max(0,y))][min(w-1,max(0,x))]
    return [[oracle.rcas_cross([get(x,y-1),get(x-1,y),get(x,y),get(x+1,y),get(x,y+1)],
                               2**(-2*(1-c['sharp']))) for x in range(w)] for y in range(h)]


def compare(c,path,baseline_path):
    data=path.read_bytes(); baseline=baseline_path.read_bytes()
    if len(data)!=c['width']*c['height']*4 or len(baseline)!=len(data):
        raise ValueError('Readback length mismatch')
    errors=[]; alpha_errors=0
    for i,p in enumerate(p for row in expected(c) for p in row):
        b,g,r,a=data[i*4:i*4+4]
        alpha_errors+=a!=baseline[i*4+3]
        errors.extend(abs(actual-oracle.code8(wanted)) for actual,wanted in zip((r,g,b),p))
    return dict(passed=max(errors)<=MAX_CODE_ERROR and alpha_errors==0,
                max_code_error=max(errors), mean_code_error=sum(errors)/len(errors),
                channels=len(errors), alpha_errors=alpha_errors, sha256=filtering.digest(path),
                original_sha256=filtering.digest(baseline_path))


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
    last=cases[-1]
    if reset!=[f"RESET_CASE index={len(cases)-1} width={last['width']} height={last['height']} checks=1 pass=1"]:
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
        reset_index=len(corpus)-1
        report['reset_image']=compare(corpus[-1],directory/'reset_case.bgra8',
                                      directory/f'reset_c{reset_index}_t0_original.bgra8')
        if not report['reset_image']['passed']: raise RuntimeError('Post-Reset independent image mismatch')
        report['compiled_shaders']={name:filtering.digest(directory/(name+'.cso')) for name in NAMES}
        if not all(x['passed'] for x in report['images']): raise RuntimeError('Independent image oracle mismatch')
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
