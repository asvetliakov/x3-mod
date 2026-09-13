#!/usr/bin/env python3
"""Prove generated motion/depth TEXCOORDs are absent from original declarations.

Uses the actual C++ profile table (including its sharing static assertions),
then token-walks local originals. Writes only a compact derived summary.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import inspect_motion_output_profiles as motion


def check(programs, profile_root=ROOT):
    source = r'''
#include "src/renderer/motion_output_profiles.h"
#include <cstdio>
int main(){for(const auto&r:x3m::renderer::motion_output_profiles)
 std::printf("%016llx %u %016llx %u %u %u %u\n",
 (unsigned long long)r.vertex_fingerprint,r.vertex_dword_count,
 (unsigned long long)r.pixel_fingerprint,r.pixel_dword_count,
 r.texcoord_index,r.depth_texcoord_index,r.depth_output);}
'''
    with tempfile.TemporaryDirectory(prefix='x3-wrap-table-') as temp:
        temp = Path(temp)
        (temp / 'table.cpp').write_text(source)
        subprocess.run([shutil.which('clang++') or 'c++', '-std=c++17', '-O2',
                        '-I', str(profile_root), str(temp / 'table.cpp'), '-o', str(temp / 'table')],
                       check=True, capture_output=True, text=True)
        rows = subprocess.check_output([str(temp / 'table')], text=True).splitlines()
    cache = {}
    indices, depth_indices = Counter(), Counter()
    for row in rows:
        vs, vs_words, ps, ps_words, texcoord, depth, depth_output = row.split()
        texcoord, depth, depth_output = int(texcoord), int(depth), int(depth_output)
        assert 0 <= texcoord < 16 and (depth == 255 or 0 <= depth < 16 and depth != texcoord)
        indices[texcoord] += 1
        if depth_output:
            assert depth != 255
            depth_indices[depth] += 1
        for stage, fingerprint, word_count in [('vs', vs, vs_words), ('ps', ps, ps_words)]:
            key = f'{stage}_{fingerprint}'
            if key not in cache:
                code = (programs / (key + '.bin')).read_bytes()
                facts = motion.profile(code, key, stage, '3_0')
                assert facts['parsed'] and facts['fnv1a64'] == fingerprint
                cache[key] = facts
            facts = cache[key]
            assert facts['dword_count'] == int(word_count)
            original = {d['usage_index'] for d in facts['declarations']
                        if d['role'] == ('output' if stage == 'vs' else 'input') and d['usage'] == 5}
            assert texcoord not in original, (key, 'motion semantic occupied', texcoord)
            # Depth is zeroed only for a pair whose PS consumes it.
            if depth_output:
                assert depth not in original, (key, 'depth semantic occupied', depth)
    return {'pairs': len(rows), 'original_programs': len(cache),
            'motion_semantic_counts': dict(sorted(indices.items())),
            'depth_semantic_counts': dict(sorted(depth_indices.items())),
            'declaration_collisions': 0,
            'scope': 'original semantic absence and compiled table sharing; not native GPU behavior'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--programs', type=Path, required=True)
    parser.add_argument('--profile-root', type=Path, default=ROOT)
    args = parser.parse_args()
    print(json.dumps(check(args.programs, args.profile_root), indent=2))


if __name__ == '__main__':
    main()
