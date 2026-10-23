#!/usr/bin/env python3
"""Read-only verifier of the combat-music trigger's engine sites (src/proxy/music_alert_core.h; the facts in
docs/reverse-engineering/music-script-driver.md sections 2, 5 and 9).

On the installed X3AP.exe (unmodified): the executable identity; every window the installer compares (the alert
routine's head, its parent-sector test and its read of the list array [sector+0x50], its range test, the six-byte site 0x0042c974 in its context, the commit tail, the whole relation and
stance routines 0x00450890 / 0x00450810, the whole distance routine 0x00419280, and the instruction witnesses of the
fields the rule reads: the collision pass clearing Flags2 0x80000/0x40, the player fire counters M+0x40/0x44, the hit
counters M+0x48/0x4c, the attack order 0x10 at flight+0x258 with its target at flight+0x224, the cockpit target
+0x1e0, the clock cfg+0x718, the player group 10, the death fix-up of flight+0x224, the SA_ counter getters); the
site decoded as two whole instructions; the alert routine's only caller 0x0041cdea and the registry walk's only
caller 0x00403f2f; the branch 0x0042c96f onto the site's first byte and no rel8/rel32 branch in .text into its
interior; the range test's calls into 0x00419280 and the relation routine's calls into 0x00450810. The WINDOWS table
here must equal core::windows (test_music_alert cross-checks it). No Wine, nothing launched.

    python3 -I verification/results/music-script-driver/verify_music_alert_sites.py [X3 dir]
Writes verify_music_alert_output.json beside itself and prints PASS or FAIL; exit status 0 only on PASS.
"""
import json
import os
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'verification' / 'probe'))
import exe_identity  # noqa: E402

EXE = Path(os.environ.get('X3_DIR', '/mnt/c/X3')) / 'X3AP.exe'  # main() takes an X3 directory as argv[1]
TEXT_VA, TEXT_RAW, TEXT_SIZE = 0x401000, 0x400, 0x130630

ALERT_VA, SITE_VA, CONTINUE_VA, SITE_BRANCH_VA = 0x0042c890, 0x0042c974, 0x0042c97a, 0x0042c96f
WALK_VA, WALK_CALLER_VA, MAIN_LOOP_ALERT_VA = 0x0041cde0, 0x0041cdea, 0x00403f2f
RELATION_VA, STANCE_VA, DISTANCE_VA = 0x00450890, 0x00450810, 0x00419280
SITE_BYTES = bytes.fromhex('8b4e08 8b4604'.replace(' ', ''))
# The two whole instructions of the site: (bytes, text)
SITE_INSTRUCTIONS = ((bytes.fromhex('8b4e08'), 'mov ecx,[esi+8]'), (bytes.fromhex('8b4604'), 'mov eax,[esi+4]'))
# (name, VA, hex) in the order of core::windows
WINDOWS = [
    ('alert_head', 0x0042c890, 'a10c856000 83ec08 53 55 56 57 8b7838'),
    ('sector_walk', 0x0042c8a9, '8b4754 85c0 0f84b1000000 6683784801 0f85a6000000 8b4050 89442410 c744241420000000 8b30 8b1e 85db 7479'),
    ('range_window', 0x0042c915, '8b54241c 837a0400 8bc6 8bd7 750c e858c9feff 3da0252600 eb0a e84cc9feff 3d404b4c00 7d05 bd01000000'),
    ('site_window', 0x0042c965, '8b74241c 0fbfc5 3b4608 7e03 894608 8b4e08 8b4604 3bc8 0f84ce000000'),
    ('commit_window', 0x0042ca4a, '8b4608 894604 5f c7460800000000 5e 5d 5b 83c408 c20400'),
    ('relation_body', 0x00450890,
     '56 8bf0 85f6 750b 33c9 85c0 0f9cc1 8ac1 5e c3 0fb74748 663d0700 740c 663d0500 7406 663d0600 7525 8b4750 '
     '83b8d401000000 7419 8bc7 e845ffffff 85c0 7d0e 83c8ff 33c9 85c0 0f9cc1 8ac1 5e c3 8bc6 8bf7 e82affffff 33c9 85c0 '
     '0f9cc1 8ac1 5e c3'),
    ('stance_body', 0x00450810,
     '85c0 7473 668378480a 751f f7404000000001 7416 8b4850 39710c 740e 668b503c 663b563c 7404 83c8ff c3 '
     'f7464400000040 7544 0fb74e48 6683f907 740c 6683f905 7406 6683f906 752e 0fb7483c 6685c9 7c25 8b4650 ba01000000 '
     'd3e2 8590cc010000 7406 b801000000 c3 8b80d0010000 23c2 f7d8 1bc0 c3 33c0 c3'),
    ('distance_body', 0x00419280,
     '8b4870 8b5270 8b4230 83c230 83c130 2b01 7902 f7d8 56 8b7204 2b7104 7902 f7de 8b5208 2b5108 7902 f7da 3bc6 7e04 '
     '3bc2 eb04 3bf2 8bc6 7f02 8bc2 5e c3'),
    ('collide_clear', 0x0045d280, '816040ffcf9fff 816044bcfff7ff'),
    ('bullet_counter', 0x004463b0, '837dc800 8b0d0c856000 ba01000000 899564ffffff 740a 015140 81484400010000'),
    ('missile_counter', 0x00449312, 'f6451802 740b 83404401 814b4400010000'),
    ('hit_counters', 0x0045e473,
     '8b4344 a900010000 8b0d0c856000 7432 a900080000 752b 0fb74348 6685c0 7506 83414801 eb11 663d0a00 750b '
     '66837e4800 7404 83414c01 834e4440 814b4400080000 814e4400000800 8b750c 0fb746'),
    ('attack_order', 0x0046151a, '66c781580200001000 8b5650 898224020000'),
    ('cockpit_target', 0x00445255, '8b8ee0010000 894b0c'),
    ('clock_witness', 0x00446578,
     '8b0d346f6000 8b7588 894710 894714 8b9118070000 03560c 8b4750 8910 8b4f50 8b4308 894104 668b503c 8b4f50 8b45e8 '
     '6689573c 894170 8b'),
    ('group10', 0x00450730, '663d0a00 56 8bf1 6689463c'),
    ('death_fixup_target', 0x0044a88a, '8b4750 399824020000 7506 89a824020000'),
    ('sa_counters', 0x00464797,
     '8b150c856000 8b4240 50 e94ce6ffff 8b150c856000 8b4244 50 e93de6ffff 8b150c856000 8b4248 50 e92ee6ffff '
     '8b150c856000 8b424c 50 e91fe6ffff'),
    ('walk_caller', 0x0041cdea, 'e8a1fa0000'),
]
# The main loop's call 0x00403f2f -> 0x0041cde0 is checked here (CALLS, CALLERS) but not by the installer: the
# game-phase group may redirect it in the same install window (game_phase_sites.h "game_phase_cockpits").
# Direct E8 callers expected, complete lists.
CALLERS = {ALERT_VA: [WALK_CALLER_VA], WALK_VA: [MAIN_LOOP_ALERT_VA]}
# Calls inside pinned windows whose rel32 must resolve to the named routine.
CALLS = [(0x0042c923, DISTANCE_VA), (0x0042c92f, DISTANCE_VA), (0x004508c6, STANCE_VA), (0x004508e1, STANCE_VA),
         (WALK_CALLER_VA, ALERT_VA), (MAIN_LOOP_ALERT_VA, WALK_VA)]


def window_bytes(hx):
    return bytes.fromhex(hx.replace(' ', ''))


def verify(data):
    """(failures, checks, detail) for the bytes of an X3AP.exe image."""
    failures, checks, detail = [], 0, {}
    text = data[TEXT_RAW:TEXT_RAW + TEXT_SIZE]

    def rd(va, n):
        if not TEXT_VA <= va < TEXT_VA + TEXT_SIZE:
            raise ValueError(hex(va))
        return text[va - TEXT_VA:va - TEXT_VA + n]

    checks += 1
    if not exe_identity.identity_ok(data):
        failures.append('exe_identity')
    for name, va, hx in WINDOWS:
        checks += 1
        if rd(va, len(window_bytes(hx))) != window_bytes(hx):
            failures.append('window_' + name)
    checks += 1
    if rd(SITE_VA, len(SITE_BYTES)) != SITE_BYTES or b''.join(b for b, _ in SITE_INSTRUCTIONS) != SITE_BYTES:
        failures.append('site_bytes')
    checks += 1
    if SITE_VA + len(SITE_BYTES) != CONTINUE_VA:
        failures.append('continue_va')
    # The branch just before the site lands on its first byte.
    checks += 1
    branch = rd(SITE_BRANCH_VA, 2)
    if branch[0] != 0x7e or SITE_BRANCH_VA + 2 + struct.unpack('<b', branch[1:2])[0] != SITE_VA:
        failures.append('site_branch')
    for at, target in CALLS:
        checks += 1
        code = rd(at, 5)
        if code[0] != 0xe8 or at + 5 + struct.unpack('<i', code[1:5])[0] != target:
            failures.append('call_%08x' % at)
    # Raw scan of .text: every E8 to the two routines, and every rel8/rel32 branch into the site's interior.
    found = {t: [] for t in CALLERS}
    interior = []
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
            if SITE_VA < t < SITE_VA + len(SITE_BYTES):
                interior.append('%08x->%08x' % (va, t))
    for t, want in CALLERS.items():
        checks += 1
        if sorted(found[t]) != sorted(want):
            failures.append('callers_%08x' % t)
    checks += 1
    if interior:
        failures.append('interior_branch')
    detail = {'callers': {'%08x' % t: ['%08x' % v for v in sorted(v_)] for t, v_ in found.items()},
              'interior_branches': interior, 'windows': len(WINDOWS), 'window_bytes': sum(len(window_bytes(h)) for _, _, h in WINDOWS)}
    return failures, checks, detail


def main():
    exe = Path(sys.argv[1]) / 'X3AP.exe' if len(sys.argv) > 1 else EXE
    data = exe.read_bytes()
    failures, checks, detail = verify(data)
    out = {'status': 'FAIL' if failures else 'PASS', 'failures': failures, 'checks': checks, 'exe': str(exe),
           'site': '%08x' % SITE_VA, 'site_bytes': SITE_BYTES.hex(), **detail}
    (HERE / 'verify_music_alert_output.json').write_text(json.dumps(out, indent=1) + '\n')
    print('%s checks=%d windows=%d window_bytes=%d failures=%s' % (out['status'], checks, detail['windows'],
                                                                   detail['window_bytes'], ','.join(failures) or 'none'))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
