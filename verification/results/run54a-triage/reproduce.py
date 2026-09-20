#!/usr/bin/env python3
"""Read-only compact witness for Run54 crash triage bundles."""
from pathlib import Path
import hashlib, json, re, subprocess

ROOT = Path('/tmp')
RUNS = (195, 196, 197)
fault = re.compile(r'Unhandled page fault on read access to ([0-9A-F]+) at address ([0-9A-F]+) \(thread ([0-9A-F]+)\)', re.I)

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()

out = {'candidate': {}, 'runs': {}}
candidate = ROOT/'x3-run54-candidate/build/d3d9.dll'
with candidate.open('rb') as f:
    header = f.read(4096)
pe = int.from_bytes(header[0x3c:0x40], 'little')
assert header[pe:pe+4] == b'PE\0\0'
optional = pe + 24
assert int.from_bytes(header[optional:optional+2], 'little') == 0x10b
image_base = int.from_bytes(header[optional+28:optional+32], 'little')
image_size = int.from_bytes(header[optional+56:optional+60], 'little')

symbols_wanted = {
    '_x3m_voice_dmo_fallback_enter': 'voice_dmo_fallback',
    '_x3m_collide_sat_thunk': 'collide_sat',
    '_x3m_collide_memo_thunk': 'collide_memo',
}
symbols = {}
nm = 'i686-w64-mingw32-nm'
try:
    proc = subprocess.Popen([nm, '-n', str(candidate)], stdout=subprocess.PIPE, text=True)
except FileNotFoundError:
    proc = subprocess.Popen(['nm', '-n', str(candidate)], stdout=subprocess.PIPE, text=True)
assert proc.stdout is not None
for line in proc.stdout:
    fields = line.split()
    if len(fields) == 3 and fields[2] in symbols_wanted:
        symbols[symbols_wanted[fields[2]]] = int(fields[0], 16)
assert proc.wait() == 0 and set(symbols) == set(symbols_wanted.values())

out['candidate'] = {'path': str(candidate), 'sha256': digest(candidate), 'bytes': candidate.stat().st_size,
                    'pe_image_base': f'0x{image_base:08x}', 'pe_sizeofimage': f'0x{image_size:x}',
                    'symbols': {k: f'0x{v:08x}' for k, v in sorted(symbols.items())}}
for run in RUNS:
    d = ROOT/f'x3-bottleX3-run{run}'
    session = next(d.glob('session-*.log'))
    stderr = d/'launcher-stderr.log'
    text = stderr.read_text(errors='replace')
    fault_match = fault.search(text)
    qpc = media = owned = 0
    runtime = {}
    with session.open(errors='replace') as f:
        for line in f:
            qpc += line.count('qpc=')
            media += bool(re.match(r'^media_cue(?:_enter| frame|_window)', line))
            owned += line.startswith('media_owned_snapshot')
            if line.startswith('voice_dmo_fallback '):
                m = re.search(r'\benter=([0-9a-fA-F]+)', line)
                if m: runtime['voice_dmo_fallback'] = int(m.group(1), 16)
            elif line.startswith('collide_sat_sse2 '):
                m = re.search(r'\bhandler=0x([0-9a-fA-F]+)', line)
                if m: runtime['collide_sat'] = int(m.group(1), 16)
            elif line.startswith('collide_memo '):
                m = re.search(r'\bhandler=0x([0-9a-fA-F]+)', line)
                if m: runtime['collide_memo'] = int(m.group(1), 16)
    assert set(runtime) == set(symbols)
    anchors = []
    bases = set()
    for name in sorted(symbols):
        rva = symbols[name] - image_base
        base = runtime[name] - rva
        bases.add(base)
        anchors.append({'name': name, 'preferred': f'0x{symbols[name]:08x}',
                        'runtime': f'0x{runtime[name]:08x}', 'derived_base': f'0x{base:08x}'})
    assert len(bases) == 1
    loaded_base = bases.pop()
    record = {'session': session.name, 'session_sha256': digest(session), 'session_bytes': session.stat().st_size,
              'stderr_sha256': digest(stderr), 'gstreamer_critical': text.count('GStreamer-CRITICAL'),
              'qpc_fields': qpc, 'media_cue_records': media, 'media_owned_snapshot_records': owned}
    record['candidate_anchors'] = anchors
    record['candidate_loaded_base'] = f'0x{loaded_base:08x}'
    record['candidate_loaded_end'] = f'0x{loaded_base + image_size:08x}'
    if fault_match: record['fault'] = {'read': fault_match.group(1).lower(), 'pc': '0x'+fault_match.group(2).lower(), 'thread': '0x'+fault_match.group(3).lower()}
    out['runs'][str(run)] = record
frames = sorted(int(p.stem.rsplit('_', 1)[1]) for p in (ROOT/'x3-bottleX3-run197').glob('color_1_*.bgra8'))
out['run197_manual_color_frames'] = {'count': len(frames), 'first': frames[0], 'last': frames[-1]}
print(json.dumps(out, indent=2, sort_keys=True))
