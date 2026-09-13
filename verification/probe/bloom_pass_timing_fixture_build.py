#!/usr/bin/env python3
"""Cross-compile the retained-CSO BloomPass timing fixture; never runs it."""
from pathlib import Path
import argparse
import subprocess

import bloom_pass_fixture_build as base


def build(directory: Path):
    directory.mkdir(parents=True, exist_ok=True)
    executable = directory / 'bloom_pass_timing_fixture.exe'
    command = [
        'i686-w64-mingw32-g++', *base.FLAGS,
        str(base.ROOT / 'verification/probe/bloom_pass_timing_fixture.cpp'),
        str(base.ROOT / 'src/renderer/bloom_pass.cpp'),
        '-o', str(executable), '-luser32', '-ldxguid',
    ]
    subprocess.run(command, cwd=base.ROOT, check=True)
    return executable, command


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path,
                        default=base.ROOT / 'verification/probe/build/bloom-pass-timing')
    print(build(parser.parse_args().output_dir)[0])
