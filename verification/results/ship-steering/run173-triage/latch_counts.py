"""Run173: amendment 16's slow join / un-latch and amendment 15's aim-at-A fallback in the flown log (stage B deletion, item 1).
Usage: python3 -I latch_counts.py x3m.log > latch_counts.txt. Prints the latch row events and the last summary's gate_capital_* counters."""
import collections
import re
import sys

events = collections.Counter()
last = None
with open(sys.argv[1], 'rb') as f:
    for raw in f:
        if raw.startswith(b'ship_gate_capital_latch '):
            m = re.search(rb'event=([a-z_]+)', raw)
            events[m.group(1).decode() if m else '?'] += 1
        elif raw.startswith(b'ship_steering_summary '):
            last = raw
print('latch_rows', sum(events.values()), ' '.join(f'{k}={v}' for k, v in sorted(events.items())))
want = ('gate_capital_join', 'gate_capital_slow_join', 'gate_capital_unlatch', 'gate_capital_fallback_pred', 'gate_capital_fallback_past_x')
fields = dict(re.findall(r'(\w+)=(\S+)', last.decode('ascii', 'replace'))) if last else {}
print('last_summary', ' '.join(f'{k}={fields.get(k, "na")}' for k in want))
