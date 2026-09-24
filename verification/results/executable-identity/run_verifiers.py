#!/usr/bin/env python3
"""Run every hook-site verifier that carries the executable identity on X3AP.exe variants.

Variants (copies in a temporary directory, deleted afterwards; the bottle file is
only read): shipped (the installed file), laa_cleared (bit 0x20 cleared),
ntcore_4gb (bit set and CheckSum rewritten as NTCore 4gb_patch.exe does),
unknown_hash (one .rsrc byte changed: a hash outside the known list, sites and
anchors intact), different_build (link stamp flipped: another build of the
game) and, for five verifiers, site_corrupt (one site byte flipped).
Expectation: PASS everywhere except different_build (every verifier FAIL, the
identity false) and site_corrupt (FAIL with the identity true); the raw hash is
reported as INFO. Records the anchor count and the source commit. Prints one JSON object (docs/reverse-engineering/executable-identity.md).

usage: run_verifiers.py [--exe PATH] [--output JSON]
"""
import argparse
import os
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PROBE = ROOT / 'verification/probe'
sys.path.insert(0, str(PROBE))
import exe_identity  # noqa: E402

EXE_OPTION = ['exe_identity', 'verify_chase_camera_site', 'verify_chase_aim_sites', 'verify_chase_lead_sites', 'verify_collide_sites',
              'verify_point_light_site', 'verify_collide_memo_site', 'verify_cull_small_parts_site', 'verify_cull_census_sites',
              'verify_lod_scale_site', 'verify_game_phase_sites', 'verify_frame_phase_sites', 'verify_media_cue_site',
              'verify_loop_phase_sites', 'verify_residual_phase_sites', 'verify_pass_phase_sites', 'verify_voice_dmo_site',
              'verify_submit_phase_sites', 'verify_post_phase_sites', 'verify_sun_occlusion_sites',
              'verify_terran_lod_site', 'verify_lod_occlusion_site', 'verify_fov_site']
RESULTS = {'pause': ROOT / 'verification/results/pause-dialog-input/verify_pause_sites.py',
           'music_restart': ROOT / 'verification/results/music-restart/verify_music_restart_sites.py'}
# One byte per corrupt case: the chase camera's jz displacement, the LOD-scale fmul operand, the
# Terran-station LOD reader's je opcode (74 -> 75), the LOD occlusion gate's rel32 low byte (c9 -> c8) and the FOV
# constructor's imm32 second byte (40 -> 41).
SITE_CORRUPT = {'verify_chase_camera_site': 0x00420e0f, 'verify_lod_scale_site': 0x0047d44d, 'verify_terran_lod_site': 0x0047d01c,
                'verify_lod_occlusion_site': 0x004c34f9, 'verify_fov_site': 0x0041c9dd}


def verdict(stdout):
    match = re.search(r'\{.*\}\s*\Z', stdout, re.S)
    try:
        report = json.loads(match.group(0)) if match else None
    except json.JSONDecodeError:
        report = None
    if not isinstance(report, dict):
        return None, None
    result = report.get('result', report.get('verdict', report.get('passed')))
    result = {True: 'PASS', False: 'FAIL'}.get(result, result)
    return result, report.get('exe_info')


def run(command, env=None):
    done = subprocess.run(command, capture_output=True, text=True, cwd=ROOT, env=env, timeout=600)
    result, info = verdict(done.stdout)
    return {'result': result, 'rc': done.returncode, 'laa': info.get('laa') if info else None,
            'known': info.get('known') if info else None}


def light_phase(exe):
    code = ('import sys,json;sys.path.insert(0,sys.argv[1]);import verify_light_phase_sites as m;from pathlib import Path;'
            'm.common.DEFAULT_EXE=Path(sys.argv[2]);r=m.inspect(Path(sys.argv[2]).read_bytes(),m.decode(Path(sys.argv[2])),'
            'm.SOURCE.read_text(),*m.shared.other_claims(m.ROOT/"src/proxy",m.SOURCE));print(json.dumps(r,default=str))')
    return run([sys.executable, '-c', code, str(PROBE), str(exe)])


def run_all(exe):
    rows = {name: run([sys.executable, str(PROBE / f'{name}.py'), '--exe', str(exe)]) for name in EXE_OPTION}
    rows['verify_light_phase_sites'] = light_phase(exe)
    rows['pause'] = run([sys.executable, str(RESULTS['pause']), str(exe)])
    rows['music_restart'] = run([sys.executable, str(RESULTS['music_restart'])], env=dict(os.environ, X3AP_EXE=str(exe)))
    return rows


def patched(data, va, xor=0x01):
    out = bytearray(data)
    for name, virtual_size, virtual_address, raw_size, raw_pointer, _ in exe_identity.section_table(data):
        start = exe_identity.IMAGE_BASE + virtual_address
        if start <= va < start + raw_size:
            out[raw_pointer + va - start] ^= xor
            return bytes(out)
    raise ValueError(hex(va))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--exe', type=Path, default=exe_identity.DEFAULT_EXE)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    shipped = args.exe.read_bytes()
    rsrc = exe_identity.section_table(shipped)[3]
    variants = {
        'laa_cleared': exe_identity.with_laa(shipped, on=False),
        'ntcore_4gb': exe_identity.with_laa(shipped, on=True, new_checksum=exe_identity.pe_checksum(exe_identity.with_laa(shipped))),
        'unknown_hash': patched(shipped, exe_identity.IMAGE_BASE + rsrc[2] + rsrc[3] - 1, 0xff),
    }
    commit = subprocess.run(['git', 'rev-parse', 'HEAD'], capture_output=True, text=True, cwd=ROOT).stdout.strip()
    dirty = bool(subprocess.run(['git', 'status', '--porcelain', '--untracked-files=no'], capture_output=True, text=True,
                                cwd=ROOT).stdout.strip())
    report = {'commit': commit + ('-dirty' if dirty else ''), 'anchors': len(exe_identity.ANCHORS),
              'shipped': {'info': exe_identity.info(shipped), 'verifiers': run_all(args.exe)}}
    with tempfile.TemporaryDirectory(prefix='x3m-exe-identity-') as scratch:
        for name, data in variants.items():
            path = Path(scratch) / f'{name}.exe'
            path.write_bytes(data)
            report[name] = {'info': exe_identity.info(data), 'verifiers': run_all(path)}
            path.unlink()
        corrupt = {}
        for name, va in SITE_CORRUPT.items():
            path = Path(scratch) / f'corrupt-{name}.exe'
            path.write_bytes(patched(shipped, va))
            corrupt[name] = {'va': f'{va:#010x}', 'identity_ok': exe_identity.identity_ok(path),
                             **run([sys.executable, str(PROBE / f'{name}.py'), '--exe', str(path)])}
            path.unlink()
        report['site_corrupt'] = corrupt
        other = bytearray(shipped)
        other[exe_identity.nt_offset(shipped) + 8] ^= 0x01  # TimeDateStamp low byte
        path = Path(scratch) / 'different_build.exe'
        path.write_bytes(bytes(other))
        report['different_build'] = {'identity_ok': exe_identity.identity_ok(path), 'verifiers': run_all(path)}
        path.unlink()
    expected = all(row['result'] == 'PASS' for key in ('shipped', *variants) for row in report[key]['verifiers'].values())
    expected = expected and all(row['result'] == 'FAIL' and row['identity_ok'] for row in report['site_corrupt'].values())
    different = report['different_build']
    expected = expected and not different['identity_ok'] and all(r['result'] == 'FAIL' for r in different['verifiers'].values())
    report['summary'] = {key: {'sha256': report[key]['info']['sha256'], 'known': report[key]['info']['known'],
                               'laa': report[key]['info']['laa'],
                               'pass': sum(r['result'] == 'PASS' for r in report[key]['verifiers'].values()),
                               'total': len(report[key]['verifiers'])} for key in ('shipped', *variants)}
    report['result'] = 'PASS' if expected else 'FAIL'
    text = json.dumps(report, indent=1)
    if args.output:
        args.output.write_text(text + '\n')
    print(json.dumps({'result': report['result'], 'commit': report['commit'], 'anchors': report['anchors'],
                      'summary': report['summary'],
                      'different_build': {'identity_ok': different['identity_ok'],
                                          'fail': sum(r['result'] == 'FAIL' for r in different['verifiers'].values()),
                                          'total': len(different['verifiers'])},
                      'site_corrupt': {k: (v['result'], v['identity_ok']) for k, v in report['site_corrupt'].items()}}, indent=1))
    return 0 if expected else 1


if __name__ == '__main__':
    sys.exit(main())
