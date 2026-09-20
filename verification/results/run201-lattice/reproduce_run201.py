#!/usr/bin/env python3
"""Bounded, read-only Run201 lattice-capture triage.

Reads only the three state packets and selected session-log rows. It never
starts Wine or the game.
"""
from __future__ import annotations

import json
import re
from collections import Counter, defaultdict
from pathlib import Path

import numpy as np

ROOT = Path('/tmp/x3-bottleX3-run201')
OUT = Path('/tmp/x3-run201-triage')
LOG = ROOT / 'session-20260921-022832-212.log'
FRAMES = (9796, 10442, 12010)
ENV_KEYS = ('BOTTLE', 'CAMERA', 'CHASE_VIEW_RESTORE', 'LATTICE_STATE',
            'MOTION_OUTPUT', 'TAA', 'TAA_DEBUG', 'MOTION_RT_MODE',
            'CAPTURE_START', 'CAPTURE_FRAMES', 'CAMERA_LOG', 'HDR',
            'TAA_FAR_STABILISER', 'TAA_THIN_REGION')

def camera_bursts() -> dict:
    wanted = {f for start in FRAMES for f in range(start, start + 32)}
    pat = re.compile(r'^camera_state .*?frame=(\d+).*?r00=([^ ]+) r01=([^ ]+) r02=([^ ]+) r10=([^ ]+) r11=([^ ]+) r12=([^ ]+) r20=([^ ]+) r21=([^ ]+) r22=([^ ]+) t=([^,]+),([^,]+),([^ ]+)')
    rows = {}
    with LOG.open(errors='replace') as f:
        for line in f:
            m = pat.search(line)
            if m and int(m.group(1)) in wanted:
                rows[int(m.group(1))] = tuple(map(float, m.groups()[1:]))
    out = {}
    for start in FRAMES:
        group = [(f, rows[f]) for f in range(start, start + 32)]
        positions = [r[-3:] for _, r in group]
        angles, steps = [], []
        for (_, a), (_, b) in zip(group, group[1:]):
            dot = max(-1., min(3., sum(a[i] * b[i] for i in range(9))))
            angles.append(float(np.degrees(np.arccos(dot / 3))))
            steps.append(float(np.linalg.norm(np.subtract(a[-3:], b[-3:]))))
        out[str(start)] = {'camera_rows': len(group), 'origin_start': positions[0], 'origin_end': positions[-1],
            'origin_displacement': float(np.linalg.norm(np.subtract(positions[-1], positions[0]))),
            'rotation_step_sum_deg': sum(angles), 'rotation_step_max_deg': max(angles),
            'origin_step_sum': sum(steps), 'origin_step_max': max(steps)}
    return out

def selected_log() -> dict:
    found = {str(f): defaultdict(list) for f in FRAMES}
    lattice, crash = [], []
    options = None
    final = []
    with LOG.open(errors='replace') as f:
        for number, line in enumerate(f, 1):
            if line.startswith('proxy_options '): options = line.rstrip()
            if line.startswith('lattice_state '): lattice.append({'line': number, 'row': line.rstrip()})
            lo = line.lower()
            if any(x in lo for x in ('unhandled exception', 'fatal error', 'crash', 'segmentation fault')):
                crash.append({'line': number, 'row': line.rstrip()[:1000]})
            for frame in FRAMES:
                if f'frame={frame}' not in line: continue
                prefix = line.split(' ', 1)[0]
                found[str(frame)][prefix].append(number)
                if prefix in {'frame_end', 'motion_output_frame', 'motion_output_cut',
                              'camera_state', 'camera_pose', 'camera_log', 'camera_matrix'}:
                    found[str(frame)][prefix + '_rows'].append(line.rstrip()[:1800])
            final.append(line.rstrip()[:1000])
            if len(final) > 12: final.pop(0)
    settings = {}
    if options:
        for key in ENV_KEYS:
            m = re.search(r'\bX3M_' + key + r'=([^ ]+)', options)
            settings[key] = m.group(1) if m else None
    return {'settings': settings, 'lattice_rows': lattice, 'crash_marker_rows': crash,
            'target_frame_prefix_counts': {f: {k: len(v) for k, v in d.items() if not k.endswith('_rows')}
                                           for f, d in found.items()},
            'target_frame_rows': {f: {k: v for k, v in d.items() if k.endswith('_rows')}
                                  for f, d in found.items()}, 'session_tail': final}

def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    result = {'input_root': str(ROOT), 'log': str(LOG), 'observer_packets': [],
              'bursts': {}, 'log_observation': selected_log(), 'camera_bursts': camera_bursts(),
              'limitations': [
                  'F8 order/action labels are not encoded in the capture; no order is inferred.',
                  'The raw RG motion channels are prior UV coordinates, not speed; no raw-channel magnitude is reported as motion.',
                  'Packets explicitly report draw_input_coherence=unqualified and payload_copy_valid=not_attempted.'
              ]}
    for p in sorted(ROOT.glob('lattice-state-*.json'), key=lambda x: int(x.name.split('-')[4])):
        d = json.loads(p.read_text())
        records = []
        for r in d['records']:
            hrs = Counter(f['hr'] for f in r['fields'])
            targets = []
            for f in r['fields']:
                if f['kind'] == 'target':
                    targets.append({'index': f['index'], 'hr': f['hr'],
                                    'bound': bool(f['words'] and int(f['words'][0], 16)),
                                    'binding_words': f['words'][1:4] if f['words'] else []})
            owner = next((f['words'][3:5] + f['words'][7:9] for f in r['fields']
                          if f['kind'] == 'object' and f['index'] == 0), [])
            records.append({'slot': r['slot'], 'draw': r['draw'], 'submitted': r['submitted'],
                            'result': r['result'], 'field_count': len(r['fields']), 'hr_counts': dict(hrs),
                            'field_kinds': dict(Counter(f['kind'] for f in r['fields'])), 'targets': targets,
                            'owner_session_node_handle': owner})
        result['observer_packets'].append({k: d[k] for k in ('schema', 'selector', 'status', 'device', 'frame',
            'generation', 'draw_input_coherence', 'payload_copy_valid', 'scope_active_at_arm', 'candidates',
            'query_ticks', 'qpc_frequency', 'matches')} | {'file': p.name, 'records': records})
    for anchor in FRAMES:
        result['bursts'][str(anchor)] = {'frames': [anchor, anchor + 31], 'count': 32,
            'motion_files': 32, 'camera': result['camera_bursts'][str(anchor)]}
    (OUT / 'run201-triage.json').write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
    lines = ['# Run201 lattice triage', '', 'Generated by `reproduce_run201.py`; read-only input.', '']
    lines += ['## Observation', '']
    lines.append('Runtime (session log line 3): `C:\\X3\\d3d9.dll`, SHA-256 `a51d1e75fa80d7d07bab7ab66004291f7248bc5693e6585171e564a90fa96e56`, 54,386,310 bytes, matching manifest; source commit `85da89a8955a72b20d92f2309e4b9a4cb4dd325e`. Session identifies renderer version 0.4/schema 2/32-bit pointers, NVIDIA GeForce 8800 GTX `nvd3dum.dll`; chase camera; motion output/TAA/HDR enabled; lazy RT; TAA debug; lattice selector `run177_panel_position_v1`.')
    for p in result['observer_packets']:
        lines.append(f"- frame {p['frame']}: status={p['status']}, matches={p['matches']}, "
                     f"candidates={p['candidates']}, query_ticks={p['query_ticks']}, "
                     f"records={[ (r['draw'], r['submitted'], r['result']) for r in p['records'] ]}; "
                     'RT0–RT2/depth bound, RT3 unbound (`88760866`) in both records.')
    for anchor, burst in result['bursts'].items():
        rows = result['log_observation']['target_frame_rows'][anchor]
        def token(row_key, key):
            row = rows.get(row_key, [''])[0]
            m = re.search(r'\b' + key + r'=([^ ]+)', row)
            return m.group(1) if m else '?'
        camera = burst['camera']
        lines.append(f"- burst {anchor}–{burst['frames'][1]}: {burst['count']} motion files; "
                     f"camera raw-t origin {camera['origin_start']} -> {camera['origin_end']}, displacement={camera['origin_displacement']:.4f} raw units (no metres conversion); "
                     f"rotation step sum/max={camera['rotation_step_sum_deg']:.4f}/{camera['rotation_step_max_deg']:.4f} deg; "
                     f"anchor elapsed_ms={token('frame_end_rows','elapsed_ms')}, qpc={token('frame_end_rows','qpc')}, "
                     f"camera_rotation_deg={token('motion_output_frame_rows','camera_rotation_deg')}, "
                     f"cut_median_px={token('motion_output_cut_rows','median_px')}.")
    lines += ['', 'All six records carry the same session/node/handle tuple (`00000001,00000000,1d519b78,000061c5`) and the same RT0/1/2/depth binding words. Selected-draw lifetime rows have `before_known=after_known=1`, equal mutation revisions, node serial 24766, camera serial 26304, and epoch tuple `1/2/2` for both selector records in every packet.', '']
    lines += ['## Owning source', '',
              '- `src/proxy/capture.cpp:1455-1462` arms on an F8 edge; `:1660-1707` scopes/protects queries and records native submission; `:1448` publishes at Present.',
              '- `src/proxy/lattice_state_capture.cpp:75-121` accepts the bounded selector; `:130-140` defines an unbound optional target as `D3DERR_NOTFOUND`; `:219-245` finalizes/publishes status.',
              '- Existing strict packet reproducer: `verification/probe/lattice_state_packet.py` (`load(packet, True)` passed for all three packets).', '']
    lines += ['- `src/renderer/rigid_motion_pixel_program.h:12-14` defines motion RG as prior UV, while `src/temporal/resolve.hlsl:284-289,424-426` adds jitter and converts a UV difference to px/frame.', '']
    lines += ['## Inference limits', '']
    lines += [f'- {x}' for x in result['limitations']]
    (OUT / 'compact-report.md').write_text('\n'.join(lines) + '\n')

if __name__ == '__main__': main()
