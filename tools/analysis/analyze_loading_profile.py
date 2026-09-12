#!/usr/bin/env python3
"""Turn one `--telemetry --profile` session log into a loading attribution report.

For every presentation gap longer than the threshold (found exactly as
analyze_iteration08_loading.py finds them) and for every report stall inside it,
the tool sets two views of the same QPC interval side by side:

* **hooked time** — the instrumented `loading_metric` deltas whose report
  window overlaps the interval (inclusive/exclusive seconds per operation), and
* **sampled attribution** — the sampling-profiler delta blocks whose report
  interval overlaps it: samples per thread with the leaf-module split, the top
  leaf RVAs, the top main-executable frames, the top caller pairs, and — the
  primary ranking — leaf samples aggregated by *containing function* once the
  RVAs are symbolized (Ghidra `X3ProfileSymbols.java`, run here with `--ghidra`
  or supplied as `--symbols`). Function starts are then matched against the
  hand-maintained role table `x3ap_function_labels.json`.

Gap labels are mechanical heuristics over the hooked counts inside the gap
(see `label_gap`); they are evidence-tagged, never inferred from timing alone.
The log is streamed once; only sparse telemetry, loading and `profile_*` lines
are retained. Outputs: `<dir>/loading-profile.json` (compact) and
`<dir>/loading-profile.md`.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import time
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import analyze_iteration08_loading as loading  # noqa: E402
import summarize_profile as profile  # noqa: E402

LABELS_PATH = Path(__file__).with_name('x3ap_function_labels.json')
GHIDRA_HEADLESS = '/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless'
JAVA_HOME = '/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home'
GHIDRA_PROJECT = '/tmp/x3-ghidra-research'
GHIDRA_PROJECT_NAME = 'X3Render'
GHIDRA_PROGRAM = 'X3AP.exe'
DEFAULT_MAIN_BASE = 0x00400000
TOP = 40

# Gap-label thresholds. A menu load repeats ~1,017 GenerateAdjacency calls with
# ~299 MB of 2D texture-helper input; a save load is the only phase with a gzip
# save stream; sector changes are adjacency-dominated with little texture input
# (docs/reverse-engineering/iteration08-loading.md).
MENU_ADJACENCY = 500
MENU_TEXTURE_BYTES = 200_000_000
SECTOR_ADJACENCY = 100
ADJACENCY = 'ID3DXMesh::GenerateAdjacency'
TEXTURE = 'D3DXCreateTextureFromFileInMemoryEx'
MODULE_KINDS = tuple(kind[5:] for kind in profile.LEAF_KINDS)

# Instrumentation envelope components measured by the gz-buffer fixture on this
# bottle (verification/results/bottle-X3/gz-buffer-summary.json,
# docs/verification/loading-probes.md): the light span costs 351 ns per call, of
# which 207.5 ns are its three QueryPerformanceCounter reads (69.2 ns each). A
# timed probe reads the clock twice, a count-only probe not at all, so 351 ns and
# the 143.6 ns non-clock remainder are upper bounds for the two probe kinds.
LIGHT_ENVELOPE_NS = 351.0
QPC_READ_NS = 69.2
TIMED_PROBE_NS = LIGHT_ENVELOPE_NS - QPC_READ_NS
COUNT_PROBE_NS = LIGHT_ENVELOPE_NS - 3 * QPC_READ_NS


def scan(path, hash_source=True):
    """One streaming pass: loading/telemetry lines and profiler lines, kept apart."""
    digest = hashlib.sha256()
    loading_lines, profile_lines, total = [], [], 0
    with Path(path).open('rb') as stream:
        for raw in stream:
            total += len(raw)
            if hash_source:
                digest.update(raw)
            if not raw.endswith(b'\n'):
                continue
            head = raw.split(b' ', 1)[0]
            if head.startswith(b'profile_'):
                profile_lines.append(raw.decode('utf-8', 'replace').rstrip('\r\n'))
                continue
            name = head.decode('ascii', 'replace')
            if name in loading.KEEP:
                text = raw.decode('utf-8', 'replace').rstrip('\r\n')
                loading_lines.append(text)
                if name == 'telemetry_start':
                    profile_lines.append(text)
    return loading_lines, profile_lines, dict(
        name=Path(path).name, bytes=total, sha256=digest.hexdigest() if hash_source else None)


def count(operations, name, key='count'):
    return operations.get(name, {}).get(key, 0)


def label_gap(operations, previous_labels):
    """Mechanical phase label from the hooked counts inside the gap."""
    gzread = count(operations, 'gzread')
    gzopen_ok = count(operations, 'gzopen') - count(operations, 'gzopen', 'failures')
    adjacency = count(operations, ADJACENCY)
    texture_bytes = count(operations, TEXTURE, 'bytes')
    after_phase = any(label != 'unlabelled' for label in previous_labels)
    if gzread > 0 or gzopen_ok > 0:
        return 'save load', (f'{gzread} gzread calls and {gzopen_ok} successful gzopen inside the gap '
                             '(gzip save stream)')
    if adjacency >= MENU_ADJACENCY and texture_bytes >= MENU_TEXTURE_BYTES:
        return 'menu load', (f'{adjacency} GenerateAdjacency calls and {texture_bytes:,} B of 2D '
                             'texture-helper input match the main-menu work vector')
    if adjacency >= SECTOR_ADJACENCY and after_phase:
        return 'sector change', (f'{adjacency} GenerateAdjacency calls with {texture_bytes:,} B of '
                                 'texture input after an earlier labelled phase')
    return 'unlabelled', (f'{adjacency} GenerateAdjacency calls, {texture_bytes:,} B texture input, '
                          f'{gzread} gzread calls: no rule matches')


def instrumented(windows, start, end):
    """Hooked deltas whose report window overlaps [start, end] (same rule as `attribute`)."""
    inside, straddling = [], []
    for window in windows:
        if window['report_seconds'] <= start or window['begin_seconds'] >= end:
            continue
        (inside if window['begin_seconds'] >= start and window['report_seconds'] <= end
         else straddling).append(window)
    table = loading.totals(inside + straddling)
    return dict(windows=len(inside) + len(straddling), straddling_windows=len(straddling),
                operations=table, probes=loading.probe_totals(inside + straddling),
                exclusive_seconds=sum(item['exclusive_seconds'] for item in table.values()))


def load_labels(path=LABELS_PATH):
    if not path or not Path(path).exists():
        return {}
    data = json.loads(Path(path).read_text())
    return {int(key, 16): value for key, value in data.get('labels', {}).items()}


def main_base_of(parsed):
    start = parsed.get('start') or {}
    if start.get('main_base'):
        return int(start['main_base'], 16)
    for module in parsed['modules'].values():
        if module.get('kind') == 'x3ap':
            return module['base']
    return DEFAULT_MAIN_BASE


def main_module_names(parsed):
    names = {m['name'].lower() for m in parsed['modules'].values() if m.get('kind') == 'x3ap' and m.get('name')}
    return names or {'x3ap.exe'}


def block_bounds(block, parsed):
    end = profile.seconds(block['qpc'], parsed)
    return end - float(block['fields'].get('elapsed_us', 0)) / 1e6, end


def sampled(parsed, start, end):
    """Raw sampled tables for one interval (no symbols yet), or None without coverage."""
    window = profile.summarize_window(parsed, 'interval', start, end, None, {})
    if window['blocks'] == 0:
        return None
    inside = straddling = 0
    elapsed = 0.0
    for block in parsed['blocks']:
        if block['scope'] != 'delta' or not profile.overlaps(block, parsed, start, end):
            continue
        begin, finish = block_bounds(block, parsed)
        elapsed += finish - begin
        if begin >= start and finish <= end:
            inside += 1
        else:
            straddling += 1
    cost = window['sampler_cost']
    return dict(blocks=window['blocks'], inside_blocks=inside, straddling_blocks=straddling,
                first_report_s=window['first_report_s'], last_report_s=window['last_report_s'],
                covered_seconds=elapsed, samples=window['samples'], ticks=window['ticks'],
                dropped=window['dropped'],
                samples_per_second=window['samples'] / elapsed if elapsed else 0.0,
                ticks_per_second=window['ticks'] / elapsed if elapsed else 0.0,
                sampler_cost=dict(cost, busy_share=cost['sampler_busy_s'] / elapsed if elapsed else 0.0),
                module_split=window['module_split'], threads=window['threads'],
                leaves=window['top_leaves'], frames=window['top_frames'], pairs=window['top_pairs'])


def collect_rvas(intervals, main_names):
    rvas = set()
    for item in intervals:
        table = item.get('sampled')
        if not table:
            continue
        for row in table['leaves']:
            if row['module'].lower() in main_names:
                rvas.add(int(row['rva'], 16))
        for row in table['frames']:
            rvas.add(int(row['rva'], 16))
        for row in table['pairs']:
            rvas.add(int(row['rva'], 16))
            rvas.add(int(row['caller'], 16))
    rvas.discard(0)
    return sorted(rvas)


def resolve(rva, symbols):
    """(function start RVA, Ghidra name, resolved?) for an instruction RVA."""
    entry = symbols.get(rva)
    if entry and entry.get('function_start_rva') is not None:
        return int(entry['function_start_rva'], 16), entry.get('function_name'), True
    return rva, None, False


def aggregate_functions(table, symbols, labels, main_base, main_names, operations, top):
    """Leaf and frame samples grouped by containing function; the primary ranking."""
    functions = {}
    thread_samples = {row['slot']: row['samples'] for row in table['threads']}
    total = table['samples'] or 1

    def entry(rva):
        start, name, resolved = resolve(rva, symbols)
        item = functions.get(start)
        if item is None:
            va = main_base + start
            label = labels.get(va, {})
            item = functions[start] = dict(
                start_rva=hex(start), start_va=f'0x{va:08x}', name=name, resolved=resolved,
                label=label.get('label'), hooked_apis=label.get('hooked_apis', []),
                self_samples=0, inclusive_samples=0, self_by_slot=defaultdict(int),
                inclusive_by_slot=defaultdict(int))
        return item

    for row in table['leaves']:
        if row['module'].lower() in main_names:
            item = entry(int(row['rva'], 16))
            item['self_samples'] += row['count']
            item['self_by_slot'][row['slot']] += row['count']
    no_frame = 0
    for row in table['frames']:
        rva = int(row['rva'], 16)
        if rva == 0:
            no_frame += row['count']
            continue
        item = entry(rva)
        item['inclusive_samples'] += row['count']
        item['inclusive_by_slot'][row['slot']] += row['count']
    ranked = sorted(functions.values(),
                    key=lambda f: (-f['self_samples'], -f['inclusive_samples'], f['start_rva']))
    # The engine thread: the slot holding the most main-module samples. Every other
    # sampled thread is an idle Wine/audio/input waiter whose samples would otherwise
    # dilute every share by the thread count, so shares are also given against it.
    by_slot = defaultdict(int)
    for item in functions.values():
        for slot, n in item['self_by_slot'].items():
            by_slot[slot] += n
        for slot, n in item['inclusive_by_slot'].items():
            by_slot[slot] += n
    engine_slot = max(by_slot.items(), key=lambda kv: kv[1])[0] if by_slot else None
    engine_total = thread_samples.get(engine_slot, 0) or 1
    rows = []
    for item in ranked[:top]:
        busiest = max(item['self_by_slot'].items(), key=lambda kv: kv[1], default=(None, 0))
        slot_total = thread_samples.get(busiest[0], 0) if busiest[0] is not None else 0
        rows.append(dict(
            start_rva=item['start_rva'], start_va=item['start_va'], name=item['name'],
            resolved=item['resolved'], label=item['label'],
            self_samples=item['self_samples'], self_share=item['self_samples'] / total,
            self_share_engine=item['self_samples'] / engine_total,
            inclusive_samples=item['inclusive_samples'],
            inclusive_share=item['inclusive_samples'] / total,
            inclusive_share_engine=item['inclusive_samples'] / engine_total,
            busiest_slot=busiest[0], busiest_slot_share=busiest[1] / slot_total if slot_total else 0.0,
            hooked=[dict(op=api, count=count(operations, api),
                         inclusive_seconds=operations.get(api, {}).get('inclusive_seconds', 0.0))
                    for api in item['hooked_apis'] if api in operations]))
    return rows, no_frame, engine_slot, thread_samples.get(engine_slot, 0)


def symbolize_pairs(pairs, symbols, labels, main_base, top):
    rows = []
    for row in pairs[:top]:
        start, name, _ = resolve(int(row['rva'], 16), symbols)
        caller_start, caller_name, _ = resolve(int(row['caller'], 16), symbols)
        rows.append(dict(row, function=name or hex(start), function_label=labels.get(main_base + start, {}).get('label'),
                         caller_function=caller_name or hex(caller_start),
                         caller_label=labels.get(main_base + caller_start, {}).get('label')))
    return rows


def attach_symbols(intervals, parsed, symbols, labels, top):
    main_base = main_base_of(parsed)
    names = main_module_names(parsed)
    for item in intervals:
        table = item.get('sampled')
        if not table:
            continue
        functions, no_frame, engine_slot, engine_samples = aggregate_functions(
            table, symbols, labels, main_base, names, item['hooked']['operations'], top)
        table['functions'] = functions
        table['frames_without_main_module'] = no_frame
        table['engine_slot'] = engine_slot
        table['engine_samples'] = engine_samples
        table['pairs'] = symbolize_pairs(table['pairs'], symbols, labels, main_base, top)
        for row in table['leaves'][:top]:
            if row['module'].lower() in names:
                start, name, resolved = resolve(int(row['rva'], 16), symbols)
                row['function'] = name or (hex(start) if resolved else None)
        for row in table['frames'][:top]:
            start, name, resolved = resolve(int(row['rva'], 16), symbols)
            row['function'] = name or (hex(start) if resolved else None)
        table['leaves'] = table['leaves'][:top]
        table['frames'] = table['frames'][:top]


def ghidra_command(rva_path, symbols_path, project=GHIDRA_PROJECT, name=GHIDRA_PROJECT_NAME,
                   program=GHIDRA_PROGRAM, script_dir=None):
    script_dir = script_dir or Path(__file__).resolve().parent
    return [GHIDRA_HEADLESS, str(project), name, '-process', program, '-readOnly', '-noanalysis',
            '-scriptPath', str(script_dir), '-postScript', 'X3ProfileSymbols.java',
            str(rva_path), str(symbols_path)]


def run_ghidra(rva_path, symbols_path, **options):
    command = ghidra_command(rva_path, symbols_path, **options)
    env = dict(os.environ, JAVA_HOME=os.environ.get('JAVA_HOME', JAVA_HOME))
    started = time.time()
    completed = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True)
    notes = [line for line in completed.stdout.splitlines() if 'X3ProfileSymbols' in line]
    if completed.returncode != 0 or not Path(symbols_path).exists():
        raise RuntimeError('Ghidra symbolization failed:\n' + completed.stdout[-4000:])
    return dict(seconds=time.time() - started, notes=notes)


def analyze(path, threshold=2.0, hash_source=True, symbols=None, labels=None, top=TOP):
    """Gap detection plus per-interval hooked and sampled tables (unsymbolized)."""
    loading_lines, profile_lines, provenance = scan(path, hash_source)
    clock, anchors, present, windows, _, _ = loading.parse(loading_lines)
    parsed = profile.parse(profile_lines)
    delta_blocks = [b for b in parsed['blocks'] if b['scope'] == 'delta']
    found = loading.gaps(present, threshold)
    gap_source = 'present'
    if clock is None:
        # No telemetry at all (a plain --direct run): the frame_end lines bound the gaps.
        gap_source = 'frame_end'
        found = loading.frame_end_gaps(anchors['frame_ends'], threshold)
    accountings = [loading.attribute(gap, windows) for gap in found]
    gaps = []
    labels_so_far = []
    for index, (gap, accounting) in enumerate(zip(found, accountings), 1):
        label, evidence = label_gap(accounting['operations'], labels_so_far)
        labels_so_far.append(label)
        start, end = accounting['start_seconds'], accounting['end_seconds']
        if gap_source == 'frame_end':
            label, evidence = 'frame_end gap', (f"{gap['window_frames']} frames between two frame_end lines "
                                                f"({gap['clock']} clock); no telemetry in this log")
        item = dict(index=index, label=label, evidence=evidence, gap_seconds=gap['gap_seconds'],
                    start_seconds=start, end_seconds=end, end_frame=gap['end_frame'],
                    hooked=dict(windows=accounting['windows'],
                                straddling_windows=accounting['straddling_windows'],
                                operations=accounting['operations'], probes=accounting['probes'],
                                exclusive_seconds=accounting['instrumented_exclusive_seconds']),
                    unexplained_seconds=accounting['unexplained_seconds'],
                    sampled=sampled(parsed, start, end) if delta_blocks else None,
                    stalls=[])
        for number, stall in enumerate(accounting['report_stalls'], 1):
            s_start, s_end = stall['begin_seconds'], stall['end_seconds']
            hooked = instrumented(windows, s_start, s_end)
            item['stalls'].append(dict(
                index=number, start_seconds=s_start, end_seconds=s_end,
                interval_seconds=stall['interval_seconds'], hooked=hooked,
                unexplained_seconds=stall['interval_seconds'] - hooked['exclusive_seconds'],
                sampled=sampled(parsed, s_start, s_end) if delta_blocks else None))
        gaps.append(item)
    intervals = gaps + [stall for gap in gaps for stall in gap['stalls']]
    start_fields = parsed['start'] or {}
    result = dict(
        source=provenance, clock=clock, threshold_seconds=threshold, gap_source=gap_source,
        probe_sites=anchors['probe_sites'], probe_paths=anchors['probe_paths'],
        frame_ends=len(anchors['frame_ends']),
        coverage_begin_seconds=(loading.seconds(anchors['coverage_begin_qpc'], clock)
                                if anchors['coverage_begin_qpc'] and clock else None),
        hooks=anchors['hooks'], first_presents=anchors['first_presents'],
        last_present_seconds=present[-1]['seconds'] if present else 0.0,
        report_windows=len(windows),
        profile=dict(present=bool(delta_blocks), delta_blocks=len(delta_blocks),
                     interval_us=start_fields.get('interval_us'), report_s=start_fields.get('report_s'),
                     main_base=f'0x{main_base_of(parsed):08x}',
                     modules=len(parsed['modules']),
                     first_report_s=profile.seconds(delta_blocks[0]['qpc'], parsed) if delta_blocks else None,
                     last_report_s=profile.seconds(delta_blocks[-1]['qpc'], parsed) if delta_blocks else None),
        gaps=gaps, symbols=dict(source=None, resolved=0, requested=0), labels_known=len(labels or {}),
        limits=[
            'Hooked seconds are completion deltas of report windows overlapping the interval; only the exclusive column may be added and it is a lower bound.',
            'Profiler delta blocks are attributed by overlap of their report interval; a block straddling a boundary counts in both neighbours and is reported as straddling.',
            'Per-block leaf/frame/pair tables are truncated to the top 48/48/32 rows, so function sums are lower bounds; per-thread module splits and sample totals are exact.',
            'Frame RVAs are return addresses; inclusive-by-frame shares attribute DLL and wait time to the first main-executable frame, not to a full call stack.',
            'Gap labels are heuristics over hooked counts (label_gap); no phase marker exists in the log.',
            'The probe exclusive column subtracts only the timed probe children of the documented nesting '
            '(analyze_iteration08_loading.PROBE_CHILDREN); it still contains the hooked imports and the CRT work '
            'below the site, and it is left blank for the two texture sites whose resource_load child has eleven callers.',
            'Probe stub cost is a bound from the fixture envelope components, not a per-site measurement; '
            'the wrapper tail column is measured.',
            'Write-side paths are logged once each (first 16) and are placed by their report window, '
            'so a path may be listed one window away from the interval that made the call.',
            'Function labels come from hand-maintained notes; an unlabelled function is merely undocumented, not unimportant.'])
    result['_parsed'] = parsed
    result['_intervals'] = intervals
    return result


def finish(result, symbols, symbol_source, labels, top=TOP):
    parsed = result.pop('_parsed')
    intervals = result.pop('_intervals')
    attach_symbols(intervals, parsed, symbols, labels, top)
    requested = collect_rvas(intervals, main_module_names(parsed))
    result['symbols'] = dict(source=symbol_source, requested=len(requested),
                             resolved=sum(1 for rva in requested if rva in symbols and symbols[rva]))
    return result


def pct(value):
    return f'{value * 100:.1f} %'


def secs(value):
    return f'{value:.3f} s'


def render_hooked(hooked, limit=12):
    ops = sorted(hooked['operations'].items(), key=lambda kv: -kv[1]['inclusive_seconds'])[:limit]
    if not ops:
        return ['No hooked call completed inside this interval.', '']
    lines = ['| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |', '| --- | ---: | ---: | ---: | ---: | ---: |']
    for op, m in ops:
        lines.append(f"| `{op}` | {m['count']:,} | {m['inclusive_seconds']:.3f} | {m['exclusive_seconds']:.3f} "
                     f"| {m['mean_ms']:.3f} | {m['bytes']:,} |")
    lines.append('')
    return lines


def render_probes(probes, sites, limit=16):
    """Engine probe table for one interval (loading_probe deltas of the overlapping windows)."""
    rows = sorted(probes.items(), key=lambda kv: -kv[1]['inclusive_seconds'])[:limit]
    if not rows:
        return []
    lines = ['| Probe | Calls | Exits | Incl. s | Excl. s | Mean ms | Max ms | Bytes | Extras |',
             '| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |']
    for site, m in rows:
        names = sites.get(site, {}).get('extras', ['x0', 'x1', 'x2', 'x3'])
        extras = ', '.join(f"{name}={m['x%d' % i]:,}" for i, name in enumerate(names) if name != '-' and m['x%d' % i])
        if m['overflow'] or m['desync']:
            extras = (extras + ', ' if extras else '') + f"overflow={m['overflow']}, desync={m['desync']}"
        for caller, count in sorted(m.get('callers', {}).items(), key=lambda kv: -kv[1])[:4]:
            extras = (extras + ', ' if extras else '') + f"caller {caller} {count:,}"
        excl = m.get('exclusive_seconds')
        excl_text = '—' if excl is None else f'{excl:.3f}'
        lines.append(f"| `{site}` | {m['calls']:,} | {m['exits']:,} | {m['inclusive_seconds']:.3f} | {excl_text} "
                     f"| {m['mean_ms']:.3f} | {m['max_seconds'] * 1000:.1f} | {m['bytes']:,} | {extras or '—'} |")
    lines.append('')
    return lines


def render_writes(hooked, paths, start, end):
    """Write-side file APIs inside one interval (§7 of the stall study).

    The question the table answers is whether any file write can overlap a
    catalogue read: if it cannot, a catalogue handle pool and a negative
    name-probe cache need no invalidation inside a load.
    """
    ops = hooked['operations']
    rows = [(op, ops[op]) for op in loading.WRITE_OPS if op in ops and ops[op]['count']]
    reads = hooked.get('probes', {}).get('resource_read', {})
    catalogue = reads.get('x1', 0)
    if not rows:
        return [f'No write-side call (`{"`, `".join(loading.WRITE_OPS)}`) completed in this interval, '
                f'against {catalogue:,} catalogue reads: nothing can invalidate a catalogue handle '
                'or a negative name probe here.', '']
    lines = ['| Operation | Calls | Failures | Incl. s | Bytes | Paths logged |',
             '| --- | ---: | ---: | ---: | ---: | --- |']
    for op, m in rows:
        seen = [item['path'] for item in paths
                if item['op'] == op and (item['report_seconds'] is None
                                         or start - 1.0 <= item['report_seconds'] <= end + 1.0)]
        shown = '; '.join(f'`{path}`' for path in seen[:4]) or '—'
        lines.append(f"| `{op}` | {m['count']:,} | {m['failures']:,} | {m['inclusive_seconds']:.3f} "
                     f"| {m['bytes']:,} | {shown} |")
    total = sum(m['count'] for _, m in rows)
    failed = sum(m['failures'] for _, m in rows)
    lines += ['', f'{total:,} write-side calls ({failed:,} failed) against {catalogue:,} catalogue reads '
                  'in the same interval.', '']
    return lines


def render_instrumentation(hooked, length):
    """What the instrumentation itself cost inside one interval."""
    ops = hooked['operations']
    tail = sum(m['wrapper_tail_seconds'] for m in ops.values())
    hooked_calls = sum(m['count'] for m in ops.values())
    inclusive = sum(m['inclusive_seconds'] for m in ops.values())
    probes = hooked.get('probes', {})
    timed = sum(m['calls'] for site, m in probes.items() if m['inclusive_seconds'] > 0 or m['exits'])
    counted = sum(m['calls'] for site, m in probes.items() if not (m['inclusive_seconds'] > 0 or m['exits']))
    probe_cost = (timed * TIMED_PROBE_NS + counted * COUNT_PROBE_NS) / 1e9
    lines = ['| Item | Calls | Seconds | Share of interval |', '| --- | ---: | ---: | ---: |',
             f'| import wrapper tails (measured) | {hooked_calls:,} | {tail:.3f} | '
             f'{pct(tail / length if length else 0)} |',
             f'| timed probe stubs (bound, {TIMED_PROBE_NS:.0f} ns/call) | {timed:,} | '
             f'{timed * TIMED_PROBE_NS / 1e9:.3f} | {pct(timed * TIMED_PROBE_NS / 1e9 / length if length else 0)} |',
             f'| count-only probe stubs (bound, {COUNT_PROBE_NS:.0f} ns/call) | {counted:,} | '
             f'{counted * COUNT_PROBE_NS / 1e9:.3f} | {pct(counted * COUNT_PROBE_NS / 1e9 / length if length else 0)} |',
             f'| **total instrumentation** | {hooked_calls + timed + counted:,} | '
             f'**{tail + probe_cost:.3f}** | {pct((tail + probe_cost) / length if length else 0)} |',
             '',
             f'Hooked inclusive time in the interval is {inclusive:.3f} s; the wrapper tail is '
             f'{pct(tail / inclusive if inclusive else 0)} of it. The span between the two clock reads '
             'of a light row is inside the inclusive column, so that part of the envelope is already '
             'attributed to the operation it wraps.', '']
    return lines


def render_threads(table):
    lines = ['| Slot | TID | Samples | ' + ' | '.join(MODULE_KINDS) + ' | Start |',
             '| ---: | ---: | ---: | ' + ' | '.join('---:' for _ in MODULE_KINDS) + ' | --- |']
    for row in table['threads']:
        cells = ' | '.join(f"{row['leaf_' + kind]:,}" for kind in MODULE_KINDS)
        start = f"{row.get('start_module')}:{row.get('start_rva')}"
        lines.append(f"| {row['slot']} | {row['tid']} | {row['samples']:,} | {cells} | {start} |")
    total = table['samples'] or 1
    split = ', '.join(f"{kind} {pct(table['module_split']['leaf_' + kind] / total)}" for kind in MODULE_KINDS)
    lines += ['', f'All threads: {split}.', '']
    return lines


def render_functions(table):
    rows = table.get('functions', [])
    if not rows:
        return ['No main-executable leaf or frame samples in this interval.', '']
    engine = table.get('engine_samples') or 0
    head = (f"Shares are of the engine thread (slot {table.get('engine_slot')}, {engine:,} samples); "
            'the other sampled threads are idle Wine/audio/input waiters.')
    lines = [head, '',
             '| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |',
             '| --- | --- | --- | ---: | ---: | ---: | --- |']
    for row in rows:
        hooked = '; '.join(f"`{h['op']}` {h['count']:,} calls / {h['inclusive_seconds']:.3f} s" for h in row['hooked']) or '—'
        name = row['name'] or ('(unresolved RVA)' if not row['resolved'] else '')
        busiest = (f"slot {row['busiest_slot']} {pct(row['busiest_slot_share'])}"
                   if row['busiest_slot'] is not None else '—')
        lines.append(f"| `{row['start_va']}` | {name} | {row['label'] or '—'} | {pct(row['self_share_engine'])} "
                     f"({row['self_samples']:,}) | {pct(row['inclusive_share_engine'])} ({row['inclusive_samples']:,}) "
                     f"| {busiest} | {hooked} |")
    if table.get('frames_without_main_module'):
        lines.append(f"| (no main-module frame) | | | | {pct(table['frames_without_main_module'] / (table['samples'] or 1))} "
                     f"({table['frames_without_main_module']:,}) | | |")
    lines.append('')
    return lines


def render_pairs(table, limit=12):
    rows = table.get('pairs', [])[:limit]
    if not rows:
        return []
    lines = ['| Slot | Frame | Caller | Samples |', '| ---: | --- | --- | ---: |']
    for row in rows:
        frame = f"`{row['rva']}` {row['function']}" + (f" ({row['function_label']})" if row.get('function_label') else '')
        caller = f"`{row['caller']}` {row['caller_function']}" + (f" ({row['caller_label']})" if row.get('caller_label') else '')
        lines.append(f"| {row['slot']} | {frame} | {caller} | {row['count']:,} |")
    lines.append('')
    return lines


def candidates(item, length, table):
    """Purely mechanical statements about the interval."""
    hooked = item['hooked']['exclusive_seconds']
    sentences = [f"Hooked calls account for {secs(hooked)} of the {secs(length)} interval "
                 f"({pct(hooked / length if length else 0)}); {secs(item['unexplained_seconds'])} is unexplained by any hook."]
    if not table:
        sentences.append('No sampled attribution exists for this interval.')
        return ' '.join(sentences)
    total = table['samples'] or 1
    split = table['module_split']
    dominant = max(MODULE_KINDS, key=lambda kind: split['leaf_' + kind])
    sentences.append(f"The sampler recorded {table['samples']:,} samples over {secs(table['covered_seconds'])} of "
                     f"report coverage ({table['samples_per_second']:.0f} samples/s, "
                     f"{table['ticks_per_second']:.0f} ticks/s per thread); the dominant leaf module is "
                     f"{dominant} at {pct(split['leaf_' + dominant] / total)}.")
    if table['straddling_blocks']:
        sentences.append(f"{table['straddling_blocks']} of {table['blocks']} delta blocks straddle the interval boundary "
                         'and carry samples from outside it.')
    for row in table.get('functions', [])[:3]:
        name = row['name'] or ('an unresolved RVA' if not row['resolved'] else 'an unnamed function')
        known = f"it is a known routine ({row['label']})" if row['label'] else 'it is not a known routine'
        slot = (f" ({pct(row['busiest_slot_share'])} of slot {row['busiest_slot']}'s samples)"
                if row['busiest_slot'] is not None else '')
        sentences.append(f"Function `{row['start_va']}` ({name}) holds {pct(row['self_share_engine'])} of the engine thread's "
                         f"samples as leaf{slot} and {pct(row['inclusive_share_engine'])} as first main-module frame; {known}.")
    return ' '.join(sentences)


def render_interval(item, length, heading, evidence=None, sites=None, paths=None):
    table = item['sampled']
    lines = [heading, '']
    if evidence:
        lines.append(f'Label evidence: {evidence}.')
    lines.append(f"Interval {item['start_seconds']:.3f}–{item['end_seconds']:.3f} s, length {secs(length)}; "
                 f"hooked exclusive {secs(item['hooked']['exclusive_seconds'])} over {item['hooked']['windows']} report windows "
                 f"({item['hooked']['straddling_windows']} straddling); unexplained {secs(item['unexplained_seconds'])}.")
    if table:
        cost = table['sampler_cost']
        lines.append(f"Samples {table['samples']:,} in {table['blocks']} delta blocks ({table['straddling_blocks']} straddling), "
                     f"{table['covered_seconds']:.3f} s covered, {table['samples_per_second']:.0f} samples/s, "
                     f"{table['ticks_per_second']:.0f} ticks/s per thread, {table['dropped']} dropped; sampler tick "
                     f"{cost['tick_us_mean']:.0f} µs mean / {cost['tick_us_max']:.0f} µs max, busy {pct(cost['busy_share'])} of the covered time.")
    else:
        lines.append('**No profile data**: no `profile_report scope=delta` block overlaps this interval.')
    lines.append('')
    lines += ['**Hooked time**', ''] + render_hooked(item['hooked'])
    probes = render_probes(item['hooked'].get('probes', {}), sites or {})
    if probes:
        lines += ['**Engine probes (loading_probe deltas of the overlapping windows)**', ''] + probes
    lines += ['**Write-side file APIs**', ''] + render_writes(
        item['hooked'], paths or [], item['start_seconds'], item['end_seconds'])
    lines += ['**Instrumentation cost**', ''] + render_instrumentation(item['hooked'], length)
    if table:
        lines += ['**Sampled attribution: per-thread module split**', ''] + render_threads(table)
        lines += ['**Top functions (leaf samples by containing function)**', ''] + render_functions(table)
        pairs = render_pairs(table)
        if pairs:
            lines += ['**Top caller pairs**', ''] + pairs
    lines += ['**Candidates**', '', candidates(item, length, table), '']
    return lines


def render(result):
    source, prof = result['source'], result['profile']
    clock_text = f"QPC {result['clock']['frequency']:,} Hz" if result.get('clock') else 'no telemetry clock (frame_end elapsed_ms since DllMain)'
    lines = [f"# Loading attribution: `{source['name']}`", '',
             f"Source {source['bytes']:,} bytes, sha256 `{source['sha256']}`; {clock_text}; "
             f"{result['report_windows']} loading report windows; {result['hooks']} import hooks; "
             f"last Present at {result['last_present_seconds']:.3f} s.",
             '']
    if result.get('gap_source') == 'frame_end':
        lines.append(f"**Gaps come from the {result.get('frame_ends', 0)} `frame_end` lines** (every 300 frames or a capture frame): "
                     'each gap is the interval between two lines whose dt_ms exceeds the threshold, so it also contains up to 300 ordinary frames.')
        lines.append('')
    if result.get('probe_sites'):
        active = sorted(name for name, site in result['probe_sites'].items() if site.get('status') == 'active')
        lines.append(f"Engine probes: {len(active)} of {len(result['probe_sites'])} sites active ({', '.join(active) or 'none'}).")
        lines.append('')
    if prof['present']:
        lines.append(f"Profiler: {prof['delta_blocks']} delta blocks from {prof['first_report_s']:.3f} to {prof['last_report_s']:.3f} s, "
                     f"interval {prof['interval_us']} µs, report period {prof['report_s']} s, {prof['modules']} modules, main base {prof['main_base']}. "
                     f"Symbols: {result['symbols']['resolved']}/{result['symbols']['requested']} RVAs resolved"
                     + (f" ({result['symbols']['source']})" if result['symbols']['source'] else ' (no symbol file)')
                     + f"; {result['labels_known']} labelled function starts.")
    else:
        lines.append('**The log contains no `profile_*` lines**: every section below has hooked time only. '
                     'Launch with `--profile` (`X3M_PROFILE=1`) for sampled attribution.')
    lines += ['', f"## Gaps over {result['threshold_seconds']:.1f} s", '']
    if not result['gaps']:
        lines += ['No presentation gap over the threshold.', '']
    else:
        lines += ['| # | Label | Gap | Interval | Hooked excl. | Unexplained | Samples | Stalls |',
                  '| ---: | --- | ---: | --- | ---: | ---: | ---: | ---: |']
        for gap in result['gaps']:
            samples = f"{gap['sampled']['samples']:,}" if gap['sampled'] else 'none'
            lines.append(f"| {gap['index']} | {gap['label']} | {gap['gap_seconds']:.3f} s | {gap['start_seconds']:.3f}–{gap['end_seconds']:.3f} s "
                         f"| {gap['hooked']['exclusive_seconds']:.3f} s | {gap['unexplained_seconds']:.3f} s | {samples} | {len(gap['stalls'])} |")
        lines.append('')
    for gap in result['gaps']:
        lines += render_interval(gap, gap['gap_seconds'],
                                 f"## Gap {gap['index']}: {gap['label']} ({gap['gap_seconds']:.3f} s, ends at frame {gap['end_frame']})",
                                 gap['evidence'], result.get('probe_sites'), result.get('probe_paths'))
        for stall in gap['stalls']:
            lines += render_interval(stall, stall['interval_seconds'],
                                     f"### Gap {gap['index']} stall {stall['index']}: report stall {stall['interval_seconds']:.3f} s",
                                     None, result.get('probe_sites'), result.get('probe_paths'))
    lines += ['## Limits', ''] + [f'- {limit}' for limit in result['limits']] + ['']
    return '\n'.join(lines)


def write_outputs(result, directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'loading-profile.json').write_text(
        json.dumps(result, separators=(',', ':'), sort_keys=True) + '\n')
    (directory / 'loading-profile.md').write_text(render(result))
    return directory / 'loading-profile.json', directory / 'loading-profile.md'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('log', type=Path)
    parser.add_argument('--output', required=True, type=Path, help='directory for loading-profile.{json,md}')
    parser.add_argument('--symbols', type=Path, help='JSON from X3ProfileSymbols.java')
    parser.add_argument('--ghidra', action='store_true', help='run X3ProfileSymbols.java headless on the collected RVAs')
    parser.add_argument('--ghidra-project', default=GHIDRA_PROJECT)
    parser.add_argument('--ghidra-name', default=GHIDRA_PROJECT_NAME)
    parser.add_argument('--ghidra-program', default=GHIDRA_PROGRAM)
    parser.add_argument('--labels', type=Path, default=LABELS_PATH)
    parser.add_argument('--threshold', type=float, default=2.0, help='minimum presentation gap in seconds')
    parser.add_argument('--top', type=int, default=TOP)
    parser.add_argument('--no-hash', action='store_true')
    args = parser.parse_args()

    started = time.time()
    labels = load_labels(args.labels)
    result = analyze(args.log, args.threshold, not args.no_hash, labels=labels, top=args.top)
    scanned = time.time() - started
    args.output.mkdir(parents=True, exist_ok=True)
    symbols, source = {}, None
    rvas = collect_rvas(result['_intervals'], main_module_names(result['_parsed']))
    rva_path = args.output / 'rvas.txt'
    rva_path.write_text(''.join(f'{hex(rva)}\n' for rva in rvas))
    if args.ghidra and rvas:
        symbols_path = args.output / 'symbols.json'
        run = run_ghidra(rva_path, symbols_path, project=args.ghidra_project, name=args.ghidra_name,
                         program=args.ghidra_program)
        for note in run['notes']:
            print(note)
        print(f'ghidra: {run["seconds"]:.1f} s')
        symbols, source = profile.load_symbols(symbols_path), str(symbols_path)
    elif args.ghidra:
        print('ghidra: skipped, no main-module RVAs inside any gap')
    elif args.symbols:
        symbols, source = profile.load_symbols(args.symbols), str(args.symbols)
    finish(result, symbols, source, labels, args.top)
    json_path, md_path = write_outputs(result, args.output)
    gaps = result['gaps']
    summary = ', '.join('{} {:.1f} s'.format(g['label'], g['gap_seconds']) for g in gaps) or 'none'
    print(f"{result['source']['name']}: {result['source']['bytes']:,} bytes scanned in {scanned:.1f} s; "
          f"{len(gaps)} gaps over {args.threshold:g} s ({summary}); "
          f"profile blocks {result['profile']['delta_blocks']}; {len(rvas)} RVAs, "
          f"{result['symbols']['resolved']} resolved; total {time.time() - started:.1f} s")
    print(f'wrote {json_path} and {md_path}')


if __name__ == '__main__':
    main()
