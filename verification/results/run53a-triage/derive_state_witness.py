#!/usr/bin/env python3
"""Derive compact Run53A receipt/state evidence from preserved read-only files."""
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
RUN = Path('/tmp/x3-bottleX3-run193')
LOG = RUN / 'session-20260920-215401-212.log'
PACKETS = sorted(RUN.glob('lattice-state-*.json'))
OUT = Path(__file__).with_name('state-witness.json')
sys.path.insert(0, str(ROOT / 'verification/probe'))
from lattice_state_packet import load  # noqa: E402


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def words(record):
    return {(field['kind'], field['index']): field['words'] for field in record['fields']}


def line_fields(line):
    return dict(part.split('=', 1) for part in line.split() if '=' in part)


def burst_state(start, end):
    cameras, motions = [], []
    for line in LOG.open(errors='replace'):
        if not line.startswith(('camera_state', 'motion_output_frame')):
            continue
        fields = line_fields(line)
        frame = int(fields.get('frame', '-1'))
        if start <= frame <= end:
            (cameras if line.startswith('camera_state') else motions).append(fields)
    first, last = cameras[0], cameras[-1]
    t0 = [float(x) for x in first['t'].split(',')]
    t1 = [float(x) for x in last['t'].split(',')]
    return {
        'frames': [start, end], 'camera_rows': len(cameras), 'motion_rows': len(motions),
        'rotation_deg_max': max(float(x['rotation_deg']) for x in cameras),
        'view_translation_delta': [round(b - a, 3) for a, b in zip(t0, t1)],
        'motion_classification': ('stationary_view' if max(float(x['rotation_deg']) for x in cameras) == 0
                                  else 'unclassified_nonzero_rotation'),
        'routed_minmax': [min(int(x['routed']) for x in motions), max(int(x['routed']) for x in motions)],
        'cut_median_px_max': max(float(x['cut_median_px']) for x in motions),
        'cut_missing_max': max(float(x['cut_missing']) for x in motions),
    }


def differing_words(a, b):
    return sum(sum(x != y for x, y in zip(xa, xb)) + abs(len(xa) - len(xb))
               for key, xa in a.items() for xb in [b[key]] if xa != xb)


def main():
    packets = [load(path, require_complete=True) for path in PACKETS]
    if len(packets) != 3:
        raise ValueError(f'expected 3 packets, got {len(packets)}')
    frames = [packet['frame'] for packet in packets]
    records = [[words(record) for record in packet['records']] for packet in packets]
    identity = [('source_shader', 0), ('source_shader', 1), ('declaration', 0), ('route', 0),
                ('caps', 0), ('viewport', 0), ('scissor', 0), *[('clip', n) for n in range(6)]]
    same_identity = all(records[0][0][key] == records[i][slot][key]
                        for i in range(3) for slot in range(2) for key in identity)
    cross = []
    for slot in range(2):
        base = records[0][slot]
        for i in (1, 2):
            changed = [key for key in base if base[key] != records[i][slot][key]]
            cross.append({'slot': slot, 'frames': [frames[0], frames[i]],
                          'fields': [f'{kind}/{index}' for kind, index in changed],
                          'changed_words': differing_words(base, records[i][slot])})
    slot_pairs = []
    for packet, pair in zip(packets, records):
        changed = [key for key in pair[0] if pair[0][key] != pair[1][key]]
        slot_pairs.append({'frame': packet['frame'], 'changed_fields': len(changed),
                           'changed_words': differing_words(pair[0], pair[1]),
                           'unchanged_pipeline_identity': all(pair[0][key] == pair[1][key] for key in identity)})
    data = {
        'schema': 1,
        'input': {'session': str(RUN), 'session_log_sha256': sha(LOG),
                  'packets': [{'name': p.name, 'sha256': sha(p), 'bytes': p.stat().st_size} for p in PACKETS]},
        'receipt': {'file_count': len(list(RUN.iterdir())), 'bursts': [[4558, 4589], [5257, 5288], [8691, 8722]],
                    'packet_frames': frames, 'validated_complete_packets': len(packets)},
        'packets': [{'frame': p['frame'], 'device': p['device'], 'generation': p['generation'],
                     'candidates': p['candidates'], 'matches': p['matches'],
                     'draws': [r['draw'] for r in p['records']],
                     'query_us': p['query_ticks'] * 1_000_000 / p['qpc_frequency'],
                     'coherence': p['draw_input_coherence'], 'payload': p['payload_copy_valid']} for p in packets],
        'motion_bursts': [burst_state(4558, 4589), burst_state(5257, 5288), burst_state(8691, 8722)],
        'state_comparison': {'all_six_same_shader_declaration_route_caps_viewport_scissor_clip': same_identity,
                             'cross_burst': cross, 'within_frame_slots': slot_pairs,
                             'fixture_comparison': 'output schema lacks effective D3D state; source/input-manifest comparison delegated'},
        'limits': ['Complete packet validates selected state observation only.',
                   'No payload identity, coherent input lifetime, fragment ownership, later-writer, RGB, or TAA conclusion.'],
    }
    OUT.write_text(json.dumps(data, indent=2, sort_keys=True) + '\n')
    print(json.dumps({'result': 'PASS', 'output': str(OUT), 'packets': len(packets), 'frames': frames}, sort_keys=True))


if __name__ == '__main__':
    main()
