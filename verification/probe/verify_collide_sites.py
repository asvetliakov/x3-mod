#!/usr/bin/env python3
"""Read-only qualification of the two sector-collide box-cull sites.

src/proxy/collide_box_cull.cpp claims the six bytes `mov ecx,[ebx+0x70];
mov edx,[ecx+0x30]` at 0x0045d58e (P1, the all-pairs loop of 0x0045d250) and
`mov eax,[edi+0x70]; mov ecx,[eax+0x30]` at 0x0045cc7c (P2, the swept scan of
0x0045cab0), docs/reverse-engineering/sector-collide.md sections 6 and 10.
This checks the installed X3AP.exe on the host: exact identity, every byte
window of collide_box_cull_core.h, that each site is two whole decoded
instructions, that the first instruction after each site writes EFLAGS and the
first x87 instruction comes later, that no direct branch lands inside either
displaced span, that the inbound branches are exactly the documented ones,
that the compare/reject/continue instructions decode to the documented
targets, the constants 0x1028f / 0x30000 / 0x3d090 / 0x8200404 / 0x20000, the
single `ret 4`, the three jump tables, the two helpers and the four calls to
them, that the claim windows are disjoint from every other site the proxy
patches, and that the core header carries the same constants. Also the stub
encoders (twins of the C++ ones) and the log-line parsers. No Wine, no game.
"""
import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
from pathlib import Path

import verify_chase_aim_sites as common

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'src/proxy/collide_box_cull_core.h'
DEFAULT_EXE = common.DEFAULT_EXE
P1_FUNCTION, P2_FUNCTION = (0x45d250, 0x45e0b8), (0x45cab0, 0x45cf57)
P1_SITE, P1_NEXT, P1_COMPARE, P1_REJECT, P1_CONTINUE = 0x45d58e, 0x45d594, 0x45d5e7, 0x45d6cc, 0x45df90
P2_SITE, P2_NEXT, P2_COMPARE, P2_CONTINUE = 0x45cc7c, 0x45cc82, 0x45cce6, 0x45ce07
P1_SOURCES, P2_SOURCES = [0x45d516], [0x45cc5e, 0x45cc70]
P1_SITE_WINDOW = bytes.fromhex('8b4b70 8b5130 8b4670 2b5030 83c130 83c030 89542428 db442428'.replace(' ', ''))
P1_COMPARE_WINDOW = bytes.fromhex('8bc8 894c241c 8b442424 ba8f020100 f7ea 0500800000 83d200 0facd010 3bc8 0f8fc0000000'.replace(' ', ''))
P1_REJECT_WINDOW = bytes.fromhex('0fb74348 bf07000000 663bc7 740a 66397e48 0f85ac080000'.replace(' ', ''))
P1_CONTINUE_WINDOW = bytes.fromhex('8b74243c 833e00 89742414'.replace(' ', ''))
P2_SITE_WINDOW = bytes.fromhex('8b4770 8b4830 8b7370 2b4e30 8b5034 2b5634 83c030 8b4008 2b4638 894c2430 db442430'.replace(' ', ''))
P2_COMPARE_WINDOW = bytes.fromhex('8b8fa4000000 034c2420 3bc1 0f8f0f010000'.replace(' ', ''))
P2_CONTINUE_WINDOW = bytes.fromhex('8b3f 833f00 897c2424'.replace(' ', ''))
SQRT_HELPER_VA, FTOL_HELPER_VA = 0x412440, 0x52b5d0
SQRT_HELPER = bytes.fromhex('558bec83e4f8d94508d9fa8be55dc3')
FTOL_HELPER = bytes.fromhex('833dec19660000742d558bec83ec0883e4f8dd1c24f20f2c0424c9c3')
CALLS = {0x45d5da: SQRT_HELPER_VA, 0x45d5e2: FTOL_HELPER_VA, 0x45ccd9: SQRT_HELPER_VA, 0x45cce1: FTOL_HELPER_VA}
RET_VA = 0x45e0b5
AXIS_CAP, MARGIN, STUB_CAPACITY = 0x40000000, 64, 160
P1_STUB_COUNTED, P1_STUB_PLAIN, P2_STUB_COUNTED, P2_STUB_PLAIN = 139, 127, 117, 105
INSTALL_RE = re.compile(r'\bcollide_box_cull requested=(?P<requested>[01]) patched=(?P<patched>[01]) reason=(?P<reason>\S+) p1_site=0x(?P<p1_site>[0-9a-f]{8}) '
                        r'p2_site=0x(?P<p2_site>[0-9a-f]{8}) write_p1=(?P<write_p1>none|atomic|plain) write_p2=(?P<write_p2>none|atomic|plain) '
                        r'stub_p1=0x(?P<stub_p1>[0-9a-f]{8}) stub_p2=0x(?P<stub_p2>[0-9a-f]{8}) counters=(?P<counters>[01]) enabled=(?P<enabled>[01])')
FRAME_RE = re.compile(r'\bcollide_census_frame device=(?P<device>\d+) frame=(?P<frame>\d+) p1_pairs=(?P<p1_pairs>\d+) p1_rejected=(?P<p1_rejected>\d+) '
                      r'p2_cands=(?P<p2_cands>\d+) p2_rejected=(?P<p2_rejected>\d+) counters=(?P<counters>[01]) enabled=(?P<enabled>[01])')
COUNTERS = ('p1_pairs', 'p1_rejected', 'p2_cands', 'p2_rejected')
WINDOW_RE = re.compile(r'\bcollide_census frame=(?P<frame>\d+) frames=(?P<frames>\d+) '
                       + ' '.join(rf'{c}_p50=(?P<{c}_p50>\d+) {c}_max=(?P<{c}_max>\d+) {c}_sum=(?P<{c}_sum>\d+)' for c in COUNTERS)
                       + r' counters=(?P<counters>[01]) enabled=(?P<enabled>[01])')


class _Writer:
    def __init__(self):
        self.out, self.fixes, self.labels = bytearray(), [], {}

    def raw(self, text):
        self.out += bytes.fromhex(text.replace(' ', ''))

    def dword(self, value):
        self.out += struct.pack('<I', value & 0xffffffff)

    def jcc(self, opcode, label):
        self.out.append(opcode)
        self.fixes.append((len(self.out), label))
        self.out.append(0)

    def label(self, name):
        self.labels[name] = len(self.out)

    def inc(self, slot):
        if slot:
            self.raw('ff05')
            self.dword(slot)

    def resolve(self):
        for at, label in self.fixes:
            rel = self.labels[label] - (at + 1)
            if not -128 <= rel <= 127:
                raise ValueError('short jump out of range')
            self.out[at] = rel & 0xff
        if len(self.out) > STUB_CAPACITY:
            raise ValueError('stub too long')
        return bytes(self.out)


def _check32(*values):
    for value in values:
        if not 0 <= value <= 0xffffffff:
            raise ValueError('addresses must be 32-bit VAs')


def encode_p1_stub(at, enabled, entered, rejected, next_slot, continue_target):
    """Twin of core::encode_p1_stub (see the listing there)."""
    _check32(at, enabled, entered, rejected, next_slot, continue_target)
    w = _Writer()
    w.raw('803d'); w.dword(enabled); w.raw('00'); w.jcc(0x74, 'continue')
    w.inc(entered)
    w.raw('66837b4807'); w.jcc(0x74, 'continue')
    w.raw('66837e4807'); w.jcc(0x74, 'continue')
    w.raw('8b442424 85c0'); w.jcc(0x78, 'continue')
    w.raw('8bd0 c1fa05 03c2'); w.jcc(0x70, 'continue')
    w.raw('83c040'); w.jcc(0x70, 'continue')
    w.raw('8b4b70 8b5670 8b7930 2b7a30 85ff 7902 f7df 50')
    w.raw('8b4134 2b4234 85c0 7902 f7d8 3bf8 0f42f8')
    w.raw('8b4138 2b4238 85c0 7902 f7d8 3bf8 0f42f8 58')
    w.raw('81ff'); w.dword(AXIS_CAP); w.jcc(0x73, 'continue')
    w.raw('3bf8'); w.jcc(0x7f, 'reject')
    w.label('continue'); w.raw('ff25'); w.dword(next_slot)
    w.label('reject'); w.inc(rejected)
    w.raw('bf07000000 e9'); w.dword(continue_target - (at + len(w.out) + 4))
    return w.resolve()


def encode_p2_stub(at, enabled, entered, rejected, next_slot, continue_target):
    """Twin of core::encode_p2_stub."""
    _check32(at, enabled, entered, rejected, next_slot, continue_target)
    w = _Writer()
    w.raw('803d'); w.dword(enabled); w.raw('00'); w.jcc(0x74, 'continue')
    w.inc(entered)
    w.raw('8b8fa4000000 034c2420'); w.jcc(0x70, 'continue')
    w.raw('83c140'); w.jcc(0x70, 'continue')
    w.raw('8b4770 8b5370 8b7030 2b7230 85f6 7902 f7de 51')
    w.raw('8b4834 2b4a34 85c9 7902 f7d9 3bf1 0f42f1')
    w.raw('8b4838 2b4a38 85c9 7902 f7d9 3bf1 0f42f1 59')
    w.raw('81fe'); w.dword(AXIS_CAP); w.jcc(0x73, 'continue')
    w.raw('3bf1'); w.jcc(0x7f, 'reject')
    w.label('continue'); w.raw('ff25'); w.dword(next_slot)
    w.label('reject'); w.inc(rejected)
    w.raw('8bf2 e9'); w.dword(continue_target - (at + len(w.out) + 4))
    return w.resolve()


def parse_install_line(line):
    match = INSTALL_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    return {k: (v == '1' if k in ('requested', 'patched', 'counters', 'enabled') else int(v, 16) if k.endswith('_site') or k.startswith('stub_') else v)
            for k, v in row.items()}


def _counts(match):
    row = {k: int(v) for k, v in match.groupdict().items()}
    row['counters'], row['enabled'] = bool(row['counters']), bool(row['enabled'])
    return row


def parse_frame_line(line):
    """One `collide_census_frame` line -> dict with the consistency bound, or None."""
    match = FRAME_RE.search(line)
    if not match:
        return None
    row = _counts(match)
    row['bounded'] = row['p1_rejected'] <= row['p1_pairs'] and row['p2_rejected'] <= row['p2_cands']
    return row


def parse_window_line(line):
    """One `collide_census` window line -> dict, or None."""
    match = WINDOW_RE.search(line)
    if not match:
        return None
    row = _counts(match)
    row['bounded'] = all(row[f'{c}_p50'] <= row[f'{c}_max'] <= row[f'{c}_sum'] or row[f'{c}_sum'] == 0 for c in COUNTERS) \
        and row['p1_rejected_sum'] <= row['p1_pairs_sum'] and row['p2_rejected_sum'] <= row['p2_cands_sum']
    return row


def source_constants(text):
    def value(name):
        match = re.search(rf'\b{name}\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*[;,]', text)
        return int(match.group(1), 0) if match else None

    def array(name):
        match = re.search(rf'\b{name}\[\w+\]\s*=\s*\{{([^}}]*)\}}', text)
        return bytes(int(b, 0) for b in re.findall(r'0x[0-9a-fA-F]{2}', match.group(1))) if match else b''
    names = ('p1_function_va', 'p1_function_end_va', 'p1_site_va', 'p1_next_va', 'p1_compare_va', 'p1_reject_va', 'p1_continue_va', 'p1_sqrt_call_va', 'p1_ftol_call_va',
             'p1_compare_offset', 'p1_reject_offset', 'p1_continue_offset', 'site_length', 'p1_radius_sum_slot',
             'p2_function_va', 'p2_function_end_va', 'p2_site_va', 'p2_next_va', 'p2_compare_va', 'p2_continue_va', 'p2_sqrt_call_va', 'p2_ftol_call_va',
             'p2_compare_offset', 'p2_continue_offset', 'p2_sweep_slot', 'sqrt_helper_va', 'ftol_helper_va', 'class_offset', 'physics_offset', 'radius_offset',
             'position_offset', 'class_gate', 'ret_pop', 'axis_cap', 'margin', 'stub_capacity', 'p1_stub_length_counted', 'p1_stub_length_plain',
             'p2_stub_length_counted', 'p2_stub_length_plain', 'window_frames')
    arrays = ('p1_site_window', 'p1_compare_window', 'p1_reject_window', 'p1_continue_window', 'p1_site', 'p2_site_window', 'p2_compare_window',
              'p2_continue_window', 'p2_site', 'sqrt_helper', 'ftol_helper')
    return {n: value(n) for n in names} | {n: array(n) for n in arrays}


EXPECTED_CONSTANTS = {
    'p1_function_va': P1_FUNCTION[0], 'p1_function_end_va': P1_FUNCTION[1], 'p1_site_va': P1_SITE, 'p1_next_va': P1_NEXT, 'p1_compare_va': P1_COMPARE,
    'p1_reject_va': P1_REJECT, 'p1_continue_va': P1_CONTINUE, 'p1_sqrt_call_va': 0x45d5da, 'p1_ftol_call_va': 0x45d5e2,
    'p1_compare_offset': P1_COMPARE - P1_SITE, 'p1_reject_offset': P1_REJECT - P1_SITE, 'p1_continue_offset': P1_CONTINUE - P1_SITE, 'site_length': 6,
    'p1_radius_sum_slot': 0x24, 'p2_function_va': P2_FUNCTION[0], 'p2_function_end_va': P2_FUNCTION[1], 'p2_site_va': P2_SITE, 'p2_next_va': P2_NEXT,
    'p2_compare_va': P2_COMPARE, 'p2_continue_va': P2_CONTINUE, 'p2_sqrt_call_va': 0x45ccd9, 'p2_ftol_call_va': 0x45cce1,
    'p2_compare_offset': P2_COMPARE - P2_SITE, 'p2_continue_offset': P2_CONTINUE - P2_SITE, 'p2_sweep_slot': 0x20,
    'sqrt_helper_va': SQRT_HELPER_VA, 'ftol_helper_va': FTOL_HELPER_VA, 'class_offset': 0x48, 'physics_offset': 0x70, 'radius_offset': 0xa4,
    'position_offset': 0x30, 'class_gate': 7, 'ret_pop': 4, 'axis_cap': AXIS_CAP, 'margin': MARGIN, 'stub_capacity': STUB_CAPACITY,
    'p1_stub_length_counted': P1_STUB_COUNTED, 'p1_stub_length_plain': P1_STUB_PLAIN, 'p2_stub_length_counted': P2_STUB_COUNTED,
    'p2_stub_length_plain': P2_STUB_PLAIN, 'window_frames': 300,
    'p1_site_window': P1_SITE_WINDOW, 'p1_compare_window': P1_COMPARE_WINDOW, 'p1_reject_window': P1_REJECT_WINDOW, 'p1_continue_window': P1_CONTINUE_WINDOW,
    'p1_site': P1_SITE_WINDOW[:6], 'p2_site_window': P2_SITE_WINDOW, 'p2_compare_window': P2_COMPARE_WINDOW, 'p2_continue_window': P2_CONTINUE_WINDOW,
    'p2_site': P2_SITE_WINDOW[:6], 'sqrt_helper': SQRT_HELPER, 'ftol_helper': FTOL_HELPER}

_SPEC_RE = re.compile(r'\{\s*"([a-z_0-9]+)"\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*\{([^}]*)\}\s*,\s*(\d+)')
_HOOK_RE = re.compile(r"\bH(?:ookSpec)?\(\s*'([a-z_0-9]+)'\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*bytes\.fromhex\('([0-9a-fA-F]+)'\)")
_CORE_SITE_RE = re.compile(r'\b(\w*site_va)\s*=\s*(0x[0-9a-fA-F]+)')
OWN_NAMES = ('collide_box_cull_p1', 'collide_box_cull_p2')


def other_claims(root=ROOT):
    """(name, address, length) of every other engine site the proxy patches: SiteSpec initialisers and *site_va constants
    under src/proxy, HookSpec tables of the chase verifiers."""
    claims = {}
    for path in sorted((root / 'src/proxy').glob('*')):
        if path.suffix not in ('.h', '.cpp') or path.name.startswith('collide_box_cull'):
            continue
        text = path.read_text(errors='replace')
        for name, address, _, length in _SPEC_RE.findall(text):
            claims[(name, int(address, 16))] = int(length)
        for name, address in _CORE_SITE_RE.findall(text):
            claims[(f'{path.stem}:{name}', int(address, 16))] = 8
    for path in sorted((root / 'verification/probe').glob('verify_chase_*.py')):
        for name, address, raw in _HOOK_RE.findall(path.read_text()):
            claims[(name, int(address, 16))] = len(raw) // 2
    return sorted((name, address, length) for (name, address), length in claims.items() if name not in OWN_NAMES)


def overlaps(claims):
    # The tail copies only the six displaced bytes, but the whole compared windows are kept clear of other patches.
    own = [(P1_SITE, P1_SITE + len(P1_SITE_WINDOW)), (P1_COMPARE, P1_COMPARE + len(P1_COMPARE_WINDOW)), (P1_REJECT, P1_REJECT + len(P1_REJECT_WINDOW)),
           (P1_CONTINUE, P1_CONTINUE + len(P1_CONTINUE_WINDOW)), (P2_SITE, P2_SITE + len(P2_SITE_WINDOW)), (P2_COMPARE, P2_COMPARE + len(P2_COMPARE_WINDOW)),
           (P2_CONTINUE, P2_CONTINUE + len(P2_CONTINUE_WINDOW)), (SQRT_HELPER_VA, SQRT_HELPER_VA + len(SQRT_HELPER)), (FTOL_HELPER_VA, FTOL_HELPER_VA + len(FTOL_HELPER))]
    return [(name, hex(address)) for name, address, length in claims for lo, hi in own if address < hi and lo < address + length]


def decode(exe):
    tool = shutil.which(common.OBJDUMP)
    if not tool:
        raise RuntimeError(f'{common.OBJDUMP} not found')
    decoded = {}
    for bounds in (P1_FUNCTION, P2_FUNCTION):
        run = subprocess.run([tool, '-d', '-Mintel', '--insn-width=16', f'--start-address={bounds[0]:#x}', f'--stop-address={bounds[1]:#x}', str(exe)],
                             check=True, capture_output=True, text=True, timeout=60)
        decoded[bounds] = common.parse_objdump(run.stdout, *bounds)
    return decoded


def _x87(i):
    return i.mnemonic.startswith('f')


def _writes_flags(i):
    return i.mnemonic in ('sub', 'add', 'cmp', 'test', 'and', 'or', 'xor', 'neg', 'inc', 'dec')


def _site_checks(prefix, instructions, site, next_va, window, sources):
    by_va = {i.va: i for i in instructions}
    branches = [(i.va, t) for i in instructions for t in [common._is_direct_control(i)] if t is not None]
    first, second, after = by_va.get(site), by_va.get(site + 3), by_va.get(next_va)
    ordered = [i for i in instructions if i.va >= site]
    first_x87 = next((i.va for i in ordered if _x87(i)), None)
    first_flags = next((i.va for i in ordered[2:] if _writes_flags(i)), None)
    reads_flags_before = [i.va for i in ordered[2:] if first_flags and i.va < first_flags and (i.mnemonic.startswith('j') and i.mnemonic != 'jmp' or i.mnemonic.startswith(('set', 'cmov', 'adc', 'sbb')))]
    return {
        f'{prefix}_site_two_whole_movs': first is not None and second is not None and first.mnemonic == 'mov' and second.mnemonic == 'mov'
                                         and first.raw + second.raw == window[:6] and second.end == next_va and after is not None,
        f'{prefix}_no_interior_branch': [(a, t) for a, t in branches if site < t < site + 6] == [],
        f'{prefix}_sources': sorted(a for a, t in branches if t == site) == sources,
        # The displaced span sits before the first x87 push, and EFLAGS are rewritten before anything reads them.
        f'{prefix}_x87_after_site': first_x87 is not None and first_x87 >= site + len(window) - 4,
        f'{prefix}_flags_dead': first_flags is not None and reads_flags_before == [],
    }


def inspect(data, decoded, core_text, claims):
    image = common.Image(data)
    p1, p2 = decoded[P1_FUNCTION], decoded[P2_FUNCTION]
    by1, by2 = {i.va: i for i in p1}, {i.va: i for i in p2}
    target = lambda table, va: common._is_direct_control(table[va]) if va in table else None
    rets = [i for i in p1 if i.mnemonic == 'ret']
    raws = b''.join(i.raw for i in p1)

    def table(va, count):
        return [struct.unpack('<I', image.read(va + 4 * k, 4))[0] for k in range(count)]
    constants = source_constants(core_text)
    checks = {
        'exe_identity': hashlib.sha256(data).hexdigest() == common.EXPECTED_SHA256 and len(data) == common.EXPECTED_SIZE,
        'preferred_base': image.image_base == common.IMAGE_BASE,
        'p1_windows': image.read(P1_SITE, len(P1_SITE_WINDOW)) == P1_SITE_WINDOW and image.read(P1_COMPARE, len(P1_COMPARE_WINDOW)) == P1_COMPARE_WINDOW
                      and image.read(P1_REJECT, len(P1_REJECT_WINDOW)) == P1_REJECT_WINDOW and image.read(P1_CONTINUE, len(P1_CONTINUE_WINDOW)) == P1_CONTINUE_WINDOW,
        'p2_windows': image.read(P2_SITE, len(P2_SITE_WINDOW)) == P2_SITE_WINDOW and image.read(P2_COMPARE, len(P2_COMPARE_WINDOW)) == P2_COMPARE_WINDOW
                      and image.read(P2_CONTINUE, len(P2_CONTINUE_WINDOW)) == P2_CONTINUE_WINDOW,
        **_site_checks('p1', p1, P1_SITE, P1_NEXT, P1_SITE_WINDOW, P1_SOURCES),
        **_site_checks('p2', p2, P2_SITE, P2_NEXT, P2_SITE_WINDOW, P2_SOURCES),
        # The compare the stub anticipates and where its reject path leads.
        'p1_compare_jg_reject': target(by1, 0x45d606) == P1_REJECT and by1[0x45d606].mnemonic == 'jg',
        'p1_reject_falls_to_continue': target(by1, 0x45d6de) == P1_CONTINUE and by1[0x45d6de].mnemonic == 'jne' and target(by1, 0x45d6d8) == 0x45d6e4,
        'p1_class7_box_rejects_to_continue': [target(by1, va) for va in (0x45d553, 0x45d570, 0x45d588)] == [P1_CONTINUE] * 3,
        'p1_continue_whole_instructions': all(va in by1 for va in (P1_CONTINUE, 0x45df94, 0x45df97, 0x45df9b)) and target(by1, 0x45df9b) == 0x45d410,
        'p2_compare_jg_continue': target(by2, 0x45ccf2) == P2_CONTINUE and by2[0x45ccf2].mnemonic == 'jg',
        'p2_continue_whole_instructions': all(va in by2 for va in (P2_CONTINUE, 0x45ce09, 0x45ce0c, 0x45ce10)) and target(by2, 0x45ce10) == 0x45cc10,
        'p2_esi_written_after_site': P2_NEXT in by2 and by2[P2_NEXT].raw == bytes.fromhex('8b7370'),
        'calls_to_helpers': all(target(by1 if va > P1_FUNCTION[0] else by2, va) == callee for va, callee in CALLS.items()),
        'helper_bytes': image.read(SQRT_HELPER_VA, len(SQRT_HELPER)) == SQRT_HELPER and image.read(FTOL_HELPER_VA, len(FTOL_HELPER)) == FTOL_HELPER,
        'constants_present': all(struct.pack('<I', c) in raws for c in (0x1028f, 0x30000, 0x3d090, 0x8200404, 0x20000)),
        'single_ret_4': len(rets) == 1 and rets[0].va == RET_VA and rets[0].raw == bytes.fromhex('c20400') and rets[0].end == P1_FUNCTION[1],
        'jump_tables': table(0x45e0b8, 2) == [0x45dfd9, 0x45d339] and table(0x45e0dc, 4) == [0x45d3b1, 0x45dfad, 0x45d3b6, 0x45d3c0]
                       and table(0x45e108, 2) == [0x45dfad, 0x45d3e6],
        'claims_disjoint': overlaps(claims) == [] and len(claims) >= 40,
        'source_constants': constants == EXPECTED_CONSTANTS,
        'encoders': [len(encode_p1_stub(0x10000000, 0x20000000, a, b, 0x10000090, P1_CONTINUE)) for a, b in ((1, 2), (0, 0))] == [P1_STUB_COUNTED, P1_STUB_PLAIN]
                    and [len(encode_p2_stub(0x10000000, 0x20000000, a, b, 0x10000090, P2_CONTINUE)) for a, b in ((1, 2), (0, 0))] == [P2_STUB_COUNTED, P2_STUB_PLAIN],
    }
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks, 'p1_site': hex(P1_SITE), 'p2_site': hex(P2_SITE),
            'other_claims_checked': len(claims), 'overlaps': overlaps(claims), 'jump_table_0x45e108': [hex(v) for v in table(0x45e108, 2)],
            'instructions': {'p1': len(p1), 'p2': len(p2)}, 'exe_sha256': hashlib.sha256(data).hexdigest()}


def verify(exe=DEFAULT_EXE, core=CORE):
    data = Path(exe).read_bytes()
    try:
        return inspect(data, decode(exe), Path(core).read_text(), other_claims())
    except (ValueError, OSError, KeyError, subprocess.SubprocessError, RuntimeError) as error:
        return {'result': 'FAIL', 'checks': {'decode': False}, 'error': repr(error)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE)
    parser.add_argument('--core', type=Path, default=CORE)
    args = parser.parse_args()
    report = verify(args.exe, args.core)
    print(json.dumps(report, indent=2, sort_keys=True))
    raise SystemExit(report['result'] != 'PASS')
