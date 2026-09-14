# Project status

Updated 2026-09-15. This is the short current handoff; the current session
handoff is [handoff-2026-09-15.md](handoff-2026-09-15.md). The day's narrative
(chronology, superseded candidates, run-by-run detail, earlier prepared-design
prose, stable-foundation prose) is in
[status history 2026-09-14](archive/status-history-2026-09-14.md); earlier
checkpoints are in [status history 2026-09-13](archive/status-history-2026-09-13.md).
Read history only for a relevant unresolved question. The
[goal checklist](goals.md), [run queue](verification/user-runs.md) and
[original objective](user-objective.md) retain the full scope.

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Installed DLL SHA-256 is
`39b090d02c150e1da443532c84ad299334d17e49a8d9ec6bd16d05ecd4fe96cd` (14,247,499 bytes), a clean
build of `77a649b` (installed 2026-09-15 early morning): screen emission step D (per-draw bullet
vertex hull), the `loading_phase` markers, the z_only depth-prepass jitter with the
`unjittered_depth_writers` counter, on top of everything in the previous build. The
[install record](../verification/results/run20-candidate-install.json) binds its source, audits
(no-x87 224 functions, 17 exports, load check), scoped qualification (locked-prefix live 24 frames,
screen-emission live 14 cases, motion-output 118 cases at `1d49489`, host tests 48) and the
rollback DLL `ab6e17ba…` (`5d06316`) kept with its manifest in the candidate directory. EXE and
`cxbottle.conf` unchanged.

The installed renderer includes verified TAA, an FP16 scene target, AgX SDR writeback, Auto capped at +1.5 EV by default,
and a fixed EV 0 comparison through Ctrl+Shift+F9. Ctrl+Shift+F10 switches bloom contribution. Bloom, linear materials, and linear
emissions remain opt-in. Installed material coverage is **168 exact pairs / 137 originals**; installed default-off
emission coverage is twenty exact SM2 DEFAULT/INSTANCE pairs.

The installed chase defaults remain 13° pitch, distance 0.9, rotation/position response 0.28/0.38 s, offset 0.45,
and lag limits 8°/0.10. Vanilla camera is the default. Loading acceleration, predictive lead marker, central chase
HUD, and the selected WRAP/motion fixes are included.

## Next user action

Run 20 (snapshot run48) is complete on the installed `39b090d0…` build:
asteroid triangle dropout fixed and accepted; step D bullets accepted (no
fullscreen bracket in 50,654 frames); loading markers read menu 12.9 s and a
21.7 s save-load stall; the far port was captured (37 px) and its radiance
moves only 6 % between far and near; AO ran in the debug factor view for the
whole session and is a few pixels wide at gameplay distances. Run 21 session A
(snapshot run49) is complete: AO invisible at radius 100 and closed as default-off
(zero cost when off); the bullet dimming is refuted (witness every frame, zero
pixels outside the hull, peak higher than runs 19/20); a new symptom, one station
section trembling at 4.7 km, is under diagnosis. Session B (vanilla far/near
approach to the port and a ship) is not reported yet. No run is queued until the
trembling diagnosis and the directional-shadows note land. Details in the
[completed-run archive](archive/user-runs-completed.md). Run 19
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
- **Fade-band objects tremble under TAA (run 21, owner found):** the "station
  section" in `screenshots/jitter1.png` is an asteroid in its distance-fade band
  seen through the hangar gap. Its blended colour pass is refused at the motion
  gate, the fade route binds it as a region and that region becomes the reactive
  mask, so the resolve returns the raw jittered sample every frame (measured
  shift = Δjitter, up to 0.9 px) while routed neighbours are reprojected
  ([asteroid-fog-temporal.md](reverse-engineering/asteroid-fog-temporal.md),
  "Run 49"). Fix in implementation on Fable: route fade-band draws of reviewed
  pairs with their own motion rows and keep them out of the mask above a fade
  threshold; rides the next candidate.
- **Distant shimmer:** root cause named — `cutout::missed` on a source-over
  *blended* alpha-tested cutout draw refused at the motion gate drops the whole
  frame's TAA history (15 % of frames in runs 11/14, none in run 15). The
  exemption is merged and rides the installed build; pending run 19 confirmation.
  Run 19 confirmed the exemption (reason 3 at 0.01 %) but distant zoomed asteroids
  still lost triangles: owner found and fixed in the installed build. The engine draws
  fogged asteroids as a depth-only prepass then a blended draw; the route jittered
  only the second, so facets failed LESSEQUAL on the jitter side
  ([asteroid-fog-temporal.md](reverse-engineering/asteroid-fog-temporal.md), "Run 47").
  The z_only programs are now jittered with the scene (fixture: zero holes, control
  frames drop out); run 20 confirms in game and its `unjittered_depth_writers`
  counter names any other unjittered depth writer. Ledger
  [motion-output.md](verification/motion-output.md).
- **Docking-port darkening (unexplained, vanilla too):** the user sees a port
  that is fine near and black when flying away; run 21 session B shows the same
  in vanilla, so it is not renderer-introduced. Neither of the two candidate
  owners survives the captures: the LOD 2/3 switch shows no radiance step on the
  measured same-node crossings, and the port pair carries no fog or fade constant
  at any distance (the only dark bay, model `543f`, is less dark at 53 px than at
  109–146 px). The run-20 "port" measurement was an asteroid part (corrected).
  Next: the user's screenshot pair (fine near, black far) with an F8 at each on
  the same port, so the analysis compares the pixels the user means. Notes
  [station-material-distance.md](reverse-engineering/station-material-distance.md),
  [docking-port-lod-consistency.md](architecture/docking-port-lod-consistency.md)
  (native parity ratified, low priority). A separate `--lod-scale` option is in
  implementation from [lod-selection.md](reverse-engineering/lod-selection.md)
  (one global float, same-length patch at `0x0047d44b`, default-off, cap 4).
- **Bullets / screen emission:** step E (publication-time decode, parity within
  one FP16 code) and step D (per-draw vertex hull, fan batch 1.2 % of the viewport
  vs 89.6 % for the box; derive 1.7–27 µs per draw, sentinel fill 17 µs per lock)
  are installed; run 19 accepted the bolts at gain 1 and run 20 showed step D
  working (hull a third of the box on firing frames, no fullscreen bracket); run
  21 refuted the dimming (witness every frame, zero pixels outside the hull).
  Step D accepted in game. Ledger
  [screen-emission.md](verification/screen-emission.md), design
  [screen-emission-bullet-bound.md](architecture/screen-emission-bullet-bound.md).
- **Ambient occlusion (closed 2026-09-15):** step 2 stays installed and
  default-off behind `--ambient-occlusion`. Runs 19–21 showed the term invisible
  at gameplay distance at 2, 20 and 100 m (`radius_px = 256·radius_m/distance_m`,
  cap 64); the user decided AO stays off (open-space games do not read by AO
  either) and no sun-weighted AO v2 follows. Cost when off is zero (never called
  without the option; one early return when toggled off). The chain remains only
  as a base if the directional-shadows design chooses screen-space shadows.
  [ambient-occlusion-scale.md](architecture/ambient-occlusion-scale.md); design
  [ambient-occlusion.md](architecture/ambient-occlusion.md), ledger
  [ambient-occlusion.md](verification/ambient-occlusion.md).
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
  save-load-complete: installed and read in run 20 (menu_shown 12.9 s, save load
  19.2–40.9 s with a 21.7 s stall); next is attribution of that stall
  ([loading-observations.md](reverse-engineering/loading-observations.md), "Phase markers").
