#!/usr/bin/env python3
"""Attribute the live motion route's engine-thread CPU cost from a gameplay log.

Two independent instruments in the same session log are combined:

* the in-process sampling profiler (`X3M_PROFILE=1`, `profile_*` lines), whose
  per-thread leaf-kind counters are exact and give the engine thread's module
  split (x3ap / our proxy d3d9.dll / wine backend / ntdll / d3dx), and whose
  per-report leaf tables name individual instruction RVAs;
* the route's own telemetry (`motion_output_frame` lines), whose `gate_us`,
  `route_draw_us`, `set_rt_us`, `fill_us` and `taa_run_us` are CPU-inclusive QPC
  spans with per-frame `draws` / `routed` / `set_rt` counts.

What the profiler can and cannot answer (`src/proxy/sampling_profiler.cpp`):

* `profile_thread` leaf-kind counters are **exact** sample counts, so the module
  split of the engine thread is exact.
* `profile_leaf` rows carry a module index, so leaves *can* be attributed to our
  proxy and to ntdll. Per-report tables are truncated (top 48 delta / 256
  cumulative rows across all threads), so a module whose samples are spread thin
  over many RVAs is under-listed; `coverage` reports the listed fraction against
  the exact counter and must be read before any per-function claim.
* `profile_frame` / `profile_pair` RVAs are **main-module (X3AP.exe) addresses
  only** (`no_frame` = "no main-module frame" in the sampler): the frame chain
  is collapsed to the nearest X3AP return address and its X3AP caller. There is
  therefore **no proxy-internal call-pair table and no inclusive-by-caller time
  for proxy functions** in this log, and the leaf/frame/pair tables are separate
  aggregates that cannot be joined per sample.

Per-sample wall time is the window's summed report interval divided by the
engine thread's samples in it; the engine thread is always runnable, so a
leaf-kind share of its samples is a share of its wall time. Absolute ms/frame
uses the frame count from the `telemetry_summary` frame/QPC series.

Symbolization: our proxy's RVAs are resolved against the *installed* DLL named
by --proxy-dll, whose SHA-256 must be recorded as this session's provenance; the
module table's `text_rva`/`text_size` is checked against the binary's `.text`
span so a mismatched layout is refused rather than mis-symbolized. ntdll RVAs
are resolved to the nearest preceding export of --ntdll-dll, which names Wine's
syscall thunks (each sampled at a fixed +0xc offset, the syscall return site).
"""
import argparse
import bisect
import json
import re
import subprocess
from collections import defaultdict
from pathlib import Path

LEAF_KINDS = ('leaf_x3ap', 'leaf_proxy', 'leaf_wine', 'leaf_ntdll', 'leaf_d3dx',
              'leaf_zlib', 'leaf_xml', 'leaf_other')
KEEP = (b'profile_', b'telemetry_start ', b'telemetry_summary ', b'motion_output_frame ')


def fields(line):
    out = {}
    for token in line.split()[1:]:
        key, _, value = token.partition('=')
        out[key] = value
    return out


def scan(path):
    """Stream the log, keeping only the sparse profiler and frame-telemetry lines."""
    kept = []
    with Path(path).open('rb') as stream:
        for raw in stream:
            if raw.startswith(KEEP):
                kept.append(raw.decode('utf-8', 'replace').rstrip('\r\n'))
    return kept


def parse(lines):
    anchor = frequency = None
    modules, blocks, summaries, frames, current = {}, [], [], [], None
    for line in lines:
        head, _, _ = line.partition(' ')
        f = fields(line)
        if head == 'telemetry_start':
            anchor, frequency = int(f['qpc']), int(f['qpc_frequency'])
        elif head == 'profile_start':
            if anchor is None:
                anchor, frequency = int(f['qpc']), int(f['frequency'])
        elif head == 'profile_module' and 'size' in f:
            modules[int(f['index'])] = dict(
                name=f['name'], kind=f.get('kind'), base=int(f['base'], 16),
                size=int(f['size'], 16), text_rva=int(f['text_rva'], 16),
                text_size=int(f['text_size'], 16))
        elif head == 'profile_report':
            current = dict(scope=f['scope'], qpc=int(f['qpc']),
                           elapsed_us=float(f.get('elapsed_us', 0)),
                           threads=[], leaves=[], pairs=[])
            blocks.append(current)
        elif head == 'telemetry_summary' and f.get('device') == '1':
            summaries.append((int(f['qpc']), int(f['frame'])))
        elif head == 'motion_output_frame' and f.get('device') == '1':
            frames.append(f)
        elif current is None:
            continue
        elif head == 'profile_thread':
            current['threads'].append(f)
        elif head == 'profile_leaf':
            current['leaves'].append(f)
        elif head == 'profile_pair':
            current['pairs'].append(f)
    summaries.sort()
    return dict(anchor=anchor, frequency=frequency, modules=modules, blocks=blocks,
                summaries=summaries, frames=frames)


def seconds(qpc, parsed):
    return (qpc - parsed['anchor']) / parsed['frequency']


class FrameClock:
    """Frame number <-> seconds after proxy initialization, from telemetry_summary."""

    def __init__(self, parsed):
        self.t = [seconds(q, parsed) for q, _ in parsed['summaries']]
        self.f = [n for _, n in parsed['summaries']]

    def frames_between(self, lo, hi):
        """Frames presented in [lo, hi], interpolating inside a summary interval."""
        if not self.t:
            return 0.0
        return max(0.0, self._at(hi) - self._at(lo))

    def _at(self, when):
        i = bisect.bisect_left(self.t, when)
        if i == 0:
            return float(self.f[0])
        if i >= len(self.t):
            return float(self.f[-1])
        t0, t1, f0, f1 = self.t[i - 1], self.t[i], self.f[i - 1], self.f[i]
        return f0 if t1 <= t0 else f0 + (f1 - f0) * (when - t0) / (t1 - t0)

    def time_of(self, frame):
        i = bisect.bisect_left(self.f, frame)
        if i == 0:
            return self.t[0] if self.t else 0.0
        if i >= len(self.f):
            return self.t[-1] if self.t else 0.0
        f0, f1, t0, t1 = self.f[i - 1], self.f[i], self.t[i - 1], self.t[i]
        return t0 if f1 <= f0 else t0 + (t1 - t0) * (frame - f0) / (f1 - f0)


# ---- symbolization ---------------------------------------------------------

def text_span(dll):
    """(.text rva, page-rounded rva span) of a PE, as the sampler would log it."""
    out = subprocess.run(['i686-w64-mingw32-objdump', '-h', str(dll)],
                         capture_output=True, text=True, check=True).stdout
    m = re.search(r'^\s*\d+\s+\.text\s+([0-9a-f]+)\s+([0-9a-f]+)', out, re.M)
    if not m:
        return None
    size, vma = int(m.group(1), 16), int(m.group(2), 16)
    base = vma & ~0xFFFF                     # image base: sections start at +0x1000
    rva = vma - base
    end = (rva + size + 0xFFF) & ~0xFFF
    return rva, end - rva


def image_base(dll):
    """Link-time image base, from the .text section's VMA."""
    out = subprocess.run(['i686-w64-mingw32-objdump', '-h', str(dll)],
                         capture_output=True, text=True).stdout
    m = re.search(r'^\s*\d+\s+\.text\s+[0-9a-f]+\s+([0-9a-f]+)', out, re.M)
    return (int(m.group(1), 16) & ~0xFFFF) if m else 0


def proxy_symbols(dll, rvas):
    """RVA -> (function, file:line) via DWARF, for the *installed* proxy binary."""
    if not rvas:
        return {}
    base = image_base(dll)
    ordered = sorted(rvas)
    proc = subprocess.run(['i686-w64-mingw32-addr2line', '-f', '-C', '-e', str(dll)]
                          + [hex(base + r) for r in ordered], capture_output=True, text=True)
    lines = proc.stdout.splitlines()
    table = {}
    for i, rva in enumerate(ordered):
        function = lines[2 * i].strip() if 2 * i < len(lines) else '?'
        location = lines[2 * i + 1].strip() if 2 * i + 1 < len(lines) else '?'
        location = re.sub(r'^.*/(src|include)/', r'\1/', location)
        table[rva] = (function, location)
    return table


def export_table(dll):
    out = subprocess.run(['i686-w64-mingw32-objdump', '-x', str(dll)],
                         capture_output=True, text=True).stdout.splitlines()
    addresses, names, mode = {}, {}, None
    for line in out:
        if line.startswith('Export Address Table'):
            mode = 'address'
            continue
        if line.startswith('[Ordinal/Name Pointer] Table'):
            mode = 'name'
            continue
        if mode == 'address':
            m = re.match(r'\s*\[\s*(\d+)\]\s*\+base\[\s*\d+\]\s+([0-9a-f]{8})\s+Export RVA', line)
            if m:
                addresses[int(m.group(1))] = int(m.group(2), 16)
        elif mode == 'name':
            m = re.match(r'\s*\[\s*(\d+)\]\s*\+base\[\s*\d+\]\s+[0-9a-f]{4}\s+(\S+)', line)
            if m:
                names[int(m.group(1))] = m.group(2)
    return sorted({(addresses[o], n) for o, n in names.items() if o in addresses})


def nearest_export(exports, rva):
    if not exports:
        return hex(rva)
    keys = [e[0] for e in exports]
    i = bisect.bisect_right(keys, rva) - 1
    return f'{exports[i][1]}+0x{rva - exports[i][0]:x}' if i >= 0 else hex(rva)


# ---- windows ---------------------------------------------------------------

def capture_windows(parsed, clock, pad=5.0):
    """One window around each capture burst, from the frames with readbacks."""
    bursts, current = [], []
    for f in parsed['frames']:
        if int(f.get('readbacks', 0)):
            frame = int(f['frame'])
            if current and frame - current[-1] > 8:
                bursts.append(current)
                current = []
            current.append(frame)
    if current:
        bursts.append(current)
    return [(f'burst{i + 1}', round(clock.time_of(b[0]) - pad, 2), round(clock.time_of(b[-1]) + pad, 2))
            for i, b in enumerate(bursts)]


def routed_span(parsed, clock):
    """The contiguous span in which the route reported routed draws."""
    routed = [int(f['frame']) for f in parsed['frames'] if int(f.get('routed', 0))]
    if not routed:
        return None
    return round(clock.time_of(min(routed)), 2), round(clock.time_of(max(routed)), 2)


# ---- per-window attribution ------------------------------------------------

def engine_slot(parsed, tid=None):
    """The engine thread's (slot, tid): the thread with the most samples overall."""
    totals = defaultdict(int)
    for b in parsed['blocks']:
        if b['scope'] != 'delta':
            continue
        for t in b['threads']:
            totals[(t['slot'], t['tid'])] += int(t['samples'])
    if tid:
        for key in totals:
            if key[1] == str(tid):
                return key
    return max(totals.items(), key=lambda kv: kv[1])[0] if totals else ('0', '0')


def window(parsed, clock, label, lo, hi, slot, tid, top):
    kinds = defaultdict(int)
    samples = 0
    covered_lo = covered_hi = None
    leaves = defaultdict(lambda: defaultdict(int))
    pairs = defaultdict(int)
    for b in parsed['blocks']:
        if b['scope'] != 'delta':
            continue
        end = seconds(b['qpc'], parsed)
        begin = end - b['elapsed_us'] / 1e6
        if not (end >= lo and begin <= hi):
            continue
        covered_lo = begin if covered_lo is None else min(covered_lo, begin)
        covered_hi = end if covered_hi is None else max(covered_hi, end)
        for t in b['threads']:
            if t['tid'] != tid:
                continue
            samples += int(t['samples'])
            for k in LEAF_KINDS:
                kinds[k] += int(t.get(k, 0))
        for row in b['leaves']:
            if row['slot'] != slot:
                continue
            kind = parsed['modules'].get(int(row['module']), {}).get('kind', 'other')
            leaves[kind][int(row['rva'], 16)] += int(row['count'])
        for row in b['pairs']:
            if row['slot'] == slot:
                pairs[(int(row['rva'], 16), int(row['caller'], 16))] += int(row['count'])
    # The reports that overlap [lo, hi] cover [covered_lo, covered_hi]; elapsed
    # time and frames are both taken over that span so ms/frame is consistent.
    elapsed = (covered_hi - covered_lo) if covered_lo is not None else 0.0
    frames = clock.frames_between(covered_lo, covered_hi) if covered_lo is not None else 0.0
    ms_per_sample = (elapsed / samples * 1e3) if samples else 0.0
    per_frame = (samples / frames) if frames else 0.0

    def cost(count):
        """(share of engine thread, ms of engine thread per presented frame)."""
        share = count / samples if samples else 0.0
        return dict(samples=count, share=round(share, 5),
                    ms_per_frame=round(share * per_frame * ms_per_sample, 3))

    return dict(
        label=label, from_s=lo, to_s=hi,
        covered_s=[round(covered_lo, 2), round(covered_hi, 2)] if covered_lo is not None else None,
        elapsed_s=round(elapsed, 2), samples=samples,
        frames=round(frames, 1), ms_per_sample=round(ms_per_sample, 3),
        engine_ms_per_frame=round(per_frame * ms_per_sample, 3),
        module_split={k[5:]: cost(v) for k, v in sorted(kinds.items(), key=lambda kv: -kv[1]) if v},
        ntdll=dict(
            exact=kinds['leaf_ntdll'],
            listed=sum(leaves['ntdll'].values()),
            coverage=round(sum(leaves['ntdll'].values()) / kinds['leaf_ntdll'], 3) if kinds['leaf_ntdll'] else 0.0,
            rows=[dict(rva=hex(r), **cost(c)) for r, c in
                  sorted(leaves['ntdll'].items(), key=lambda kv: -kv[1])[:top]]),
        proxy=dict(
            exact=kinds['leaf_proxy'],
            listed=sum(leaves['proxy'].values()),
            coverage=round(sum(leaves['proxy'].values()) / kinds['leaf_proxy'], 3) if kinds['leaf_proxy'] else 0.0,
            rows=[dict(rva=hex(r), **cost(c)) for r, c in
                  sorted(leaves['proxy'].items(), key=lambda kv: -kv[1])[:top]]),
        backend=dict(
            exact=kinds['leaf_wine'],
            rows=[dict(rva=hex(r), **cost(c)) for r, c in
                  sorted(leaves['wine'].items(), key=lambda kv: -kv[1])[:12]]),
        x3ap_pairs=[dict(rva=hex(r), caller=hex(k), count=c) for (r, k), c in
                    sorted(pairs.items(), key=lambda kv: -kv[1])[:12]],
        telemetry=telemetry_costs(parsed, clock, covered_lo, covered_hi)
        if covered_lo is not None else None,
    )


def savings(w):
    """Expected ms/frame from each optimization, from this window's own numbers.

    `read_process_memory` is the whole ReadProcessMemory line: the only callers
    of it in the process are ours (`object_trace`, `object_lifetime`,
    `scene_hook`), so every ZwReadVirtualMemory sample is the route's or the
    observer's. `frame_constant_reads` is the part of `object_trace::current`
    that re-reads per-frame constants for every routed draw (the world, world
    basis, view and projection matrices at two reads each, plus the engine and
    registry pointers): 10 of that function's 12 reads.
    """
    by_function = {row.get('function', row['rva']): row for row in w['ntdll']['rows']}
    read = by_function.get('ZwReadVirtualMemory+0xc', {}).get('ms_per_frame', 0.0)
    qpc = by_function.get('ZwQueryPerformanceCounter+0xc', {}).get('ms_per_frame', 0.0)
    write = by_function.get('ZwWriteFile+0xc', {}).get('ms_per_frame', 0.0)
    t = w.get('telemetry') or {}
    per_frame = t.get('per_frame', {})
    stamps = per_frame.get('stamps', 0)
    # Our stamps against the total QPC cost: the game calls QPC too, so this
    # apportions the measured cost by the share the instrumentation can remove.
    return dict(
        read_process_memory_ms=read,
        frame_constant_reads_ms=round(read * 10 / 22, 3),
        telemetry_stamps_ms=qpc,
        stamps_per_frame=stamps,
        us_per_qpc=round(qpc * 1e3 / stamps, 3) if stamps else None,
        logging_ms=write,
        set_rt_ms=per_frame.get('set_rt_us', 0) / 1e3,
    )


# ---- telemetry decomposition ----------------------------------------------

def fit(rows, keys, target):
    """Least squares for target = sum(coefficient * key) with no intercept."""
    n = len(keys)
    a = [[0.0] * n for _ in range(n)]
    b = [0.0] * n
    for row in rows:
        x = [float(row[k]) for k in keys]
        y = float(row[target])
        for i in range(n):
            b[i] += x[i] * y
            for j in range(n):
                a[i][j] += x[i] * x[j]
    for i in range(n):                                  # Gauss-Jordan
        pivot = max(range(i, n), key=lambda r: abs(a[r][i]))
        if abs(a[pivot][i]) < 1e-12:
            return None
        a[i], a[pivot] = a[pivot], a[i]
        b[i], b[pivot] = b[pivot], b[i]
        scale = a[i][i]
        a[i] = [v / scale for v in a[i]]
        b[i] /= scale
        for r in range(n):
            if r != i and a[r][i]:
                factor = a[r][i]
                a[r] = [v - factor * w for v, w in zip(a[r], a[i])]
                b[r] -= factor * b[i]
    return dict(zip(keys, b))


def telemetry_costs(parsed, clock, lo, hi, capture_frames=False):
    """Per-draw µs for the gate, split between routed and non-routed draws.

    `stamps` counts the QPC calls the instrumentation itself makes in one frame:
    every timed span is a pair (`MotionOutput::stamp` returns 0 with telemetry
    off, so the whole count disappears with `X3M_TELEMETRY=0`). Spans counted:
    `route_gate` (one per draw), the apply block and `route_draw` (one each per
    routed draw), `route_set_rt`, `route_jitter`, `route_fill`, `draw_backend`
    (one per draw, in the capture layer) and the seven `taa_*` phases.
    """
    rows = []
    for f in parsed['frames']:
        t = clock.time_of(int(f['frame']))
        if not (lo <= t <= hi) or not int(f.get('routed', 0)):
            continue
        if not capture_frames and int(f.get('readbacks', 0)):
            continue
        routed, draws = int(f['routed']), int(f['draws'])
        set_rt, jitter = int(f['set_rt']), int(f['jitter_writes'])
        rows.append(dict(routed=routed, rejected=draws - routed, draws=draws,
                         set_rt=set_rt, jitter_writes=jitter,
                         stamps=2 * (2 * draws + 2 * routed + set_rt + jitter + 1 + 7),
                         gate_us=float(f['gate_us']),
                         route_draw_us=float(f['route_draw_us']),
                         set_rt_us=float(f['set_rt_us']), fill_us=float(f['fill_us']),
                         taa_run_us=float(f['taa_run_us'])))
    if not rows:
        return None
    total = lambda key: sum(r[key] for r in rows)
    mean = lambda key: round(total(key) / len(rows), 1)
    return dict(
        records=len(rows), mean_draws=mean('draws'), mean_routed=mean('routed'),
        gate=fit(rows, ('routed', 'rejected'), 'gate_us'),
        route_draw_per_routed=round(total('route_draw_us') / max(1, total('routed')), 3),
        set_rt_per_call=round(total('set_rt_us') / max(1, total('set_rt')), 3),
        set_rt_per_routed=round(total('set_rt_us') / max(1, total('routed')), 3),
        exclusive_route_us=round((total('gate_us') + total('route_draw_us')
                                  + total('fill_us')) / len(rows), 1),
        per_frame=dict(
            gate_us=mean('gate_us'), route_draw_us=mean('route_draw_us'),
            set_rt_us=mean('set_rt_us'), fill_us=mean('fill_us'),
            taa_run_us=mean('taa_run_us'), set_rt=mean('set_rt'),
            jitter_writes=mean('jitter_writes'), stamps=mean('stamps')),
    )


def analyze(log, proxy_dll=None, ntdll=None, extra=(), top=20, tid=None, pad=5.0):
    parsed = parse(scan(log))
    clock = FrameClock(parsed)
    slot, engine_tid = engine_slot(parsed, tid)
    windows = list(capture_windows(parsed, clock, pad)) + [tuple(w) for w in extra]
    span = routed_span(parsed, clock)
    result = dict(
        source=str(log), anchor_qpc=parsed['anchor'], qpc_frequency=parsed['frequency'],
        engine_thread=dict(slot=slot, tid=engine_tid),
        routed_span_s=span,
        windows=[window(parsed, clock, label, lo, hi, slot, engine_tid, top)
                 for label, lo, hi in windows],
        symbolization=dict(proxy=None, ntdll=None),
        limits=[
            'profile_thread leaf-kind counters are exact; leaf RVA tables are truncated per report '
            '(coverage states the listed fraction).',
            'profile_frame/profile_pair RVAs are X3AP.exe addresses only, so no proxy-internal '
            'call-pair or inclusive-by-caller time exists in this log.',
            'Leaf, frame and pair tables are separate aggregates and cannot be joined per sample.',
            'ms_per_frame is engine-thread CPU, not GPU time, and boundary reports count in both '
            'neighbouring windows.',
        ],
    )
    if span:
        result['telemetry'] = telemetry_costs(parsed, clock, *span)
    proxy_module = next((m for m in parsed['modules'].values() if m.get('kind') == 'proxy'), None)
    if proxy_dll and proxy_module:
        span_text = text_span(proxy_dll)
        matched = bool(span_text) and span_text == (proxy_module['text_rva'], proxy_module['text_size'])
        result['symbolization']['proxy'] = dict(
            dll=str(proxy_dll), binary_text=span_text,
            log_text=(proxy_module['text_rva'], proxy_module['text_size']), layout_matches=matched)
        if matched:
            rvas = {int(row['rva'], 16) for w in result['windows'] for row in w['proxy']['rows']}
            table = proxy_symbols(proxy_dll, rvas)
            for w in result['windows']:
                for row in w['proxy']['rows']:
                    function, location = table.get(int(row['rva'], 16), ('?', '?'))
                    row['function'], row['location'] = function, location
    ntdll_module = next((m for m in parsed['modules'].values() if m.get('kind') == 'ntdll'), None)
    if ntdll and ntdll_module:
        span_text = text_span(ntdll)
        matched = bool(span_text) and span_text == (ntdll_module['text_rva'], ntdll_module['text_size'])
        result['symbolization']['ntdll'] = dict(
            dll=str(ntdll), binary_text=span_text,
            log_text=(ntdll_module['text_rva'], ntdll_module['text_size']), layout_matches=matched)
        exports = export_table(ntdll) if matched else []
        for w in result['windows']:
            for row in w['ntdll']['rows']:
                row['function'] = nearest_export(exports, int(row['rva'], 16))
    for w in result['windows']:
        w['savings'] = savings(w)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('log', type=Path)
    parser.add_argument('--proxy-dll', type=Path, help='installed d3d9.dll of this session')
    parser.add_argument('--ntdll', type=Path, help="the bottle's ntdll.dll, for syscall names")
    parser.add_argument('--window', action='append', default=[], metavar='LABEL=FROM:TO')
    parser.add_argument('--engine-tid', help='engine thread id (default: busiest thread)')
    parser.add_argument('--pad-s', type=float, default=5.0, help='padding around capture bursts')
    parser.add_argument('--top', type=int, default=20)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    extra = []
    for text in args.window:
        label, _, span = text.partition('=')
        lo, _, hi = span.partition(':')
        extra.append((label, float(lo), float(hi)))
    result = analyze(args.log, args.proxy_dll, args.ntdll, extra, args.top,
                     args.engine_tid, args.pad_s)
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    for w in result['windows']:
        print(f"\n== {w['label']} [{w['from_s']}, {w['to_s']}] elapsed={w['elapsed_s']}s "
              f"frames={w['frames']} engine={w['engine_ms_per_frame']} ms/frame")
        print('   ' + '  '.join(f"{k}={100 * v['share']:.1f}%/{v['ms_per_frame']}ms"
                                for k, v in w['module_split'].items()))
        for row in w['ntdll']['rows'][:6]:
            print(f"   ntdll {row.get('function', row['rva']):40s} "
                  f"{100 * row['share']:5.2f}%  {row['ms_per_frame']:6.3f} ms/frame")
        print(f"   proxy leaf coverage {100 * w['proxy']['coverage']:.0f}% of {w['proxy']['exact']} samples")
        for row in w['proxy']['rows'][:6]:
            print(f"   proxy {row.get('function', row['rva'])[:38]:38s} "
                  f"{row.get('location', '')[:34]:34s} {row['ms_per_frame']:6.3f} ms/frame")
        t = w.get('telemetry')
        if t:
            print(f"   telemetry: {t['records']} records, {t['mean_draws']} draws "
                  f"({t['mean_routed']} routed); gate {t['gate']['routed']:.2f} us/routed, "
                  f"{t['gate']['rejected']:.2f} us/rejected; exclusive route "
                  f"{t['exclusive_route_us'] / 1e3:.2f} ms/frame")
        print('   savings: ' + ' '.join(f'{k}={v}' for k, v in w['savings'].items()))
    t = result.get('telemetry')
    if t:
        print(f"\ntelemetry over the routed span {result['routed_span_s']}: {t['records']} frame records, "
              f"{t['mean_draws']} draws ({t['mean_routed']} routed) per frame")
        print(f"   gate: {t['gate']['routed']:.2f} us per routed draw, "
              f"{t['gate']['rejected']:.2f} us per rejected draw")
        print(f"   route_draw {t['route_draw_per_routed']} us/routed, "
              f"set_rt {t['set_rt_per_call']} us/call ({t['set_rt_per_routed']} us/routed draw)")
        print('   per frame: ' + ' '.join(f'{k}={v}' for k, v in t['per_frame'].items()))


if __name__ == '__main__':
    main()
