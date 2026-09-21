#!/usr/bin/env python3
"""Read-only structural verification of the chase aim diagnostic hook sites.

The expected bytes alone are insufficient on x86: an opcode-looking byte can
occur inside another instruction.  This probe asks MinGW objdump to decode each
complete containing function, then requires every patch span to start and end
on decoded instruction boundaries and rejects real direct branches into a span
interior.  It also checks that the production SiteSpec declarations still match
the independently derived addresses, bytes and plain-copy relocation fields.

usage: verify_chase_aim_sites.py [--exe PATH] [--source PATH] [--json]
"""
import argparse
import dataclasses
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from verify_chase_camera_site import (  # Reuse installed-image provenance and PE mapping.
    DEFAULT_EXE,
    EXPECTED_SHA256,
    EXPECTED_SIZE,
    IMAGE_BASE,
    Image,
)


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCE = ROOT / 'src/proxy/chase_aim_trace.cpp'
OBJDUMP = 'i686-w64-mingw32-objdump'


@dataclasses.dataclass(frozen=True)
class HookSpec:
    name: str
    va: int
    expected: bytes
    function_start: int
    function_end: int  # exclusive

    @property
    def end(self):
        return self.va + len(self.expected)


@dataclasses.dataclass(frozen=True)
class Instruction:
    va: int
    raw: bytes
    mnemonic: str
    operands: str

    @property
    def end(self):
        return self.va + len(self.raw)


# Function bounds come from the complete read-only Ghidra listings named in
# docs/reverse-engineering/chase-mouse-fire.md.  The end is one byte beyond the
# inclusive listing bound, after each function's final ret instruction.
FIRE_FUNCTION = (0x00445170, 0x00446BFA)
WRITER_FUNCTION = (0x00406DE0, 0x0040770B)
SITES = (
    HookSpec('chase_fire_gate', 0x00445A15, bytes.fromhex('8b43148bf8'), *FIRE_FUNCTION),
    HookSpec('chase_fire_ray', 0x00445B70, bytes.fromhex('8945e48b45e4'), *FIRE_FUNCTION),
    HookSpec('chase_fire_final', 0x0044605A, bytes.fromhex('8b53088b4a70'), *FIRE_FUNCTION),
    HookSpec('chase_cursor_write', 0x004074DE, bytes.fromhex('890de87c6000'), *WRITER_FUNCTION),
)


_INSTRUCTION_RE = re.compile(
    r'^\s*([0-9a-fA-F]+):\s*'
    r'((?:[0-9a-fA-F]{2}(?:\s+|$))+)\s*'
    r'(\S+)(?:\s+(.*?))?\s*$'
)
_SOURCE_SPEC_RE = re.compile(
    r'\{\s*"([^"]+)"\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*'
    r'\{([^}]*)\}\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}'
)


def parse_objdump(text, start, end):
    """Parse and require a gap-free objdump decode of [start, end)."""
    instructions = []
    malformed = []
    for line in text.splitlines():
        if not re.match(r'^\s*[0-9a-fA-F]+:', line):
            continue
        match = _INSTRUCTION_RE.match(line)
        if not match:
            malformed.append(line.strip())
            continue
        raw = bytes.fromhex(match.group(2))
        instructions.append(Instruction(
            int(match.group(1), 16), raw, match.group(3).lower(), match.group(4) or ''))
    if malformed:
        raise ValueError(f'unparsed objdump instruction line: {malformed[0]}')
    if not instructions:
        raise ValueError(f'objdump decoded no instructions in {start:#010x}..{end:#010x}')
    cursor = start
    for instruction in instructions:
        if instruction.va != cursor:
            raise ValueError(
                f'objdump coverage gap/overlap at {cursor:#010x}, next {instruction.va:#010x}')
        if not instruction.raw or instruction.mnemonic == '(bad)':
            raise ValueError(f'invalid instruction at {instruction.va:#010x}')
        cursor = instruction.end
    if cursor != end:
        raise ValueError(f'objdump coverage ends at {cursor:#010x}, expected {end:#010x}')
    return instructions


# Enough trailing bytes for an instruction that straddles the window's end to
# decode exactly as it does inside the whole image (the longest x86 instruction
# is 15 bytes).
CODE_WINDOW_SLACK = 16


def image_bytes(source):
    """The image bytes of a path, or the bytes themselves when already in memory."""
    if isinstance(source, (bytes, bytearray)):
        return bytes(source)
    return Path(source).read_bytes()


def objdump_window(source, start, end, objdump=OBJDUMP, timeout=60):
    """objdump's decode text for the virtual range [start, end) of an image.

    `source` is either a path to a container objdump recognises, which is the
    installed-executable verification path and is passed through unchanged, or
    the image bytes. Bytes are mapped VA -> file offset with Image and only
    that code window is handed to objdump, as a headerless binary placed at its
    own virtual address: no MZ/PE header is ever written out.

    That matters because Microsoft Defender for Endpoint classifies the
    synthetic PE images these checks build as Trojan:Win32/Wacatac.C!ml and
    quarantines them while a test is running. The detection is content-based
    and racy: neither the file name nor inert trailing padding avoids it, and a
    quarantined file makes objdump fail with "Operation not permitted" and then
    vanishes. A raw code blob is not a portable executable, so there is nothing
    to classify. Both branches reach the same parse_objdump contract, and the
    decoded instructions are identical.
    """
    tool = shutil.which(objdump)
    if not tool:
        raise RuntimeError(f'{objdump} not found')
    window = ['-Mintel', '--insn-width=16', f'--start-address={start:#x}', f'--stop-address={end:#x}']
    if not isinstance(source, (bytes, bytearray)):
        run = subprocess.run([tool, '-d', *window, str(source)],
                             check=True, capture_output=True, text=True, timeout=timeout)
        return run.stdout
    mapped = Image(bytes(source))
    code = mapped.read(start, end - start + CODE_WINDOW_SLACK) or mapped.read(start, end - start)
    if code is None:
        raise ValueError(f'no mapped image bytes for {start:#010x}..{end:#010x}')
    with tempfile.TemporaryDirectory(prefix='x3-code-window-') as directory:
        blob = Path(directory) / f'{start:08x}.code'
        blob.write_bytes(code)
        run = subprocess.run([tool, '-D', '-b', 'binary', '-m', 'i386',
                              f'--adjust-vma={start:#x}', *window, str(blob)],
                             check=True, capture_output=True, text=True, timeout=timeout)
        return run.stdout


def disassemble_functions(exe, objdump=OBJDUMP):
    """Decode each unique containing function from its known entry boundary."""
    decoded = {}
    for start, end in sorted({(site.function_start, site.function_end) for site in SITES}):
        try:
            text = objdump_window(exe, start, end, objdump=objdump, timeout=30)
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
            detail = getattr(error, 'stderr', '') or str(error)
            raise RuntimeError(f'objdump failed for {start:#010x}..{end:#010x}: {detail.strip()}') from error
        decoded[(start, end)] = parse_objdump(text, start, end)
    return decoded


def _is_direct_control(instruction):
    mnemonic = instruction.mnemonic
    if not (mnemonic == 'call' or mnemonic.startswith('j') or mnemonic.startswith('loop')):
        return None
    # Direct objdump operands start with a numeric address.  Memory/register
    # operands (for example "DWORD PTR [eax]") are intentionally excluded.
    match = re.match(r'^(?:short\s+|near\s+ptr\s+)?0x([0-9a-fA-F]+)(?:\s|$)', instruction.operands)
    return int(match.group(1), 16) if match else None


def _instruction_dict(instruction):
    return {
        'va': f'{instruction.va:#010x}',
        'length': len(instruction.raw),
        'bytes': instruction.raw.hex(),
        'mnemonic': instruction.mnemonic,
        'operands': instruction.operands,
    }


def inspect_site(image, spec, instructions):
    """Verify bytes, whole decoded instructions, and real incoming branches."""
    actual = image.read(spec.va, len(spec.expected))
    by_address = {instruction.va: instruction for instruction in instructions}
    span = []
    cursor = spec.va
    while cursor < spec.end:
        instruction = by_address.get(cursor)
        if instruction is None:
            break
        span.append(instruction)
        cursor = instruction.end
    whole = bool(span) and cursor == spec.end
    controls = [instruction for instruction in span if _is_direct_control(instruction) is not None]
    hits = []
    for instruction in instructions:
        target = _is_direct_control(instruction)
        if target is not None and spec.va < target < spec.end:
            hits.append({'at': f'{instruction.va:#010x}', 'target': f'{target:#010x}',
                         'mnemonic': instruction.mnemonic})
    return {
        'name': spec.name,
        'va': f'{spec.va:#010x}',
        'length': len(spec.expected),
        'bytes_ok': actual == spec.expected,
        'actual': actual.hex() if actual is not None else None,
        'expected': spec.expected.hex(),
        'whole_instructions': whole,
        'covered_end': f'{cursor:#010x}',
        'instructions': [_instruction_dict(instruction) for instruction in span],
        'plain_no_relative_control': whole and not controls,
        'relative_controls': [_instruction_dict(instruction) for instruction in controls],
        'no_interior_branch': not hits,
        'interior_branches': hits,
        'ok': actual == spec.expected and whole and not controls and not hits,
    }


def parse_source_specs(text):
    parsed = []
    for match in _SOURCE_SPEC_RE.finditer(text):
        byte_values = re.findall(r'0x([0-9a-fA-F]{1,2})', match.group(3))
        parsed.append({
            'name': match.group(1),
            'va': int(match.group(2), 16),
            'bytes': bytes(int(value, 16) for value in byte_values),
            'length': int(match.group(4)),
            'rel32_offset': int(match.group(5)),
            'rel32_target': int(match.group(6)),
        })
    return parsed


def check_source_specs(text, specs=SITES):
    actual = parse_source_specs(text)
    expected = [{
        'name': spec.name,
        'va': spec.va,
        'bytes': spec.expected,
        'length': len(spec.expected),
        'rel32_offset': 0,
        'rel32_target': 0,
    } for spec in specs]
    serial = lambda row: {
        **row, 'va': f"{row['va']:#010x}", 'bytes': row['bytes'].hex(),
    }
    return {
        'ok': actual == expected,
        'expected': [serial(row) for row in expected],
        'actual': [serial(row) for row in actual],
    }


def verify(data, decoded, source_text, sha256=None, specs=SITES):
    report = {'result': 'FAIL', 'checks': {}}
    checks = report['checks']
    digest = sha256 or hashlib.sha256(data).hexdigest()
    checks['sha256'] = {'ok': digest == EXPECTED_SHA256, 'value': digest}
    checks['size'] = {'ok': len(data) == EXPECTED_SIZE, 'value': len(data)}
    try:
        image = Image(data)
    except (ValueError, IndexError, struct.error) as error:
        checks['pe'] = {'ok': False, 'error': str(error)}
        return report
    checks['pe'] = {'ok': image.image_base == IMAGE_BASE,
                    'image_base': f'{image.image_base:#010x}'}
    checks['source_specs'] = check_source_specs(source_text, specs)
    function_rows = []
    sites = []
    for bounds in sorted({(site.function_start, site.function_end) for site in specs}):
        instructions = decoded.get(bounds)
        function_rows.append({
            'start': f'{bounds[0]:#010x}', 'end': f'{bounds[1]:#010x}',
            'instruction_count': len(instructions) if instructions else 0,
            'ok': bool(instructions),
        })
    checks['function_disassembly'] = {
        'ok': all(row['ok'] for row in function_rows), 'functions': function_rows}
    for spec in specs:
        instructions = decoded.get((spec.function_start, spec.function_end))
        if instructions:
            sites.append(inspect_site(image, spec, instructions))
        else:
            sites.append({'name': spec.name, 'va': f'{spec.va:#010x}', 'ok': False,
                          'error': 'containing function was not decoded'})
    checks['sites'] = {'ok': all(site['ok'] for site in sites), 'items': sites}
    report['result'] = 'PASS' if all(check.get('ok') for check in checks.values()) else 'FAIL'
    return report


def verify_path(exe, source=DEFAULT_SOURCE, sha256=None, objdump=OBJDUMP):
    data = image_bytes(exe)
    source_text = Path(source).read_text(encoding='utf-8')
    try:
        decoded = disassemble_functions(exe, objdump=objdump)
    except (RuntimeError, ValueError) as error:
        return {'result': 'FAIL', 'checks': {
            'disassembler': {'ok': False, 'error': str(error)},
        }}
    return verify(data, decoded, source_text, sha256=sha256)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE)
    parser.add_argument('--source', type=Path, default=DEFAULT_SOURCE)
    parser.add_argument('--json', action='store_true', help='print the full report')
    args = parser.parse_args()
    if not args.exe.is_file():
        print(json.dumps({'result': 'SKIP', 'reason': f'{args.exe} not found'}))
        return 2
    if not args.source.is_file():
        print(json.dumps({'result': 'FAIL', 'reason': f'{args.source} not found'}))
        return 1
    report = verify_path(args.exe, args.source)
    report['exe'] = str(args.exe)
    report['source'] = str(args.source)
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        summary = {'result': report['result'], 'exe': report['exe']}
        summary.update({name: check.get('ok', False) for name, check in report['checks'].items()})
        print(json.dumps(summary))
    return 0 if report['result'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
