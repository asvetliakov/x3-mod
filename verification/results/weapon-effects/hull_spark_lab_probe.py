#!/usr/bin/env python3
"""The lab's hull-hit sparks alone (tools/effects/weapon_effects_lab.html renderHull, instrumented under node): per
preset and game age, the spark quads drawn, their lit width and rest length (canvas px, 480x270), the pixels the sparks
light, the peak spark radiance per pixel and its tonemapped value (1 - e^-v, the lab's mapping), and r_hit in metres.
Usage: python3 verification/results/weapon-effects/hull_spark_lab_probe.py [html]"""
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
# Instrument: every spark deposit also into SPARK[], every drawn quad's lit width / rest length into GEOM, r_hit.
ours, n1 = re.subn(r'add\(y \* W \+ x, sp\.colour, ([^;]*)\);',
                   r'{ const __v = \1; add(y * W + x, sp.colour, __v); SPARK[y * W + x] += __v * Math.max(...sp.colour); }', ours)
ours, n2 = re.subn(r'(\n\s*)stats\.sparks\+\+;', r'\1GEOM.push([lit, rest, drawn, E]); stats.sparks++;', ours)
ours, n3 = re.subn(r'(\n\s*)record\.hullStats = stats;', r'\1stats.r_hit = rHit; stats.U = U; record.hullStats = stats;', ours)
assert n1 == 1 and n2 == 1 and n3 == 1, (n1, n2, n3)
presets = json.loads(re.search(r'const PRESETS = (\[.*?\]);', html).group(1))
want = ['hull/fighter-ire-side', 'hull/capital-ppc-flank-300m', 'hull/fighter-ire-chase-nose']
probe = (header + helpers + ours).replace('const HEADER', 'var HEADER') + r'''
const W = 480, H = 270; function fmt(v) { return v; } function assets() { return null; } function drawBackground() {} function label() {}
const vals = {c_cell_fighter: HEADER.cell_m[0], c_cell_corvette: HEADER.cell_m[1], c_cell_capital: HEADER.cell_m[2],
  c_rings_fighter: HEADER.rings[0], c_rings_corvette: HEADER.rings[1], c_rings_capital: HEADER.rings[2],
  c_ripple_speed: HEADER.ripple_speed, c_centre_gain: HEADER.centre_gain, c_fade_ms: HEADER.fade_ms, c_stagger: HEADER.stagger,
  c_fill_ratio: HEADER.fill_ratio, c_colour_high: HEADER.colour_high, c_colour_mid: HEADER.colour_mid,
  c_colour_low: HEADER.colour_low, c_colour_full: '#a0c0ff', c_colour_half: '#ffff80', c_colour_empty: '#ff8080',
  c_weapon_tint: 0, c_seta: 1, c_hits: 0, c_tier: 0, c_shield: 100, c_drain: 0, c_frame: 'patch'};
const checks = {};
function $(id) { return {value: vals[id], checked: !!checks[id]}; }
let SPARK, GEOM;
const out = [];
for (const p of __PRESETS__) {
  for (const age of [17, 40, 100, 200, 300]) {
    Object.assign(vals, {c_family: 'hull', c_tier: p.tier, c_hull: p.hull, c_camera: p.camera, c_seta: p.seta, c_hits: p.hps,
                         c_variant: p.variant, c_k: p.k, c_shield: p.shield});
    checks.c_own = p.own; checks.c_cockpit = p.cockpit;
    SPARK = new Float64Array(W * H); GEOM = [];
    const data = new Uint8ClampedArray(W * H * 4);
    const rec = {slot: 'impact', ctx: {setTransform() {}, getImageData() { return {data}; }, putImageData() {}}, weapon: {colour: [50, 255, 50]}};
    renderOurs(rec, age);
    let lit = 0, peak = 0;
    for (let i = 0; i < W * H; i++) if (SPARK[i] > 1e-4) { lit++; peak = Math.max(peak, SPARK[i]); }
    const mean = a => a.length ? a.reduce((s, v) => s + v, 0) / a.length : 0;
    out.push({preset: p.id, age_ms: age, quads: GEOM.length, lit_w_px: mean(GEOM.map(g => g[0])), rest_px: mean(GEOM.map(g => g[1])),
              drawn_px: mean(GEOM.map(g => g[2])), E: mean(GEOM.map(g => g[3])), spark_lit_px: lit, peak_v: peak,
              peak_tm: 1 - Math.exp(-peak), r_hit: rec.hullStats.r_hit, U: rec.hullStats.U});
  }
}
console.log(JSON.stringify(out));
'''.replace('__PRESETS__', json.dumps([p for p in presets if p['id'] in want]))
with tempfile.NamedTemporaryFile('w', suffix='.js', delete=False) as f:
    f.write(probe)
r = subprocess.run(['node', f.name], capture_output=True, text=True)
Path(f.name).unlink()
if r.returncode:
    raise SystemExit(r.stderr[:2000])
print('preset age_ms quads lit_w_px rest_px drawn_px E spark_lit_px peak_v peak_tonemapped U_m r_hit_m')
for row in json.loads(r.stdout):
    print(f"{row['preset']} {row['age_ms']} {row['quads']} {row['lit_w_px']:.3f} {row['rest_px']:.3f} {row['drawn_px']:.2f} "
          f"{row['E']:.3f} {row['spark_lit_px']} {row['peak_v']:.4f} {row['peak_tm']:.4f} {row['U']:.2f} {row['r_hit']:.2f}")
