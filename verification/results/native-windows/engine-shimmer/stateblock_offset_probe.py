"""Builds stateblock_offset_probe.cpp (MinGW i686) and runs it on the native Windows host through WSL interop
(verification/probe/native_host.py: the system d3d9.dll, no Wine, never the game). Writes stateblock_offset_probe.txt
beside this script: a `HOST` line (native_host.identity(): Windows version, adapter, driver, d3d9.dll provenance)
followed by the probe's rows. Usage, from the checkout, under the runner lock that Wine and native fixture runs share:
  python3 verification/probe/wine_lock.py python3 verification/results/native-windows/engine-shimmer/stateblock_offset_probe.py
"""
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import native_host  # noqa: E402

if native_host.missing():
    raise SystemExit('native host unavailable: ' + '; '.join(native_host.missing()))
if native_host.game_running_windows():
    raise SystemExit('X3 is running on the Windows side; postpone the probe')
build = ROOT / 'verification/probe/build/stateblock-offset-probe'
build.mkdir(parents=True, exist_ok=True)
exe = build / 'stateblock_offset_probe.exe'
subprocess.run([native_host.COMPILER, '-std=c++17', '-O2', '-Wall', '-msse2', '-mfpmath=sse', '-mstackrealign',
                '-mincoming-stack-boundary=2', '-static', '-static-libgcc', '-static-libstdc++',
                str(HERE / 'stateblock_offset_probe.cpp'), '-o', str(exe), '-ld3d9', '-luser32'], check=True)
done, text = native_host.run_native(exe, [], build, 120)
if done.returncode:
    raise SystemExit(f'probe exit {done.returncode}')
host = native_host.identity(text)
(HERE / 'stateblock_offset_probe.txt').write_text('HOST ' + json.dumps(host, sort_keys=True) + '\n' + text)
print(json.dumps({'exit': done.returncode, 'rows': len(text.splitlines()), 'adapter': host['adapter'].get('description'),
                  'driver_version': host['adapter'].get('driver_version'),
                  'windows': (host.get('windows') or {}).get('version')}))
