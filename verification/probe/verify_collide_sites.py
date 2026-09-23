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

The narrow-phase census (src/proxy/collide_narrow_census.cpp, section 11.7)
adds three sites: the `call` at 0x0045d665 (-> 0x0048ac80) and at 0x0048a9a5
(-> 0x0047f1b0), redirected, and the first instruction of 0x004e2530,
displaced. Checked here: each is one whole instruction with the documented
bytes and target, the windows and callee bodies of
collide_narrow_census_core.h, that nowhere in the image a rel32 branch or an
abs32 pointer lands inside a displaced span (whole-image byte scan, a
superset of the real branches) and no decoded short jump does either, the
five inbound calls of 0x004e2530, the liveness the stubs rely on (EFLAGS dead
at both callee entries, at the return of site 5 and after site 7; EBX/ESI
still the pair at site 5; the callee of site 5 returns with a plain `ret`
and takes ECX/EAX as the pre-window loads them), and disjointness from every
other claim including the box cull's windows and cull_small_parts. Site 8
(section 12.6) displaces the three entry instructions of the leaf triangle
test 0x004e2190 (sole caller 0x004e25cd) under the same rules.

The SSE2 separating-axis replacement (src/proxy/collide_sat_sse2.cpp, section
12.8) redirects the `call 0x004e3280` at 0x004e25a3. Checked here: one whole
instruction, the sole reference to 0x004e3280 in the image, nothing entering
+1..+4, the windows, the pinned hash of the 1,582-byte body, that the body
writes none of ECX/EDX/ESI/EDI, calls only the `fabs` helper and returns with
plain `ret`s, that the x87 stack is empty at the site, EFLAGS dead at the
return, no XMM/MMX register anywhere in the descent, its leaf test or the body,
and disjointness from every other claim, the census windows included (both
options may be on). `sat_disjoint` is the Python twin of core::obb_disjoint.
"""
import argparse
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path

import exe_identity  # structure + anchors gate; hashes are INFO (docs/reverse-engineering/executable-identity.md)

import verify_chase_aim_sites as common

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'src/proxy/collide_box_cull_core.h'
NARROW_CORE = ROOT / 'src/proxy/collide_narrow_census_core.h'
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


def other_claims(root=ROOT, skip_prefix='collide_box_cull', own_names=None):
    """(name, address, length) of every other engine site the proxy patches: SiteSpec initialisers and *site_va constants
    under src/proxy, HookSpec tables of the chase verifiers."""
    claims = {}
    for path in sorted((root / 'src/proxy').glob('*')):
        if path.suffix not in ('.h', '.cpp') or path.name.startswith(skip_prefix):
            continue
        text = path.read_text(errors='replace')
        for name, address, _, length in _SPEC_RE.findall(text):
            claims[(name, int(address, 16))] = int(length)
        for name, address in _CORE_SITE_RE.findall(text):
            claims[(f'{path.stem}:{name}', int(address, 16))] = 8
    for path in sorted((root / 'verification/probe').glob('verify_chase_*.py')):
        for name, address, raw in _HOOK_RE.findall(path.read_text()):
            claims[(name, int(address, 16))] = len(raw) // 2
    return sorted((name, address, length) for (name, address), length in claims.items() if name not in (OWN_NAMES if own_names is None else own_names))


def overlaps(claims):
    # The tail copies only the six displaced bytes, but the whole compared windows are kept clear of other patches.
    own = [(P1_SITE, P1_SITE + len(P1_SITE_WINDOW)), (P1_COMPARE, P1_COMPARE + len(P1_COMPARE_WINDOW)), (P1_REJECT, P1_REJECT + len(P1_REJECT_WINDOW)),
           (P1_CONTINUE, P1_CONTINUE + len(P1_CONTINUE_WINDOW)), (P2_SITE, P2_SITE + len(P2_SITE_WINDOW)), (P2_COMPARE, P2_COMPARE + len(P2_COMPARE_WINDOW)),
           (P2_CONTINUE, P2_CONTINUE + len(P2_CONTINUE_WINDOW)), (SQRT_HELPER_VA, SQRT_HELPER_VA + len(SQRT_HELPER)), (FTOL_HELPER_VA, FTOL_HELPER_VA + len(FTOL_HELPER))]
    return [(name, hex(address)) for name, address, length in claims for lo, hi in own if address < hi and lo < address + length]


# ---- narrow-phase census: sites 5, 6, 7 (section 11.7) ----
N5_SITE, N5_TARGET, N5_RETURN = 0x45d665, 0x48ac80, 0x45d66a
N6_SITE, N6_TARGET = 0x48a9a5, 0x47f1b0
N7_SITE, N7_NEXT, N7_HITS, N7_MODE = 0x4e2530, 0x4e2535, 0x60854c, 0x596934
N5_CALLEE, N6_FUNCTION, N6_CALLEE_HEAD, N7_FUNCTION = (0x48ac80, 0x48ace7), (0x48a890, 0x48ac27), (0x47f1b0, 0x47f1c6), (0x4e2530, 0x4e2780)
N5_PRE_WINDOW, N5_POST_WINDOW = bytes.fromhex('518b4b70508b4670'), bytes.fromhex('83c40885c07e5b')
N6_PRE_WINDOW, N6_POST_WINDOW = bytes.fromhex('d9ee51d91c248bce6a016a02505733ff8bc3'), bytes.fromhex('83c41485c00f8491000000')
N7_WINDOW = bytes.fromhex('a14c85600083ec40833d346959000053555657740e85c07e0a33c05f5e5d5b83c440c3')
N5_CALLEE_BYTES = bytes.fromhex('5133d256899190010000899090010000578db19001000089918001000089918401000089918801000089918c0100008db890010000'
                                '5789908001000089908401000089908801000089908c0100008b54241856528b54241c525051e8b0fbffff83c4185f5e59c3')
N6_CALLEE_BYTES = bytes.fromhex('8b5424048b525c83ec6485d2568b7424708b765c7507')
N5_CALLEE_REL32 = 0x5c
N7_INBOUND = [0x4e264c, 0x4e269f, 0x4e2705, 0x4e2767, 0x4e2956]
N8_SITE, N8_NEXT, N8_CALLER, N8_FUNCTION = 0x4e2190, 0x4e2195, 0x4e25cd, (0x4e2190, 0x4e2530)
N8_WINDOW = bytes.fromhex('83ec3453578bf88b4644d94004')
NARROW_SPANS = {'n5': N5_SITE, 'n6': N6_SITE, 'n7': N7_SITE, 'n8': N8_SITE}
NARROW_STUB_CAPACITY, N5_STUB, N6_STUB, N7_STUB, N8_STUB = 320, 289, 11, 16, 16
NARROW_OWN_NAMES = ('collide_narrow_census_n7', 'collide_narrow_census_n8')
_SAVE = '9c 60 fc 81ec80000000 ' + ' '.join(f'0f11{0x44 | (i << 3):02x}24{i * 16:02x}' for i in range(8))
_RESTORE = ' '.join(f'0f10{0x44 | (i << 3):02x}24{i * 16:02x}' for i in range(8)) + ' 81c480000000 61 9d'
NARROW_SERIES = ('accepted', 'mesh_pairs', 'node_pairs', 'narrow_us', 'tri_tests')
NARROW_SUMS = ('recorded_sum', 'with_previous_sum', 'unchanged_sum', 'memo_would_hit_sum', 'memo_would_hit_permille', 'memo_visits_sum', 'memo_visits_permille',
               'memo_unsafe_sum', 'memo_visits_differ_sum', 'changed_pos_sum', 'changed_xform_sum', 'changed_saved_sum', 'ring_overflow', 'dropped', 'deferred',
               'nested', 'foreign', 'cross_thread_frames')
NARROW_WINDOW_RE = re.compile(r'\bcollide_narrow frame=(?P<frame>\d+) frames=(?P<frames>\d+) '
                              + ' '.join(rf'{c}_p50=(?P<{c}_p50>\d+) {c}_max=(?P<{c}_max>\d+) {c}_sum=(?P<{c}_sum>\d+)' for c in NARROW_SERIES)
                              + ' ' + ' '.join(rf'{c}=(?P<{c}>\d+)' for c in NARROW_SUMS) + r'\s*$')
_PAIR_HEX = ('a', 'b', 'flags40_a', 'flags44_a', 'flags40_b', 'flags44_b', 'node_flags_a', 'node_flags_b')
_PAIR_FIELDS = (('device', r'\d+'), ('frame', r'\d+'), ('rank', r'\d+'), ('of', r'\d+'), ('a', r'0x[0-9a-f]{8}'), ('b', r'0x[0-9a-f]{8}'), ('class_a', r'\d+'), ('class_b', r'\d+'),
                ('subtype_a', r'\d+'), ('subtype_b', r'\d+'), ('model_a', r'-?\d+'), ('model_b', r'-?\d+'), ('radius_a', r'-?\d+'), ('radius_b', r'-?\d+'),
                ('flags40_a', r'0x[0-9a-f]{8}'), ('flags44_a', r'0x[0-9a-f]{8}'), ('flags40_b', r'0x[0-9a-f]{8}'), ('flags44_b', r'0x[0-9a-f]{8}'),
                ('node_flags_a', r'0x[0-9a-f]{8}'), ('node_flags_b', r'0x[0-9a-f]{8}'), ('pos_a', r'-?\d+,-?\d+,-?\d+'), ('pos_b', r'-?\d+,-?\d+,-?\d+'),
                ('d_max', r'\d+'), ('r_sum', r'-?\d+'), ('visits', r'\d+'), ('mesh_pairs', r'\d+'), ('tri_tests', r'\d+'), ('us', r'\d+'), ('result', r'-?\d+'), ('contact', r'[01]'),
                ('previous', r'[01]'), ('same_pos', r'[01]'), ('same_xform', r'[01]'), ('same_saved', r'[01]'), ('unchanged', r'[01]'), ('memo_hit', r'[01]'),
                ('memo_unsafe', r'[01]'), ('visits_differ', r'[01]'))
NARROW_PAIR_RE = re.compile(r'\bcollide_narrow_pair ' + ' '.join(rf'{k}=(?P<{k}>{v})' for k, v in _PAIR_FIELDS) + r'\s*$')
NARROW_INSTALL_RE = re.compile(r'\bcollide_narrow_census requested=(?P<requested>[01]) patched=(?P<patched>[01]) reason=(?P<reason>\S+) n5_site=0x(?P<n5_site>[0-9a-f]{8}) '
                               r'n6_site=0x(?P<n6_site>[0-9a-f]{8}) n7_site=0x(?P<n7_site>[0-9a-f]{8}) write_n5=(?P<write_n5>none|atomic|plain) write_n6=(?P<write_n6>none|atomic|plain) '
                               r'write_n7=(?P<write_n7>none|atomic|plain) stub_n5=0x(?P<stub_n5>[0-9a-f]{8}) stub_n6=0x(?P<stub_n6>[0-9a-f]{8}) stub_n7=0x(?P<stub_n7>[0-9a-f]{8}) '
                               r'ring=(?P<ring>\d+) qpc_frequency=(?P<qpc_frequency>\d+) n8_site=0x(?P<n8_site>[0-9a-f]{8}) write_n8=(?P<write_n8>none|atomic|plain) stub_n8=0x(?P<stub_n8>[0-9a-f]{8})')


class _Flat:
    """Twin of the narrow core's StubWriter: bytes plus rel32 fields relative to the stub address."""
    def __init__(self, at):
        self.at, self.out = at, bytearray()

    def raw(self, text):
        self.out += bytes.fromhex(text.replace(' ', ''))

    def dword(self, value):
        self.out += struct.pack('<I', value & 0xffffffff)

    def rel32(self, opcode, target):
        self.raw(opcode)
        self.dword(target - (self.at + len(self.out) + 4))


def encode_n5_stub(at, return_va, target, pre_handler, post_handler, busy, nested, foreign):
    """Twin of core::encode_n5_stub (see the listing there)."""
    _check32(at, return_va, target, pre_handler, post_handler, busy, nested, foreign)
    w = _Flat(at)
    w.raw('813c24'); w.dword(return_va); w.raw('0f85'); fix_foreign = len(w.out); w.dword(0)
    w.raw('803d'); w.dword(busy); w.raw('00 0f85'); fix_nested = len(w.out); w.dword(0)
    w.raw('c605'); w.dword(busy); w.raw('01')
    w.raw(_SAVE); w.raw('56 53'); w.rel32('e8', pre_handler); w.raw('83c408'); w.raw(_RESTORE)
    w.raw('8d642404'); w.rel32('e8', target)
    w.raw(_SAVE); w.raw('50'); w.rel32('e8', post_handler); w.raw('83c404'); w.raw(_RESTORE)
    w.raw('c605'); w.dword(busy); w.raw('00'); w.rel32('e9', return_va)
    label_nested = len(w.out)
    w.raw('ff05'); w.dword(nested); w.rel32('e9', target)
    label_foreign = len(w.out)
    w.raw('ff05'); w.dword(foreign); w.rel32('e9', target)
    w.out[fix_nested:fix_nested + 4] = struct.pack('<I', label_nested - (fix_nested + 4))
    w.out[fix_foreign:fix_foreign + 4] = struct.pack('<I', label_foreign - (fix_foreign + 4))
    return bytes(w.out)


def encode_n6_stub(at, counter, target):
    _check32(at, counter, target)
    w = _Flat(at)
    w.raw('ff05'); w.dword(counter); w.rel32('e9', target)
    return bytes(w.out)


def encode_n7_stub(at, counter, hits, next_va):
    _check32(at, counter, hits, next_va)
    w = _Flat(at)
    w.raw('ff05'); w.dword(counter); w.raw('a1'); w.dword(hits); w.rel32('e9', next_va)
    return bytes(w.out)


def encode_n8_stub(at, counter, next_va):
    _check32(at, counter, next_va)
    w = _Flat(at)
    w.raw('ff05'); w.dword(counter); w.raw('83ec34 53 57'); w.rel32('e9', next_va)
    return bytes(w.out)


def parse_narrow_install_line(line):
    match = NARROW_INSTALL_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    return {k: (v == '1' if k in ('requested', 'patched') else int(v) if k in ('ring', 'qpc_frequency') else int(v, 16) if k.endswith('_site') or k.startswith('stub_') else v)
            for k, v in row.items()}


def parse_narrow_window_line(line):
    """One `collide_narrow` window line -> dict with the consistency bound, or None."""
    match = NARROW_WINDOW_RE.search(line)
    if not match:
        return None
    row = {k: int(v) for k, v in match.groupdict().items()}
    row['bounded'] = all(row[f'{c}_p50'] <= row[f'{c}_max'] <= row[f'{c}_sum'] or row[f'{c}_sum'] == 0 for c in NARROW_SERIES) \
        and row['memo_would_hit_sum'] <= row['unchanged_sum'] <= row['with_previous_sum'] <= row['recorded_sum'] <= row['accepted_sum'] \
        and row['recorded_sum'] + row['ring_overflow'] == row['accepted_sum'] and row['memo_visits_sum'] <= row['node_pairs_sum']
    return row


def parse_narrow_pair_line(line):
    """One `collide_narrow_pair` row -> dict, or None."""
    match = NARROW_PAIR_RE.search(line)
    if not match:
        return None
    row = {}
    for key, value in match.groupdict().items():
        row[key] = int(value, 16) if key in _PAIR_HEX else tuple(int(v) for v in value.split(',')) if key.startswith('pos_') else int(value)
    row['bounded'] = row['rank'] < row['of'] and row['contact'] == int(row['result'] > 0) and (not row['memo_hit'] or (row['unchanged'] and not row['contact'])) \
        and (not row['unchanged'] or (row['previous'] and row['same_pos'] and row['same_xform'] and row['same_saved'])) \
        and row['d_max'] == max(abs(((a - b + 2 ** 31) % 2 ** 32) - 2 ** 31) for a, b in zip(row['pos_a'], row['pos_b']))
    return row


NARROW_EXPECTED_CONSTANTS = {
    'n5_site_va': N5_SITE, 'n5_target_va': N5_TARGET, 'n5_return_va': N5_RETURN, 'n6_site_va': N6_SITE, 'n6_target_va': N6_TARGET, 'n7_site_va': N7_SITE,
    'n7_next_va': N7_NEXT, 'n7_hits_va': N7_HITS, 'n7_mode_va': N7_MODE, 'n8_site_va': N8_SITE, 'n8_next_va': N8_NEXT, 'n8_caller_va': N8_CALLER,
    'n8_window_length': len(N8_WINDOW), 'n8_window': N8_WINDOW, 'n8_stub_length': N8_STUB, 'n5_function_va': P1_FUNCTION[0], 'n5_function_end_va': P1_FUNCTION[1],
    'n6_function_va': N6_FUNCTION[0], 'n6_function_end_va': N6_FUNCTION[1], 'n7_function_va': N7_FUNCTION[0], 'n7_function_end_va': N7_FUNCTION[1],
    'call_length': 5, 'n5_pre_length': len(N5_PRE_WINDOW), 'n5_post_length': len(N5_POST_WINDOW), 'n6_pre_length': len(N6_PRE_WINDOW),
    'n6_post_length': len(N6_POST_WINDOW), 'n7_window_length': len(N7_WINDOW), 'n7_hits_operand': 1, 'n7_mode_operand': 10,
    'n5_callee_length': len(N5_CALLEE_BYTES), 'n5_callee_rel32': N5_CALLEE_REL32, 'n6_callee_length': len(N6_CALLEE_BYTES),
    'flags40_offset': 0x40, 'flags44_offset': 0x44, 'class_offset': 0x48, 'subtype_offset': 0x4a, 'physics_offset': 0x70, 'radius_offset': 0xa4,
    'position_offset': 0x30, 'saved_offset': 0xb0, 'saved_words': 4, 'xform_offset': 0xc0, 'xform_words': 11, 'node_flags_offset': 0x12c, 'model_offset': 0x140,
    'ring_capacity': 256, 'stub_capacity': NARROW_STUB_CAPACITY, 'n5_stub_length': N5_STUB, 'n6_stub_length': N6_STUB, 'n7_stub_length': N7_STUB,
    'n5_pre_window': N5_PRE_WINDOW, 'n5_post_window': N5_POST_WINDOW, 'n6_pre_window': N6_PRE_WINDOW, 'n6_post_window': N6_POST_WINDOW, 'n7_window': N7_WINDOW,
    'n5_callee': N5_CALLEE_BYTES, 'n6_callee': N6_CALLEE_BYTES}


def narrow_source_constants(text):
    def value(name):
        match = re.search(rf'\b{name}\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*[;,]', text)
        return int(match.group(1), 0) if match else None

    def array(name):
        match = re.search(rf'\b{name}\[\w+\]\s*=\s*\{{([^}}]*)\}}', text)
        return bytes(int(b, 0) for b in re.findall(r'0x[0-9a-fA-F]{2}', match.group(1))) if match else b''
    return {n: array(n) if isinstance(expected, bytes) else value(n) for n, expected in NARROW_EXPECTED_CONSTANTS.items()}


def narrow_own_windows():
    return [(N5_SITE - len(N5_PRE_WINDOW), N5_SITE + 5 + len(N5_POST_WINDOW)), (N6_SITE - len(N6_PRE_WINDOW), N6_SITE + 5 + len(N6_POST_WINDOW)),
            (N7_SITE, N7_SITE + len(N7_WINDOW)), (N5_CALLEE[0], N5_CALLEE[1]), (N6_CALLEE_HEAD[0], N6_CALLEE_HEAD[1]), (N8_SITE, N8_SITE + len(N8_WINDOW))]


def narrow_other_claims(root=ROOT):
    """Every claim that is not the census's own: the rest of src/proxy, the chase verifiers, and the box cull's windows."""
    box = [('collide_box_cull_p1', P1_SITE, len(P1_SITE_WINDOW)), ('collide_box_cull_p1_compare', P1_COMPARE, len(P1_COMPARE_WINDOW)),
           ('collide_box_cull_p1_reject', P1_REJECT, len(P1_REJECT_WINDOW)), ('collide_box_cull_p1_continue', P1_CONTINUE, len(P1_CONTINUE_WINDOW)),
           ('collide_box_cull_p2', P2_SITE, len(P2_SITE_WINDOW)), ('collide_box_cull_p2_compare', P2_COMPARE, len(P2_COMPARE_WINDOW)),
           ('collide_box_cull_p2_continue', P2_CONTINUE, len(P2_CONTINUE_WINDOW))]
    return sorted(other_claims(root, skip_prefix='collide_narrow_census', own_names=NARROW_OWN_NAMES) + box)


def narrow_overlaps(claims):
    return [(name, hex(address)) for name, address, length in claims for lo, hi in narrow_own_windows() if address < hi and lo < address + length]


_REL32_RE = re.compile(rb'[\xe8\xe9]|\x0f[\x80-\x8f]', re.S)


def rel32_references(image, lo, hi):
    """(va, opcode byte) of every byte position in .text that reads as a rel32 call/jmp/jcc whose target is in [lo, hi):
    a superset of the real branches (no decode), so an empty result is a proof."""
    out = []
    for name, base, vsize, rp, rsize in image.sections:
        if name != '.text':
            continue
        text = image.data[rp:rp + min(vsize, rsize)]
        for match in _REL32_RE.finditer(text):
            field = match.end()
            if field + 4 > len(text):
                continue
            target = (base + field + 4 + struct.unpack_from('<i', text, field)[0]) & 0xffffffff
            if lo <= target < hi:
                out.append((base + match.start(), text[match.end() - 1]))
    return out


def abs32_references(image, lo, hi):
    """(file offset, value) of every little-endian dword in the image whose value is in [lo, hi)."""
    out = []
    for va in range(lo, hi):
        needle = struct.pack('<I', va)
        at = image.data.find(needle)
        while at >= 0:
            out.append((at, va))
            at = image.data.find(needle, at + 1)
    return out


_FLAG_READERS = ('set', 'cmov', 'adc', 'sbb', 'pushf', 'lahf', 'rcl', 'rcr')


def _flags_dead(instructions):
    """True when the first EFLAGS-touching instruction of the sequence writes them."""
    for i in instructions:
        if (i.mnemonic.startswith('j') and i.mnemonic != 'jmp') or i.mnemonic.startswith(_FLAG_READERS):
            return False
        if _writes_flags(i):
            return True
        if i.mnemonic in ('call', 'ret', 'jmp'):
            return False
    return False


def _writes_register(i, registers):
    if i.mnemonic in ('cmp', 'test', 'push', 'call', 'nop') or i.mnemonic.startswith('j'):
        return False
    return i.operands.split(',')[0].strip().lower() in registers


def decode_narrow(exe):
    decoded = {}
    for bounds in (N5_CALLEE, N6_FUNCTION, N6_CALLEE_HEAD, N7_FUNCTION, N8_FUNCTION, SAT_CALLEE):
        decoded[bounds] = common.parse_objdump(common.objdump_window(exe, *bounds), *bounds)
    return decoded


def inspect_narrow(image, p1, decoded, core_text, claims):
    """The census's checks; `p1` is the decoded 0x0045d250 (site 5 lives in it)."""
    by1 = {i.va: i for i in p1}
    callee5, f6, head6, f7 = decoded[N5_CALLEE], decoded[N6_FUNCTION], decoded[N6_CALLEE_HEAD], decoded[N7_FUNCTION]
    by6, by7 = {i.va: i for i in f6}, {i.va: i for i in f7}
    f8 = decoded[N8_FUNCTION]
    by8 = {i.va: i for i in f8}
    target = lambda i: common._is_direct_control(i) if i is not None else None
    callee_image = image.read(N5_CALLEE[0], len(N5_CALLEE_BYTES)) or b''
    rel = N5_CALLEE_REL32
    short_into = lambda instructions, site: [(i.va, t) for i in instructions for t in [common._is_direct_control(i)] if t is not None and site < t < site + 5]
    between = [i for i in p1 if 0x45d620 <= i.va < N5_SITE]
    raw_at = lambda table, va: table[va].raw if va in table else b''
    return {
        'n5_windows': image.read(N5_SITE - len(N5_PRE_WINDOW), len(N5_PRE_WINDOW)) == N5_PRE_WINDOW and image.read(N5_RETURN, len(N5_POST_WINDOW)) == N5_POST_WINDOW,
        'n6_windows': image.read(N6_SITE - len(N6_PRE_WINDOW), len(N6_PRE_WINDOW)) == N6_PRE_WINDOW and image.read(N6_SITE + 5, len(N6_POST_WINDOW)) == N6_POST_WINDOW,
        'n7_window': image.read(N7_SITE, len(N7_WINDOW)) == N7_WINDOW and struct.unpack('<II', N7_WINDOW[1:5] + N7_WINDOW[10:14]) == (N7_HITS, N7_MODE),
        'n5_site_whole_call': N5_SITE in by1 and by1[N5_SITE].mnemonic == 'call' and len(by1[N5_SITE].raw) == 5 and target(by1[N5_SITE]) == N5_TARGET and N5_RETURN in by1,
        'n6_site_whole_call': N6_SITE in by6 and by6[N6_SITE].mnemonic == 'call' and len(by6[N6_SITE].raw) == 5 and target(by6[N6_SITE]) == N6_TARGET and N6_SITE + 5 in by6,
        'n8_window': image.read(N8_SITE, len(N8_WINDOW)) == N8_WINDOW,
        'n8_site_three_whole_instructions': [(by8[va].mnemonic, by8[va].raw) for va in (N8_SITE, N8_SITE + 3, N8_SITE + 4) if va in by8]
                                            == [('sub', N8_WINDOW[:3]), ('push', N8_WINDOW[3:4]), ('push', N8_WINDOW[4:5])] and N8_NEXT in by8,
        'n8_sole_caller': rel32_references(image, N8_SITE, N8_SITE + 1) == [(N8_CALLER, 0xe8)] and abs32_references(image, N8_SITE, N8_SITE + 1) == [],
        # The stub's `inc` runs before the re-executed `sub esp,0x34`, which rewrites every flag the inc touched.
        'n8_flags_written_by_displaced_sub': N8_SITE in by8 and _writes_flags(by8[N8_SITE]) and _flags_dead(f8),
        'n7_site_whole_mov': N7_SITE in by7 and by7[N7_SITE].mnemonic == 'mov' and by7[N7_SITE].raw == N7_WINDOW[:5] and N7_NEXT in by7,
        # Image-wide: nothing may enter a displaced span past its first byte.
        'no_rel32_into_spans': all(rel32_references(image, va + 1, va + 5) == [] for va in NARROW_SPANS.values()),
        'no_abs32_into_spans': all(abs32_references(image, va + 1, va + 5) == [] for va in NARROW_SPANS.values()),
        'no_short_jump_into_spans': short_into(p1, N5_SITE) == [] and short_into(f6, N6_SITE) == [] and short_into(f7, N7_SITE) == [] and short_into(f8, N8_SITE) == [],
        'n7_inbound_calls': sorted(rel32_references(image, N7_SITE, N7_SITE + 1)) == [(va, 0xe8) for va in N7_INBOUND] and abs32_references(image, N7_SITE, N7_SITE + 1) == [],
        # Callee bodies: the register convention the pre-window loads, one plain `ret` (the caller pops its two stack words).
        'n5_callee_bytes': callee_image[:rel] == N5_CALLEE_BYTES[:rel] and callee_image[rel + 4:] == N5_CALLEE_BYTES[rel + 4:]
                           and target(next((i for i in callee5 if i.mnemonic == 'call'), None)) == N6_FUNCTION[0],
        'n5_callee_plain_ret': [i.raw for i in callee5 if i.mnemonic.startswith('ret')] == [b'\xc3'] and callee5[-1].mnemonic == 'ret',
        'n6_callee_bytes': image.read(N6_TARGET, len(N6_CALLEE_BYTES)) == N6_CALLEE_BYTES,
        # Liveness the stubs rely on.
        'n5_flags_dead_at_callee_entry': _flags_dead(callee5),
        'n5_flags_dead_at_return': N5_RETURN in by1 and _writes_flags(by1[N5_RETURN]),
        'n5_pair_registers_live': raw_at(by1, 0x45d620) == bytes.fromhex('094344') and raw_at(by1, 0x45d623) == bytes.fromhex('094644')
                                  and not any(_writes_register(i, ('ebx', 'esi', 'bx', 'si')) for i in between)
                                  and not any(i.mnemonic in ('call', 'ret') for i in between)
                                  and raw_at(by1, 0x45d676) == bytes.fromhex('094344') and raw_at(by1, 0x45d679) == bytes.fromhex('094644'),
        'n6_flags_dead_at_callee_entry': _flags_dead(head6),
        'n7_flags_dead_after_site': _flags_dead([i for i in f7 if i.va >= N7_NEXT]),
        'n7_eax_defined_by_displaced': N7_SITE in by7 and by7[N7_SITE].operands.lower().replace(' ', '').startswith('eax,'),
        'narrow_claims_disjoint': narrow_overlaps(claims) == [] and len(claims) >= 40 and all(any(address == wanted for _, address, _ in claims) for wanted in (0x47d2a2, P1_SITE, P2_SITE)),
        'narrow_source_constants': narrow_source_constants(core_text) == NARROW_EXPECTED_CONSTANTS,
        'narrow_encoders': [len(encode_n5_stub(0x10000000, N5_RETURN, N5_TARGET, 0x20001000, 0x20002000, 0x20000000, 0x20000008, 0x2000000c)),
                            len(encode_n6_stub(0x10000200, 0x20000010, N6_TARGET)), len(encode_n7_stub(0x10000300, 0x20000014, N7_HITS, N7_NEXT)),
                            len(encode_n8_stub(0x10000400, 0x20000018, N8_NEXT))] == [N5_STUB, N6_STUB, N7_STUB, N8_STUB],
    }


# ---- SSE2 separating-axis replacement: the call at 0x004e25a3 (section 12.8) ----
SAT_CORE = ROOT / 'src/proxy/collide_sat_sse2_core.h'
SAT_SITE, SAT_TARGET, SAT_RETURN, SAT_HEAD = 0x4e25a3, 0x4e3280, 0x4e25a8, 0x4e2564
SAT_CALLEE = (0x4e3280, 0x4e38ae)
SAT_PRE_WINDOW = bytes.fromhex('d9c983c030d95c242050d9453453d8c98d7c2428d95c242cd84d38d95c2430')
SAT_POST_WINDOW = bytes.fromhex('83c40885c0759a')
SAT_CALLEE_SHA256 = '5167e784d126610a63b7562889e7402670cfaae6aa63900bcc2ad0b486ca56e6'
SAT_CALLEE_FNV1A = 0xad8a2cb66c0bf30c
FABS_HELPER_VA, FABS_HELPER, REPS_VA = 0x40e710, bytes.fromhex('558bec83e4f8d94508d9e18be55dc3'), 0x565600
SAT_INSTALL_RE = re.compile(r'\bcollide_sat_sse2 requested=(?P<requested>[01]) patched=(?P<patched>[01]) reason=(?P<reason>\S+) site=0x(?P<site>[0-9a-f]{8}) '
                            r'target=0x(?P<target>[0-9a-f]{8}) write=(?P<write>none|atomic|plain) handler=0x(?P<handler>[0-9a-f]{8})')
SAT_EXPECTED_CONSTANTS = {
    'sat_site_va': SAT_SITE, 'sat_target_va': SAT_TARGET, 'sat_return_va': SAT_RETURN, 'sat_target_end_va': SAT_CALLEE[1], 'sat_function_va': N7_FUNCTION[0],
    'sat_function_end_va': N7_FUNCTION[1], 'call_length': 5, 'sat_pre_length': len(SAT_PRE_WINDOW), 'sat_post_length': len(SAT_POST_WINDOW),
    'sat_callee_length': SAT_CALLEE[1] - SAT_CALLEE[0], 'sat_pre_window': SAT_PRE_WINDOW, 'sat_post_window': SAT_POST_WINDOW, 'sat_callee_fnv1a': SAT_CALLEE_FNV1A}


def fnv1a(data):
    h = 0xcbf29ce484222325
    for c in data:
        h = ((h ^ c) * 0x100000001b3) & 0xffffffffffffffff
    return h


def parse_sat_install_line(line):
    match = SAT_INSTALL_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    return {k: (v == '1' if k in ('requested', 'patched') else int(v, 16) if k in ('site', 'target', 'handler') else v) for k, v in row.items()}


def sat_source_constants(text):
    def value(name):
        match = re.search(rf'\b{name}\s*=\s*(0x[0-9a-fA-F]+|\d+)(?:ull)?\s*[;,]', text)
        return int(match.group(1), 0) if match else None

    def array(name):
        match = re.search(rf'\b{name}\[\w+\]\s*=\s*\{{([^}}]*)\}}', text)
        return bytes(int(b, 0) for b in re.findall(r'0x[0-9a-fA-F]{2}', match.group(1))) if match else b''
    return {n: array(n) if isinstance(expected, bytes) else value(n) for n, expected in SAT_EXPECTED_CONSTANTS.items()}


def _f32(value):
    """Round a Python double to float32 and back (round to nearest even, overflow to infinity, NaN kept)."""
    try:
        return struct.unpack('<f', struct.pack('<f', value))[0]
    except OverflowError:
        return float('inf') if value > 0 else float('-inf')


SAT_SLACK, SAT_REPS = 1.0 + 2.0 ** -20, _f32(1e-6)
_SAT_AXES = (  # per axis: the |T.L| terms (sign, T index, R index or None) and the radius terms (extent 'a'/'b', index, Bf index or None), in the engine's order
    (((1, 0, None),), (('b', 2, 2), ('b', 1, 1), ('b', 0, 0), ('a', 0, None))),
    (((1, 1, 3), (1, 2, 6), (1, 0, 0)), (('a', 2, 6), ('a', 1, 3), ('a', 0, 0), ('b', 0, None))),
    (((1, 1, None),), (('b', 2, 5), ('b', 1, 4), ('b', 0, 3), ('a', 1, None))),
    (((1, 2, None),), (('b', 2, 8), ('b', 1, 7), ('b', 0, 6), ('a', 2, None))),
    (((1, 2, 7), (1, 1, 4), (1, 0, 1)), (('a', 2, 7), ('a', 1, 4), ('a', 0, 1), ('b', 1, None))),
    (((1, 0, 2), (1, 1, 5), (1, 2, 8)), (('a', 2, 8), ('a', 1, 5), ('a', 0, 2), ('b', 2, None))),
    (((1, 2, 3), (-1, 1, 6)), (('b', 1, 2), ('b', 2, 1), ('a', 1, 6), ('a', 2, 3))),
    (((1, 2, 4), (-1, 1, 7)), (('b', 2, 0), ('a', 1, 7), ('a', 2, 4), ('b', 0, 2))),
    (((1, 2, 5), (-1, 1, 8)), (('b', 1, 0), ('a', 1, 8), ('a', 2, 5), ('b', 0, 1))),
    (((1, 0, 6), (-1, 2, 0)), (('b', 1, 5), ('b', 2, 4), ('a', 0, 6), ('a', 2, 0))),
    (((1, 0, 7), (-1, 2, 1)), (('b', 2, 3), ('a', 0, 7), ('a', 2, 1), ('b', 0, 5))),
    (((1, 0, 8), (-1, 2, 2)), (('b', 1, 3), ('a', 0, 8), ('a', 2, 2), ('b', 0, 4))),
    (((1, 1, 0), (-1, 0, 3)), (('b', 1, 8), ('b', 2, 7), ('a', 0, 3), ('a', 1, 0))),
    (((1, 1, 1), (-1, 0, 4)), (('b', 2, 6), ('a', 0, 4), ('a', 1, 1), ('b', 0, 8))),
    (((1, 1, 2), (-1, 0, 5)), (('b', 1, 6), ('a', 0, 5), ('a', 1, 2), ('b', 0, 7))))


def sat_disjoint(R, b, T, a):
    """Twin of core::obb_disjoint: 0 = no separating axis, else 1..15. Inputs are float32-representable Python floats."""
    bf = [_f32(abs(r) + SAT_REPS) for r in R]
    extent = {'a': a, 'b': b}
    for number, (t_terms, r_terms) in enumerate(_SAT_AXES, 1):
        t = 0.0
        for k, (sign, ti, ri) in enumerate(t_terms):
            term = T[ti] * (R[ri] if ri is not None else 1.0)
            t = term if k == 0 else t + sign * term
        t = abs(_f32(t)) if len(t_terms) > 1 else abs(t)
        radius = 0.0
        for k, (which, index, fi) in enumerate(r_terms):
            term = extent[which][index] * (bf[fi] if fi is not None else 1.0)
            radius = term if k == 0 else radius + term
        if not t <= radius * SAT_SLACK:   # unordered separates, as the engine's fcompp does
            return number
    return 0


def sat_own_windows():
    return [(SAT_SITE - len(SAT_PRE_WINDOW), SAT_RETURN + len(SAT_POST_WINDOW)), SAT_CALLEE, (FABS_HELPER_VA, FABS_HELPER_VA + len(FABS_HELPER))]


def sat_other_claims(root=ROOT):
    """Every claim that is not this module's: the rest of src/proxy (the census's four sites included), the chase verifiers,
    the box cull's windows and the census's compared windows."""
    box = [(name, address, length) for name, address, length in narrow_other_claims(root) if name.startswith('collide_box_cull_p')]
    census = [(f'collide_narrow_census_window_{k}', lo, hi - lo) for k, (lo, hi) in enumerate(narrow_own_windows())]
    return sorted(set(other_claims(root, skip_prefix='collide_sat_sse2', own_names=()) + box + census))


def sat_overlaps(claims):
    return [(name, hex(address)) for name, address, length in claims for lo, hi in sat_own_windows() if address < hi and lo < address + length]


def _x87_depth(instructions):
    """x87 stack depth after a straight-line sequence that starts empty, or None when an instruction is not modelled."""
    depth = 0
    for i in instructions:
        if not i.mnemonic.startswith('f'):
            continue
        if i.mnemonic == 'fld':
            depth += 1
        elif i.mnemonic == 'fstp':
            depth -= 1
        elif i.mnemonic not in ('fmul', 'fxch'):
            return None
        if depth < 0:
            return None
    return depth


def _mentions_simd(i):
    return re.search(r'\b(xmm|mm)[0-7]\b', i.operands.lower()) is not None or i.mnemonic == 'emms'


def inspect_sat(image, decoded, core_text, claims):
    f7, f8, callee = decoded[N7_FUNCTION], decoded[N8_FUNCTION], decoded[SAT_CALLEE]
    by7 = {i.va: i for i in f7}
    target = lambda i: common._is_direct_control(i) if i is not None else None
    body = image.read(SAT_CALLEE[0], SAT_CALLEE[1] - SAT_CALLEE[0]) or b''
    head = [i for i in f7 if SAT_HEAD <= i.va < SAT_SITE]
    calls = [i for i in callee if i.mnemonic == 'call']
    rets = [i for i in callee if i.mnemonic.startswith('ret')]
    raw_at = lambda va: by7[va].raw if va in by7 else b''
    reps = image.read(REPS_VA, 4) or b''
    return {
        'sat_windows': image.read(SAT_SITE - len(SAT_PRE_WINDOW), len(SAT_PRE_WINDOW)) == SAT_PRE_WINDOW and image.read(SAT_RETURN, len(SAT_POST_WINDOW)) == SAT_POST_WINDOW,
        'sat_site_whole_call': SAT_SITE in by7 and by7[SAT_SITE].mnemonic == 'call' and len(by7[SAT_SITE].raw) == 5 and target(by7[SAT_SITE]) == SAT_TARGET and SAT_RETURN in by7,
        'sat_sole_caller': rel32_references(image, SAT_TARGET, SAT_TARGET + 1) == [(SAT_SITE, 0xe8)] and abs32_references(image, SAT_TARGET, SAT_TARGET + 1) == [],
        'sat_no_rel32_into_span': rel32_references(image, SAT_SITE + 1, SAT_SITE + 5) == [],
        'sat_no_abs32_into_span': abs32_references(image, SAT_SITE + 1, SAT_SITE + 5) == [],
        'sat_no_short_jump_into_span': [t for i in f7 for t in [common._is_direct_control(i)] if t is not None and SAT_SITE < t < SAT_SITE + 5] == [],
        'sat_callee_hash': len(body) == SAT_CALLEE[1] - SAT_CALLEE[0] and hashlib.sha256(body).hexdigest() == SAT_CALLEE_SHA256 and fnv1a(body) == SAT_CALLEE_FNV1A,
        # The body: 24 calls, all to the float `fabs` helper; plain rets; nine loads of reps; ECX/EDX/ESI/EDI never written.
        'sat_callee_calls_fabs_only': len(calls) == 24 and all(target(i) == FABS_HELPER_VA for i in calls) and image.read(FABS_HELPER_VA, len(FABS_HELPER)) == FABS_HELPER,
        'sat_callee_plain_rets': len(rets) == 16 and all(i.raw == b'\xc3' for i in rets) and callee[-1].mnemonic == 'ret',
        'sat_callee_reps': body.count(b'\xd8\x05' + struct.pack('<I', REPS_VA)) == 9 and len(reps) == 4 and struct.unpack('<f', reps)[0] == SAT_REPS,
        'sat_callee_keeps_ecx_edx_esi_edi': not any(_writes_register(i, ('ecx', 'edx', 'esi', 'edi', 'cx', 'dx', 'si', 'di', 'cl', 'dl', 'ch', 'dh')) or i.mnemonic == 'pop' and
                                                    i.operands.strip().lower() not in ('ebp', 'ebx') for i in callee),
        # The caller: arguments as the thunk reads them, x87 stack empty at the site, EFLAGS rewritten at the return.
        'sat_arguments': raw_at(0x4e2573) == bytes.fromhex('8b5c2460') and raw_at(0x4e2577) == bytes.fromhex('8b74245c') and raw_at(0x4e258d) == b'\x50' and raw_at(0x4e2591) == b'\x53'
                         and raw_at(0x4e2594) == bytes.fromhex('8d7c2428') and not any(_writes_register(i, ('esi', 'ebx')) for i in f7 if 0x4e257b <= i.va < SAT_SITE),
        'sat_x87_empty_at_site': not any(_x87(i) for i in f7 if i.va < SAT_HEAD) and _x87_depth(head) == 0,
        'sat_flags_dead_at_return': SAT_RETURN in by7 and _writes_flags(by7[SAT_RETURN]),
        'sat_no_xmm_in_descent': not any(_mentions_simd(i) for i in f7 + f8 + callee),
        'sat_claims_disjoint': sat_overlaps(claims) == [] and len(claims) >= 40 and all(any(address == wanted for _, address, _ in claims) for wanted in (N7_SITE, N8_SITE, N5_SITE, P1_SITE)),
        'sat_source_constants': sat_source_constants(core_text) == SAT_EXPECTED_CONSTANTS,
    }


def decode(exe):
    decoded = {}
    for bounds in (P1_FUNCTION, P2_FUNCTION):
        decoded[bounds] = common.parse_objdump(common.objdump_window(exe, *bounds), *bounds)
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


def inspect(data, decoded, core_text, claims, narrow=None, sat=None):
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
        'exe_identity': exe_identity.identity_ok(data),
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
    if narrow is not None:
        checks.update(inspect_narrow(image, p1, narrow['decoded'], narrow['core_text'], narrow['claims']))
    if narrow is not None and sat is not None:
        checks.update(inspect_sat(image, narrow['decoded'], sat['core_text'], sat['claims']))
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks, 'exe_info': exe_identity.info(data), 'p1_site': hex(P1_SITE), 'p2_site': hex(P2_SITE),
            'narrow_sites': [hex(N5_SITE), hex(N6_SITE), hex(N7_SITE), hex(N8_SITE)] if narrow is not None else [],
            'sat_site': hex(SAT_SITE) if sat is not None else None, 'sat_other_claims_checked': len(sat['claims']) if sat is not None else 0, 'narrow_other_claims_checked': len(narrow['claims']) if narrow is not None else 0,
            'other_claims_checked': len(claims), 'overlaps': overlaps(claims), 'jump_table_0x45e108': [hex(v) for v in table(0x45e108, 2)],
            'instructions': {'p1': len(p1), 'p2': len(p2)}, 'exe_sha256': hashlib.sha256(data).hexdigest()}


def narrow_inputs(exe, core=NARROW_CORE):
    return {'decoded': decode_narrow(exe), 'core_text': Path(core).read_text(), 'claims': narrow_other_claims()}


def sat_inputs(core=None):
    return {'core_text': Path(core or SAT_CORE).read_text(), 'claims': sat_other_claims()}


def verify(exe=DEFAULT_EXE, core=CORE, narrow_core=NARROW_CORE, sat_core=None):
    data = common.image_bytes(exe)
    try:
        return inspect(data, decode(exe), Path(core).read_text(), other_claims(), narrow_inputs(exe, narrow_core), sat_inputs(sat_core))
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
