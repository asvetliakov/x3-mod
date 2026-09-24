"""Bolt footprint rows per session: sums over bolt_footprint windows (draws, written, expanded, gated, refused_*, failures),
the last row's session_* counters, bolt_footprint_refused reasons, bolt_footprint_buffer rows, and the chase-view
bolt_footprint_hist instance total. usage: bolt_rows.py RUN..."""
import glob, re, sys, collections
for run in sys.argv[1:]:
    logs = sorted(glob.glob(f'/tmp/x3-bottleX3-run{run}/session-*.log'))
    if not logs: print(f'run{run} no log'); continue
    s = collections.Counter(); last = ''; ref = collections.Counter(); buf = 0; hist = 0; nwin = 0
    for l in open(logs[0], errors='replace'):
        if l.startswith('bolt_footprint device'):
            nwin += 1; last = l
            for k, v in re.findall(r' (draws|written|expanded|gated|instances|failures|refused_\w+)=(\d+)', l): s[k] += int(v)
        elif l.startswith('bolt_footprint_refused'): ref[re.search(r'reason=(\w+)', l).group(1)] += 1
        elif l.startswith('bolt_footprint_buffer'): buf += 1
        elif l.startswith('bolt_footprint_hist'): hist += int(re.search(r'instances=(\d+)', l).group(1))
    sess = ' '.join(re.findall(r'session_\w+=\d+', last))
    print(f'run{run} windows={nwin} sums={dict(s)} | last: {sess} | refused_rows={dict(ref)} buffer_rows={buf} hist_instances={hist}')
