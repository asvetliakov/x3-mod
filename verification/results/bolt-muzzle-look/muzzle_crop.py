#!/usr/bin/env python3
"""muzzle_crop.py <ppm> <fixture.log> <out.png> [scale=4] [half_w=100] [half_h=60]: the step-4 dump's muzzle crop. The
muzzle's centre is the BOLT_DUMP row's muzzle_x / muzzle_y for the PPM's case, width and time (the fixture's
dump_scenarios, weapon_effects_bolt_draw_cases.h); the window (2 half_w x 2 half_h px) is scaled up `scale` times
(nearest). Pillow."""
import re
import sys
from pathlib import Path
from PIL import Image

ppm, log, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
scale = int(sys.argv[4]) if len(sys.argv) > 4 else 4
hw = int(sys.argv[5]) if len(sys.argv) > 5 else 100
hh = int(sys.argv[6]) if len(sys.argv) > 6 else 60
m = re.fullmatch(r'(.+)_(\d+)x(\d+)-(\d+)', ppm.stem)
case, width, ms = m[1], m[2], m[4]
x = y = None
for line in log.read_text(errors='replace').splitlines():
    if (line.startswith('BOLT_DUMP ') and f'case={case} ' in line and f'width={width} ' in line and f't_ms={ms} ' in line
            and 'muzzle_x=' in line):
        x = float(re.search(r'muzzle_x=(\S+)', line)[1])
        y = float(re.search(r'muzzle_y=(\S+)', line)[1])
if x is None:
    raise SystemExit(f'no BOLT_DUMP muzzle row for {ppm.stem} in {log}')
img = Image.open(ppm).convert('RGB')
box = (int(round(x)) - hw, int(round(y)) - hh, int(round(x)) + hw, int(round(y)) + hh)
crop = img.crop(box).resize((2 * hw * scale, 2 * hh * scale), Image.NEAREST)
crop.save(out)
print(f'{out}: centre {x:.1f},{y:.1f} box {box} scale {scale}')
