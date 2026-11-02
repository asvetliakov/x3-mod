#!/usr/bin/env python3
"""The far-jet hand-off's engine read set on X3FL.exe, verified from the bytes (2026-11-03).

Owning notes: docs/reverse-engineering/fl/cull-chain.md and fl/engine-effects.md, "Verified 2026-11-03". The far-jet
handler (src/proxy/engine_far_jets.cpp) runs at the small-parts stub's far block inside the FL selector 0x495e50 (site
0x4961d3: node ESI, measure ECX, camera EBX); the plume stage (motion_output_engine_plumes_inc.h engine_far_append)
consumes its copies outside the pass. Every field either side reads is pinned here by a whole-instruction anchor
(Capstone over tools/analysis/inspect_x3.py's PE reader, the Image and Cfg classes of the fl-engine-effects and
fl-cull-chain producers), plus three structural facts over the selector's CFG:

  * dominance: the selector's own node-context loads 0x495fc7 `mov eax,[esi+0x1c]` / 0x495fce `movss xmm2,[eax+0x2c]`
    lie on every path from the entry to the site (removing 0x495fc7 leaves 0x4961d3 unreachable), so the FL handler's
    owned context scale is a value the selector itself read on the same node in the same invocation;
  * ESI's only writer that reaches the site is 0x495e5e `mov esi,ecx` (every ESI writer Capstone reports, pop esi
    included; the child loop's 0x495f27 / 0x495f3c and the epilogues do not reach it),
    and no selector instruction writes node +0x1c, +0x18, +0x28, +0x70, +0x80..+0x88, +0xb0..+0xe8, +0x144 or +0x1d8
    between entry and site. That covers the selector's own instructions only: the calls between 0x495fc7 and the
    site (listed as calls_between_context_load_and_site) were not checked, so the handler's own reads at the site
    are not proven to see the values the selector loaded, and nothing relies on that;
  * the walker passes its camera argument to the selector unchanged (0x49772a / 0x497742 / 0x497745) and the sector
    scene loads camera +0x1c as the walker's context (0x48ba48), the context whose +0x2c the camera publication reads
    (0x495687 -> 0x4ded90 -> 0x4def18).

Writes read_set.json beside this script (RVAs, offsets, booleans; no bytes, no listing) and prints one JSON line.
usage: python3 -I verification/results/fl-far-jets/read_set.py [--exe /mnt/c/X3FL/X3FL.exe] [--ap /mnt/c/X3AP-orig/X3AP.exe]
Exit 1 when an anchor or a structural fact fails.
"""
import argparse
import importlib.util
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
_spec = importlib.util.spec_from_file_location('fl_cull_produce', ROOT / 'verification/results/fl-cull-chain/produce.py')
_cc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_cc)
Image, Cfg, BASE, FL_SHA256, AP_SHA256 = _cc.Image, _cc.Cfg, _cc.BASE, _cc.FL_SHA256, _cc.AP_SHA256
OUT = Path(__file__).resolve().parent / 'read_set.json'
SEL, SEL_END, SITE, NODE_CONTEXT = 0x495e50, 0x496446, 0x4961d3, 0x495fc7

# (field, AP offset, FL offset, side, [(FL VA, exact instruction, role), ...]); side: handler (in the pass) or consumer
# (the plume stage, outside the pass) or stub (the far block's flag test).
FIELDS = [
    ('node parent', 0x18, 0x18, 'handler', [
        (0x4961d3, 'mov eax, dword ptr [esi + 0x18]', 'selector loads the parent at the site'),
        (0x4a5809, 'mov dword ptr [edi + 0x18], ebp', 'reparent stores the parent')]),
    ('node handle', 0x28, 0x28, 'handler', [
        (0x4a22a8, 'mov dword ptr [edi + 0x28], eax', 'node allocation stores the registered handle')]),
    ('node base scale', 0x70, 0x70, 'handler', [
        (0x4de59a, 'imul dword ptr [edi + 0x70]', 'world matrix base factor'),
        (0x4a3b28, 'imul dword ptr [esi + 0x70]', 'scale setter: +0xa0 = +0x70 x max axis')]),
    ('node x scale', 0x80, 0x80, 'handler', [
        (0x4a3af8, 'mov dword ptr [esi + 0x80], edi', 'scale setter writes x')]),
    ('node z scale (throttle)', 0x88, 0x88, 'handler', [
        (0x4a3b09, 'mov dword ptr [esi + 0x88], ebp', 'scale setter writes z'),
        (0x464932, 'mov edx, dword ptr [esi + 0x88]', 'jet drive reads the current z')]),
    ('node position x/y/z', 0xb0, 0xb0, 'handler', [
        (0x4deb15, 'movd xmm0, dword ptr [edi + 0xb0]', 'world matrix position x'),
        (0x4deb2a, 'movd xmm0, dword ptr [edi + 0xb4]', 'position y'),
        (0x4deb3f, 'movd xmm0, dword ptr [edi + 0xb8]', 'position z'),
        (0x4deb20, 'mulss xmm0, dword ptr [ebx + 0x2c]', 'position x context scale')]),
    ('node basis row 0 (model x)', 0xc0, 0xc0, 'handler', [
        (0x4de9ef, 'movd xmm0, dword ptr [edi + 0xc0]', 'world matrix basis row 0'),
        (0x4dea02, 'movd xmm0, dword ptr [edi + 0xc4]', 'row 0 y'),
        (0x4dea16, 'movd xmm0, dword ptr [edi + 0xc8]', 'row 0 z')]),
    ('node basis row 2 (model z)', 0xe0, 0xe0, 'handler', [
        (0x4dea66, 'movd xmm0, dword ptr [edi + 0xe0]', 'world matrix basis row 2'),
        (0x4dea7a, 'movd xmm0, dword ptr [edi + 0xe4]', 'row 2 y'),
        (0x4dea8e, 'movd xmm0, dword ptr [edi + 0xe8]', 'row 2 z')]),
    ('node flags +0x12c (bit 26: measure<1 rule)', 0x12c, 0x12c, 'handler', [
        (0x4961fc, 'cmp ecx, 1', 'measure < 1 ?'),
        (0x496201, 'mov eax, dword ptr [esi + 0x12c]', 'selector loads +0x12c'),
        (0x496207, 'test eax, 0x4000000', 'bit 26 keeps a sub-1 measure')]),
    ('node flags +0x130 (JET pair, stub)', 0x130, 0x130, 'stub', [
        (0x43deb3, 'or dword ptr [ecx + 0x130], 0x4000001', 'JET classifier writes the pair')]),
    ('node model id', 0x140, 0x144, 'handler', [
        (0x49621c, 'mov ecx, dword ptr [esi + 0x144]', 'selector model read after the verdict'),
        (0x4a365d, 'mov dword ptr [esi + 0x144], edi', 'model setter')]),
    ('node own threshold', 0x1d8, 0x1d8, 'handler', [
        (0x4961d6, 'mov edx, dword ptr [esi + 0x1d8]', 'selector own threshold at the site')]),
    ('parent threshold', 0x1d8, 0x1d8, 'handler', [
        (0x4961e0, 'mov eax, dword ptr [eax + 0x1d8]', 'selector parent threshold')]),
    ('node context (FL owned scale, new)', None, 0x1c, 'handler', [
        (0x495fc7, 'mov eax, dword ptr [esi + 0x1c]', 'selector loads the node context before the frustum test'),
        (0x495fce, 'movss xmm2, dword ptr [eax + 0x2c]', 'selector reads the context scale'),
        (0x4a5824, 'mov eax, dword ptr [eax + 0x1c]', 'reparent copies the parent context ...'),
        (0x4a5827, 'mov dword ptr [edi + 0x1c], eax', '... into the child')]),
    ('camera handle', 0x28, 0x28, 'handler', [
        (0x4a45d6, 'push 0x790', 'camera block 0x790 bytes (memset)'),
        (0x4a4606, 'mov dword ptr [esi + 0x28], eax', 'camera allocation stores the registered handle')]),
    ('camera context', 0x1c, 0x1c, 'handler', [
        (0x48ba48, 'mov ecx, dword ptr [esi + 0x1c]', 'sector scene: walker context = camera +0x1c'),
        (0x48ba4b, 'push esi', 'camera pushed'),
        (0x48ba4c, 'call 0x497720', 'walker'),
        (0x49772a, 'mov edi, dword ptr [esp + 0x14]', 'walker camera argument'),
        (0x497742, 'push edi', 'camera to the selector'),
        (0x497745, 'call 0x495e50', 'selector'),
        (0x4960d5, 'mov ebx, dword ptr [ebp + 8]', 'selector camera -> EBX (the stub pushes EBX)')]),
    ('context scale (float)', 0x2c, 0x2c, 'handler (FL) / consumer (AP)', [
        (0x48ba3f, 'mov ecx, dword ptr [esi + 0x1c]', 'camera publication context'),
        (0x495687, 'call 0x4ded90', 'ordinary setter, ECX = context'),
        (0x4deda3, 'mov esi, ecx', 'setter keeps the context in ESI'),
        (0x4def18, 'mulss xmm1, dword ptr [esi + 0x2c]', 'view translation x context scale')]),
    ('parent radius (plume floor)', 0xa4, 0xa4, 'consumer', [
        (0x4a39c3, 'mov eax, dword ptr [edi + 0xa4]', 'radius cache reads the cached subtree radius'),
        (0x4a3b1b, 'mov dword ptr [esi + 0xa4], 0xffffffff', 'scale setter marks it dirty'),
        (0x4a580f, 'mov dword ptr [ebp + 0xa4], 0xffffffff', 'reparent marks the new parent dirty')]),
]
AP_ANCHORS = [  # the AP twins of the two offsets that differ or are new
    (0x47d2ec, 'mov eax, dword ptr [edi + 0x140]', 'AP model read after the verdict (+0x140)'),
]
# Selector writes that would invalidate a copied field between entry and site.
COPIED = (0x18, 0x1c, 0x28, 0x70, 0x80, 0x84, 0x88, 0xb0, 0xb4, 0xb8, 0xc0, 0xc4, 0xc8, 0xe0, 0xe4, 0xe8, 0x144, 0x1d8)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', default='/mnt/c/X3FL/X3FL.exe')
    ap.add_argument('--ap', default='/mnt/c/X3AP-orig/X3AP.exe')
    args = ap.parse_args()
    fl, apimg = Image(args.exe), Image(args.ap)
    assert fl.sha256 == FL_SHA256 and apimg.sha256 == AP_SHA256, 'input hashes'
    failures, table = [], []
    for field, ap_off, fl_off, side, anchors in FIELDS:
        rows = []
        for va, text, role in anchors:
            try:
                fl.at(va, text)
                rows.append(dict(rva='%#08x' % (va - BASE), role=role, ok=True))
            except AssertionError as e:
                failures.append(str(e))
                rows.append(dict(rva='%#08x' % (va - BASE), role=role, ok=False))
        table.append(dict(field=field, ap_offset=None if ap_off is None else '%#x' % ap_off, fl_offset='%#x' % fl_off,
                          side=side, anchors=rows))
    for va, text, role in AP_ANCHORS:
        try:
            apimg.at(va, text)
        except AssertionError as e:
            failures.append(str(e))
    # Dominance of the selector's node-context load over the site.
    g = Cfg(fl, SEL, SEL_END)

    def reach(skip, start=SEL):
        seen, work = set(), [start]
        while work:
            a = work.pop()
            if a in seen or a == skip:
                continue
            seen.add(a)
            work += g.succ[a]
        return seen
    dominates = SITE in reach(None) and SITE not in reach(NODE_CONTEXT)
    if not dominates:
        failures.append('0x495fc7 does not dominate the site')
    # No selector store to a copied node field (ESI = node on every path: its only writer is 0x495e5e).
    # Every instruction Capstone reports as writing ESI or SI (pop esi, xchg, lea and mov included).
    esi_writers = [i.address for i in g.ins if {'esi', 'si'} & {i.reg_name(r) for r in i.regs_access()[1]}]
    stores = []
    for i in g.ins:
        m = re.match(r'^(?:byte|word|dword) ptr \[esi \+ (0x[0-9a-f]+)\], ', i.op_str)
        if m and int(m.group(1), 16) in COPIED and i.mnemonic not in ('cmp', 'test'):
            stores.append('%#08x' % (i.address - BASE))
    # ESI's other writers are the child loop's (0x495f27 / 0x495f3c): the site is not reachable from them.
    esi_reaching = [a for a in esi_writers if a != 0x495e5e and any(SITE in reach(None, t) for t in g.succ[a])]
    if 0x495e5e not in esi_writers or esi_reaching:
        failures.append('ESI writers reaching the site %s' % esi_reaching)
    if stores:
        failures.append('selector stores to copied fields at %s' % stores)
    calls_between = ['%#08x->%s' % (i.address - BASE, i.op_str) for i in g.ins
                     if i.mnemonic == 'call' and NODE_CONTEXT < i.address < SITE]
    result = dict(fl_sha256=fl.sha256[:16], ap_sha256=apimg.sha256[:16], fields=len(table),
                  calls_between_context_load_and_site=calls_between,
                  anchors=sum(len(t['anchors']) for t in table) + len(AP_ANCHORS),
                  selector_instructions=len(g.ins), node_context_dominates_site=dominates,
                  esi_writers=['%#08x' % (a - BASE) for a in esi_writers],
                  esi_writers_reaching_site=['%#08x' % (a - BASE) for a in esi_reaching], copied_field_stores=stores,
                  failures=failures, passed=not failures, table=table)
    OUT.write_text(json.dumps(result, indent=1) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'table'}))
    return 0 if not failures else 1


if __name__ == '__main__':
    sys.exit(main())
