# Runs `unittest test_ship_steering` unchanged, keeping the fixture's lane-cost rows (stdout tee via a subprocess.run wrapper).
import re, subprocess, sys, unittest
real = subprocess.run
rows = []
def run(*a, **k):
    r = real(*a, **k)
    out = r.stdout if isinstance(r.stdout, str) else ''
    rows.extend(re.findall(r'lane cost \(host\) (bench24 tf \d+):.*?(\d+) us third-worst.*?best of 3: lane (\d+) us', out or ''))
    return r
subprocess.run = run
res = unittest.main(module='test_ship_steering', exit=False, argv=['x']).result
print('OK' if res.wasSuccessful() else 'FAILED', '| ' + '; '.join('%s third(worst-of-3) %s best3 %s' % t for t in rows))
