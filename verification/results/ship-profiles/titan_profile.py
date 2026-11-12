"""The FL Argon Titan's ship profile (tables.ship_profiles, tools/effects/ship_profiles.py) against the FL 67 walk
figures (verification/results/fl-run67/titan_scene.py; docs/verification/weapon-effects.md "FL 67 A").
Usage: python3 -I verification/results/ship-profiles/titan_profile.py [game-root] > titan_profile.json
Prints one JSON object: the Titan entry, the widest slice, the generation time of the section and the walk figures.
Frozen: titan_profile.json is the profile before the hull rules and the easing (commit 0defbd821), the figures the
native fixture's titan_profile() embeds; rerunning this gives the current rules' entry. Superseded by hull_rules.py
(titan_profile_raw.json, titan_profile_smoothed.json, hull_rules_fl.txt)."""
import json, sys, time
from pathlib import Path
R = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(R / 'tools/effects'), str(R / 'tools/analysis')]
import engine_bodies, ship_profiles

root = Path(sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3FL')
assets, _ = engine_bodies.load_assets(root, False, 'fl')
t = time.monotonic()
entries, skipped, prov = ship_profiles.ship_profiles(assets)
seconds = round(time.monotonic() - t, 2)
titan = next(e for e in entries if e['scene'].lower() == 'ships\\argon\\argon_m2_scene' and e['class'] == 'OBJ_SHIP_M2')
area = [s[2] * s[3] for s in titan['slices']]
w = max(range(len(area)), key=area.__getitem__)
hx = max(range(len(area)), key=lambda i: titan['slices'][i][2])
hy = max(range(len(area)), key=lambda i: titan['slices'][i][3])
step = (titan['z1'] - titan['z0']) / len(titan['slices'])
# FL 67 walk figures (engine units): the hull part at z 17329, LOD-0 value 528422, stored extent (151433, 166374, 528422)
# from titan_scene.py; the walked box c (0, -2610, 17329), e (174148, 194332, 607685); scene_radius estimate 545751.
walk = dict(hull_z=[17329 - 528422, 17329 + 528422], hull_half_extent=[151433, 166374, 528422],
            walk_c=[0, -2610, 17329], walk_e=[174148, 194332, 607685], scene_radius_estimate=545751)
print(json.dumps(dict(game_root=str(root), tships=prov['tships'], section_seconds=seconds, profiles=prov['profiles'],
                      skipped=prov['skipped'], titan=titan,
                      widest_slice=dict(index=w, z=[round(titan['z0'] + w * step, 1),
                                                    round(titan['z0'] + (w + 1) * step, 1)],
                                        cx_cy_hx_hy=titan['slices'][w]),
                      max_hx_slice=hx, max_hy_slice=hy, walk=walk), indent=1))
