"""Per mode of the native record: exit code, CHECK count, the fixture's last STEP row and the session log's exception
row (the run directories under verification/probe/build/ are local). Writes crash_rows.json beside this script."""
import glob
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
record = json.loads((Path(__file__).parent / 'summary.json').read_text())
out = {}
for mode, run in record['runs'].items():
    directory = ROOT / run['directory']
    stdout = (directory / 'stdout.txt').read_text(errors='replace').splitlines() if (directory / 'stdout.txt').exists() else []
    exception = [line.split('exception ', 1)[1][:160] for log in glob.glob(str(directory / 'x3-modern-captures/*.log'))
                 for line in open(log, errors='replace') if ' exception code=' in line or line.startswith('exception code=')]
    out[mode] = {'exit': run['exit'], 'checks': run['checks'], 'steps': [l for l in stdout if l.startswith('STEP')],
                 'identity_rows': sum(l.startswith(('ADAPTER', 'D3D9_MODULE', 'SEAM_MODULE')) for l in stdout),
                 'exception': exception[0] if exception else None}
(Path(__file__).parent / 'crash_rows.json').write_text(json.dumps(out, indent=1) + '\n')
for mode, row in out.items():
    print(mode, row['exit'], row['checks'], row['steps'], row['identity_rows'], (row['exception'] or '')[:90])
