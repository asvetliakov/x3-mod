#!/usr/bin/env python3
"""Read-only qualification of the FL ship-avoidance site E and the snapshot bindings in X3FL.exe
(src/engine/fl/ship_avoidance_sites_fl.h, ship_avoidance_fl.cpp; docs/reverse-engineering/fl/ship-avoidance.md, "Port (2026-11-03)").

Checks on the installed exe (default /mnt/c/X3FL/X3FL.exe; every value an RVA, preferred base 0x400000):
- site E, the native warn test 0x057600..0x057ccb: the six-byte entry `push ebp; mov ebp,esp; and esp,-16` (1+2+3, no relative
  flow, no HIGHLOW, no direct edge or relocated pointer into its interior), the continuation 0x057606, every return of the body
  `ret 0xc` (five), A in ECX and B / distance / &out at entry [esp+4/+8/+12];
- its exactly three callers (proximity 0x057db5 and its order-0x12 refresh 0x057dd0, the avoid order 0x060581), each loading
  ECX = A and pushing three words, each followed by `test eax,eax; je` (the false path skips every output read: the avoid
  caller reads [esp+0x80] only after the JE); the proximity true path's native warn (flags |= 0x1000, the list append
  +0x1e4 / +0x1f8 below the count +0x1e0 < 5, |= 0x2000);
- the snapshot roots: sector+0x50 bucket table, stride 0xc (the collision pass's L0 and the bucket helper 0x057e10), the next
  link [obj] ended by a zero-first-dword sentinel, the object's sector +0x54; the warn reset mask 0xff9fcfff and the ships'
  count reset; the native pair masks flags 0x08200404 and flags2 0x20000 (both partners); the warn's class tests (stations
  5/6, asteroid 0x11) and radii obj+0xa4; the integrator's velocity phys+0x100.. plus slip obj+0x60..;
- the station part layout from the cached subtree-radius getter 0x0a39b0: children head +0xc, sibling [child], zero sentinel,
  own radius +0xa0, cache +0xa4 (store at 0x0a3ac1), child offsets +0x30/+0x34/+0x38 scaled by the parent's 16.16 +0x70 under
  child +0x12c & 0x40, `ret 4`;
- every identity window of the header (bytes with relocated dwords zeroed, FNV-1a, fixup targets) and the AP addresses (the AP
  warn entry and callers' bytes at their AP VAs do not match: an AP binding is refused).

usage: verify_ship_avoidance_sites_fl.py [--exe PATH] [--json] [--emit-windows]   (exit 0 = PASS)
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import seta_fl_probe as P  # noqa: E402
import ship_steering_fl_probe as S  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
SITES = ROOT / 'src/engine/fl/ship_avoidance_sites_fl.h'
BASE = P.BASE
WARN, WARN_END, WARN_LEN = 0x057600, 0x057ccb, 6
WARN_RETS = (0x05773c, 0x05776f, 0x057ae1, 0x057c39, 0x057cc8)
CALLERS = {0x057db5: 0x057e08, 0x057dd0: 0x057e08, 0x060581: 0x0609cd}
COMMAND_PASS = (0x05c810, 0x062efe)
PROXIMITY = (0x057cd0, 0x057e10)
# (name, rva, length, part, code): part 2 = needed by the avoidance (the snapshot and the target adjustment), part 3 = site E.
WINDOW_SPANS = (
    ('collision_reset', 0x066e10, 0x41, 2, True),      # L0: bucket head [table + 0xc k], warn bits reset, f+0x1e0 = 0, next link
    ('pair_mask_a', 0x066f00, 0x25, 2, True),          # flags & 0x08200404, flags2 & 0x20000 (first partner)
    ('pair_mask_b', 0x066fd1, 0x29, 2, True),          # ... (second partner)
    ('bucket_helper', 0x057e10, 0x30, 2, True),        # sector in ECX, [sector+0x50] + 0xc class, obj+0x54, the sentinel
    ('subtree_radius', 0x0a39b0, 0x123, 2, True),      # the station part layout (children, offsets, scale flag, radii)
    ('integrator_velocity', 0x064020, 0x3a, 2, True),  # phys+0x100.. + obj+0x60..
    ('warn_entry', 0x057600, 0x21, 3, True),           # E's six bytes, A = ECX
    ('warn_classes', 0x057632, 0x13, 3, True),         # B's station test 5/6
    ('warn_radii', 0x057694, 0x0e, 3, True),           # obj+0xa4 of A and B
    ('warn_ret_0', 0x05773c, 3, 3, True),
    ('warn_ret_1', 0x05776f, 3, 3, True),
    ('warn_ret_2', 0x057ae1, 3, 3, True),
    ('warn_ret_3', 0x057c39, 3, 3, True),
    ('warn_ret_4', 0x057cc8, 3, 3, True),
    ('proximity_calls', 0x057dac, 0x5c, 3, True),      # both proximity calls, the native warn on a true verdict
    ('avoid_call', 0x06057a, 0x14, 3, True),           # the avoid order's call, the output read after the JE
)
ANCHORS = (
    (0x05760b, 'mov edi, ecx', 'E: A in ECX'),
    (0x057632, 'mov esi, dword ptr [ebp + 8]', 'E: B at entry [esp+4]'),
    (0x057639, 'cmp cx, 5', 'E: station class 5'),
    (0x05763f, 'cmp cx, 6', 'E: station class 6'),
    (0x057705, 'cmp cx, 0x11', 'E: asteroid class 0x11'),
    (0x057694, 'mov edx, dword ptr [esi + 0xa4]', 'E: B radius +0xa4'),
    (0x05769a, 'mov eax, dword ptr [edi + 0xa4]', 'E: A radius +0xa4'),
    (0x057dac, 'lea eax, [esp + 0x10]', 'proximity: &out'),
    (0x057db0, 'mov ecx, esi', 'proximity: A in ECX'),
    (0x057db3, 'push ebx', 'proximity: distance'),
    (0x057db4, 'push edi', 'proximity: B'),
    (0x057dba, 'test eax, eax', 'proximity: the verdict tested at once'),
    (0x057dbc, 'je 0x457e08', 'proximity: false -> no native warn'),
    (0x057dbe, 'or dword ptr [esi + 0x40], 0x1000', 'native warn bit 0x1000'),
    (0x057dcb, 'mov ecx, esi', 'refresh: A in ECX'),
    (0x057dd5, 'test eax, eax', 'refresh: the verdict tested at once'),
    (0x057dd7, 'je 0x457e08', 'refresh: false -> no append'),
    (0x057ddc, 'mov ecx, dword ptr [eax + 0x1e0]', 'append: count f+0x1e0'),
    (0x057de2, 'cmp ecx, 5', 'append: at most five'),
    (0x057de7, 'mov dword ptr [eax + ecx*4 + 0x1e4], edi', 'append: B into f+0x1e4[n]'),
    (0x057dfb, 'or dword ptr [esi + 0x40], 0x2000', 'native warn bit 0x2000'),
    (0x057e02, 'inc dword ptr [eax + 0x1e0]', 'append: count'),
    (0x057cd6, 'mov dword ptr [esp + 0xc], 0', 'proximity: the output local zeroed, never read'),
    (0x06057e, 'mov ecx, ebx', 'avoid order: A in ECX'),
    (0x060580, 'push esi', 'avoid order: B'),
    (0x060586, 'test eax, eax', 'avoid order: the verdict tested at once'),
    (0x060588, 'je 0x4609cd', 'avoid order: false skips the output read'),
    (0x06058e, 'mov ecx, dword ptr [esp + 0x80]', 'avoid order: the output read on the true path only'),
    (0x066e10, 'mov eax, dword ptr [ebx + 0x50]', 'L0: the sector bucket table +0x50'),
    (0x066e16, 'cmp dword ptr [ecx], 0', 'L0: the zero-first-dword sentinel'),
    (0x066e20, 'and dword ptr [ecx + 0x40], 0xff9fcfff', 'L0: the warn reset'),
    (0x066e38, 'mov dword ptr [eax + 0x1e0], 0', 'L0: the ships\' list reset'),
    (0x066e42, 'mov ecx, dword ptr [ecx]', 'L0: the next link [obj]'),
    (0x066e49, 'add edx, 0xc', 'L0: bucket stride 0xc'),
    (0x066f00, 'test dword ptr [edi + 0x40], 0x8200404', 'pair mask flags (first)'),
    (0x066f19, 'test ecx, 0x20000', 'pair mask flags2 (first)'),
    (0x066fd1, 'test dword ptr [ebx + 0x40], 0x8200404', 'pair mask flags (second)'),
    (0x066fee, 'test esi, 0x20000', 'pair mask flags2 (second)'),
    (0x057e1a, 'cmp dword ptr [eax + 0x54], edx', 'bucket helper: obj+0x54 sector'),
    (0x057e28, 'lea ecx, [eax + eax*2]', 'bucket helper: 0xc x class'),
    (0x057e2b, 'mov eax, dword ptr [edx + 0x50]', 'bucket helper: sector+0x50'),
    (0x057e2e, 'mov eax, dword ptr [eax + ecx*4]', 'bucket helper: the head'),
    (0x0a39bd, 'mov edi, ecx', 'radius getter: node in ECX'),
    (0x0a39c3, 'mov eax, dword ptr [edi + 0xa4]', 'radius getter: the cache +0xa4'),
    (0x0a39d1, 'mov ebx, dword ptr [edi + 0xc]', 'radius getter: children head +0xc'),
    (0x0a39d4, 'mov esi, dword ptr [edi + 0xa0]', 'radius getter: own radius +0xa0'),
    (0x0a39da, 'cmp dword ptr [ebx], 0', 'radius getter: the sentinel'),
    (0x0a39ec, 'test byte ptr [ebx + 0x12c], 0x40', 'radius getter: the scaled-child bit'),
    (0x0a39fb, 'mov edi, dword ptr [edi + 0x70]', 'radius getter: the parent scale +0x70'),
    (0x0a39fe, 'mov eax, dword ptr [ebx + 0x30]', 'radius getter: child offset +0x30'),
    (0x0a3ab6, 'mov ebx, dword ptr [ebx]', 'radius getter: the sibling link'),
    (0x0a3ac1, 'mov dword ptr [edi + 0xa4], esi', 'radius getter: the cache store'),
    (0x0a3ad0, 'ret 4', 'radius getter: ret 4'),
    (0x064020, 'mov ecx, dword ptr [ebx + 0x70]', 'integrator: phys'),
    (0x064023, 'lea esi, [ebx + 0x60]', 'integrator: the slip obj+0x60'),
    (0x064032, 'mov eax, dword ptr [ecx + 0x100]', 'integrator: phys velocity +0x100'),
    (0x064038, 'add eax, dword ptr [esi]', 'integrator: + slip x'),
    (0x064044, 'add eax, dword ptr [ecx + 0x104]', 'integrator: phys velocity +0x104'),
    (0x064054, 'add eax, dword ptr [ecx + 0x108]', 'integrator: phys velocity +0x108'),
)
# AP preferred VAs (src/proxy/ship_steering_sites.h: the warn entry and its three call sites, the bucket table read): their AP
# bytes (the entry span plus the first instruction after it) must not match in X3FL.exe.
AP_SPANS = ((0x44dd90, '558bec83e4f083ec'), (0x44e522, 'e869f8ffff'), (0x44e540, 'e84bf8ffff'), (0x456c02, 'e88971ffff'))


def verify(data):
    report = {'result': 'FAIL', 'edition': 'fl', 'checks': {}}
    checks = report['checks']
    sha = hashlib.sha256(data).hexdigest()
    report['exe_info'] = {'sha256': sha, 'known': sha == P.FL_SHA256}
    img = P.FlImage(data)
    checks['fl_identity'] = {'ok': img.timestamp == P.FL_TIMESTAMP and img.image_size == P.FL_IMAGE_SIZE}
    # ---- site E ----
    span = S.span_row(img, 'ship_avoid_warn_test_fl', WARN, '558bec83e4f0', (1, 2, 3))
    body = img.insns(WARN, WARN_END) or []
    rets = [(i.address - BASE, i.op_str) for i in body if i.mnemonic == 'ret']
    x87 = sum(i.mnemonic.startswith('f') for i in body)
    callers = sorted(P.raw_callers(img, WARN))
    checks['site_e'] = {'ok': span['ok'] and bool(body) and rets == [(r, '0xc') for r in WARN_RETS] and not x87 and
                        callers == sorted(CALLERS),
                        'span': span, 'returns': [f'{a:#08x} ret {o}' for a, o in rets], 'x87': x87,
                        'callers': [f'{c:#08x}' for c in callers], 'body': [f'{WARN:#08x}', f'{WARN_END:#08x}']}
    # ---- the callers: three pushed words, ECX = A, the verdict tested at once and its false path ----
    starts = P.starts(img, *COMMAND_PASS) | P.starts(img, *PROXIMITY)
    rows = []
    for call, false_target in CALLERS.items():
        after = img.text_at(call + 5)[0]
        nxt = img.text_at(call + 7)[0]
        rows.append({'call': f'{call:#08x}', 'start': call in starts, 'after': after, 'jcc': nxt,
                     'ok': call in starts and after == 'test eax, eax' and nxt is not None and nxt.split()[0] == 'je' and
                     int(nxt.split()[1], 16) - BASE == false_target})
    checks['callers'] = {'ok': all(r['ok'] for r in rows) and 0x06057a in starts, 'callers': rows}
    checks['anchors'] = S.anchors(img, ANCHORS)
    header_text = SITES.read_text() if SITES.is_file() else ''
    checks['windows'] = S.windows_check(img, WINDOW_SPANS, header_text)
    checks['ap_refused'] = S.ap_refusal(img, AP_SPANS, [header_text])
    report['result'] = 'PASS' if all(c.get('ok') for c in checks.values()) else 'FAIL'
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--exe', type=Path, default=P.FL_DEFAULT_EXE)
    parser.add_argument('--json', action='store_true', help='print the full report')
    parser.add_argument('--emit-windows', action='store_true', help='print the C++ window rows of the sites header')
    args = parser.parse_args()
    if not args.exe.is_file():
        print(json.dumps({'result': 'SKIP', 'reason': f'{args.exe} not found'}))
        return 2
    data = args.exe.read_bytes()
    if args.emit_windows:
        print(S.emit_windows(P.FlImage(data), WINDOW_SPANS))
        return 0
    report = verify(data)
    report['exe'] = str(args.exe)
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(json.dumps({'result': report['result'], 'exe': report['exe'], 'exe_info': report['exe_info'],
                          **{k: v['ok'] for k, v in report['checks'].items()}}))
    return 0 if report['result'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
