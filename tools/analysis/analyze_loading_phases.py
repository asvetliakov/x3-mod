#!/usr/bin/env python3
"""Analyze a newline-complete, stable prefix of an active diagnostics 0.3 log.

Windows below group deltas by report time, not by individual call start/end.
No unlabeled interval is guessed to be a menu, New Game or gameplay. Inclusive
API spans are never added into an alleged loading-time attribution percentage.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
from summarize_capture import fields
from summarize_telemetry import summarize


def read_prefix(path):
    """Read only the initial file length, dropping an unfinished trailing line."""
    with path.open('rb') as stream:
        before = os.fstat(stream.fileno())
        raw = stream.read(before.st_size)
        after = os.fstat(stream.fileno())
    if after.st_size < before.st_size or len(raw) != before.st_size:
        raise ValueError('source shrank during snapshot')
    length = raw.rfind(b'\n') + 1
    raw = raw[:length]
    return raw, dict(name=path.name, bytes=length, size_before=before.st_size,
                     size_after=after.st_size, dropped_tail_bytes=before.st_size-length,
                     sha256=hashlib.sha256(raw).hexdigest())


LOADING_PHASE_NAMES = ('menu_shown', 'save_load_begin', 'save_load_complete')


def extract_loading_phases(text):
    """Collect the proxy's cadence-derived `loading_phase` lines (every mode).

    Each name is kept once (first occurrence). Deltas are in ms from the
    line's own elapsed_ms field (origin DllMain, like frame_end): menu_ms is
    process start to menu shown, menu_to_save_ms the menu dwell, save_load_ms
    the save-load stall; a delta is None while either end is missing.
    """
    markers, rejected = {}, []
    for line_number, line in enumerate(text.splitlines(), 1):
        if not line.startswith('loading_phase '):
            continue
        values = fields(line)
        try:
            name = values['name']
            if name not in LOADING_PHASE_NAMES:
                raise ValueError('unknown loading_phase name ' + name)
            marker = dict(name=name, frame=int(values['frame']), elapsed_ms=int(values['elapsed_ms']),
                          stall_ms=int(values['stall_ms']) if 'stall_ms' in values else None,
                          line=line_number)
            if marker['elapsed_ms'] < 0 or marker['frame'] < 0 or (marker['stall_ms'] is not None and marker['stall_ms'] < 0):
                raise ValueError('negative loading_phase field')
        except (KeyError, ValueError) as error:
            rejected.append(dict(line=line_number, reason=str(error)))
            continue
        if name in markers:
            rejected.append(dict(line=line_number, reason='duplicate loading_phase ' + name))
            continue
        markers[name] = marker

    def delta(begin, end):
        if begin is None or end is None:
            return None
        return markers[end]['elapsed_ms'] - markers[begin]['elapsed_ms']
    present = {name: (name if name in markers else None) for name in LOADING_PHASE_NAMES}
    return dict(markers=[markers[n] for n in LOADING_PHASE_NAMES if n in markers],
                menu_ms=markers['menu_shown']['elapsed_ms'] if 'menu_shown' in markers else None,
                menu_to_save_ms=delta(present['menu_shown'], present['save_load_begin']),
                save_load_ms=delta(present['save_load_begin'], present['save_load_complete']),
                rejected=rejected)


def analyze(text, completed=False):
    combined = summarize(text)
    clock = combined['clock']
    if not clock:
        raise ValueError('missing telemetry clock')
    frequency, origin = clock['frequency'], clock['start_qpc']
    coverage = next((int(e['coverage_begin']) for e in combined['loading_events']
                     if 'coverage_begin' in e), origin)
    windows, device_reports, rejected = [], {}, []
    current = None
    last_loading_report = coverage
    last_qpc = origin
    for line_number, line in enumerate(text.splitlines(), 1):
        event = line.partition(' ')[0]
        if event not in ('telemetry_summary', 'loading_metric'):
            continue
        values = fields(line)
        try:
            stamp = int(values['qpc'])
            last_qpc = max(last_qpc, stamp)
            if event == 'telemetry_summary':
                device = int(values['device'])
                if device:
                    device_reports.setdefault(device, []).append(dict(qpc=stamp, frame=int(values['frame'])))
                    current = None
                else:
                    current = dict(begin_qpc=last_loading_report, report_qpc=stamp,
                                   begin_seconds=(last_loading_report-origin)/frequency,
                                   report_seconds=(stamp-origin)/frequency, metrics={})
                    windows.append(current)
                    last_loading_report = stamp
            else:
                if current is None:
                    raise ValueError('loading delta lacks a preceding process summary')
                keys=('count','failures','pending','ambiguous','bytes','inclusive_ticks',
                      'exclusive_ticks','max_ticks','wrapper_tail_ticks')
                metric = {key:int(values[key]) for key in keys}
                if min(metric.values()) < 0:
                    raise ValueError('negative loading delta')
                for key in ('inclusive','exclusive','max','wrapper_tail'):
                    metric[key+'_seconds'] = metric[key+'_ticks']/frequency
                current['metrics'][values['op']] = metric
                current['report_qpc'] = max(current['report_qpc'],stamp)
                current['report_seconds'] = (current['report_qpc']-origin)/frequency
                last_loading_report = current['report_qpc']
        except (KeyError,ValueError) as error:
            rejected.append(dict(line=line_number,reason=str(error)))
    for window in windows:
        window['report_interval_seconds'] = (window['report_qpc']-window['begin_qpc'])/frequency
        window['largest_metric'] = max(window['metrics'],key=lambda op:window['metrics'][op]['inclusive_ticks'],default=None)
    unchanged = []
    for device, reports in device_reports.items():
        run = None
        for report in reports:
            if run is None or run['frame'] != report['frame']:
                if run and run['reports'] > 1:
                    unchanged.append(run)
                run=dict(device=device,frame=report['frame'],begin_qpc=report['qpc'],end_qpc=report['qpc'],reports=1)
            else:
                run['end_qpc']=report['qpc'];run['reports']+=1
        if run and run['reports']>1:
            unchanged.append(run)
    for run in unchanged:
        run['begin_seconds']=(run['begin_qpc']-origin)/frequency
        run['end_seconds']=(run['end_qpc']-origin)/frequency
        run['observed_duration_seconds']=(run['end_qpc']-run['begin_qpc'])/frequency
    return dict(completed_as_declared=completed, clock=clock,
                last_report_seconds=(last_qpc-origin)/frequency,
                coverage_begin_seconds=(coverage-origin)/frequency,
                loading_totals=combined['loading_metrics'],graphics_totals=combined['metrics'],
                startup_spans=combined['spans'],first_presents=combined['first_presents'],
                markers=combined['markers'],loading_phases=extract_loading_phases(text),windows=windows,
                unchanged_reported_frame_runs=sorted(unchanged,key=lambda x:x['observed_duration_seconds'],reverse=True),
                rejected=combined['rejected']+rejected,
                limits=combined['limits']+[
                    'Windows contain report-time deltas; calls may start before a window or remain in flight.',
                    'An unchanged reported frame is evidence only between its observed summary timestamps.',
                    'No phase labels are inferred without user/marker evidence.',
                    'No process CPU statistics exist in this log; low traced time is not proof of CPU-bound engine work.',
                    'A completed process can retain unflushed tail counters; completed_as_declared is external metadata.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace',type=Path)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--snapshot',type=Path,help='Optional local prefix copy; keep raw captures untracked')
    parser.add_argument('--completed',action='store_true',help='Caller confirms the test/trace has completed')
    args=parser.parse_args()
    raw,source=read_prefix(args.trace)
    report=analyze(raw.decode('utf-8'),args.completed);report['source']=source
    if args.snapshot:args.snapshot.write_bytes(raw)
    args.output.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    print(f"{source['bytes']} bytes; {report['last_report_seconds']:.3f}s observed; "
          f"{len(report['windows'])} process windows; {len(report['rejected'])} rejected")


if __name__=='__main__':main()
