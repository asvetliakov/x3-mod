"""The 60 fps / seed 1 rows of two ship_steering_host outputs side by side: for every row label present in both, the
key=value numbers that changed (old -> new). Rows only in one file are listed by label.
Usage: python3 -I fixture_deltas.py old/ship_steering_host.txt verification/results/ship-steering/ship_steering_host.txt"""
import re
import sys

NUM = re.compile(r'([A-Za-z_][\w()/>-]*)=\s*(-?[\d.]+)')


def rows(path):
    out = {}
    for line in open(path, encoding='utf-8', errors='replace'):
        line = line.rstrip('\n')
        if not line or line.startswith(('ship_steering_host', 'checks', 'GAP', 'FAIL')):
            continue
        head = line.split('=', 1)[0].rsplit(' ', 1)[0]  # the label up to the first key=value
        label = ' '.join(head.split())
        if 'theta' in line and 'theta' not in label:
            label += ' theta ' + line.split('theta', 1)[1].split()[0]
        if ' sub=' in line:
            label += ' sub=' + line.split(' sub=', 1)[1].split()[0]
        if ' slot=' in line:
            label += ' slot=' + line.split(' slot=', 1)[1].split()[0]
        k = label
        n = 2
        while k in out:
            k = '%s #%d' % (label, n)
            n += 1
        out[k] = NUM.findall(line)
    return out


def main(old, new):
    a, b = rows(old), rows(new)
    for k in a:
        if k not in b:
            print('only old:', k)
            continue
        changed = []
        for (ka, va), (kb, vb) in zip(a[k], b[k]):
            if ka == kb and va != vb:
                changed.append('%s %s -> %s' % (ka, va, vb))
        if changed:
            print('%s: %s' % (k, '; '.join(changed)))
    for k in b:
        if k not in a:
            print('only new:', k)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
