#!/usr/bin/env python3
"""Derived FOV figures for docs/reverse-engineering/field-of-view.md (pure arithmetic, no game data).

Engine law (static, 0x004be460 + 0x004dac90): half = F*pi/65536 (F binary angle, 65536 = 360 deg);
m00 = cot(half)/W, m11 = cot(half)/H; default plane H = 0.75 (0xc000) and W = 0.75*w/h when h/w <= 0.75,
else W = 1.0 and H = h/w.
"""
import math

def plane(w, h):
    r = (h << 16) // w                     # 0x00412450(h, w), truncating
    if r > 0xC000:
        return 1.0, ((0x10000 * h) // w) / 65536.0
    return ((0xC000 * w) // h) / 65536.0, 0.75

def vertical(F, w, h):
    W, H = plane(w, h)
    t = math.tan(F * math.pi / 65536.0)
    return math.degrees(2 * math.atan(t * H)), math.degrees(2 * math.atan(t * W)), 1 / (t * W), 1 / (t * H)

def focus_for_vertical(vdeg, w, h):
    W, H = plane(w, h)
    half = math.atan(math.tan(math.radians(vdeg) / 2) / H)
    return round(half * 65536.0 / math.pi)

rows = []
for label, F in (("vanilla 0x4000", 0x4000), ("min 0x106", 0x106), ("zn threshold 0x2147", 0x2147),
                 ("SG_MIN_FOV 70", 70 * 65536 // 360), ("SG_MAX_FOV 100", 100 * 65536 // 360)):
    for w, h in ((1280, 768), (1920, 1080), (5120, 1440), (1280, 1024)):
        v, hz, m00, m11 = vertical(F, w, h)
        rows.append((label, F, w, h, v, hz, m00, m11))
for label, F, w, h, v, hz, m00, m11 in rows:
    print(f"{label:22s} F=0x{F:04x} {w}x{h}: vfov={v:8.4f} hfov={hz:8.4f} m00={m00:.6f} m11={m11:.6f}")
for vdeg in (59.0, 2 * math.degrees(math.atan(9 / 16)), 73.7398):
    for w, h in ((1920, 1080), (5120, 1440), (1280, 1024)):
        F = focus_for_vertical(vdeg, w, h)
        v, hz, m00, m11 = vertical(F, w, h)
        print(f"--fov {vdeg:8.4f} at {w}x{h}: F=0x{F:04x} ({F * 360 / 65536:.4f} deg) -> vfov={v:.4f} hfov={hz:.4f} lod_scale F/0x4000={F / 0x4000:.4f} true_ratio={1 / math.tan(F * math.pi / 65536):.4f}")
