#!/usr/bin/env python3
"""FL cull/LOD pass sites of the AP engine-side draw-reduction modules, verified from X3FL.exe bytes (2026-11-02).

Owning note: docs/reverse-engineering/fl/cull-chain.md, section "Verified 2026-11-02 against X3FL.exe".
Decodes every anchor with Capstone over tools/analysis/inspect_x3.py's PE32 reader (the Image class of
verification/results/fl-engine-effects/produce.py), asserts whole instructions and operand facts in X3FL.exe and
the AP twins in X3AP.exe, runs reaching-definition and stack-depth analyses over the FL selector's CFG, a direct
edge census of .text, a raw rel8/rel32 byte-position scan and an unaligned dword scan for every patched span,
and writes contract.json beside this script: RVAs, offsets, counts, booleans and window hashes only (no
executable bytes, no disassembly listing).

Window identity: bytes of [start, end) with every base-relocation HIGHLOW dword inside the window zeroed, hashed
FNV-1a 32 and SHA-256 (the fl-engine-effects convention). AP windows are also compared byte for byte with the
arrays in src/proxy (cull_small_parts_core.h, terran_lod_sites.h, lod_occlusion_sites.h, cull_census_core.h).

usage: python3 -I verification/results/fl-cull-chain/produce.py [--exe C:/X3FL/X3FL.exe] [--ap C:/X3/X3AP.exe] [--check]
"""
import argparse
import hashlib
import importlib.util
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
_spec = importlib.util.spec_from_file_location('fl_ee_produce', ROOT / 'verification/results/fl-engine-effects/produce.py')
_ee = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_ee)
Image, fnv1a32, BASE, FL_SHA256, AP_SHA256 = _ee.Image, _ee.fnv1a32, _ee.BASE, _ee.FL_SHA256, _ee.AP_SHA256
OUT = Path(__file__).resolve().parent / 'contract.json'

SEL, SEL_END = 0x495e50, 0x496446          # FL selector (thiscall ECX=node, [ebp+8]=camera, [ebp+0xc]=flag, ret 8)
AP_PASS, AP_PASS_END = 0x47cfe0, 0x47d552  # AP pass
CALLEE_POP = {0x4a39b0: 4, 0x495e50: 8}    # stack bytes the callee pops (RET n); every other selector callee: 0
REGS32 = ('eax', 'ebx', 'ecx', 'edx', 'esi', 'edi', 'ebp')
SUB = {'eax': ('al', 'ah', 'ax'), 'ebx': ('bl', 'bh', 'bx'), 'ecx': ('cl', 'ch', 'cx'), 'edx': ('dl', 'dh', 'dx'),
       'esi': ('si',), 'edi': ('di',), 'ebp': ('bp',)}


def h(v):
    return 'entry' if v == -1 else '%#08x' % v


def qword(start, length):
    return dict(start_rva=start - BASE, length=length, aligned_qword_rva=(start & ~7) - BASE,
                one_aligned_qword=(start & ~7) == ((start + length - 1) & ~7))


class Cfg:
    """Intraprocedural CFG of a linear-decoded function; calls fall through."""

    def __init__(self, img, start, end):
        self.ins = img.insns(start, end)
        self.by = {i.address: i for i in self.ins}
        self.succ, self.pred = {}, {a: [] for a in self.by}
        for k, i in enumerate(self.ins):
            nxt = self.ins[k + 1].address if k + 1 < len(self.ins) else None
            s = []
            if i.mnemonic.startswith('ret'):
                pass
            elif i.mnemonic == 'jmp':
                s.append(int(i.op_str, 16))
            elif i.mnemonic.startswith('j'):
                s += [int(i.op_str, 16), nxt]
            else:
                s.append(nxt)
            s = [t for t in s if t is not None]
            for t in s:
                assert t in self.by, f'edge {i.address:#x}->{t:#x} leaves the function or splits an instruction'
                self.pred[t].append(i.address)
            self.succ[i.address] = s

    def depth(self, origin):
        """ESP delta of every instruction start relative to the ESP right after `origin` executes."""
        d = {}
        work = [(self.succ[origin][0], 0)]
        while work:
            a, v = work.pop()
            if a in d:
                assert d[a] == v, f'inconsistent stack depth at {a:#x}'
                continue
            d[a] = v
            i = self.by[a]
            m, op = i.mnemonic, i.op_str
            if m == 'push':
                v -= 4
            elif m == 'pop':
                v += 4
            elif m == 'sub' and op.startswith('esp, '):
                v -= int(op[5:], 16)
            elif m == 'add' and op.startswith('esp, '):
                v += int(op[5:], 16)
            elif m == 'call':
                v += CALLEE_POP.get(int(op, 16), 0) if op.startswith('0x') else 0
            elif m == 'mov' and op == 'esp, ebp':
                continue  # epilogue: depth relative to EBP from here on (pop ebp; ret 8)
            elif m in ('mov', 'and') and op.startswith('esp'):
                raise AssertionError(f'esp rewritten at {a:#x}')
            for t in self.succ[a]:
                work.append((t, v))
        return d

    @staticmethod
    def writes(i, reg):
        """'kill' when i writes all of reg unconditionally, 'part' for a partial/conditional write, None otherwise."""
        if i.mnemonic == 'call':
            return 'kill' if reg in ('eax', 'ecx', 'edx') else None
        _, w = i.regs_access()
        names = {i.reg_name(r) for r in w}
        if reg in names:
            return 'part' if i.mnemonic.startswith('cmov') else 'kill'
        if names & set(SUB.get(reg, ())):
            return 'part'
        return None

    def reaching(self, site, reg):
        """Writers of reg that reach `site` (exclusive); 'entry' when a path reaches the function entry."""
        defs, seen, work = set(), set(), list(self.pred[site])
        if not self.pred[site] and site == self.ins[0].address:
            return ['entry']
        while work:
            a = work.pop()
            if a in seen:
                continue
            seen.add(a)
            w = self.writes(self.by[a], reg)
            if w:
                defs.add(a)
                if w == 'kill':
                    continue
            if a == self.ins[0].address:
                defs.add('entry')
            work += self.pred[a]
        return sorted((-1 if d == 'entry' else d - BASE) for d in defs)  # -1 = function entry (incoming value)

    def slot_writers(self, site, depth, slot):
        """Stores to the frame slot `slot` (relative to the origin ESP) that reach `site`."""
        def stores(i):
            if not i.operands or i.mnemonic in ('cmp', 'test', 'push'):
                return False
            o = i.operands[0]
            if o.type != 3 or i.reg_name(o.mem.base) != 'esp' or o.mem.index:  # X86_OP_MEM
                return False
            return depth[i.address] + o.mem.disp == slot
        defs, seen, work = set(), set(), list(self.pred[site])
        while work:
            a = work.pop()
            if a in seen:
                continue
            seen.add(a)
            if stores(self.by[a]):
                defs.add(a - BASE)
                continue
            if a == self.ins[0].address:
                defs.add(-1)  # a path with no store: the slot holds whatever the stack held
            work += self.pred[a]
        return sorted(defs)

    def node_offsets(self, node_reg, lo, hi):
        """Displacements of [node_reg+disp] memory operands in [lo, hi) with their read/write counts."""
        out = {}
        for i in self.ins:
            if not (lo <= i.address < hi):
                continue
            for o in i.operands:
                if o.type == 3 and i.reg_name(o.mem.base) == node_reg and not o.mem.index:
                    out.setdefault('%#x' % o.mem.disp, 0)
                    out['%#x' % o.mem.disp] += 1
        return dict(sorted(out.items(), key=lambda kv: int(kv[0], 16)))


def header_array(path, name):
    text = (ROOT / path).read_text()
    m = re.search(r'\b' + re.escape(name) + r'\[[^\]]*\]\s*=\s*\{([^}]*)\}', text)
    assert m, f'{name} not in {path}'
    return bytes(int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]+', m.group(1)))


def raw_scans(img, lo, hi):
    """Byte-position rel8/rel32 candidates and unaligned dwords (preferred base) landing in (lo, hi)."""
    start, end, off = img.text
    code = img.data[off:off + (end - start)]
    rel = 0
    for p in range(len(code)):
        b = code[p]
        va = start + p
        if b == 0xeb or 0x70 <= b <= 0x7f:
            t = va + 2 + struct.unpack_from('<b', code, p + 1)[0] if p + 1 < len(code) else None
            if t is not None and lo < t < hi:
                rel += 1
        if b in (0xe8, 0xe9) and p + 5 <= len(code):
            if lo < va + 5 + struct.unpack_from('<i', code, p + 1)[0] < hi:
                rel += 1
        if b == 0x0f and p + 6 <= len(code) and 0x80 <= code[p + 1] <= 0x8f:
            if lo < va + 6 + struct.unpack_from('<i', code, p + 2)[0] < hi:
                rel += 1
    dw = sum(1 for p in range(len(img.data) - 3) if lo < struct.unpack_from('<I', img.data, p)[0] < hi)
    return dict(raw_rel_candidates_into_interior=rel, unaligned_dwords_into_interior=dw)


def produce(fl_path, ap_path):
    fl, ap = Image(fl_path), Image(ap_path)
    assert fl.sha256 == FL_SHA256 and ap.sha256 == AP_SHA256
    c = dict(schema=1, producer='verification/results/fl-cull-chain/produce.py',
             inputs=dict(fl_sha256=fl.sha256, ap_sha256=ap.sha256, fl_dynamic_base=bool(fl.dll_characteristics & 0x40),
                         fl_highlow_fixups=len(fl.fixups)),
             convention='RVA = VA - 0x400000; window hash = FNV-1a 32 / SHA-256 of the window with relocated dwords zeroed')

    # ---- anchors: FL selector and AP pass, whole instructions --------------------------------------------------
    fl_anchors = [
        (0x495e50, 'push ebp'), (0x495e53, 'and esp, 0xfffffff8'), (0x495e56, 'sub esp, 0x1c'),
        (0x495e5e, 'mov esi, ecx'), (0x495e78, 'mov dword ptr [esi + 0x150], 0'),
        # Terran root flag (AP 0x47d00b..0x47d01e)
        (0x495e82, 'test al, al'), (0x495e84, 'jne 0x495ea1'), (0x495e86, 'cmp dword ptr [esi + 0x18], 0'),
        (0x495e8a, 'jne 0x495ea1'), (0x495e8c, 'cmp dword ptr [esi + 0x12c], 0'), (0x495e93, 'movzx eax, al'),
        (0x495e96, 'cmovl eax, ecx'), (0x495e99, 'mov dword ptr [esp + 0x1c], eax'),
        (0x495e9d, 'mov byte ptr [esp + 0x24], al'), (0x495ea1, 'mov eax, dword ptr [esi + 0x12c]'),
        (0x495e69, 'mov ecx, 1'), (0x495f2f, 'mov ebx, dword ptr [esp + 0x24]'), (0x495f33, 'push ebx'),
        (0x495f37, 'call 0x495e50'), (0x49628a, 'cmp byte ptr [esp + 0x1c], 0'), (0x49628f, 'je 0x496354'),
        # child loop / exit (AP 0x47d528)
        (0x495f24, 'mov edi, dword ptr [ebp + 8]'), (0x495f27, 'mov esi, dword ptr [esi + 0xc]'),
        (0x495f2a, 'cmp dword ptr [esi], 0'), (0x495f2d, 'je 0x495f43'), (0x495f49, 'ret 8'), (0x495ed0, 'ret 8'),
        (0x495f4e, 'and eax, 0x4000002'), (0x495f58, 'je 0x495f43'),
        (0x4960ad, 'cmp dword ptr [esi + 0x144], 0'), (0x4960c1, 'je 0x495f27'),
        (0x4960d5, 'mov ebx, dword ptr [ebp + 8]'), (0x4960f2, 'mov eax, dword ptr [ebx + 0x298]'),
        (0x496113, 'mov ecx, dword ptr [0x5aa100]'), (0x496119, 'mov edx, dword ptr [ecx + 0x5c]'),
        (0x49612c, 'mov ecx, dword ptr [esi + 0xa0]'), (0x496133, 'call 0x412b50'),
        (0x496142, 'mov dword ptr [esp + 0x14], eax'), (0x496163, 'mov edx, 0x280'),
        (0x496177, 'cmove eax, ecx'),
        # census measure site (AP 0x47d258)
        (0x49617a, 'mov ecx, dword ptr [esp + 0x14]'), (0x49617e, 'mov dword ptr [esp + 0x18], eax'),
        (0x496182, 'mov dword ptr [esp + 0x10], eax'), (0x496186, 'mov eax, dword ptr [esi + 0x1dc]'),
        (0x49618c, 'test eax, eax'),
        # shared cull site (AP 0x47d294..0x47d2cc)
        (0x4961af, 'test dword ptr [ebx + 0x270], 0x1000000'), (0x4961b9, 'je 0x4961d3'),
        (0x4961c5, 'cmp ecx, 0x14'), (0x4961c8, 'jge 0x4961d3'), (0x4961ca, 'xor ecx, ecx'),
        (0x4961cc, 'and dword ptr [esi + 0x12c], 0xfffffffd'),
        (0x4961d3, 'mov eax, dword ptr [esi + 0x18]'), (0x4961d6, 'mov edx, dword ptr [esi + 0x1d8]'),
        (0x4961dc, 'test eax, eax'), (0x4961de, 'je 0x4961eb'), (0x4961e0, 'mov eax, dword ptr [eax + 0x1d8]'),
        (0x4961e6, 'cmp eax, edx'), (0x4961e8, 'cmovg edx, eax'), (0x4961eb, 'test edx, edx'),
        (0x4961ed, 'jle 0x4961fc'), (0x4961ef, 'cmp ecx, edx'), (0x4961f1, 'jge 0x4961fc'),
        (0x4961f3, 'and dword ptr [esi + 0x12c], 0xfffffffd'), (0x4961fa, 'jmp 0x496201'),
        (0x4961fc, 'cmp ecx, 1'), (0x4961ff, 'jge 0x49621c'), (0x496201, 'mov eax, dword ptr [esi + 0x12c]'),
        (0x496207, 'test eax, 0x4000000'), (0x49620c, 'jne 0x49621c'), (0x496217, 'jmp 0x495f24'),
        (0x49621c, 'mov ecx, dword ptr [esi + 0x144]'), (0x496222, 'call 0x4a1a40'),
        (0x496227, 'mov dword ptr [esp + 0x14], eax'), (0x496246, 'movsx ebx, word ptr [eax + 0x10]'),
        (0x496306, 'mov dword ptr [esi + 0x150], ecx'), (0x496372, 'movd xmm0, dword ptr [eax + 0x34]'),
        (0x496425, 'or eax, 2'), (0x49642e, 'cmp ecx, 3'), (0x496431, 'jl 0x495f27'),
        (0x496437, 'or dword ptr [esi + 0x130], 0x100000'), (0x496441, 'jmp 0x495f27'),
    ]
    for va, text in fl_anchors:
        fl.at(va, text)
    ap_anchors = [
        (0x47d01c, 'je 0x47d023'), (0x47d258, 'mov eax, dword ptr [edi + 0x1dc]'), (0x47d2a2, 'mov ecx, dword ptr [edi + 0x18]'),
        (0x47d2c3, 'and dword ptr [edi + 0x12c], 0xfffffffd'), (0x47d2ec, 'mov eax, dword ptr [edi + 0x140]'),
        (0x47d528, 'mov edi, dword ptr [edi + 0xc]'), (0x47d1ef, 'mov eax, dword ptr [0x608518]'),
        (0x47e7a5, 'call 0x47cfe0'), (0x472466, 'mov eax, dword ptr [0x608518]'), (0x4c34f7, 'jne 0x4c35c6'),
        (0x441644, 'or dword ptr [eax + 0x12c], 0x80000000'),
    ]
    for va, text in ap_anchors:
        ap.at(va, text)
    # Material occlusion gate, projectile marker, JET writer, dock arithmetic, lens walk (FL)
    more = [
        (0x4e56b1, 'mov eax, dword ptr [ebp - 0x1c]'), (0x4e56b4, 'cmp dword ptr [eax + 0x150], 0'),
        (0x4e56bb, 'mov eax, dword ptr [0x5a9014]'), (0x4e56c0, 'jne 0x4e5731'), (0x4e56c2, 'mov dword ptr [ebp - 0x2c], eax'),
        (0x4e56c5, 'mov eax, dword ptr [ebp - 0xa0]'), (0x4e56cb, 'test eax, eax'),
        (0x4e5731, 'push -1'), (0x4e5733, 'push dword ptr [eax + 0x34]'),
        (0x449b1d, 'mov dword ptr [ebp - 0x20], 0x20800000'), (0x44a6b9, 'test cx, cx'), (0x44a6bc, 'jne 0x44a815'),
        (0x44a807, 'mov eax, dword ptr [esi + 0x70]'), (0x44a80a, 'mov ecx, dword ptr [ebp - 0x20]'),
        (0x44a80d, 'or dword ptr [eax + 0x130], ecx'),
        (0x43deb3, 'or dword ptr [ecx + 0x130], 0x4000001'),
        (0x44ab6a, 'or dword ptr [edx + 0x12c], 0x80000000'),  # Terran root bit 31 (AP 0x441644)
        (0x4ae169, 'imul ecx, dword ptr [ebp - 0x58], 0x186a0'), (0x4ae175, 'add ecx, 0xfffe7960'),
        (0x4ad62d, 'imul eax, dword ptr [ebp - 0x70], 0x186a0'), (0x4ad634, 'sub eax, 0x186a0'),
        (0x48bc8c, 'mov ecx, dword ptr [0x5aa100]'), (0x48bc93, 'mov ecx, dword ptr [ecx + 0x64]'),
        (0x48bc96, 'call 0x497720'), (0x497740, 'push 0'), (0x497742, 'push edi'), (0x497743, 'mov ecx, esi'),
        (0x497745, 'call 0x495e50'),
    ]
    for va, text in more:
        fl.at(va, text)
    c['anchors'] = dict(fl=len(fl_anchors) + len(more), ap=len(ap_anchors))

    # ---- windows ------------------------------------------------------------------------------------------------
    def win(img, s, e):
        w = img.window(s, e)
        w['relocated_targets_rva'] = [h(struct.unpack_from('<I', img.read(BASE + r, 4))[0] - BASE)
                                      for r in w['relocated_dword_rvas']]
        w['relocated_dword_rvas'] = [h(r) for r in w['relocated_dword_rvas']]
        w['start_rva'], w['end_rva'] = h(w['start_rva']), h(w['end_rva'])
        return w
    windows = {
        'shared_cull_window': ((0x4961c5, 0x4961fc), (0x47d294, 0x47d2cc), ('src/proxy/cull_small_parts_core.h', 'window')),
        'size_verdict': ((0x4961d3, 0x49621c), (0x47d2a2, 0x47d2ec), None),
        'terran_root_flag': ((0x495e82, 0x495ea1), (0x47d012, 0x47d023), ('src/proxy/terran_lod_sites.h', 'expected_window')),
        'lod_occlusion_gate': ((0x4e56b1, 0x4e56c5), (0x4c34e7, 0x4c3506), ('src/proxy/lod_occlusion_sites.h', 'expected_window')),
        'census_measure': ((0x49617a, 0x496190), (0x47d248, 0x47d267), ('src/proxy/cull_census_core.h', 'measure_window')),
        'census_exit_head': ((0x495f24, 0x495f2f), (0x47d519, 0x47d534), ('src/proxy/cull_census_core.h', 'exit_window')),
        'census_exit_tail': ((0x49642e, 0x496446), None, None),
        'projectile_store': ((0x449b1d, 0x449b24), (0x4401ae, 0x4401b6), ('src/proxy/cull_small_parts_core.h', 'marker_store')),
        'projectile_or': ((0x44a807, 0x44a813), (0x44123b, 0x441248), ('src/proxy/cull_small_parts_core.h', 'marker_or')),
        'jet_writer': ((0x43deb3, 0x43debd), (0x434708, 0x434712), ('src/proxy/cull_small_parts_core.h', 'jet_writer')),
        'dock_cut_text': ((0x4ae169, 0x4ae17d), None, None),
        'dock_cut_binary': ((0x4ad62d, 0x4ad63c), None, None),
        'lens_scene_walk': ((0x48bc8c, 0x48bc9b), (0x472466, 0x472476), None),
        'walker_selector_call': ((0x497740, 0x49774a), (0x47e7a0, 0x47e7aa), None),
        'model_read': ((0x49621c, 0x496222), (0x47d2ec, 0x47d2f2), None),
    }
    c['windows'] = {}
    for name, (f, a, hdr) in windows.items():
        rec = dict(fl=win(fl, *f))
        if a:
            rec['ap'] = win(ap, *a)
            if hdr:
                expect = header_array(*hdr)
                got = ap.read(a[0], a[1] - a[0])
                assert got == expect, f'{name}: AP bytes differ from {hdr}'
                rec['ap']['equals_source_array'] = hdr[0] + ':' + hdr[1]
        c['windows'][name] = rec
    assert c['windows']['size_verdict']['fl']['fnv1a32'] == '8f680dbc'  # engine-effects.md, same window

    # ---- CFG facts in the FL selector ---------------------------------------------------------------------------
    g = Cfg(fl, SEL, SEL_END)
    depth = g.depth(0x495e56)  # origin: ESP after `sub esp,0x1c` (= align_down(EBP,8) - 0x1c)
    epilogue = {i.address for k, i in enumerate(g.ins) if k and g.ins[k - 1].mnemonic == 'mov' and g.ins[k - 1].op_str == 'esp, ebp'}
    epilogue |= {g.ins[k + 1].address for k, i in enumerate(g.ins) if i.address in epilogue and k + 1 < len(g.ins)}
    unreached = [a - BASE for a in g.by if a not in depth and a > 0x495e56 and a not in epilogue]
    assert not unreached, unreached
    sites = dict(cull=0x4961d3, measure=0x496186, measure_alt=0x496182, exit=0x495f27, cull_target=0x4961f3,
                 terran=0x495e96)
    frame = {}
    for name, s in sites.items():
        d = depth[s]
        frame[name] = dict(rva=s - BASE, esp_minus_aligned_ebp=d - 0x1c,
                           reaching={r: g.reaching(s, r) for r in REGS32})
    c['selector'] = dict(start_rva=SEL - BASE, end_rva=SEL_END - BASE, instructions=len(g.ins),
                         ret8_rvas=[h(i.address - BASE) for i in g.ins if i.mnemonic == 'ret' and i.op_str == '8'],
                         x87_instructions=sum(1 for i in g.ins if i.mnemonic.startswith('f')),
                         sites=frame)
    # facts the stubs rely on (asserted, not just recorded)
    cull = frame['cull']['reaching']
    assert frame['cull']['esp_minus_aligned_ebp'] == -0x28
    assert cull['esi'] == [0x495e5e - BASE] and cull['ebx'] == [0x4960d5 - BASE]
    assert cull['ecx'] == [0x49617a - BASE, 0x4961ca - BASE]           # native measure (or 0 in env-map views)
    assert set(cull['edi']) == {0x4960cd - BASE, 0x4960d8 - BASE, 0x4960e3 - BASE, 0x49610d - BASE}  # D
    ms = frame['measure']['reaching']
    assert ms['ecx'] == [0x49617a - BASE] and ms['esi'] == [0x495e5e - BASE] and ms['ebx'] == [0x4960d5 - BASE]
    assert frame['measure']['esp_minus_aligned_ebp'] == -0x28 and frame['exit']['esp_minus_aligned_ebp'] == -0x28
    ex = frame['exit']['reaching']
    assert ex['esi'] == [0x495e5e - BASE]
    slot = {k: depth[sites['cull']] + k for k in (0x10, 0x14, 0x18, 0x1c, 0x24)}
    c['selector']['slots_at_cull_site'] = {
        '[esp+%#x]' % k: dict(writers_rva=g.slot_writers(sites['cull'], depth, v)) for k, v in slot.items()}
    assert c['selector']['slots_at_cull_site']['[esp+0x10]']['writers_rva'] == [0x496182 - BASE]   # s
    assert c['selector']['slots_at_cull_site']['[esp+0x18]']['writers_rva'] == [0x49617e - BASE]   # s copy
    assert c['selector']['slots_at_cull_site']['[esp+0x14]']['writers_rva'] == [0x496142 - BASE]   # native measure
    # The exit: [esp+0x14] holds the model pointer only on paths through 0x496227
    c['selector']['slots_at_exit'] = {'[esp+0x14]': g.slot_writers(sites['exit'], depth, depth[sites['exit']] + 0x14),
                                      '[esp+0x10]': g.slot_writers(sites['exit'], depth, depth[sites['exit']] + 0x10)}
    c['selector']['node_offsets_fl_esi'] = g.node_offsets('esi', 0x495e5e, 0x495f24) | g.node_offsets('esi', 0x495f4c, SEL_END)
    ga = Cfg(ap, AP_PASS, AP_PASS_END)
    c['selector']['node_offsets_ap_edi'] = ga.node_offsets('edi', 0x47cfeb, 0x47d528)
    fo, ao = c['selector']['node_offsets_fl_esi'], c['selector']['node_offsets_ap_edi']
    assert '0x144' in fo and '0x150' in fo and '0x140' not in fo and '0x14c' not in fo
    assert '0x140' in ao and '0x14c' in ao and '0x144' not in ao and '0x150' not in ao
    for k in ('0x18', '0x12c', '0x130', '0x1d8', '0x1dc', '0xa0', '0xf0', '0xf8'):
        assert k in fo and k in ao, k

    for f in frame.values():
        f['reaching'] = {r: [h(v) for v in d] for r, d in f['reaching'].items()}
    c['ap_has_relocations'] = bool(ap.fixups)
    def bit31_writers(img):
        st, en, off = img.text
        return [h(st + q.start() - BASE) for q in re.finditer(rb'\x81[\x88-\x8f]\x2c\x01\x00\x00\x00\x00\x00\x80',
                                                                img.data[off:off + en - st])]
    c['terran_bit31_or_writers'] = dict(fl=bit31_writers(fl), ap=bit31_writers(ap))
    assert c['terran_bit31_or_writers']['fl'] == [h(0x44ab6a - BASE)]
    for k, v in c['selector']['slots_at_cull_site'].items():
        v['writers_rva'] = [h(x) for x in v['writers_rva']]
    c['selector']['slots_at_exit'] = {k: [h(x) for x in v] for k, v in c['selector']['slots_at_exit'].items()}
    for f in frame.values():
        f['rva'] = h(f['rva'])

    # ---- edges ----------------------------------------------------------------------------------------------------
    edges = fl.direct_edges()
    c['edge_census'] = dict(fl_direct_edges=len(edges))

    def into(lo, hi):
        return [[h(a), h(t)] for a, t in sorted({(a - BASE, t - BASE) for a, m, t in edges if lo <= t < hi})]
    c['entries'] = {
        'cull_site_0x4961d3': into(0x4961d3, 0x4961d4),
        'cull_span_interior': into(0x4961d4, 0x4961dc),
        'cull_target_0x4961f3': into(0x4961f3, 0x4961f4),
        'measure_span_interior_0x496186': into(0x496187, 0x49618c),
        'measure_alt_span_interior_0x496182': into(0x496183, 0x49618c),
        'measure_site_0x496182_0x496186': into(0x496182, 0x496183) + into(0x496186, 0x496187),
        'exit_site_0x495f27': into(0x495f27, 0x495f28),
        'exit_span_interior': into(0x495f28, 0x495f2d),
        'exit_0x495f24': into(0x495f24, 0x495f25),
        'terran_cmov_interior': into(0x495e97, 0x495e99),
        'occlusion_jne_interior': into(0x4e56c1, 0x4e56c2),
        'occlusion_placeholder_0x4e5731': into(0x4e5731, 0x4e5732),
    }
    for k in ('cull_span_interior', 'measure_span_interior_0x496186', 'measure_alt_span_interior_0x496182',
              'exit_span_interior', 'terran_cmov_interior', 'occlusion_jne_interior'):
        assert not c['entries'][k], (k, c['entries'][k])
    assert c['entries']['occlusion_placeholder_0x4e5731'] == [[h(0x4e56c0 - BASE), h(0x4e5731 - BASE)]]
    assert {a for a, _ in c['entries']['cull_site_0x4961d3']} == {h(0x4961b9 - BASE), h(0x4961c8 - BASE)}
    c['raw_scans'] = {
        'cull_span': raw_scans(fl, 0x4961d3, 0x4961dc), 'measure_span_0x496186': raw_scans(fl, 0x496186, 0x49618c),
        'measure_span_0x496182': raw_scans(fl, 0x496182, 0x49618c), 'exit_span': raw_scans(fl, 0x495f27, 0x495f2d),
        'terran_cmov': raw_scans(fl, 0x495e96, 0x495e99), 'occlusion_jne': raw_scans(fl, 0x4e56c0, 0x4e56c2)}

    def callers(t):
        return [h(a) for a in sorted({a - BASE for a, m, x in edges if x == t and m == 'call'})]
    c['callers'] = {name: callers(t) for name, t in (
        ('selector_0x495e50', 0x495e50), ('scene_walker_0x497720', 0x497720), ('model_lookup_0x4a1a40', 0x4a1a40),
        ('measure_helper_0x412b50', 0x412b50), ('frustum_0x4e9000', 0x4e9000), ('radius_cache_0x4a39b0', 0x4a39b0))}
    assert c['callers']['selector_0x495e50'] == [h(0x495f37 - BASE), h(0x497745 - BASE)]
    c['caller_counts'] = {k: len(v) for k, v in c['callers'].items()}
    for k in ('measure_helper_0x412b50', 'model_lookup_0x4a1a40', 'radius_cache_0x4a39b0'):
        c['callers'][k] = 'count only (see caller_counts)'

    # ---- patch geometry -----------------------------------------------------------------------------------------
    c['writes'] = {
        'cull_jmp5_0x4961d3': qword(0x4961d3, 5),
        'census_measure_jmp5_0x496186': qword(0x496186, 5),
        'census_measure_jmp5_0x496182': qword(0x496182, 5),
        'census_exit_jmp5_0x495f27': qword(0x495f27, 5),
        'terran_modrm_0x495e98': qword(0x495e98, 1),
        'occlusion_rel8_0x4e56c1': qword(0x4e56c1, 1),
        'ap_cull_jmp5_0x47d2a2': qword(0x47d2a2, 5), 'ap_measure_0x47d258': qword(0x47d258, 5),
        'ap_exit_0x47d528': qword(0x47d528, 5), 'ap_terran_0x47d01c': qword(0x47d01c, 2),
        'ap_occlusion_rel32_0x4c34f9': qword(0x4c34f9, 4)}
    for v in c['writes'].values():
        v['start_rva'], v['aligned_qword_rva'] = h(v['start_rva']), h(v['aligned_qword_rva'])
    # the one-byte edits decode to the intended instruction without moving a boundary
    t = bytearray(fl.read(0x495e96, 3)); t[2] = 0xc0
    i = next(fl.md.disasm(bytes(t), 0x495e96)); assert (i.mnemonic, i.op_str, i.size) == ('cmovl', 'eax, eax', 3)
    o = bytearray(fl.read(0x4e56c0, 2)); assert o[1] == 0x6f; o[1] = 0
    i = next(fl.md.disasm(bytes(o), 0x4e56c0)); assert (i.mnemonic, i.op_str, i.size) == ('jne', '0x4e56c2', 2)
    c['edits'] = dict(terran=dict(rva=0x495e98 - BASE, before=0xc1, after=0xc0, result='cmovl eax, eax'),
                      occlusion=dict(rva=0x4e56c1 - BASE, before=0x6f, after=0x00, result='jne to fall-through 0x4e56c2'))
    return c


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', default='/mnt/c/X3FL/X3FL.exe')
    ap.add_argument('--ap', default='/mnt/c/X3/X3AP.exe')
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args()
    text = json.dumps(produce(a.exe, a.ap), indent=1, sort_keys=True) + '\n'
    if a.check:
        same = OUT.exists() and OUT.read_text() == text
        print('contract.json', 'current' if same else 'differs')
        return 0 if same else 1
    OUT.write_text(text)
    print('wrote', OUT.relative_to(ROOT))
    return 0


if __name__ == '__main__':
    sys.exit(main())
