#!/usr/bin/env python3
"""Read-only instruction/ABI qualification of the voice DMO fallback hook site.

src/proxy/voice_dmo_fallback.cpp patches the return of the media constructor's
IDMOWrapperFilter::Init call (docs/reverse-engineering/voice-startup-sequence.md
section 12): 0x004cfd46 `mov esi,eax` / `cmp esi,0x8007000e`. The ledger below
is independent of the source: the complete constructor 0x4cf460..0x4d0428 is
decoded with MinGW objdump, the span must be whole instructions with no
relative branch inside and no direct branch into its interior, the SiteSpec in
the source must match, and the ABI context the hook relies on is pinned: the
`call edx` immediately before it, the two GUID pushes (category 0x563ae0, DMO
CLSID 0x563b00) and `push eax` (the IDMOWrapperFilter view), the +0x9c slot
reads/writes around it, and the speech-decoder CLSID bytes at 0x563b00. No Wine
or game launch.
"""
import argparse
import json
import subprocess
from pathlib import Path

import exe_identity  # structure + anchors gate; hashes are INFO (docs/reverse-engineering/executable-identity.md)

import verify_chase_aim_sites as common

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/proxy/voice_dmo_fallback.cpp'
DEFAULT_EXE = common.DEFAULT_EXE
MEDIA_CREATE = (0x4cf460, 0x4d0428)
SITE = common.HookSpec('voice_dmo_init_after', 0x4cfd46, bytes.fromhex('8bf081fe0e000780'), *MEDIA_CREATE)
# ABI context: exact bytes the hook's reading of EAX/EBX and the +0x9c slot depend on.
WITNESSES = {
    'cocreate_slot_lea': (0x4cfcd0, '8d839c000000'),            # lea eax,[ebx+0x9c] (&+0x9c for CoCreateInstance)
    'wrapper_clsid_push': (0x4cfcdf, '68f03a5600'),             # push CLSID_DMOWrapperFilter
    'slot_load_for_qi': (0x4cfd0e, '8b839c000000'),             # mov eax,[ebx+0x9c]
    'qi_iid_push': (0x4cfd1b, '68a0ce5400'),                    # push IID_IDMOWrapperFilter
    'init_vtable_slot': (0x4cfd36, '8b510c'),                   # mov edx,[ecx+0xc] (Init)
    'init_args_and_call': (0x4cfd39, '68e03a560068003b560050ffd2'),  # push cat; push clsid; push this; call edx
    'retry_loop': (0x4cfd5e, '3bf77d0639442410 76c8'.replace(' ', '')),  # cmp esi,edi; jge 4cfd68; cmp [esp+0x10],eax; jbe 4cfd30
    'add_filter_guard': (0x4cfe03, '8b8b9c0000003bcf740d'),     # mov ecx,[ebx+0x9c]; cmp ecx,edi; je 4cfe1a
}
SPEECH_CLSID = bytes.fromhex('cb314187cc4e3b4489 48746b89595d20'.replace(' ', ''))  # {874131cb-4ecc-443b-8948-746b89595d20}
AUDIO_DECODER_CATEGORY = bytes.fromhex('8bdbf257bbe61345 9d43dcd2a6593125'.replace(' ', ''))  # {57f2db8b-e6bb-4513-9d43-dcd2a6593125}
INCOMING = {0x4cfd46: set()}  # the site is reached only by fall-through from the call


def decode(exe=DEFAULT_EXE):
    run = subprocess.run([common.OBJDUMP, '-d', '-Mintel', '--insn-width=16',
                          f'--start-address={MEDIA_CREATE[0]}', f'--stop-address={MEDIA_CREATE[1]}', str(exe)],
                         check=True, capture_output=True, text=True, timeout=30)
    return common.parse_objdump(run.stdout, *MEDIA_CREATE)


def inspect(image, instructions, source):
    checks = {'preferred_base': image.image_base == common.IMAGE_BASE,
              'source_specs': common.check_source_specs(source, (SITE,))['ok']}
    row = common.inspect_site(image, SITE, instructions)
    checks['site'] = row['ok']
    by_va = {i.va: i for i in instructions}
    checks['complete_routine'] = bool(instructions) and 0x4cf460 in by_va
    checks['preceded_by_indirect_call'] = 0x4cfd44 in by_va and by_va[0x4cfd44].raw == b'\xff\xd2'
    checks['abi_context'] = all(image.read(va, len(bytes.fromhex(raw))) == bytes.fromhex(raw)
                                for va, raw in WITNESSES.values())
    checks['speech_clsid'] = image.read(0x563b00, 16) == SPEECH_CLSID
    checks['audio_decoder_category'] = image.read(0x563ae0, 16) == AUDIO_DECODER_CATEGORY
    checks['no_direct_edges_to_site'] = {i.va for i in instructions if common._is_direct_control(i) == SITE.va} == INCOMING[SITE.va]
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks, 'site': row}


def verify(exe=DEFAULT_EXE, source=SOURCE):
    data = Path(exe).read_bytes()
    try:
        report = inspect(common.Image(data), decode(exe), Path(source).read_text())
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        return {'result': 'FAIL', 'checks': {'decode': False}, 'error': str(error)}
    report['checks']['exe_identity'] = exe_identity.identity_ok(data)
    report['exe_info'] = exe_identity.info(data)
    report['exe_sha256'] = report['exe_info']['sha256']
    report['result'] = 'PASS' if all(report['checks'].values()) else 'FAIL'
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE)
    parser.add_argument('--source', type=Path, default=SOURCE)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    report = verify(args.exe, args.source)
    print(json.dumps(report if args.json else {'result': report['result'], **report['checks']}, indent=2))
    raise SystemExit(report['result'] != 'PASS')
