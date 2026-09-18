# Handoff 2026-09-18 (evening)

Short current handoff; [status](status.md) is the authoritative summary, the
[goals table](goals.md) the acceptance state, [user-runs.md](verification/user-runs.md)
the run queue. Previous handoffs: [archive/handoff-2026-09-18-morning.md](archive/handoff-2026-09-18-morning.md),
[archive/handoff-2026-09-17-evening.md](archive/handoff-2026-09-17-evening.md).

## Where things stand

- **Installed:** run41 candidate `b6ea8569…` from main `e29d6399`
  ([build](../verification/results/run41-candidate-build.json),
  [install](../verification/results/run41-candidate-install.json)), through
  `python3 tools/manage.py install --dll-source <dll>` (always; a direct copy
  leaves the ownership manifest stale). Rollback `c47f039c…` (run40) in
  `/tmp/x3-candidate-gHUSU7/rollback`; older builds in `/tmp/x3-candidate-iK0cir`,
  `-eBwRHq`, `-wRyHZV` rollback dirs.
- **Run 41 is queued** (§41): A/A2 the run117 station with the old and new
  receiver encoding (`--sun-shadow-receiver-depth linear`), B the corvette at the
  run119 part with and without the slope margin (`--sun-shadow-bias-slope-texels 0`),
  C the run122 station at `--hull-lightmap-gain 4`. FPS overlay on in all
  (Ctrl+Alt+F7). Shadow set now 2048² maps; K 1.5 for the corvette.
- **User configuration:** original hull shading; defaults unchanged (camera
  0.5°/0.50, EV +1.3, mip bias −0.5, sharpen 0.75, bolts additive 2 / alpha 0,
  engine gain 2, bloom clamp 1.0, media-cue cache on); decoder
  `--voice-decoder /tmp/x3-wma-plugin-v4`. Units: 1 m = 5 u. The user plans
  `--original-fill` 0.02–0.04 later (fill, not a shadow lift, is the ambient floor).

## What was done on 2026-09-18 (evening)

1. **Run 40 read** (run117 A, run118 A2, run119 B, run121/122 C); outcomes in
   the run table. Period-2 blink fixed (23.8 % → 0.79 %). 2048² accepted.
   K 1.5 keeps the corvette in C0 with margin; retention clean. The 150,000
   extent is a half-extent (60 km box, 42 km corner, 120 km deep).
2. **Far-station flicker = RT2 fp32 z/w receiver precision** (13.6 u per ULP at
   37 km; ±1 ULP re-rolls 9–16 % of far-cascade pixels on single-sided faces).
   Design ratified ([note](architecture/shadow-receiver-depth.md)): RT2
   A32B32G32R32F with clip w in `.b`, apply reads `.b`; gated
   `--sun-shadow-receiver-depth {device,linear}`, default device this flight,
   flip after. Witness on run117 captures: z/w 9.1/14.2/15.7 % → w 0/0.004/0 %.
3. **Near flicker (run119) = sun-grazing plane** whose receiver-plane
   extrapolation amplified fp32 noise: slope-scaled margin in the cascade apply
   (default 0.2 texel, program 509/512 slots), 10.6 → 1.4 % per ULP.
4. **Hull emitters ≠ windows.** Windows are the light-map term added inside 100
   of 108 opaque hull programs ([RE](reverse-engineering/hull-self-illumination.md));
   `--hull-lightmap-gain G` multiplies it (plain, fill and sun-share variants;
   Ctrl+Shift+F4 pairs it with the guide-light gain).
5. **FPS overlay** (`--fps-overlay`, Ctrl+Alt+F7) on the bitmap notice;
   **telemetry** `apply_us=`, `flip_c<k>=`, `period2_c<k>=`; tools
   `shadow_map_diff.py` (basis-aligned map diffs), `shadow_receiver_reroll.py`,
   retention summariser five-cascade fix. Host suite 2,204 OK.

## Next steps

1. Run 41: A vs A2 flicker verdict and `frame_end` delta (expect ≈ 0.2 ms);
   B slope A/B; C windows at gain 4 (choose the gain); the solar-plant leg with
   Ctrl+Shift+F12 (shadows or TAA). Triage with the new counters.
2. If A2 is clean: flip the receiver-depth default to linear and delete the
   device path; ship slope 0.2; set the light-map gain default from C.
3. Open shadow items: the ±0.2 u per-frame along-ray receiver offset (game-side?),
   own-ship C0 re-roll 9–11 %/frame under a moving ship, `shadow_map_diff`
   ship-anchored alignment, lane self-test still G32R32F. Retention age cap
   from the run119 census; `--original-fill` value flight.

## Decisions (do not reopen without a reason)

- Original hull shading; no PBR/blanket conversion/selective exposure;
  emitters above 1.0 via source gains (effects, bolts, guide lights, and now
  hull light maps); no engine-side state filter, proxy instancing, draw
  sorting or pass replay; busy frame ≈ 22 ms accepted; media-cue cache on;
  video parked; point-light root admission off; no shadow-only lightening
  (fill is the ambient floor).
- Shadows: proxy replay + cascades; positional sun from the engine light
  array; far cascades back-face; static-only rule off; five cascades at
  2048², half-extents 250/1,500/7,500/37,500/150,000; receiver depth in
  RT2 `.b` (gated this flight); slope margin 0.2 on; K 1.5.

## Housekeeping

- Extracted-snippet mocks drift at every member addition (six mock modules);
  the converted-corpus pins move with every depth-fragment change.
- `/tmp/x3-bottleX3-run*` are the preserved sessions; `/tmp/x3-candidate-*`
  keep the rollback DLLs (`-JPyWEO` holds the superseded e1c4afcc build).
- 62 worktrees remain under `.claude/worktrees/` (unmerged or dirty branches
  from earlier sessions, 4.5 GB); the merged ones from today are removed.
