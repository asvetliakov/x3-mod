import re, subprocess, sys
exe = sys.argv[1]
tag = sys.argv[2] if len(sys.argv) > 2 else ''
r = subprocess.run([exe], capture_output=True, text=True)
fails = [l for l in (r.stdout + r.stderr).splitlines() if 'FAIL' in l]
rows = []
for l in r.stdout.splitlines():
    m = re.match(r'lane cost \(host\) (\S+) tf (\d+): lane ([\d.]+) us mean, (\d+) us worst frame, (\d+) us third-worst', l)
    if m:
        b = re.search(r'best of 3: lane (\d+) us', l)
        rows.append('%s/tf%s mean %s worst %s third %s' % m.groups() + (' best3third %s' % b.group(1) if b else ''))
print(tag, 'exit', r.returncode, 'fail-lines', len(fails), '|', '; '.join(rows))
for f in fails[:5]:
    print('  ', f[:200])
