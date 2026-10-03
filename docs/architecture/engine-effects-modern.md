# Modern engine plumes and trails: replacing the game's engine effects from the proxy

2026-10-01, design only (no source edit, no launch, no Wine, no build). Decision for the main session to ratify.
Inputs: [engine-effects.md](../reverse-engineering/engine-effects.md) (the drive, the draw, the four native parts),
the data census under `verification/results/engine-effects-census/` (plus
[`particles3_materials.py`](../../verification/results/engine-effects-census/particles3_materials.py) →
`particles3_materials_out.txt`, written for this note), the dropped effects stage
([effects-modernisation-opus.md](effects-modernisation-opus.md), commit `9375a1e7`, removed in `479cd036`), the dust
motes ([fog-dust-motes.md](fog-dust-motes.md)) and the lens-flare gain / engine cull
([sun-partial-occlusion.md](sun-partial-occlusion.md), `src/proxy/lens_flare_cull*.h`). Tags: **[m]** measured in a
cited note, census or source read this session; **[i]** inferred; **[u]** not established.

## Decision

**Go, in four phases, with phase 1 (suppression and census) as the gate.** The proxy recognises every glow-jet
draw by the node flag the object scope already reads (`flags130 & 0x4000001`), takes a 64-byte record (origin,
axis, base size, the engine's own z-scale = throttle, tint from an offline body table) and returns `S_OK` without
forwarding the draw. The emitter sprite, its lens flare and the Particles3 trail are never created: two 5-byte call
redirects inside the effect routine `0x00414590` (sites `0x004147eb` and `0x0041482c`, measured in
engine-effects.md §7) skip the effect-instance and trail-generator calls for objects of class 7 (ships) and forward
every other class — missiles pass the same two sites — unchanged; a draw-level cut is impossible because ship and
weapon trails share the particle material. The redirects are armed at install only, never toggled in flight.
Missiles (class 10) reach the same JET child walk, so under `off` their engine glow jets are suppressed too (the
draw-path rule has no class gate), while missile emitter sprites and trails stay native until a later phase (both
redirects forward class 10); in phase 2 missiles get plumes like ships.
Plumes and ribbons are drawn by a proxy stage in the TAA resolve's step-0 bracket (the dropped stage's
slot, own-pass price 0.008–0.016 ms at 5120x1440 [m]), as **two draws for all ships**: one dynamic VB of axial
plume quads + nozzle discs, one dynamic VB of ribbon strips, ONE/ONE on the FP16 target, occluded per pixel and
softly by the RT2 depth lane. Nothing is ever drawn both natively and by the stage; a frame the stage does not
reach shows no engine effect rather than the game's.

Hot-path cost [i unless marked]: per glow draw −7.5 µs of Wine frontend time saved [m, run95] against ≈ 0.5 µs
of scope read and record; the stage 2 draws + ≈ 12 state calls ≈ 5–10 µs CPU; GPU 0.05–0.08 ms at 30 ships and
0.08–0.12 ms at 100 at 5120x1440; ribbon CPU ≤ 0.3 ms at 100 ships (bounded by the instance caps of section 3).

## 1. Suppression (requirement 1)

| Native part | How the proxy recognises it, no capture needed | Mechanism | Both trees |
| --- | --- | --- | --- |
| **Glow jet** (scene node, `engine.fx`/`effects.fx` pair `d5e1c753…`/`8360f422…`, Z-write off, blend ONE/INVSRCCOLOR or ONE/ONE) | The draw runs inside the object scope with the jet node at `ESP+8` [m]; `object_trace::Snapshot.flags130` is `node+0x130`, set to `0x4000001` for every JET-list body (`0x00434708`) [m]; `model` is the body id; `scale[0..3]` = `+0x70, +0x80, +0x84, +0x88` [m, `object_trace.cpp:300-307`]. Rule: effect pair bound **or** any pair with blending on and Z-write off, **and** `flags130 & 0x4000001 == 0x4000001`. An opaque JET draw (Z-write on: the 9 `standard_lighting` / 4 `terran.fx` nozzle bodies) stays native: it is geometry | `route.submit = false`, `submission_error = D3D_OK` (the lens-gain skip contract); the engine never tests the result (`0x004c403c`) [m] | Mayhem 2,858 glow parts in 499 ships; stock 432 in 259 ships [m]. The flag, not the texture, so the stock `-79` animated bodies and the 99 legacy stock materials are covered the same way. **Missiles** (class 10) reach the same JET child walk: their glow jets are suppressed under `off` too, their sprites and trails stay native until a later phase, and phase 2 gives them plumes like ships |
| **Emitter sprite** `B_GLOW` (`objects/v/00011`/`00213`, legacy MATERIAL3, texture 369/368) | Not recognised at the draw (its pair is untraced [u]); it is never created | Call redirect A at **`0x004147eb`** (`E8 rel32` → `0x004148a0`, `__cdecl`, 10 dwords: k `[esp+8]`, eff `[esp+0xc]`, obj `[esp+0x10]`, `&pos` `[esp+0x20]`; result unused; the five bytes lie in one aligned qword, so `claim_call` writes them with `cmpxchg8b`) [m, engine-effects.md §7]. **Missiles (class 10) reach the same site**: the stub skips only when `*(int16_t*)(obj+0x48) == 7` (`xor eax,eax; ret`) and otherwise `jmp`s to the original with stack and registers untouched; EBX/EBP live across, EAX/ECX/EDX/EFLAGS dead after [m]. "Not created" is a state the game already runs: every `0x7001` main part carries the `0x4000` bit, and no reader of the instance list assumes presence [m] | Mayhem rows 700–729 (sprite + lens flare), reached only by ships with glow parts; stock: rows 5, 12 and 210–236 are the only ones reached, and ships **without** glow parts reach only row 5 (a speed-dependent `EEDF_LIGHT`, no sprite, no flare) [m, `phase0_data_out.txt`]. **No `eff` allowlist is needed** |
| **Engine lens flare** (`EEDF_LENSFLARE` element, bodies 753/754/778/781/`v\01016`, drawn in the engine's lens scene: 113–169 draws per frame on Mayhem 3, 0–10 on stock [m, run364/run375]) | Element of the same effect instance | Redirect A removes it with the sprite. The existing lens-flare gain (default 0.3 [m, launcher]) and the engine cull at gain 0 stay for the sun; they cannot separate engine halos from the sun's rays by body (753/754/778 are in both sets [m]) | Both |
| **Particles3 trail** (particle renderer `0x004bf4c0`, one `DrawPrimitive` per material batch [i]) | Not separable at the draw: **on Mayhem 3 every Particles3 row but IPG (407) uses material 406**, ship rows 1–41 and 58–60 together with MAML/PSP/FBL/… weapon rows 42–57 [m, `particles3_materials_out.txt`]; stock ship row 1 uses 1198 alone, row 14 (6 ships) shares 395/406 with weapons [m] | Call redirect B at **`0x0041482c`** (`E8 rel32` → `0x00412d70`, **`__stdcall`, 4 dwords, the stub must `ret 0x10`**: k, trail, obj `[esp+0xc]`, `&pos`; result unused) [m, §7]. The gate is `VideoD3DFlags` bit 30 (`+0xfc & 0x40000000`: default and registry only, bottle value `0x523bad5e` = trails on, no dialog item) [m]. Its rel32 straddles a qword boundary, so the write is `engine_patch`'s plain copy, safe only in the install window before the first Present (`0x00414590` runs only in the frame loop [i]). The same class test as A: ships skipped, **missiles forwarded** to the original. A skipped call is the `C & 0x2000` state: no link, no generator, nothing in the pool [m]. Weapon/laser trails from the other spawn sites (`0x004146fa` class 0, `0x004151c3` effect elements) are untouched | Both |

What breaks if a class is left native: glow native + plume = a double plume at the nozzle (the native mesh is
2 × value long at full speed, the same length law) and the 7.5 µs per draw are not saved; sprite native = a grey
disc at the nozzle under the plume and its red streaked halo (Mayhem) at 0.3 gain; trail native = Mayhem's
40–120 unit grey puffs beside the ribbon (lifetime 1.5 s, the heaviest of the four on screen). The three must go
together; the ribbon is armed only when redirect B is installed (fail closed), the plume only when redirect A is.
**The redirects are decided at install/load only, never toggled per frame:** an instance or generator created
before a stub arms lives on, frozen at its last refresh, until its ship is removed [i, §7 "Arm once"]. The
option (`engine_effects = native | off | plumes`) is therefore a load-time setting; there is no native/plumes
comparison hotkey (user decision), only strength presets for the plume look (section 6).

**RCS jets** (`v/00566`, 2,583 parts, mode words without bit 0, +1.0 z per steering axis [m]) are on **both** the
JET and the SMALLJET list, so they carry the full `0x4000001` flag plus `+0x1d8 = 5` (the small-object cull drops
them below measure 5) [m, §7]; the recogniser matches them, and phase 2 draws them as short puffs from the same
record path (their z runs 0.01 → 1.0 on steering), so the user sees steering feedback; below measure 5 the puff
vanishes with the native jet.

## 2. Record extraction (requirement 2)

Per recognised draw, from the Snapshot (no device call) and the constant shadow:

| Field | Source | Note |
| --- | --- | --- |
| Nozzle origin, axis, base size | The world rows c4–6 of the draw, captured by extending the `SetVertexShaderConstantF` shadow (`set_vertex_constants_f` already copies clip-row windows [m, `motion_output.cpp:4977`]) with a 3-row window while the effect pair is bound: origin = translation, axis = −(model-z row), base size `value` = `|x row|` (`0x004bdee0`: rows = basis × `+0x70·scale/65536` × context scale [m]) | Zero reads per draw; the c4–6 register order (**[u]**) is resolved by the Snapshot cross-check below and pinned by the first F8 |
| Throttle `s` | `z = scale[3]/65536` from the Snapshot (`+0x88`), `s = clamp((z − 0.25)/1.75)` for a main jet (`C = 0x7001`, 99.4 % of installed parts) [m: the drive at `0x0045ad8b..0x0045b03c`]; the same number is the z/x row-length ratio of c4–6, which also identifies the z row when `z ≠ 1` | Rate-limited by the engine (0.004/ms, 437 ms for 0.25 → 2.0) [m]; steering/brake bits push `z` above 2.0 on flagged jets (up to 9.0): `s` saturates, the extra length is kept as a brake flare |
| Tint, size class, extent | `model` (body id) → body name through the engine body table (the `lens_flare_cull` resolver: names → ids at begin_frame, 210 µs restart over 13,200 slots, 0.134 µs per frame for 5 mappings [m]) → the shipped `engine_bodies.json` of `tools/effects/engine_bodies.py` (merged `c7f6c05a`): keyed by the `types/Bodies` spelling, `lists` an array (`v\00566` on both), per body the LOD-0 value, z extent (negative / both), x/y half-width, material/blend, alpha-weighted mean and peak colour; Mayhem 253 bodies / 16 missing, stock 224 / 23 [m] | Mayhem: 80 `engine.fx` bodies, 11 colours (darkblue #5858f7 … lime #19a319), values 504 … 211,762 [m]. **Every size law uses the body's own `value`, never a tier label**: stock ships 98 `fx_engine_xtc_*` bodies at twice the Mayhem values under the same tier names [m, c7f6c05a]. Stock: 110 of 138 bodies share the animated blue family (#4ba0bf cyan), 28 white/grey [m]: the table gives cyan/white, which **is** the stock record (TShips carries no colour; col 11 only selects the sprite/flare row). A per-race override on stock needs the ship, which the jet node does not reach (phase 4). Not covered, forwarded natively: three Mayhem bodies under `effects\engines` that are on no list (`fx_engine_paranid_m6_axeface`, `fx_engine_xtraotas_ts1/ts3`: no flag, no entry) and `v/00114`, a MATERIAL3 text body the loader refuses (flagged, no table entry → `unknown_body`) [m] |
| Identity | `object_lifetime` node serial (handle + lifetime epoch), else the spatial identity (key + origin quantised to 8 units) as the dropped stage did | Ring buffers and fades hang on it |
| Fallback | A JET-flagged draw with no scope (depth 0), a failed node read, or no live redirects (`engine_effects_patch::installed()` false, cached per frame) → forwarded natively, counted (`forwarded_unscoped`, `forwarded_snapshot`, `forwarded_patch_missing`): the glow never disappears while native sprites and trails stay. Under `plumes`, a stage that is not attached on the device (refused at attach or creation, failed until Reset, or within its 64-frame disarm) forwards the glow too, from the next frame on (`forwarded_stage_off`, after the review of flight C); the sprite and trail redirects stay (load-time). A flagged draw whose body is not in the table **is** suppressed, with a record of the default tint cluster, counted `unknown_body` (phase 1 relaxation: `v/00114` and any mod body) | Phase 3 may add the removed texture-key registry as a second key if the census shows unscoped glow draws |

A suppressed draw never reaches the boundary selector: `before_draw` returns before `route.evaluated` is set and
`after_draw` returns at `!route.evaluated` ahead of `observe` [m, source]. On a capture frame its `motion_input` row
is still written (`record_draw_input` is not gated on `route.submit`; the row shows result 0 for a draw the device
never saw) [m, source]. Flight A's capture must confirm that the scene-boundary selection (bloom copy, scene phase)
is unaffected with `off`.

**Stock ships without glow parts** produce no glow draw and therefore no record. Measured [m, §7]: the stock rows
whose scenes have emitter dummies but no glow parts reach site A only through row 5, an `EEDF_LIGHT` element (a
hull light, no sprite, no flare); rows 3, 9–11, 13, 14 (71 TShips rows) are never reached because their scenes
have no emitter dummies, so their engine look is in the hull materials and is unaffected. Redirect A therefore
costs those ships only the engine light, and no allowlist exists. Phase 4 option: the stub records `(obj, k, pos)`
before skipping — an anchor per emitter; `pos` is the caller's 16-byte local in the **ship model frame, scene
units** (the part key × the root's per-axis scale, 4th dword uninitialised), and `obj+0x10` (speed) and
`TShips[obj+0x4a]+0x44` (`vmax`) are safe reads at that point [m, §7] — which gives plumes for those ships and a
per-race tint on stock; the proxy transforms `pos` with the root's draw-time matrix itself.

## 3. Rendering (requirements 2.1, 3, 5)

**Geometry, one stage, two draws.**

- *Plume* = per nozzle an **axial billboard quad** (the quad contains the axis and faces the camera) of length
  `L = z · value` (the engine's own law: 0.25 × value idle, 2.0 × value at `vmax`) and width `value`, plus a
  **camera-facing nozzle disc** of radius `0.6 · value` whose weight is `|axis · view|` (it carries the look when
  the axis points at or away from the camera, where the axial quad degenerates). Both are vertices of the same
  VB (8 per nozzle, 40 B each; a per-vertex kind selects the profile), one `DrawIndexedPrimitive`.
- *Ribbon* = per nozzle a camera-facing strip through a ring buffer of nozzle world positions (16 samples; a sample
  is pushed when the nozzle moved ≥ 0.5 · value since the last one, so SETA just advances the samples), width
  `0.5 · value` tapering to 0, one VB (2 vertices per sample, 32 B), one `DrawIndexedPrimitive`.
- Impostor versus cone mesh versus sprite strip: a mesh cone needs per-instance transforms (instancing is not used
  by the route; VTF neither) and gives hard silhouettes; a sprite strip (the native trail) needs many quads per
  plume. The analytic quad pair is 2 triangles + 2 triangles per nozzle with the whole look in the pixel program.

**Radiance (FP16, engine space, ONE/ONE, alpha never written):**

| Term | Law | Numbers ("restrained") |
| --- | --- | --- |
| Core | `I_core(s) · tint_core · G(r/ (0.12 · value)) · exp(−3 t)`, `t` = distance along the axis / L, `tint_core = lerp(tint, white, 0.6)` | `I_core` 1.5 at idle → **4.0** at full speed (above the stabiliser's emitter bound E 1 [m], so it survives the resolve at 1.00 [m, motes/bolts rows]) |
| Halo | `I_halo(s) · tint · G(r / (0.5 · value)) · exp(−2 t)` | 0.3 → **0.8** (below E: shows at 0.37–0.66 through TAA [m], dim by design) |
| Disc | `I_core · tint_core · G(r / (0.3 · value)) · |axis·view|` | the head-on and tail-on look |
| Flicker | `1 + 0.1 · noise(id, 10 Hz)` on the core only | ≤ 10 %, ≤ 12 Hz: the resolve averages it |
| Ribbon | `0.6 · tint · (1 − u)^2 · w(u)` along the strip (`u` 0 at the nozzle), width `0.5 · value · (1 − u)` | length `T(value) · v · s`, `T` by the body's own value, not its name: 0.5 s below 1,500 (fighters on Mayhem), 0.8 s to 8,000 (TS/TP/TM/M6/M8), 1.2 s above (capitals); on stock the same thresholds land one class lower because its `xtc_*` values are doubled [m], which the flight rates; capped at 60 · value; full speed = full length (requirement 2.1) |
| Minimum pixel size | the quad's screen width is clamped ≥ 3 px and the core's length ≥ 6 px (the 3x3-clip survival rule of the motes [m]); a nozzle under 1.5 px is not drawn | bounds the fill and the flicker of distant ships |
| Chase view | the own ship's plume is capped at 0.12 H wide on screen and faded to 0.5 within `2 · value` of the camera; its ribbon starts `3 · value` behind the camera | the plume stays the main visual without filling the screen |
| Size class | everything scales with `value`; no extra per-class gain (M1 `value` 9,366–211,762 against M3 504–4,003 [m]) | engine size = plume size, as the data already encodes |

**Batching and bounds.** Records per frame ≤ 1,024 (overflow is forwarded natively and counted; Mayhem per-ship
glow parts: median M3 3, M6 4, TS 4, M7 6, M1 6, M2 8, max 66 [m]; 30 ships ≈ 100–150 records, 100 ships ≈
300–500 [i]). Ribbons ≤ 256 per frame, the nearest nozzles by projected size; the rest draw the plume only. VBs:
plumes 1,024 × 8 × 40 B = 320 KB, ribbons 256 × 32 × 32 B = 256 KB, one DISCARD lock each per frame, DEFAULT pool,
released at Reset and recreated at the next latch (the motes' rule). Programs: plume vs_3_0 ≈ 60 slots, ps ≈
100–140 (lane fetch, two Gaussians, one noise fetch), ribbon vs ≈ 50, ps ≈ 40 [i]; created at the latch, not at
first use.

**Cost at 5120x1440** [i, anchors: ≈ 1 µs per ps slot per full frame = 0.135 ns/px/slot, 0.035–0.04 ms fixed per
full-screen draw, pass price 0.008–0.016 ms [m]]: 30 ships ≈ 0.4 Mpx of plume + ribbon × 120 slots ≈ 6 µs, the
own-ship chase plume ≈ 5 % of the screen ≈ 6 µs, two small draws + pass ≈ 0.04–0.06 ms → **0.05–0.08 ms**; 100
ships (mostly distant, clamped to 3 px) **0.08–0.12 ms**. CPU: recogniser ≈ 0.5 µs per glow draw (one
`object_trace::current(matrices=false)` where the route did not already read it, four validated reads), ribbon
update ≈ 0.3 µs per nozzle, VB fill ≈ 20 ns per vertex → ≈ 0.05 ms at 30 ships, ≤ 0.3 ms at 100, against 0.75–1.1
ms (30) / 2.2–3.7 ms (100) of frontend time saved by the suppressed draws. The engine's own ≈ 24 µs per issued draw
[m, run384/385 flares] is **not** recovered by proxy suppression (option A in the last section recovers it).

## 4. Occlusion (requirement 4)

The stage runs after RT1/RT2 are unbound, so the plume samples the completed lane: `.b` = view depth where the sun
lane is on (the user's launch), the `R32F` z/w fallback otherwise [m]. Per fragment
`vis = saturate((d_lane − z_frag) / (SOFT · value))` with the sentinel meaning "no occluder": **core SOFT 0.15, halo
SOFT 1.0**. That is per-pixel, not per-nozzle: a nozzle behind the hull at a slight angle hides only the part of
the plume inside the hull's silhouette; the 2 × value plume and the ribbon reach past the silhouette and are
visible there, which is the requirement's case. Seen exactly from the front the core is hidden, the halo bleeds a
soft rim past the silhouette (its SOFT is a full `value`), and the ribbon shows beyond it; nothing draws through
the hull. A `glow_through` tunable (0–0.3 of the halo drawn regardless of the lane; default 0) is the
knob if the user wants the game-like shine-through. Two-pass alternatives (a per-nozzle visibility probe like the
engine's flare test, or a depth pre-pass of the plumes) buy nothing over the lane and were not taken.

Lane precision: the `R32F` fallback stores z/w = m22 + m32 / z (m22 ≈ 1, m32 ≈ −6), so one float step near 1
(≈ 6e-8) is a view-depth step of about z² · 6e-8 / 6: ≈ 0.04 units at 2 km, ≈ 4 units at 20 km [i]. Against the
halo's SOFT of one `value` that is harmless for ship-sized values, but a small value far away gets a coarse cut;
the four-channel lane (`.b` = view depth, the sun lane's form, the user's launch) has no such loss and is the
preferred source. Jitter: the plumes are rasterised under the scene's jitter (the routed draws' projection, as the
sun pass does), so they land where the lane and the scene's own pixels are; section 5's "unrouted sentinel pixels"
describes their history handling in the resolve, not an unjittered raster (a wording deviation, built this way).

Interactions: the ship's own LOD draws write the lane whatever LOD they are at (the lane covers the conventional
opaque union [m, material-coverage]); the suppressed jets write nothing. Unrouted near-pass opaque draws do not
occlude (the motes' accepted gap). Fog: the stage runs after fog, so a plume is not fogged; phase 3 applies the
fog pass's own distance law analytically per nozzle (`transmittance(d)` from the accepted 13 → 22.5 km fade, one
constant row) so distant plumes in a fogged sector dim with their hulls [i]; exact coupling to the march is not
planned.

## 5. Temporal behaviour and lifetime (requirement 5)

- **Through the resolve:** stage pixels are unrouted sentinel pixels: far-plane camera history, 3x3 clip, sentinel
  stabiliser S 0.7 / E 1. A core above E keeps 1.00 of its radiance (bolts at 0/4/8 px per frame, motes at M 2
  [m]); the halo below E shows at 0.37–0.66 [m] and is sized for that. No motion is written (RT1 is unbound); the
  own ship is static on screen in chase view, an NPC plume moves with its hull and overlaps its previous position
  (the streak argument: a plume is ≥ 6 px long along its motion). Flicker is bounded by the 10 % / 10 Hz noise and
  the 3 px minimum width.
- **Ghosting over starfields:** the measured worst case is the motes' synthetic flickering sky (34 % of M, decaying
  at 0.97 [m]); the user accepts a little ghosting; the flight checks real starfields (F8 4). Escalation, only on
  evidence: a reactive mark in the resolve for stage coverage (phase 3 of the dropped design, not built).
- **The engine's ramp versus the proxy's history:** `z` is the engine's, rate-limited in game time and clamped to
  1 s per step [m]; under SETA it reaches its target in fewer frames and the proxy does nothing. Ribbons are
  distance-sampled (0.5 · value per sample), so SETA stretches nothing: samples just advance faster; the length cap
  `T(value) · v · s` is evaluated from the positions themselves (speed = displacement per frame from the records),
  and a strip whose first segment exceeds 8 · value in one frame (a jump, a cut, a load) is cleared.
  Under sustained SETA the ribbon's length in world units grows with `v_est` (measured over wall-clock time, so the
  game's faster motion reads as a higher speed); the sampling does not change (still one sample per spacing moved).
  In pause the ribbon shrinks to the head within a second (`v_est` falls as the newest sample ages with no motion) and
  returns one frame after resume.
- **Cuts, docking, death, eviction:** a record is a per-frame fact; a plume is drawn only for a record this frame.
  The ring buffer is keyed by node serial in an open-addressed map (512 entries, O(visible nozzles)); an entry not
  seen this frame fades its ribbon over 0.3 s at its last positions, then is evicted; the resolve's cut verdict
  (`cut_finished_ && counters_.cut`), a Reset, a sector change or load epoch (serial epoch) clears the map. A
  docked ship stops being drawn, so its records stop; a destroyed ship's jets spawn no debris [m] and its node
  dies (serial retired by `object_lifetime`).

## 6. Risks, verdict, fixtures, phases

**Verdict: go.** The three biggest risks:

1. **The two call redirects in `0x00414590`**: bytes, ABI and liveness are measured (§7), the residual risks are
   the B site's plain (non-atomic) write, made at the first d3d9 export call (`load_backend` under InitOnce, inside
   `initialize_log`), before any device exists and so before any frame, while `0x00414590` runs only in the frame
   loop [i]; a claim after the first Present is refused (`late_claim`); a
   missile reaching a stub (class 10 must be forwarded byte-for-byte: `jmp` to the original, nothing touched),
   and an indirect reader of the two lists through a copied pointer (none found, not excluded [i]). Mitigation:
   the hook fixture drives both stubs with class 7 and class 10 objects under hostile LastError/MXCSR/x87 (the
   sun-occlusion fixture's shape), checks `ret 0x10` at B and the stack/register identity of the forward path,
   and the install-window claim is the existing `engine_patch` rule. Fallback if a site refuses: the data route
   (option D below).
2. **TAA on thin features**: distant plumes and ribbons under 3 px, ghost trails over real starfields; only a flight
   measures them. Mitigation: the pixel clamps, cores above E, the ribbon overlapping itself; the resolve fixture
   runs the plume over the dark and the flickering sky at 0/4/8 px per frame.
3. **Taste**: the previous effects stage was dropped after one flight. Mitigation: phase 1 stands alone as a clean
   "no engine effects" state the user judges immediately; phases 2–3 ship three strength presets (restrained /
   default / strong, scaling `I_core`, `I_halo`, `T`) as `x3m.ini` values chosen per launch, every number in the
   file. No native/plumes comparison hotkey (user decision; the redirects cannot be toggled in flight anyway).

**Fixtures.**

- Host (deterministic, no device): the record parser (synthetic Snapshot + c4–6 with known `z`, both register
  orders, steering bits, the `value`/axis recovery within 1e-5); the ring buffer (distance sampling, SETA stretch,
  gap/cut clear, eviction and fade, map capacity); the body-table parser (fail closed on a malformed table); the
  two stubs' byte layout, the class-7 skip and the class-10 forward, `ret` / `ret 0x10`, and register preservation
  (the sun-occlusion hook fixture's shape).
- GPU (`run_engine_effects.py`, the dropped `run_effects_stage.py` pattern, through the real `TemporalPass`): a
  plume at rest and at 4/8 px per frame over dark and flickering sky (core 1.00, trail ≤ 3 px on dark sky; the
  flickering-sky trail reported); a plane at the nozzle depth head-on and at 20° (core hidden inside the
  silhouette, visible outside, soft rim widths); the chase cap; Reset and refusal (FP16 blend cap) paths; timing
  EVENT-fenced at 1080p and 5120x1440 with 30 and 100 synthetic ships (the 0.1 ms gate).
- One capture flight (`--debug`, F8): (1) chase view, own ship at rest and (2) at full speed — pins the c4–6
  order, `scale[3]`, the glow rows and the scope; (3) an NPC capital from behind and (4) the same from the front
  at a slight angle — occlusion; (5) a fighter group over a starfield — ghosting; (6) a frame with native halos and
  trails before the redirects are armed — the sprite/flare/trail targets and the particle VB prefix. The census
  rows `engine_draw` (pair, `flags130`, model/name, `z`, blend, scope serial, verdict) and `engine_frame` (records,
  suppressed, forwarded per reason, nozzles drawn, ribbons, stage_us) replace the removed `effect_draw`.

**Phases (agent tasks).**

| Phase | Content | Tasks |
| --- | --- | --- |
| 0 (**done** 2026-10-01) | engine-effects.md §7 (both sites, ABI, `&pos`, list readers, `v/00566`, the registry bit) and `tools/effects/engine_bodies.py` (`c7f6c05a`); left for phase 1: the body-name resolver generalised from `lens_flare_cull_core.h` | — |
| 1 (**built and installed** 2026-10-01 as Run118, `526a741c`; reviewed; Run 118 A queued = flight A) | `implement-deep`: recogniser, record, suppression, the c4–6 shadow window, the resolver, census rows; `implement-deep`: the two redirects (class-7 skip, class-10 forward, install-window claim) with the hook fixture; `implement`: the load-time option (`engine_effects=native|off|plumes`, default native until flown), launcher, ledger; one review; flight A (suppression only, F8 set 1–6) | 3 + review |
| 2 (**built** 2026-10-01, not flown) | `implement-deep`: the stage pass, plume programs, GPU fixture, timing; `implement`: strength presets as `x3m.ini` keys; review; flight B | 2 + review |
| 3 (ribbons, fog law, SETA/cut rules **built** 2026-10-01, not flown; RCS puffs closed: phase 2 draws RCS records as short quads) | `implement-deep`: ribbons (ring buffer core, program, fixture rows), fog law, SETA/cut rules; `implement`: RCS puffs, docs; review; flight C | 2 + review |
| 4 (optional, on evidence) | emitter-site anchor records (stock capitals, per-race stock tint); engine-side JET cull for the engine's per-draw time; texture-key fallback for unscoped draws; reactive mark | 1–2 each |

**Phase 2 as built (2026-10-01; ledger [engine-effects.md](../verification/engine-effects.md), "Phase 2").** Code:
`src/proxy/engine_plumes_core.h` (presets, the F6 latch, the CPU builder), `src/renderer/engine_plumes_pass.{h,cpp}`,
`src/effects/engine_plume_{vs,ps}.hlsl` (vs 10 / ps 92 slots after the review fixes [m]), `src/proxy/motion_output_engine_plumes_inc.h`
(arming, census) and `TemporalPass::FrameInputs::stage_callback`. Where the phase-2 brief set numbers that differ from
section 3 above, the brief's are built: core radius 0.15 value (not 0.12) tapering to 0 at L, halo `exp(-d/sigma)` with
sigma 0.5 value at the nozzle (half at the tip), a camera-facing disc of diameter 0.5 value, the flicker evaluated per
nozzle on the CPU (+-10 %, 8-frame value-noise cells), presets scaling I_core, I_halo and sigma (not the ribbon's T),
and a Ctrl+Alt+F6 preset key (risk 3 above said none). Decisions of the build: the near-camera cap holds the plume's
projected **width** (2 sigma at the axis point nearest the camera) to 0.12 H by shrinking the whole plume about the
nozzle, radiance 1 -> 0.5 over the last 20 % (a bounding-sphere rule shrank every long side-on plume); the occlusion
depth of a pixel is that of the nearest axis point (the billboard's own depth would let the halo behind the nozzle
pass in front of the hull at a tilt), pulled 0.5 value x max(0, axis . to_camera) towards the camera so a tail-on
exhaust clears its own hull; RCS records draw unlengthened with radiance x z and are skipped below z 0.02; the screen
minimums (core radius 1.5 px, main-jet L 6 px, cull under 1.5 px) apply after the cap. The record's c4-6 origin and
axis are taken to be in the camera latch's world (`camera_scene_` rows) [i: settled by flight B's first frame].

Review fixes (2026-10-01, ledger "Phase 2 review fixes"): the nearest axis point's depth is the nozzle's view z plus
the axis's view z x clamp(u, 0, L) (the billboard's side vector has a view z component off-centre; its omission let
the core show through a hull near the screen edge); records carry the scope's camera handle and whether they were
recorded in the scene phase, and only the scene view's are drawn (the camera handle the own ship's jets were recorded
under in the scene phase, else the frame's most frequent camera handle among the scene-phase records, `engine_stage
view_rule=own|majority`; the rest count `skipped_other_view`) [i: whether a target-monitor view issues glow jets at all is
settled by flight B's F8 with a target selected]; the lane's size and format are checked at arming (reason `lane`);
three consecutive failed stage frames refuse until Reset (`failed_until_reset`, the third `engine_plumes_failed` row
final=1); the pass's own reset-pending flag fails the stage with `E_FAIL` (only the stage is skipped, the resolve goes
on; lost codes the device returned still fail the resolve); NEAR is the latch's -m32/m22 (6 in the game, not 1); the
axial quad is a trapezoid reaching 2.25 local sigma (the halo window, now linear in d / (2.25 sigma), reaches 0 there)
and the disc is drawn only from |axis . to_camera| 0.15, fading in to 0.3: half the rasterised area [m, host] and
0.276 -> 0.158 ms (1080p) / 0.281 -> 0.060 ms (5120x1440) at 100 nozzles [m].

**Phase 3 as built (2026-10-01; ledger [engine-effects.md](../verification/engine-effects.md), "Phase 3").** Code:
`src/proxy/engine_ribbons_core.h` (pool, length law, strip builder), `src/renderer/engine_ribbons_pass.{h,cpp}` (the
second draw and the pool's owner), `src/effects/engine_ribbon_{vs,ps}.hlsl` (vs 8 / ps 34 slots [m]),
`src/proxy/motion_output_engine_ribbons_inc.h` (attach, run, the fog law) and `src/renderer/fog_transmittance.h`. The
phase-3 brief's numbers are built where they differ from section 3 above:
- *Pool and sampling.* 256 ribbons x 16 samples (position, half-width, time), keyed by the node serial (bit 63) or
  node handle + model, a 512-slot map rebuilt per update; main jets only (RCS and rowless records skipped). A sample is
  appended when the nozzle moved at least `max(1.5 m, 0.02 value, L / 15)`: 1.5 m = 7.5 render units at the fog
  look's 5 units per metre; the `L / 15` term (not in the brief) keeps the 16 samples covering `L` when a frame's
  motion exceeds the fixed spacing (at 8 px per frame and value 30 px the fixed spacing alone covers 15 frames, half
  of `T`). The strip starts at the live nozzle, so there is never a gap at the nozzle.
- *Length.* `L = T(value) x preset x v_est x s` with **T 0.5 s below value 2,000, 0.8 s to 20,000, 1.2 s above**
  (the brief's thresholds, not 1,500 / 8,000; value in the record's scene units); `v_est` = the path from the nozzle
  back through the samples of at least the last 0.1 s, never across a pause of more than 0.25 s, over their age. The
  strip is cut at `L` (the last point interpolated) or ends at the oldest sample when the history is shorter (after a
  SETA burst the path flown, not the extrapolated `L`). No `60 x value` cap.
- *Look.* Half-width `0.6 x the nozzle's half-width` = 0.3 value at the nozzle tapering linearly to 0, held at 1.5 px
  (a 3 px strip, no radiance compensation); radiance `I_ribbon(s) = lerp(0.2, 0.9, s) x preset x tint x (1 - u)` with
  a `(1 - a^2)^2` profile across (not `0.6 (1 - u)^2`); the plume's near-camera rule per strip point (width held to
  0.12 H, radiance 1 -> 0.5 over the last 20 %); occlusion the halo's SOFT 1.0 value at the centre line's view depth
  (per vertex: a strip point lies on its own centre line, the corrected plume depth law without an axis offset). Only
  records passing the plumes' scene-view filter take a ribbon (`ribbon_skipped_other_view`).
  Presets scale the radiance and `T` (0.6 / 1 / 1.5).
- *Lifetime.* Fade 1 -> 0 over 0.3 s at the last positions, then eviction (evictions run first in every update, so a
  stage gap of 0.3 s empties the pool); clears on the resolve's cut (`FrameInputs::cut = counters_.cut ||
  decision.cut || chase_snap`, the TAA cut verdict; `note_cut` fires only on a resolve with that verdict set, drawn
  or not), on Reset (`before_reset`), on a change of the object_lifetime load epoch read with the jet's serial,
  and per ribbon on a jump of more than 8 value in one update (section 5's guard).
- *Fog (section 4).* Not the 13 -> 22.5 km fade alone: the mean transmittance of the look's column,
  `T_rgb(d) = exp(-k_rgb x sigma_eff x rho_mean x D(d))`, `D` the integral of the column weight (1 to 65,000 units,
  smoothstep to 0 at 112,500), `sigma_eff` the march's own extinction (family sigma x density_scale x ready_far x 8),
  `rho_mean` the look's mean shaped density (0.05613 at occupancy 0.12, 0.10980 at 0.24 [m,
  `verification/results/engine-effects/phase3_fog_mean_density.py`], linear between), `k = 1 + 0.6 (1 - chroma)`;
  per nozzle at its distance, on the plume's and the ribbon's colours; on only on frames whose density composite
  applied. Bluewell at 1.0x: T 0.978 at 4 km, 0.905 at the cap [m, host]. A plume inside a cloud is attenuated less
  than the hull behind it, one in a gap more (a mean, not the march).

**Native Windows.** Documented D3D9 only: dynamic VBs, `DrawIndexedPrimitive`, vs_3_0/ps_3_0, `tex2Dlod` on the
lane, FP16 post-pixel-shader blending behind `CheckDeviceFormat` (the motes' query), no VTF, instancing, point
sprites or MRT in the stage. The EXE seams (the object scope at `0x004c5228`, the two new call sites) are the same
bytes on Windows and are validated at exact sites. Unverified natively like the rest of the proxy; add the row to
[platform-portability.md](platform-portability.md) when phase 1 lands.

## Unknown, and what settles it

| # | Unknown | Settles it |
| ---: | --- | --- |
| 1 | Whether `0x00414590` can run before the first d3d9 export call, where the redirects install (`load_backend`, InitOnce, before any device or frame; the B site's plain write relies on that window, met as long as the effect routine runs only in the frame loop [i]); the docking path; an indirect reader of the two lists through a copied pointer (none found) | The hook fixture cannot; a loading-trace row at the first Present (phase 1) and the flight's `engine_frame` counts settle the first two |
| 2 (**settled** run401: order a on 117,442 records, 0 b, 1 mismatch) | The c4–6 register order of the glow draw (needed only for the cross-check and the census) | F8 1–2; the Snapshot's `scale[3]` carries `z` regardless |
| 3 | Whether every JET-flagged draw has an object scope (by construction yes: the jet takes the ordinary path [m]) | Census `scoped=` |
| 4 | Settled: `v/00566` is on both lists, flag `0x4000001` plus `+0x1d8 = 5` [m, §7] | — |
| 5 | Interaction with the x3m small-parts cull (launcher default 4 px): a culled jet gives no record, so a distant plume disappears with the native glow; exempting JET nodes costs one flag test in the stub | Cull census of model ids at a station view; decide in phase 1 |
| 6 | Trails of bright plumes over real starfields; the meter's response to many cores | Flight B/C |
| 7 | The body-unit to world-unit factor (hull lengths are in LOD-0 units); every law above is relative to `value`, so nothing depends on it | The F8's `object_bounds` against a known ship |
| 8 | Native Windows behaviour | Not verifiable by the user |
| 9 | Whether glow jets drawn in another view (target monitor) reach the record ring inside the Scene phase; phase 2 draws only the frame's most frequent scene-phase camera handle and counts the rest `skipped_other_view` | Flight B: one F8 with a target selected; `engine_stage skipped_other_view=` |

## Options considered and why they lose

- **A. Engine-side cull of JET nodes on the small-parts site + node reads at scene end** (the `lens_flare_cull`
  shape: the stub lists JET nodes, the proxy reads `+0xb0/+0xc0/+0x80..+0x88/+0x70` per node). Recovers the
  engine's ≈ 24 µs per issued draw as well as the frontend's 7.5 µs, but needs a per-frame node list written by
  the stub, a context-scale conversion for every record, and reads of nodes the draw path never validated.
  Kept as the phase-4 optimisation if the flight's `--perf` phases show the glow draws' engine time matters.
- **B. Update-site hook `0x0045b09a`** (EBX = object, ECX = node, `z`): gives true speed and `vmax` and the race
  for every ship, but fires only when `z` changes, gives no draw-time world matrix, and reads game objects. Not
  needed: the draw already carries `z` and the world.
- **C. Keep the native glow mesh and add plumes on top** (the dropped design's 3.6): the user asks for the native
  effects to be blocked, and the native mesh's screen blend cannot be gained (refused_screen [m]).
- **D. Data route: overlay copies of `types/Effects` and `types/Particles3` with the engine rows removed**, written
  by `x3m-regenerate`. No EXE site, trivially native, but it masks a mod's later table updates until regeneration,
  depends on the engine's handling of missing ids (17 Mayhem ships already reference a missing Effects row [m];
  a missing Particles3 id is [u]) and still cannot touch the glow. Fallback for the sprite/trail half if a site in
  option 1 is not patchable.
- **E. Texture-key recognition** (the removed upload-time registry): needs 135 lines back in the ownership layer
  and a key per texture including the stock animated family; the node flag is one compare on data the route
  already reads. Kept as the phase-4 fallback for unscoped draws.
- **F. Per-particle filtering of the trail VB at Unlock**: the vertex carries nothing that separates a ship puff
  from a missile puff on material 406 [m]; refused.
- **G. Lens-bracket filtering of engine halos by body**: the engine rows' bodies are the sun's ray and ring bodies
  [m]; by record owner it would need the node → record association [u]; redirect A removes the element at birth.
- **H. A cone mesh per nozzle or a sprite strip**: more vertices or per-instance transforms for no look the
  analytic quad pair does not give; a soft-particle depth fade was not requested and the lane term already
  softens the silhouette.

## Plume look redesign (2026-10-03, after flight B)

Flight B (run403) rejected the analytic cone as static. The look is redesigned in an offline WebGL mock-up (`tools/effects/engine_exhaust_lab.html`,
published as the Engine Exhaust Lab artifact) with the same throttle law, and the user chose, with licence for fine-tuning:

```
exhaust: bulge=1.15 taper=0.45 tail=0.7 ring=0.6 turb=0.6 flow=3 erode=0.57 pulse=0.25 shock=0.5 period=0.16 cfade=0.6 heat=0.7 core=0.45 halo=1.1 hb=0.35 exp=1 tint=Split
```

Meaning (all relative to the nozzle width `value` and the game length `L = z·value`): a mouth bulge of 1.15 then a near-cylindrical section tapering
(0.45 between cylinder and cone), a soft tail (0.7), a bright ring at the nozzle mouth (0.6); 3-octave value-noise turbulence (0.6) flowing away from
the nozzle at 3 lengths/s with edge erosion 0.57 and a 25 % length pulse; shock diamonds of strength 0.5 at a period 0.16·L fading along the plume
(0.6); a white-hot core (heat 0.7) of radius 0.45 nozzle widths cooling into the race tint; halo 1.1 wide at 0.35. The mock-up's `plume()` function is
the reference for the ps_3_0 port; presets keep scaling I_core, I_halo and sigma.

**Ported (2026-10-03, review fixes the same day).** `src/effects/engine_plume_ps.hlsl` is the mock-up's `plume()` (modern branch) in ps_3_0, the
constants in one block, `engine_plumes_core.h` `Look`, uploaded per frame to c3..c7 (one `SetPixelShaderConstantF` of eight registers with the lane
terms, the clock and the flow phase). The readback matches a CPU replica of the law within 0.1 % of I_core (fixture `law_*`). Final constants: the
user's numbers, unchanged: bulge 1.15, taper 0.45, tail 0.7 (the mock-up's tail narrowing 0.6), ring 0.6, turb 0.6, flow 3, erode 0.57, pulse 0.25,
shock 0.5, period 0.16, cfade 0.6, heat 0.7, core 0.45, halo 1.1, hb 0.35; disc sample u = 0.16. The first port's tunings (bulge 1.20, tail
narrowing 1.6, the halo sigma following the local width) are reverted: the fixture's shape gates now measure the body alone against the mock-up's law
instead of forcing the look. Port decisions:
- *Nozzle width = value / 4*, a load-time knob: `engine_plume_nozzle` (ini), `X3M_ENGINE_PLUME_NOZZLE`, launcher `--engine-plume-nozzle`, one
  plain decimal 0.1..1.0, default **0.25** (0.5 after flight C, below) ([config-file.md](config-file.md)). The mock-up's length is L = 4 (0.25 + 1.75 s) nozzle widths and the
  game's z value with z = 0.25 + 1.75 s, so value / 4 keeps the chosen proportions (the first look's core, 0.3 value across, is the mock-up's
  one-nozzle "current" cone); 0.5 draws a plume twice as wide relative to its length (L = 4 nozzle widths at full throttle), for a flight A/B of
  0.25 against 0.5 in two launches.
- *Flow phase.* The noise field translates along the axis by a phase in nozzle widths accumulated on the CPU once per frame (`FlowPhase`:
  phase += rate x the stage clock's step, wrapped at 4,096 nozzle widths, uploaded in c0.z), rate = the mock-up's scroll 0.35 flow L / 1.6 at the
  unpulsed design length of s = 1 (L = 2 / nozzle_width nozzle widths): 5.25 nozzle widths per second at the default width, the same speed in value
  units at any width, constant whatever the pulsed, throttle-dependent L. The first port scrolled by t x 0.35 flow L with the pulsed L, so the phase
  jumped every frame and the turbulence decorrelated frame to frame from t ~ 10 s; the mock-up takes the same accumulator. The mock-up's speed scaled
  with the throttle's length; the port's does not.
- *Length pulse* on the CPU per seed byte and frame (`PulseCache`: at most 256 fbm evaluations a frame), recentred to mean 1 (1 + 0.25 (2 fbm / 0.875
  - 1); measured 0.83..1.18 over 66 s, mean 1.000), so the throttle's length law holds on average and the quad follows each frame's length.
- *Halo* about the segment nozzle..tip (no cut at u < 0 or u > 1 as in the mock-up) at the nozzle's sigma along the whole plume (the mock-up's),
  tapered only over its last 0.5 sigma before the quad's reach of 2.25 sigma (`saturate((reach - d) / (0.5 sigma))`), so inside the reach it is the
  mock-up's. *Ring* Gaussian widened to the pixel footprint (energy kept). *Shock mask* the mock-up's `1 - smoothstep(0, 0.8, radial)`. *Disc*
  (head-on / tail-on): the same law end-on at u = 0.16 (one cell in, the cell's crest). *Fog*: the white-hot core and the ring take the
  transmittance too (a third vertex colour; the vertex is 72 bytes).
- *Quad:* a trapezoid linear in x, each end the wider of the body's eroded edge over the width line and the halo's reach (constant along the plume),
  + 1 px; the chord of a convex bound encloses it.
- *Near-camera cap:* the plume's drawn width, 2 x its widest half-width (the halo's reach 2.25 x 0.55 x the preset nozzle widths, or the body's
  eroded edge), is held to 0.12 H, no longer the first look's reference of 1 value (about 1.6 x the drawn width at the default preset).
- *Unchanged:* the soft lane occlusion, the near fade, the presets (I_core, I_halo and the halo sigma x 0.6 / 1 / 1.5), ONE/ONE on FP16. The flicker
  of the first look is gone (the turbulence replaces it).
- *Clock:* `StageClock`, the performance counter between stage runs, a step on or after an F8 capture frame held to the last ordinary step (at most
  0.1 s); wrapped at 1,024 s for the pixel program. The ribbons' pool and the flow phase take the same clock (the run403 capture-frame eviction).
Cost: ps 92 -> 385 slots, vs 10 -> 11 (measured); the axial quad's area at value 100 px is 0.12 / 0.19 / 0.23 of the first look's at s = 0 / 0.5 / 1
(`verification/results/engine-effects/plume_look_area.py`; the halo keeps its width to the tip); the fenced stage cost stays within the method's noise
and the CPU build of 1,024 records takes 0.46 of the per-record pulse's time with distinct seeds (ledger).

**After flight C (2026-10-03, Run 120 A: run404 nozzle 0.25, run405 nozzle 0.5).** The look is kept; the user chose
nozzle 0.5 and asked for three adjustments. All constants are in `engine_plumes_core.h` `Look`.
- *Default nozzle 0.5* (`engine_plume_nozzle`, [config-file.md](config-file.md)). L = 2 z nozzle widths, 4 at full
  throttle. The flow is 2.625 nozzle widths/s (the same speed in value units as before). A plume's drawn width is now
  2 x 2.25 x 0.55 x value / 2 = 1.24 value, so the near-camera fade started at a value of 0.077 H, half the earlier
  threshold (the behaviour flown in run405; superseded below: the fade keys on the body width, 0.73 value, from 0.13 H).
- *End-on disc.* The camera-facing disc represents the whole plume seen along its axis. Its body is the law
  integrated along the axis: the mean over 8 samples u_k = (k + 0.5) / 8 of the law at radial = rho / w(u_k). The
  per-look table w, tail, cell, heat sits in c8..c15 (`look_tables`), and the shock cells form rings at 0.8 w(u_k).
  The disc's noise lies in its own plane (x 3 per nozzle width, like the axial quad's y), and the flow along the line
  of sight is the noise's third coordinate.
  - Radiance: I x `disc_kappa` 1.8 x L / n x f, with f = |axis . to_camera| as the view factor. The halo has the
    nozzle's sigma and gain I_halo x `disc_halo` 3 x L / n x f. The ring keeps its law.
  - Bound: the total is soft-capped, cap x (1 - exp(-total / cap)), with cap = `disc_cap` 1.5 x the side view's axis
    peak (1.6 x I x max(1, tail(period) (1 + s x cell(period)))).
  - kappa is the ratio of the side view's body energy to the integrated profile's, per nozzle width of length; the
    model gives 1.76..1.78. The halo gain carries the energy the cap removes. Model:
    `verification/results/engine-effects/plume_end_on_model.py` -> `plume_end_on_model_out.txt`.
  - Facing: the disc's weight is smoothstep(0.3, 0.7, f), so no disc is drawn below 0.3. The axial quad's weight is
    1 - 0.5 x the disc's (`axial_floor`), so from 0.7 up the foreshortened plume keeps its length at half weight.
  - Occlusion: the disc's depth stays the nozzle's. The soft lane occlusion decides what a front view shows, and the
    head-on core stays hidden inside the silhouette (fixture `headon_core_hidden_*`).
- *Mouth.* The ring and the halo combine as a soft maximum (a^4 + b^4)^(1/4), with the colours weighted by a^4 and
  b^4; the body still adds. The ring drops from 0.6 to 0.3. The shock cells ramp in over the first period
  (x smoothstep(0, period, u)), so the mouth is no longer a crest: with the cells' u = 0 crest, the mouth at s = 1 was
  1.31 x the first crest at u = 0.16 on the axis.
  - Hand-over: where the disc is drawn, the axial quad gives its mouth to the disc. It is multiplied by
    1 - w_disc x (1 - smoothstep(0.3, 0.8, d)), where d is the screen-plane distance from the nozzle in nozzle widths,
    (x sin(view), y).
  - Vertex bytes: the disc weight travels in params A and sin(view) = sqrt(1 - f^2) in the fog colour's A; c7.xy hold
    0.3 / 0.8. Without the hand-over, the half-weight axial mouth on the full disc peaked at 1.74 x the side body at
    30 degrees (measured, before the hand-over).
- *Capital sub-engines.* A main jet's value becomes max(value, `sub_floor` 0.45 x the largest main-jet value of its
  ship).
  - The ship is the record's parent node, node+0x18, which is the ship's root for every engine part
    ([engine-effects.md](../reverse-engineering/engine-effects.md)). It is read once per suppressed record beside the
    own-ship tag (LastError preserved) into `Ring::parent`; 0 when unreadable means no floor.
  - Scope: the floor raises the plume's size, length, minimum sizes, cull, SOFT and bias, never the nozzle's
    position. RCS jets neither count nor take the floor, and only the drawn view's records count.
  - Grouping: `ShipFloor`, an open-addressing map with linear probing, sized to at least twice the records (at most
    2,048 slots, 16 KB, held by the pass), cleared only over the slots in use, with one note and one lookup per record.
  - Cost: 0.42 / 4.39 us at 100 / 1,024 records under Wine (0.38 / 2.36 us on the host).
  - Logging: the `engine_stage` row gains `floored=` and `ships=`.
- *Slots.* ps 385 -> 680 (vs 11), above the 512 this runtime reports. The pass no longer refuses on the reported cap:
  creation is the capability test, per the slot-budget rule in
  [platform-portability.md](platform-portability.md#shader-slot-budget). The disc's integration sits behind a dynamic
  branch, so axial pixels do not pay for it.

**Review fixes after flight C (2026-10-03).** Decided by the orchestrator from the review of af932635; numbers in the
ledger ([engine-effects.md](../verification/engine-effects.md), "Review fixes after flight C").
- *Near fade and cap on the body width.* The near-camera cap (0.12 H) and its fade band key on the body's half-width at
  the nozzle, spread x line0 = 0.732 nozzle widths (the eroded edge 1.2736 x 0.575, wider than the ring's 0.7226), no
  longer on the halo's reach 1.2375. Keyed on the halo it bit on 12,323 of run405's 19,319 plume frames (measured).
  The fade now starts at a value of 0.13 H. The halo may reach past the cap (131 px against 129.6 at 1080 in the
  fixture's three-value case).
  - The disc's radiance (body, halo, ring and its soft cap) takes max(fade, `chase_disc_floor` 0.6) instead of the
    fade: it shrinks with the plume, it does not go dim. The axial quad still fades to 0.5.
  - Fixture own-ship case: an M3 main jet (1,000 camera units = 10 world units) at run405's chase boom (18,832, half
    vfov tan 0.5625), the nozzle a quarter of the boom nearer, full throttle at the pulse's top. Its body is 61 px at
    1080 (q 0.47): not faded. Under the halo key, q would be 0.80.
- *Floor scope.* `flag_brake` main bodies (z above 2: the brake or steering bits) neither count toward a ship's
  largest main jet nor take the floor, like the RCS jets.
- *Ship key read.* `object_trace::current` copies node+0x18 into `Snapshot::parent` from the node block it already
  reads (it used to only with `matrices`); the recogniser takes it there, so the second bounded read per suppressed
  record is gone.

**After flight D (2026-10-03, Run 121 A: run406).** Two findings
([run406 triage](../../verification/results/run406-engine-plumes/README.md)). First, the game culls nozzle nodes one
by one, so a per-frame floor taken from the largest drawn jet of a ship changed a secondary's size with the screen
position (a capital drew 1 of 2 `huge` and 4 of 8 `big3` on one capture frame). Second, small ships' plumes read small
and the mouth too strong. User decisions: the floor comes from the ship itself, not from frame memory, and the mouth
never exceeds the body at any throttle. All constants are in `engine_plumes_core.h` `Look`.
- *The plume floor from the ship's radius.* A main jet draws `value_eff = min(max(value, k(R) x R), 4 x value)`. RCS
  (`flag_steering`) and brake- or steering-pushed bodies (`flag_brake`) are excluded as before. The per-frame
  `ShipFloor` map and `sub_floor` are gone.
  - R is the ship's root node's `+0xa4`, read through the jet's parent (node+0x18): the engine's cached subtree radius
    (`0x00488170`, [engine-effects.md](../reverse-engineering/engine-effects.md) "Ship radius"). It is the maximum
    over the root's children and the three axes of |child offset| + the child's own radius, so it covers the hull
    and every part, culled or not. Its units are those of node+0x70 (the LOD-0 value), so the radius in the record's
    units is R x size / (+0x70 x +0x80 / 65536), with the jet's own +0x70/+0x80 from the node block already read
    (`parent_radius_in_record`). A dirty (-1), unread or non-positive R gives no floor (counted `floor_unknown=` in
    `engine_stage`), and so does an R above `parent_radius_max_ratio` 10,000 x the record's value: a garbage positive
    read would otherwise always land on the 4x cap (the fleet's largest main nozzle / R is about 0.09).
  - The read: one bounded `engine_memory` read of parent+0xa4 per parent while it stays among the frame's four most
    recently read parents (`engine_parent_radius`, a four-entry memo cleared each frame, the oldest entry replaced on
    a miss), LastError preserved, stored per record in `Ring::parent_radius`. Only where it is used: a suppressed
    record while plumes are requested with `engine_plume_floor` > 0 (never in `off` mode or with the floor at 0), and
    a JET draw (suppressed or forwarded) that writes an `engine_draw` row under --debug, not after the row caps.
  - k depends on the ship's size, because the user wants small ships' plumes to stop being small while capitals stay
    as they are. One k cannot do both: the Mayhem fleet's largest main nozzle / R is about the same at every size (the
    M6's 0.086 against the capital's 0.094; offline estimate over 405 ship scenes: q1 0.057, p40 0.086, median 0.099,
    q3 0.162; stock 198 scenes: q1 0.167, p40 0.222, median 0.280, q3 0.411).
    - Rule: k(R) runs through three anchors (`floor_r` / `floor_k`): 0.35 at R <= 150 record units (fighters), 0.25 at
      500, 0.10 at R >= 5,000 (capitals), linear in ln R between neighbours (`floor_ratio_at`; `law::ln` without x87).
      The cap is `floor_cap` 4 x value.
    - Knob: `engine_plume_floor` ([config-file.md](config-file.md)) scales the whole curve (`floor_scale`, default 1,
      0.5 after flight E; 0..3); 0 turns the floor off.
    - Reach: 393 of 405 Mayhem ships raise their largest main jet (77 to the cap; k 0.35 applies to 152, 0.10 to 59).
      Stock: 124 of 198 (1 capped).
  - Effects on run406's ships (record units = value x 0.01; R estimated offline, the runtime +0xa4 is at least the
    hull's):

    | Ship (scene) | R (record units) | k(R) | Nozzles before -> after |
    | --- | --- | --- | --- |
    | Capital, `split_m2p_ocelot` | 10,022 | 0.100 | `huge` 939.2 -> 1,002.2 (+7 %); `big3` 187.5 -> 750 (the 4x cap; was 422.6 beside a drawn `huge` and 187.5 without) |
    | M6, `split_m6_heavy_dragon` | 467 | 0.256 | `nor3` 40 -> 119.4 |
    | Own ship, if a Split M4 (`split_m4_scorpion`) | 67.3 | 0.350 | `nor` 10 -> 23.5, `tiny` 5 -> 20.2 (cap) |
    | Own ship, if a Split TS (`split_ts_caiman`) | 159.7 | 0.345 | `nor` 10 -> 40 (cap), `tiny` 5 -> 20.2 (cap) |

    Script: `verification/results/engine-effects/floor_ratio_effects.py` -> `floor_ratio_effects_out.txt`. The run406
    log does not name the own ship.
  - Flight check: `engine_draw` census rows now carry `radius=` (the ship's radius in record units, 0 unknown) and
    `value_eff=` (the value the stage draws at, `floored_value` with the device's look). Comparing a ship's `radius=`
    across F8 frames verifies the live +0xa4 read and shows whether it drifts with throttle.
  - Not analysed, for Run 122 to check:
    - The floor applies to every main JET record with a readable parent, so also to missile jets and any other
      non-ship JET parent; their R and the resulting lift were not estimated (`radius=` / `value_eff=` rows of
      non-ship models).
    - R may step with throttle: a jet's own radius is value x z above z = 1, and it enters the root's +0xa4 whenever
      that is recomputed (attach, save restore) ([engine-effects.md](../reverse-engineering/engine-effects.md)
      "Ship radius").
    - The own ship's lifted plume reaches the chase-view cap and fade more often (`capped=` / `faded=` in
      `engine_stage`).
- *Mouth.* The halo and the ring now follow the body's throttle curve I(s) / I(1): hb x lerp(1.2, 4, s) / 4 and
  ring x lerp(1.2, 4, s) / 4, where the halo was already lerp(0.3, 1, s) and the ring was lerp(0.4, 1, s).
  - That alone cannot hold the mouth below the body. At s = 0 the brightest point of the side view was the body
    itself at the nozzle: the tail falls from u = 0 and there are no cells. The model gave 1.088 from the body alone,
    against 1.128 measured with the mouth terms.
  - So the body ramps in, x (1 - `mouth_dip` 0.5 (1 - smoothstep(0, `mouth_ramp` 0.3, u))), in the side view (c16)
    and in the disc's tail samples (`law::tail`).
  - The side view's axis peak per I_core drops from 1.832 to 1.462 at s = 1 and from 1.600 to 1.258 at s = 0
    (`peak_axis_at`, now the maximum of tail x ramp x (1 + s cell) over 513 samples of u for s in eighths). The end-on
    disc's cap follows it. The first 0.3 L reads dimmer; this is the look change to judge in flight. The ramp applies
    to every body, RCS included, which takes no floor: RCS plumes lose up to 50 % near the nozzle with nothing to
    offset it.
  - Mouth peak / body peak in the fixture: 0.595 / 0.658 / 0.773 at s = 1 / 0.5 / 0 (1080p; 0.642 at s = 0.5 at
    5120x1440), gated at 0.85. Before: 0.890 / 0.998 / 1.128.
  - The lab (`tools/effects/engine_exhaust_lab.html`) mirrors the ramp and the curve. ps 680 -> 687 slots.
- *Cost (measured, Wine, X3 bottle).* Per suppressed draw (run_engine_effects timing): 0.694 / 0.668 / 0.666 us with
  the parent's radius on the memo, against 0.657 / 0.629 / 0.614 us for the same draw without a parent in the same
  runs (+0.04..0.05 us). A memo miss (the parent alternating every draw) costs 0.744 / 0.706 / 0.711 us, which a ship
  pays once per frame. The CPU build with cached look tables: 9.55 vs 9.40 us at 100 records and 101.3 vs 97.0 us at
  1,024, without and with the floor (no measurable change).
  - Review fixes (2026-10-03): the read now runs only with the floor on or for a census row (none in `off` mode), and
    the memo is a four-entry recent list. Timing mode now runs `plumes` (the floor on). One entry vs four: hit 0.687 /
    0.695 us, two interleaved parents 0.733 / 0.683 us, a five-parent miss 0.738 / 0.738 us, no parent 0.661 / 0.683 us
    (one run each; `verification/results/engine-effects/radius_memo_ab_out.json`).
- *Not taken from the data.* The ship scene's id rides on every scene-part node (+0x258), and the generator could key
  a per-ship table on it. Per the user's decision, the floor uses the radius at draw time and the generator is
  unchanged.
- *Disc gains bounded.* L / n in the disc's body and halo gains is held to `disc_length_max` 8. A thin nozzle (0.1: L / n
  20) would otherwise saturate the soft cap into a flat disc; its end-on energy is then 0.28 of the side view
  (reported, 0.25: 0.71, 1.0: 1.09).
- *Tables cached.* `look_tables` runs once at load (`MotionOutput::plumes_tables_`, `EnginePlumesFrame::tables`), not
  twice per frame. The fixture's frames without tables compute them once per run.
- *Stage off: the game's glow.* With `engine_effects = plumes` and the stage not attached on this device (refused at
  attach or creation, failed until Reset, or within the 64-frame disarm), the recognised glow draws are forwarded
  natively, counted `forwarded_stage_off` in `engine_frame`, and `engine_plumes_state` says `glow=native`.
  - The latch is taken once per frame in `engine_effects_frame_begin` from the state the last resolve left, so the
    frame that finds the refusal still hides its glow.
  - Configuration and path reasons (`suppression_off`, `hdr_taa_path`, `camera`, `lane`) keep the off look.
  - The sprite and trail redirects are load-time and stay.
- *Ribbon slots.* The ribbon pass no longer refuses on the reported slot caps (creation is the test, as for the
  plumes), and `engine_ribbons_device` logs `max_vs_slots` and `max_ps_slots`.
- *Lab.* `tools/effects/engine_exhaust_lab.html` draws L = 2 (0.25 + 1.75 s) nozzle widths (4 at full throttle, the
  game at nozzle 0.5), the flow at L1 = 4, and defaults the core radius to 0.45. Its note says the end-on disc, the
  hand-over, the floor and the near fade are game-only.

**After flight E (2026-10-03, Run 122 A: run407 / run408).** Far ships showed no plumes at all
([run407/408 triage](../../verification/results/run407-408-engine-plumes/README.md)): the proxy's small-parts cull
(`cull_small_parts`, 4 px, threshold 3) culled every far jet node (s = 1..2) before the engine submitted its glow, so
the recogniser never saw them. User decisions: keep culling them (the engine's submission work per jet, about 10 us, is
what the cull saves; 100 far jets would cost about 1 ms) and draw them as faint sparks.
- *Far jets from the cull.* With `engine_effects = plumes` the cull stub carries a far block
  ([cull-small-parts.md](../verification/cull-small-parts.md) "Far engine jets"): a node it culls whose +0x130
  carries the JET flag pair 0x4000001 is handed to `x3m_engine_far_jet` (`engine_far_jets.cpp`, integer only, no SSE,
  no Win32, inside the pass) before the cull, which copies the node's raw fields into a per-frame buffer of 1,024. It
  skips v/00566 (RCS) and a jet the engine would cull itself (the size limit max(+0x1d8, parent +0x1d8) or the
  degenerate test: the census's `culled_size` / `culled_min`). The jet stays culled: nothing is submitted.
  - At the plume stage (`engine_far_append`, once per frame, before the scene view is chosen) each copy becomes a
    64-byte record (`engine_effects_core.h` `far_record`, `flag_far`): origin = +0xb0 x the view's context scale
    (the float at `*(view+0x1c)+0x2c`, one bounded read per context and frame), axis = -(basis row 2), size = |basis
    row 0| / 65536 x +0x70 x +0x80 / 65536 x the scale, s and z from +0x88 (the construction of `0x004bdee0` with c4-6
    order a, flight A). Tags as a suppressed draw: the camera handle the pass's view carries (+0x28, the object
    scope's camera), the scene phase the selector was in when the pass met it, the parent and its radius through the
    same memo (the floor), the own-ship tag. SMALLJET table entries are dropped. The ring's cap applies.
  - A frame whose only jets are far ones runs the stage too (the resolve installs the callback for buffered copies).
  - The engine's own culls stay: a hull culled whole takes its jets with it (their children never reach the site).
  - Counts in `engine_stage`: `far_jets` (copies), `far_records` (appended), `far_engine`, `far_overflow`,
    `far_dropped`, `far_disarmed`; the cull's rows say `far_jets=on|off|writer_mismatch`.
  - Not verified in flight: the view's +0x28 equals the draw scope's camera handle (else far records count
    `skipped_other_view`), and the context scale is 0.01 in the scene view. Ribbons on far records key on node handle +
    model (no lifetime serial), so a jet crossing the cull threshold restarts its ribbon.
- *Distance law.* A plume whose projected nozzle width (after the floor and the near cap, before the dot floor) is under
  `far_px_full` 12 px scales its radiance (core, halo, ring and disc alike) by `far_low` + (1 - `far_low`) x
  smoothstep(`far_px_min` 2, 12, px), 0.15 at 2 px and below, 0.449 at 6 px. The drawn geometry never falls below a
  2 px wide, 4 px long dot (the minimums were 3 / 6). Ribbons take the same factor from the plume's nozzle (Look nozzle
  width x the floored value) at the head and keep their 3 px floor. `engine_stage far=` counts the nozzles under 12 px.
- *Halo* `hb` 0.35 -> 0.20 (plume and disc halo; the lab's slider default). *Floor* `engine_plume_floor` default
  1.0 -> 0.5 (run408 confirmed it); per class at 0.5:
  `verification/results/engine-effects/floor_by_class.py` -> `floor_by_class_out.txt`.
- *Cost (measured, Wine, X3 bottle; fixture timings, not game FPS).* The cull stub with a far copy: 5.6 ns per culled
  jet over a plain culled node (64-node tree, `run_cull_small_parts.py`). The CPU build at 300 records (250 far):
  29.9 us, 32.3 us with the floor. The stage's GPU at 300 nozzles of which 250 are far: 0.84 ms at 1080p, 0.26 ms at
  5120x1440 (EVENT-fenced tail; 100 nozzles of the old crowd: 0.36 / 0.14 ms).

**After flight E, the gap analysis' phases 2 and 3 (2026-10-03, worktree build, not flown).** Gaps 4, 5, 3, 10, 6 and 7
of [engine-exhaust-gap-analysis.md](engine-exhaust-gap-analysis.md) (section 5 there has the measured numbers; ledger
[engine-effects.md](../verification/engine-effects.md)). Every new term is bounded; none brightens the cruise look.
- *Flow in world units (gap 4).* The frame's flow accumulator (`FlowPhase`, flow_rate 2.625 nozzle widths per second,
  unwrapped in double) x the nozzle's `flow_factor` = `flow_reference` 500 / value clamped to [`flow_slow` 0.3, 1]
  (value: the floored value before the near cap), wrapped at 4,096 per nozzle in double and written per vertex
  (`shape.w`; the kind moved to the new colour's alpha). One world speed, 656.25 per second, from value 500 to 1,667;
  smaller nozzles keep today's rate in nozzle widths (no strobing), capitals crawl at 0.3.
- *Two-tone colour (gap 5).* The body's colour is lerp(lerp(head, mean, smoothstep(0.3, 1, u)), white, heat); the head is
  the table's peak colour scaled to the mean's Rec. 709 luminance when brighter (`head_colour`: the peak is the whiter
  colour at 1.0-3.6x the mean's luminance, so the head turns whiter, not brighter); a fourth D3DCOLOR in the vertex (76 B).
  The disc integrates the same split exactly (the tail weight per sample, a compile-time constant). Halo and ring keep
  the mean. Without table colours head = mean (today's look).
- *Nozzle spill (gap 3).* The halo's lane visibility is max(soft, spill) with spill = `glow_through` 0.15 x (1 -
  smoothstep(`spill_inner` 0.8, `spill_reach` 1.0, d)) x saturate(1 + gap / (`spill_depth` 2 x value)), d the
  screen-plane distance from the nozzle in nozzle widths (the hand-over's): around the nozzle rim only, and only through
  an occluder within 2 value in front of the nozzle (its own hull, not a ship passing in front). Body and ring unchanged.
- *Idle floor (gap 10).* A main jet's L = max(z, `idle_length` 0.5) x value: 1 nozzle width at idle (was 0.5); RCS keeps
  z value.
- *RCS puff attack and retro flare (gap 6).* `Transients`: 512 slots of the last z per steering or brake record, keyed like
  the ribbon pool (serial, else node handle + model), probed 8 from the key's home, free after 0.5 s unseen; a rising z
  multiplies the radiance by 1 + `attack_gain` 0.5 x saturate(dz / (0.004 x dt_game_ms)) (the game's own rate limit,
  dt in game ms = wall x the SETA rate), decaying linearly to 1 over `attack_decay` 120 ms; updated before the idle cull
  so a puff from z 0.01 is seen; no shape change; main jets unaffected. Overflow draws without the attack (counted).
- *Travel look under SETA (gap 7).* One bounded read per stage frame (`engine_effects::seta_read`): `*0x00606f34`, then 8
  bytes at +0xcc (warp, governor), bound once by the 12-byte compare of the tick's `mov edx,[ecx+0xd0]; mov eax,[ecx+0xcc]`
  at 0x004d1ef0; refused without the identity, on a mismatch, a null pointer or a failed read; values outside
  0 < warp <= 0x640000, 0x4ccc <= mult <= 0x10000 count invalid; both fail closed to 1.0. `TravelRamp`: engaged at once
  when the warp is above 1.0, released after 0.3 s at 1.0, weight 0 -> 1 over 0.5 s (smoothstep), steps held to 0.1 s.
  At weight 1: a main jet's L x 2, its radiance x 1.25, the ribbons' T x 2, the flow x 1.5. `engine_seta` rows under
  --debug ([logging-tiers.md](logging-tiers.md)).
- *Lab.* `tools/effects/engine_exhaust_lab.html` mirrors the flow factor (a nozzle-value slider), the two-tone head and
  the idle floor; the spill, the distance law, the attack and the travel look are game-only.
