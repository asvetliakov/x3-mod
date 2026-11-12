#!/usr/bin/env python3
"""FL 70 A log query: print selected key=value fields of one row kind in a frame window.

usage: log_fields.py LOG ROWKIND key1,key2,... FRAME_LO FRAME_HI [FILTER_KEY=VALUE ...]
Streams the log line by line (the FL 70 A x3m.log is 133 MB); prints only the asked fields.
"""
import sys


def main() -> None:
    path, kind, keys = sys.argv[1], sys.argv[2], sys.argv[3].split(',')
    lo, hi = int(sys.argv[4]), int(sys.argv[5])
    filt = dict(a.split('=', 1) for a in sys.argv[6:])
    prefix = kind + ' '
    with open(path, errors='replace') as f:
        for line in f:
            if not line.startswith(prefix):
                continue
            d = dict(kv.split('=', 1) for kv in line.split()[1:] if '=' in kv)
            try:
                fr = int(d.get('frame', -1))
            except ValueError:
                continue
            if not lo <= fr <= hi or any(d.get(k) != v for k, v in filt.items()):
                continue
            print(' '.join(f"{k}={d.get(k, '?')}" for k in ['frame'] + keys))


if __name__ == '__main__':
    main()
