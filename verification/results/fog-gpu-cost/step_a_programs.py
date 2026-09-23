#!/usr/bin/env python3
"""Fog route step A (docs/architecture/fog-gpu-cost.md): each fog program against the base commit 72645b5e:
bytecode identical or changed, ps_3_0 slots / texture instructions / loops / breakc before and after.
Usage: /usr/bin/python3 verification/results/fog-gpu-cost/step_a_programs.py"""
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import fog_density_shader_slots as slots  # noqa: E402

BASE = '72645b5e'
BREAKC = 45


def at_base(path):
    return subprocess.run(['git', '-C', str(ROOT), 'show', f'{BASE}:{path}'], capture_output=True, text=True, check=True).stdout


def breaks(words):
    n, i = 0, 1
    while i < len(words) - 1:
        token = words[i]
        if token & 0xffff == 0xfffe:
            i += 1 + ((token >> 16) & 0x7fff)
            continue
        n += (token & 0xffff) == BREAKC
        i += 1 + ((token >> 24) & 15)
    return n


def main():
    for name, header in slots.PROGRAMS.items():
        provenance = ROOT / f"verification/results/{name.replace('_', '-')}-program.json"
        new, old = json.loads(provenance.read_text()), json.loads(at_base(provenance.relative_to(ROOT)))
        words_new = slots.words_of(header)
        words_old = [int(w, 16) for w in re.findall(r'0x([0-9a-f]{8})u', at_base(header.relative_to(ROOT)))]
        a, b = slots.count(words_old), slots.count(words_new)
        same = new['bytecode_sha256'] == old['bytecode_sha256']
        print(f"{name:28s} bytecode={'identical' if same else 'changed  '} slots {a['slots']:3d} -> {b['slots']:3d} "
              f"tex {a['texture_instructions']:2d} -> {b['texture_instructions']:2d} loops {a['loops']} -> {b['loops']} "
              f"breakc {breaks(words_old)} -> {breaks(words_new)} below_512={b['slots'] < 512}")


if __name__ == '__main__':
    main()
