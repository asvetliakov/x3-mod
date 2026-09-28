#!/usr/bin/env python3
"""Release 0.7.0: compare the loaded sections of the stripped DLLs of 0.6.0 and 0.7.0 (tools/release/release.py
pe_image/section_bytes/marker_values). Prints JSON: size, sha256, header/export equality and, per differing
section, the differing byte runs with their ASCII context, each classed as marker, version or other.

  release-0.7.0-dll-compare.py OLD_DLL NEW_DLL
"""
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'tools' / 'release'))
import release  # noqa: E402


def runs(a, b):
    out, i, n = [], 0, min(len(a), len(b))
    while i < n:
        if a[i] != b[i]:
            j = i
            while j < n and a[j] != b[j]:
                j += 1
            out.append((i, j))
            i = j
        else:
            i += 1
    return out


def context(data, i, j):
    lo = max(data.rfind(b'\0', 0, i) + 1, i - 64)
    hi = data.find(b'\0', j)
    hi = j + 64 if hi < 0 or hi > j + 64 else hi
    return data[lo:hi].decode('ascii', 'replace')


def main(old_path, new_path):
    old, new = Path(old_path).read_bytes(), Path(new_path).read_bytes()
    (ho, so), (hn, sn) = release.pe_image(old), release.pe_image(new)
    result = dict(old=dict(path=old_path, bytes=len(old), sha256=release.sha256_file(old_path),
                           marker=release.marker_values(old)),
                  new=dict(path=new_path, bytes=len(new), sha256=release.sha256_file(new_path),
                           marker=release.marker_values(new)),
                  header_equal=ho == hn, exports_equal=release.pe_exports(old, ho, so) == release.pe_exports(new, hn, sn),
                  section_table_equal=[(s['name'], s['va'], s['vsize'], s['flags']) for s in so]
                  == [(s['name'], s['va'], s['vsize'], s['flags']) for s in sn],
                  sections={})
    newsec = {s['name']: s for s in sn}
    for s in so:
        if release.is_debug_section(s['name']) or s['name'] not in newsec:
            continue
        a, b = release.section_bytes(old, s), release.section_bytes(new, newsec[s['name']])
        if a == b:
            result['sections'][s['name']] = 'identical'
            continue
        rows = []
        for i, j in runs(a, b):
            ca, cb = context(a, i, j), context(b, i, j)
            kind = ('marker' if 'X3M_SOURCE_COMMIT=' in cb else
                    'version' if ('0.6' in ca and '0.7' in cb) else 'other')
            rows.append(dict(offset=i, length=j - i, kind=kind, old=ca[:120], new=cb[:120]))
        result['sections'][s['name']] = dict(len_equal=len(a) == len(b), runs=len(rows),
                                             kinds={k: sum(r['kind'] == k for r in rows) for k in
                                                    ('marker', 'version', 'other')},
                                             rows=rows[:20])
    print(json.dumps(result, indent=1))


if __name__ == '__main__':
    main(*sys.argv[1:3])
