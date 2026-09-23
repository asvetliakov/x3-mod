#!/usr/bin/env python3
"""bob1.parse_text against the engine reference (verification/results/bob1-format/text_loader_reference.py,
the port of 0x00483f20 kept as the oracle) over every winning text body of a game root. Read-only.

  PYTHONPATH=tools/analysis python3 engine_oracle.py [GAME]

Per body that bob1.parse_text accepts: the reference parses the same body text from the first
record on (the material records are skipped with bob1's reader) and every record is compared:
value, point count, every point's int16 position (v >> 2), uv, smoothing value, int16 normal
(largest component difference), and the triangle lists (indices and face word). Bodies
parse_text refuses are counted by reason. Prints counts only.
"""
import collections
import sys
from pathlib import Path

HERE = Path(__file__).resolve()
sys.path.insert(0, str(HERE.parents[2] / 'bob1-format'))
import bob1                          # noqa: E402
import sector_fog_census as sfc      # noqa: E402
import text_loader_reference as ref  # noqa: E402

NORMAL_TOL = 4                       # int16 units (16384 = 1)


def body_start(data):
    """Line where the first record starts, reading the materials with bob1's reader."""
    r = bob1.TextReader(data)
    while not r.done() and r.peek().upper().startswith('MATERIAL'):
        key, _, first = r.next().partition(':')
        bob1._read_text_material(r, key.strip().upper(), first.strip())
    return r.fields[r.i][1], r.peek()


def compare_body(data, tree, S):
    text = (bytes(data)[3:] if bytes(data).startswith(b'\xef\xbb\xbf') else bytes(data)).decode('latin1')
    line, first = body_start(data)
    lx = ref.Lex('\n'.join(text.split('\n')[line - 1:]))
    if not lx.toks or lx.toks[0].strip() != first:
        S['reference_body_start_mismatch'] += 1
        return
    eng, stats = [], collections.Counter()
    while not lx.done():
        eng.append(ref.engine_record(lx, stats))
    lods = bob1.lods(tree)
    S['records'] += len(lods)
    S['record_count_diff'] += len(eng) != len(lods)
    for e, b in zip(eng, lods):
        S['value_diff'] += e['value'] != b['value']
        pts = b['points']
        if len(e['points']) != len(pts):
            S['record_point_count_diff'] += 1
            continue
        S['points'] += len(pts)
        pos = sum(1 for p, q in zip(e['points'], pts) if p['p16'] != tuple(c >> 2 for c in q[1:4]))
        S['point_position_diff'] += pos
        S['record_position_diff'] += pos > 0
        S['point_uv_diff'] += sum(1 for p, q in zip(e['points'], pts) if p['uv'][:2] != tuple(q[4:6]))
        S['point_smoothing_diff'] += sum(1 for p, q in zip(e['points'], pts) if p['smooth'] != q[-1])
        for p, q in zip(e['points'], pts):
            d = max(abs(x - (y >> 2)) for x, y in zip(p['n16'], q[-4:-1]))
            S['normal_d0' if d == 0 else 'normal_d1_4' if d <= NORMAL_TOL else 'normal_over_tol'] += 1
        ef = [f for part in e['parts'] for _, g in part['groups'] for f in g]
        bf = [f for part in b['parts'] for g in part['groups'] for f in g['faces']]
        S['faces'] += len(ef)
        S['face_diff'] += sum(1 for x, y in zip(ef, bf) if tuple(x) != tuple(y)) + abs(len(ef) - len(bf))
        S['group_material_diff'] += ([[m for m, _ in part['groups']] for part in e['parts']]
                                     != [[g['material'] for g in part['groups']] for part in b['parts']])
        S['part_flag_diff'] += [part['flags'] for part in e['parts']] != [part['flags'] for part in b['parts']]


def compare_game(game):
    a = sfc.Assets(Path(game))
    keys = sorted(k for k, v in a.entries.items() if v[-1]['path'].lower().endswith(('.pbd', '.bod')))
    S = collections.Counter()
    for k in keys:
        data = a.read_entry(a.entries[k][-1])
        a.cache.clear()
        if bob1.text_kind(data) == 'scene':
            continue
        S['bodies'] += 1
        try:
            tree = bob1.parse_text(data)
        except bob1.FormatError as exc:
            msg = str(exc)
            S['refused: ' + ('MATERIAL3' if 'MATERIAL3' in msg else "MATERIAL (MAT1)" if "'MATERIAL'" in msg
                             else msg.split(': ', 1)[-1][:50])] += 1
            continue
        S['compared'] += 1
        compare_body(data, tree, S)
    return S


def main():
    S = compare_game(sys.argv[1] if len(sys.argv) > 1 else bob1.DEFAULT_GAME)
    for k in sorted(S):
        print(f'{k} {S[k]}')


if __name__ == '__main__':
    main()
