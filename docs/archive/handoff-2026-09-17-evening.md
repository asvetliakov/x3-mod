# Handoff 2026-09-17 evening (archived from handoff-2026-09-18.md)



- **Installed:** run38 candidate `5b4be52e…` from main `e575136`
  ([build](../../verification/results/run38-candidate-build.json),
  [install](../../verification/results/run38-candidate-install.json)); rollback
  run37 `61725145…` in `/tmp/x3-candidate-Ek2A0u/rollback`. Every session log opens with
  `proxy_identity` / `proxy_options` / `proxy_environment` (FEX_*, WINE*,
  CX_*), `clock_anchor`, `loaded_module` (with `image_size`, `stamp`,
  `wine_builtin`); the launcher's first stderr line records the command,
  the `--dll` override string and the experiment options.
- **User configuration:** original hull shading (no linear materials).
  Defaults unchanged: camera 0.5°/0.50, EV ceiling +1.3, mip bias -0.5,
  sharpen 0.75, fill 0.05 (linear only), bolts `--screen-emission-additive 2
  --screen-emission-additive-alpha 0`, engines `--emission-source-gain 2`,
  `--bloom-source-clamp 1.0`, `--state-shadow auto`, `--media-cue-cache on`.
  Decoder runtime `--voice-decoder /tmp/x3-wma-plugin-v4`.
- **Run 38 is queued** ([user-runs.md](../verification/user-runs.md)): A wide
  single shadow map (extent 1,500, depth half 3,000, 4096², cap 512, clamp 4
  texels), A2 own-ship baseline at 4096², B `--residual-phases` at the busy
  view, C `--hull-emitters` bracket. Runs 28–37 are complete (run74–110).
- **Concurrency:** the user raised the limit to six to eight agents; one Wine
  queue (`wine_lock.py`), one candidate owner, one writer per file set.

## Results of 2026-09-17 (runs 35–37)

1. **Shadows are in game.** Original-program share producer, lane latch
   without linear materials, cutout pairs admitted, one-cascade depth replay
   with casters chosen by geometry (draw-range vertex extents against the map
   box, cap), scene-end apply quad (`--sun-shadow-apply`, default off). Run
   37 B (run109): first visible shadows (hull from station parts, ship on
   nearby parts); station-on-station shadows were limited by the 250-unit
   box; the box, depth range, cap and a world-unit bias with a texel term
   are launcher options now ([contract](../architecture/legacy-sun-application.md),
   [gates](../architecture/shadow-replay-gates.md),
   [ledger](../verification/directional-shadows.md)). The run106 "no shadow" was
   a backlit view; the apply path was proven correct offline with the CPU
   twin (`verification/probe/sun_shadow_apply.py`, reads real captures via
   the `sun_shadow_apply_params` line).
2. **Cascade design ratified** ([shadow-cascades.md](../architecture/shadow-cascades.md)):
   configurable count and extents, intended 250 / 1,500 / 7,500 / 25,000 all
   at 4096², shared depth, far cascade on alternate frames with distance
   fade, per-pixel selection by sun-space extent with a blend band; near-band
   choice (per-object own-ship map vs screen-space contact shadows) after run
   38; survey of shipped space games in
   [space-shadow-survey.md](../architecture/space-shadow-survey.md).
   Implementation starts after run 38 calibrates the single wide map.
3. **Busy frame closed at ~22 ms** ([note](../architecture/effect-pass-replay.md),
   decision section): builtin D3DX +32 % (run104/105), FEX TSO off no change,
   wined3d CSMT off +46 % (run107/108); native BeginPass fixture: D3DX's own
   walk is 1.5 µs of the 6.6 with nothing to dedup; pass replay not pursued.
   Left: `--residual-phases` (run 38 B) may show the per-draw
   `GetTechniqueByName`+`SetTechnique` as a cacheable engine cost; the
   engine-side state filter stays declined at ≤ 1 ms.
4. **Video parked:** the H.264/AVI avatar file (tool
   `tools/media_transcode.py`, original restored) froze at the same
   post-create stage as MPEG-1 with zero blits witnessed
   (`media_video_blit`, run110): codec-independent Wine `amstream`/DirectDraw
   block; the six remaining candidate sites are in
   [media-cue-playback.md](../reverse-engineering/media-cue-playback.md) §8.
   Music and speech unaffected. `media_cue_enter` line merged.
5. **Emitter plan phase 3 wired** (`--hull-emitters`): whole-output gain on
   the twelve ONE/ONE hull originals (the art is in the diffuse slot, the
   lightmap slot is black); first bracket is run 38 C.
6. **Tooling:** `--d3dx`, `--fex-tso`, `--wined3d`, `--residual-phases`
   (arena 24,576 B), `loaded_module` image fields, `proxy_environment`,
   snapshot tool accepts `rg32f` and the sun-map dumps, effect state
   classification (`effect_passes.py`), shader-fingerprint fallback design
   ratified (not scheduled), host suite green (2,101 tests, census repair).

## Next steps, in the order recommended

1. Read run 38: A/A2 for the cascade budgets and the near-band decision; B
   for the engine split; C for the emitter bracket. Then implement the
   cascades per the note (Fable; replay pass, candidates, apply quad, twin
   and fixtures), with the far-cascade fade and the near-band choice.
2. If run 38 B shows the technique lookup large: a trampoline caching the
   technique handle per effect and name (engine-side fix, Fable, site RE in
   effect-pass-loop.md §7).
3. Emitter phase 3 tuning from the bracket; then position-light population
   census if some lights did not brighten.
4. Optional, user decision: engine-side state filter (≤ 1 ms); video entry
   stamps at the six `amstream` sites; shader-fingerprint fallback
   implementation.

## Decisions (do not reopen without a reason)

- Linear hulls optional and off; no blanket conversion, PBR, selective
  exposure or code-value fill; emitters above 1.0 via source gains.
- Proxy over engine trampolines for rendering; trampolines only for
  engine-side fixes (stamps, media-cue gate, a technique cache if measured).
- No engine-side state filter (≤ 1 ms), no proxy instancing or draw sorting,
  no pass replay; busy frame accepted at ~22 ms on this backend.
- Media-cue negative cache default on; v5 runtime parked; video parked.
- Point-light root admission default-off and out of the run command.
- Cascades: near cascade 4096² (user), far cascade 25,000 units (user).

## Housekeeping

- The extracted-snippet mocks drift at every member addition; run the mock
  tests with each source commit (`test_linear_material_live`,
  `test_motion_wrap_states`, `test_capture_bloom_lifetime`,
  `test_motion_hdr_scene`, `test_linear_cutout_contract`,
  `test_capture_device_creation`). Shared-driver edits under `tools/shaders/`
  re-drift the nine bloom manifests' tool hash (regenerate; words unchanged).
- `/tmp/x3-candidate-*` keep the rollback DLLs; `/tmp/x3-bottleX3-run*` are
  the preserved sessions; `/tmp/x3-media-transcode` holds the H.264 files.
- Finished agent worktrees under `.claude/worktrees/` are removed after
  merge; 60 candidate worktrees under `/tmp` and the system temp remain.
