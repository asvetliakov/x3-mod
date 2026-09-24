#!/usr/bin/env python3
"""Evidence producer for the X3M_HDR_DITHER ledger entry (docs/verification/
hdr-scene-path.md, "Display dither").

  bins OUT [--before REV]   write ps_<name>_{before,after}.bin from the embedded
                            *_program_inc.h (before: git show REV, default HEAD)
  slots ASM                 print the D3DX "instruction slots used" line of each
                            disassembly ASM/*.txt (produced by
                            tools/analysis/disassemble_shaders.cpp under Wine:
                            X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py
                            wine --bottle X3 --no-update disasm.exe C:\\X3\\d3dx9_37.dll <bins> <asm>)
  keep SUMMARY REPORT       copy the per-case figures of a selected
                            run_motion_output.py run (its motion-output-partial.json
                            and .txt) into motion-output-cases-2026-09-24.json here,
                            with each case's RESULT line and the FP16 history
                            equality of each dither case with its undithered twin
                            (computed while the case directories still exist)
  fixture [KEPT]            print the figures from that tracked copy
  bloom SUMMARY [KEPT]      keep / print the run_bloom_pass.py image figures
                            (bloom-pass-2026-09-24.json here)
  reference                 the CPU reference's constant-input statistics
Bytecode and disassembly stay local (our own programs, but build products).
"""
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
PROGRAMS = ('hdr_tonemap', 'hdr_tonemap_sharpen', 'taa_sharpen', 'hdr_writeback', 'bloom_agx', 'hdr_writeback_dither')


def words(text):
    return [int(x, 16) for x in re.findall(r'0x([0-9a-f]{8})u', text)]


def bins(out, before='HEAD'):
    out = Path(out); out.mkdir(parents=True, exist_ok=True)
    for name in PROGRAMS:
        rel = f'src/renderer/{name}_program_inc.h'
        texts = {'after': (ROOT / rel).read_text()}
        old = subprocess.run(['git', 'show', f'{before}:{rel}'], cwd=ROOT, capture_output=True, text=True)
        if old.returncode == 0:
            texts['before'] = old.stdout
        for tag, text in texts.items():
            w = words(text)
            (out / f'ps_{name}_{tag}.bin').write_bytes(struct.pack('<%dI' % len(w), *w))
            print(f'{name} {tag} words={len(w)}')


def slots(asm):
    for path in sorted(Path(asm).glob('*.txt')):
        text = path.read_bytes().replace(b'\0', b'').decode('latin-1')
        match = re.search(r'approximately (\d+) instruction slots used \((\d+) texture, (\d+) arithmetic\)', text)
        if match:
            print(f'{path.name}: slots={match.group(1)} texture={match.group(2)} arithmetic={match.group(3)}')
        else:  # D3DX prints no count for a one-instruction program
            body = [l for l in text.splitlines() if l.strip() and not l.lstrip().startswith(('//', 'ps_', 'dcl', 'def'))]
            print(f'{path.name}: slots={len(body)} (counted)')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


HERE = Path(__file__).resolve().parent
KEPT = HERE / 'motion-output-cases-2026-09-24.json'
BLOOM = HERE / 'bloom-pass-2026-09-24.json'
TWINS = (('seam-taa-hdr-sharpen-dither', 'seam-taa-hdr-sharpen-on'),
         ('seam-taa-hdr-tonemap-sharpen-dither', 'seam-taa-hdr-tonemap-sharpen-on'))
CASE_KEYS = ('ramp', 'ramp_frame2', 'sharpen', 'checks', 'tonemap_line', 'exit')


def keep(summary, report):
    data = json.loads(Path(summary).read_text())
    text = Path(report).read_text(errors='replace')
    results, current = {}, None
    for line in text.splitlines():
        if line.startswith('==== '):
            current = line.split()[1]
        elif line.startswith('RESULT ') and current:
            results[current] = line
    out = {'source': 'run_motion_output.py selected run (PARTIAL by design)', 'status': data.get('status'),
           'bottle': data.get('bottle'), 'binaries': data.get('binaries'), 'cases': {}, 'history_twins': {}}
    for name, case in data['cases'].items():
        entry = {k: case[k] for k in CASE_KEYS if k in case}
        if 'sharpen' in entry:  # drop nothing; the per-frame entries are small
            pass
        entry['result_line'] = results.get(name)
        out['cases'][name] = entry
    for dithered, twin in TWINS:
        if dithered in data['cases'] and twin in data['cases']:
            files = [{p.name: sha(p) for p in sorted((ROOT / data['cases'][n]['directory'] / 'x3-modern-captures').glob('taa_*.rgba16f'))}
                     for n in (dithered, twin)]
            out['history_twins'][dithered] = {'twin': twin, 'files': len(files[0]), 'identical': files[0] == files[1] and len(files[0]) > 0}
    KEPT.write_text(json.dumps(out, indent=1, sort_keys=True) + '\n')
    print('kept', len(out['cases']), 'cases ->', KEPT.relative_to(ROOT))


def fixture(kept=KEPT):
    data = json.loads(Path(kept).read_text())
    print('status', data.get('status'), 'cases', len(data['cases']))
    for name, case in sorted(data['cases'].items()):
        if case.get('result_line'):
            print(f"{name}: {' '.join(case['result_line'].split()[:3])} exit={case.get('exit')}")
        if 'ramp' in case:
            r, s = case['ramp'], case['ramp'].get('dither')
            repeat = case.get('ramp_frame2', {}).get('channels') == r['channels']
            print(f"{name}: ramp max={r['max']:.3f} mean={r['mean']:.3f} alpha_max={r['alpha_max']} frames_1_2_channel_stats_equal={repeat}"
                  + (f" exact={s['exact_fraction']:.4f} signed_mean={s['mean_signed_error_vs_undithered']:+.4f} nonuniform_cells={s['cells_nonuniform']}" if s else ''))
        if 'sharpen' in case:
            s = case['sharpen']
            exact = [v.get('exact_pixel_fraction') for v in s['images'].values()]
            outside = max(v['outside_3x3'] for v in s['images'].values())
            print(f"{name}: sharpen max={s['max_code_error']:.3f} mean={s['mean_code_error']:.3f} changed={s['changed_fraction']:.3f}"
                  f" outside_3x3={outside} exact_min={min(exact):.4f}")
    for dithered, h in sorted(data['history_twins'].items()):
        print(f"{dithered}: history files={h['files']} identical_to_{h['twin']}={h['identical']}")


def bloom(summary=None):
    if summary:
        data = json.loads(Path(summary).read_text())
        keys = ('index', 'passed', 'max_code_error', 'mean_code_error', 'channels', 'alpha_errors', 'dither',
                'mean_signed_error_vs_undithered', 'codes_per_channel', 'max_code_error_vs_undithered')
        out = {'source': 'run_bloom_pass.py', 'passed': data['passed'], 'phase': data['phase'], 'bound_codes': data['max_code_error'],
               'bottle': data.get('bottle'), 'cases': [{k: v for k, v in c.items()} for c in data['cases'][45:]],
               'images': [{k: i[k] for k in keys if k in i} for i in data['images']]}
        BLOOM.write_text(json.dumps(out, indent=1, sort_keys=True) + '\n')
    data = json.loads(BLOOM.read_text())
    images = data['images']
    print(f"bloom passed={data['passed']} images={len(images)} max_code_error={max(i['max_code_error'] for i in images)} bound={data['bound_codes']}")
    for i in images:
        if 'dither' in i:
            print(f"  case {i['index']}: max={i['max_code_error']} mean={i['mean_code_error']:.4f} signed_vs_undithered={i['mean_signed_error_vs_undithered']:+.4f}"
                  f" codes_per_channel={i['codes_per_channel']} max_vs_undithered={i['max_code_error_vs_undithered']:.3f}")


def reference():
    sys.path.insert(0, str(ROOT / 'tools/analysis'))
    import agx_reference as ref
    for w, h in ((128, 128), (1920, 1080)):
        worst, spread = 0.0, 0
        for fraction in (0.0, 0.1, 0.25, 0.5, 0.73, 0.9, 0.99):
            v = (100.0 + fraction) / 255.0
            codes = [ref.store_code(ref.dither_display(v, x, y)) for y in range(h) for x in range(w)]
            worst = max(worst, abs(sum(codes) / len(codes) - 255.0 * v)); spread = max(spread, max(codes) - min(codes))
        print(f'{w}x{h}: max |mean code - 255 v| = {worst:.5f}, max spread = {spread}')


if __name__ == '__main__':
    command, *rest = sys.argv[1:] or ['help']
    if command == 'bins':
        bins(rest[0], rest[2] if len(rest) > 2 and rest[1] == '--before' else 'HEAD')
    elif command == 'slots':
        slots(rest[0])
    elif command == 'keep':
        keep(*rest)
    elif command == 'fixture':
        fixture(*rest)
    elif command == 'bloom':
        bloom(*rest)
    elif command == 'reference':
        reference()
    else:
        print(__doc__)
