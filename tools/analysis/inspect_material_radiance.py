#!/usr/bin/env python3
"""Derive exact-profile RGB clamp replacements from trusted local captures.

No game assets are bundled. Optional patched outputs must stay outside the repo;
the JSON contains only hashes, token offsets and the minimal replacement spans.
This is an offline specification inspector, not a runtime shader patcher.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

# Full-program digests include version, comment/preshader payloads and END.
PROFILES = (
    ("5f82ecacd39529cd", "e62d0f041430e54fdea5800ef6576d812a2486f12a980f921553e7292349d8c2", (1692, 1709), 1307, 24, 1),
    ("fffdabd910793aba", "8fc110cf9cf4631ac0f7052b8f61ec6c5908bcaa7be830853570404ddfa95b72", (1575, 1592), 1268, 22, 1),
    ("7c83ed50c9894e44", "a171c7d7e3dcdfc87816fc651bf93918399594ec457ce1822dcd47a0cff2374a", (1252,), 1091, 12, 1),
    ("8759c7838bbc86c2", "9fd15484fe419295cfb3534bd4f978efc8855c1e3e6a06e776533497dad48dc0", (1206,), 1041, 8, 1),
    ("f1b0e820c7b488c3", "8f6517d6730a53d52712a335253d272663e34d05e3cdcdccc93c05249014515e", (1718, 1735), 1307, 24, 2),
)


def fnv(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return f"{value:016x}"


def instructions(words):
    """Walk SM3 framing, excluding comments even if payload looks like opcodes."""
    assert words[0] == 0xffff0300
    result = {}
    offset = 1
    while offset < len(words):
        token = words[offset]
        opcode = token & 0xffff
        if token == 0x0000ffff:
            assert offset == len(words) - 1
            return result
        count = (token >> 16) & 0x7fff if opcode == 0xfffe else (token >> 24) & 15
        assert offset + count < len(words)
        if opcode != 0xfffe:
            result[offset] = words[offset:offset + count + 1]
        offset += count + 1
    raise ValueError("missing END")


def inspect(directory, output_directory=None):
    profiles = []
    for key, digest, sites, zero_offset, zero_register, component in PROFILES:
        data = (directory / f"ps_{key}.bin").read_bytes()
        assert hashlib.sha256(data).hexdigest() == digest and fnv(data) == key
        assert len(data) % 4 == 0
        words = list(struct.unpack(f"<{len(data)//4}I", data))
        spans = instructions(words)
        zero = spans[zero_offset]
        assert len(zero) == 6 and zero[:2] == [0x05000051, 0xa00f0000 | zero_register]
        assert zero[2 + component] == 0  # Exact +0 literal, not application state.
        zero_source = 0xa0000000 | ((component * 0x55) << 16) | zero_register
        detected = [offset for offset, tokens in spans.items()
                    if len(tokens) == 3 and tokens[0] == 0x02000001
                    and tokens[1] & 0x00100000 and tokens[2] == 0x90e40000]
        assert detected == list(sites)
        replacements = []
        patched = list(words)
        for offset in reversed(sites):
            before = spans[offset]
            assert before[1] in (0x80370001, 0x80370004, 0x80370005)
            after = [0x0300000b, before[1] & ~0x00100000, before[2], zero_source]
            patched[offset:offset + 3] = after
            replacements.append({
                "instruction_dword": offset, "destination_dword": offset + 1,
                "instruction_byte": offset * 4, "destination_byte": (offset + 1) * 4,
                "expected": [f"0x{x:08x}" for x in before],
                "replacement": [f"0x{x:08x}" for x in after],
            })
        # No instruction, comment or other payload changes outside selected spans.
        patched_spans = instructions(patched)
        reconstructed = list(patched)
        for ordinal, offset in reversed(list(enumerate(sites))):
            shifted = offset + ordinal
            assert patched_spans[shifted][0] == 0x0300000b
            reconstructed[shifted:shifted + 4] = spans[offset]
        assert reconstructed == words
        patched_data = struct.pack(f"<{len(patched)}I", *patched)
        if output_directory:
            (output_directory / f"ps_{key}.bin").write_bytes(patched_data)
        profiles.append({
            "fnv1a64": key, "sha256": digest, "version": "0xffff0300",
            "byte_count": len(data), "word_count": len(words),
            "zero_def_dword": zero_offset, "zero_register": zero_register,
            "zero_component": component, "zero_literal_dword": zero_offset + 2 + component,
            "zero_source_token": f"0x{zero_source:08x}",
            "sites": list(reversed(replacements)),
            "patched_byte_count": len(patched_data), "patched_word_count": len(patched),
            "patched_fnv1a64": fnv(patched_data),
            "patched_sha256": hashlib.sha256(patched_data).hexdigest(),
        })
    return {"schema": 1, "offset_units": "zero-based DWORD index from version token; byte offsets also supplied",
            "transformation": "mov_sat_pp temp.xyz, v0 -> max_pp temp.xyz, v0, shader-local DEF zero",
            "profiles": profiles}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture_directory", type=Path)
    parser.add_argument("--json", type=Path, required=True)
    parser.add_argument("--local-patched-directory", type=Path)
    args = parser.parse_args()
    if args.local_patched_directory:
        repo = Path(__file__).resolve().parents[2]
        output = args.local_patched_directory.resolve()
        if output == repo or repo in output.parents:
            parser.error("raw patched game shader outputs must stay outside the repository")
        if output == args.capture_directory.resolve():
            parser.error("never overwrite source capture directory")
        output.mkdir(parents=True, exist_ok=True)
    result = inspect(args.capture_directory, args.local_patched_directory)
    args.json.write_text(json.dumps(result, indent=2) + "\n")
    print(f"verified {len(result['profiles'])} profiles, {sum(len(p['sites']) for p in result['profiles'])} sites")


if __name__ == "__main__":
    main()
