#!/usr/bin/env python3
"""Disjointness of the FL SETA windows and claims from the FL music family's on the merged tree (read-only, headers
only). SETA: every identity window of seta_restore_sites_fl.h / seta_manual_flight_sites_fl.h plus the claimed spans
(factor hook, the stop-row span, the five SSE spans). Music: every window of music_windows_fl_inc.h, the alert routine
range alert_rva..alert_end_rva, and, conservatively, 16 bytes from every `*_rva` constant of the three music site
headers (claims, calls, entries; slot and IAT RVAs above .text are skipped). A window that overlaps a window is shared
read-only (both modules only compare those bytes) and listed; any overlap with a claim on either side (a SETA claim, or
a music RVA constant or the alert routine, which hold the music claims) fails. Exit 0 = no claim overlap.

usage: python3 verification/results/fl-seta/overlap_check.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
FL = ROOT / 'src/engine/fl'
TEXT_END = 0x14f000  # data and slot RVAs (0x15xxxx and above) are not code ranges


def seta_ranges():
    out = []
    for name in ('seta_restore_sites_fl.h', 'seta_manual_flight_sites_fl.h'):
        text = (FL / name).read_text()
        for w, rva, length in re.findall(r'\{"([a-z_0-9]+)", 0x([0-9a-f]+), (\d+), 0x[0-9a-f]{8}u,', text):
            out.append((f'{name}:{w}', int(rva, 16), int(length)))
    out.append(('claim:factor_hook', 0x0ce09e, 8))
    out.append(('claim:stop_rows', 0x0d2186, 0x0d21d0 - 0x0d2186))
    for axis, rva, n in (('yaw', 0x0ce878, 16), ('pitch', 0x0ceb88, 16), ('roll', 0x0ced98, 16),
                         ('strafe_y', 0x0d1f04, 15), ('strafe_x', 0x0d1f8b, 15)):
        out.append((f'claim:{axis}', rva, n))
    return out


def music_ranges():
    out = []
    inc = (FL / 'music_windows_fl_inc.h').read_text()
    for w, rva, length in re.findall(r'\{"([a-z_0-9]+)", 0x([0-9a-f]+), 0x([0-9a-f]+), \d+, \d+, 0x[0-9a-f]+\}', inc):
        out.append((f'music_window:{w}', int(rva, 16), int(length, 16)))
    alert = (FL / 'music_alert_sites_fl.h').read_text()
    a = re.search(r'alert_rva = 0x([0-9a-f]+), alert_end_rva = 0x([0-9a-f]+)', alert)
    if a:
        out.append(('music_alert:routine', int(a.group(1), 16), int(a.group(2), 16) - int(a.group(1), 16)))
    for name in ('media_cue_sites_fl.h', 'music_keep_sites_fl.h', 'music_alert_sites_fl.h'):
        for const, rva in re.findall(r'\b([a-z_0-9]+_rva) = 0x([0-9a-f]+)', (FL / name).read_text()):
            if int(rva, 16) < TEXT_END:
                out.append((f'{name}:{const}', int(rva, 16), 16))
    return out


def main():
    seta, music = seta_ranges(), music_ranges()
    hits = [(s, m) for s in seta for m in music if s[1] < m[1] + m[2] and m[1] < s[1] + s[2]]
    conflicts = 0
    for s, m in hits:
        shared = not s[0].startswith('claim:') and m[0].startswith('music_window:')
        conflicts += not shared
        print(f'{"SHARED_READ_ONLY" if shared else "CONFLICT"} {s[0]} [{s[1]:#08x},{s[1] + s[2]:#08x}) '
              f'{m[0]} [{m[1]:#08x},{m[1] + m[2]:#08x})')
    manager = next(r for r in seta if r[0].endswith(':manager'))
    print(f'seta ranges={len(seta)} music ranges={len(music)} overlaps={len(hits)} conflicts={conflicts} '
          f'manager=[{manager[1]:#08x},{manager[1] + manager[2]:#08x})')
    return 1 if conflicts else 0


if __name__ == '__main__':
    sys.exit(main())
