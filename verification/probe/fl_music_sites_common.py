"""Shared reader for the FL music family's site verifiers (verify_media_cue_sites_fl.py, verify_music_keep_sites_fl.py,
verify_music_alert_sites_fl.py): the X3FL.exe image with its HIGHLOW census, Capstone decoding, raw branch scans,
direct-caller scans, and the checked-in identity windows (src/engine/fl/music_windows_fl_inc.h) compared with the
windows regenerated from the exe (verification/results/fl-music/generate_windows.py). Read-only; no Wine, no game.
"""
import hashlib
import re
import struct
import sys
from pathlib import Path

import capstone

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
sys.path.insert(0, str(ROOT / 'verification/results/fl-music'))
from inspect_x3 import PE32  # noqa: E402
import generate_windows as gen  # noqa: E402

DEFAULT_EXE = Path('/mnt/c/X3FL/X3FL.exe')
AP_EXE = Path('/mnt/c/X3AP-orig/X3AP.exe')
FL_SHA256 = gen.FL_SHA256
BASE = 0x400000
INC = ROOT / 'src/engine/fl/music_windows_fl_inc.h'


class Image(gen.Image):
    def __init__(self, path):
        super().__init__(path)
        text = next(s for s in self.pe.sections if s['name'] == '.text')
        self.text = (text['rva'], text['rva'] + text['raw_size'], text['offset'])

    def insns(self, start, end):
        out = list(self.md.disasm(self.read(start, end - start), BASE + start))
        ok = bool(out) and out[-1].address + out[-1].size == BASE + end
        return out, ok

    def fixups_in(self, start, end):
        return sorted(f for f in self.fixups if start <= f < end)

    def imports(self):
        return self.pe.imports()


def raw_branch_hits(im, start, end):
    """Raw E8/E9/EB/7x/0F8x encodings in .text whose target lies strictly inside (start, end): [(source, target)]."""
    lo, hi, off = im.text
    data = im.data[off:off + (hi - lo)]
    hits = []
    for i in range(len(data) - 1):
        b, rva, target = data[i], lo + i, None
        if b in (0xe8, 0xe9) and i + 5 <= len(data):
            target = (rva + 5 + struct.unpack_from('<i', data, i + 1)[0]) & 0xffffffff
        elif b == 0xeb or 0x70 <= b <= 0x7f:
            target = rva + 2 + struct.unpack_from('<b', data, i + 1)[0]
        elif b == 0x0f and 0x80 <= data[i + 1] <= 0x8f and i + 6 <= len(data):
            target = (rva + 6 + struct.unpack_from('<i', data, i + 2)[0]) & 0xffffffff
        if target is not None and start < target < end:
            hits.append((rva, target))
    return hits


def direct_callers(im, target):
    """RVAs of every E8 in .text whose rel32 lands on RVA `target`."""
    lo, hi, off = im.text
    data = im.data[off:off + (hi - lo)]
    return [lo + i for i in range(len(data) - 5)
            if data[i] == 0xe8 and (lo + i + 5 + struct.unpack_from('<i', data, i + 1)[0]) & 0xffffffff == target]


def pointers_to(im, va):
    """HIGHLOW dwords holding `va` (a raw function-pointer reference)."""
    return sorted(f for f in im.fixups if im.u32(f) == va)


def declared_windows(table):
    """The checked-in table: [(name, rva, length, fnv, [(offset, target)])]."""
    text = INC.read_text()
    body = re.search(r'constexpr CodeWindow %s_windows\[\] = \{(.*?)\n\};' % table, text, re.S).group(1)
    fx = re.search(r'constexpr Fixup %s_fixups\[\] = \{(.*?)\n\};' % table, text, re.S).group(1)
    fixups = [(int(a, 16), int(b, 16)) for a, b in re.findall(r'\{0x([0-9a-f]+), 0x([0-9a-f]+)\}', fx)]
    out = []
    for name, rva, length, first, count, h in re.findall(
            r'\{"([a-z_0-9]+)", 0x([0-9a-f]+), 0x([0-9a-f]+), (\d+), (\d+), 0x([0-9a-f]+)\}', body):
        first, count = int(first), int(count)
        out.append((name, int(rva, 16), int(length, 16), int(h, 16), fixups[first:first + count]))
    return out


def check_windows(im, table, checks, report):
    """The header's windows equal the exe's (bytes hash, HIGHLOW census and targets, whole instructions)."""
    declared = declared_windows(table)
    regenerated = {name: (rva, length) for t, name, rva, length in gen.WINDOWS if t == table}
    hashes = {}
    ok_all = len(declared) == len(regenerated)
    for name, rva, length, h, fixups in declared:
        masked, fx, whole = gen.window(im, rva, length)
        same = (regenerated.get(name) == (rva, length) and gen.fnv1a(masked) == h and fx == fixups and whole)
        hashes[name] = '%08x' % h
        ok_all = ok_all and same
        if not same:
            report.setdefault('window_failures', []).append(name)
    checks[f'{table}_windows_match_exe'] = ok_all
    report[f'{table}_windows'] = hashes
    report[f'{table}_window_count'] = len(declared)
    report[f'{table}_fixup_count'] = sum(len(d[4]) for d in declared)
    return declared


def masked_matches(im, rva, length, fnv, fixups, base=BASE):
    """The production decision (code_window_fl.h matches) on a file image at `base`."""
    raw = bytearray(im.read(rva, length))
    for off, target in fixups:
        if struct.unpack_from('<I', raw, off)[0] != (base + target) & 0xffffffff:
            return False
        raw[off:off + 4] = b'\0\0\0\0'
    return gen.fnv1a(bytes(raw)) == fnv


def ap_refusal(table, checks, report, ap_sites):
    """Every FL window fails on the AP exe at the same RVAs, and the AP site VAs (`ap_sites`: name -> (va, bytes)) do
    not hold their AP bytes in the FL exe: the FL tables cannot admit the AP image, nor the AP sites the FL image."""
    if not AP_EXE.exists():
        report['ap_refusal'] = 'ap_exe_absent'
        checks['ap_addresses_refused'] = False
        return
    ap = Image(AP_EXE)
    fl = Image(DEFAULT_EXE)
    admitted = []
    for name, rva, length, h, fixups in declared_windows(table):
        try:
            if masked_matches(ap, rva, length, h, fixups):
                admitted.append(name)
        except (ValueError, struct.error):
            pass
    ap_bytes_in_fl = []
    for name, (va, expected) in ap_sites.items():
        try:
            if fl.read(va - BASE, len(expected)) == expected:
                ap_bytes_in_fl.append(name)
        except ValueError:
            pass
        if ap.read(va - BASE, len(expected)) != expected:
            ap_bytes_in_fl.append(name + '_not_in_ap')
    report['ap_windows_admitted'] = admitted
    report['ap_site_bytes_in_fl'] = ap_bytes_in_fl
    report['ap_sites_checked'] = len(ap_sites)
    checks['ap_addresses_refused'] = not admitted and not ap_bytes_in_fl


def span_report(im, start, end, decode_from=None):
    """Whole-instruction decode of [start, end) from `decode_from` (default start), its relocations, qword geometry
    of a five-byte jump at start, and every raw branch into its interior."""
    insns, ok = im.insns(decode_from if decode_from is not None else start, end)
    starts = [i.address - BASE for i in insns]
    return {
        'whole': ok and start in starts,
        'insns': [(i.address - BASE, i.mnemonic, i.op_str) for i in insns if i.address - BASE >= start],
        'fixups': im.fixups_in(start, end),
        'atomic_jump': (start & 7) <= 3,
        'interior_hits': [(hex(s), hex(t)) for s, t in raw_branch_hits(im, start, end)],
    }


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def finish(report, checks):
    report['checks'] = checks
    report['result'] = 'PASS' if all(checks.values()) else 'FAIL'
    report['failed'] = [k for k, v in checks.items() if not v]
    return report
