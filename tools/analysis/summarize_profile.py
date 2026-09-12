#!/usr/bin/env python3
"""Summarize sampling-profiler reports (X3M_PROFILE=1) from a proxy session log.

The log is streamed line by line; only the sparse `profile_*` lines and the
`telemetry_start` anchor are retained, never the per-draw capture payload.
Time windows are seconds after proxy initialization on the same QPC clock as
`loading_metric`, so a window taken from analyze_iteration08_loading.py's gap
bounds selects the profiler reports that overlap that gap.

Definitions and limits:

* Only `scope=delta` blocks are summed. Each block covers the interval from the
  previous report to its own `qpc`, and is attributed to a window by overlap
  (`elapsed_us` gives its length), so blocks straddling a boundary are counted
  in both neighbours.
* Per-block tables are truncated (top 48 leaves/frames, top 32 pairs), so the
  summed table counts are lower bounds; the per-thread module split and the
  sample totals are exact.
* `rva` values are exact instruction addresses relative to the module base.
  With `--symbols` (output of tools/analysis/X3ProfileSymbols.java) main-module
  entries are grouped by containing function as well.
* The sampler's own cost (`tick_us_mean/max`, `refresh_us`, `report_us`) is
  reported alongside, and is wall time of the sampler thread, not game time.
"""
import argparse
import json
from collections import defaultdict
from pathlib import Path

KEEP = ('profile_', 'telemetry_start ', 'frame_end ')
LEAF_KINDS = ('leaf_x3ap', 'leaf_ntdll', 'leaf_wine', 'leaf_d3dx', 'leaf_zlib', 'leaf_xml', 'leaf_proxy', 'leaf_other')


def fields(line):
    out = {}
    for token in line.split()[1:]:
        key, _, value = token.partition('=')
        out[key] = value
    return out


def scan(path):
    """Stream the log, keeping only profiler lines (bounded: ~200 lines per report)."""
    kept = []
    with Path(path).open('rb') as stream:
        for raw in stream:
            if raw.startswith(b'profile_') or raw.startswith(b'telemetry_start ') or raw.startswith(b'frame_end '):
                kept.append(raw.decode('utf-8', 'replace').rstrip('\r\n'))
    return kept


def parse(lines):
    anchor, frequency = None, None
    start, modules, blocks, current = None, {}, [], None
    frame_ends = []
    for line in lines:
        head, _, _ = line.partition(' ')
        f = fields(line)
        if head == 'frame_end':
            if 'elapsed_ms' in f:
                frame_ends.append(dict(device=int(f['device']), frame=int(f['frame']), capture=f.get('capture') == '1',
                                       elapsed_seconds=int(f['elapsed_ms']) / 1000.0, dt_seconds=int(f.get('dt_ms', 0)) / 1000.0,
                                       qpc=int(f['qpc']) if 'qpc' in f else None))
            continue
        if head == 'telemetry_start':
            anchor, frequency = int(f['qpc']), int(f['qpc_frequency'])
        elif head == 'profile_start':
            start = f
            if anchor is None:
                anchor, frequency = int(f['qpc']), int(f['frequency'])
        elif head == 'profile_module':
            if 'unloaded' in f:
                modules.setdefault(int(f['index']), {})['unloaded'] = True
            elif 'size' not in f:
                # A module that could not be pinned (already unloading) is logged with
                # `pinned=0 error=<n>` and no kind/size/text range; it is never sampled.
                modules[int(f['index'])] = dict(name=f.get('name'), kind=f.get('kind'),
                                                base=int(f['base'], 16) if 'base' in f else None,
                                                size=None, text_rva=None, text_size=None,
                                                pinned=False, error=f.get('error'))
            else:
                modules[int(f['index'])] = dict(name=f['name'], kind=f.get('kind'), base=int(f['base'], 16), size=int(f['size'], 16),
                                                text_rva=int(f['text_rva'], 16), text_size=int(f['text_size'], 16),
                                                pinned=f.get('pinned') != '0')
        elif head == 'profile_report':
            current = dict(scope=f['scope'], qpc=int(f['qpc']), fields=f, threads=[], leaves=[], frames=[], pairs=[], report_us=None)
            blocks.append(current)
        elif head == 'profile_report_end' and current is not None:
            current['report_us'] = float(f['report_us'])
        elif current is None:
            continue
        elif head == 'profile_thread':
            current['threads'].append(f)
        elif head == 'profile_leaf':
            current['leaves'].append(f)
        elif head == 'profile_frame':
            current['frames'].append(f)
        elif head == 'profile_pair':
            current['pairs'].append(f)
    return dict(anchor=anchor, frequency=frequency, start=start, modules=modules, blocks=blocks, frame_ends=frame_ends)


def seconds(qpc, parsed):
    return (qpc - parsed['anchor']) / parsed['frequency'] if parsed['anchor'] is not None and parsed['frequency'] else None


def frame_end_gaps(parsed, threshold=2.0):
    """Load gaps bounded by consecutive frame_end lines of one device.

    frame_end is written in every mode (every 300 frames or a capture frame)
    with elapsed_ms since DllMain, dt_ms since the previous line and the QPC
    stamp. A dt_ms over the threshold marks a gap inside [previous, this]; the
    interval also holds up to 300 ordinary frames. With a profile/telemetry
    anchor the bounds are on that clock (usable as --window values); without
    one they are seconds since DllMain.
    """
    found = []
    previous = {}
    for item in parsed.get('frame_ends', []):
        last = previous.get(item['device'])
        if last is not None and item['dt_seconds'] >= threshold:
            on_anchor = seconds(item['qpc'], parsed) is not None if item.get('qpc') is not None and last.get('qpc') is not None else False
            start = seconds(last['qpc'], parsed) if on_anchor else last['elapsed_seconds']
            end = seconds(item['qpc'], parsed) if on_anchor else item['elapsed_seconds']
            found.append(dict(start_s=start, end_s=end, dt_seconds=item['dt_seconds'], frames=item['frame'] - last['frame'],
                              end_frame=item['frame'], device=item['device'], clock='anchor' if on_anchor else 'dll_load'))
        previous[item['device']] = item
    return found


def overlaps(block, parsed, from_s, to_s):
    end = seconds(block['qpc'], parsed)
    if end is None:
        return True
    begin = end - float(block['fields'].get('elapsed_us', 0)) / 1e6
    return (from_s is None or end >= from_s) and (to_s is None or begin <= to_s)


def load_symbols(path):
    if not path:
        return {}
    data = json.loads(Path(path).read_text())
    table = data.get('symbols', data) if isinstance(data, dict) else {}
    return {int(k, 16) if isinstance(k, str) else int(k): v for k, v in table.items()}


def symbol_of(rva, symbols):
    entry = symbols.get(rva)
    if not entry:
        return None
    return entry.get('function_name') or hex(int(entry.get('function_start_rva', rva)))


def summarize_window(parsed, label, from_s, to_s, top, symbols):
    delta = [b for b in parsed['blocks'] if b['scope'] == 'delta' and overlaps(b, parsed, from_s, to_s)]
    samples = ticks = dropped = 0
    tick_total_us = refresh_us = report_us = 0.0
    tick_max_us = 0.0
    threads = defaultdict(lambda: dict(samples=0, **{k: 0 for k in LEAF_KINDS}))
    leaves, frames, pairs = defaultdict(int), defaultdict(int), defaultdict(int)
    first_qpc, last_qpc = None, None
    for b in delta:
        f = b['fields']
        n_ticks = int(f.get('ticks', 0))
        samples += int(f.get('samples', 0))
        ticks += n_ticks
        dropped += int(f.get('dropped', 0))
        tick_total_us += float(f.get('tick_us_mean', 0)) * n_ticks
        tick_max_us = max(tick_max_us, float(f.get('tick_us_max', 0)))
        refresh_us += float(f.get('refresh_us', 0))
        report_us += b['report_us'] or 0.0
        first_qpc = b['qpc'] if first_qpc is None else min(first_qpc, b['qpc'])
        last_qpc = b['qpc'] if last_qpc is None else max(last_qpc, b['qpc'])
        for t in b['threads']:
            row = threads[(int(t['slot']), int(t['tid']))]
            row['samples'] += int(t['samples'])
            for k in LEAF_KINDS:
                row[k] += int(t.get(k, 0))
            row['start_rva'] = t.get('start_rva')
            row['start_module'] = t.get('start_module')
        for row in b['leaves']:
            leaves[(int(row['slot']), row.get('name', '?'), int(row['rva'], 16))] += int(row['count'])
        for row in b['frames']:
            frames[(int(row['slot']), int(row['rva'], 16))] += int(row['count'])
        for row in b['pairs']:
            pairs[(int(row['slot']), int(row['rva'], 16), int(row['caller'], 16))] += int(row['count'])
    functions = defaultdict(int)
    for (slot, rva), count in frames.items():
        name = symbol_of(rva, symbols)
        if name:
            functions[(slot, name)] += count
    def ranked(table, n):
        return sorted(table.items(), key=lambda item: (-item[1], item[0]))[:n]
    module_split = {k: sum(row[k] for row in threads.values()) for k in LEAF_KINDS}
    return dict(
        label=label, from_s=from_s, to_s=to_s, blocks=len(delta),
        first_report_s=seconds(first_qpc, parsed) if first_qpc is not None else None,
        last_report_s=seconds(last_qpc, parsed) if last_qpc is not None else None,
        samples=samples, ticks=ticks, dropped=dropped,
        sampler_cost=dict(tick_us_mean=(tick_total_us / ticks) if ticks else 0.0, tick_us_max=tick_max_us,
                          refresh_us_total=refresh_us, report_us_total=report_us,
                          sampler_busy_s=(tick_total_us + refresh_us + report_us) / 1e6),
        module_split=module_split,
        threads=[dict(slot=slot, tid=tid, **row) for (slot, tid), row in sorted(threads.items())],
        top_leaves=[dict(slot=s, module=m, rva=hex(r), count=c, function=symbol_of(r, symbols) if m and m.lower().startswith('x3ap') else None)
                    for (s, m, r), c in ranked(leaves, top)],
        top_frames=[dict(slot=s, rva=hex(r), count=c, function=symbol_of(r, symbols)) for (s, r), c in ranked(frames, top)],
        top_pairs=[dict(slot=s, rva=hex(r), caller=hex(k), count=c, function=symbol_of(r, symbols), caller_function=symbol_of(k, symbols))
                   for (s, r, k), c in ranked(pairs, top)],
        top_functions=[dict(slot=s, function=name, count=c) for (s, name), c in ranked(functions, top)] if symbols else [],
        limits=['Per-block tables are truncated to the top 48/48/32 rows, so summed table counts are lower bounds.',
                'Blocks are attributed to a window by overlap of their report interval; boundary blocks count in both neighbours.',
                'RVAs are instruction addresses relative to the module base; frame RVAs are return addresses (the instruction after the call).'],
    )


def main_module_rvas(parsed):
    """Every main-module RVA the tables mention, for the Ghidra symbol script."""
    rvas = set()
    main_names = {m['name'].lower() for m in parsed['modules'].values() if m.get('kind') == 'x3ap'}
    for b in parsed['blocks']:
        for row in b['frames']:
            rvas.add(int(row['rva'], 16))
        for row in b['pairs']:
            rvas.add(int(row['rva'], 16))
            rvas.add(int(row['caller'], 16))
        for row in b['leaves']:
            if row.get('name', '').lower() in main_names:
                rvas.add(int(row['rva'], 16))
    rvas.discard(0)
    return sorted(rvas)


def summarize(path, windows, top=48, symbols=None, frame_gaps=False, threshold=2.0):
    parsed = parse(scan(path))
    gaps = frame_end_gaps(parsed, threshold)
    if frame_gaps:
        windows = list(windows) + [(f'frame_gap_{i}', g['start_s'], g['end_s']) for i, g in enumerate(gaps, 1) if g['clock'] == 'anchor']
    result = dict(
        source=str(path), anchor_qpc=parsed['anchor'], qpc_frequency=parsed['frequency'],
        profile_start=parsed['start'], modules={str(k): v for k, v in sorted(parsed['modules'].items())},
        blocks=dict(delta=sum(b['scope'] == 'delta' for b in parsed['blocks']), cumulative=sum(b['scope'] == 'cumulative' for b in parsed['blocks'])),
        windows=[summarize_window(parsed, label, lo, hi, top, symbols or {}) for label, lo, hi in windows],
        main_module_rvas=[hex(r) for r in main_module_rvas(parsed)],
        frame_end_lines=len(parsed['frame_ends']), frame_end_gaps=gaps,
    )
    return result


def parse_window(text):
    label, _, span = text.rpartition('=') if '=' in text else ('', '', text)
    lo, _, hi = span.partition(':')
    return (label or span, float(lo) if lo else None, float(hi) if hi else None)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('log', type=Path)
    parser.add_argument('--from-s', type=float, help='window start, seconds after proxy initialization')
    parser.add_argument('--to-s', type=float, help='window end, seconds after proxy initialization')
    parser.add_argument('--window', action='append', default=[], metavar='LABEL=FROM:TO', help='additional labelled window (repeatable)')
    parser.add_argument('--top', type=int, default=48)
    parser.add_argument('--symbols', type=Path, help='JSON from X3ProfileSymbols.java to name main-module RVAs')
    parser.add_argument('--output', type=Path, help='write the JSON summary here')
    parser.add_argument('--rva-list', type=Path, help='write every main-module RVA (one hex per line) for the Ghidra script')
    parser.add_argument('--frame-gaps', action='store_true', help='add one window per frame_end-derived gap (dt_ms over --gap-threshold) on the profile clock')
    parser.add_argument('--gap-threshold', type=float, default=2.0, help='seconds of dt_ms between frame_end lines that count as a gap')
    args = parser.parse_args()
    windows = [('whole' if args.from_s is None and args.to_s is None else 'window', args.from_s, args.to_s)]
    windows += [parse_window(w) for w in args.window]
    result = summarize(args.log, windows, args.top, load_symbols(args.symbols), args.frame_gaps, args.gap_threshold)
    for g in result['frame_end_gaps']:
        print(f"frame_end gap: {g['start_s']:.3f}-{g['end_s']:.3f} s ({g['clock']} clock) dt={g['dt_seconds']:.3f} s frames={g['frames']} end_frame={g['end_frame']}")
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    if args.rva_list:
        args.rva_list.write_text('\n'.join(result['main_module_rvas']) + '\n')
    for w in result['windows']:
        cost = w['sampler_cost']
        print(f"window {w['label']} [{w['from_s']}, {w['to_s']}] blocks={w['blocks']} samples={w['samples']} ticks={w['ticks']} "
              f"tick_us_mean={cost['tick_us_mean']:.1f} tick_us_max={cost['tick_us_max']:.1f} dropped={w['dropped']}")
        print('  module split: ' + ' '.join(f'{k[5:]}={v}' for k, v in w['module_split'].items()))
        for row in w['top_frames'][:12]:
            print(f"  frame slot={row['slot']} rva={row['rva']} count={row['count']}" + (f" {row['function']}" if row['function'] else ''))
        for row in w['top_pairs'][:8]:
            print(f"  pair slot={row['slot']} rva={row['rva']} caller={row['caller']} count={row['count']}")


if __name__ == '__main__':
    main()
