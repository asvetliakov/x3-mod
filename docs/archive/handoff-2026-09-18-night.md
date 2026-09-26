# Handoff 2026-09-18 (night)

Short current handoff; [status](../status.md) is the authoritative summary, the
[goals table](../goals.md) the acceptance state, [user-runs.md](../verification/user-runs.md)
the run queue. Previous handoffs: [archive/handoff-2026-09-18-evening.md](handoff-2026-09-18-evening.md),
[archive/handoff-2026-09-18-morning.md](handoff-2026-09-18-morning.md).

## Where things stand

- **Installed:** run42 candidate `1a5dd46c…` from main `903be726`
  ([build](../../verification/results/run42-candidate-build.json),
  [install](../../verification/results/run42-candidate-install.json)), through
  `python3 tools/manage.py install --dll-source <dll>` (always). Rollback run41
  `b6ea8569…` in `/tmp/x3-candidate-Iv6Z6G/rollback`; run40 `c47f039c…` in
  `/tmp/x3-candidate-gHUSU7/rollback`.
- **Run 42 is queued** (§42): A telemetry-only (`--loop-phases --game-phases
  --pass-phases --residual-phases --telemetry-draw`) in the run125 24 fps area,
  empty space, and the run117 station; B the solar plant and an antenna (fade
  route fix by eye, F8 each); C `--cull-census` F8 at the run117 ≈ 900-draw
  view; D View Distance Very High vs High by FPS-overlay reading (no code).
- **User configuration:** original hull shading; launcher defaults now include
  `--hull-lightmap-gain 4` (Ctrl+Shift+F4) and guide lights on the effects gain
  2 (Ctrl+Shift+F6); shadows five cascades at 2048², linear receiver depth,
  slope margin 0.2, K 1.5 for the corvette; FPS overlay Ctrl+Alt+F7. Units:
  1 m = 5 u. `--original-fill` 0.02–0.04 still to be flown.

## What was done on 2026-09-18 (night)

1. **Run 41 read** (run123/124/125/126/128): distant flicker gone under the
   linear encoding (now the only one); shadows ≈ 1.4 ms; ≥ 30 ms frames are
   engine view submission; retention clean; light-map gain accepted at 4.
2. **Solar-panel shimmer** (run125) = the fade-band route inert under original
   shading since 2026-09-15 (cutout-caps probe gated on linear materials);
   fixed with fixture; audit found no second functional gate of that class
   (only the `fade_route_mode` line, fixed). Bolts on the corvette go through
   the shared effects PS and are gained by the effects gain, not the bolt route.
3. **Engine frame time** design ratified ([note](../architecture/engine-frame-time.md)):
   24 fps area = 18 ms game-side pre-render episode (owner unknown); busy
   station = 22/32 ms view submission at 23.7 µs/draw; census (world-scale
   proxy) 50–65 % of draws under 2 px (≈ 12 ms); cull census hook installed to
   size it from the engine; `--game-phase-threshold-ms 20`, `--telemetry-draw`.
4. Hotkeys regrouped (F4 light map, F6 effects + guide lights); telemetry
   counters `apply_us`/`flip_c<k>` proved useful (period-2 clean).

## Next steps

1. Run 42: A → attribute pre_render (loop/game phase rows; then disassembly of
   the owner); C → `tools/analysis/cull_census.py` bucket table and the
   projected-size lever's real saving; D → LOD-step draw delta; B → shimmer gone?
2. Then choose among: pre_render patch (if a redundant scan is named),
   projected-size culling stub at `0x0047d42f` (if the census confirms),
   fractional LOD bias, proxy per-draw hook trimming (lazy RT mode, gate reads).
3. Open shadow items: ±0.2 u per-frame along-ray receiver offset; own-ship C0
   re-roll under a moving ship; retention age cap from the run119/125 census;
   `sort_us` hook site (≤ 1 ms). `--original-fill` value flight.

## Decisions (do not reopen without a reason)

- Original hull shading; no PBR/blanket conversion/selective exposure; no
  shadow-only lightening (fill is the ambient floor); emitters via source gains
  (effects 2 incl. guide lights, hull light maps 4); no engine-side state
  filter, instancing, draw sorting, pass replay or threading (closed with
  numbers in the engine note); busy frame ≈ 22 ms at ~500 draws accepted;
  media-cue cache on; video parked; point-light root admission off.
- Shadows: proxy replay + cascades; positional sun; far cascades back-face;
  five cascades at 2048², half-extents 250/1,500/7,500/37,500/150,000 (a 60 km
  box, 42 km corner, 120 km deep); linear receiver depth only; slope margin 0.2
  on; K 1.5; static-only rule off.

## Housekeeping

- Snippet mocks and the converted-corpus pins drift at every member/fragment
  change (the run41 build caught 15; the run42 verify caught 1).
- `/tmp/x3-bottleX3-run*` preserved sessions; `/tmp/x3-candidate-*` rollback DLLs.
- 62 stale worktrees under `.claude/worktrees/` (unmerged/dirty, 4.5 GB) from
  earlier sessions; today's merged ones are removed.
