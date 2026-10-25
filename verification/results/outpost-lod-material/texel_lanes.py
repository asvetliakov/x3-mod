#!/usr/bin/env python3
"""Which lanes of each sampled texel a hull pixel program reads (docs/architecture/overlay-material-strength.md):
for every ps_*.bin given, the CTAB sampler names, each texld's destination register, and the set of lanes of that
register read afterwards before the lane is overwritten at the texld's flow-control depth (a write inside a deeper
if/else block does not kill the lane). A lane is 'free' for the design when the program never reads it: the baker
may encode the diffuse-strength ratio there. Also prints the exposure-seed join (material_exposure_profiles_inc.h)
of the program, when it has one, with the instruction index and the register whose lanes are read there.
Usage (repo root): python3 texel_lanes.py ps_*.bin  (the bins are the user's local captures; nothing is written)"""
import re, struct, sys
sys.path.insert(0, 'tools/analysis')
sys.path.insert(0, 'verification/results/hull-widen-native-validator')
import sm3_rules
from shader_constants import parse_ctab

SEEDS = {int(h, 16): (int(at), int(lanes), prod == 'true') for h, at, lanes, prod in re.findall(
    r'\{0x([0-9a-f]{16})ull,(\d+),(\d+),(true|false)\}', open('src/renderer/material_exposure_profiles_inc.h').read())}
PER_COMPONENT = {1, 2, 3, 4, 5, 18, 23, 11, 10, 12, 13, 36, 16, 17}  # mov add sub mad mul lrp max min slt sge frc dsx dsy ...
XYZ = {8, 36, 33}       # dp3 nrm crs
XYZW = {9}              # dp4
REPL = {6, 7, 14, 15, 32, 37}   # rcp rsq exp log pow sincos (first comp)
TEX = {66, 93, 95}
IF_OPS = {40, 41}; ELSE = 42; ENDIF = 43


def rtype(t):
    return ((t >> 28) & 7) | ((t >> 8) & 0x18)


def reads(op, dst, srcs):
    """[(regtype, regnum, {lanes read})] for one instruction."""
    out = []
    mask = (dst >> 16) & 0xf if dst is not None else 0xf
    for i, s in enumerate(srcs):
        swz = [(s >> (16 + 2 * k)) & 3 for k in range(4)]
        if op in PER_COMPONENT or op == 25 or op == 26:    # per-component; 25/26 = if/ifc read .x anyway
            lanes = {swz[k] for k in range(4) if mask >> k & 1}
        elif op in XYZ:
            lanes = {swz[0], swz[1], swz[2]}
        elif op == 90:                                       # dp2add: a.xy, b.xy, c.x
            lanes = {swz[0], swz[1]} if i < 2 else {swz[0]}
        elif op in REPL:
            lanes = {swz[0]}
        elif op in TEX:
            lanes = {swz[0], swz[1], swz[2], swz[3]} if i == 0 else set()
        else:
            lanes = {swz[0], swz[1], swz[2], swz[3]}
        out.append((rtype(s), s & 0x7ff, lanes))
    return out


def analyse(path):
    code = open(path, 'rb').read()
    words = list(struct.unpack(f'<{len(code) // 4}I', code))
    h = path.rsplit('ps_', 1)[1][:16]
    if words[0] != 0xffff0300:
        print(f'{h}\twords={len(words)}\tnot ps_3_0 ({words[0]:08x}), skipped')
        return
    try:
        samplers = {c['register']: c['name'] for c in parse_ctab(code) if c.get('register_set') == 3}
    except Exception:
        samplers = {}
    ins = [(at, op, p) for at, op, p in sm3_rules.parse(words) if op not in (81, 31, 'end')]
    body = [(idx, at, op, p) for idx, (at, op, p) in enumerate(ins)]
    depth = 0
    depth_at = []
    for idx, at, op, p in body:
        if op == ENDIF or op == ELSE:
            depth -= 1
        depth_at.append(depth)
        if op in IF_OPS or op == ELSE:
            depth += 1
    rows = []
    for idx, at, op, p in body:
        if op not in TEX:
            continue
        dst = p[0]; dreg = dst & 0x7ff
        if rtype(dst) != 0:
            continue
        sreg = p[2] & 0x7ff
        live = {0, 1, 2, 3}; used = set()
        for jdx in range(idx + 1, len(body)):
            _, at2, op2, p2 = body[jdx]
            has_dst = op2 not in sm3_rules.NO_DST and p2
            d2 = p2[0] if has_dst else None
            for t, n, lanes in reads(op2, d2, p2[1:] if has_dst else p2):
                if t == 0 and n == dreg:
                    used |= lanes & live
            if has_dst and rtype(d2) == 0 and (d2 & 0x7ff) == dreg and depth_at[jdx] <= depth_at[idx]:
                live -= {k for k in range(4) if (d2 >> 16) & (1 << k)}
            if not live:
                break
        rows.append((idx, sreg, dreg, used))
    seed = SEEDS.get(int(h, 16))
    seed_txt = '-'
    if seed:
        at, lanes, prod = seed
        m = [(idx, op, p) for idx, a, op, p in body if a == at]
        if m:
            idx, op, p = m[0]
            seed_txt = f'join#{idx} {sm3_rules.disasm(op, p)} ({"product" if prod else "addend"})'
        else:
            seed_txt = f'join at {at} not on a boundary'
    print(f'{h}\twords={len(words)}\t{seed_txt}')
    for idx, sreg, dreg, used in rows:
        name = samplers.get(sreg, '?')
        print(f'\ts{sreg} {name}\ttexld#{idx} -> r{dreg}\tlanes read {"".join("xyzw"[k] for k in sorted(used)) or "-"}')


for f in sys.argv[1:]:
    analyse(f)
