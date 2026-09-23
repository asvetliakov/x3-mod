#!/usr/bin/env python3
"""Read-only byte/census check for docs/reverse-engineering/alternative-video-playback.md.

Pins the instruction bytes behind the "Use Alternative Video Playback" finding
(registry load/store of VideoD3DFlags2 into *0x00606f34+0x100, the launcher
checkbox 1258 <-> bit 0x4000, the three media-constructor branches keyed on the
bit, the P_Get/SetSysD3DFlags2 script commands), counts every direct encoding
of bit 0x4000 on +0x100 in .text, resolves the GUID/string constants, reads the
effective checkbox caption from the game text archives, and pins the two
CrossOver amstream.dll behaviours the Wine-side conclusion rests on. It reports
the bottle's current registry bit as an observation only. No Wine, no game.
Prints one JSON object; exit status 0 only on PASS.
"""
import hashlib
import json
import os
import re
import struct
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BOTTLE = Path(os.environ.get('X3_BOTTLE', Path.home() / 'Library/Application Support/CrossOver/Bottles/X3'))
EXE = Path(os.environ.get('X3AP_EXE', BOTTLE / 'drive_c/X3/X3AP.exe'))
AMSTREAM = BOTTLE / 'drive_c/windows/syswow64/amstream.dll'
SHA256 = 'fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab'
AMSTREAM_SHA256 = 'a280ac310690a7fb2141dafb083439a9bb5a3160c93ed5c48efeb41024e41ca9'

# (name, VA, hex bytes) in X3AP.exe
PATTERNS = [
    ('reg_load_videod3dflags2', 0x4b7278, '68 94 2c 56 00 51 89 7c 24 24 ff d6 85 c0 75 11 8b 54 24 10 a1 34 6f 60 00 89 90 00 01 00 00'),
    ('reg_save_videod3dflags2_reg_binary', 0x4b7e27, 'a1 34 6f 60 00 8b 88 00 01 00 00 8b 44 24 0c 6a 04 8d 54 24 0c 52 6a 03 6a 00 68 94 2c 56 00 50'),
    ('defaults_videod3dflags2_0x281', 0x4b6d2a, 'c7 81 00 01 00 00 81 02 00 00'),
    ('startup_keep_bit8_then_set_bit8', 0x402f33, '83 e6 08 09 b0 00 01 00 00 33 f6 83 88 00 01 00 00 08'),
    ('dialog_init_checkbox_1258', 0x4cd7bd, '8b 0d 34 6f 60 00 f7 81 00 01 00 00 00 40 00 00 6a 00 74 04 6a 01 eb 02 6a 00 68 f1 00 00 00 68 ea 04 00 00 56 ff d7 50 ff d5'),
    ('dialog_reset_checkbox_1258', 0x4cce30, '8b 0d 34 6f 60 00 f7 81 00 01 00 00 00 40 00 00 6a 00 74 04 6a 01 eb 02 6a 00 68 f1 00 00 00'),
    ('dialog_ok_1258_to_bit_0x4000', 0x4cd1ee, '83 f8 01 a1 34 6f 60 00 75 0c 81 88 00 01 00 00 00 40 00 00 eb 0a 81 a0 00 01 00 00 ff bf ff ff'),
    ('dialog_caption_page1912_id1258', 0x4cda36, '68 ea 04 00 00 ba 78 07 00 00 e8 bb d7 fd ff 50 e8 e5 e2 ff ff 83 c4 08 50 68 ea 04 00 00 56 ff d7 50 ff d5'),
    ('ctor_audio_only_forces_0x40', 0x4cf504, '8b 45 0c a8 10 74 03 83 c8 40'),
    ('ctor_amms_initialize', 0x4cf548, '8b 06 8b 10 57 6a 01 57 50 8b 42 30 ff d0 85 c0'),
    ('ctor_audio_stream_gate_0x8', 0x4cf591, 'f6 83 8c 00 00 00 08 0f 85 b3 00 00 00'),
    ('ctor_D1_test_bit_0x4000', 0x4cf59e, '8b 0d 34 6f 60 00 f7 81 00 01 00 00 00 40 00 00 74 53'),
    ('ctor_D1_alt_addmediastream_flags', 0x4cf5c0, '8b 8b 8c 00 00 00 8b 43 04 8b 10 8b 52 3c c1 e9 06 57 f7 d1 83 e1 01 51 68 84 2b 53 00 6a 00 50 ff d2'),
    ('ctor_audio_stream_failed_sets_0x8', 0x4cf64a, '83 8b 8c 00 00 00 08'),
    ('ctor_iaudiomediastream_gate_0x8', 0x4cfafc, 'f6 83 8c 00 00 00 08 0f 85 a9 00 00 00'),
    ('ctor_get_filter_graph', 0x4cfbd3, '8b 43 04 8b 10 8d 4b 78 51 50 8b 42 34 ff d0'),
    ('ctor_audio_decoder_arm_select', 0x4cfc32, '8b 83 8c 00 00 00 a8 08 74 0c 39 7c 24 1c 0f 84 d4 01 00 00 eb 06 39 7c 24 1c 74 61'),
    ('ctor_speech_or_mpeg_audio', 0x4cfcaf, 'a9 00 01 00 00 0f 84 e5 00 00 00 a8 10 0f 84 e1 00 00 00'),
    ('ctor_mpeg_audio_decoder', 0x4cfd9f, 'a8 10 75 60 c7 44 24 10 00 00 00 00 8d bb 9c 00 00 00 57 68 a4 2a 53 00 6a 03 6a 00 68 44 2b 53 00'),
    ('ctor_video_decoder_gate', 0x4cfe1a, 'f6 83 8c 00 00 00 10 0f 85 80 00 00 00 83 7c 24 20 00 75 79 83 7c 24 1c 00 75 72'),
    ('ctor_manual_render_needs_both_mpeg_decoders', 0x4cff4b, 'b9 a4 3b 56 00 8d 44 24 40 e8 a7 97 f9 ff 85 c0 75 1a b9 bc 3b 56 00 8d 44 24 24 e8 95 97 f9 ff 85 c0 75 08 c7 44 24 18 01 00 00 00'),
    ('ctor_D2_dsound_renderer_gate', 0x4cff87, 'f6 83 8c 00 00 00 48 75 6b 8b 0d 34 6f 60 00 f7 81 00 01 00 00 00 40 00 00 75 59'),
    ('ctor_D3_manual_render_gate', 0x4d0024, '33 ff 39 7c 24 18 0f 84 00 01 00 00 8b 0d 34 6f 60 00 f7 81 00 01 00 00 00 40 00 00 0f 85 ea 00 00 00'),
    ('ctor_manual_addsource_findpin_render', 0x4d0046, '8b 43 78 8b 10 8b 52 38 8d b3 a8 00 00 00 56 68 f0 3b 56 00 8d 8c 24 c0 01 00 00 51 50 ff d2 3b c7 7c 5f 8b 06 8d 54 24 10 52 89 7c 24 14 8b 08 68 0c 3c 56 00 50 8b 41 2c ff d0 3b c7 7c 43 8b 43 78 8b 54 24 10 8b 08 52 50 8b 41 30 ff d0'),
    ('ctor_openfile_path', 0x4d0130, '8b 43 04 8b 08 6a 00 8d 94 24 bc 01 00 00 52 50 8b 41 40 ff d0'),
    ('script_get_sysd3dflags2', 0x497c68, '8b 0d 34 6f 60 00 8b 91 00 01 00 00 a1 e4 85 60 00 52 50 8b 44 24 10 e8 6c cb 00 00 b8 01 00 00 00 c3'),
    ('script_set_sysd3dflags2', 0x497c8a, '8b 4c 24 14 8b 41 01 8b 15 34 6f 60 00 50 89 82 00 01 00 00'),
    ('p_module_jump_table_9_10', 0x498078, '68 7c 49 00 8a 7c 49 00'),
    ('p_module_name_table_9_10', 0x57a3b4, '38 14 56 00 24 14 56 00'),
]

GUIDS = {
    0x532ae4: ('CLSID_AMMultiMediaStream', '49c47ce5-9ba4-11d0-8212-00c04fc32c45'),
    0x532af4: ('IID_IAMMultiMediaStream', 'bebe595c-9a6f-11d0-8fde-00c04fd9189d'),
    0x532b84: ('MSPID_PrimaryAudio', 'a35ff56b-9fda-11d0-8fdf-00c04fd9189d'),
    0x532b94: ('MSPID_PrimaryVideo', 'a35ff56a-9fda-11d0-8fdf-00c04fd9189d'),
    0x532b34: ('CLSID_DSoundRender', '79376820-07d0-11cf-a24d-0020afd79767'),
    0x532b44: ('CLSID_CMpegAudioCodec', '4a2286e0-7bef-11ce-9bd9-0000e202599c'),
    0x532b54: ('CLSID_CMpegVideoCodec', 'feb50740-7bef-11ce-9bd9-0000e202599c'),
    0x532b64: ('CLSID_MPEG1Splitter', '336475d0-942a-11ce-a870-00aa002feab5'),
    0x563b10: ('CLSID MPEG Layer-3 Decoder', '38be3000-dbf4-11d0-860e-00a024cfef6d'),
    0x563af0: ('CLSID_DMOWrapperFilter', '94297043-bd82-4dfd-b0de-8177739c6d20'),
}
ASCII = {
    0x562c94: 'VideoD3DFlags2', 0x562bb8: 'Software\\EGOSOFT\\%s',
    0x561438: 'P_GetSysD3DFlags2', 0x561424: 'P_SetSysD3DFlags2',
    0x563ba4: 'X MPEG Audio Decoder', 0x563bbc: 'X MPEG Video Decoder',
    0x563b74: 'X MPEG Layer-3 Decoder', 0x563bd4: 'X MPEG-I Stream Splitter',
}
WIDE = {0x563bf0: 'X File Source', 0x563c0c: 'Output', 0x6f2788: 'Disable Manual Codec Control'}

# Whole-file occurrences of renderer/source CLSIDs (little-endian GUID bytes).
CLSID_COUNTS = {
    'VideoRenderer': ('70e102b0-5556-11ce-97c0-00aa0055595a', 0),
    'VMR7': ('b87beb7b-8d29-423f-ae4d-6582c10175ac', 0),
    'VMR9': ('51b4abf3-748f-4e3b-a276-c828330e926a', 0),
    'EVR': ('fa10746c-9b63-4b6c-bc49-fc300ea5f256', 0),
    'NullRenderer': ('c1f400a4-3f08-11d3-9f0b-006008039e37', 0),
    'SampleGrabber': ('c1f400a0-3f08-11d3-9f0b-006008039e37', 0),
    'OverlayMixer': ('cd8743a1-3736-11d0-9e69-00c04fd7c15b', 0),
    'AsyncReader': ('e436ebb5-524f-11ce-9f53-0020af0ba770', 0),
    'LAVVideo': ('ee30215d-164f-4a92-a4eb-9d4c13390f9f', 0),
    'LAVSplitterSource': ('b98d13e7-55db-4385-a33d-09fd1ba26338', 0),
    'DSoundRender': ('79376820-07d0-11cf-a24d-0020afd79767', 1),
    'AMMultiMediaStream': ('49c47ce5-9ba4-11d0-8212-00c04fc32c45', 1),
}

# Every direct encoding of bit 0x4000 on [reg+0x100] (disp32 forms, no SIB).
CENSUS = {
    'test_dword': (rb'\xf7[\x80-\x83\x85-\x87]\x00\x01\x00\x00\x00\x40\x00\x00',
                   [0x4cce36, 0x4cd7c3, 0x4cf5a4, 0x4cff96, 0x4d0036]),
    'or_dword': (rb'\x81[\x88-\x8b\x8d-\x8f]\x00\x01\x00\x00\x00\x40\x00\x00', [0x4cd1f8]),
    'and_dword_clear': (rb'\x81[\xa0-\xa3\xa5-\xa7]\x00\x01\x00\x00\xff\xbf\xff\xff', [0x4cd204]),
    'test_byte_0x101_0x40': (rb'\xf6[\x80-\x83\x85-\x87]\x01\x01\x00\x00\x40', []),
    'bt_bit14': (rb'\x0f\xba[\xa0-\xa3\xa5-\xa7\xb0-\xb3\xb5-\xb7\xa8-\xab\xad-\xaf\xb8-\xbb\xbd-\xbf]\x00\x01\x00\x00\x0e', []),
}

# CrossOver amstream.dll (image base 0x10000000): (name, VA, hex)
AMS_PATTERNS = [
    ('addmediastream_ret_stream_in_ebx', 0x1000d21f, '8b5d18'),
    ('addmediastream_flag_adddefaultrenderer', 0x1000d2b0, '8b4514a8010f85a1000000'),
    ('adddefaultrenderer_with_ret_stream_is_e_invalidarg', 0x1000d35c, 'b85700078085db0f854e010000'),
    ('openfile_renderex_flags_not_flags_and_1', 0x1000d7f2, '8b4510f7d083e0018b4df08b116a0050ff731851ff5250'),
    ('openfile_partial_render_ok_cannot_render_to_cannot_connect', 0x1000d80b, '3d420204000f45c83d18020480bf170204800f45f9'),
]


class PE:
    def __init__(self, data):
        self.b = data
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        ns = struct.unpack_from('<H', data, pe + 6)[0]
        so = struct.unpack_from('<H', data, pe + 20)[0]
        self.base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
        self.secs = []
        for k in range(ns):
            o = pe + 24 + so + 40 * k
            name = data[o:o + 8].rstrip(b'\0').decode()
            vs, va, rs, ra = struct.unpack_from('<IIII', data, o + 8)
            self.secs.append((name, va, vs, ra, rs))

    def off(self, va):
        r = va - self.base
        for _, sva, _, ra, rs in self.secs:
            if sva <= r < sva + rs:
                return r - sva + ra
        raise ValueError(hex(va))

    def va(self, off):
        for _, sva, _, ra, rs in self.secs:
            if ra <= off < ra + rs:
                return self.base + sva + off - ra
        return None

    def at(self, va, n):
        o = self.off(va)
        return self.b[o:o + n]


def main():
    fails = []
    out = {}
    data = EXE.read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    out['exe_sha256'] = sha
    if sha != SHA256:
        fails.append('exe hash')
    pe = PE(data)
    pats = {}
    for name, va, hx in PATTERNS:
        want = bytes.fromhex(hx)
        ok = pe.at(va, len(want)) == want
        pats[name] = ok
        if not ok:
            fails.append('pattern ' + name)
    out['patterns'] = {'count': len(PATTERNS), 'pass': sum(pats.values())}
    for va, (name, want) in GUIDS.items():
        got = str(uuid.UUID(bytes_le=pe.at(va, 16)))
        if got != want:
            fails.append('guid ' + name)
    out['guids_checked'] = len(GUIDS)
    for va, want in ASCII.items():
        if pe.at(va, len(want) + 1) != want.encode() + b'\0':
            fails.append('string ' + want)
    for va, want in WIDE.items():
        if pe.at(va, 2 * len(want) + 2) != want.encode('utf-16le') + b'\0\0':
            fails.append('wide ' + want)
    out['strings_checked'] = len(ASCII) + len(WIDE)
    counts = {}
    for name, (guid, want) in CLSID_COUNTS.items():
        counts[name] = data.count(uuid.UUID(guid).bytes_le)
        if counts[name] != want:
            fails.append('clsid count ' + name)
    out['clsid_counts'] = counts
    _, tva, tvs, tra, trs = next(s for s in pe.secs if s[0] == '.text')
    text = data[tra:tra + trs]
    census = {}
    for key, (rx, want) in CENSUS.items():
        hits = sorted(pe.va(tra + m.start()) for m in re.finditer(rx, text))
        census[key] = [hex(h) for h in hits]
        if hits != want:
            fails.append('census ' + key)
    out['census_bit_0x4000_on_0x100'] = census
    # Effective launcher caption: text page 1912, id 1258 (last archive layer wins).
    try:
        sys.path.insert(0, str(ROOT / 'tools/analysis'))
        import sector_fog_census as sfc
        assets = sfc.Assets(EXE.parent)
        entries = assets.entries.get('addon/t/0001-l044.xml') or assets.entries.get('t/0001-l044.xml')
        text_l044 = assets.read_entry(entries[-1]).decode('utf-8', 'replace')
        page = re.search(r'<page id="\d*1912"[^>]*>(.*?)</page>', text_l044, re.S)
        cap = re.search(r'<t id="1258">([^<]*)</t>', page.group(1)) if page else None
        out['caption_1258'] = {'source': entries[-1]['source'], 'member': entries[-1]['path'],
                               'text': cap.group(1) if cap else None}
        if not cap or 'Alternative Video' not in cap.group(1):
            fails.append('caption 1258')
    except Exception as exc:  # archive layout problem is a failure, not a skip
        fails.append('caption read: %r' % exc)
    if AMSTREAM.exists():
        ab = AMSTREAM.read_bytes()
        ams = {'sha256': hashlib.sha256(ab).hexdigest()}
        if ams['sha256'] != AMSTREAM_SHA256:
            fails.append('amstream hash')
        ape = PE(ab)
        for name, va, hx in AMS_PATTERNS:
            want = bytes.fromhex(hx)
            ok = ape.at(va, len(want)) == want
            ams[name] = ok
            if not ok:
                fails.append('amstream ' + name)
        out['amstream_syswow64'] = ams
    else:
        out['amstream_syswow64'] = 'absent'
    # Observation only: the bottle's current setting.
    reg = BOTTLE / 'user.reg'
    obs = None
    if reg.exists():
        section = None
        for line in reg.read_text(encoding='latin-1').splitlines():
            if line.startswith('['):
                section = line.split(']')[0][1:]
            elif section == 'Software\\\\EGOSOFT\\\\X3AP' and line.startswith('"VideoD3DFlags2"='):
                raw = line.split('=', 1)[1]
                val = (int(raw[4:], 16) if raw.startswith('dword:')
                       else int.from_bytes(bytes.fromhex(raw[4:].replace(',', '')), 'little'))
                obs = {'value': hex(val), 'alt_video_bit_0x4000': bool(val & 0x4000), 'raw': raw}
    out['bottle_registry_observation'] = obs
    out['fails'] = fails
    out['result'] = 'PASS' if not fails else 'FAIL'
    print(json.dumps(out, indent=1))
    return 0 if not fails else 1


if __name__ == '__main__':
    sys.exit(main())
