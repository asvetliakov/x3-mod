#!/usr/bin/env python3
"""The lab's hull presets (tools/effects/weapon_effects_lab.html renderHull under node, the 720 x 405 preview): per
preset the pixels each element lights (luma above 1e-3 of its own deposit) and its summed light at 40 ms of wall time,
the chase nose's sparks at 200 ms, r_hit and the counts. Usage: python3 verification/results/weapon-effects/
hull_burst_lab_probe.py [html]"""
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/effects'))
import generate_weapon_effects_lab as gen  # noqa: E402

html = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / 'tools/effects/weapon_effects_lab.html').read_text()
script = re.search(r'<script>\n(.*)</script>', html, re.S).group(1)
header = script[script.index(gen.HEADER_BEGIN):script.index(gen.END, script.index(gen.HEADER_BEGIN))]
ours = script[script.index('// ---------------------------------------------------------------- our renderer'):
              script.index('// ---------------------------------------------------------------- helpers')]
helpers = script[script.index('function rng('):script.index('function hash(')]
ours, n = re.subn(r'(\n\s*)record\.hullStats = stats;', r'\1stats.r_hit = rHit; record.hullStats = stats;', ours)
assert n == 1
presets = [p for p in json.loads(re.search(r'const PRESETS = (\[.*?\]);', html).group(1)) if p['family'] == 'hull']
probe = (header + helpers + ours).replace('const HEADER', 'var HEADER') + r'''
const OW = 720, OH = 405; function fmt(v) { return v; } function assets() { return null; }
function drawBackground() {} function label() {}
const vals = {c_cell_fighter: HEADER.cell_m[0], c_cell_corvette: HEADER.cell_m[1], c_cell_capital: HEADER.cell_m[2],
  c_rings_fighter: HEADER.rings[0], c_rings_corvette: HEADER.rings[1], c_rings_capital: HEADER.rings[2],
  c_ripple_speed: HEADER.ripple_speed, c_centre_gain: HEADER.centre_gain, c_fade_ms: HEADER.fade_ms, c_stagger: HEADER.stagger,
  c_fill_ratio: HEADER.fill_ratio, c_colour_high: HEADER.colour_high, c_colour_mid: HEADER.colour_mid,
  c_colour_low: HEADER.colour_low, c_colour_full: '#a0c0ff', c_colour_half: '#ffff80', c_colour_empty: '#ff8080',
  c_weapon_tint: 0, c_seta: 1, c_hits: 0, c_tier: 0, c_shield: 100, c_drain: 0, c_frame: 'patch'};
const checks = {};
function $(id) { return {value: vals[id], checked: !!checks[id]}; }
const out = {};
for (const p of __PRESETS__) {
  Object.assign(vals, {c_family: 'hull', c_tier: p.tier, c_hull: p.hull, c_camera: p.camera, c_seta: p.seta, c_hits: p.hps,
                       c_variant: p.variant, c_k: p.k, c_shield: p.shield});
  checks.c_own = p.own; checks.c_cockpit = p.cockpit;
  const data = new Uint8ClampedArray(OW * OH * 4);
  const ctx = {setTransform() {}, getImageData() { return {data}; }, putImageData() {}};
  const rec = {slot: 'impact', ctx, weapon: {colour: [50, 255, 50]}};
  renderOurs(rec, 40);
  const st = rec.hullStats, row = {r_hit: +st.r_hit.toFixed(2), counts: [st.flashes, st.bursts, st.sparks, st.rings], lit_px: st.lit_px};
  if (p.camera === 'chase') { const later = {slot: 'impact', ctx, weapon: rec.weapon}; renderOurs(later, 200); row.sparks_200ms_px = later.hullStats.lit_px.sparks; }
  out[p.id] = row;
}
console.log(JSON.stringify(out));
'''.replace('__PRESETS__', json.dumps(presets))
with tempfile.NamedTemporaryFile('w', suffix='.js', delete=False) as f:
    f.write(probe)
r = subprocess.run(['node', f.name], capture_output=True, text=True, check=True)
Path(f.name).unlink()
for pid, row in json.loads(r.stdout).items():
    print(pid, json.dumps(row, separators=(',', ':')))
