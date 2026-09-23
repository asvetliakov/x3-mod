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

**Superseded in part (Run 73 B, last section).** In run273 the R/G rule
expanded 22 % of the chase-view instances of the second flight (664 of 3,084)
and 16 % of the first-person ones (1,181 of 7,368); the built rule is now a visibility rule (full width
below W = 3 px widened to W, full length below L = 12 px lengthened to L along
the projected flight axis), written in the chase view only, with
`--bolt-footprint W[,L]`.

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

## Implementation (2026-09-23, option A′ as built)

The rule, the option syntax and the telemetry fields below are the first
build's; Run 73 B (last section) replaces them. Grouping, the substitute
buffer, the camera-plane displacement and the refusals are unchanged.

Code: `src/proxy/bolt_footprint_core.h` (the rule, header-only, no Windows/D3D,
single-precision SSE only), `MotionOutput::prepare_bolt_footprint` /
`finish_bolt_footprint` / `ensure_bolt_buffer` in `src/proxy/motion_output.cpp`,
the `X3M_BOLT_FOOTPRINT` gate in `src/proxy/capture.cpp`, the scan enable in
`src/proxy/loader.cpp`, `--bolt-footprint` in `tools/manage.py`. Ledger:
`docs/verification/bolt-footprint.md`.

**What is read.** The locked system copy the step-D machinery keeps
(`locked_prefix_core.h`), not the pre-draw hook's cached copy: the Table's
Unlock scan already touches every written vertex, so keeping the other 12 bytes
(TEXCOORD FLOAT2 at 12, D3DCOLOR at 20; `extras`, 3 words per vertex, 72 KB
more per slot, allocated at mark) costs one extra `memcpy` per vertex on the
Unlock path and nothing at the draw, and the copy comes with the revision
recheck and the sentinel-exact count the draw side already relies on. The
hook-site copy (`[rec+0x04]`, §5) would add an engine-layout dependency for
the same bytes. The option enables the scan itself (`loader.cpp`:
`locked_prefix_enabled` when `X3M_OWNERSHIP=1` and
`bolt_footprint_requested_gate()`), without `X3M_SCREEN_EMISSION_BOUND`, so
`derive_prefix_region` (the hull projection) does not run for it; the footprint
does its own lookup (`fade_region::locked_prefix_vertices`, marks the buffer)
and `recheck_locked_prefix` after the copy.

**Where.** At the end of `prepare_screen_additive`, after the additive
admission (DESTBLEND ONE, the PS variant, the alpha law applied), one bool
test, then the bullet-producer gate `screen_emission::admitted_vertex_shader`
(the guard step D's `derive_prefix_region` opens with: six of the nine
additive pairs are other SM1 screen emitters transforming through their own
matrices, never c0–3 world positions, and are left alone before any counter);
`finish_screen_additive` restores the stream first. A frame without an
admitted bullet draw pays the additive route's pair test and one bool per
admitted additive draw; what still runs every frame is the per-Present window
counter and, once a bullet buffer is marked, the Unlock sentinel and scan of
that buffer's DISCARD locks (the step-D cost, now with 24 bytes copied per
written vertex instead of 12).

**Grouping rule as implemented.** `detect_period`: p is the smallest multiple
of 3 in [24, 234] that divides the drawn count with `uv[i] == uv[i − p]`
(exact word equality) for every i ≥ p. 234 is the largest stock body
(`bullet_Repeat`, 78 faces): p = count is a period too (one bolt in flight),
so a remapped stream of several bodies exceeds the cap and is refused, while
one of at most 234 vertices is indistinguishable from a single body and
treated as one instance. Stock bodies whose own UVs repeat (measured,
`verification/results/bolt-footprint/bullet_uv_periods.py`, 20 bodies in the
writer's face-expanded order): PlasmaBeam and Repair (72 = 3 × 24; the thirds
are crossed-card groups whose centroids sit within 0.06 of the body's 1.0
half-extent and share its axis half-extent, so each third gets the verdict
the whole body would: the split is harmless by the numbers) and Repeat
(234 = 3 × 78 collinear segments, centroid offsets 0.64 of a 1.05
half-extent, segment half-extent 0.41: a split would change verdicts in the
16–24 px range). 78 is no stock body length, so a found period that is not a
stock length (`stock_body_lengths`: 24, 36, 54, 66, 72, 84, 90, 108, 234) is
promoted to the smallest stock length it divides that also divides the count
(78 → 234); a non-stock body that shares no such multiple keeps its own
period, and a mod body with a stock-length sub-period would split (inferred
harmless only where its segments share the centroid, as the two stock cases
do). Refused: count < 24, count not a multiple of 3, no candidate divides,
more than 256 instances (impossible under the 6144-vertex scan bound).

**Expansion as implemented** (`prepare_frame`, `plan_instance`,
`write_instance`). Rows = the shadowed c0–3 (window 0), viewport = the
shadowed application viewport. Per draw: n = rows[12..14] (the w row's world
direction); the z row must be parallel to it (|z × n| ≤ 10⁻⁴ |z||n|, else
`not_perspective`, draw refused); (r, u) = an orthonormal basis ⊥ n seeded by
the axis least aligned with n; a_r = (row0·r, row1·r), a_u = (row0·u, row1·u),
det ≠ 0. Per instance: q_i = (X + (cx/w + 1) half_w, Y + (1 − cy/w) half_h),
any w ≤ 10⁻³ refuses the instance; centroid q_c, covariance, major
eigenvector e1, e2 = e1⊥, A = max|d·e1|, B = max|d·e2|;
t = saturate((A − R)/(G − R)), A* = R, B* = R(1 − t),
s1 = max(1, A*/max(A, 0.05)), s2 = max(1, B*/max(B, 0.05)); s1 = s2 = 1 →
untouched (bytes copied verbatim, and when no instance of the draw needs
writing nothing is locked or bound). The 0.05 px floor caps the scale at
R/0.05 = 60, so an instance whose half-extent is below 0.05 px (a bolt seen
exactly end-on at extreme range, or a degenerate one) stays under R; the
measured third-person bolts (1–2 px area, note §1) sit well above the floor.
Else per vertex
δ = (s1 − 1)(d·e1) e1 + (s2 − 1)(d·e2) e2 px, Δclip = (δx w/half_w,
−δy w/half_h), [a_r a_u](α, β)ᵀ = Δclip, p′ = p + α r + β u. Depth: the
host oracle holds clip w and clip z within 2·10⁻⁴ relative per vertex at
sector coordinates (fp32 rounding of p + Δ at |p| ≈ 10⁵ is the residue).
A non-finite result demotes the instance to a verbatim copy.

**Substitute buffer.** `CreateVertexBuffer(147 456, D3DUSAGE_DYNAMIC |
D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT)` once per device epoch at the scan
bound (the game's own buffer size, so every drawable prefix fits and nothing
is regrown on the render thread); one
`Lock(0, count·24, D3DLOCK_DISCARD)` per written draw, the whole prefix
written in vertex order (positions patched, UV/colour words verbatim),
`Unlock`, the revision recheck, then `GetStreamSource(0)` (the application's
buffer must be the shadow's identity at offset 0, stride 24, else refused)
and `SetStreamSource(0, proxy, 0, 24)`; after the draw the owned binding is
put back and released. Released before Reset and at detach, recreated by the
next draw; a creation failure is final until Reset. Any failure on the way
leaves the draw as the additive route left it.

**Telemetry.** `bolt_footprint_mode requested=1 enabled= r= g= valid=
additive= ownership=` at start; `bolt_footprint_buffer bytes= hr=` per
creation; `bolt_footprint_refused reason= detail=` once per reason per device
(shape, rows, viewport, nonfinite, degenerate, not_perspective, buffer with
the prefix lookup reason, instanced, period, recheck, binding); one
`bolt_footprint … draws= written= untouched= instances= expanded=
refused_period= refused_w= refused_buffer= refused_rows= refused_shape=
refused_recheck= failures= locks= timed_draws= us= session_*` line per 300
frames (`us` is QPC time inside `prepare_bolt_footprint` over the
`timed_draws` draws that were timed: every bullet draw with `--telemetry`,
otherwise only those of the window's last frame, so the two QPC calls are
not a per-draw cost on a plain launch; refusals included). The loader's
`screen_emission_bound … source=` field says who enabled the scan
(`bound`, `bolt_footprint_only` or both). A 31+-character
`X3M_BOLT_FOOTPRINT` value is refused with the mode line, never silently
off.

**Cost** (inferred from step D's figures; the harness is a correctness
oracle, nothing is host-timed). Every frame: the per-Present window counter,
and for each marked bullet buffer the Unlock sentinel and scan, which now
reads and copies 24 bytes per written vertex instead of 12 (on native Windows
the mapping is write-combined memory: the uncached read the step-D note
carries as unmeasured is doubled for step C/D too whenever the footprint or
the bound is on). Per admitted bullet draw: the registry lookup (one
`registry_mutex` take, one table probe), `GetStreamSourceFreq`, and the
projection of each vertex in fp32 with one divide; that is the whole cost on
the untouched path (first person: every instance A ≥ G). On the written path
add the second projection, a 24-byte copy per vertex into the mapping and
six documented calls in all: `GetStreamSourceFreq`, `Lock`, `Unlock`,
`GetStreamSource`, `SetStreamSource` for the substitute and `SetStreamSource`
for the restore. The 300-frame `us` field measures the draw side in game.

**Option.** `--bolt-footprint [R[,G]]`: launcher default `3,8` on every modded
launch (forwarded as `X3M_BOLT_FOOTPRINT=3,8`), `--bolt-footprint 0` off,
nothing under `--vanilla` (an explicit value there is refused). The DLL enables
it only with the additive route (`--screen-emission-additive`, i.e.
`--motion-output --hdr`) and `--ownership`; otherwise `bolt_footprint_mode
enabled=0` and nothing runs. An explicit non-zero value implies
`--screen-emission-additive 1` when that option is absent and requires
`--motion-output --hdr --ownership`; the default never implies the additive
route, so a plain launch keeps its native bullet blend (assumption: the
ratified "implies" applies to the option as typed, not to the launcher
default, which would otherwise change every `--hdr` launch's blend law).

## Run 73 B: visibility rule, chase-view gate, telemetry (2026-09-23)

User report on the Run73 DLL (`--bolt-footprint 3,8`): in the chase view the
corvette's bolts are still barely visible small white balls. Evidence under
`verification/results/run273-fog-bolts/` (session log
`/tmp/x3-bottleX3-run273/session-20260923-182033-212.log`, local), measured
unless marked:

| Question | Finding | Script / output |
| --- | --- | --- |
| Bolts on screen in the F8 burst (frames 15495–15502, pre-resolve FP16 scene, blue-dominant, luminance > 0.4) | 2 bolts in flight on each bullet frame (15496, 15498, 15499, 15500), 3–4 × 3–5 px, area 9–14 px, peak 2.8–3.4; none on the frames without a bullet draw. They already carry the 3,8 rule's expansion of draw 90 (round, R = 3): the "small white balls" | `bolt_components.py` / `.txt` |
| What one draw holds | 48 primitives = 144 vertices = 2 instances of a 72-vertex body (the two guns); 72 primitives = 3 instances. The part batch's buffer is 1,769,472 B = 1024 × 72 × 24 (inferred: a 72-vertex part batch; the stock 72-vertex bodies are flamethrower, PlasmaBeam and Repair) | `bullet_pair_state_diff.txt`, `bullet_uv_periods_out.txt` |
| Old rule per view | chase windows 22,312 instances, 10,716 expanded (48 %; second flight 3,084 / 664 = 22 %); first person (chase verdict InternalView throughout, frames 13800–14399) 7,368 instances, 1,181 expanded (16 %) | `bolt_views.py` / `.txt` |
| Native projected length and width per instance | not in the log (no vertex dump): the new `bolt_footprint_hist` row measures them on the next flight. Inferred from the old rule: an untouched thin instance had A ≥ ≈ 8 px half-length (the minor target R(1 − t) only reaches a sub-pixel B near G), so most untouched chase-view instances were long streaks, and the expanded ones sub-3-px dots, the end-on bolts ahead of the ship | rule arithmetic |

**Rule (as built, `bolt_footprint_core.h`).** Minimum full width W
(default 3 px) and full length L (default 12 px). The length axis e_L is the
projected world axis of the body: the major eigenvector of the area-weighted
second moment of the instance's triangles about their area centroid (a
per-vertex covariance is biased by the triangle list's repeated diagonal: 4°
on a 3:1 card body in the host oracle), used when the body is elongated
(λ1 ≥ 2 (λ2 + λ3)), projected as the derivative of the pixel position along
it at the centroid, which is the line to the axis' vanishing point and stays
defined when the bolt is seen end-on. Without a world axis, the major axis of
the 2D shape covariance (also area-weighted, over the projected triangles; a
per-vertex covariance tilted a round crossed-card body by 20°) when the shape
is clearly elongated (extents ratio ≥ 1.5). With no usable axis at all (an
elongated body exactly end-on, derivative below 10⁻³ of the rate of an axis
perpendicular to the view, or a round body with a round shape) the footprint
is a W × W square on the screen axes (`disc`): no lengthening, the same on
every frame (review F2: the 2D major axis is rounding noise there and turned an
end-on bolt into an 8.6 × 8.3 px diagonal streak). A = max |d·e_L|,
B = max |d·e_W| about the pixel centroid; s1 = L / max(2A, 0.1) when 2A < L,
s2 = W / max(2B, 0.1) when 2B < W, each 1 otherwise; a scale whose largest
displacement (s − 1)·extent stays below 0.25 px is dropped, so an axis the
vertices do not span (all on one point, a zero-width line) is never reported
as expanded (review F3); s1 = s2 = 1 leaves the bytes the game's. The
displacement, the camera-plane solve (clip z and w unchanged), the substitute
buffer (147,456 B, the scan bound: every drawable prefix fits, unchanged) and
every refusal are the first build's. An end-on chase-view bolt becomes an
L × W streak on the line to its vanishing point; a thin long bolt is widened
to W and keeps its length; a bolt at or above both minimums is untouched. The
rule is continuous in both extents up to that 0.25 px step.

**First person unchanged: a view gate, not a size argument.** The old
numbers show first-person instances below the minimums (16 % expanded under
the weaker R/G rule; receding first-person bolts converge on the crosshair and
shrink to dots), so the same rule would change them. The footprint writes only
while `chase_camera::pose_applied_since(mark)` holds, a frame-stamped gate
(review F1): the chase handler's last visit of the active control cockpit wrote
its pose (verdict Applied, both writes done) and a pose was written after
`mark`, the write count MotionOutput takes at every Present. So the gate is
closed on a frame without an admitted visit (a target or remote view whose view
object is not the ref object, a load, a menu), after an internal
(first-person), front, side, scripted or refused visit, with `--camera
vanilla` and with an uninstalled site. A closed gate keeps every bullet draw
byte for byte the game's: no plan runs, only the projected bounding box of
each instance feeds the `other` histogram (review F4), the draw is counted
`gated=`, and nothing is created, locked or bound (the gate sits before
`plan_draw` and `ensure_bolt_buffer`). Native Windows:
the gate is the mod's own byte-verified EXE hook state, no Wine dependency; the
footprint therefore needs `--camera chase` (the user's launches carry it).

**Draw 9 (task 3).** Every bullet frame of the burst draws the pair twice
from the same buffer (identity 5797, consecutive DISCARD revisions), same
primitive count, same c0–3 rows hash, same colour blend ONE / INVSRCCOLOR /
ADD, Z test LESSEQUAL, Z write off. Draw 9 comes right after the depth clear
that follows the far pass (draws 1–8) and before the ships (10–89); draw 90
after them. They differ in exactly two states: draw 9 has
SEPARATEALPHABLENDENABLE = 1 (alpha triple ZERO/ZERO/ADD: it writes scene
alpha 0 under the bolt) and ALPHATESTENABLE = 0; draw 90 has 0 and 1. The
additive admission requires separate alpha off (`composition_blend[3] == 0`),
hence `reason=state` on draw 9 every time. Whether the two draws carry the
same instances is inferred (identical counts on all four frames, one writer
cycle per pass), not proven: the vertex bytes are not logged. If they do, the
visible bolt is draw 9's native core (occluded by ships drawn after it) plus
draw 90's gained, expanded footprint, so the expansion reaches the corvette's
bolts. A third draw with the same VS and PS `0a523f33` (1,068–1,136
primitives, draw 91) is not an additive pair.

**Admission drop after the new game (task 4).** From frame 16258 to the end:
0 admitted, 0 refused, pair mask 000, 0 `bolt_footprint` draws, and 0 chase
fire events (`chase_fire_window native_inactive_override` 0 in every window,
against 216 in 12160–15871; `fire_segments.py`). A proxy gate would have
counted: every bullet frame before produced one refusal (draw 9) and one
admission (draw 90), the pair lookup is by shader hash (no bullet PS was
recreated after 16258, no Reset was logged, the motion route stayed latched and
the Ctrl+Shift+F5 key stayed on), so no bullet draw with the pair bound
reached the route: nothing was fired or drawn there (inferred from the
counters; the first-person fire key is not counted by `chase_fire`). No proxy
state is carried across the new game; nothing to fix. The new
`screen_emission_additive_refused_window` row separates the cases from now on.

**Telemetry (per 300 frames).**
`bolt_footprint … draws= written= untouched= gated= instances= expanded=
lengthened= widened= world_axis= disc= refused_* failures= locks= timed_draws= us=
session_draws= session_written= session_expanded= session_gated=
buffer_bytes=`; `bolt_footprint_hist … view=chase|other measure=axis|bbox
instances= edges_px=0.5,1,1.5,2,3,4,6,8,12,16,32 half_length=<12 counts>
width=<12 counts> min_width= min_length=` for each view with measured
instances, before expansion (bucket i counts values below edge i, the last one
≥ 32 px; the chase view along the plan's axes, the gated views from the
projected bounding box: half its larger side, its smaller side);
`screen_emission_additive_refused_window … pair_draws= admitted= refused=
apply_failures= not_reached= state= draw_shape= scene= no_fp16_target=
no_variant= srgb_sampler= projected= dither= alpha_state= alpha_caps=
enabled=` whenever the additive route is requested (telemetry or not),
`not_reached` being the pair draws the route never evaluated (routed,
composed, fog-masked, not submitted, key off). The mode line is
`bolt_footprint_mode requested=1 enabled= w= l= valid= additive= ownership=
view_gate=chase_camera`.

**Cost (inferred; the `us=` field measures it).** Per admitted bullet draw,
added to the first build's projection: one cross product and one `sqrtss` per
triangle in world and on screen, a 3×3 moment and 16 power iterations per
instance, one derivative; about twice the old planning arithmetic, no
allocation, no new D3D call. The histogram is two increments per instance.
Gated draws (first person) pay the projection and a bounding box only, less
than the first build, which planned them fully; they are never written. The additive window
adds one bool test and one increment per bullet draw.

**Option.** `--bolt-footprint [W[,L]]`, launcher default `3,12`
(`X3M_BOLT_FOOTPRINT=3,12`), `0` off, W in (0, 64], W ≤ L ≤ 256, L defaults to
12; the prerequisites and the `--vanilla` refusal are unchanged. An old `R,G`
value parses as W,L (for example `3,8` = 3 px wide, 8 px long).

**Next flight.** Chase view, fire held, one F8 burst, then a first-person
stretch: `bolt_footprint_hist` for the native size distribution in each view,
`bolt_footprint` `lengthened`/`widened`/`gated`, `bolt_components.py` on the
burst (bolts 12 px long on the line to the aim point, peak not lower), and the
first-person burst byte-identical to the vanilla bolt look (`gated` equals the
first-person draws, `written=0` there).
