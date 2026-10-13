"""The worst-case length of the ship_steering_summary's gates[] text (ship_steering.cpp frame_boundary, buffer 4096 B): every
format string appended to gates[], each conversion at its widest (%lu / %u 10 characters, %ld 11, a float 24 [i]: 1e18 printed with
%.1f is 21 characters, %s 48: the longest state string is 40), the per-source drop list included.
Usage: python3 -I summary_bound.py src/proxy/ship_steering.cpp"""
import re
import sys

WIDTH = {'lu': 10, 'u': 10, 'ld': 11, 'f': 24, 's': 48, 'llu': 20, 'x': 8, 'lx': 8}


def main(path):
    src = open(path).read()
    start = src.index('char gates[4096] = "";')
    end = src.index('char trace[96] = "";', start)
    body = src[start:end]
    total = 0
    # every string literal (adjacent ones concatenate) inside snprintf calls on gates
    for call in re.finditer(r'std::snprintf\(gates[^;]*?;', body, re.S):
        fmt = ''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', call.group(0)))
        n = 0
        for m in re.finditer(r'%[-0-9.]*(llu|lu|ld|lx|u|f|s|x)|[^%]', fmt):
            n += WIDTH[m.group(1)] if m.group(1) else 1
        total += n
    drops = len(re.findall(r'"[a-z_]+"', src[src.index('kDropNames[unsigned(Drop::count)]'):src.index('Counter rows_dropped_')]))
    total_with_loop = total + (drops - 1) * (WIDTH['s'] + WIDTH['lu'] + 2)  # the loop's format counted once above
    print('gates[] worst case %d characters (buffer 4096, %d drop sources)' % (total_with_loop, drops))
    return 0 if total_with_loop < 4096 else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
