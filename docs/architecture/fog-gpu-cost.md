# Fog route GPU cost at 1920x1080 (design note, 2026-09-23)

Question: how to cut the GPU cost of the stored-density fog route without changing the accepted look
(L2 law, density scale 1.0x, 22.5 km fade, dust motes 1300,3 / MAX_PX 8, shadow pass off). Plan for
ratification; nothing here is implemented. [M] measured, [I] inferred.

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
