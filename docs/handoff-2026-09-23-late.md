# Handoff 2026-09-23 (late, updated at night) — Run 68–70 flown, Run70 installed, LOD 0 atlas overlay, Run 71 queued

Replaces the [morning handoff](archive/handoff-2026-09-23-morning.md); its operating rules
still bind (never launch the game; every Wine command through
`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py`, never `--help` to a runner;
one candidate owner; install only with `tools/manage.py install --bottle X3 --dll-source <dll>`;
no host suite or build while the user's game is up — run257's timings were contaminated by one;
native Windows is a required, unverified target). Installed-build identity lives only in
[status](status.md). Run authority: [user-runs.md](verification/user-runs.md).

## State

- **Installed (night):** Run70 DLL `a773e9f2…` from `0bfa11ac` ([status](status.md)); rollback
  Run69 `/tmp/x3-run69-candidate/build/d3d9.dll`. Defaults now: `--taa-motion-weight 0.7,2,8`
  under TAA with an age program; fog dust motes on under the stored range at `1300,3,128` with
  MAX_PX 8 (`--fog-dust-motes 0` opts out). Game data: the merged-LOD overlay `addon/05.cat`
  `d29b0c88…` / `05.dat` `5fd877c1…` (LOD 0 meshes with diffuse/light/bump/specular atlases,
  ships 2 draws, outpost 4, collision vanilla; [record](../verification/results/lod-overlay-pilot/install.json)).
- **Run 71 A is queued and ready** ([user-runs.md](verification/user-runs.md)): the LOD 0 atlas
  overlay at the stand; the question is whether the switch at 80 / 150 px is visible at all.
- **An agent may still be running at the compaction:** the fleet-wide overlay eligibility census
  (`tools/analysis/lod_batch_census.py`, worktree `.claude/worktrees/agent-af6a3758990f2ab5f`,
  outputs under `verification/results/lod-overlay-batch/`); merge its staged work (`git -C <wt>
  add -A` first) after a factual review.

## Night verdicts (Run 70, all measured; ledgers linked)

- **A (run262 at 0.8, run263 at 0.7):** the motion weight halves the SETA hull blur at ≥ 8
  px/frame (σ 1.4 → 0.7–1.0, E ratio ×1.7–4) for ×1.1–2 ripple there, unchanged below 2 px/frame;
  the user prefers 0.7 → default ([temporal-resolve.md](verification/temporal-resolve.md) "Run 262").
- **B (run264, B2):** motes cost nothing (+12 calls, +0.08 ms); 2048,4 was "snow" (~110 discs of
  6–8 px); 1300,2 liked, settled on size 3 → default on ([volumetric-fog.md](verification/volumetric-fog.md) "Run 264").
- **C (run265), C2 (run268):** the atlas overlay in effect; the outpost pair matches the fine
  model in sun (100 %) and non-sun (94 %); the NULL specular slot bound a bright placeholder →
  specular atlas; the remaining Titan step (lit windows/exhausts ×1.5) was not reproduced offline
  as mip bleed → the coarse record now uses the LOD 0 mesh (tile-aware mips kept)
  ([engine-frame-time.md](architecture/engine-frame-time.md) "Run 265", "Run 268").
- Weapons never read the drawn LOD; collision comes from the last record = the pad = the original
  coarsest record ([lod-child-hide.md](reverse-engineering/lod-child-hide.md)).

## Verdicts since the morning (all measured; ledgers linked)

1. **Run 68 A (run254):** exit reset accepted, 3–12 px genuine dark-sky share 7 % / 6 % (was
   27 % / 34 %); strict + band + exit are the defaults. SETA station blur = history resample
   softening (σ 1.0–1.4 px above 1 px/frame) → `--taa-motion-weight`
   ([temporal-resolve.md](verification/temporal-resolve.md) "Run 254").
2. **Run 68 B (run255):** 466 draws at the stand; the pilot placement rule corrected
   ([engine-frame-time.md](architecture/engine-frame-time.md) "Run 255").
3. **Run 68 C (run256):** the fog shadow-pass toggle works (no notice by design); no visible or
   median-cost difference → the pass stays default off ([volumetric-fog.md](verification/volumetric-fog.md) "Run 256").
4. **Merged-LOD pilot, Run 69 A–D (run257/258/259–260/261):** placement fixed twice
   (pad, then compact: C at index 1, no `0x100000` DEFAULT-technique flag); engine glows are the
   exhaust materials' light maps (glow collapse); 80 / 150 px accepted; "no sun lighting" =
   light-map self-illumination lost (−72 %) plus the dominant material's low diffuse strength
   (−23 %) → glow-area 70 + synthesized material (94 % / 73 % of fine); weapons never read the
   drawn LOD ([lod-child-hide.md](reverse-engineering/lod-child-hide.md)). Atlas collapse built:
   one material per coarse record with diffuse/light/bump atlases, 1 draw per ship, 2 for the
   outpost, 33 MiB textures for four bodies ([merged-lod-feasibility.md](architecture/merged-lod-feasibility.md) "Overlay tooling").

## Merged since the morning (reviewed; resolve and GPU changes twice)

Sky-history defaults; cleanup batch 6 (four TAA variants, 2,761 lines, five kept programs
byte-identical); `--taa-motion-weight` (gate = min(parallax, screen motion); far_camera 505/512;
fixture 712/528 then 744/546 with the motes rows); `--fog-dust-motes` (last fog stage, unrouted,
+12 calls, cut-aware streak; fog pass 117 checks, bridge 36,333/36,927, temporal 744/546);
lod_overlay.py: pad → compact placement, glow / glow-area / atlas collapses, `--replace`, running-
game guard, synthesized material, first-use tangent records; body_materials.py, atlas_census.py,
lod_atlas.py.

## Decisions (user, 2026-09-23)

- Fog shadow pass stays off (no win). Strict sky + exit reset are defaults.
- Merged-LOD: switch sizes 80 px ships / 150 px stations; light maps kept via the atlas rather
  than per-material groups; DXT1/DXT5 atlases; bump atlas yes, specular NULL; no normal-map
  concern at these sizes. lod-scale stays off (the overlay is the lever).
- Dust motes are an addition to vanilla stardust; a stardust dimming hook only if the flight
  shows doubling.

## Next steps (night)

- **Triage Run 71 A** with `verification/results/run268-atlas-spec/` scripts (pair_diff268.py,
  view_delta.py) and `run257-pilot/node_census.py`: expect 2 draws per ship / 4 for the outpost at
  LOD 1 below the switch, the atlases bound (s0 diffuse DXT1, s1 bump, s2 specular, s3 light),
  the outpost pair below 150 px; if the switch is invisible or only texture resampling, accept.
- **Fleet batch:** from the census (rule proposed: T_pad = max(80, 2.5 × T₁) ships / 150 stations,
  cap 200; atlas sized by texels/px at the real screen width; resident budget per sector for the
  32-bit process), add a batch mode to lod_overlay.py, build the heavy bodies of the flown sectors,
  one flight in a busy sector; then decide `--lod-scale` (stays off).
- **Later (user interest):** an atlas material shader for LOD 0 at all distances (tile id per
  face, per-tile wrap in the pixel shader, one atlas per material library): design note first.
- **Handoff hygiene:** rewrite this file as `handoff-2026-09-24.md` at the next pause; the
  `grep -c` pitfall: it exits 1 on a zero count, so never put it inside an `&&` chain before an
  install (it silently skipped one install today).

## Next steps (as of the late handoff, superseded where the night list says so)

- **Install the atlas overlay** when the game is not running:
  `python3 tools/analysis/lod_overlay.py --install --replace --collapse atlas 'ships/argon/argon_TL=80' 'ships/argon/argon_M2=80' 'ships/argon/argon_M1=80' 'Stations/others/military_outpost_middleb=150'`
  (expected `05.cat` `f3f607fa…`, `05.dat` `54acfc47…`), update the install record and mark
  Run 70 C ready. Flight checklist in the feasibility note (diffuse, lights, glows, bump relief,
  the outpost's NULL specular, DXT5 light-map header, collision unaffected).
- **Triage Run 70:** A with `verification/results/run254-exit/hull_sharp.py` / `hull_blurfit.py`
  on the SETA bursts against run254 (σ must drop at ≥ 8 px/frame; pan and comoving rows
  unchanged) and the user's shimmer verdict → default or `0.7,2,8`; B by the user's look, the
  `volumetric_fog_frame` mote fields, fps both ways, the view-switch flash; C with
  `draw_accounting.py --ladder` and `run259-lighting/outpost_pair.py` on a paired capture
  (expect 1 draw per ship, 2 outpost, lighting within a few % of fine).
- **After the pilot:** scale the overlay to the other heavy bodies (draw_accounting names
  them; a texture budget is needed: ~8 MiB per 2048² body), then decide lod-scale.
- Carried over: `run_ownership_integration.py` configures with Release; bloom manifests'
  generator hash awaits a Wine `--check`; the two launcher host modules fail only under the
  installer lock; the route-bridge check count varies with density-fill timing (36,333 vs 36,927).

## Worktrees

`.claude/worktrees/agent-*` from today are merged and may be removed on request, except
`agent-a2b067e4005541a7a` (GPU timer). `/tmp/x3-run5x/6x-candidate` hold the retained DLLs.
