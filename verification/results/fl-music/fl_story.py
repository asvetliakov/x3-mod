#!/usr/bin/env python3
"""Offline model check of the track queue's story detection on FL (docs/reverse-engineering/fl/music-keep.md,
"Port (2026-11-03)"; docs/verification/farnhams-legacy.md, "music family on FL (host evidence, 2026-11-03)").

Loads FL's `addon2/L/x3story.obj` from the installed `addon2/03.cat`, builds the RAM image of the loaded VM with the
offline model (verification/probe/story_vm_image.py: the converted CODE, class/method/string tables, native names) and
runs the production `story_detect.h` over it through the model's host harness. This is a model of the in-memory
object, not a game capture; the copyrighted member stays in a temporary directory. Prints the member's SHA-256, the
queue detection (fail, method, warp entry / fork / continue offsets, the stop / play IPs, depth, reads) and the SETA
detection the harness also runs. Reviewer's script, kept beside the port's other producers.

usage: python3 verification/results/fl-music/fl_story.py [scratch dir]
"""
import hashlib
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/probe'))
sys.path.insert(0, str(ROOT / 'verification/results/ship-navigation-ai/gate-queue'))
import story_vm_image as img  # noqa: E402
from kc_obj import load_member  # noqa: E402

raw = load_member('/mnt/c/X3FL/addon2/03.cat', 'addon2/L/x3story.obj')
print('member sha256', hashlib.sha256(raw).hexdigest())
with tempfile.TemporaryDirectory(dir=sys.argv[1] if len(sys.argv) > 1 else None) as t:
    host, log = img.compile_host(t)
    if host is None:
        print(log)
        sys.exit(1)
    p = Path(t) / 'image.bin'
    p.write_bytes(img.build_image(raw))
    r = img.run_host(host, p)
    print('queue', r['queue'])
    print('seta', {k: r['seta'].get(k) for k in ('fail', 'where', 'reads')})
