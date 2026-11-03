#!/usr/bin/env python3
"""Each single-byte mutation at a pinned FL text-density site must turn verify_text_density_sites_fl.verify() to FAIL
(in memory; the executable is only read)."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'probe'))
import verify_text_density_sites_fl as v  # noqa: E402
from verify_chase_camera_site import FlImage  # noqa: E402

data = Path('/mnt/c/X3FL/X3FL.exe').read_bytes()
img = FlImage(data)
MUTATE = (0x0a8990, 0x0a8993, 0x0a69f7, 0x0a69fc, 0x0a7b32, 0x0a7f80, 0x0b22f6, 0x0a6d92, 0x0a6b62, 0x1151b8,
          0x01dfda, 0x01e001, 0x01e005, 0x01e00c, 0x109edb, 0x10a373, 0x11519d, 0x1151a6, 0x0a7d6f, 0x10bd00,
          0x0b2355, 0x18cc39, 0x1123ab)
fails = 0
for rva in MUTATE:
    off = img.file_offset(0x400000 + rva)
    mutated = bytearray(data)
    mutated[off] ^= 0x01
    result = v.verify(bytes(mutated))['result']
    fails += result == 'FAIL'
    print(f'{rva:#08x} {result}')
print(f'rejected {fails}/{len(MUTATE)}; clean {v.verify(data)["result"]}')
