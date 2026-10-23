#!/usr/bin/env python3
"""Read-only check for docs/reverse-engineering/music-script-driver.md section 9 (engagement data for a narrowed
combat-music trigger).

EXE side (X3AP.exe, unmodified): pins the instruction bytes of the projectile-hit handler 0x0045e130 (attacker =
projectile owner, the four AttackedBy notifications, the per-frame Flags2 hit bits 0x80000 / 0x40 and the player hit
counters), the collision pass that clears those bits and its place before the alert routine in the main loop, the
player-shot flag on bullets and missiles (fire control 0x00445170, missile launch 0x00448b30), the relation test the
alert routine uses (0x00450890 / 0x00450810), the order/target fields of the attack and defensive orders, the death
reference fix-up 0x0044a7c0, the SA_ getters of the player counters and of Flags2, and the KC OR opcode. Counts the
writers/readers of the Flags2 bits and the direct callers of the routines named; raw-scans .text for any rel8/rel32
branch into the interior of the candidate hook spans.

KC side (L/x3story.obj, Mayhem addon/07.cat and stock addon/04.cat): census of every SA_FireOnObject / SA_FireAhead
call with its symbolically tracked arguments; asserts that only 606::FireLaser passes a flags argument with bit 2
(the player-shot bit) and that every other site passes 1. Prints decoded text only, no script bytes. No Wine.

    python3 -I verification/results/music-script-driver/verify_engagement_data.py [X3 dir] > .../verify_engagement_output.json
Exit status 0 only on PASS.
"""
import bisect
import json
import os
import re
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent / 'ship-navigation-ai' / 'gate-queue'))
sys.path.insert(0, str(ROOT / 'verification' / 'probe'))
import kc_obj  # noqa: E402
from kc_obj import Obj, load_member  # noqa: E402
import exe_identity  # noqa: E402

X3 = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(os.environ.get('X3_DIR', '/mnt/c/X3'))
EXE = X3 / 'X3AP.exe'
TEXT_VA, TEXT_RAW, TEXT_SIZE = 0x401000, 0x400, 0x130630
SECTIONS = ((0x401000, 0x130630, 0x400), (0x532000, 0x4074d, 0x130c00), (0x573000, 0xefb58, 0x171400))

# (name, VA, instruction bytes separated by spaces): all inside .text
PATTERNS = [
    ('collide_owner_of_bullet_a', 0x45e27b,
     '0fb74348 6685c0 7512 8b4b50 8b4904 3bce 894c240c 0f84060c0000'),
    ('collide_owner_of_bullet_b', 0x45e296,
     '0fb75648 6685d2 7512 8b4e50 8b4904 3bcb 894c2414 0f84eb0b0000'),
    ('collide_owner_of_missile', 0x45e2b1,
     '663d0a00 b900000001 750f 854b40 740a 8b7b50 8b7f0c 897c240c 6683fa0a 750f 854e40 740a 8b5650 8b520c 89542414'),
    ('hit_a_attackedby_args', 0x45e438,
     '8b8e94000000 85c9 7431 8b44240c 85c0 7405 8b4008 eb02 33c0 8b5308 52 50 a10c856000 6a02 6a01 689cfd5500 51 8b4860 6a00 51 e850100400 83c420'),
    ('hit_a_player_counters_flags', 0x45e473,
     '8b4344 a900010000 8b0d0c856000 7432 a900080000 752b 0fb74348 6685c0 7506 83414801 eb11 663d0a00 750b 66837e4800 7404 83414c01 834e4440 814b4400080000 814e4400000800'),
    ('hit_b_attackedby_args', 0x45e626,
     '8b7d0c 8b8b94000000 85c9 7431 8b442414 85c0 7405 8b4008 eb02 33c0 8b5708 52 50 a10c856000 6a02 6a01 689cfd5500 51 8b4860 6a00 51 e85f0e0400 83c420'),
    ('hit_b_player_counters_flags', 0x45e664,
     '8b4744 a900010000 8b0d0c856000 7432 a900080000 752b 0fb74748 6685c0 7506 83414801 eb11 663d0a00 750b 66837b4800 7404 83414c01 834b4440 814f4400080000 814b4400000800'),
    ('ram_gate_manual_control', 0x45e96f,
     '8bc3 e80a16feff 84c0 750b 8bc6 e8ff15feff 84c0 7472 8bc3 e834a8fbff 84c0 7567 8bc6 e829a8fbff 84c0 755c'),
    ('ram_attackedby_both', 0x45e99b,
     '8b8694000000 85c0 7424 8b4b08 8b150c856000 6a00 51 6a02 6a01 689cfd5500 50 8b4260 6a00 50 e8fa0a0400 83c420 8b8394000000 85c0 7424 8b4e08 8b150c856000 6a00 51 6a02 6a01 689cfd5500 50 8b4260 6a00 50 e8cc0a0400 83c420'),
    ('collide_pass_clears_flags2', 0x45d280,
     '816040ffcf9fff 816044bcfff7ff 6683784807 7509 8b5850 89bbe0010000'),
    ('sector_pass_collide_then_simulate', 0x43a38e,
     '56 e8bc2e0200 56 e836870100 56 e880130200'),
    ('main_loop_sector_pass', 0x403b17,
     'e844680300'),
    ('main_loop_alert', 0x403f2f,
     'e8ac8e0100'),
    ('fire_target_from_cockpit_1e0', 0x445255,
     '8b8ee0010000 894b0c'),
    ('fire_flag_bit2_local', 0x445f0c,
     '8b4b14 83e102 894dc8'),
    ('bullet_player_flag_and_counter', 0x4463b0,
     '837dc800 8b0d0c856000 ba01000000 899564ffffff 740a 015140 81484400010000'),
    ('bullet_expiry_owner_group', 0x446578,
     '8b0d346f6000 8b7588 894710 894714 8b9118070000 03560c 8b4750 8910 8b4f50 8b4308 894104 668b503c 8b4f50 8b45e8 6689573c'),
    ('missile_launcher_target_time_group', 0x448ce7,
     '8b4b50 8b7d0c 89710c 8b5350 897a10 8b0d346f6000 8b4350 8b9118070000 895008 668b463c 6689433c'),
    ('missile_player_flag_and_counter', 0x449312,
     'f6451802 740b 83404401 814b4400010000'),
    ('player_missile_arming_window', 0x45cc29,
     '66837f480a 7526 f7474400010000 741d 8b15346f6000 8b4f50 8b8218070000 2b4108 3dd0070000'),
    ('hostile_test_full', 0x450890,
     '56 8bf0 85f6 750b 33c9 85c0 0f9cc1 8ac1 5e c3 0fb74748 663d0700 740c 663d0500 7406 663d0600 7525 8b4750 83b8d401000000 7419 8bc7 e845ffffff 85c0 7d0e 83c8ff 33c9 85c0 0f9cc1 8ac1 5e c3 8bc6 8bf7 e82affffff 33c9 85c0 0f9cc1 8ac1 5e c3'),
    ('relation_test_full', 0x450810,
     '85c0 7473 668378480a 751f f7404000000001 7416 8b4850 39710c 740e 668b503c 663b563c 7404 83c8ff c3 f7464400000040 7544 0fb74e48 6683f907 740c 6683f905 7406 6683f906 752e 0fb7483c 6685c9 7c25 8b4650 ba01000000 d3e2 8590cc010000 7406 b801000000 c3 8b80d0010000 23c2 f7d8 1bc0 c3 33c0 c3'),
    ('enemy_of_enabled_writer', 0x450966,
     '8b4150 8b4c2404 8988d4010000'),
    ('sa_attack_order_target', 0x46151a,
     '66c781580200001000 8b5650 898224020000'),
    ('sa_defensive_order_attacker', 0x461818,
     '66c781580200002200 8b5650 898224020000'),
    ('refclear_bullet_owner', 0x44a840,
     '396c241c 750b 8b4750 395804 7503 896804'),
    ('refclear_missile_launcher_target', 0x44a818,
     '396c241c 750b 8b4750 39580c 7503 89680c 8b7f50 395f10 0f8539020000 896f10'),
    ('refclear_ship_target', 0x44a88a,
     '8b4750 399824020000 7506 89a824020000'),
    ('hud_hit_blink_1000ms', 0x4283ab,
     'f7434400000800 7415 83bb2401000000 750c c78324010000e8030000 eb28 8b8324010000 85c0 7e1e 8b15346f6000 2b8214070000 898324010000 790a c7832401000000000000'),
    ('sa_get_laser_missile_fired_hits', 0x464797,
     '8b150c856000 8b4240 50 e94ce6ffff 8b150c856000 8b4244 50 e93de6ffff 8b150c856000 8b4248 50 e92ee6ffff 8b150c856000 8b424c 50 e91fe6ffff'),
    ('sa_get_set_flags2', 0x46382e,
     '8b4044 50 a1e4856000 50 e9bcf5ffff 8b7518 8b5601 a10c856000 e8a36cfdff 85c0 74bd 8b4e06 894844 51'),
    ('manual_control_test', 0x43ff80,
     '8b4040 a900000400 740d a900000800 7506 b801000000 c3 33c0 c3'),
    ('alert_walk_level1_site', 0x42c939,
     '7d05 bd01000000'),
    ('alert_walk_level2_site', 0x42c94a,
     'bd02000000'),
    ('alert_hook_site', 0x42c965,
     '8b74241c 0fbfc5 3b4608 7e03 894608 8b4e08 8b4604 3bc8'),
    ('kc_op53_and', 0x4a33cb,
     '8b4301 214306'),
    ('kc_op54_or', 0x4a33e9,
     '8b4b01 094b06'),
]

STRINGS = [(0x55fd9c, 'AttackedBy'), (0x55b7f0, 'NotifyAlert'), (0x55fcac, 'MissileLaunched')]

# SA_ module: name table 0x0057a7f0, jump table 0x004690f0 (ship-navigation-ai.md 1.1)
SA = [(0x11, 'SA_AttackObject', 0x4614da), (0x13, 'SA_DefensiveMove', 0x4617cf), (0x66, 'SA_GetFlags2', 0x463816),
      (0x65, 'SA_SetFlags2', 0x46383d), (0x6b, 'SA_IsEnemy', 0x46689a), (0x70, 'SA_GetGroup', 0x462bdb),
      (0xf3, 'SA_GetLaserFired', 0x464797), (0xf4, 'SA_GetMissileFired', 0x4647a6),
      (0xf5, 'SA_GetLaserHits', 0x4647b5), (0xf6, 'SA_GetMissileHits', 0x4647c4),
      (0xfc, 'SA_FireOnObject', 0x461ac1), (0xfd, 'SA_FireAhead', 0x461b27), (0xff, 'SA_ShootMissile', 0x461d9c),
      (0x154, 'SA_SetEnemyOfEnabled', 0x466868)]

# direct E8 callers expected (complete lists)
CALLERS = {
    0x45e130: [0x45d233, 0x45d2ff, 0x45d691, 0x45da3c, 0x45df5c],   # hit handler: only from the collision pass
    0x45d1b0: [0x45d4cf, 0x45d4fa],                                 # the helper holding 0x45d233, inside 0x45d250
    0x45d250: [0x43a38f],                                           # collision pass: per sector
    0x43a360: [0x403b17],                                           # sector pass: once per main-loop iteration
    0x41cde0: [0x403f2f],                                           # alert routine's registry walk
    0x448b30: [0x461df0],                                           # missile launch: only SA_ShootMissile
    0x450950: [0x466887],                                           # +0x1d4 writer: only SA_SetEnemyOfEnabled
}

# raw-scanned spans: every branch into them must land on the first byte
HOOK_SPANS = [(0x42c974, 6), (0x42c93b, 5), (0x45e4b5, 7), (0x45e6a6, 7)]

# Flags2 (+0x44, disp8) bit sites: (regex over .text, expected VAs)
FLAG_SITES = {
    'or_0x80000_hit': (rb'\x81[\x48-\x4f]\x44\x00\x00\x08\x00', [0x45e4b5, 0x45e6a6]),
    'test_0x80000_hit': (rb'\xf7[\x40-\x47]\x44\x00\x00\x08\x00', [0x4283ab]),
    'or_0x100_player_projectile': (rb'\x81[\x48-\x4f]\x44\x00\x01\x00\x00', [0x4463ca, 0x44931c]),
    'or_0x40_hit_by_player': (rb'\x83[\x48-\x4f]\x44\x40', [0x45e4aa, 0x45e69b]),
    'and_clear_0x80000_0x40': (rb'\x81[\x60-\x67]\x44\xbc\xff\xf7\xff', [0x45d287]),
}

FIRE_NATIVES = ('SA_FireOnObject', 'SA_FireAhead')
PLAYER_SHOT_OWNER = '606::FireLaser'


def split_args(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch == ',' and depth == 0:
            out.append(cur.strip()); cur = ''
            continue
        depth += ch in '(['
        depth -= ch in ')]'
        cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def kc_census(cat):
    kc_obj.BIN.setdefault(0x54, '|')  # opcode 0x54 is bitwise OR (handler 0x004a33d6, pinned above)
    o = Obj(load_member(cat))
    owners = set()
    for pc in o.all_insns():
        if o.code[pc] == 0x82 and o.operand(pc) in FIRE_NATIVES:
            owners.add(o.entries[bisect.bisect_right(o.entries, pc) - 1])
    rows, tracked = [], set()
    for e in sorted(owners):
        name = ' '.join('%d::%s' % m[:2] for m in o.meth[e])
        lines, bad = o.track(e)
        for pc, _, ln in lines:
            m = re.match(r'call (SA_FireOnObject|SA_FireAhead)\((.*)\)$', ln)
            if m:
                args = split_args(m.group(2))
                flags = args[3] if len(args) > 3 else None
                tracked.add(pc)
                rows.append({'pc': '%05x' % pc, 'owner': name, 'native': m.group(1), 'argc': len(args),
                             'flags': flags, 'player_bit': player_bit(flags), 'how': 'tracked'})
    # sites the flow tracker does not reach (behind a KC switch): straight-line evaluation from the last
    # drop / jump / call before the native
    pcs = list(o.all_insns())
    for k, pc in enumerate(pcs):
        if o.code[pc] != 0x82 or o.operand(pc) not in FIRE_NATIVES or pc in tracked:
            continue
        j = k - 1
        while j > 0 and o.code[pcs[j]] not in (0x23, 0x24, 0x32, 0x33, 0x34, 0x82, 0x83, 0x85, 0x86, 0x88):
            j -= 1
        st = []
        for q in pcs[j + 1:k]:
            op = o.code[q]
            if 1 <= op <= 4:
                st.append(str(op - 1))
            elif op == 0x05:
                v = o.code[q + 1]
                st.append(str(v - 256 if v > 127 else v))
            elif op in (0x0d, 0x0e, 0x0f):
                st.append('%s%d' % ({0x0d: 'loc', 0x0e: 'g', 0x0f: 'm'}[op], struct.unpack_from('>h', o.code, q + 1)[0]))
            elif op == 0x10:
                a = st.pop() if st else '?'
                ix = st.pop() if st else '?'
                st.append('%s[%s]' % (a, ix))
            elif op in kc_obj.BIN:
                b_ = st.pop() if st else '?'
                a_ = st.pop() if st else '?'
                st.append('(%s %s %s)' % (a_, kc_obj.BIN[op], b_))
            else:
                st.append('?op%02x' % op)
        n = int(st.pop()) if st and st[-1].isdigit() else 0
        args = [st.pop() if st else '?' for _ in range(n)]
        e = o.entries[bisect.bisect_right(o.entries, pc) - 1]
        flags = args[3] if len(args) > 3 else None
        rows.append({'pc': '%05x' % pc, 'owner': ' '.join('%d::%s' % m[:2] for m in o.meth[e]),
                     'native': o.operand(pc), 'argc': n, 'flags': flags, 'player_bit': player_bit(flags),
                     'how': 'linear'})
    return rows


def player_bit(flags):
    """True when the flags expression is a literal or an OR of literals with bit 0x2 set; None when not literal."""
    if flags is None:
        return None
    lits = re.findall(r'-?\d+', flags)
    if not re.fullmatch(r'[\d\s|()-]+', flags):
        return None
    v = 0
    for x in lits:
        v |= int(x)
    return bool(v & 2)


def main():
    data = EXE.read_bytes()
    text = data[TEXT_RAW:TEXT_RAW + TEXT_SIZE]
    failures = []

    def rdany(va, n):
        for sva, size, raw in SECTIONS:
            if sva <= va < sva + size:
                return data[raw + va - sva:raw + va - sva + n]
        raise ValueError(hex(va))

    def u32(va):
        return struct.unpack('<I', rdany(va, 4))[0]

    if not exe_identity.identity_ok(data):
        failures.append('exe_identity')
    for name, va, hx in PATTERNS:
        want = bytes.fromhex(hx.replace(' ', ''))
        if rdany(va, len(want)) != want:
            failures.append('pattern_' + name)
    for va, s in STRINGS:
        if rdany(va, len(s) + 1) != s.encode() + b'\0':
            failures.append('string_' + s)
    for idx, nm, handler in SA:
        p = u32(0x57a7f0 + 4 * idx)
        if rdany(p, len(nm) + 1) != nm.encode() + b'\0' or u32(0x4690f0 + 4 * idx) != handler:
            failures.append('sa_' + nm)
    found = {t: [] for t in CALLERS}
    spans = {va: [] for va, _ in HOOK_SPANS}
    for i in range(len(text) - 6):
        b = text[i]
        va = TEXT_VA + i
        targets = []
        if b in (0xe8, 0xe9):
            t = va + 5 + struct.unpack_from('<i', text, i + 1)[0]
            targets.append(t)
            if b == 0xe8 and t in found:
                found[t].append(va)
        if 0x70 <= b <= 0x7f or b == 0xeb or 0xe0 <= b <= 0xe3:
            targets.append(va + 2 + struct.unpack_from('<b', text, i + 1)[0])
        if b == 0x0f and 0x80 <= text[i + 1] <= 0x8f:
            targets.append(va + 6 + struct.unpack_from('<i', text, i + 2)[0])
        for t in targets:
            for s, n in HOOK_SPANS:
                if s < t < s + n:
                    spans[s].append('%08x->%08x' % (va, t))
    for t, want in CALLERS.items():
        if sorted(found[t]) != sorted(want):
            failures.append('callers_%08x' % t)
    for s, hits in spans.items():
        if hits:
            failures.append('interior_branch_%08x' % s)
    flag_counts = {}
    for name, (rx, want) in FLAG_SITES.items():
        got = [TEXT_VA + m.start() for m in re.finditer(rx, text)]
        flag_counts[name] = ['%08x' % v for v in got]
        if sorted(got) != sorted(want):
            failures.append('flags_' + name)
    kc = {}
    for label, cat in (('mayhem', X3 / 'addon/07.cat'), ('stock', X3 / 'addon/04.cat')):
        rows = kc_census(cat)
        kc[label] = rows
        player = [r for r in rows if r['player_bit']]
        if not rows or any(r['owner'] != PLAYER_SHOT_OWNER for r in player) or not player:
            failures.append('kc_player_bit_%s' % label)
        if any(r['player_bit'] is not False for r in rows if r['owner'] != PLAYER_SHOT_OWNER):
            failures.append('kc_other_flags_%s' % label)
    out = {
        'status': 'FAIL' if failures else 'PASS', 'failures': failures,
        'exe': str(EXE), 'patterns': len(PATTERNS), 'sa_entries': len(SA),
        'callers': {'%08x' % t: ['%08x' % v for v in sorted(v_)] for t, v_ in found.items()},
        'hook_spans_interior_branches': {'%08x' % s: v for s, v in spans.items()},
        'flags2_sites': flag_counts,
        'kc_fire_sites': {k: [{x: r[x] for x in ('pc', 'owner', 'native', 'argc', 'flags', 'player_bit', 'how')} for r in v]
                          for k, v in kc.items()},
    }
    json.dump(out, sys.stdout, indent=1)
    sys.stdout.write('\n')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
