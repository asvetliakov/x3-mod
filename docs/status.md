# Project status

Updated 2026-09-14. This is the short current handoff; the current session
handoff is [handoff-2026-09-14d.md](handoff-2026-09-14d.md). The day's narrative
(chronology, superseded candidates, run-by-run detail, earlier prepared-design
prose, stable-foundation prose) is in
[status history 2026-09-14](archive/status-history-2026-09-14.md); earlier
checkpoints are in [status history 2026-09-13](archive/status-history-2026-09-13.md).
Read history only for a relevant unresolved question. The
[goal checklist](goals.md), [run queue](verification/user-runs.md) and
[original objective](user-objective.md) retain the full scope.

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Installed DLL SHA-256 is
`ab6e17bacbb525bc4f80fb6361498133cbf377704c9ff2f1fe309ab4a9395262` (14,209,414 bytes), a clean
rebuild of `5d06316` (AO step 2 with the Reset fix, screen emission step E, the cutout-miss
exemption, the bullet diagnostics and pad, the TAA invalidation-site diagnostic, fade default-on).
The [install record](../verification/results/ao-stepe-install.json) binds its source, scoped
verification (motion-output suite 100 + 26, AO live 7 twins, step-C live 14 cases, locked prefix,
voice replica), load check, and the rollback DLL `5b92484a…` (`066e18f`). EXE and `cxbottle.conf`
unchanged. Builds are not byte-reproducible (PE timestamp): the record notes the recorded build
`4866a41a` was overwritten by a fixture `--build` step, and the installed bytes were re-audited.

The installed renderer includes verified TAA, an FP16 scene target, AgX SDR writeback, Auto capped at +1.5 EV by default,
and a fixed EV 0 comparison through Ctrl+Shift+F9. Ctrl+Shift+F10 switches bloom contribution. Bloom, linear materials, and linear
emissions remain opt-in. Installed material coverage is **168 exact pairs / 137 originals**; installed default-off
emission coverage is twenty exact SM2 DEFAULT/INSTANCE pairs.

The installed chase defaults remain 13° pitch, distance 0.9, rotation/position response 0.28/0.38 s, offset 0.45,
and lag limits 8°/0.10. Vanilla camera is the default. Loading acceleration, predictive lead marker, central chase
HUD, and the selected WRAP/motion fixes are included.

## Next user action

Run 20 on the next candidate (step D, loading-phase markers, AO radius/debug
test, asteroid diagnostic, port and ship far/near retry); it is queued in the
[run queue](verification/user-runs.md) once the candidate is installed. Run 19
(snapshot run47) is complete: shimmer history drops gone (reason 3 at 0.01 %)
but distant asteroids still lose triangles; bolts accepted at gain 1; AO runs
but is invisible at the 2 m radius; the port pair was captured at one distance
(57/54 px, same draw path, radiance within 3 %) and the user also sees the
darkening on a ship. Details in the
[completed-run archive](archive/user-runs-completed.md).

## User decisions (2026-09-14)

- Fade route default-on accepted (`--no-linear-distance-fade` opts out).
- AO fixture cost accepted.
- No per-frame screen-emission bracket cap; the bound fix comes first.
- Screen emission step E contract: decode the accumulated native B once at
  publication, `--screen-emission-gain` default 1 = presented-frame parity.
- Cutout-miss exemption: known-blended draws no longer count as a cutout miss.
- Voice: plugin path only, no native-codec or bottle-clone route; fail early on
  missing audio if the plugin path fails.
- After run 19 (late evening): docking-port design note stays unratified and the
  linear rule unchanged until run 20 captures a real far state and a ship pair;
  `--screen-emission-gain` 1 stays the default (no gain-2 run); AO stays
  default-off and run 20 tests appearance with `--ao-radius 20` and `--ao-debug`
  instead of a code change.

## Open items

- **Target speech (resolved):** plays in gameplay with `--voice-decoder
  /tmp/x3-wma-plugin-v4` and the fixed DMO fallback hook (runs 16 and 18); the
  run-16 crackle was a full-scale wrap in CrossOver's stock converter, removed by
  the plugin v4 float limit. The plugin lives outside the bottle and the repo
  (untracked, backed up). Ledger [voice-decoder.md](verification/voice-decoder.md),
  sequence [voice-startup-sequence.md](reverse-engineering/voice-startup-sequence.md).
- **Selection pause (resolved):** no target publication over 10 ms in runs 16/18
  against run 28's median 463 ms. Other unexplained slow-frame residuals remain
  open; see the [33-site trace](reverse-engineering/selection-native-vm.md).
- **Distant shimmer:** root cause named — `cutout::missed` on a source-over
  *blended* alpha-tested cutout draw refused at the motion gate drops the whole
  frame's TAA history (15 % of frames in runs 11/14, none in run 15). The
  exemption is merged and rides the installed build; pending run 19 confirmation.
  Ledger [motion-output.md](verification/motion-output.md). The fade route is not
  implicated (witness clean). Independent minification hypothesis and bounded
  diagnostics: [asteroid-specular-minification.md](reverse-engineering/asteroid-specular-minification.md);
  no smoothing policy selected, and no forced opaque depth or inferred LOD change.
- **Docking-port darkening:** owner is the engine's LOD boundary
  (`0x0047cfe0`, `s = r·640/D`, no hysteresis); beyond ≈52–57 px port width the
  station is one merged opaque subset, nearer the port is its own source-over draw
  over a dark interior. Normal-map minification is refuted. The design note
  [docking-port-lod-consistency.md](architecture/docking-port-lod-consistency.md)
  is **not ratified**: native parity for the near port versus the slightly
  brighter linear rule is a user decision after run 19. Measurements in
  [station-material-distance.md](reverse-engineering/station-material-distance.md).
- **Bullets / screen emission:** step E (publication-time decode, parity within
  one FP16 code) and step D (per-draw vertex hull, fan batch 1.2 % of the viewport
  vs 89.6 % for the box; derive 1.7–27 µs per draw, sentinel fill 17 µs per lock)
  are merged; step E is installed, step D rides the candidate after run 19.
  Gameplay parity pending runs 19/20. Ledger
  [screen-emission.md](verification/screen-emission.md), design
  [screen-emission-bullet-bound.md](architecture/screen-emission-bullet-bound.md).
- **Ambient occlusion:** step 2 is merged and installed behind
  `--ambient-occlusion` (Ctrl+Shift+F11 toggle, `--ao-timing`, live fixture 7
  twins, two reviews); an off/on gameplay run is pending run 19. Design
  [ambient-occlusion.md](architecture/ambient-occlusion.md).
- **Distance fade:** default-on in the installed build; the witness stayed clean
  in runs 11/14/15 (0 covered pixels outside the derived rectangles, 0
  full-viewport fallbacks). Gameplay acceptance of the default rides run 19.
  [linear-distance-fade-region.md](architecture/linear-distance-fade-region.md).
- **Exposure meter under `X3M_HDR_DECODE=none`:** the GPU level-0 value
  disagrees with the CPU reference beyond 1e-4 only in that decode mode, so the
  pass refuses the meter (`meter_reason=self_test`) and Auto exposure is off
  there; every other decode gives `ok`. Not a default configuration; open, not
  yet owned.
- **Bloom/exposure:** the installed correction uses gain 0.375/scatter 0.65 and
  the +1.5 EV Auto ceiling is the accepted default. The meter still mostly
  reaches its ceiling; physically informed adaptation remains unproved.
- **Material appearance and coverage:** exclude accidental loss of native gloss
  terms before artistic tuning. The [coverage ledger](architecture/material-coverage.md)
  accounts for all 817 archive pass identities; older profiles, transparent,
  background and other scene writers remain beyond installed coverage. The
  [glass extension](architecture/glass-materials.md) adds six reviewed SM3 pairs
  (168 pairs / 137 originals), preserving native gloss/Fresnel; gameplay
  acceptance pending.
- **HDR scope:** FP16 and AgX work, but much of the scene is still
  compatibility-decoded gamma-space lighting. Scene-referred lighting, complete
  linear blending and verified HDR display output remain incomplete.
- **Native Windows:** Windows-compatible source cross-compiles, but no
  native-Windows runtime is verified. Depth provision, CreateDeviceEx adoption,
  MRT/PS2.1, Reset/presentation, performance and HDR output remain gaps
  ([platform-portability.md](architecture/platform-portability.md)).
- **Window/cursor:** the macOS menu bar remains open. The double cursor after
  alt-tab reproduces in vanilla (run 4, 2026-09-14: move the desktop cursor
  outside the window before alt-tabbing back), so it is not a proxy regression;
  recipe and reading in [window-and-cursor.md](architecture/window-and-cursor.md).

Keep raw captures and builds local, use focused verification, and update the owning
note instead of expanding this handoff. Remaining scope: [roadmap](architecture/roadmap.md).

- **Loading time (census 2026-09-14 late evening):** no log marker splits menu and save load, so the
  34–38 s figure cannot be re-measured directly; the `frame_end elapsed_ms` at frame 600 is 40–45 s in
  runs 11/14/16/17, with one 23–32 s stall in the first 600 frames of which the instrumented reader/inflate
  path explains 10–11 s and the rest is unattributed. Next: a `loading_phase` marker at menu-shown and
  save-load-complete, then attribution of the remaining stall; not started.
