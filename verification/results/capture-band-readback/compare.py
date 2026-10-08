"""Compare capture readback files and rows between fixture case directories.

usage: python3 compare.py LABEL=CASE_DIRECTORY [LABEL=CASE_DIRECTORY ...]
       python3 compare.py --runs RUNS_FILE LABEL [LABEL ...]   (directories from run_cases.py's OUT_FILE)

For every case directory (run_motion_output.py's motion-output-<case>-<stamp>,
raw and untracked under verification/probe/build/) this reads the session
log's `*_readback` rows (tag, file, width, height, format, result, bytes,
bands, staging_bytes, fallback) and the files under x3-modern-captures/. The
first directory is the reference: every other directory's files are compared
byte for byte by SHA-256 against it. Per format it prints the file count,
identical/different/missing counts, the bands and staging_bytes seen, and the
per-file route_readback time from the motion_output_frame rows
(readback_us / readbacks for the frame) and the session's route_readback
telemetry summary (count, mean, min, max). Prints only summary lines.
"""
import collections
import hashlib
import pathlib
import re
import statistics
import sys

FIELD = re.compile(r'(\w+)=(\S+)')


def fields(line):
    return dict(FIELD.findall(line))


def load(directory):
    directory = pathlib.Path(directory)
    captures = directory / 'x3-modern-captures'
    logs = sorted(captures.glob('session-*.log'))
    assert len(logs) == 1, (directory, logs)
    rows, frames, summary = [], [], None
    with logs[0].open(errors='replace') as log:
        for line in log:
            tag = line.split(' ', 1)[0]
            if tag.endswith('_readback') and ' file=' in line:
                row = fields(line)
                row['tag'] = tag
                rows.append(row)
            elif tag == 'motion_output_frame' and 'readbacks=' in line:
                frames.append(fields(line))
            elif 'route_readback count=' in line:
                summary = fields(line[line.index('route_readback count='):].split(' buckets=')[0])
    files = {}
    for row in rows:
        path = captures / row['file']
        files[row['file']] = hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None
    return rows, files, frames, summary


def main(argv):
    if argv[:1] == ['--runs']:
        listed = dict(re.findall(r'^(\S+) \S+ exit=\S+ dir=(\S+)', pathlib.Path(argv[1]).read_text(), re.M))
        argv = [f'{label}={listed[label]}' for label in argv[2:]]
    runs = [a.split('=', 1) for a in argv]
    loaded = {label: load(path) for label, path in runs}
    reference_label = runs[0][0]
    _, reference_files, _, _ = loaded[reference_label]
    for label, _ in runs:
        rows, files, frames, summary = loaded[label]
        by_format = collections.defaultdict(list)
        for row in rows:
            by_format[row['format']].append(row)
        print(f'[{label}] rows={len(rows)} files_present={sum(v is not None for v in files.values())}')
        for fmt, group in sorted(by_format.items()):
            same = diff = missing = 0
            for row in group:
                mine, ref = files.get(row['file']), reference_files.get(row['file'])
                if mine is None or ref is None:
                    missing += 1
                elif mine == ref:
                    same += 1
                else:
                    diff += 1
            results = collections.Counter(row['result'] for row in group)
            print(f'  {fmt}: files={len(group)} sizes={sorted({(row["width"], row["height"]) for row in group})} '
                  f'identical_to_{reference_label}={same} different={diff} missing={missing} results={dict(results)} '
                  f'bands={sorted({row.get("bands") for row in group})} '
                  f'staging_bytes={sorted({row.get("staging_bytes") for row in group})} '
                  f'fallback={dict(collections.Counter(row.get("fallback") for row in group))} '
                  f'bytes_total={sum(int(row["bytes"]) for row in group)}')
        per_file = [float(f['readback_us']) / int(f['readbacks']) for f in frames
                    if 'readback_us' in f and int(f.get('readbacks', 0) or 0) > 0]
        if per_file:
            print(f'  route_readback per file: frames={len(per_file)} median_us={statistics.median(per_file):.1f} '
                  f'min_us={min(per_file):.1f} max_us={max(per_file):.1f}')
        if summary:
            count = int(summary['count'])
            print(f'  route_readback telemetry: count={count} mean_us={float(summary["total_us"]) / max(count, 1):.1f} '
                  f'min_us={summary["min_us"]} max_us={summary["max_us"]} failures={summary["failures"]}')


if __name__ == '__main__':
    main(sys.argv[1:])
