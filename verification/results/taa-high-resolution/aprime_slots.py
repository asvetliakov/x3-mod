#!/usr/bin/env python3
"""A' re-baseline (docs/architecture/taa-plan-lifted-slot-cap.md step 1): instruction counts of the TAA resolve and box
programs before (ee3bbf88, S3) and after, from the embedded headers. Host only; the fixture's RESOLVE_BUDGET rows (D3DX's
"instruction slots used") are the slot record, this is the executed-work estimate beside them.

Per program: `slots` with src/renderer/ps3_program_slots.h's table (a different table from D3DX's, so not the budget figure),
`outside` = instructions outside every loop (each executes at most once per pixel), and the two paths of the history lookup:
the rest branch is the `if` whose then-block fetches s2 (the point read) and whose else-block fetches s11 (the 5-tap bilinear
history). `rest` = outside minus the else-block, `moving` = outside minus the then-block (a program without the branch runs
the same straight line on both). Loops (3x3 dilation, 2x2 depth proof, 3x3 clip) are the same code before and after, so the
deltas of these straight-line counts are the executed-instruction deltas per pixel on the path that reaches the blend
(early returns and the other data-dependent branches are the same in both builds).

usage: aprime_slots.py [BASE_REVISION]   (default ee3bbf88)
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BASE = sys.argv[1] if len(sys.argv) > 1 else 'ee3bbf88'
PROGRAMS = ['temporal_resolve', 'temporal_resolve_thin', 'temporal_resolve_age', 'temporal_resolve_far', 'temporal_resolve_far_camera',
            'temporal_resolve_far_camera_hold', 'temporal_thin_box', 'temporal_thin_box_hold', 'temporal_thin_box_rows',
            'temporal_thin_box_rows_hold', 'temporal_thin_box_columns', 'temporal_thin_box_columns_hold', 'temporal_line_mask_camera',
            'temporal_resolve_far_camera_taps16']
COST = {37: 8, 38: 3, 36: 3, 32: 3, 33: 2, 90: 2, 18: 2, 91: 2, 92: 2, 95: 2}
FREE = {31, 48, 81, 46}
LOOP_OPEN, LOOP_CLOSE = {27, 38}, {29, 39}
IF_OPEN, ELSE, ENDIF = {40, 41}, 42, 43
TEX = {66, 95, 93, 94}


def words(text):
    return [int(w, 16) for w in re.findall(r'0x([0-9a-f]{8})u', text)]


def sampler_of(ws, i, operands):
    # texld / texldl: dst, src0, sampler (the last operand); register type 10 = sampler
    token = ws[i + operands]
    kind = ((token >> 28) & 7) | ((token >> 8) & 0x18)
    return token & 0x7ff if kind == 10 else None


def analyse(ws):
    if not ws or ws[0] != 0xffff0300 or ws[-1] != 0xffff:
        return None
    slots = outside = texture = loop_depth = 0
    blocks, stack = [], []  # block: dict(then=, else_=, then_s=set, else_s=set)
    i = 1
    while i < len(ws) - 1:
        token = ws[i]
        if token & 0xffff == 0xfffe:
            i += 1 + ((token >> 16) & 0x7fff)
            continue
        op, operands = token & 0xffff, (token >> 24) & 15
        if op not in FREE:
            slots += COST.get(op, 1)
            if op in LOOP_OPEN:
                loop_depth += 1
            counted = loop_depth == 0 and op not in LOOP_CLOSE
            if counted:
                outside += 1
            sampler = sampler_of(ws, i, operands) if op in TEX else None
            texture += op in TEX
            for block in stack:  # every enclosing if counts it in its current part
                part = 'else_' if block['in_else'] else 'then'
                if counted and not (block is stack[-1] and op in (ELSE, ENDIF)):
                    block[part] += 1
                if sampler is not None:
                    block[part + '_s'].add(sampler)
            if op in IF_OPEN:
                stack.append(dict(then=0, else_=0, then_s=set(), else__s=set(), in_else=False, loop=loop_depth))
            elif op == ELSE:
                stack[-1]['in_else'] = True
            elif op == ENDIF:
                blocks.append(stack.pop())
            if op in LOOP_CLOSE:
                loop_depth -= 1
        i += 1 + operands
    rest = [b for b in blocks if b['loop'] == 0 and 2 in b['then_s'] and 11 in b['else__s']]
    then_n, else_n = (rest[0]['then'], rest[0]['else_']) if rest else (0, 0)
    return dict(slots=slots, outside=outside, texture=texture, rest=outside - else_n, moving=outside - then_n, branch=bool(rest))


def header(name, revision=None):
    path = 'src/renderer/%s_program_inc.h' % name
    if revision is None:
        p = ROOT / path
        return p.read_text() if p.exists() else None
    r = subprocess.run(['git', 'show', '%s:%s' % (revision, path)], cwd=ROOT, capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else None


print('program slots_before slots_after outside_before outside_after rest_before rest_after rest_delta moving_before moving_after moving_delta rest_branch_after')
for name in PROGRAMS:
    before, after = header(name, BASE), header(name)
    b = analyse(words(before)) if before else None
    a = analyse(words(after)) if after else None
    f = lambda d, k: str(d[k]) if d else '-'
    dl = lambda k: str(a[k] - b[k]) if a and b else '-'
    print(name, f(b, 'slots'), f(a, 'slots'), f(b, 'outside'), f(a, 'outside'), f(b, 'rest'), f(a, 'rest'), dl('rest'), f(b, 'moving'), f(a, 'moving'), dl('moving'), f(a, 'branch'))
