#!/usr/bin/env python3
"""The shadow replay's alpha caster pair through the native D3D9 runtime validator
(d3d9.dll!Direct3DShaderValidatorCreate9, verification/results/hull-widen-native-validator/native_validate.cpp)
on the Windows side of this WSL2 host: the working tree's alpha_pixel_words / alpha_vertex_words and, as the
control, the same programs of a base source file (the pass before the change). Writes validator.txt beside this
script (the validator's messages and End() result per program) and prints the instruction counts.
usage: validate_alpha_program.py BASE_SOURCE   (a copy of src/renderer/shadow_replay_pass.cpp before the change)"""
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
WORK = ROOT / 'verification/probe/build/shadow-penumbra/validator'
SOURCE = ROOT / 'src/renderer/shadow_replay_pass.cpp'


def words(text, name):
    body = re.search(r'constexpr DWORD ' + name + r'\[\] = \{(.*?)\};', text, re.S).group(1)
    body = re.sub(r'//[^\n]*', '', body)
    return [int(t.strip().rstrip('u'), 16) if t.strip().startswith('0x') else int(t.strip().rstrip('u'))
            for t in body.split(',') if t.strip()]


def instructions(w):
    """Arithmetic and texture instructions (def and dcl excluded), the SM3 slot count of these programs."""
    n, i = 0, 1
    while w[i] != 0x0000ffff:
        op, length = w[i] & 0xffff, (w[i] >> 24) & 0xf
        n += op not in (0x1f, 0x51)
        i += 1 + length
    return n


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    WORK.mkdir(parents=True, exist_ok=True)
    exe = WORK / 'native_validate.exe'
    subprocess.run(['i686-w64-mingw32-g++', '-O1', '-static',
                    str(ROOT / 'verification/results/hull-widen-native-validator/native_validate.cpp'), '-o', str(exe)],
                   check=True)
    sources = {'tree': SOURCE.read_text(), 'base': Path(sys.argv[1]).read_text()}
    files, counts = [], {}
    for tag, text in sources.items():
        for name in ('alpha_pixel_words', 'alpha_vertex_words'):
            w = words(text, name)
            path = WORK / f'{tag}_{name}.bin'
            path.write_bytes(struct.pack('<%dI' % len(w), *w))
            files.append(path.name)
            counts[f'{tag} {name}'] = (len(w), instructions(w))
    done = subprocess.run([str(exe), *files], cwd=WORK, capture_output=True)
    text = done.stdout.decode('utf-8', 'replace').replace('\r\n', '\n')
    lines = [f'# {k}: words={v[0]} instructions={v[1]}' for k, v in counts.items()] + [text.rstrip()]
    ends = re.findall(r'end=([0-9a-f]+) messages=(\d+) instruction_worst=([0-9a-f]+)', text)
    ok = len(ends) == len(files) and all(e == ('00000000', '0', '00000000') for e in ends)
    lines.append(f'VALIDATOR {"PASS" if ok else "FAIL"} programs={len(ends)}')
    (HERE / 'validator.txt').write_text('\n'.join(lines) + '\n')
    print('\n'.join(lines))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
