# Fog route GPU cost at 1920x1080 (design note, 2026-09-23)

**Removed 2026-09-25** (user decision): step B's 24-far-bin variant (`--fog-far-bins`, `X3M_FOG_FAR_BINS`) and its programs are gone; the look marches 40 far bins at spacing 4 (default) or 2 (`docs/verification/launcher-options-inventory.md`, "4. Removed 2026-09-25").

Question: how to cut the GPU cost of the stored-density fog route without changing the accepted look
(L2 law, density scale 1.0x, 22.5 km fade, dust motes 1300,3 / MAX_PX 8, shadow pass off). Plan for
ratification; steps A, B and C are implemented (sections "Step A implemented", "Step B implemented", "Step C
implemented"); B is a runtime variant, off by default; C (march spacing 4) is the default since Run 77 C2 and 2 the
opt-out (section "Step C default flip"). [M] measured, [I] inferred.

Measured (Run 73 C, run274, `--gpu-sync-timing`, stand window W4, 300 frames all `reason=ok`,
`verification/results/run274-gpu-sync/windows.txt`): `fog_route` 4.43 ms median / 5.15 p90, W5-W6
4.38 / 4.40 (stable to 0.05 ms), `motes` nested 0.227 median with `med-floor` 0.000, `fog_fill` 0.02,
`taa` 2.90, `engine` 5.15, `scene` 19.4, serialised dt 22.8 ms. Sync floor per pair 0.264 ms. The
`fog_route` envelope contains its own pair floor and the nested motes pair, so the route's exclusive
serialised cost is about **3.9 ms** [I: 4.43 - 0.264 - 0.227]. Windows W9-W15 (second sector) carry no
`fog_route` row at all: every frame there is `reason=card_refused` before `execute` (run274 log, counted
per window), so the no-fog case already costs zero device calls and no sync pair. Host GPU: Apple M5 Pro,
20 cores [M, `system_profiler`]; the D3D9 adapter string under D3DMetal is a spoof (GeForce 8800 GTX).

## Recommendation

No look-neutral change with a material saving exists inside today's route: the law already branches
around empty samples, the 40 far bins are skipped on short geometry rays, the no-fog sector draws
nothing, the composite/repair split is forced by the 512-slot ps_3_0 ceiling (composite 210 + march 425),
and the targets are already FP16. Every cut that moves more than ~0.3 ms changes the sampling law and
therefore needs a new host reference and one flight. So:

1. **Build A (no new reference, fixture bit-identical):** sub-pass sync boundaries `fog_march`,
   `fog_composite`, `fog_repair` inside the transaction, the transmittance early-out in the march
   (`break` when `sum.z < 1e-4`, march program only), and a host census of the repair-pixel fraction on a
   captured RT2 at 2-px and 4-px sample spacing. Saving 0-0.3 ms [I]; the point of the build is the
   numbers that rank steps B and C.
2. **Build B (new reference + one flight): far bins 40 -> 24** over [12000, cap], ds 4187 units, one
   sample per 4096-unit far node, the same ratio the near range already uses (500-unit bins on 512-unit
   nodes). Expected saving **0.6-0.9 ms** of the 3.9 [I].
3. **Build C, conditional on the census: quarter-resolution march** (480x270) with the composite footprint
   at 4-px spacing. Expected **1.2-2.0 ms net** [I], but the repair fraction on thin lattice geometry is
   unknown and can swallow half of it; the census settles that before anything is built.

## 1. Cost model per frame at 1920x1080 [I unless marked]

Route as it runs today (`src/renderer/fog_pass.cpp` `execute`, stored path, grid off): state block
capture -> normalize (~120 calls) -> `StretchRect` scene copy to `scratch_` -> march quad into `lit_`
(half res) -> composite quad into the scene target -> repair quad into the scene target (clip) -> motes
indexed draw (ONE/ONE) -> block apply. All targets are the ones `prepare_targets` creates: `lit_`
A16B16G16R16F 960x540 (S.rgb, T), `scratch_` A16B16G16R16F 1920x1080; the depth share is the motion
output's A32B32G32R32F RT2 (not owned here). Sampler state: everything POINT except s1/s7 (atlases,
LINEAR); no mips anywhere.

Per-bin fetch and slot counts from `src/fog/fog_density_field_inc.h` (FOG_LOOK, in-march shafts) and
the compiled records (`verification/results/fog-density-shader/summary.json`: march-look 425 slots /
15 static fetch instructions / 1 loop, composite-look 210 / 10, repair-look 510 / 20):

| Stage | Res / pixels | Fetches per pixel | ALU per pixel (ps_3_0 slots, executed) | Share of 3.9 ms |
| --- | --- | --- | --- | --- |
| March | 960x540 = 518,400 | Sky ray to the 112,500 cap: 1 depth + 136 atlas (24 near bins x 2 fine, 3 far bins x 2 fine, 4 blend bins x 4, 33 far bins x 2) + 6 per non-empty bin (2 sun-ward far tap + 4 shaft PCF), so ~290 on a fogged sky ray, 136 on an empty one; geometry ray with L <= 12,000: 48 + 6 per non-empty bin (the 40 far bins branch out at ~5 slots each) | Prologue ~60; per bin ~110 (bin/lambda 18, warp 25, level sample 25 (+25 in the blend), coverage waves 15, remap/exp/accumulate 27); +~135 per non-empty bin (one-hot shaft lookup ~90, tap + powder ~45). Fogged sky ray ~10.6 k, empty sky ray ~7.1 k, near geometry ray ~3-5 k | **75-85 % (2.9-3.3 ms)**; ALU-bound: 518,400 x ~9 k slots = ~4.7 G slots, ~150 M fetches |
| Composite | 1920x1080 = 2,073,600 | 10: scene FP16, own depth RGBA32F, 4 half depths RGBA32F, 4 st FP16 (manual bilinear, POINT) | ~150 of 210 | 8-12 % (0.3-0.45 ms); bandwidth-bound on RGBA32F depth (~70 MB unique reads + 16.6 MB write) |
| Repair | 2,073,600 | 5 depth RGBA32F for every pixel; repaired pixels add 1 scene + a full 510-slot march at full resolution | ~70 prologue, `clip` for the rest; 510 x 64 bins on repaired pixels | 4-8 % (0.15-0.3 ms) plus the repaired-pixel marches: fraction **unknown** in flight (the fixture forces odd pixels; the game log has no count) |
| Scene copy | `StretchRect` 16.6 MB FP16 | | | 2-4 % (0.1-0.15 ms) |
| Binds, constants, block capture/apply | ~100 device calls | | | <= 0.1 ms serialised (CPU submit; `cpu_us` p50 682 us in run251 without the diagnostic [M]) |
| Motes | 1300 capsules, ~5-30 k px | 5-9 per pixel | ~250 slots per pixel, 40 per vertex | < 0.05 ms [M: nested pair 0.227 = the pair floor] |

Cross-check: 2.9-3.3 ms for ~4.7 G executed slots is ~1.5 G slots/ms, i.e. ~4-6 TFLOP/s effective at
2-4 scalar ops per slot, plausible for this GPU class; the model is consistent with the measured
envelope but the shares are not measured (the shader fixture's own timings do not scale with the work
submitted, ledger "Fixture timing at 1280x768"). The sub-pass split in build A turns the shares into
measurements.

Sky versus geometry mix at the stand is unknown; the bracket above spans a sky-dominated view (upper
bound) and a station-filling view (lower bound, more short geometry rays, more repair pixels).

## 2. Candidate cuts, ranked by expected saving per unit of look risk

| # | Cut | Mechanism | Saving [I] | Fixture effect | Verification |
| --- | --- | --- | --- | --- | --- |
| 1 | **Far bins 40 -> 24** | `march_depth`: 24 bins over [12000, cap] (ds = (cap-12000)/24 = 4187 at the 112,500 cap; 7833 at the 200,000 override); loop stays 48 iterations, one `rep`; repair identical; host twin `look_march` in `tools/analysis/fog_density_shader_reference.py` gets the same bin law | 0.6-0.9 ms: the far bins hold the cloud samples, so both the base and the non-empty per-bin cost fall by 16/64 on sky and far-geometry rays | **New reference**: `reference.npz` regenerated, the 11 `ACCEPTED_LOOK_HASHES` re-pinned, gates T/S p99 .002 / max .003 re-passed against the new host law; also 24 + 24 must still fit 512 slots (it removes slots, none added) | Host study first: 40-bin vs 24-bin `look_march` on the reference poses, report max/p99 T and S delta and the fraction of pixels past .003 (the look-change witness the orchestrator ratifies); fixture PASS; one flight at the stand, W4-type window, `fog_march` median before/after |
| 2 | **Quarter-res march** 480x270 | `prepare_targets` `lit_` at (w+3)/4; `sizes.zw`; composite/repair footprint law at 4-px spacing (`hp = pixel*0.25`, samples at full pixels = 0 mod 4, `needs_repair` the same class law); march uv `(4p+0.5)/full`; shaft-lookup noise cell = the quarter cell | 1.2-2.0 ms net: march 3.1 -> 0.8 minus the extra repaired-pixel marches (every full pixel of a strut thinner than 4 px, and sky pixels beside it, with no class-compatible sample) | New reference (composite/repair cases at 128x72 -> quarter 32x18), new footprint checks | **Census before building**: host script over a captured RT2 of the stand computing the class-incompatible fraction at 2-px and 4-px spacing (today's repair fraction and the quarter one); go only if the 4-px fraction x 64-bin full-res marches stays under ~0.5 ms [I: fraction <= ~4 %]. Then fixture, then one flight with the sub-pass split (`fog_repair` shows the blow-up directly) |
| 3 | Transmittance early-out | `[branch] if (sum.z < 1e-4) break;` inside the march loop (ps_3_0 `break_comp`, ~4 slots: 425 -> ~429; **not** in repair at 510/512). Residual after the break: T <= 1e-4, S <= 1e-4 x albedo x (E/pi x phase(<= 4.9 at cos 1) + ambient) ~ 1.5e-3 < the .003 gate | 0-0.3 ms: zero where 1-T stays <= .26 (the ledger's 1.0x figure), real only in a dense family or at 1.5x | Bit-identical on the fixture (every look case has min T .285 [M]); hashes unchanged, `slots_below_512` and `march_loops_kept` hold | Fixture PASS with unchanged hashes; one new fixture case with a saturating column proves the break fires and the residual is below the gate |
| 4 | Adaptive near bins for short geometry rays | n_near = clamp(ceil(L/500), 6, 24) so ds >= ~500 on geometry closer than 12,000; sky rays untouched | 0.1-0.3 ms (only rays with L < 12,000, which already cost 37 % of a sky ray) | New reference (A_look_depth3 changes) | Same as #1; low look risk: under TAA the shaft-lookup offset integrates the bin, the converged image is the same, per-frame shaft noise on near hulls rises |
| 5 | Occupancy-gated far bins | Worker keeps a max raw rho per 4x4x1-node far cell (1-node dilation for the 1900-unit warp), one L8 texture per level (32x32x128 texels), one point fetch per far bin, skip warp/sample/remap when max < coverage - variation (.23) | <= 0.3 ms: the fine range cannot be gated (warp dilation 4 fine nodes), and 5-km patches leave few 12-km-dilated far cells empty | Bit-identical in law (skipped bins add exactly 0); compiler re-scheduling may still move ulps, then within tolerance and re-pin | 2-3 days: new resource, upload path, fixture atlas twin. Not worth it at this field scale; revisit only for a sparse family |
| 6 | Temporal interleave (alternate bins or checkerboard pixels per frame, TAA integrates) | Half the bins or pixels per frame | up to 1.5 ms | New reference + flight | **Rejected for now**: the retired L3 per-frame sample offset measured .69 % temporal std / .51 % spatial high-pass and read as grain (ledger, "Shaft lookup offset"); a bin interleave is that dither at twice the amplitude; a camera cut shows one frame of alternation. Keep as step 4 only if 1-3 fall short; the user's tolerance for TAA ghosting does not cover grain |
| 7 | Merge composite and repair (or march) into one full-res pass | | | | Impossible under ps_3_0: composite 210 + march 425 > 512; repair is at 510. The visibility grid is off and Run 256's A/B showed it not cheaper (p95 19 vs 17 ms on) |
| 8 | Render-target formats | | | | Already FP16 for `lit_`/`scratch_`; the RGBA32F depth is the motion output's and both full-res passes need `hd.b` (the relative-depth weight), so its reads cannot be replaced by a class mask. No lever |
| 9 | Cheaper sampling | | | | `level_sample` is already 2 fetches (bilinear XY + lane tent in Z); a single nearest-Z fetch aliases the 512-unit nodes; no mips exist (129-texel tiles in one atlas level); volume textures were rejected for capability reasons (fog-density-runtime-integration.md section 2). No lever |
| 10 | No-fog full-screen pass | | | | Already absent: `card_refused` / `strength` / `unprepared` skip before `execute` (W9-W15 have no `fog_route` rows). Nothing to do |
| 11 | Dust motes | | | | < 0.05 ms measured; no lever. Under the diagnostic the nested pair adds ~0.23 ms to the `fog_route` envelope: subtract it when comparing |
| 12 | Scene copy | | | | One `StretchRect` is the minimum for read-scene/write-target under D3D9 without touching the HDR target ownership. No lever |

## 3. What must not change

- The look law and its constants (`FogLookTuning` defaults: coverage .35, exponent 2, sigma x8,
  coverage variation .12 on the three oblique waves, warp 13/500 and 5/1400, lobes .75/.7/-.15,
  albedo white .5, ambient .35, extinction tint .6, scatter lift .5 / floor .5, shadow floor .15,
  self-shadow 3 / powder .5, tap 3000 / 9000, shadow jitter 1), the sky cap 112,500 / taper start
  65,000 (22.5 km / 13 km), density scale 1.0x (`fog_strength_default .02`), the two-cascade one-hot
  shaft law with the TAA-phase lookup offset, the LOD blend 20,000-30,000, the exact-empty identity
  (T = 1, S = 0 leaves the scene byte-identical), the tinted extinction in composite/repair.
- The near bin law (24 over [0, 12,000]) in every step; the far bin law only in step B, deliberately.
- Fixture pins: `ACCEPTED_LOOK_HASHES` (11 images: bit-identical for A, re-pinned with a recorded delta for
  B/C), gates T/S p99 .002 / max .003, `parity_T_max` .001, `temporal` .003, every program < 512 slots,
  march/repair >= 1 loop and <= 24 texture instructions, the 29 mote checks and `motes_off_bit_identical`,
  the grid checks (pass off stays byte-identical to pass on's control), the bridge's
  `legacy_bit_identical_to_baseline` (the legacy 24-step path is untouched by every step here).
- Motes 1300,3, MAX_PX 8, streak 128; shadow pass default off; the hand-over and cold-fill behaviour
  (`fog_fill`, another note); the D3DSBT_ALL transaction shape, the scene-open/close and device-loss
  rules; no per-draw hook work (the fog touches none today and none of the steps adds any).
- Native Windows: every step uses documented D3D9 only (ps_3_0 `break_comp` in a `rep` loop, the same
  texture formats, `DrawPrimitiveUP`, `D3DQUERYTYPE_EVENT` for the split); nothing verified natively,
  the existing fog row in `platform-portability.md` covers it.

## 4. Steps

**A (this candidate).** (1) Three new `gpu_sync_timing` passes `fog_march`, `fog_composite`,
`fog_repair` (pass_count 14 -> 17, 34 event queries; `Marks` already reaches `execute` through
`configure_sync_timing`, so the spans go around the three `quad` calls like the mote span; off, each
boundary is one null branch). (2) The early-out (#3). (3) `tools/analysis/fog_repair_census.py`: from a
captured RT2 (`.r` class, `.b` depth) the fraction of full pixels with no class-compatible sample at
spacing 2 and 4, per frame of a stand burst; if the flight captures carry no RT2, an occlusion query
around the repair quad (`D3DQUERYTYPE_OCCLUSION`, diagnostic only, created with the sync queries) is
the fallback. Acceptance: `fog_density_shader_run.py build/run/check` PASS with the hashes unchanged
and slots < 512; `run_gpu_sync_timing.py` 30/30 with the new passes; one diagnostic flight at the stand.
Expected saving 0-0.3 ms [I]; expected information: the march/composite/repair split and the repair fraction.

**B.** Far bins 24. Host study, new reference, fixture, one flight. Ratify on the host delta witness
and the flight's `fog_march` median (target 2.3-2.6 ms from ~3.1 [I]) and the user's verdict on far
cloud edges and shafts (the risk: coarser per-bin stamps of shaft edges beyond 12,000 units before TAA
converges, and a slightly different per-bin alpha discretisation on dense patches).

**C.** Quarter-res march if the census says the 4-px repair fraction is small. Otherwise stop at B
and revisit #6 with a proper history-reprojection design rather than a bin interleave.

## 5. Measurement plan

- Windows: a fogged stand window with all 300 frames `reason=ok` (run274 W4-W6 are the baseline:
  `fog_route` 4.43 / 4.38 / 4.40, `taa` 2.90 / 3.06 / 2.91, `engine` 5.15 / 5.69 / 5.40). Same save, same
  view, host idle. Compare medians and p90 of `fog_route`, `fog_march`, `fog_composite`, `fog_repair`;
  `taa` and `engine` are the controls (unchanged expected). Summariser:
  `verification/results/run274-gpu-sync/gpu_sync_windows.py` (already prints `med-floor`).
- Boundary cost: each new pair adds ~0.264 ms to the serialised frame and to the `fog_route` envelope,
  so with the split on, `fog_route` reads ~0.8 ms higher than run274's; compare `fog_route - 3 x 0.264 -
  motes` against 3.9, or the sub-pass `med-floor` sum. The split is worth its cost for exactly the flights
  that rank B and C; it stays behind `--gpu-sync-timing`.
- The serialised figures are GPU headroom, not FPS: the stand is CPU-bound (dt 22 ms at run270 with
  the proxy's GPU passes overlapping). The user-visible gain is read from an ordinary flight's
  `frame_timing` dt; a 1 ms GPU cut may show no FPS change at the stand and show in GPU-heavier views
  (second sector: `taa` 5.25, `engine` 10.3 serialised).

## Unknown, and what settles it

- Sky/geometry mix and the repair-pixel fraction at the stand: the census (A.3) or the occlusion query.
- The march/composite/repair split of the 3.9 ms: the sub-pass boundaries (A.1).
- Whether the flown family ever saturates T (the early-out's value): a per-window minimum-T is not
  logged; the fixture case in A proves the mechanism, the flight's `fog_march` delta prices it.
- The look delta of 24 far bins: the host study in B; the perceptual verdict needs the flight.
- Native Windows timing: not measurable here; the D3D9 surface of every step is documented.

## Decision (main session, 2026-09-23)

Ratified: step A (sub-pass sync boundaries `fog_march` / `fog_composite` / `fog_repair`, the transmittance early-out
in the march program, the repair-pixel census) goes into the candidate after the Run 73 B hand-over fixes land in
`fog_pass.cpp`; it must keep the fog fixture bit-identical. Step B (far bins 40 → 24) waits for step A's in-flight
split and a new reference; step C (quarter-resolution march) waits for the census. Context: the fog shadow pass
itself costs about 0.3 ms of the route (Run 74 B), so the cuts have to come from the march and composite; the stand
is CPU-bound at 22 ms, so a GPU cut shows as headroom there and as FPS only in GPU-bound scenes.

## Step A implemented (2026-09-23)

Uncommitted worktree on 72645b5e, not installed. [M] measured on the fixtures; ledgers
[volumetric-fog.md](../verification/volumetric-fog.md) and [gpu-sync-timing.md](../verification/gpu-sync-timing.md), same date.

- **Sub-pass boundaries.** `fog_march` (19), `fog_composite` (20), `fog_repair` (21) are appended after `taa_display`,
  so the first 19 indices and names are unchanged: 22 passes, 44 event queries (section 4's "14 -> 17" predates the
  `taa_*` passes). One `gpu_sync_timing::Span` around each of the three stored-path quads in `FogPass::execute` (the
  binds before a quad are CPU state until the draw, so they stay in `fog_route` only). Off: one null branch per
  boundary, no device call, nothing per draw.
- **Early-out: in the source, off by default.** `[branch] if (sum.z < 1e-4) break;` at the end of the look's bin loop
  in `fog_density_field_inc.h`, compiled only under `FOG_MARCH_EARLY_OUT`, which no program defines. Why off: at the
  accepted 1.0x look 1-T stays <= .26, so T never reaches 1e-4 and the break never fires, while defined it cost
  11-12 slots inside the 64-bin loop [M: `fog_density_march_look` 425 -> 436, `fog_density_march_grid` 352 -> 364,
  one `breakc` each], a per-pixel cost with no gain. As shipped every fog program is byte-identical to 72645b5e
  [M, `verification/results/fog-gpu-cost/step_a_programs.py`: 12/12, march_look 425, march_grid 352]. Available for
  a dense-look experiment (a family or strength that saturates T): define it in `fog_density_march_ps.hlsl` (march
  programs only; repair is at 510 of 512), regenerate, and add a saturating fixture case with a new reference to prove
  the residual stays under the .003 gate; then price it with `fog_march` before/after at the same view.
- **Repair census.** The occlusion-query fallback of section 4 (no flight capture carries RT2): one
  `D3DQUERYTYPE_OCCLUSION` query created with the sync queries when the device supports it, bracketing the
  `fog_repair` quad inside its pair, read without FLUSH after the frame's Present pair (the end sync has retired it:
  no extra spin). Row per 300-frame window: `volumetric_fog_repair_census window= frames= n= median_ppm= p90_ppm=
  max_ppm= last_pixels= area= unread= lost= failed= device=` (unread: not ready; lost / failed: `GetData` refused). Cost off: none (no object exists). Cost on: two `Issue` calls and one
  `GetData` per fogged frame, nothing on the GPU beyond the occlusion counter [I]. **Semantics** [M, pass fixture]:
  the repair clips pixels that need no repair *and* pixels whose march comes out exactly empty, so the count is the
  written repair pixels, a lower bound on the marched ones (odd 31x17 target: 255 need the repair, 125 written, equal
  to the CPU twin). It measures today's 2-px spacing only; the 4-px fraction for step C still needs the RT2 census.
- **Boundary overhead in the diagnostic.** The three pairs add about 3 x 0.264 = 0.79 ms to `fog_route` and to the
  serialised frame [I, from the light-pair floor]; with the nested `motes` pair the envelope carries four floors.

How to read the next `--gpu-sync-timing` flight (a fogged stand window, all 300 frames `reason=ok`):

- `fog_route` reads about 0.8 ms above run274's 4.43 for the same work. Its exclusive remainder, `fog_route -
  fog_march - fog_composite - fog_repair - motes` (the sub-pass medians already include their own floors), is the
  normalize/copy/binds plus the route's own pair floor; compare that and the sum against run274's ~3.9 ms exclusive.
- `fog_march - 0.264` against section 1's 2.9-3.3 ms prices step B (far bins 24 cut the march's far share); a
  `fog_march` well below that range means the model over-weights the march and B's saving shrinks with it.
- `fog_composite - 0.264` against 0.3-0.45 ms and `fog_repair - 0.264` against 0.15-0.3 ms plus repaired marches.
  With the census: `(fog_repair - 0.264) / (median_ppm x 2,073,600 / 1e6)` is the cost per written repair pixel,
  an upper bound on the per-pixel march cost because the 5-tap prologue of every pixel is in it. Step C multiplies the
  pixel count at 4-px spacing by that cost; the census alone does not give the 4-px count.
- The early-out is off in this build, so `fog_march` carries no break; see above for the dense-look experiment.

## Step B implemented (2026-09-23)

Uncommitted worktree on b4e2fcff, not installed; default off. Ledger: [volumetric-fog.md](../verification/volumetric-fog.md),
same date. Evidence scripts: `verification/results/fog-gpu-cost/step_b_*.py` with their `_out.txt`.

**The law.** `FOG_FAR_BINS` in `fog_density_field_inc.h` (look law only; default 40): far step
`(L - 12000) / FOG_FAR_BINS`, loop `24 + FOG_FAR_BINS`. At 24 and the 112,500 column cap: ds 4187.5 units = 1.02 far
nodes (4096) per sample, the near law's ratio (500 on 512); the far range is unchanged, [12000, L], so the last far
sample sits at 110,406 (22.1 km; 111,244 at 40) and the smoothstep taper 65,000-112,500 still reaches zero at the cap
(22.5 km), sampled by 11 bins instead of 19. The LOD blend 20,000-30,000 gets 2 samples (4 at 40). **Cap rule:** 24 bins
hold one sample per far node only near the default cap; at the 200,000 override (`X3M_FOG_LOOK_SKY_CAP`) ds would be
7833 (1.9 nodes) and alias, so the variant is refused above a 120,000 cap (`fog_far_bins_coarse_cap_max`, ds <= 4500,
1.1 nodes) and 40 draws; with only the 40 and 24 pairs compiled, a count scaled to the cap is not available. Nothing
else moves: near law, cap, taper start, coverage/warp/lobes, shaft law, repair
footprint, composite; the unshaped reference law and the grid variants (`FOG_SHADOW_PASS`) keep 40 [M: 12 default
programs byte-identical to b4e2fcff, `step_b_programs_out.txt`].

**Variant mechanism: a runtime switch over two compiled program pairs.** `fog_density_{march,repair}_look_far24_ps.hlsl`
(`#define FOG_FAR_BINS 24` + the look source) compile to `*_look_far24_program_inc.h`; composite never marches and is
shared. `FogDensityConfig::far_bins` (40 or 24; `prepare_density` refuses any other count with `E_INVALIDARG`) selects
the pair `density_resources` creates at prepare, never on a draw path. A different count on a live pass (the proxy never
does this: the count is launch-fixed) builds and slot-checks the new march/repair pair first and swaps it in only when
complete; the shared composite stays. A pair that cannot be built leaves the working pair drawing and that count
refused until detach (`far_bins_refused=program`, no retries); a first creation whose 24-bin pair fails falls back to
40 instead of refusing the stored path. FogPass itself clamps 24 to 40 (`FogDensityStatus::far_bins_refused`, logged
once by the proxy as `fog_far_bins_refused reason= requested= drawn=`): `shadow_pass` from the first prepare that asks
for the grid until detach, so a grid frame and its toggled-off frame draw one far law; `cap` above 120,000. Launcher
`--fog-far-bins {40,24}` → `X3M_FOG_FAR_BINS` (always written, `24` only with `--volumetric-fog-range stored`, refused
with `--fog-shadow-pass on`); the DLL takes exactly `24` (or `40`) under the stored range, applies the shadow-pass and
cap rules, and logs `volumetric_fog_far_bins bins= requested=<value> refused=none|shadow_pass|cap|invalid sky_cap=`
once at init (any other value is echoed, characters outside `[0-9A-Za-z._+-]` as `?`); `far_bins=` is on the
`volumetric_fog_cache … event=config` row. Per frame: the same device calls as 40 [M, pass
fixture], no added CPU work.

**Counts per variant** [M: `step_b_programs_out.txt`; I: `step_b_bin_law_out.txt`, static per-ray counts]:

| Program | ps_3_0 slots | texture instructions | rep | Sky ray to the cap: iterations / atlas fetches empty / all bins fogged |
| --- | --- | --- | --- | --- |
| march look (40) | 425 | 15 | 64 | 64 / 137 / 521 |
| march look far24 | 425 | 15 | 48 | 48 / 101 / 389 |
| repair look (40) | 510 | 20 | 64 | (same per repaired pixel) |
| repair look far24 | 510 | 20 | 48 | |

Geometry rays with L <= 12,000 keep their 24 near samples (49 fetches); they only skip 24 instead of 40 empty
far iterations. Expected march saving [I]: at most the removed quarter of the iterations on rays past 12 km, 0.8-1.15 ms
of the measured 4.5-4.7 ms net `fog_march` (run280) on a sky-dominated view, less on a hull-filled one.

**Fixture** (bottle X3, `wine_lock.py`, one run of both references: `fog_density_shader_run.py build/run/check --reference
/tmp/x3-run67-fog-ref --variant-reference /tmp/x3-run76-fog-ref-far24`; the variant reference is generated once by `build`
with the exporter's `--far-bins 24` from the same packets, 3 s on the host) [M]:

- Default unchanged: 30/30 default gates incl. `pass_off_bit_identical` (11/11 accepted look hashes); against b4e2fcff's
  committed summary 662 figures compared, 0 differ in the default path (the 2 that change are counts the variant adds to
  the same run: fixture checks 30 → 35, generated atlases 28 → 32; `step_b_fixture_identity_out.txt`). The exporter at 40
  bins reproduces all 37 look/repair/grid arrays of the run67 reference bit for bit.
- Variant PASS against its own reference: 7 `far24_*` gates, 37/37 in all. Look cases GPU against host T max .00073,
  S max .00045 (gates .003); repair split with the far24 programs .00051 from its host, .0105 from the offset-lookup
  law; pass fixture 19 far-bin checks of 142 (programs at prepare, CPU twin at 24 bins worst T .00054 / S .00011 against
  .0084 / .0047 from the 40-bin twin, the same pass back at 40 byte-identical to the default frames, a swap creating
  exactly the two programs, equal device calls 322, count 32 refused, Reset, cap 150,000 refused and 112,500 back, an
  injected 24-bin creation failure keeping the 40 pair with no retries, the shadow pass clamping to 40 also toggled
  off, detach, refcount). The variant's record is `verification/results/fog-density-shader/far24.json`. One gate differs in form: the stripes case's offset lookup moves S by
  .0053 at 24 bins (.0076 at 40), under the default 2x-gate margin; the variant gate requires the move minus the case's
  GPU error (.00033) to exceed the .003 gate, which still makes a dropped offset fail the look gate.
- How far the look moves (`step_b_deviation_out.txt`; GPU RGBA16F far24 against 40 over each 128x72 case, fogged pixels
  3540-4740 of 9216; host law against host law on 576 rays):

| Case | T max | T mean (fogged) | S max | S mean (fogged) | Pixels past .003 | Host T / S max |
| --- | --- | --- | --- | --- | --- | --- |
| A sky (= A depth3) | .0190 | .0023 | .0134 | .0007 | 1353 / 4740 | .0171 / .0134 |
| A depth 90000 | .0100 | .0014 | .0073 | .0004 | 553 / 4690 | .0072 / .0048 |
| A stripes / held | .0190 | .0023 | .0120 / .0115 | .0007 | 1438 / 1419 | .0171 / .0118 |
| A shadowed | .0190 | .0023 | .0076 | .0004 | 1333 / 4740 | .0171 / .0072 |
| B sky | .0298 | .0046 | .0079 | .0008 | 1715 / 3540 | .0261 / .0071 |

  Unbiased: signed mean T(24) - T(40) over fogged pixels +.0003 (A sky), -.0004 (A depth 90000), +.0006 (B sky), half
  the pixels up and half down: a resampling of the far clouds, not a thinner or denser fog. 12-48 % of fogged pixels move
  past the .003 gate, up to 3 % in T; whether that is visible (edges of far patches, the taper) is the flight's question.
  The fixture's march slope timings do not scale with the work (as in step A) and price nothing.

**Flight plan (one user flight, `--gpu-sync-timing`).** Same save and stand as Run 75 C (run280, fogged sector idx 2),
two launches with everything else equal: `--fog-far-bins 40`, then `--fog-far-bins 24`. Hold a still view for two
300-frame windows, then turn in place for two; host idle, nothing else on the GPU. Read `fog_march` median and p90
per window (target [I] 3.4-3.8 ms from 4.79-5.01 including the 0.264 floor), `fog_repair` and `fog_composite`
unchanged (controls), `taa`/`engine` controls, and `volumetric_fog_far_bins bins=24` in the second log. Look check,
same stand, both launches: far cloud edges and the taper toward 22 km (banding or stepping of the far fog), shafts
beyond 12 km (coarser stamps of an occluder's edge before TAA settles, visible while turning), dense patches
(per-bin alpha steps). Accept B when the user sees no difference at the stand and while turning and `fog_march`
drops by at least 0.5 ms; then the default flips to 24 in a later candidate, with the accepted-look hashes re-pinned.

**Step C assessment (quarter-resolution march), from the measured split** [I unless marked]:

- Today (run280 fogged, [M]): march 4.5-4.7 ms net over 518,400 half-res pixels (~8.9 ns per marched pixel),
  composite ~0.3, repair ~0.3 net while it writes only 30-104 ppm (62-216 pixels per frame): the repair's cost is its
  full-screen 5-tap prologue, not its marches.
- Quarter (480x270 = 129,600 marched pixels): march ~1.15 ms at 40 bins, ~0.85 ms on top of B. Composite: the same 4 +
  4 + 2 fetches per full pixel, ~0.3 ms unchanged; repair prologue unchanged, ~0.3 ms. Repair marches at 4-px spacing:
  the edge band doubles and features 2-4 px wide lose every class-compatible sample, so plausibly 2-4x today's count
  (120-860 pixels); at a worst case of one 32-lane SIMD group per scattered repaired pixel (~0.28 us each) that is
  0.03-0.25 ms. Net saving ~3.2-3.4 ms at 40 bins (~2.6-2.7 ms more after B). The repair fraction no longer threatens C;
  its risks are the look: fog detail and shaft edges at 4-px spacing (bilinear between samples 4 px apart; TAA
  integrates only the sub-pixel jitter, not the 4x4 cell) and 4x coarser cells of the shaft-lookup noise.
- What the depth-class upsample needs: `lit_` at ((w+3)/4, (h+3)/4) and `sizes.zw` of that; one fixed full pixel per
  cell for the march ray and its class (uv (4p+1.5)/full, nearer the cell centre than 4p), read identically by
  composite and repair; `footprint_weight` / `needs_repair` with `hp = pixel*0.25` and the sample uv `(4q+1.5)/full`
  (same class law, same relative-depth weight, same 2x2 footprint and fetch count); the shaft-lookup cell
  `floor(uv*sizes.xy*0.25)`; the grid pass is already quarter-resolution and needs nothing. A new reference for every
  look case and the composite/repair split (half 128x72 → quarter 64x36 of the 256x144 fixture target), and the pass
  fixture's 64x36 images → 32x18. Before building: count the 4-px repair set in flight with the existing census
  mechanism, one more occlusion-counted quad whose program clips unless `needs_repair` at 4-px spacing (colour writes
  off, diagnostic only), beside today's count.


## Run 76 D (run284 40 bins, run285 24 bins, run286 default; 2026-09-24): step B in flight

Fogged stand under `--gpu-sync-timing`, 300-frame windows, `fog_march` serialised medians incl. its 0.264 ms floor
(measured; `verification/results/run284-286-far-bins/`): 40 bins 4.77–4.89 ms (run284) and 4.86–4.96 (run286);
24 bins 4.37–4.71 ms (run285). Saving 0.2–0.5 ms, well under the 0.8–1.15 ms the cost model inferred, so the far
loop is not where most of the march time goes: the per-ray base (near bins, the shaft/ambient terms, the atlas
fetch cost per sample) dominates. User: no visible difference between 24 and 40 in a short stand. Decision: 40 stays
the default (the saving does not buy the far-detail risk); step C (quarter-resolution march) is the lever and is being
implemented as a runtime variant.

## Step C implemented (2026-09-24)

Uncommitted worktree on 638b19ad, not installed; default 2 (the accepted half-resolution march). Ledger:
[volumetric-fog.md](../verification/volumetric-fog.md), same date. Evidence scripts: `verification/results/fog-gpu-cost/step_c_*.py`
with their `_out.txt`. [M] measured, [I] inferred.

**The law.** `FOG_MARCH_SCALE` in `fog_density_field_inc.h` (2 or 4; anything else is a compile error) expands to
`FOG_MARCH_STEP` / `FOG_MARCH_INVERSE`, which at 2 are the literals `2.0` / `0.5` they replaced, so all 14 pre-existing
fog programs are byte-identical to 638b19ad (7 are new; the 15 regenerated records, dust-mote vertex included, change only
their include and tool hashes, `header_sha256` unchanged) [M, `step_c_programs_out.txt`]. At 4, march pixel p takes the ray and the
depth class of full pixel 4p (uv `(4p+.5)/full`), the shaft-lookup noise cell is the quarter pixel, and composite, repair
and `needs_repair` read their samples at full pixels 4q with `hp = pixel*0.25`: the same class law, relative-depth weight,
2x2 footprint and fetch count. The sample sits at the cell's first pixel rather than at 4p+1.5 as the assessment above
suggested: with `hp = pixel*0.25` a sample at 4p+1.5 would be interpolated 1.5 px away from where its ray was taken, while
at 4p a pixel on a sample has f = 0 (exact alignment) and the scale-2 expansion stays token-identical. The last 1-3 columns
and rows clamp to the last sample, as the last odd column does at scale 2. The grid programs (`FOG_SHADOW_PASS`) exist at
spacing 2 only (`#error` otherwise).

**Program matrix** [M, `step_c_programs_out.txt`; ps_3_0 slots / texture instructions / rep count]:

| Program | scale 2, 40 far bins | scale 2, 24 | scale 4, 40 | scale 4, 24 |
| --- | --- | --- | --- | --- |
| march look | 425 / 15 / 64 (default) | 425 / 15 / 48 | 425 / 15 / 64 | 425 / 15 / 48 |
| repair look | 510 / 20 / 64 (default) | 510 / 20 / 48 | 510 / 20 / 64 | 510 / 20 / 48 |
| composite look | 210 / 10 (shared by both bin counts) | | 210 / 10 (shared) | |
| needs census (diagnostic only) | 77 / 5 | | 77 / 5 | |

The DLL now carries 12 look programs (4 march, 4 repair, 2 composite, 2 census); 7 are new. Each attachment creates
three of them, plus the census one under `--gpu-sync-timing`. There is no new constant: the spacing is a define, and
`sizes.zw` carries the march target's extent.

**Variant mechanism** (the far-bins pattern). `FogDensityConfig::march_scale` accepts 2 or 4; anything else returns
`E_INVALIDARG` at `prepare_density`. The value is latched at `prepare_density`, and `density_resources` creates the
following, never on a draw path:

- first the quarter target: A16B16G16R16F, `fog_march_extent(w,4)` x `fog_march_extent(h,4)`, 480x270 at 1080p. It is
  sized from and released with the targets and re-created at the next prepare after Reset or resize;
- then the march/repair/composite of the requested (far bins, spacing).

Switching and failure rules:

- A spacing change builds the new set before it drops the old one. The composite is rebuilt only when the spacing
  changes; a far-bin change alone swaps the march/repair pair.
- A set that cannot be built leaves the working one drawing (`march_scale_refused=program`, sticky until detach, no
  retries). One exception (review F1): after a Reset or resize while 4 draws, if the quarter target cannot be re-created
  and the half set cannot be built in the same prepare, the quarter set is dropped (it cannot draw without its target),
  4 stays refused as `target`, and the next prepare builds the half set from scratch [M, pass fixture `q4_double_*`].
- A quarter target that cannot be created refuses 4 as `target` (sticky).
- The quarter target exists only while the quarter programs draw.
- From the first prepare that asks for the shadow pass until detach, 4 is clamped to 2 (`shadow_pass`), so a grid frame
  and its toggled-off frame draw at one spacing.

The legacy path and the half-resolution `lit_` are untouched. The half target stays allocated while 4 draws (4 MB at
1080p), so a refusal can fall back without an allocation.

Proxy and launcher:

- `--fog-march-scale {2,4}` → `X3M_FOG_MARCH_SCALE`. The variable is always written; 4 needs the stored range, is
  refused with `--fog-shadow-pass on`, and combines with `--fog-far-bins`.
- The DLL accepts exactly `4`. It logs `volumetric_fog_march_scale scale= requested= refused=none|shadow_pass|invalid`
  once at init, adds `march_scale=` to the cache config row, and logs `fog_march_scale_refused reason= requested= drawn=`
  once.
- Per frame, 2 and 4 issue the same device calls [M: 322, pass fixture]; nothing is added per draw.

**The repair at 4 px and the needs census.** The repair marches at full resolution every valid-class pixel whose four
nonzero-weight samples (full pixels 4q) all belong to another depth class. The class law is today's; only the spacing
widens.

Under `--gpu-sync-timing` the census works as follows:

- A second `D3DQUERYTYPE_OCCLUSION` query, created with the first and sharing its lifetime and Reset handling, brackets
  one full-screen quad of `fog_density_needs_census[_q4]` between composite and repair, with colour writes off.
- The quad clips unless `needs_repair` holds at the drawn spacing, so it counts the pixels the repair marches, including
  those whose march comes out empty. The step A census counts only the pixels written.
- `volumetric_fog_repair_census` gains `needs_n=`, `needs_px=` (median per frame), `needs_p90_px=`, `needs_max_px=`,
  `needs_scale=` and `needs_missed=`.
- Cost with the diagnostic on: a 77-slot, 5-fetch full-screen pass inside `fog_route` but outside the three sub-pass
  pairs, about the cost of the repair prologue, ~0.3 ms [I]. With it off, nothing is created and nothing is drawn.
- GPU check [M, pass fixture, two 31x17 layouts]: needs 255/255/0/255 (layout A at 2 and 4, layout B at 2 and 4) equal
  the CPU twin; written 125/125/0/128 equal the twin's non-empty marches.

**Fixture** (bottle X3, `wine_lock.py`; `fog_density_shader_run.py build/run/check --reference /tmp/x3-run67-fog-ref
--variant-reference /tmp/x3-run76-fog-ref-far24 --scale4-reference /tmp/x3-run77-fog-ref-scale4`) [M]:

- Default unchanged. 30/30 default gates pass, including `pass_off_bit_identical` (11/11 hashes). Against 638b19ad's
  summary, 685 default figures were compared: 683 are equal and 2 differ as expected, both counts the step C cases add to
  the same run (shader fixture checks 35 → 41, generated atlases 32 → 36); the script names those two and exits 0
  (`step_c_fixture_identity_out.txt`). `far24.json` is byte-identical to the committed
  one, and the exporter reproduces the run67 references (153/153 arrays) and the far24 ones (26/26) bit for bit
  (`step_c_exporter_identity_out.txt`).
- Scale-4 reference `/tmp/x3-run77-fog-ref-scale4`: exporter `--march-scale 4` over every pixel of the 64x36 quarter
  grid, reference_sha256 `0d6b8e09f39a…`, cases `fe7c6cf092dc…`, pinned in `fog-density-shader/q4.json`. All 7 `q4_*`
  gates pass:
  - look cases, GPU against host: T max .00077, S max .00044;
  - shaft offset exercised, shadowed fog coloured;
  - repair split at spacing 4: .00049 from its host law and .009 from the offset law;
  - programs under 512 slots with the default fetch counts;
  - the look moves;
  - 35 pass-fixture checks (after the review). They cover quarter frames against the CPU twin (T .00046 / S .00017) and far24 at spacing 4
    (T .00047 / S .00014). Back at 2 the frames are byte-identical, 3 programs are created and the device calls match.
    Spacing 3 is refused, Reset re-creates the target, and injected program and target failures and the shadow-pass
    clamp each keep the half-resolution frame; the double failure after a Reset. The census checks are included.
- Totals: 44/44 gates; pass fixture 177 checks; summary 35.2 KB, q4.json 23.1 KB.

**How far the look moves** [M, `step_c_deviation_out.txt`; synthetic poses, not the flown sector]. Look cases have one
depth class each. The FP16 march images of both spacings are upsampled to the 256x144 screen by the composite's bilinear
law and compared on fogged pixels. This table models the fixture's look-case rays, which sit at march-cell centres (the
fixture's c0 shifts them there, full coordinate s p + s/2), not at the production placement 4q; it measures the coarser
sampling of a one-class field. Only the depth-edge study below uses the production placement (c0 zero, samples at full
pixels s q, the composite and repair programs themselves):

| Case | T max | T mean | S max | S mean | Past .003 | Signed T mean |
| --- | --- | --- | --- | --- | --- | --- |
| A sky (= A depth3) | .0265 | .0012 | .0214 | .0006 | 11.2 % | +.00004 |
| A depth 90000 | .0266 | .0012 | .0217 | .0006 | 11.3 % | +.00004 |
| A stripes / held | .0265 | .0012 | .0146 / .0154 | .0007 / .0005 | 11.5 / 11.1 % | +.00004 |
| A shadowed | .0265 | .0012 | .0113 | .0004 | 11.0 % | +.00004 |
| B sky | .0227 | .0013 | .0063 | .0003 | 9.9 % | +.00003 |

For depth edges, the fixture draws a station-like layout on the same screen: a hull, a nearer module across its edge, a
far hull, struts 1-6 px wide over sky and over the far hull, and a 2-px cable. Each spacing is drawn the production way
(FP16 march, composite, repair into FP32 over scenes 0 and 1, which yields S and the tinted T^k) and compared with the look
marched at every full pixel:

| Pixels (fogged) | n | 2 vs truth: T / S max, past .003 | 4 vs truth: T / S max, past .003 | 4 vs 2: T / S max, past .003 |
| --- | --- | --- | --- | --- |
| class edge band (within 4 px of sky/geometry) | 5796 | .039 / .016, 9.1 % | .126 / .053, 34.7 % | .087 / .037, 29.5 % |
| geometry depth edge band | 261 | .0009 / .0002, 0 | .0014 / .0004, 0 | .0013 / .0004, 0 |
| off band | 9986 | .012 / .005, 1.5 % | .035 / .010, 10.9 % | .024 / .007, 8.4 % |
| repaired at 4 | 78 | .0007 / 0 | 0 / 0 | .0007 / 0 |

Pixels needing repair (host twin): 222 at 2 and 300 at 4, i.e. 0.60 % → 0.81 % (1.35x). The GPU repair writes 52 → 78
of them.

The repair itself is exact; the look moves on the pixels the class law still serves. The worst are sky pixels beside a
thin feature (a cable or a hull corner) whose nearest sky samples sit up to 4 px away across a sky fog gradient: T error
.126 at 4 against .039 at 2, and the same pixel is the worst at both spacings. Elsewhere the change is unbiased (signed
mean T +.00004). Under TAA the per-frame jitter moves the ray within the pixel, not within the 4x4 cell, so the result is
a fixed softening of fog detail near silhouettes and at cloud edges, not noise [I].

**Expected saving** [I]:

- The march costs ~8.9 ns per marched pixel (run280). A quarter of the pixels is ~1.15 ms instead of 4.5-4.7 ms net at
  40 bins, or ~0.85 ms with 24 far bins.
- Composite and the repair prologue are unchanged (~0.3 ms each).
- Repaired marches rise to about 1.5x today's 62-216 written pixels, well under 0.1 ms.
- Net: ~3.4 ms of GPU time at 1080p, ~3.6 ms together with step B. At the CPU-bound stand this is headroom, not FPS.

**Flight plan (one user flight, `--gpu-sync-timing`).**

- Setup: the same save and stand as Run 75 C (run280, fogged sector idx 2). Two launches with everything else equal:
  `--fog-march-scale 2`, then `--fog-march-scale 4` (far bins at the default 40). In each, two still 300-frame windows,
  then two while turning in place; host idle.
- Read per window:
  - `fog_march` median and p90. Target [I]: ~1.4-1.6 ms including the 0.264 floor, down from 4.79-5.01.
  - `fog_composite` and `fog_repair` as controls; repair may rise by the extra marches.
  - `taa` and `engine` as controls.
  - `volumetric_fog_repair_census` `needs_px` at 2 and at 4, with `needs_scale`.
  - `volumetric_fog_march_scale scale=4 refused=none` in the second log.
  - In both launches the census quad adds ~0.3 ms to `fog_route`.
- Look check in both launches: station hulls, struts and cables against fog and fogged sky (the measured worst case is
  sky pixels beside thin features), and cloud and shaft edges while turning (softer detail, 4x coarser lookup cells).
- Accept when the user sees no difference at the stand and while turning, and `fog_march` drops by at least 2 ms. The
  default then flips in a later candidate, with the accepted-look hashes re-pinned. Done: the default is 4 since Run 77 C2
  (section "Step C default flip").

## Run 77 C (run289 scale 2, run290 scale 4; 2026-09-24): step C at 5120x1440, the rings

First sessions at 5120x1440 under `--gpu-sync-timing` (medians of the per-window medians, measured;
[triage](../../verification/results/run289-290-march-scale/)): `fog_march` 8.99 ms at scale 2 (2.0x the 1080p net,
against 3.56x by pixels) and 2.68 ms at scale 4, a saving of 6.3 ms; `fog_composite` / `fog_repair` 1.15 / 1.16 at both;
`fog_route` 13.59 -> 7.21; TAA stage 8.81 (mask 2.95 = tests 1.51 + x 0.68 + y 0.67, box 2.21, resolve 2.81 still /
3.49 turning, copy 0.02 = the S1 fold at its floor); engine 7.5; serialised dt 45.9 -> 39.0 ms; the leaf-pass GPU wait
sum 29.5 -> 23.1 ms, so the frame is GPU-bound at scale 2 and close to it at scale 4 (inferred). Needs census 205 px
(28 ppm) at scale 2, 556 px (75 ppm) at scale 4.

Look: the user reports transparent moving rings ("oil rings on water", the fog's own colour) at scale 4 only. The
captures show no interpolation seam at the 4-px sample columns in either run (second-difference phase ratio
0.99-1.03, no autocorrelation peak at lag 4 or 8). Inside a diagonal sky band (a light shaft, inferred) the pre-TAA
HDR carries a lattice that changes per frame; its autocorrelation peaks are multiples of 4 px at scale 4 and of 2 px at
scale 2 (weaker), and all lattice vectors are near-periods of the shaft-offset noise cell (`fog_density_field_inc.h`
245-246, keyed per march cell, shifted each frame by `fog_look_math.h:107`). So the rings are the shaft-offset noise
at 4x4-px cells, not the density upsample; the repair pass cannot reach open sky. Ranked remedies: `SHADOW_JITTER=0`
(free, brings back the shaft comb), a per-frame jitter of the quarter grid (the resolve can integrate it since fog runs
first, but 4 px exceeds the 3x3 clamp), scale 3 / a 4x2 target (refused by the `#error` today, ~4.0-4.5 ms), a
bicubic or bilateral upsample (targets seams that were not found). Decision: scale 2 stays the default; Run 77 C2
(scale 4 with and without the jitter, `--taa-debug`) decides between decoupling the noise cell from the march cell
(key the shaft noise per 2-px or per pixel inside the scale-4 march) and a scale-3 variant. Superseded by Run 77 C2: the
user accepted the scale-4 look and the default flipped (next section).

## Step C default flip (2026-09-24): march spacing 4 is the default

Decision: the user accepted the scale-4 look at 5120x1440 in Run 77 C2, after Run 77 C measured `fog_march` 8.99 -> 2.68 ms
(-6.3 ms) and `fog_route` 13.59 -> 7.21 ms there (serialised dt 45.9 -> 39.0 ms). Spacing 4 is the default; 2 stays as the
opt-out. Uncommitted worktree on c45c5dc0, not installed. Ledger: [volumetric-fog.md](../verification/volumetric-fog.md),
same date. [M] measured, [I] inferred.

What changed:

- `fog_look_math.h`: `fog_march_scale_default` is `fog_march_scale_quarter` (4); the new `fog_march_scale_half` (2) is
  what every refusal of 4 falls back to (shadow pass, programs, target). `FogDensityConfig::march_scale` defaults to 4.
  No program changed; the shader records are untouched.
- DLL (`capture.cpp`, stored range only): absent, `4` or an invalid value draw 4; exactly `2` draws 2. The shadow pass
  clamps 4 to 2 and logs `volumetric_fog_march_scale scale=2 requested=4 refused=shadow_pass`, also when the variable is
  absent. An invalid value now keeps the default 4, not 2.
- Launcher: `X3M_FOG_MARCH_SCALE` is always written: the given value, else 4. With `--fog-shadow-pass on` and no value the
  launcher writes 2 and prints one line, `fog march scale: 2 (the default 4 is clamped to 2: --fog-shadow-pass on keeps
  spacing 2)`. An explicit 4 still needs the stored range and is still refused with the shadow pass. The default is also
  written under the legacy range, which never reads it.
- FogPass fixture: its base frames stay at spacing 2 (the CPU twin, the grid and the shared machinery), set explicitly.
  The new check `march_scale_default_is_quarter` pins the config default, and the quarter section sets 4 explicitly.

Fog fixture regroup (`fog_density_shader_run.py`): 44 gates before, 52 after.

- Default set: the 25 spacing-independent gates (parity, the grid, motes, the pass fixture, slots), and the default
  look against the scale-4 reference (`--scale4-reference`, now required):
  - `look_cases`, `look_shaft_offset_exercised`, `look_shadowed_coloured`, `repair_shaft_lookup`;
  - `pass_off_bit_identical`, re-pinned to the 11 scale-4 look hashes;
  - `q4_programs`, `q4_moves_the_look`, `q4_pass_fixture` (record `q4.json`);
  - `far24_*` (7): far24 at spacing 4, against the new reference `/tmp/x3-fog-ref-far24-scale4`
    (`--far24-scale4-reference`, reference_sha256 `c4a023d6067b…`, cases `9f3df2cb5643…`; record `far24.json`).
- Scale-2 variant `s2_*` (12, record `s2.json`): the former default look against `/tmp/x3-run67-fog-ref`, with its
  11 accepted-look hashes as `s2_pass_off_bit_identical`, and far24 at spacing 2 as `s2_far24_*` against
  `/tmp/x3-run76-fog-ref-far24`.
- The shader fixture takes a fourth cases file, far=24 scale=4. It draws those cases with the `*_look_far24_q4` programs,
  plus the look repair split with them (`REPAIR_SHAFTS_FAR24_Q4`).

Result [M] (bottle X3, `wine_lock.py`, shader child 72 s, pass child 64 s): PASS 52/52. Details:

- Records against the parent commit's
  ([step_c_default_flip_identity_out.txt](../../verification/results/fog-gpu-cost/step_c_default_flip_identity_out.txt)):
  - the s2 look and repair equal the old default's figure for figure (153 + 13 leaves);
  - the default look and repair equal the old q4 record's (153 + 13);
  - the s2 far24 equals the old far24.json (359 leaves);
  - parity and grid are unchanged (48 + 105);
  - the s2 hashes equal the old pins (scale-2 frames byte-identical);
  - every one of the 44 old gates has a passing successor.
- far24 at spacing 4:
  - look T max .00083, S max .00050;
  - repair .00049 from its host law and .0090 from the offset law;
  - the offset lookup moves S by .0070 on 33 rays;
  - GPU T moves up to .030 from the 40-bin spacing-4 look.
- Pass fixture 178 checks (177 + 1); shader fixture 46 checks.
- The spacing-4 look leaves 10 of 2,304 rays out at a noise wrap (4 of 576 at spacing 2). The host test allows 1 % per
  case.

Not changed: the programs, the in-game look (the user's C2 acceptance is the evidence). The route bridge was not re-run
here; its harness takes the `FogDensityConfig` default, now 4, except the shadow A/B, which compares at spacing 2 (the
grid's only spacing). It passes at the scale-4 default since 2026-09-24 (volumetric-fog.md, "route bridge at the scale-4
default").
