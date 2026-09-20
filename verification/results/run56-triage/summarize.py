#!/usr/bin/env python3
"""Produce the <=10 KiB checkpoint witness from triage.py output."""
from __future__ import annotations
import json
import re
from pathlib import Path

BASE = Path('/tmp/x3-run56-triage')
DETAIL = BASE / 'run56-capture-triage.json'
SESSION = Path('/tmp/x3-bottleX3-run200/session-20260921-014107-212.log')
STDERR = Path('/tmp/x3-bottleX3-run200/launcher-stderr.log')
CANDIDATE = Path('/tmp/x3-run56-candidate/candidate.json')
OUT = BASE / 'run56-capture-summary.json'
KV = re.compile(r'([A-Za-z][A-Za-z0-9_]*)=([^\s]+)')
ISO = re.compile(r'\[(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d\d\dZ)\]')

def fields(line): return dict(KV.findall(line))

def main():
    detail = json.loads(DETAIL.read_text())
    candidate = json.loads(CANDIDATE.read_text())
    light = {'rows': 0, 'admitted': 0, 'faded': 0, 'camera_not_1': 0,
             'min_gain': None, 'max_gain': None}
    motion = {'rows': 0, 'apply_failures': 0, 'restore_failures': 0}
    fog_tail = []
    with SESSION.open(errors='replace') as f:
        for n, line in enumerate(f, 1):
            kind = line.split(' ', 1)[0]
            x = fields(line)
            if kind == 'hull_lightmap_far_fade_frame':
                light['rows'] += 1; light['admitted'] += int(x['admitted']); light['faded'] += int(x['faded'])
                light['camera_not_1'] += x.get('camera') != '1'
                value = float(x['min_gain']); light['min_gain'] = value if light['min_gain'] is None else min(light['min_gain'], value)
                light['max_gain'] = value if light['max_gain'] is None else max(light['max_gain'], value)
            elif kind == 'motion_output_frame':
                motion['rows'] += 1; motion['apply_failures'] += int(x.get('apply_failures', 0)); motion['restore_failures'] += int(x.get('restore_failures', 0))
            elif kind in ('volumetric_fog_toggle', 'volumetric_fog_frame', 'volumetric_fog_cards'):
                if int(x.get('frame', '-1')) in (38634, 38740, 42600, 43200):
                    fog_tail.append({'line': n, 'kind': kind, **{k: x[k] for k in ('frame', 'enabled', 'applied', 'reason', 'profile', 'fault', 'refused', 'warmup', 'ready') if k in x}})
    stamps = []
    with STDERR.open(errors='replace') as f:
        for line in f:
            m = ISO.search(line)
            if m: stamps.append(m.group(1))
    config = detail['configuration']
    c = lambda name: config.get(name, {}).get('fields', {})
    def burst_frame(n):
        rows = detail['capture_frame_summaries'][n]
        return {kind: row['fields'] for kind, row in rows.items()
                if kind in ('hull_lightmap_far_fade_frame', 'fade_route_frame',
                            'motion_output_frame', 'camera_state', 'frame_end')}
    summary = {
      'schema': 1, 'observation_only': True,
      'sources': detail['sources'],
      'candidate': {k: candidate[k] for k in ('checkout','commit','sha256','bytes','configuration','compiler','source_clean')},
      'runtime_proxy_identity': {k: c('proxy_identity').get(k) for k in ('path', 'bytes', 'sha256', 'source_commit')},
      'time_bounds_from_stderr': {'first': stamps[0] if stamps else None, 'last': stamps[-1] if stamps else None,
                                  'last_record_is_process_detach': 'PROCESS_DETACH' in detail['stderr_last_record']},
      'capture': detail['capture_inventory'],
      'configured_modes': {
        'fog': {k: c('volumetric_fog_mode').get(k) for k in ('requested','enabled','strength','density_scale','anisotropy','cards','keys')},
        'far_fade': {k: c('light_map_far_fade_mode').get(k) for k in ('requested','enabled','p0','p1','floor','gain')},
        'media_skip': {k: c('media_cue_mode').get(k) for k in ('enabled','id2_video_skip','status','trace','cache','owner')},
        'media_site': c('media_cue_site'), 'media_video_witness': c('media_video_witness')},
      'observed': {
        'fog_toggles_near_bursts': [x for x in detail['toggles'] if x['kind'] == 'volumetric_fog_toggle' and x['fields'].get('frame') in ('15063', '15110', '15143', '15162', '38634', '38740')],
        'fog_tail_from_frame_38000': fog_tail,
        'lightmap_all_session': light, 'motion_all_session': motion,
        'final_burst_first': burst_frame('43051'),
        'final_burst_last': burst_frame('43082'),
        'media_cue_rows': len(detail['media']['rows']),
        'media_cue_id_kind_counts': {'144:none': 3, '244:none': 3, '1:none': 4, '8404:none': 2, '8100:none': 1, '25:none': 1, '40:none': 1},
        'lav_loader_token_rows_in_stderr': detail['media']['lav_substring_rows_in_stderr'],
        'crash_markers': {'session': detail['session_marker_counts'], 'stderr': detail['stderr_marker_counts']},
      },
      'reported_by_user_not_independently_measured': ['Run56 had no crash and no media stutter.', 'Fog was seen in the first one or two F8 captures; distant-station lightmap flashes occurred during camera motion, including when fog was off. User is uncertain which burst was first/latest.'],
      'limits': ['No process exit status is present in either captured log; trailing Wine PROCESS_DETACH is termination evidence, not a normal-exit proof.', 'Media skip returns before cue telemetry, so per-call skip execution is unobservable here; zero ID2 cue records do not show that the branch was unexercised.', 'LAV substring count 0 is loader-log absence only; it does not establish the installed provider was absent or unused.'],
    }
    OUT.write_text(json.dumps(summary, indent=2, sort_keys=True) + '\n')
    if OUT.stat().st_size > 10 * 1024: raise SystemExit(f'summary too large: {OUT.stat().st_size}')

if __name__ == '__main__': main()
