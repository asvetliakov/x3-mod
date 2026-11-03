#!/usr/bin/env python3
"""The FL node walk's engine read set on X3FL.exe, verified from the bytes (2026-11-03).

Owning notes: docs/architecture/fl-nozzle-walk-activation.md ("As built") and docs/reverse-engineering/fl/engine-effects.md,
"Verified 2026-11-03: node walk". The walk (src/proxy/engine_nozzle_walk_core.h, run by motion_output_engine_plumes_inc.h
engine_node_append on FL only at the hook's scene end) reads root +0xc, child +0 / +0x18 / +0x28 / +0x12c / +0x130 and
the first 0x150 bytes of a JET child (model at +0x144), C at +0x260 with the anchor rule on; the gather owns camera
+0x1c -> context +0x2c inside the draw's material scope. Every field is pinned by a whole-instruction anchor (Capstone
over tools/analysis/inspect_x3.py's PE reader, the Image class of the fl-engine-effects producer); the far-jet read set
(verification/results/fl-far-jets/read_set.py, its FIELDS list) is re-asserted on the same image.

Writes read_set.json beside this script (RVAs, offsets, booleans; no bytes, no listing) and prints one JSON line.
usage: python3 -I verification/results/fl-nozzle-walk/read_set.py [--exe /mnt/c/X3FL/X3FL.exe]
Exit 1 when an anchor fails.
"""
import argparse
import importlib.util
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_ee = _load('fl_ee_produce', 'verification/results/fl-engine-effects/produce.py')
_far = _load('fl_far_read_set', 'verification/results/fl-far-jets/read_set.py')
Image, BASE, FL_SHA256 = _ee.Image, _ee.BASE, _ee.FL_SHA256
OUT = Path(__file__).resolve().parent / 'read_set.json'

# (field, AP offset, FL offset, reader in the proxy, [(FL VA, exact instruction, role), ...])
FIELDS = [
    ('root child list head +0xc, sentinel at +0x10', 0xc, 0xc, 'walk', [
        (0x4a22ab, 'lea ecx, [edi + 0xc]', 'node constructor: list header at +0xc'),
        (0x4a22ae, 'mov dword ptr [ecx + 8], ecx', 'tail (+0x14) = header'),
        (0x4a22b1, 'lea eax, [ecx + 4]', 'embedded sentinel at +0x10'),
        (0x4a22b4, 'mov dword ptr [ecx], eax', 'head = sentinel (empty list)'),
        (0x4a22b8, 'mov dword ptr [eax], 0', 'sentinel +0 = 0: the end test'),
        (0x414eff, 'mov esi, dword ptr [esi + 0xc]', 'emitter walk loads the head'),
        (0x414f02, 'cmp dword ptr [esi], ebx', 'emitter walk ends at [child+0] == 0 (EBX = 0)'),
        (0x464630, 'mov esi, dword ptr [ecx + 0xc]', 'jet drive loads the head')]),
    ('child next +0', 0x0, 0x0, 'walk', [
        (0x464bc4, 'mov esi, dword ptr [esi]', 'jet drive: next'),
        (0x464bd2, 'cmp dword ptr [esi], 0', 'jet drive: sentinel test'),
        (0x4a33b0, 'mov ecx, dword ptr [ebp + 0xc]', 'node destructor loads the head'),
        (0x4a33b3, 'cmp dword ptr [ecx], 0', 'destructor sentinel test'),
        (0x4a33b8, 'mov esi, dword ptr [ecx]', 'destructor: next')]),
    ('node block 0x270 bytes (0x150 prefix and C at +0x260 inside)', None, 0x270, 'walk', [
        (0x4a2203, 'push 0x270', 'node allocation size'),
        (0x4a2208, 'call 0x531395', 'allocator')]),
    ('JET bit +0x130 (drive filter)', 0x130, 0x130, 'walk', [
        (0x4646a6, 'test byte ptr [esi + 0x130], 1', 'jet drive skips a non-JET child')]),
    ('hidden flag +0x12c 0x100000', 0x12c, 0x12c, 'walk', [
        (0x464874, 'and dword ptr [esi + 0x12c], 0xffefffff', 'drive shows (main branch)'),
        (0x464880, 'or dword ptr [esi + 0x12c], 0x100000', 'drive hides (main branch)'),
        (0x4648c2, 'and dword ptr [esi + 0x12c], 0xffefffff', 'drive shows (other branch)'),
        (0x4648ce, 'or dword ptr [esi + 0x12c], 0x100000', 'drive hides (other branch)')]),
    ('node y scale +0x84', 0x84, 0x84, 'walk', [
        (0x4a3b01, 'mov dword ptr [esi + 0x84], ebx', 'scale setter writes y')]),
    ('mode word C +0x260 (anchor rule)', 0x260, 0x260, 'walk', [
        (0x4646b6, 'mov eax, dword ptr [esi + 0x260]', 'jet drive reads C')]),
    ('draw camera -> context +0x1c (gather, owned scale)', 0x1c, 0x1c, 'gather', [
        (0x48ba48, 'mov ecx, dword ptr [esi + 0x1c]', 'sector scene: ECX = camera +0x1c for the selection walk'),
        (0x48ba78, 'mov ecx, dword ptr [esi + 0x1c]', 'ECX = camera +0x1c for the material traversal'),
        (0x48ba7e, 'push esi', 'camera pushed'),
        (0x48ba7f, 'call 0x4978e0', 'material traversal'),
        (0x48ba8b, 'mov edx, esi', 'camera in EDX'),
        (0x48ba8d, 'call 0x497680', 'deferred traversal'),
        (0x49795b, 'mov edi, dword ptr [ebp + 8]', 'material traversal: EDI = its camera argument'),
        (0x497030, 'push edi', 'immediate submission pushes EDI = camera (material-submit.md section 1)'),
        (0x497701, 'push ebx', 'deferred submission pushes EBX = camera (material-submit.md section 1)'),
        (0x497702, 'call 0x4e6ef0', 'submission wrapper')]),
    ('context scale +0x2c (gather, owned scale)', 0x2c, 0x2c, 'gather', [
        (0x4def18, 'mulss xmm1, dword ptr [esi + 0x2c]', 'camera publication: view translation x context scale'),
        (0x4e7041, 'mov eax, dword ptr [edx + 0x1c]', 'submission wrapper: the node context +0x1c ...'),
        (0x4e7048, 'mulss xmm1, dword ptr [eax + 0x2c]', '... and its +0x2c scale')]),
    ('hook boundary (pre-compositor call)', None, None, 'append', [
        (0x48b929, 'call 0x4e6770', 'the scene hook site: the append runs inside this call on FL')]),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', default='/mnt/c/X3FL/X3FL.exe')
    args = ap.parse_args()
    fl = Image(args.exe)
    assert fl.sha256 == FL_SHA256, 'input hash'
    failures, table = [], []

    def anchor_rows(anchors):
        rows = []
        for va, text, role in anchors:
            try:
                fl.at(va, text)
                rows.append(dict(rva='%#08x' % (va - BASE), role=role, ok=True))
            except AssertionError as e:
                failures.append(str(e))
                rows.append(dict(rva='%#08x' % (va - BASE), role=role, ok=False))
        return rows
    for field, ap_off, fl_off, side, anchors in FIELDS:
        table.append(dict(field=field, ap_offset=None if ap_off is None else '%#x' % ap_off,
                          fl_offset=None if fl_off is None else '%#x' % fl_off, side=side, anchors=anchor_rows(anchors)))
    far_rows = [dict(field=field, anchors=anchor_rows(anchors)) for field, _, _, _, anchors in _far.FIELDS]
    # The walk's constants against the decoded facts: C inside the block, the 0x150 prefix inside the block.
    core = (ROOT / 'src/proxy/engine_nozzle_walk_core.h').read_text()
    layout = (ROOT / 'src/proxy/engine_node_layout.h').read_text()
    constants = dict(node_bytes_0x150='node_bytes = 0x150' in core, mode_offset_0x260='mode_offset = 0x260' in core,
                     fl_model_0x144='constexpr Layout fl{0x18, 0x28, 0x144};' in layout,
                     prefix_inside_block=0x150 <= 0x270 and 0x260 + 4 <= 0x270)
    for k, v in constants.items():
        if not v:
            failures.append('constant ' + k)
    result = dict(fl_sha256=fl.sha256[:16], fields=len(table), anchors=sum(len(t['anchors']) for t in table),
                  far_fields=len(far_rows), far_anchors=sum(len(r['anchors']) for r in far_rows),
                  far_anchors_ok=all(a['ok'] for r in far_rows for a in r['anchors']), constants=constants,
                  failures=failures, passed=not failures, table=table)
    OUT.write_text(json.dumps(result, indent=1) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'table'}))
    return 0 if not failures else 1


if __name__ == '__main__':
    sys.exit(main())
