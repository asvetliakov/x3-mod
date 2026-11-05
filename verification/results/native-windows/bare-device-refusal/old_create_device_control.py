"""Negative control for test_capture_device_creation: the current host fixture compiled against create_device of a
given revision (default HEAD) must fail when that revision predates the bare-route refusal.
Usage: python3 old_create_device_control.py [REV]   (2026-11-05 against 481e610c6: failures=15 of 271 checks)."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[4]
sys.path[:0] = [str(ROOT / 'verification/probe'), str(ROOT / 'verification/analysis'), str(ROOT)]
from verification.analysis.test_capture_bloom_lifetime import extract_function
from source_text import source_text
rev = sys.argv[1] if len(sys.argv) > 1 else 'HEAD'
old = subprocess.run(['git', '-C', str(ROOT), 'show', f'{rev}:src/proxy/capture.cpp'], capture_output=True, text=True, check=True).stdout
d = Path(tempfile.mkdtemp())
(d / 'old.cpp').write_text(old)
cap, own = source_text(d / 'old.cpp'), source_text(ROOT / 'src/ownership/d3d9_ownership.cpp')
cs = ('HRESULT WINAPI create_device(IDirect3D9* d,UINT adapter,D3DDEVTYPE type,HWND window,DWORD flags,'
      'D3DPRESENT_PARAMETERS* p,IDirect3DDevice9** out)')
os_ = ('HRESULT create_device(Factory* node, UINT adapter, D3DDEVTYPE type, HWND window,\n'
       '                      DWORD flags, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out)')
(d / 'capture_create_device_under_test_inc.h').write_text(extract_function(cap, cs))
(d / 'ownership_create_device_under_test_inc.h').write_text(extract_function(own[own.rindex(os_):], os_))
b = subprocess.run(['c++', '-std=c++17', '-O2', '-w', '-pthread', '-I', str(d),
                    str(ROOT / 'verification/probe/capture_device_creation_fixture.cpp'), '-o', str(d / 'x')],
                   capture_output=True, text=True)
print('build', b.returncode)
r = subprocess.run([str(d / 'x')], capture_output=True, text=True)
print('rc', r.returncode, r.stdout.strip())
