# Station source-over draws in linear light (docking port)

Design note, 2026-09-14, for ratification; owns the decision for source-over draws of
already-converted standard-lighting pairs. [linear-distance-fade.md](linear-distance-fade.md)
owns the fade route, [linear-distance-fade-region.md](linear-distance-fade-region.md) the
in-place bracket and bound, [station-material-distance.md](../reverse-engineering/station-material-distance.md)
the native producer. Not implemented; no Wine, build or game was run.

## Decision

Ratified by the main session 2026-09-14 with one amendment: the pixel proof of
section 4 does not gate implementation. The refused-draw rectangle logging is built
together with the route so one user run provides both the proof and the appearance
verdict; until that run, the change is a consistency fix, not a demonstrated repair.

Admit the source-over draws of pair `4944d81dfe531b37/64bac8bb307eb896` to the **existing
distance-fade composition route** — same B/E/C/M pool, source-over accumulation and in-place
region bracket (policy 4, exchange policy 2 as fallback) — by adding the pair to the fade
admission set. No new policy, no appearance matching. The route is pair-agnostic: the
composite program and the pass never inspect the pair; the pair-specific pieces are the dual
VS/PS producer (`transform(..., fade=true)`, `linear_material.cpp:1011`) and the admission
mask (`linear_distance_fade_sampler_mask`, `:1298`). The producer refuses non-Asteroid rows
only by the explicit guard at `:1021` (`asteroid_layout`); the dual replay itself is generic:
it replays the untouched native body and duplicates every alpha-masked `oC0` write into `oC1`
(`:1178`), which is exactly what the standard-lighting PS needs.

## What the evidence establishes

Run 36 frames 1476/1699/1917/13681/14601: `motion_route gate=4 routed=0` for this pair in
2/2/3/2/1 draws and `routed=1` in **0** draws — the pair is used only source-over here; the
opaque siblings on the same models use other converted pairs. Draw 13681/44 has
`object_context scoped=1 scope_depth=1 model=5436 lod=2` (the seam scope the bound needs) and
`motion_input vb=4241 ib=4240 position_type=16 width=1280 height=768`. That the native-gamma
port beside linear-lit siblings is the *visible* darkening is **not pixel-proven** (section 4);
whether a composed port still darkens with distance is a separate question (native alpha stays).

## 1. Route choice

- **Existing fade route (recommended).** Admission at `motion_output.cpp:2797` already
  requires the captured state: ZENABLE, Z-write off, alpha-test off, blend on, mask 7, sRGB
  write off, SRCALPHA/INVSRCALPHA/ADD; cost, failure ladder, Reset, witness and TAA mask rules
  are qualified. Changes: a per-row `fade_eligible` flag replacing the guard at `:1021`; the
  pair in the mask function with `0x1f` (hull BUMP samples s0–s4); the sampler-sRGB refusal
  loop at `:2823` (`stage < 4`) widened to the mask, since stage 4 is otherwise unchecked;
  required-producer bit stays `2`. The other BUMPMAP/LOW PS and the toggle VS are not admitted
  until captured in fade state.
- **Separate blended policy.** Same blend, mask, alpha contract, pool and bracket; a policy
  bit would duplicate the bracket and its fixture ladder for no semantic gain.
- **Leave native, match appearance.** Needs a second, gamma-space lighting model with no native
  reference to verify against.

## 2. Source alpha and the dual PS output

Native: `a = interp(AlphaValue * (b0 ? sat(c41.x − c41.y·d) : 1)) * lrp(EnableGlow, Diffuse.a,
LightMap.a)`, all `_pp`, one alpha write. With `b0 = 0` and `alpha13c = 0` the vertex factor is
`c39.x` (its captured value and `c3.x` EnableGlow are unread; the run-27 constants reduction
settles both) and the texture alpha drives `a`. Pool outputs: A/B the untouched native `oC0`
(RGB source-over, alpha unwritten under mask 7); E `(L, a)` with `L` the standard BUMPMAP linear
RGB sanitized at `final_rgb` (fade branch, `:1164`) and `a` the byte-exact duplicate of the
native `mul_pp oC0.w`; M positive coverage. `a` is transmittance, not color, and is not
converted. Parity: the composed pixel is `encode(a·L + (1−a)·decode(A))`, native source-over
evaluated in linear light; the oracle must show E.a equals the native blend factor bit for bit,
as for the six Asteroid pairs.

## 3. Bound

Station parts are child nodes drawn through the same `0x004c403c` path under the `0x004c5228`
seam scope (`scoped=1` above) with the part back-link and subset records the step-1 table
checks; `derive_fade_region` applies unchanged (rows `c24–c27` via `matrix_register`, jitter,
`2^-10` half expansion). Untested: the `status=bound` hit rate for station VBs (many parts per
station; 1024 slots, 32-slot windows, eviction counted); `--fade-witness` remains the detector.

## 4. Fixtures

- Host: `linear_distance_fade_pixel_variant` on the local 1,392-DWORD original must return
  `Applied` with the slot/temporary count logged (feasibility gate: Asteroid duals peak at 152
  weighted slots; this body is several times larger), one duplicated alpha write, native span
  byte-equal; VS variant `Applied` for `4944d81dfe531b37`; profile tests gain the row.
- Detached `run_linear_distance_fade.py`: a seventh producer with six input variants (AlphaValue
  0/0.625/1, EnableGlow 0/1/0.375, asymmetric diffuse/lightmap alpha, fog off/on, jitter,
  mips), the exact blend state, Z-test on / Z-write off over a depth occluder, overlapping
  primitives in one DIP; oracle `linear_material_reference.py` (standard BUMPMAP) for `L`,
  RGBA32F twins for `a`, existing Q/q/C checks; in-place twin bit-exact with the exchange policy.
- Live `run_linear_distance_fade_live.py`: the pair as a fade source drawn after an opaque
  converted sibling with the same rows, before an opaque draw that overwrites part of it
  (Z-write on), and as the frame's last draw; exact destination alpha, source-once, mixed
  fade/emission order, `refusal[]` histogram, paired windows at 1/4/16 DIPs.
- Pixel proof of the split, before implementing: a capture-only diagnostic that runs the step-1
  rectangle for *refused* recognised source-over draws and logs `rect` (no composition). Decode
  that rectangle from `hdr_1_13681.rgba16f` / `hdr_1_14601.rgba16f` (1280×768 RGBA16F, 8 B/px)
  and a routed sibling's rectangle on the same model; compare decoded gamma-2.2 luminance of the
  port pixels with the sibling's under the same lights. Acceptance is a decoded ratio outside
  FP16/texture noise. Without it the change is a consistency fix, not a demonstrated repair.

## 5. Risks and cost

- Hot path: no new per-draw work; admitted draws pay the in-place bracket, 0.10–0.16 ms fixed
  (65 getters, 285 setters) plus `48f` B/px at tiny `f`. At 1–3 draws per frame against the
  run-11 4.1 ms median frame that is 2–10 % of frame time: the fixed bracket cost, already the
  named next optimisation target, now outweighs region traffic.
- Ordering/depth: the bracket composes over whatever A holds at the draw under the game's depth
  test, so opaque draws before or after keep native semantics; overlapping rectangles back up
  the already composed A. Z-write off leaves RT2 untouched: port pixels carry the underlying
  depth and M rejects their history (current-only TAA, possible edge aliasing) — the asteroid
  contract, not a regression of native.
- Population: a fade-state draw of this pair failing refusals 1–5 stops the frame's history
  (required producer); run 36 shows 1–3 per frame, other scenes are unmeasured (live counters).
- Program budget: a dual PS over the slot/temporary limit yields `ResourceLimit`, no variant,
  refusal 2 on every such draw. Fail-closed fallback: keep the pair out of the mask; the host
  test decides before any build.
- Unquoted states: ZENABLE, DITHERENABLE, SEPARATEALPHABLENDENABLE (admission needs them known;
  the pass refuses dither and separate alpha). The existing capture state rows answer this.

## 6. Disassembly before implementation

Bounded `disassemble` task: at `0x004c2d01..0x004c2d3c` and the cached parameter-block restore
before `0x004c300e`, determine where the blend state of a draw with `b0 = 0`, `alpha13c = 0`
and descriptor `+1a4 != 0` comes from, and read `+1a4` for the port's descriptor
(`object_context mesh=0f9c01c0`, frame 13681/44). Outcome: fixed material property (admission
set is the pair alone) or per-draw override that can also present the pair opaque or fogged
(then the fade-state check selects and the `b0 = 1` variant joins the fixture). Not blocking.

Unknowns, each settled by the item named above and none by a game launch: `c39.x`/`c3.x` for
the port draws; dual-PS slot count; station bound hit rate; ZENABLE/dither/separate-alpha rows;
whether the composed port stops darkening (native alpha is unchanged, so it may not).
