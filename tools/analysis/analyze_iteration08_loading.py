#!/usr/bin/env python3
"""Characterize long loading gaps in a diagnostics 0.4 telemetry log.

The input logs are hundreds of megabytes of per-draw capture records, so this
tool streams the file line by line and retains only the sparse telemetry and
loading records. Nothing from the raw capture payload is kept or emitted.

Definitions used here, and their limits:

* A **presentation gap** is a Present-to-Present interval measured by the
  proxy's own `frame_normal` metric. The metric reports count/total/min/max per
  report window, so a window whose `max_us` exceeds the threshold contains one
  gap of exactly that length. Its end is bounded by the report timestamp minus
  the remaining intervals of the same window; its start is the end minus the
  gap. This is a bound, not an exact Present timestamp.
* A **report window** is the interval between two consecutive process
  (`device=0`) summaries. `loading_metric` deltas are posted on call
  completion inside that interval; a call may start before the window opens.
  Windows are therefore attributed to a gap by overlap, and windows that
  straddle a gap boundary are reported separately from fully contained ones.
* Loading spans are inclusive wall time and subtract nested *loading* spans
  only. Only the exclusive column may be added, and even that sum is a lower
  bound on instrumented time because concurrent threads overlap.
* A **report stall** is an interval between consecutive reports much longer
  than the nominal one-second cadence. Reports are emitted from instrumented
  callbacks, so a stall is evidence that no instrumented boundary was crossed;
  it is not proof of a particular kind of work.
"""
import argparse
import hashlib
import json
from pathlib import Path

KEEP = ('telemetry_start', 'telemetry_span', 'telemetry_summary', 'telemetry_metric',
        'loading_metric', 'loading_trace', 'loading_hook', 'mesh_hook',
        'telemetry_first_present', 'telemetry_cursor_poll', 'telemetry_presentation',
        'create_device', 'create_device_result', 'capture_event',
        'loading_probe', 'loading_probe_site', 'loading_probe_caller', 'frame_end')
METRIC_KEYS = ('count', 'failures', 'pending', 'ambiguous', 'bytes',
               'inclusive_ticks', 'exclusive_ticks', 'max_ticks', 'wrapper_tail_ticks')
# Engine probe rows (loading_probe lines, docs/verification/loading-probes.md): deltas per
# report window like loading_metric; x0..x3 are per-site extras named by loading_probe_site.
PROBE_KEYS = ('calls', 'exits', 'inclusive_ticks', 'max_ticks', 'overflow', 'desync', 'bytes', 'x0', 'x1', 'x2', 'x3')
# Fields compared when deciding whether two report windows repeat the same work.
VECTOR_KEYS = ('count', 'failures', 'pending', 'ambiguous', 'bytes')


def fields(line):
    values = {}
    for token in line.split()[1:]:
        key, _, value = token.partition('=')
        values[key] = value
    return values


def scan(path, hash_source=True):
    """Stream the log, keeping only sparse telemetry lines and the file digest."""
    digest = hashlib.sha256()
    kept, total = [], 0
    with path.open('rb') as stream:
        for raw in stream:
            total += len(raw)
            if hash_source:
                digest.update(raw)
            if not raw.endswith(b'\n'):
                continue
            head = raw.split(b' ', 1)[0].decode('ascii', 'replace')
            if head in KEEP:
                kept.append(raw.decode('utf-8', 'replace').rstrip('\n'))
    return kept, dict(name=path.name, bytes=total,
                      sha256=digest.hexdigest() if hash_source else None)


def parse(lines):
    """Build the clock, anchors, presentation windows and loading report windows."""
    clock = None
    anchors = dict(spans=[], first_presents=[], create_device=None, hooks=0,
                   coverage_begin_qpc=None, mesh_tables=0, probe_sites={}, frame_ends=[])
    present, loading, cursor, captures = [], [], [], []
    current = None
    previous_report_qpc = None
    for line in lines:
        event = line.partition(' ')[0]
        f = fields(line)
        if event == 'frame_end':
            # Every mode writes these (every 300 frames or a capture frame) with
            # elapsed_ms since DllMain and dt_ms since the previous line, so a
            # plain --direct log still bounds its load gaps (frame_end_gaps).
            if 'elapsed_ms' in f:
                anchors['frame_ends'].append(dict(
                    device=int(f['device']), frame=int(f['frame']), capture=f.get('capture') == '1',
                    elapsed_seconds=int(f['elapsed_ms']) / 1000.0, dt_seconds=int(f.get('dt_ms', 0)) / 1000.0,
                    qpc=int(f['qpc']) if 'qpc' in f else None))
            continue
        if event == 'telemetry_start':
            frequency = int(f['qpc_frequency'])
            if frequency <= 0:
                raise ValueError('invalid QPC frequency')
            clock = dict(frequency=frequency, start_qpc=int(f['qpc']), anchor=f.get('anchor'))
            continue
        if clock is None:
            continue
        if event == 'loading_trace':
            anchors['coverage_begin_qpc'] = int(f['coverage_begin'])
            anchors['hooks'] = int(f['hooks'])
        elif event == 'mesh_hook' and f.get('installed') == '1':
            anchors['mesh_tables'] += 1
        elif event == 'telemetry_span':
            begin, end = int(f['qpc_begin']), int(f['qpc_end'])
            if end < begin:
                raise ValueError('reversed span')
            anchors['spans'].append(dict(name=f['name'], begin_seconds=seconds(begin, clock),
                                         duration_seconds=(end - begin) / clock['frequency'],
                                         success=f.get('success')))
        elif event == 'telemetry_first_present':
            anchors['first_presents'].append(dict(device=int(f['device']), frame=int(f['frame']),
                                                  seconds=seconds(int(f['qpc']), clock)))
        elif event == 'create_device':
            anchors['create_device'] = dict(width=int(f['width']), height=int(f['height']),
                                            windowed=f.get('windowed'), interval=f.get('interval'))
        elif event == 'telemetry_cursor_poll':
            cursor.append(dict(frame=int(f['frame']), seconds=seconds(int(f['qpc']), clock),
                               flags=f.get('flags')))
        elif event == 'capture_event':
            captures.append(dict(frame=int(f['frame']), seconds=seconds(int(f['qpc']), clock)))
        elif event == 'telemetry_summary':
            stamp = int(f['qpc'])
            if f['device'] == '0':
                current = dict(begin_seconds=seconds(previous_report_qpc, clock)
                               if previous_report_qpc is not None else seconds(stamp, clock),
                               report_seconds=seconds(stamp, clock), report_qpc=stamp, metrics={},
                               probes={}, probe_callers={})
                loading.append(current)
                previous_report_qpc = stamp
            else:
                present.append(dict(device=int(f['device']), frame=int(f['frame']),
                                    seconds=seconds(stamp, clock), frame_normal=None))
        elif event == 'telemetry_metric':
            if f.get('name') == 'frame_normal' and present and f.get('device') != '0':
                present[-1]['frame_normal'] = dict(
                    count=int(f['count']), total_seconds=float(f['total_us']) / 1e6,
                    min_seconds=float(f['min_us']) / 1e6, max_seconds=float(f['max_us']) / 1e6)
        elif event == 'loading_metric':
            if current is None:
                raise ValueError('loading delta without a preceding process summary')
            metric = {key: int(f[key]) for key in METRIC_KEYS}
            if min(metric.values()) < 0:
                raise ValueError('negative loading delta')
            for key in ('inclusive', 'exclusive', 'max', 'wrapper_tail'):
                metric[key + '_seconds'] = metric[key + '_ticks'] / clock['frequency']
            operation = f['op']
            existing = current['metrics'].get(operation)
            if existing:
                for key in METRIC_KEYS:
                    existing[key] += metric[key]
                for key in ('inclusive', 'exclusive', 'wrapper_tail'):
                    existing[key + '_seconds'] += metric[key + '_seconds']
                existing['max_seconds'] = max(existing['max_seconds'], metric['max_seconds'])
            else:
                current['metrics'][operation] = metric
            stamp = int(f['qpc'])
            if stamp > current['report_qpc']:
                current['report_qpc'] = stamp
                current['report_seconds'] = seconds(stamp, clock)
                previous_report_qpc = stamp
        elif event == 'loading_probe_site':
            anchors['probe_sites'][f['name']] = dict(
                index=int(f.get('index', 0)), va=f.get('va'), length=int(f.get('length', 0)),
                timed=f.get('timed') == '1', kind=f.get('kind'), status=f.get('status'),
                extras=[f.get('x0', '-'), f.get('x1', '-'), f.get('x2', '-'), f.get('x3', '-')])
        elif event == 'loading_probe':
            if current is None:
                raise ValueError('loading probe delta without a preceding process summary')
            row = {key: int(f[key]) for key in PROBE_KEYS}
            if min(row.values()) < 0:
                raise ValueError('negative probe delta')
            row['inclusive_seconds'] = row['inclusive_ticks'] / clock['frequency']
            row['max_seconds'] = row['max_ticks'] / clock['frequency']
            existing = current['probes'].get(f['site'])
            if existing:
                for key in PROBE_KEYS + ('inclusive_seconds',):
                    if key not in ('max_ticks',):
                        existing[key] += row[key]
                existing['max_ticks'] = max(existing['max_ticks'], row['max_ticks'])
                existing['max_seconds'] = max(existing['max_seconds'], row['max_seconds'])
            else:
                current['probes'][f['site']] = row
            stamp = int(f['qpc'])
            if stamp > current['report_qpc']:
                current['report_qpc'] = stamp
                current['report_seconds'] = seconds(stamp, clock)
                previous_report_qpc = stamp
        elif event == 'loading_probe_caller':
            if current is not None:
                key = (f['site'], f['caller'])
                current['probe_callers'][key] = current['probe_callers'].get(key, 0) + int(f['calls'])
    if clock is None and not anchors['frame_ends']:
        raise ValueError('missing telemetry clock')
    return clock, anchors, present, loading, cursor, captures


def probe_totals(windows):
    """Per-site probe totals over report windows (deltas summed; maxima kept)."""
    result = {}
    callers = {}
    for window in windows:
        for site, row in window.get('probes', {}).items():
            aggregate = result.setdefault(site, dict(
                calls=0, exits=0, inclusive_ticks=0, max_ticks=0, overflow=0, desync=0, bytes=0,
                x0=0, x1=0, x2=0, x3=0, inclusive_seconds=0.0, max_seconds=0.0))
            for key in PROBE_KEYS + ('inclusive_seconds',):
                if key != 'max_ticks':
                    aggregate[key] += row[key]
            aggregate['max_ticks'] = max(aggregate['max_ticks'], row['max_ticks'])
            aggregate['max_seconds'] = max(aggregate['max_seconds'], row['max_seconds'])
        for (site, caller), count in window.get('probe_callers', {}).items():
            callers.setdefault(site, {})[caller] = callers.get(site, {}).get(caller, 0) + count
    for site, aggregate in result.items():
        aggregate['mean_ms'] = aggregate['inclusive_seconds'] / aggregate['calls'] * 1000.0 if aggregate['calls'] else 0.0
        aggregate['callers'] = callers.get(site, {})
    return result


def frame_end_gaps(frame_ends, threshold, clock=None):
    """Gaps bounded by consecutive frame_end lines of one device (a 300-frame cadence).

    dt_seconds between two lines contains up to 300 ordinary frames as well as
    any load stall, so the bounds are coarse: the stall lies inside
    [previous line, this line]. With a telemetry clock the bounds use the qpc
    field on the same axis as the report windows; otherwise seconds since
    DllMain (elapsed_ms). Same keys as gaps() so attribute() accepts them.
    """
    found = []
    previous = {}
    for item in frame_ends:
        last = previous.get(item['device'])
        if last is not None and item['dt_seconds'] >= threshold:
            if clock and item.get('qpc') is not None and last.get('qpc') is not None:
                start, end = seconds(last['qpc'], clock), seconds(item['qpc'], clock)
            else:
                start, end = last['elapsed_seconds'], item['elapsed_seconds']
            found.append(dict(
                gap_seconds=item['dt_seconds'], source='frame_end', device=item['device'],
                end_earliest_seconds=start, end_latest_seconds=end,
                start_earliest_seconds=start, start_latest_seconds=end,
                end_frame=item['frame'], window_frames=item['frame'] - last['frame'],
                window_present_count=item['frame'] - last['frame'], report_seconds=end,
                clock='telemetry' if clock and item.get('qpc') is not None else 'dll_load'))
        previous[item['device']] = item
    return found


def seconds(qpc, clock):
    return (qpc - clock['start_qpc']) / clock['frequency']


def totals(windows):
    """Whole-run loading totals, summed from the report deltas."""
    result = {}
    for window in windows:
        for operation, metric in window['metrics'].items():
            aggregate = result.setdefault(operation, dict(
                count=0, failures=0, pending=0, ambiguous=0, bytes=0,
                inclusive_seconds=0.0, exclusive_seconds=0.0, max_seconds=0.0,
                wrapper_tail_seconds=0.0))
            for key in VECTOR_KEYS:
                aggregate[key] += metric[key]
            for key in ('inclusive', 'exclusive', 'wrapper_tail'):
                aggregate[key + '_seconds'] += metric[key + '_seconds']
            aggregate['max_seconds'] = max(aggregate['max_seconds'], metric['max_seconds'])
    for aggregate in result.values():
        aggregate['mean_ms'] = (aggregate['inclusive_seconds'] / aggregate['count'] * 1000.0
                                if aggregate['count'] else 0.0)
    return result


def gaps(present, threshold):
    """Presentation gaps longer than the threshold, bounded by their report window.

    Summary reports are emitted on observed proxy activity, not by Present, so a
    report can arrive well after the Present that closed the gap. The gap's
    closing Present therefore lies between the previous device report and this
    one, less the other intervals this window already accumulated.
    """
    found = []
    for index, report in enumerate(present):
        stats = report['frame_normal']
        if not stats or stats['max_seconds'] < threshold:
            continue
        # The window's other intervals elapsed after the gap ended.
        trailing = max(stats['total_seconds'] - stats['max_seconds'], 0.0)
        previous = present[index - 1] if index else None
        end_latest = report['seconds'] - trailing
        end_earliest = min(previous['seconds'] if previous else end_latest, end_latest)
        found.append(dict(
            gap_seconds=stats['max_seconds'],
            end_earliest_seconds=end_earliest, end_latest_seconds=end_latest,
            start_earliest_seconds=end_earliest - stats['max_seconds'],
            start_latest_seconds=end_latest - stats['max_seconds'],
            end_frame=report['frame'],
            window_frames=report['frame'] - (previous['frame'] if previous else 0),
            window_present_count=stats['count'], report_seconds=report['seconds']))
    return found


def stalls(loading, threshold, detail=0):
    """Report-to-report intervals far longer than the nominal one-second cadence.

    Summary reports follow observed proxy/graphics activity, so a stall marks an
    interval the engine spent without crossing the proxy's device boundary. The
    instrumented loading calls completed inside it are still reported.
    """
    found = []
    for window in loading:
        interval = window['report_seconds'] - window['begin_seconds']
        if interval < threshold:
            continue
        item = dict(begin_seconds=window['begin_seconds'], end_seconds=window['report_seconds'],
                    interval_seconds=interval,
                    instrumented_exclusive_seconds=sum(
                        m['exclusive_seconds'] for m in window['metrics'].values()))
        if detail:
            ranked = sorted(window['metrics'].items(),
                            key=lambda item: -item[1]['inclusive_seconds'])[:detail]
            item['top_operations'] = [
                dict(op=op, count=m['count'], inclusive_seconds=m['inclusive_seconds'],
                     mean_us=m['inclusive_seconds'] / m['count'] * 1e6 if m['count'] else 0.0,
                     bytes=m['bytes']) for op, m in ranked]
        found.append(item)
    return found


def attribute(gap, loading):
    """Accounting table for one gap: overlapping report deltas and the remainder."""
    start = gap['start_earliest_seconds']
    end = gap['end_latest_seconds']
    inside, straddling = [], []
    for window in loading:
        if window['report_seconds'] <= start or window['begin_seconds'] >= end:
            continue
        (inside if window['begin_seconds'] >= start and window['report_seconds'] <= end
         else straddling).append(window)
    table = totals(inside + straddling)
    exclusive = sum(item['exclusive_seconds'] for item in table.values())
    # Duration-free fingerprint: equal fingerprints mean the same call/byte work.
    shape = json.dumps(sorted((op, item['count'], item['bytes'])
                              for op, item in table.items()), sort_keys=True)
    return dict(gap_seconds=gap['gap_seconds'],
                work_vector_sha256=hashlib.sha256(shape.encode()).hexdigest()[:16],
                start_seconds=start, end_seconds=end, end_frame=gap['end_frame'],
                windows=len(inside) + len(straddling), straddling_windows=len(straddling),
                operations=table, probes=probe_totals(inside + straddling),
                instrumented_exclusive_seconds=exclusive,
                unexplained_seconds=gap['gap_seconds'] - exclusive,
                report_stalls=stalls(sorted(inside + straddling,
                                            key=lambda w: w['begin_seconds']), 2.0, detail=4))


def signature(window):
    """Timestamp-free fingerprint of a report window's work vector."""
    items = sorted((op, tuple(metric[key] for key in VECTOR_KEYS))
                   for op, metric in window['metrics'].items())
    return json.dumps(items, sort_keys=True)


def substantial(block):
    """True when a block carries enough asset work to be worth reporting."""
    table = totals(block)
    return (table.get('ID3DXMesh::GenerateAdjacency', {}).get('count', 0) >= 50
            or table.get('D3DXCreateTextureFromFileInMemoryEx', {}).get('count', 0) >= 10)


def repeats(loading, length=5, minimum=2):
    """Consecutive report-window blocks whose work vectors repeat exactly."""
    signatures = [signature(window) for window in loading]
    blocks = {}
    for index in range(len(loading) - length + 1):
        key = '|'.join(signatures[index:index + length])
        if not substantial(loading[index:index + length]):
            continue
        blocks.setdefault(key, []).append(index)
    found = []
    for key, starts in blocks.items():
        if len(starts) < minimum:
            continue
        # Keep only non-overlapping occurrences.
        chosen, last = [], -length
        for start in starts:
            if start - last >= length:
                chosen.append(start)
                last = start
        if len(chosen) < minimum:
            continue
        occurrences = []
        for start in chosen:
            block = loading[start:start + length]
            table = totals(block)
            occurrences.append(dict(
                begin_seconds=block[0]['begin_seconds'],
                end_seconds=block[-1]['report_seconds'],
                adjacency_calls=table.get('ID3DXMesh::GenerateAdjacency', {}).get('count', 0),
                adjacency_seconds=table.get('ID3DXMesh::GenerateAdjacency', {}).get('inclusive_seconds', 0.0),
                texture_seconds=table.get('D3DXCreateTextureFromFileInMemoryEx', {}).get('inclusive_seconds', 0.0),
                open_calls=table.get('CreateFileA', {}).get('count', 0),
                read_bytes=table.get('ReadFile', {}).get('bytes', 0),
                inflate_calls=table.get('inflate', {}).get('count', 0)))
        found.append(dict(windows=length, occurrences=occurrences))
    found.sort(key=lambda item: item['occurrences'][0]['adjacency_seconds'], reverse=True)
    return found


def analyze(path, threshold=2.0, hash_source=True):
    lines, provenance = scan(path, hash_source)
    clock, anchors, present, loading, cursor, captures = parse(lines)
    found = gaps(present, threshold)
    return dict(
        source=provenance, clock=clock,
        coverage_begin_seconds=(seconds(anchors['coverage_begin_qpc'], clock)
                                if anchors['coverage_begin_qpc'] else None),
        hooks=anchors['hooks'], mesh_tables=anchors['mesh_tables'],
        startup_spans=anchors['spans'], first_presents=anchors['first_presents'],
        create_device=anchors['create_device'],
        last_report_seconds=loading[-1]['report_seconds'] if loading else 0.0,
        last_present_seconds=present[-1]['seconds'] if present else 0.0,
        last_frame=present[-1]['frame'] if present else 0,
        loading_totals=totals(loading), report_windows=len(loading),
        presentation_gaps=found,
        gap_accounting=[attribute(gap, loading) for gap in found],
        report_stalls=stalls(loading, 2.0),
        capture_frames=sorted({item['frame'] for item in captures}),
        first_cursor_seconds=cursor[0]['seconds'] if cursor else None,
        cursor_records=len(cursor),
        repeated_blocks=repeats(loading),
        limits=[
            'Loading spans are inclusive CPU wall time and may overlap other measurements.',
            'Only the exclusive column may be added, and that sum is a lower bound.',
            'Report windows carry completion deltas; a call may begin before its window.',
            'A report stall means no instrumented boundary was crossed, not that the CPU idled.',
            'Main-module IAT hooks miss DLL-internal I/O and pre-initialization startup.',
            'Identical report vectors show recurring work, never identical asset contents.',
            'Phase labels are inferred from anchors and frame counters, not from markers.'])


def trim(run, operations=8):
    """Compact, committable projection of one run's analysis."""
    def rank(table):
        return {op: dict(count=item['count'], failures=item['failures'],
                         bytes=item['bytes'], inclusive_seconds=item['inclusive_seconds'],
                         mean_ms=item['mean_ms'])
                for op, item in sorted(table.items(),
                                       key=lambda entry: -entry[1]['inclusive_seconds'])[:operations]}
    return dict(
        source=run['source'], clock=run['clock'],
        coverage_begin_seconds=run['coverage_begin_seconds'], hooks=run['hooks'],
        mesh_tables=run['mesh_tables'], create_device=run['create_device'],
        startup_spans=run['startup_spans'], first_presents=run['first_presents'],
        last_present_seconds=run['last_present_seconds'], last_frame=run['last_frame'],
        loading_totals=run['loading_totals'],
        gaps=[dict(gap_seconds=gap['gap_seconds'],
                   work_vector_sha256=gap['work_vector_sha256'],
                   start_seconds=gap['start_seconds'], end_seconds=gap['end_seconds'],
                   end_frame=gap['end_frame'], windows=gap['windows'],
                   instrumented_exclusive_seconds=gap['instrumented_exclusive_seconds'],
                   unexplained_seconds=gap['unexplained_seconds'],
                   operations=rank(gap['operations']), report_stalls=gap['report_stalls'])
              for gap in run['gap_accounting']],
        repeated_blocks=run['repeated_blocks'], limits=run['limits'])


def combine(runs):
    """Merge per-run analyses and group gaps whose work vectors are identical."""
    groups = {}
    for label, run in runs.items():
        for gap in run['gaps']:
            groups.setdefault(gap['work_vector_sha256'], []).append(
                dict(run=label, gap_seconds=gap['gap_seconds'],
                     start_seconds=gap['start_seconds'],
                     instrumented_exclusive_seconds=gap['instrumented_exclusive_seconds'],
                     unexplained_seconds=gap['unexplained_seconds']))
    return dict(runs=runs,
                identical_gap_work_vectors={key: value for key, value in groups.items()
                                            if len(value) > 1},
                limits=['Equal work vectors mean equal instrumented call and byte counts, '
                        'never proven identical asset contents.',
                        'Durations vary between runs with host I/O state; counts do not.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path, nargs='+',
                        help='log file, or previously produced JSON with --combine')
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--threshold', type=float, default=2.0,
                        help='minimum presentation gap in seconds')
    parser.add_argument('--combine', action='store_true',
                        help='merge label=path.json analyses into one compact summary')
    parser.add_argument('--no-hash', action='store_true')
    arguments = parser.parse_args()
    if arguments.combine:
        runs = {}
        for entry in arguments.trace:
            label, _, path = str(entry).partition('=')
            runs[label] = trim(json.loads(Path(path).read_text()))
        result = combine(runs)
        arguments.output.write_text(json.dumps(result, indent=1, sort_keys=True) + '\n')
        print(f"combined {len(runs)} runs, "
              f"{len(result['identical_gap_work_vectors'])} repeated work vectors")
        return
    for trace in arguments.trace:
        result = analyze(trace, arguments.threshold, not arguments.no_hash)
        arguments.output.write_text(json.dumps(result, indent=1, sort_keys=True) + '\n')
        print(f"{trace.name}: {result['source']['bytes']} bytes, "
              f"{len(result['presentation_gaps'])} gaps over {arguments.threshold}s, "
              f"{result['report_windows']} report windows")


if __name__ == '__main__':
    main()
