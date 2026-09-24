"""Per session: motion_output_frame TAA outcome counts (taa_resolved, taa_skip codes, taa_result), taa_hdr frames, the
taa_history_taps row, and median taa_run_us over resolved frames (CPU-side encode time, not GPU; gpu_sync was off).
usage: taa_frames.py"""
import glob, re, collections, statistics as st
for run in ('295', '296', '297', '298'):
    log = glob.glob(f'/tmp/x3-bottleX3-run{run}/session-*.log')[0]; c = collections.Counter(); run_us = []; taps = ''
    for l in open(log, errors='replace'):
        if l.startswith('motion_output_frame'):
            d = dict(re.findall(r' (taa_resolved|taa_skip|taa_result|taa_hdr|taa_run_us)=(\S+)', l))
            c[('resolved', d['taa_resolved'])] += 1; c[('skip', d['taa_skip'])] += 1; c[('result', d['taa_result'])] += 1
            if d['taa_resolved'] == '1': run_us.append(float(d['taa_run_us']))
        elif l.startswith('motion_output_taa_history_taps'): taps = l.strip()
    print(f"run{run} {taps} | {dict(sorted(c.items()))} | taa_run_us p50 {st.median(run_us) if run_us else 'n/a'} over {len(run_us)} resolved frames")
