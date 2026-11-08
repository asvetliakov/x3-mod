"""The span of a weapon-effects fixture.log where the pass drew no GPU work (gpu_us < 2 on a frame with bodies or
halos) and the big-tier readbacks, plus the DIAG rows of the instrumented run. Usage: blank_span.py <fixture.log>..."""
import re, sys
for path in sys.argv[1:]:
    rows = open(path, encoding='utf-8', errors='replace').read().splitlines()
    low = [i + 1 for i, l in enumerate(rows) if 'gpu_us=' in l and float(re.search(r'gpu_us=([\d.]+)', l).group(1)) < 2.0
           and re.search(r'(bodies=[1-9]|halo_alone)', l)]
    print(path)
    print('  low-gpu frames with bodies/halos:', len(low), 'lines', (low[0], low[-1]) if low else None)
    for i, l in enumerate(rows):
        if re.search(r'^DIAG|case=body_alone_fills_frame|case=size tier=capital_1500m|case=halo_big|case=reset |'
                     r'CHECK death_big|CHECK death_reset|RESULT', l):
            print(f'  {i + 1}: {l[:170]}')
