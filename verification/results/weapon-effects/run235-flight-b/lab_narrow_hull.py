"""The lab's shield-hit preview on the three hulls (sphere, narrow 1:1:6 mid flank, narrow aft flank) at 1.2 s of 4 hits/s on a
corvette: lit pixels (blue > 70) and the label (regions, cells) per hull and frame, through node.
Usage: python3 verification/results/weapon-effects/run235-flight-b/lab_narrow_hull.py
"""
import json, re, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools/effects'))
import generate_weapon_effects_lab as gen
html = (ROOT / 'tools/effects/weapon_effects_lab.html').read_text()
script = re.search(r'<script>\n(.*)</script>', html, re.S).group(1)
header = script[script.index(gen.HEADER_BEGIN):script.index(gen.END, script.index(gen.HEADER_BEGIN))]
ours = script[script.index('// ---------------------------------------------------------------- our renderer'):
              script.index('// ---------------------------------------------------------------- helpers')]
helpers = script[script.index('function rng('):script.index('function hash(')]
probe = (header + helpers + ours).replace('const HEADER', 'var HEADER') + r'''
const W = 480, H = 270; function fmt(v) { return v; } function assets() { return null; }
function drawBackground() {} let lastLabel = ''; function label(ctx, t) { lastLabel = t; }
const vals = {c_cell_fighter: HEADER.cell_m[0], c_cell_corvette: HEADER.cell_m[1], c_cell_capital: HEADER.cell_m[2],
  c_rings_fighter: HEADER.rings[0], c_rings_corvette: HEADER.rings[1], c_rings_capital: HEADER.rings[2],
  c_ripple_speed: HEADER.ripple_speed, c_centre_gain: HEADER.centre_gain, c_fade_ms: HEADER.fade_ms, c_stagger: HEADER.stagger,
  c_fill_ratio: HEADER.fill_ratio, c_colour_high: HEADER.colour_high, c_colour_mid: HEADER.colour_mid,
  c_colour_low: HEADER.colour_low, c_colour_full: '#a0c0ff', c_colour_half: '#ffff80', c_colour_empty: '#ff8080',
  c_weapon_tint: 0, c_seta: 1, c_hits: 4, c_tier: 1, c_shield: 100, c_drain: 0, c_frame: 'patch', c_hull: 'sphere'};
function $(id) { return {value: vals[id]}; }
const out = {};
for (const hull of ['sphere', 'narrow', 'narrow_aft']) for (const frame of ['patch', 'ship']) {
  vals.c_hull = hull; vals.c_frame = frame;
  const data = new Uint8ClampedArray(W * H * 4);
  const t0 = Date.now();
  renderOurs({slot: 'impact', ctx: {setTransform() {}, getImageData() { return {data}; }, putImageData() {}}, weapon: {}}, 1200);
  let lit = 0;
  for (let i = 0; i < W * H; i++) if (data[i * 4 + 2] > 70) lit++;
  out[hull + '/' + frame] = {lit, ms: Date.now() - t0, label: lastLabel};
}
console.log(JSON.stringify(out, null, 1));
'''
with tempfile.NamedTemporaryFile('w', suffix='.js', delete=False) as f:
    f.write(probe)
r = subprocess.run(['node', f.name], capture_output=True, text=True)
print(r.stdout, r.stderr[:2000])
