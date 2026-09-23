# Bolt footprint: a minimum on-screen size for weapon bolts in third person

Design note, 2026-09-23. Decides how the bullet draws admitted by the
screen-emission-additive route get a minimum on-screen footprint in third
person without changing the first-person look. No code exists yet.

**Outcome.** Expand each bolt instance on the CPU, in screen space about its own
projected centroid, to a minimum half-extent of R ≈ 3 px along both principal
axes, faded out between R and a gate G ≈ 8 px so bolts longer than 2G px on
screen are untouched byte for byte; write the expanded drawn prefix into a
proxy-owned dynamic vertex buffer bound at stream 0 for the admitted draw only,
under the game's own VS `5e484a06672e28fb`, PS variant, blend state and depth
test (option A′ below). Estimated ≈ 0.2 ms CPU per firing frame, no GPU work,
documented D3D9 only. The same fix also lifts the bolts out of the TAA
resolve's attenuation, which is the second, so far unmeasured, half of their
invisibility.

## 1. Evidence

Measured unless marked (paths under `verification/results/`):

| Fact | Value | Source |
| --- | --- | --- |
| Bolt draws in third person (Run 271, fire held, `--cull-small-parts 0`, 1080p, +1.10 EV) | bullet pair VS `5e484a06` / PS `ec1f5c4a`, 2 draws per frame, 792–840 primitives each, admitted 8/8 frames | `screen-emission.md` "Run 271" |
| Bolt pixels in the window ahead of the ship (frames 2116–2123, luminance > 0.5, 200×200 px) | 31–36 components, median area 1–2 px, max extent 4 px, peak luminance p50 2.0–2.4, max 4.6–5.6 (pre-resolve FP16 scene) | `run271-music-keep/bolt_center_out.txt` |
| Frame-to-frame persistence of those components | median nearest-centroid displacement 3.8–4.6 px per frame; 7–10 of 31–36 within 1 px (static stars or hull lights, inferred) | `run271-music-keep/bolt_motion_out.txt` (`bolt_motion.py`) |
| Same draws in first person (Run 270) | 144–168 primitives per draw, 8/8 frames, visible; one of the two draws refused every frame (reason `state`) | `screen-emission.md` "Run 270" |
| Stock bullet bodies `objects/effects/weapons/bullet_*.pbb` (20, 01.cat) | 8–78 faces each (24–234 vertices per instance); no 2-face quad exists | `run271-music-keep/bullet_bodies_out.txt` (`bullet_bodies.py`) |
| Primitive counts as body multiples | 168 = 28 × 6, 840 = 28 × 30, 144 = 24 × 6, 792 = 24 × 33 (inferred: two weapon bodies of 24 and 28 faces, 6 bolts in flight per gun group in first person, 30–33 with fire held in third) | arithmetic on the two rows above |

What the draw is (reverse-engineering, `effects-engine-remaining-emission.md`
"Bullet vertex buffer writer"): the game transforms each instance's model
points to **world space on the CPU** (`FUN_004bf960`, per-instance matrix),
appends them to a system-memory array, copies `count × 24` bytes into a
dynamic write-only vertex buffer under one whole-buffer `D3DLOCK_DISCARD`
lock, and draws `DrawPrimitive(TRIANGLELIST, 0, count/3)` with declaration
POSITION FLOAT3 @0, TEXCOORD FLOAT2 @12, D3DCOLOR @20 (RGB 0, alpha per
object). The VS is view-projection only (c0–3, 7 slots, vs_1_1); the PS is
one texture sample × COLOR0.w (ps_1_1). The additive route
(`MotionOutput::prepare_screen_additive`) keeps the VS, binds the gained PS2
variant and DESTBLEND ONE and draws **in place into the FP16 scene before the
TAA resolve**; it needs no rectangle. The locked-prefix machinery (step D,
`locked_prefix_core.h`) already copies the written **positions** of that
buffer at Unlock into pooled storage and projects them at the draw, but only
under `X3M_SCREEN_EMISSION_BOUND=1` or the packed route, not for the additive
route; measured 10–41 µs per scan and ≈ 17 µs per lock for the sentinel
(`screen-emission-bullet-bound.md`). The note's "6 vertices per instance" was
read from one 147 456-byte buffer (1024 × 6 × 24); no stock bullet body has 6
vertices, so vertices per instance is body-dependent (see §6).

Why the bolts vanish, two parts:

1. **Geometry.** A bolt body is a few metres of crossed cards along the flight
   axis. The chase camera looks along that axis from 1.05× boom behind the
   ship, so a bolt hundreds of metres ahead projects to its cross-section: the
   measured 1–2 px. In first person the same bolts start beside the camera and
   are tens of pixels long.
2. **TAA resolve (inferred from `src/temporal/resolve.hlsl`, unmeasured).**
   Bolts carry no motion of their own (the bullet VS is not a motion program;
   the motion buffer under a bolt is the background's), and they move a median
   4 px per frame, so every bolt pixel is "new" each frame. The resolve clamps
   the reprojected history to `[low, high]` of the current 3×3 (mean ± 1.25 σ,
   intersected with the min/max box) and blends `0.9 × history + 0.1 ×
   current`. For a 1-px dot on dark space `low ≈ 0`, the dark history passes
   the clamp unchanged and the dot reaches the screen at ≈ 0.1 × its scene
   value: the 2–5 luminance measured pre-resolve becomes ≈ 0.2–0.5 after it.
   For a pixel whose whole 3×3 is lit, `low` rises to the neighbourhood minimum
   and the history is pulled up to it, so the interior of a blob ≥ 3 px in
   both axes survives at ≈ its local minimum and only the 1-px rim is
   attenuated. A footprint of ≈ 7 px therefore gives a 5×5 interior at near
   full value. Ghosting is not a concern: at the bolt's previous position the
   history is clamped into a dark 3×3 and disappears within one frame, in
   third and first person alike.

The run271 capture has no `taa` readback (`X3M_TAA_DEBUG=<n>` was off), so
part 2 is inferred from the program; §7 says how to measure it.

## 2. Recommended: option A′, CPU screen-space expansion on a substitute vertex buffer

Per admitted bullet draw (exact pair, additive route admitted, prefix record
valid for `[0, primCount·3)`), on the CPU, before the draw is forwarded:

1. **Instances.** Group the drawn prefix into instances of period p vertices.
   The UV stream repeats exactly per instance (the same model UVs scaled by the
   same constant); p is the smallest multiple of 3, ≥ 24, dividing the drawn
   vertex count with `uv[i] == uv[i mod p]` for every i (exact word
   equality). Cost: a few candidate divisors, each a single pass over the UVs
   until the first mismatch. No period (per-object UV remap at
   `object[+0x1ac]`, a mixed record) → the draw stays native-sized
   (fail closed, counted `refused_period`).
2. **Projection.** For each instance project its p vertices through the
   shadowed c0–3 rows (the ones step D's `project_prefix` uses) and the
   shadowed viewport to pixel coordinates q_i; any w ≤ ε → the instance is
   untouched (it straddles or lies behind the camera plane, where it is large
   or invisible anyway). Centroid q_c, 2×2 covariance, principal axes e1
   (major), e2 (minor); half-extents A = max|(q_i − q_c)·e1|,
   B = max|(q_i − q_c)·e2|.
3. **Rule.** With R (minimum half-extent, default 3 px) and G (gate, default
   8 px), t = saturate((A − R)/(G − R)); targets A* = R, B* = R·(1 − t);
   scales s1 = max(1, A*/max(A, 0.05)), s2 = max(1, B*/max(B, 0.05)). When
   s1 = s2 = 1 (A ≥ G, or A ≥ R and B ≥ B*) the instance is **not written**:
   its bytes stay the game's. Otherwise each vertex gets the pixel
   displacement δ_i = (s1 − 1)(q_i − q_c)·e1 e1 + (s2 − 1)(q_i − q_c)·e2 e2.
   The rule is continuous in A (no pop as a bolt recedes through G) and
   magnifies the bolt's own projected shape, so a receding streak becomes a
   short streak of at least 2R px, not a disc.
4. **World displacement.** Let n = (VP[0][3], VP[1][3], VP[2][3]) (the world
   direction of clip w) and (r, u) an orthonormal basis of the plane ⊥ n; both
   are per-draw constants, as are a_r = (VP·(r,0)).xy and a_u = (VP·(u,0)).xy.
   Solve the 2×2 system α a_r + β a_u = δ_i · w_i · (2/viewport) per vertex
   and add Δ_i = α r + β u to the world position. Because r, u ⊥ n, clip w and
   clip z are unchanged (both depend on view-space z only), so the game's own
   VS lands the vertex at q_i + δ_i (to float rounding) with the same depth:
   the depth test, the PS, the UVs and the colour are exactly the original's.
5. **Substitute buffer.** One proxy-owned `D3DUSAGE_DYNAMIC | WRITEONLY`,
   `D3DPOOL_DEFAULT` vertex buffer of 147 456 bytes (the game's size; recreated
   after Reset), DISCARD-locked per admitted draw: the scanned prefix copied
   whole (24-byte vertices, which means step D's storage keeps the full vertex
   instead of 12 bytes, same cache-line traffic at Unlock) with the expanded
   positions patched in, Unlock, `SetStreamSource(0, proxy, 0, 24)` for the
   draw, the game's stream 0 restored through the route's existing restore
   bindings (as the fade route restores its augmented VS). Any failure on the
   way unwinds and leaves the draw as it is today (additive, native size).
6. **Option and telemetry.** `--bolt-footprint R[,G]` (`X3M_BOLT_FOOTPRINT`),
   implies `--screen-emission-additive` and the locked-prefix scan for the
   bullet buffers; per Present `bolt_footprint_frame draws= expanded=
   untouched= refused_period= refused_w= refused_buffer= us=`, and on a
   capture frame one `bolt_footprint_draw` line per draw with p, instance
   count, A/B before and after for the smallest and largest instance.

**Cost per frame (inferred, FEX numbers from the step-D ledger).** Enabling
the scan for the additive route: ≈ 17 µs sentinel + 10–41 µs scan per lock,
2 locks per firing frame. Expansion: projection of ≈ 2 400 vertices (step D
measured 27 µs per-triangle projection at 3 630 vertices), covariance and
displacement ≈ twice that, plus a 58 KB copy into the proxy buffer and five
state calls: ≈ 60–100 µs per draw, ≈ 0.2 ms per firing frame at 30 bolts,
zero GPU. Frames without an admitted bullet draw pay one bool test. Native
Windows: the Unlock-window read of write-combined memory is unmeasured (the
same caveat step D carries); §7 names the alternative source.

**Native Windows behaviour.** Documented D3D9 only: the ownership layer's own
Lock/Unlock observation, `CreateVertexBuffer`, `Lock(DISCARD)`,
`SetStreamSource`; no engine hook. The proxy's substitute buffer is drawn by
the game's shaders, so a driver that renders the original renders this.

**First-person invariance.** The player's own bolts project to tens of pixels
(A ≫ G) and are not written. Distant bolts of other ships are enlarged in
both camera modes by design; the control flight quantifies that they are the
only change (`expanded=` counts only far instances; the player's bolt pixels
are identical).

**TAA interaction.** See §1 part 2: a 7-px blob survives the resolve at its
interior minimum instead of 0.1×; no trail, because the dark neighbourhood
clamp erases the history at the previous position in one frame. The bolt draw
is unjittered today and stays so.

**Bloom.** Unchanged (`bloom_source_clamp` 1.0, gain 2). A larger source
feeds bloom's half-res extract with a real texel instead of a fraction of one,
so the halo appears without any clamp exception.

## 3. Alternatives considered

- **A (vertex-program rewrite).** A vs_1_1 program sees one vertex; the
  centroid and axes it would expand about must come from a CPU pass per vertex
  (an extra 36-byte stream or a rewritten stride), and the shader would need the
  viewport as a constant. The proxy's VS machinery (`rigid_replay_program`,
  the fade route's augmented VS) synthesises SM3 row programs for reviewed
  profiles; it has no vs_1_1 augmentation. Strictly more work than A′ with the
  same data dependency and no benefit; loses.
- **B (post-draw dilation in the additive route).** Redirect the two draws to
  a full-res FP16 mask (clear ≈ 0.37 ms measured at 1080p for the M plane),
  then a 3–5 px max-filter added into the scene over the bolt rectangle, which
  for the player's batches is 58–90 % of the viewport (run 15): ≈ 0.8–1.4 ms
  per firing frame at 1080p inferred from the bracket table in
  `linear-emission-cost.md`, plus 16 MB. A max filter dilates first-person
  bolts too, and no pixel-local "smallness" test tells a big bolt's thin tail
  from a small bolt. Loses on cost and on the first-person invariant.
- **C (point sprites / fixed-pixel billboards).** Needs the same instance
  grouping for centroids; replaces the streak by a disc with a pop at the gate;
  `D3DRS_POINTSPRITEENABLE` is a legacy path with `MaxPointSize` caps and
  uncertain D3DMetal/DXMT support; a CPU billboard is A′ with a worse shape
  rule. Loses.
- **D (per-effect gain + bloom clamp exception).** The numbers do not say
  otherwise: a 1–2 px new-every-frame dot reaches the display at ≈ 0.1× after
  the resolve (inferred), so a gain of 8–10 would be needed, it applies to the
  pair, hence to first-person bolts (4–5× over-bright), and bloom's extract sees
  under one texel. Loses.
- **E (redraw the admitted bolts after the resolve).** Removes the resolve
  attenuation entirely but needs a deferred replay of the game's draw (binding
  capture, depth test against the scene depth, the game's buffer valid until
  its next DISCARD lock): a new lifetime contract. Not chosen now; the
  follow-up if the §7 measurement shows a 7-px blob still below half its
  scene value after the resolve.

## 4. Verification

1. **Host oracle** (header-only core beside `locked_prefix_core.h`, Python
   restatement in `verification/analysis/test_fade_region.py`'s style):
   random bodies of 24–234 vertices, random VP and viewport; asserts
   untouched instances byte-identical; expanded instances' projected A, B ≥
   their targets within 1e-3 px; continuity across G; w ≤ ε untouched; UV
   period found for 24/28/78-face bodies and refused for a remapped stream;
   depth (clip z/w) unchanged to 1 ulp.
2. **Detached fixture under Wine** (the locked-prefix live fixture, the game
   bullet bytecode `vs_5e484a06672e28fb.bin` / `ps_ec1f5c4a2f4e1445.bin`):
   synthetic 28-face instances placed to project at 1, 3, 8 and 20 px;
   readback components: the 1- and 3-px ones ≥ 2R px in both axes, the 20-px
   one pixel-identical to the run without the option; counters `expanded=2
   untouched=1`, refusal cases (odd count, remapped UV, w ≤ 0) native;
   `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3
   verification/probe/run_locked_prefix_live.py ...` with a `--bolt-footprint`
   case; x87 audit on the DLL.
3. **Flights.** Third person, fire held, one F8 burst: `bolt_center.py` on the
   same window → median component area from 1–2 px to ≥ 20 px, max extent ≥ 6
   px, peak luminance not lower; `bolt_footprint_draw` lines show A/B after ≥ R.
   First person, same burst, the no-change control: the player's bolt
   components equal in area and peak to a run without the option (or to Run
   270), `expanded=` only for instances with A < G. With `X3M_TAA_DEBUG=1` on
   the burst, the `taa` readback measures the post-resolve value of the bolt
   pixels against the `hdr` readback.
4. **Cost.** `frame_end dt_ms` medians of the firing window with and without
   the option, ± 0.2 ms; the `us=` field of `bolt_footprint_frame`.

## 5. Unknown, and what settles it

- **Vertices per instance.** The UV-period rule is inferred from the writer's
  per-instance model copy; a per-object UV remap would break it (fail closed).
  Exact alternative: the part batch's `+0x10` (vertices per instance) at the
  pre-draw hook site `0x004c0074` (EBX = VB record, `EBP` effect, `ECX`/`ESI`
  live): a `disassemble` brief on whether the part batch is reachable from a
  live register or the record there. Not needed for the first build.
- **Post-resolve bolt value** (the 0.1× and the 7-px survival): one
  third-person burst with `X3M_TAA_DEBUG=1`, `hdr` versus `taa` readback at
  the bolt components, before and after the option.
- **Native write-combined read at Unlock.** Unmeasured, shared with step D.
  If it matters, the same hook site gives the cached system copy
  (`[rec+0x04]`, `[rec+0x0c]` count) without touching the mapping.
- **Which bullet pairs.** Only `5e484a06`/`ec1f5c4a` has live evidence; the
  other two bullet VS of `screen_emission_admission.h` share the body class
  and the rule applies to them unchanged if they ever draw.
