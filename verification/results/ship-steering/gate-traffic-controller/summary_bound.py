"""The worst-case length of the ship_steering_summary's gates[] text (ship_steering.cpp frame_boundary, buffer kSummaryGatesBytes):
every format string appended to gates[], each conversion at its widest (%lu / %u 10 characters, %ld 11, a float 24 [i]: 1e18 printed
with %.1f is 21 characters, %s 48: the longest state string is 40), the per-source drop list included. B.4: the source declares the
figure (kSummaryGatesWorstCase, static_assert'ed below the buffer); this script checks the declaration equals the computed worst case
and the buffer holds it (test_ship_steering runs it).
Usage: python3 -I summary_bound.py src/proxy/ship_steering_internal.h src/proxy/ship_steering_rows.cpp
(the source split of 2026-10-15: the drop names are in the internal header, the summary in the rows unit; the files are read as one
text in the order given; a single ship_steering.cpp of before the split still works)"""
import re
import sys

WIDTH = {'lu': 10, 'u': 10, 'ld': 11, 'f': 24, 's': 48, 'llu': 20, 'x': 8, 'lx': 8}


def bound(*paths):
    """(computed worst case, declared kSummaryGatesWorstCase, buffer kSummaryGatesBytes, drop sources)"""
    src = '\n'.join(open(p).read() for p in paths)
    declared = int(re.search(r'constexpr std::size_t kSummaryGatesWorstCase = (\d+);', src).group(1))
    buffer = int(re.search(r'constexpr std::size_t kSummaryGatesBytes = (\d+);', src).group(1))
    start = src.index('char gates[kSummaryGatesBytes] = "";')
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
    return total_with_loop, declared, buffer, drops


def main(*paths):
    worst, declared, buffer, drops = bound(*paths)
    ok = worst == declared and worst < buffer
    print('gates[] worst case %d characters (declared %d, buffer %d, %d drop sources) %s' % (worst, declared, buffer, drops,
                                                                                            'PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main(*sys.argv[1:]))
