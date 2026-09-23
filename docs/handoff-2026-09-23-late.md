# Handoff 2026-09-23 (late) — Run 68/69 flown, Run69 candidate, merged-LOD atlas

Replaces the [morning handoff](archive/handoff-2026-09-23-morning.md); its operating rules
still bind (never launch the game; every Wine command through
`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py`, never `--help` to a runner;
one candidate owner; install only with `tools/manage.py install --bottle X3 --dll-source <dll>`;
no host suite or build while the user's game is up — run257's timings were contaminated by one;
native Windows is a required, unverified target). Installed-build identity lives only in
[status](status.md). Run authority: [user-runs.md](verification/user-runs.md).

## State

- **Installed:** Run69 DLL `70abe438…` from `5a11ad00` ([status](status.md)); rollback Run68
  `/tmp/x3-run68-candidate/build/d3d9.dll`. Game data: the merged-LOD pilot overlay
  `addon/05.cat` (currently the glow-area 70 build `87bf16cd…`; the atlas build is on main and
  replaces it at the next install, [record](../verification/results/lod-overlay-pilot/install.json)).
- **Run 70 is queued:** A = SETA hull blur with `--taa-motion-weight 0.8,2,8`; B = fog dust
  motes 2048 with the Ctrl+Alt+F11 A/B; C = the atlas overlay at the stand (after its install).

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

## Next steps

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
