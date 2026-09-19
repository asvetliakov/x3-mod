#!/usr/bin/env python3
"""Read-only whole-image root-descent call/ABI qualification; never Wine/game."""
import json
import re
from pathlib import Path
import verify_collide_memo_site as memo

SITE, TARGET = 0x4e2956, 0x4e2530
PRE = bytes.fromhex('33c0d91c2451525357a344856000a348856000a34c856000')
POST = bytes.fromhex('83c4145f5e5d5b81c498000000c3')
RECURSIONS = [0x4e264c, 0x4e269f, 0x4e2705, 0x4e2767]
SOURCE = memo.ROOT / 'src/proxy/collide_query_phases.cpp'


def inspect(data, decoded, source):
    image = memo.common.Image(data)
    instructions = decoded[memo.COLLIDER]
    by = {i.va: i for i in instructions}
    target = memo.common._is_direct_control
    def source_bytes(name):
        match = re.search(rf'\b{name}\[\]\s*=\s*\{{([^}}]+)\}}', source)
        return bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})', match.group(1))) if match else b''
    checks = {
        'memo_engine_contract': memo.body_hashes(image) == memo.HASHES,
        'whole_call': SITE in by and by[SITE].raw == bytes.fromhex('e8d5fbffff') and target(by[SITE]) == TARGET,
        'whole_windows': SITE-len(PRE) in by and SITE+5 in by and by[SITE+5+len(POST)-1].mnemonic == 'ret',
        'pre_window': image.read(SITE-len(PRE),len(PRE)) == PRE,
        'post_window_caller_pop_20': image.read(SITE+5,len(POST)) == POST,
        'sole_external_four_recursive_calls': memo.sites.rel32_references(image,TARGET,TARGET+1) == [(v,0xe8) for v in RECURSIONS+[SITE]],
        'no_abs32_target': memo.sites.abs32_references(image,TARGET,TARGET+1) == [],
        'no_interior_rel32': memo.sites.rel32_references(image,SITE+1,SITE+5) == [],
        'no_interior_abs32': memo.sites.abs32_references(image,SITE+1,SITE+5) == [],
        'no_local_branch_interior': not any(SITE < (target(i) or 0) < SITE+5 for i in instructions),
        'five_arguments': [by[v].mnemonic for v in [0x4e293a,0x4e293b,0x4e293c,0x4e293d]] == ['push']*4 if 0x4e293a in by else False,
        'source_windows': source_bytes('pre') == PRE and source_bytes('post') == POST,
        'source_site': 'site_va=0x004e2956, target_va=0x004e2530' in source,
    }
    # The fifth float word is reserved/stored immediately before the four pushes.
    before = [i for i in instructions if SITE-len(PRE) <= i.va < SITE]
    checks['five_arguments'] = [i.raw for i in before[:7]] == [bytes.fromhex(x) for x in ('33c0','d91c24','51','52','53','57','a344856000')]
    return {'result':'PASS' if all(checks.values()) else 'FAIL','checks':checks}


def main():
    result = inspect(memo.sites.DEFAULT_EXE.read_bytes(),memo.decode(memo.sites.DEFAULT_EXE),SOURCE.read_text())
    print(json.dumps(result,indent=2))
    return result['result'] != 'PASS'

if __name__ == '__main__':
    raise SystemExit(main())
