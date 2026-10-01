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
redirects inside the per-ship effect routine `0x00414590` skip the effect-instance and trail-generator calls (ship
engines only; weapon and missile trails spawn elsewhere and share the particle material, so a draw-level cut is
impossible). Plumes and ribbons are drawn by a proxy stage in the TAA resolve's step-0 bracket (the dropped stage's
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
| **Glow jet** (scene node, `engine.fx`/`effects.fx` pair `d5e1c753…`/`8360f422…`, Z-write off, blend ONE/INVSRCCOLOR or ONE/ONE) | The draw runs inside the object scope with the jet node at `ESP+8` [m]; `object_trace::Snapshot.flags130` is `node+0x130`, set to `0x4000001` for every JET-list body (`0x00434708`) [m]; `model` is the body id; `scale[0..3]` = `+0x70, +0x80, +0x84, +0x88` [m, `object_trace.cpp:300-307`]. Rule: effect pair bound **or** any pair with blending on and Z-write off, **and** `flags130 & 0x4000001 == 0x4000001`. An opaque JET draw (Z-write on: the 9 `standard_lighting` / 4 `terran.fx` nozzle bodies) stays native: it is geometry | `route.submit = false`, `submission_error = D3D_OK` (the lens-gain skip contract); the engine never tests the result (`0x004c403c`) [m] | Mayhem 2,858 glow parts in 499 ships; stock 432 in 259 ships [m]. The flag, not the texture, so the stock `-79` animated bodies and the 99 legacy stock materials are covered the same way |
| **Emitter sprite** `B_GLOW` (`objects/v/00011`/`00213`, legacy MATERIAL3, texture 369/368) | Not recognised at the draw (its pair is untraced [u]); it is never created | Call redirect A: the `0x004148a0(0, k, eff, obj, …, &pos)` call inside `0x00414590` (reached unless `C & 0x4000`) [m: decompiler, offsets checked at `0x0041472f`/`0x004147c4`/`0x004147ff`; exact call bytes **[u]**, phase 0]. The stub returns without creating the effect instance. "Not created" is a state the game already exercises: every `0x7001` main part carries the `0x4000` bit [m] | Mayhem rows 700–729 (sprite + lens flare); stock rows 3–14 (sprite and/or `EEDF_LIGHT`). A per-`eff` allowlist in the stub keeps the stock capital rows (9–14, 3) native until phase 4 (section 2) |
| **Engine lens flare** (`EEDF_LENSFLARE` element, bodies 753/754/778/781/`v\01016`, drawn in the engine's lens scene: 113–169 draws per frame on Mayhem 3, 0–10 on stock [m, run364/run375]) | Element of the same effect instance | Redirect A removes it with the sprite. The existing lens-flare gain (default 0.3 [m, launcher]) and the engine cull at gain 0 stay for the sun; they cannot separate engine halos from the sun's rays by body (753/754/778 are in both sets [m]) | Both |
| **Particles3 trail** (particle renderer `0x004bf4c0`, one `DrawPrimitive` per material batch [i]) | Not separable at the draw: **on Mayhem 3 every Particles3 row but IPG (407) uses material 406**, ship rows 1–41 and 58–60 together with MAML/PSP/FBL/… weapon rows 42–57 [m, `particles3_materials_out.txt`]; stock ship row 1 uses 1198 alone, row 14 (6 ships) shares 395/406 with weapons [m] | Call redirect B: the `0x00412d70(k, trail, obj, &pos)` call inside `0x00414590` (reached unless `C & 0x2000`, and only with the engine-flag bit `+0xfc & 0x40000000`) [m: decompiler; bytes **[u]**]. The stub returns without a generator, so ship trails never enter the particle pool; weapon/missile trails, drawn by the same renderer from other spawn sites, are untouched | Both |

What breaks if a class is left native: glow native + plume = a double plume at the nozzle (the native mesh is
2 × value long at full speed, the same length law) and the 7.5 µs per draw are not saved; sprite native = a grey
disc at the nozzle under the plume and its red streaked halo (Mayhem) at 0.3 gain; trail native = Mayhem's
40–120 unit grey puffs beside the ribbon (lifetime 1.5 s, the heaviest of the four on screen). The three must go
together; the ribbon is armed only when redirect B is installed (fail closed), the plume only when redirect A is.

**RCS jets** (`v/00566`, 2,583 parts, mode words without bit 0, +1.0 z per steering axis [m]) carry the same flag
family (SMALLJET `0x4000000` or JET [u: which list]) and are recognised by the same rule; phase 2 draws them as
short puffs from the same record path (their z runs 0.01 → 1.0 on steering), so the user sees steering feedback.

## 2. Record extraction (requirement 2)

Per recognised draw, from the Snapshot (no device call) and the constant shadow:

| Field | Source | Note |
| --- | --- | --- |
| Nozzle origin, axis, base size | The world rows c4–6 of the draw, captured by extending the `SetVertexShaderConstantF` shadow (`set_vertex_constants_f` already copies clip-row windows [m, `motion_output.cpp:4977`]) with a 3-row window while the effect pair is bound: origin = translation, axis = −(model-z row), base size `value` = `|x row|` (`0x004bdee0`: rows = basis × `+0x70·scale/65536` × context scale [m]) | Zero reads per draw; the c4–6 register order (**[u]**) is resolved by the Snapshot cross-check below and pinned by the first F8 |
| Throttle `s` | `z = scale[3]/65536` from the Snapshot (`+0x88`), `s = clamp((z − 0.25)/1.75)` for a main jet (`C = 0x7001`, 99.4 % of installed parts) [m: the drive at `0x0045ad8b..0x0045b03c`]; the same number is the z/x row-length ratio of c4–6, which also identifies the z row when `z ≠ 1` | Rate-limited by the engine (0.004/ms, 437 ms for 0.25 → 2.0) [m]; steering/brake bits push `z` above 2.0 on flagged jets (up to 9.0): `s` saturates, the extra length is kept as a brake flare |
| Tint, size class, extent | `model` (body id) → body name through the engine body table (the `lens_flare_cull` resolver: names → ids at begin_frame, 210 µs restart over 13,200 slots, 0.134 µs per frame for 5 mappings [m]) → a shipped table `engine_bodies.json` generated offline by a new `tools/effects/engine_bodies.py` from the census code: per body the LOD-0 value, visible z extent (−z only / both ways), x/y half-width, alpha-weighted colour and cluster | Mayhem: 80 `engine.fx` bodies, 11 colours (darkblue #5858f7 … lime #19a319), tiers tiny 504 … huge3 211,762 [m]. Stock: 110 of 138 bodies share the animated blue family (#4ba0bf cyan), 28 white/grey [m]: the table gives cyan/white, which **is** the stock record (TShips carries no colour; col 11 only selects the sprite/flare row). A per-race override on stock needs the ship, which the jet node does not reach (phase 4) |
| Identity | `object_lifetime` node serial (handle + lifetime epoch), else the spatial identity (key + origin quantised to 8 units) as the dropped stage did | Ring buffers and fades hang on it |
| Fallback | A JET-flagged draw with no scope (depth 0) or an unknown model → forwarded natively, counted (`forwarded_unscoped`, `unknown_body`); never suppressed without a record | Phase 3 may add the removed texture-key registry as a second key if the census shows unscoped glow draws |

**Stock capital ships without glow parts** (112 of the stock rows; their engine look is the col-11 sprite +
`EEDF_LIGHT`) produce no glow draw and therefore no record. Phase 1–3: redirect A's stub keeps creation for an
`eff` allowlist (stock rows 3, 9–14; empty on Mayhem), so those ships keep their native sprite. Phase 4 option:
the same stub records `(obj, k, pos)` before skipping — an anchor per emitter with `obj` → speed `+0x10`,
`vmax` from TShips and the race column — which also gives per-race tint on stock; it needs the stub to write a
per-frame ring and the proxy to transform `pos` (semantics [u]).

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
| Ribbon | `0.6 · tint · (1 − u)^2 · w(u)` along the strip (`u` 0 at the nozzle), width `0.5 · value · (1 − u)` | length `T_tier · v · s`, `T` 0.5 s (tiny/nor: fighters), 0.8 s (nor2–big: TS/TP/TM/M6/M8), 1.2 s (big2 and up: capitals), capped at 60 · value; full speed = full length (requirement 2.1) |
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
  `T_tier · v · s` is evaluated from the positions themselves (speed = displacement per frame from the records),
  and a strip whose first segment exceeds 8 · value in one frame (a jump, a cut, a load) is cleared.
- **Cuts, docking, death, eviction:** a record is a per-frame fact; a plume is drawn only for a record this frame.
  The ring buffer is keyed by node serial in an open-addressed map (512 entries, O(visible nozzles)); an entry not
  seen this frame fades its ribbon over 0.3 s at its last positions, then is evicted; the resolve's cut verdict
  (`cut_finished_ && counters_.cut`), a Reset, a sector change or load epoch (serial epoch) clears the map. A
  docked ship stops being drawn, so its records stop; a destroyed ship's jets spawn no debris [m] and its node
  dies (serial retired by `object_lifetime`).

## 6. Risks, verdict, fixtures, phases

**Verdict: go.** The three biggest risks:

1. **The two call redirects in `0x00414590`** (bytes, live registers, the `&pos` argument, whether any reader of
   the effect-instance list `0x0057b0f8` or of the trail-generator list assumes presence). Mitigation: phase 0
   disassembly before any code; hook fixture with synthetic sites under hostile LastError/MXCSR/x87 (the
   sun-occlusion fixture's shape); the "not created" state is one the game runs every frame for `0x7001` parts.
   Fallback if a site is not patchable: the data route (option D below).
2. **TAA on thin features**: distant plumes and ribbons under 3 px, ghost trails over real starfields; only a flight
   measures them. Mitigation: the pixel clamps, cores above E, the ribbon overlapping itself; the resolve fixture
   runs the plume over the dark and the flickering sky at 0/4/8 px per frame.
3. **Taste**: the previous effects stage was dropped after one flight. Mitigation: phase 1 stands alone as a clean
   "no engine effects" state the user judges immediately; phases 2–3 ship three presets (restrained / default /
   strong, scaling `I_core`, `I_halo`, `T_tier`) cycled by one Ctrl+Alt key during the flight, every number in
   `x3m.ini`.

**Fixtures.**

- Host (deterministic, no device): the record parser (synthetic Snapshot + c4–6 with known `z`, both register
  orders, steering bits, the `value`/axis recovery within 1e-5); the ring buffer (distance sampling, SETA stretch,
  gap/cut clear, eviction and fade, map capacity); the body-table parser (fail closed on a malformed table); the
  two stubs' byte layout and register preservation (the sun-occlusion hook fixture's shape).
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
| 0 | `disassemble`: the two call sites in `0x00414590` (bytes, ABI, `&pos`, readers of the two lists), v/00566's list, which option sets the engine-flag bit; `implement`: `tools/effects/engine_bodies.py` + `engine_bodies.json` + host tests, the body-name resolver generalised from `lens_flare_cull_core.h` | 2 |
| 1 | `implement-deep`: recogniser, record, suppression, the c4–6 shadow window, census rows; `implement-deep`: the two redirects with the hook fixture and the `eff` allowlist; `implement`: options (`engine_effects=native|off|plumes`, default native until flown), launcher, ledger; one review; flight A (suppression only, F8 set 1–6) | 3 + review |
| 2 | `implement-deep`: the stage pass, plume programs, GPU fixture, timing; `implement`: presets, hotkey, `x3m.ini` keys; review; flight B | 2 + review |
| 3 | `implement-deep`: ribbons (ring buffer core, program, fixture rows), fog law, SETA/cut rules; `implement`: RCS puffs, docs; review; flight C | 2 + review |
| 4 (optional, on evidence) | emitter-site anchor records (stock capitals, per-race stock tint); engine-side JET cull for the engine's per-draw time; texture-key fallback for unscoped draws; reactive mark | 1–2 each |

**Native Windows.** Documented D3D9 only: dynamic VBs, `DrawIndexedPrimitive`, vs_3_0/ps_3_0, `tex2Dlod` on the
lane, FP16 post-pixel-shader blending behind `CheckDeviceFormat` (the motes' query), no VTF, instancing, point
sprites or MRT in the stage. The EXE seams (the object scope at `0x004c5228`, the two new call sites) are the same
bytes on Windows and are validated at exact sites. Unverified natively like the rest of the proxy; add the row to
[platform-portability.md](platform-portability.md) when phase 1 lands.

## Unknown, and what settles it

| # | Unknown | Settles it |
| ---: | --- | --- |
| 1 | Exact bytes and live registers of the two calls in `0x00414590`; the `&pos` argument's frame; readers of the effect-instance list `0x0057b0f8` and of the generator list | Phase 0 disassembly |
| 2 | The c4–6 register order of the glow draw (needed only for the cross-check and the census) | F8 1–2; the Snapshot's `scale[3]` carries `z` regardless |
| 3 | Whether every JET-flagged draw has an object scope (by construction yes: the jet takes the ordinary path [m]) | Census `scoped=` |
| 4 | Whether `v/00566` is JET or SMALLJET, and the SMALLJET flag's `+0x1d8 = 5` effect on the small-parts cull | One `types/Bodies` read; `lod-selection.md` |
| 5 | Interaction with the x3m small-parts cull (launcher default 4 px): a culled jet gives no record, so a distant plume disappears with the native glow; exempting JET nodes costs one flag test in the stub | Cull census of model ids at a station view; decide in phase 1 |
| 6 | Trails of bright plumes over real starfields; the meter's response to many cores | Flight B/C |
| 7 | The body-unit to world-unit factor (hull lengths are in LOD-0 units); every law above is relative to `value`, so nothing depends on it | The F8's `object_bounds` against a known ship |
| 8 | Native Windows behaviour | Not verifiable by the user |

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
