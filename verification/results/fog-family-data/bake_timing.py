#!/usr/bin/env python3
"""Time the shared bake against one family's density/colour/packetize steps (run from the repo root)."""
import sys, time
sys.path.insert(0, 'tools'); sys.path.insert(0, 'tools/build')
import numpy as np
import fog_field_recipe as recipe
import bake_fog_fields as b
t0 = time.monotonic(); shared = b.bake_fields(); t1 = time.monotonic()
carrier, mask, eligible, thr, interp = shared
name = 'fogbluedistance'; p = recipe.PROFILES[name]
d = b.family_density(carrier, mask, eligible, thr, p['occupancy']); t2 = time.monotonic()
atlas = b.atlas_from_volume(b.colour_volume(d, interp, p['colours'])); t3 = time.monotonic()
packet, row = b.packetize(atlas, p); t4 = time.monotonic()
print(dict(shared_bake_s=round(t1-t0,2), family_density_s=round(t2-t1,2), colour_atlas_s=round(t3-t2,2), packetize_s=round(t4-t3,2), packet_bytes=len(packet), decoded_bytes=row['decoded_bytes'], nonzero_texels=row['nonzero_texels'], sha_ok=row['decoded_sha256']==p['atlas_sha256']))
