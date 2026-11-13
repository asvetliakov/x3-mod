# Projectile look: the game's own bolts, re-shaded

Design and implementation note for the bullet re-shade (`weapon_effects = reshade`; the user decision of 2026-11-09:
the game's own bullet draws keep their mesh, placement and timing, only the pixel program changes). Built and flown
first on FL; since 2026-11-13 compiled for AP too: the AP batch scope site `0x0046d0a7 → 0x004bfd40`
([ap-bullet-batch-site.md](../reverse-engineering/ap-bullet-batch-site.md)), node body id `+0x140`, AP's own
standard_lighting DEFAULT pixel pair for the lone classic path; the INSTANCE_BULLETS and lone vertex programs are the
same bytes ([weapon-effects.md](../verification/weapon-effects.md), "Re-shade on AP"; not flown on AP yet). The re-shade's
recognition, program, constants, route and evidence are in
[bullet-draw-batch.md](../reverse-engineering/fl/bullet-draw-batch.md) sections 6 and 7 and the ledger
[weapon-effects.md](../verification/weapon-effects.md) ("Bullet re-shade on FL" onward); this note holds the two later
designs on top of it (the geometric size, the lone path) and the dev tuning file. The custom projectile stage that
preceded it (the IRE line-of-light bolt, bridged streak, comet, pool, object tap, batch skip and draw-hook drop; FL 58 A
to 61 A) was superseded by the re-shade and removed on 2026-11-13; its design is archived in
[projectile-look-custom.md](../archive/effects-look-2026-11-13-custom-projectile/projectile-look-custom.md)
([index](../archive/effects-look-2026-11-13-custom-projectile/index.md), last commit holding the code `04d518bf3`).
Marks: [M] measured, [I] inferred, [S] static (code or note read), [D] user decision, [U] unknown.

## Geometric size (2026-11-10)

The bullet re-shade (`X3M_WEAPON_EFFECTS=reshade`, [bullet-draw-batch.md](../reverse-engineering/fl/bullet-draw-batch.md)
section 6) keeps the game's mesh, so a re-shade cannot change the silhouette. The PPC was "too big" in flight (user,
2026-11-10; PAC and HEPT "more or less fine", IRE accepted), and the texture normaliser cannot shrink a region whose
texels are all saturated. The blob is the head quads' round glow plus bloom. The size control therefore shrinks the
**lit region inside the fixed silhouette**, per body, from the body's UV layout:

- **Generator** (`tools/effects/weapon_effects_table.py` `uv_layout`, `tables.bullet_bodies`; schema stays 2, since an
  older DLL skips an unknown table but rejects schema 3). For each body a row draws, every triangle of every LOD is
  clustered by overlapping UV boxes. The **head** is the cluster whose box is roughly square (width/height 0.67–1.5)
  and whose every triangle maps at least half of the box: the quads carrying the glow sprite. It is recorded as the
  box centre and half-side r. The **fins** are the cluster elongated along u (at least 2:1), recorded as the mean v of
  their axis vertices (radius about z under 0.1 of the largest) and of their rim vertices (over 0.9), plus the u
  range. A group is recorded only when no other triangle of the body samples inside its footprint, so the shader
  needs no other test; otherwise it is `null` with a reason. FL: 30 bodies, 20 heads, 7 fin groups
  ([`bullet_bodies_out.txt`](../../verification/results/projectile-reshade/bullet_bodies_out.txt)). IRE, PAC, HEPT and
  PPC have both groups.
- **Draw.** The batch scope stub publishes the batch's body id `[EDX+0x0c]` (`x3m_bullet_body`). The draw maps it to
  the table entry through the live TBullets rows (built once, at the first lookup that can read TBullets: validated
  reads, one `bullet_reshade_layout` row), so a mod-added body with a row works. c29..c30 travel with c31 in one
  three-register upload each way, so the call count is unchanged. A body without an entry, and every body with no
  table, gets the identity (one row per body or for the missing table) and keeps the accepted look.
- **Shader** (`src/effects/weapon_bullet_reshade.hlsl`, ps_2_0, 315 words). Inside the head's square footprint
  (1.02 r), radiance × smoothstep(1, 1 − s, |uv − centre| / (r · `rs_head`)). Inside the fin band (d = (v − axis) /
  (rim − axis) in −0.25..1.02, any u), × smoothstep(1, 1 − s, |d| / `rs_width`). Elsewhere × 1. s = 0.5. A key at 1
  uploads 0 for its reciprocal, so the factor is exactly 1.
- **Keys** `rs_head`, `rs_width` (dev tuning file, 0.2–1, default 1). They are global across bodies (one value set,
  the user's decision), so a value tuned for the PPC also shrinks the PAC (−13 % energy at the proposal) and, slightly,
  IRE and HEPT (−1 to −2 %).

Evidence [M]. D3D fixture: the falloff's readback against the CPU model at rs_head 0.5 / rs_width 0.6 at 11 UV points,
worst 0.031 of the tolerance. At keys 1 the output is identical to the accepted record's sums (IRE 76.026, PPC 2593.162) and to the identity
layout's at every point. The body change costs 0.003 µs (scan of 30 ids plus `layout_constants`) and the substitution
0.125 µs per draw. Dumps at 50 m
([`dump_out.txt`](../../verification/results/projectile-reshade/dump_out.txt), dark sum):

| Body | vanilla | re-shade, keys 1 | re-shade, rs_head 0.5 / rs_width 0.6 (proposal, not baked) |
| --- | --- | --- | --- |
| IRE | 253.713 | 76.026 (the accepted record, unchanged) | 75.086 |
| PAC | 100.824 | 25.512 (unchanged) | 22.093 |
| HEPT | 522.565 | 198.906 (unchanged) | 195.870 |
| PPC | 4611.169 (peak 1.0) | 2593.162 (unchanged) | **1699.028** (peak 0.965) |

At the proposal the PPC's fins narrow cleanly, but its head reads as a row of small beads
([defaults](../../verification/results/projectile-reshade/fixture-ppc-reshade-near.png),
[proposal](../../verification/results/projectile-reshade/fixture-ppc-reshade-near-h050-w060.png)). The head is 27
crossed quads at several depths along the axis (z 0.086–0.188 body units), and each quad's glow shrinks on its own.
Larger `rs_head` values keep them merged [I]. The user tunes in flight. Not flown.

## The lone path (2026-11-12)

FL 67 A showed bolts dimmed or half-transparent over a distant station that the re-shade never drew: the batch
submits ran (600 per 300 frames) with `draws=0`, the additive route admitted nothing, and the white meshes in those
frames carried no far flag ([ledger](../verification/weapon-effects.md), "FL 67 A"). They are bullet bodies the batch
does not take (fewer than two bolts of a body in the layer, over budget or capacity; the Titan's PPC bursts mostly),
drawn one at a time through the material submit with the body's `effects.fx` material and the effects DEFAULT pair,
where the source-only gain's clamp variant drew them white ([bullet-draw-batch.md](../reverse-engineering/fl/bullet-draw-batch.md)
section 7). Without the far flag the TAA treats them as static geometry and its far hold and clip dim them.

The rule is the batch's, by identity instead of by program:

- **Recognition.** The effects DEFAULT or INSTANCE VS bound (the ABI gate: the two share oT0.xy = UV and oD0.x = the
  fade), ADD ONE / INVSRCCOLOR or ONE / ONE, and the object-trace scope node's body id (`+0x144`, AP `+0x140`) in the body map the
  geometric size already builds from the live TBullets rows. Every other effects draw (engine glows, gate, flares)
  ends at the blend or at the 30-id scan.
- **Substitution.** A lone variant of the same program (fade from COLOR0.x; the game's colour matrix is not applied,
  as the batch pass has none), the body's c29..c31, the one sequence of `bullet_reshade_route.h`. ONE / INVSRCCOLOR
  goes through the additive route exactly as a batch draw: DESTBLEND ONE with the emission gain, the alpha law, and
  after the scene's first depth writer the flag variant writing RT2.g, so the far composite sees the bolt. Off or
  refused route, and ONE / ONE: plain under the game's blend. The re-shaded draw skips the source gain and the hull
  route. No footprint (the indexed static buffers carry no scan) and no single-copy drop (the batch's copy rule).
- **State (FL 68 A, 2026-11-12).** The route refused every lone draw it was offered for `state` (`lone_flagged=0` in
  all 151 lone windows of the flight), so the bolts were re-shaded plain and stayed unflagged. The sequence now writes,
  where they differ, only what the draw's path needs: on the route the four states the route requires (Z write off,
  colour mask 15, sRGB write off, separate alpha off), off it Z write only (a bolt writes no depth and must not enter
  the TAA depth lane); alpha test and Z test stay the game's (the programs output the texel's alpha, so the game's test
  cuts as in vanilla), and `end` puts the written states back. The route admits a lone draw on those four
  (`route_state_refuses` in `bullet_reshade_route.h`, the same verdict for the fixture); every other draw still needs the
  exact native screen state.
- **Far flag outside the route (2026-11-12).** A lone draw the route never takes (ONE / ONE: never offered) is
  flagged under the game's own blend: the route's flag targets through the shared `flag_bind` / `flag_unbind` of
  `bullet_reshade_route.h` (RT1 masked off, RT2 GREEN, the masks saved and put back after the sequence's end), the lone
  flag program, no conversion, gain 1, Z write the only state written (the MRT flag write needs no other: RT2.g is
  written under the GREEN mask the binding sets). Over the far-depth lane (RT2.g = z/w, the composite flags g > 1) ONE /
  ONE gives base + s, as the route's DESTBLEND ONE does. A ONE / INVSRCCOLOR lone draw the route refuses or is not
  requested for stays unflagged: D3D9 blends every simultaneous render target with the one SRCBLEND / DESTBLEND, so the
  lane would become s + base (1 - s), which is 1 at far depth (measured 1.0000 at base 1, s 6.45) and never passes
  g > 1.
- **Counts.** `bullet_reshade_frame` gains `lone=`, `lone_flagged=`, `lone_failures=` and `lone_us_per_draw=`, in
  none of the batch's fields; the additive route's admitted / refused / window counts include the lone draws offered.
  Since FL 68 A also `lone_flagged_native=` (of `lone_flagged=`, those flagged outside the route), `lone_refused=`
  and `lone_refused_why=` (unflagged lone draws by reason: off, scene, no_depth, no_variant, no_target, r32f_lane,
  write_masks, bind, screen_unrouted, failed), `lone_route_state=` (refused by the route for a known differing state, a
  defect) and `lone_route_unknown=`, `lone_zwrite=` .. `lone_sepalpha=` (lone draws drawn re-shaded whose state was
  normalised, per state) and `lone_additive=` (lone draws under ONE / ONE); the route's window row breaks its `state`
  refusals down (`zwrite= mask= srgb= sepalpha= blend= alphatest= unknown= lone_state=`); one
  `bullet_reshade_lone_body` row per (body, blend), at most 16 a device, names the lone bodies.
- **Body map.** Built only while the live TBullets row count equals the table's, one attempt per frame, off after 64;
  the lone path stays off until it is built.
- **Classic bodies (2026-11-13).** A classic-material bullet body drawn alone (PAL / Kyon / Tug beams, Mass Driver,
  Ion Disruptor, flak, PSG, mining; the `standard_lighting` DEFAULT pair, ONE / ONE) is re-shaded by the same law with a
  ps_3_0 twin of the lone program (fade from COLOR0.a) through the same sequence and far flag, so it matches its batched
  draw ([bullet-draw-batch.md](../reverse-engineering/fl/bullet-draw-batch.md) section 7, "Classic bodies").

Evidence [M] (native fixture; ledger "FL 67 A"): recognition by body id 9 of 9 rows; the lone program equals the model
with the fade in COLOR0.x (worst 0.018 of the tolerance); the route's path converts, flags (RT2.g 12.906 against
12.908) and restores. Cost: recognition 0.013 µs per candidate draw for a hit and 0.015 µs for a miss (node read and a
30-id scan), the sequence 0.348 µs per lone draw (0.195 for the batch's non-indexed draw). The PPC drawn through the
lone state at 50 m equals the batch frame pixel for pixel, vanilla and re-shaded (dark sum 2593.162 both, max
difference 0; [lone](../../verification/results/projectile-reshade/fixture-ppc-lone-reshade-near.png),
[batch](../../verification/results/projectile-reshade/fixture-ppc-reshade-near.png)). The vanilla lone frame uses the
identity colour matrix and no source gain, so it shows the pair's own law, not FL 67's white clamp. FL 68 A flew the
recognition (the PPC bolts dimmed like the other weapons) but not the flag; the state fix is in the ledger's "FL 68 A"
paragraph, not flown.

## Dev tuning file [D]

`<game>\x3m\projectile_tune.json` (FL, dev builds only), the flat JSON object of the re-shades' thirteen keys; the baked
values and bounds are `src/proxy/weapon_projectile_family.h`:

| Key | Baked | Bounds | Meaning |
| --- | --- | --- | --- |
| `rs_gain` | 0.75 | 0–2 | the bullet body's gain over the game's radiance (c31.x) |
| `rs_edge` | 0.4 | 0.05–1 | the luminance smoothstep's upper edge (the fringe fades below it) |
| `rs_core` | 0.5 | 0–4 | the hot core's gain (its l^4 term) |
| `mz_size` | 0.2 | 0.05–1 | the muzzle glow's radius over vanilla's lit extent ([muzzle-flash-look.md](muzzle-flash-look.md) section 3) |
| `mz_core` | 0 | 0–4 | the muzzle's white-shifted core at the gun |
| `mz_gain` | 0.15 | 0–2 | the muzzle glow's luminance at the gun |
| `mz_smooth` | 0.1 | 0–1 | the share by which the flash texel's grain is flattened |
| `rs_head` | 1 | 0.2–1 | the head glow's lit radius over the body's (the geometric size; 1 unchanged) |
| `rs_width` | 1 | 0.2–1 | the fins' lit half-width over the body's (1 unchanged) |
| `hh_gain` | 1 | 0–2 | the hull hit re-shade's body gain over the game's radiance ([hull-hit-look.md](hull-hit-look.md) section 5; not flown) |
| `hh_edge` | 0.4 | 0.05–1 | its luminance smoothstep's upper edge |
| `hh_core` | 1 | 0–4 | its hot core's gain (l^4) |
| `hh_cool` | 0.3 | 0–1 | how far a fading impact element reddens by the end of its life (0: fade only) |

The proxy checks the file's last-write time at most once a second on the Present thread (`GetFileAttributesExW`),
reads a changed file into a fixed 1 KB buffer and parses it once (no allocation; a missing key takes the baked value,
so the file states every deviation), clamps every value to its bounds (one `projectile_tune clamped key=…` row per
clamped key) and logs one `projectile_tune reloaded source=file keys=… values=…` row per reload (a key the file did
not set is starred). An unknown key (any value kind) is ignored and named once a session in a
`projectile_tune unknown key=<name> (ignored)` row; the removed custom stage's fourteen keys (`core_px`, `glow_ratio`,
`glow_gain`, `hot_share`, `lod_dot_m`, `length_cap_m`, `stops`, `trail_ms`, `head_gain`, `flash_core_mult`,
`flash_len_m`, `flash_life_ms`, `flash_stops`, `hull_pulse_r`) are unknown keys since 2026-11-13, so a file still
carrying them applies its other keys. A malformed file logs one `projectile_tune refused` row and keeps the last
values; a removed file returns to the baked values with one `source=defaults` row; a file that never existed logs
nothing. The bullet constants and every muzzle body's are refreshed on a change, once per frame while either re-shade
runs (`MotionOutput::reshade_poll_tune`). **It never ships:** the reader (`weapon_projectile_tune.cpp`) compiles only
under the CMake option `X3M_DEV_TUNING` (default ON for a RelWithDebInfo build, OFF otherwise;
`tools/release/release.py` configures OFF), so a release DLL has the baked values and no file code or path.
**Protocol:** the user edits the file, flies, reports the numbers and a screenshot; accepted values are baked into the
header and the user deletes the file. Host test: `verification/analysis/test_weapon_effects_projectile_tune.py`.

## Reproduce

```sh
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_weapon_effects.py --host native \
  --dump verification/results/projectile-reshade --dump-case reshade --lab-assets verification/results/weapon-effects-lab/fl
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_weapon_effects.py --host native
PYTHONPATH=verification/probe python3 -m unittest discover -s verification/analysis -p 'test_weapon_effects_projectile_tune.py'
```
