# Handoff 2026-09-18

Short current handoff; [status](../status.md) is the authoritative summary, the
[goals table](../goals.md) the acceptance state, [user-runs.md](../verification/user-runs.md)
the run queue. Previous handoffs: [archive/handoff-2026-09-17-evening.md](handoff-2026-09-17-evening.md),
[archive/handoff-2026-09-17.md](handoff-2026-09-17.md).

## Where things stand

- **Installed:** run40 candidate `c47f039c…` from main `e8ae3357`
  ([build](../../verification/results/run40-candidate-build.json),
  [install](../../verification/results/run40-candidate-install.json)), installed
  through `python3 tools/manage.py install --dll-source <dll>` — always use
  that (a direct copy leaves the ownership manifest stale and the launcher
  refuses). Rollback `d4d824a4…` in `/tmp/x3-candidate-iK0cir/rollback`; older
  builds in `/tmp/x3-candidate-eBwRHq`, `-wRyHZV`, `-pnXpbE` rollback dirs.
- **Run 40 is queued** (§40): A fighter, five cascades 250 / 1,500 / 7,500 /
  37,500 / 150,000 u (30 km) at 4096² with per-frame telemetry; A2 the same at
  2048²; B the corvette save with retained casters and the adaptive ladder
  (K 1.5); C hull emitters at gain 4 (Ctrl+Shift+F4). Ctrl+Shift+F12 toggles
  the shadows at rest. Session A was flown once as run116 on the previous
  build; re-fly it first.
- **User configuration:** original hull shading; defaults unchanged (camera
  0.5°/0.50, EV +1.3, mip bias −0.5, sharpen 0.75, bolts additive 2 / alpha 0,
  engine gain 2, bloom clamp 1.0, media-cue cache on); decoder
  `--voice-decoder /tmp/x3-wma-plugin-v4`. Units: 1 m = 5 u; engine positions
  are integers × 0.01; the sun is ≈1.57e7 u away.

## What was done on 2026-09-18

1. **Run 38 (run111–114) read.** Root cause of the popping/wrong-direction
   shadows: the sun was read from PS register c4 regardless of program (16 of 38
   programs keep it there). Fixed: per-program register from the constant
   table, latched validated sun; then the engine light position polled
   hook-free (`--shadow-sun-poll`, cross-checked per draw). Technique-cache
   trampoline dropped (0.007 ms/frame measured offline). Hull emitters got
   their own toggle and gain (run 38 C: 2 of 12 programs fired).
2. **Cascades** (N ≤ 5, [extents note](../architecture/shadow-cascade-extents.md)
   set R ratified, then widened to 30 km at the user's request), anchored texel
   snapping, half-texel map lookup, pancaking, extent cache; **caster retention**
   stages 1–2 on the retirement journal ([contract](../architecture/shadow-caster-retention.md));
   **caster pool control** (importance drop order with hysteresis, per-cascade
   records to 4,096; static-only + capital-ship rule available but OFF after
   run116 — no cascade ever hit a cap); **own-ship-adaptive C0 with the sliding
   ladder** for bigger ships; at-rest toggle; telemetry options.
3. **Run 39 A (run115):** camera-following serrated band = the apply quad
   reconstructing receivers half a pixel off the RT2 sample (D3D9 raster rule);
   fixed, sign verified independently; stations at 5–12 km unshadowed → 30 km.
4. **Run 40 A (run116):** band gone, shadows to 30 km; black areas flickering on
   distant lit surfaces = a store/ring static-verdict feedback cycle (period-2
   caster blink) + far-cascade self-shadowing on the compare threshold re-rolled
   by the TAA-jittered receiver + thresholds too tight for far texels + casters
   with the origin behind the camera refused everywhere. Fixed: back-face
   casters in cascades whose texel ≥ 8 u (`--shadow-cascade-backface-from`),
   verdict cycle, per-cascade eps, near gate. In-game proof is the re-fly.
5. Object-lifetime fixture: the ten FX-state failures were the environment
   (FXRSTOR does not reload x87 under FEX); control added, 0 failures. Host
   suite green (2,159).

## Next steps

1. Run 40 A re-fly: is the distant flicker gone; residual = two-sided
   (CULL_NONE) casters, counted `cull_none<k>=`; toggle A/B cost per frame from
   the stride-1 `frame_end`; the ~30 s distant flicker question with per-frame
   data; `shadow_sun_frame` re-derivation rate while moving.
2. A2 (2048² everywhere — the user expects it to suffice), B (corvette: own
   radius, slid set, `camera_state t` vs the ship node to set K; retention
   lingering shadows / crashes), C (hull emitter models via
   `tools/analysis/summarize_hull_emitters.py`).
3. Then: cascade-set defaults from the flights; retention age cap from the
   census; native Windows remains source-only.

## Decisions (do not reopen without a reason)

- Original hull shading; no PBR/blanket conversion/selective exposure;
  emitters above 1.0 via source gains; no engine-side state filter, proxy
  instancing, draw sorting or pass replay; busy frame ≈ 22 ms accepted;
  media-cue cache on; video parked; point-light root admission off.
- Shadows: proxy replay + cascades over engine trampolines; positional sun
  from the engine light array; far cascades back-face; static-only rule off;
  30 km reach with five cascades (a four-map 30 km set would need ratio ≈ 8.5
  and 3 px per texel at every seam).

## Housekeeping

- Extracted-snippet mocks drift at every member addition (run the six mock
  modules with each source commit); `tools/shaders/` edits re-drift the nine
  bloom manifests' tool hash (regenerate; words unchanged; third occurrence).
- `/tmp/x3-bottleX3-run*` are the preserved sessions (run115 had its 96
  cascade maps copied in after the snapshot tool fix); `/tmp/x3-candidate-*`
  keep the rollback DLLs.
- Finished agent worktrees under `.claude/worktrees/` can be removed after
  merge (about 12 from this session).
