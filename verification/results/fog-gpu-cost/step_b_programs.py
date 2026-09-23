#!/usr/bin/env python3
"""Fog route step B (docs/architecture/fog-gpu-cost.md): every fog program against the base commit b4e2fcff (bytecode
identical or changed; ps_3_0 slots / texture instructions / loops, and the rep count of the one march loop), and each
24-far-bin program against its 40-bin default.
Usage: /usr/bin/python3 verification/results/fog-gpu-cost/step_b_programs.py"""
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import fog_density_shader_slots as slots  # noqa: E402

BASE = 'b4e2fcff'
DEFI = 48  # defi: the integer constant a rep loop counts with


def at_base(path):
    done = subprocess.run(['git', '-C', str(ROOT), 'show', f'{BASE}:{path}'], capture_output=True, text=True)
    return done.stdout if done.returncode == 0 else None


def rep_counts(words):
    """The x of every defi i#, x, y, z (the loop trip counts)."""
    counts, i = [], 1
    while i < len(words) - 1:
        token = words[i]
        if token & 0xffff == 0xfffe:
            i += 1 + ((token >> 16) & 0x7fff)
            continue
        if token & 0xffff == DEFI:
            counts.append(words[i + 2])
        i += 1 + ((token >> 24) & 15)
    return counts


def main():
    for name, header in slots.PROGRAMS.items():
        provenance = ROOT / f"verification/results/{name.replace('_', '-')}-program.json"
        new = json.loads(provenance.read_text()); words_new = slots.words_of(header); b = slots.count(words_new)
        old_text = at_base(provenance.relative_to(ROOT))
        if old_text is None:
            print(f"{name:30s} NEW slots {b['slots']:3d} tex {b['texture_instructions']:2d} loops {b['loops']} rep {rep_counts(words_new)} below_512={b['slots'] < 512}")
            continue
        old = json.loads(old_text)
        words_old = [int(w, 16) for w in re.findall(r'0x([0-9a-f]{8})u', at_base(header.relative_to(ROOT)))]
        a = slots.count(words_old)
        same = new['bytecode_sha256'] == old['bytecode_sha256']
        print(f"{name:30s} bytecode={'identical' if same else 'changed  '} slots {a['slots']:3d} -> {b['slots']:3d} "
              f"tex {a['texture_instructions']:2d} -> {b['texture_instructions']:2d} loops {a['loops']} -> {b['loops']} rep {rep_counts(words_new)}")
    for name, header in slots.FAR24_PROGRAMS.items():
        base = name.replace('_far24', '')
        a, b = slots.count(slots.words_of(slots.PROGRAMS[base])), slots.count(slots.words_of(header))
        print(f"{name:30s} vs {base}: slots {a['slots']} -> {b['slots']} tex {a['texture_instructions']} -> {b['texture_instructions']} "
              f"words {a['words']} -> {b['words']} rep {rep_counts(slots.words_of(slots.PROGRAMS[base]))} -> {rep_counts(slots.words_of(header))}")


if __name__ == '__main__':
    main()
