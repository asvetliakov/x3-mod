"""The gate traffic controller's ring sizes (gate-traffic-controller.md D.7, stage A) replayed against the Run160b crowd window's row
demand (frames 52900-61300, ring_demand.sh). The logged E rows are the demand cut by the old caps (8 rows per frame, a 32-pair table
with a 1 s dedup). A dropped row is not remembered, so its pair retries on every following E call (each frame while the pair is inside
the engine's box test): a summary interval's gate_rows_dropped delta D over F frames is about D / F pairs left without their row
(the retry model [i]). Their rows are added to the interval's worst frame (all at once: an upper bound per frame) and to its distinct
pairs per second. The naive bound (D spread over the interval's frames at the old cap, every drop a row of its own) is printed beside
it. The replay then applies the new caps:
  * E: 32 rows per frame, a 128-pair table (1 s per pair);
  * key on: capital-capital E rows (both class codes capital) become ship_gate_ctl_pair rows (64 per frame, <= 1 per pair per
    second: the logged E rows are already deduplicated per pair per second, so their per-frame count bounds the pair rows);
  * the warn watch: 32 capital slots held 20 s after their last hit (the warn_pre / engine warn subjects);
  * every other ring at its size (ship_gate_ctl 32, ship_gate_launch 8; the rest unchanged and below 2 rows per frame).
Usage: python3 -I ring_replay.py [/mnt/c/X3/x3m.log] > ring_replay.txt"""
import re
import sys
from collections import defaultdict

LO, HI = 52900, 61300
CAPITAL = {'TL', 'M0', 'M1', 'M2', 'TL_P', 'M7'}
OLD_E, NEW_E, OLD_TABLE, NEW_TABLE, CTL_PAIR, WATCH = 8, 32, 32, 128, 64, 32


def main(path):
    e_rows = defaultdict(list)  # frame -> [(a, b, capital pair)]
    a_code = {}                 # ship -> class code, from rows where it appears as B
    summaries = []              # (frame, dropped delta)
    warn_subjects = []          # (frame, ship)
    frame_ms = {}
    rx_e = re.compile(r'^ship_avoid_e_verdict frame=(\d+) A=(0x[0-9a-f]+) .*?B=(0x[0-9a-f]+) B_class=(\d+) B_code=(\S+)')
    rx_s = re.compile(r'^ship_steering_summary frame=(\d+) .*?gate_rows_dropped=(\d+)')
    rx_w = re.compile(r'^ship_avoid_engine_warn(?:_pre)? frame=(\d+) .*?(?:obj|A)=(0x[0-9a-f]+)')
    with open(path, 'rb') as f:
        for raw in f:
            if not raw.startswith(b'ship_'):
                continue
            line = raw.decode('latin-1')
            m = rx_e.match(line)
            if m:
                fr = int(m.group(1))
                a_code.setdefault(m.group(3), m.group(5))
                if LO <= fr <= HI:
                    e_rows[fr].append((m.group(2), m.group(3), m.group(5)))
                continue
            m = rx_s.match(line)
            if m:
                fr = int(m.group(1))
                if LO <= fr <= HI + 120:
                    summaries.append((fr, int(m.group(2))))
                continue
            m = rx_w.match(line)
            if m and LO <= int(m.group(1)) <= HI:
                warn_subjects.append((int(m.group(1)), m.group(2)))
    # the drops per summary interval: the naive spread over the interval's E frames at the old cap, and the retry model's missing pairs
    extra = defaultdict(float)
    retry_pairs = {}            # interval end frame -> (first frame, missing pairs)
    table_drops = 0
    prev = LO
    total_dropped = 0
    for fr, d in summaries:
        total_dropped += d
        span = range(prev + 1, min(fr, HI) + 1)
        capped = [f for f in span if len(e_rows.get(f, ())) >= OLD_E]
        if d and capped:
            for f in capped:
                extra[f] += d / len(capped)
        elif d:
            table_drops += d
        if d and len(span):
            retry_pairs[fr] = (prev + 1, d / len(span))
        prev = fr
    retry_max_frame = 0.0
    retry_max_pairs = 0.0
    for end, (start, missing) in retry_pairs.items():
        worst = max((len(e_rows.get(f, ())) for f in range(start, min(end, HI) + 1)), default=0)
        retry_max_frame = max(retry_max_frame, worst + missing)
        retry_max_pairs = max(retry_max_pairs, missing)
    # the replay
    drops_e_off = drops_e_on = 0
    max_off = max_on = max_pair = 0
    frames = sorted(set(e_rows) | set(extra))
    for f in frames:
        rows = e_rows.get(f, [])
        cap_pairs = sum(1 for a, b, cb in rows if cb in CAPITAL and a_code.get(a, '?') in CAPITAL)
        demand_off = len(rows) + extra.get(f, 0.0)
        # key on: the capital-capital share moves to the pair rows (the extra demand split in the same proportion)
        share = cap_pairs / len(rows) if rows else 0.0
        demand_on = (len(rows) - cap_pairs) + extra.get(f, 0.0) * (1.0 - share)
        pair_rows = cap_pairs + extra.get(f, 0.0) * share
        max_off, max_on, max_pair = max(max_off, demand_off), max(max_on, demand_on), max(max_pair, pair_rows)
        drops_e_off += max(0.0, demand_off - NEW_E)
        drops_e_on += max(0.0, demand_on - NEW_E)
    # the pair table: the most distinct pairs logged within any 1 s window (60 frames at the crowd's ~60 fps), plus the table drops
    pairs_by_frame = {f: {(a, b) for a, b, _ in rows} for f, rows in e_rows.items()}
    max_pairs = 0
    fs = sorted(pairs_by_frame)
    j = 0
    window = defaultdict(int)
    for i, f in enumerate(fs):
        for p in pairs_by_frame[f]:
            window[p] += 1
        while fs[j] < f - 60:
            for p in pairs_by_frame[fs[j]]:
                window[p] -= 1
                if not window[p]:
                    del window[p]
            j += 1
        max_pairs = max(max_pairs, len(window))
    # the warn watch: capital subjects hit within the last 20 s (1,200 frames)
    max_watch = 0
    last = {}
    for fr, ship in sorted(warn_subjects):
        last[ship] = fr
        max_watch = max(max_watch, sum(1 for s, t in last.items() if fr - t <= 1200))
    print('source %s frames %d..%d' % (path, LO, HI))
    print('logged: E frames %d rows %d, at the old cap of %d: %d frames; gate_rows_dropped in the window %d (spread over capped frames %.0f, '
          'against the pair table %d)' % (len(e_rows), sum(len(v) for v in e_rows.values()), OLD_E,
                                           sum(1 for v in e_rows.values() if len(v) >= OLD_E), total_dropped, sum(extra.values()), table_drops))
    print('E ring %d per frame, the retry model: worst frame %.1f rows (logged + every missing pair of its interval at once) -> drops %d'
          % (NEW_E, retry_max_frame, int(max(0.0, retry_max_frame - NEW_E))))
    print('E ring %d per frame, the naive bound (every drop a row of its own): key off max %.1f rows/frame, drops %.0f; key on (member pairs '
          'to ship_gate_ctl_pair) max %.1f, drops %.0f' % (NEW_E, max_off, drops_e_off, max_on, drops_e_on))
    print('E pair table %d: the most distinct pairs logged in 1 s %d + the most missing pairs per interval %.1f -> drops %d'
          % (NEW_TABLE, max_pairs, retry_max_pairs, int(max(0.0, max_pairs + retry_max_pairs - NEW_TABLE))))
    print('ship_gate_ctl_pair ring %d per frame: max demand %.1f rows/frame (bound: the capital-capital E rows), drops %d'
          % (CTL_PAIR, max_pair, int(max(0.0, max_pair - CTL_PAIR))))
    print('warn watch %d slots: the most capital subjects hit within 20 s %d -> drops %d' % (WATCH, max_watch, max(0, max_watch - WATCH)))
    ok = retry_max_frame <= NEW_E and max_pairs + retry_max_pairs <= NEW_TABLE and max_pair <= CTL_PAIR and max_watch <= WATCH
    print('RESULT %s (gate_rows_dropped 0 at the new sizes under the retry model; the naive bound %s)'
          % ('PASS' if ok else 'FAIL', 'also 0' if drops_e_on == 0 and drops_e_off == 0 else 'not 0 (it counts every retry as a row)'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3/x3m.log'))
