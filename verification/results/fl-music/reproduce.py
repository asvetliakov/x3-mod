#!/usr/bin/env python3
"""Host evidence of the FL music family port (docs/verification/farnhams-legacy.md, "music family on FL (host evidence,
2026-11-03)"): the generated windows against the exe, the three read-only site verifiers, and the three native
fixtures (MinGW i686, natively through WSL interop). Writes the compact record summary.json beside this script.

usage: python3 verification/results/fl-music/reproduce.py [--exe /mnt/c/X3FL/X3FL.exe] [--no-native]
"""
import argparse
import json
import re
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
sys.path.insert(0, str(HERE))
import fl_music_fixture_build as fb  # noqa: E402
import generate_windows as gen  # noqa: E402
import verify_media_cue_sites_fl  # noqa: E402
import verify_music_alert_sites_fl  # noqa: E402
import verify_music_keep_sites_fl  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--exe', type=Path, default=fb.DEFAULT_EXE)
    parser.add_argument('--no-native', action='store_true')
    args = parser.parse_args()
    text, im = gen.generate(args.exe)
    out = {'exe_sha256': im.sha256, 'windows': len(gen.WINDOWS),
           'generated_header_current': text == gen.OUT.read_text(), 'verifiers': {}, 'fixtures': {}}
    for name, module, keys in (('media_cue', verify_media_cue_sites_fl, ('media_window_count', 'media_fixup_count',
                                                                         'allocator_instructions')),
                               ('music_keep', verify_music_keep_sites_fl, ('keep_window_count', 'keep_fixup_count',
                                                                           'caller_counts', 'a_jump_atomic',
                                                                           'c_jump_atomic', 'd_jump_atomic',
                                                                           'e_jump_atomic')),
                               ('music_alert', verify_music_alert_sites_fl, ('alert_window_count', 'alert_fixup_count',
                                                                             'alert_instructions'))):
        report = module.verify(args.exe)
        out['verifiers'][name] = {'result': report['result'], 'checks': len(report['checks']),
                                  'failed': report['failed'], **{k: report[k] for k in keys},
                                  'ap_windows_admitted': report.get('ap_windows_admitted'),
                                  'ap_site_bytes_in_fl': report.get('ap_site_bytes_in_fl')}
    if not args.no_native and fb.available():
        for feature in ('media_cue', 'music_keep', 'music_alert'):
            with tempfile.TemporaryDirectory(prefix=f'x3-fl-{feature}-') as tmp:
                run = fb.run(fb.build(feature, tmp), args.exe)
            m = re.search(r'RESULT (\w+) checks=(\d+) failures=(\d+)', run.stdout)
            flips = re.search(r'FLIPS windows=(\d+) cases=(\d+) refused=(\d+)', run.stdout)
            out['fixtures'][feature] = {
                'result': m.group(1) if m else 'NO_RESULT', 'checks': int(m.group(2)) if m else 0,
                'failures': int(m.group(3)) if m else -1, 'returncode': run.returncode,
                'window_flips': [int(x) for x in flips.groups()] if flips else None,
                'failed_labels': [l[5:].split(' ')[0] for l in run.stdout.splitlines() if l.startswith('FAIL ')]}
    (HERE / 'summary.json').write_text(json.dumps(out, indent=1, sort_keys=True) + '\n')
    ok = out['generated_header_current'] and all(v['result'] == 'PASS' for v in out['verifiers'].values()) and \
        all(v['result'] == 'PASS' for v in out['fixtures'].values())
    print(json.dumps({'ok': ok, 'verifiers': {k: v['result'] for k, v in out['verifiers'].items()},
                      'fixtures': {k: (v['result'], v['checks'], v['failures']) for k, v in out['fixtures'].items()}}))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
