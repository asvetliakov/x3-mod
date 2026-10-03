#!/usr/bin/env python3
"""Static per-region slot tally of the plume pixel program (src/effects/engine_plume_ps.hlsl, embedded as
src/renderer/engine_plume_pixel_program_inc.h; docs/architecture/engine-exhaust-look-critique.md section 6, "Review
fixes"). Walks the ps_3_0 token stream with the production counter's cost table (src/renderer/ps3_program_slots.h:
sincos 8; rep, nrm, pow 3; crs, dp2add, lrp, dsx, dsy, texldl 2; dcl, def, defi, defb and comments 0; everything else
1) and splits it at the outermost if / else / endif (the `[branch] if (disc)`): shared (before the if, after the endif,
and the three flow-control tokens), the disc branch (if .. else) and the axial branch (else .. endif). A pixel runs the
shared slots plus one branch. The total equals the pass's ps3_program_slots (the fixture's ATTACH ps_slots).
Usage: python3 plume_slot_tally.py [header]; the output is plume_slot_tally_out.txt.
"""
import collections
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..")
HEADER = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "src", "renderer", "engine_plume_pixel_program_inc.h")
COST = {31: 0, 48: 0, 81: 0, 46: 0, 37: 8, 38: 3, 36: 3, 32: 3, 33: 2, 90: 2, 18: 2, 91: 2, 92: 2, 95: 2}
NAMES = {1: "mov", 2: "add", 3: "sub", 4: "mad", 5: "mul", 6: "rcp", 7: "rsq", 8: "dp3", 9: "dp4", 10: "min", 11: "max",
         12: "slt", 13: "sge", 14: "exp", 15: "log", 18: "lrp", 19: "frc", 32: "pow", 35: "abs", 36: "nrm", 37: "sincos",
         40: "if", 41: "ifc", 42: "else", 43: "endif", 44: "break", 45: "breakc", 66: "texld", 79: "setp", 80: "cmp",
         88: "cmp", 90: "dp2add", 91: "dsx", 92: "dsy", 95: "texldl", 30: "label", 77: "texkill", 87: "sgn"}
IF_OPS, ELSE_OP, ENDIF_OP = (40, 41), 42, 43


def words(path):
    text = open(path).read()
    return [int(w, 16) for w in re.findall(r"0x([0-9a-fA-F]{8})u", text)]


def tally(w):
    assert w[0] == 0xFFFF0300 and w[-1] == 0x0000FFFF, "not a ps_3_0 program"
    regions = collections.OrderedDict((k, [0, 0, collections.Counter()]) for k in ("shared", "disc", "axial"))
    depth, region, i = 0, "shared", 1
    while i < len(w) - 1:
        token = w[i]
        if token & 0xFFFF == 0xFFFE:  # comment
            i += 1 + ((token >> 16) & 0x7FFF)
            continue
        op, operands = token & 0xFFFF, (token >> 24) & 15
        cost = COST.get(op, 1)
        here = region
        if op in IF_OPS:
            if depth == 0 and region == "shared":
                region, here = "disc", "shared"
            depth += 1
        elif op == ELSE_OP and depth == 1 and region == "disc":
            region, here = "axial", "shared"
        elif op == ENDIF_OP:
            depth -= 1
            if depth == 0 and region in ("disc", "axial"):
                region, here = "shared", "shared"
        r = regions[here]
        r[0] += cost
        r[1] += 1 if cost else 0
        r[2][NAMES.get(op, str(op))] += cost
        i += 1 + operands
    assert i == len(w) - 1, "token walk did not end on the end token"
    return regions


if __name__ == "__main__":
    w = words(HEADER)
    regions = tally(w)
    total = sum(r[0] for r in regions.values())
    print(f"# {os.path.relpath(HEADER, ROOT)}: {len(w)} words, {total} slots (ps3_program_slots)")
    print("region | slots | instructions | largest contributors (slots)")
    for name, (slots, count, ops) in regions.items():
        top = ", ".join(f"{k} {v}" for k, v in ops.most_common(8))
        print(f"{name} | {slots} | {count} | {top}")
    s, d, a = (regions[k][0] for k in ("shared", "disc", "axial"))
    print(f"# a disc pixel runs {s + d} slots, an axial pixel {s + a}")
