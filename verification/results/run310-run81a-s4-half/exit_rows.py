"""Exit rows per session: count of engine_memory_read_refused, device_destroy, motion_output_release rows; the last four
row types of the log; the last frame_end; fault lines (Unhandled/page fault/Backtrace) in launcher-stderr.log.
usage: exit_rows.py RUN..."""
import glob, os, re, subprocess, sys
for run in sys.argv[1:]:
    d = f'/tmp/x3-bottleX3-run{run}'; log = sorted(glob.glob(f'{d}/session-*.log'))[0]
    cnt = {k: int(subprocess.run(['grep', '-c', f'^{k}', log], capture_output=True, text=True).stdout.strip() or 0)
           for k in ('engine_memory_read_refused', 'device_destroy', 'motion_output_release')}
    with open(log, 'rb') as f:
        f.seek(max(0, os.path.getsize(log) - 20000)); tail = f.read().decode('utf-8', 'replace').splitlines()[1:]
    last = [l.split(' ', 1)[0] for l in tail[-4:]]
    fe = next((re.search(r'frame=(\d+).*elapsed_ms=(\d+)', l).groups() for l in reversed(tail) if l.startswith('frame_end')), None)
    faults = sum(1 for l in open(f'{d}/launcher-stderr.log', errors='replace') if re.search(r'Unhandled|page fault|Backtrace', l))
    print(f'run{run} {cnt} last_rows={last} last_frame_end(frame,elapsed_ms)={fe} stderr_fault_lines={faults}')
