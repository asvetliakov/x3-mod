# Project status

Updated 2026-09-15 (user selected EV ceiling 1.0 and fill 0.03; source changes in progress). This is the short current handoff; the current
session handoff is [handoff-2026-09-15.md](handoff-2026-09-15.md). The day's narrative
(chronology, superseded candidates, run-by-run detail, earlier prepared-design
prose, stable-foundation prose) is in
[status history 2026-09-14](archive/status-history-2026-09-14.md); earlier
checkpoints are in [status history 2026-09-13](archive/status-history-2026-09-13.md).
Read history only for a relevant unresolved question. The
[goal checklist](goals.md), [run queue](verification/user-runs.md) and
[original objective](user-objective.md) retain the full scope.

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Installed DLL SHA-256 is
`f6cf181bf20e3fa15c2b29a5c0ffe867979d6d0d8e978e0e8477ddf8d1c52857`
(14,321,844 bytes), built once on Sol from reviewed clean source `a53cf8f`.
It adds `--material-fill` (default 0) and fill-refusal diagnostics to the previous
feature set. The [install record](../verification/results/run23-candidate-install.json)
binds the clean build (12.09 s, zero warnings), 225-function no-x87 audit,
17 exports, X3 load check, scoped GPU/host qualification and verified installed
bytes. The previous `53a0d8a7…` DLL and manifest are retained together for rollback.
EXE and bottle configuration hashes are unchanged. The affected run23 launch
passed one `--dry-run`; no game was launched.

The installed renderer includes verified TAA, an FP16 scene target, AgX SDR writeback, Auto capped at +1.5 EV by default,
and a fixed EV 0 comparison through Ctrl+Shift+F9. Ctrl+Shift+F10 switches bloom contribution. Bloom, linear materials, and linear
emissions remain opt-in. Installed material coverage is **168 exact pairs / 137 originals**; installed default-off
emission coverage is twenty exact SM2 DEFAULT/INSTANCE pairs.

The installed chase defaults remain 13° pitch, distance 0.9, rotation/position response 0.28/0.38 s, offset 0.45,
and lag limits 8°/0.10. Vanilla camera is the default. Loading acceleration, predictive lead marker, central chase
HUD, and the selected WRAP/motion fixes are included.

## Next user action

**Run 23 is complete**, preserved at `/tmp/x3-bottleX3-run54` (153 referenced
files), with four EV0/+1.5 far/near screenshots. The user reports brighter hulls
and subsequently chose **Auto capped at +1.0 EV and material fill 0.03** as
the new defaults. Those source changes are in progress; the installed defaults
above have not changed yet. The run54 brightness thresholds support fill, but
unmatched colour/reprojection evidence limits acceptance and does not establish
a measured 0.03 comparison.
See the [fill ledger](verification/fill-light.md) for numbers and limitations.
The [run queue](verification/user-runs.md) records the remaining telemetry request. K=0 qualified
4,177 cases with every recorded baseline row bit-identical; live qualification
passed 8 cases / 32,516 checks. The 23-case fill oracle has pre-target scene-linear
luma error 0 FP16 codes, encoded FP16 RGB error ≤1 code, and reconstructed
FP16-image luma error ≤3 codes ([ledger](verification/fill-light.md)).

Run24's gate portion is received as `/tmp/x3-bottleX3-run56`: the user confirms
a reset and did not use jumpdrive (no suitable save). The trace destroys the
old cockpit, creates generation 2 with a different ship pointer, then requests
mode 1 at script PC `0x000f0794` before the new sector is visible. The ratified
same-lifetime restore predicate would cancel; targeted reconstruction must
establish safe cross-recreation identity before implementation. Static analysis
also finds persistent script-mode and geometry resets; mode-only restoration
would be incomplete. The [gate reconstruction](reverse-engineering/chase-view-transition.md#2026-09-15-run24-gate-reconstruction-snapshot-run56)
requires consolidated identity/provenance/geometry diagnostics. Jumpdrive shares
the static warp path but remains gameplay-unverified.

The chase HUD anchor (`centre` default) is now implemented and independently
reviewed in source, separately from the fill candidate ([ledger](verification/chase-hud-anchor.md));
81 host tests pass, with gameplay glyph calibration pending. The gate trace above now drives the view-restoration prerequisite. Shadows follow the ratified
route-B order: sun-lit-share lane, replay feasibility without shading, then cascades;
screen-space shadows are fallback only. The [sun-share extraction contract](reverse-engineering/sun-share-material-contract.md)
now covers all 108 pixel originals; the receiver/capability boundary is ratified
in the shadow note, with portable depth-history channel copying required. No
live shadow lane or replay is implemented yet. Sun-share extraction is now
reviewed and host-qualified in source (432 variants; 1,388 legacy outputs unchanged);
GPU precision and live MRT qualification remain ([ledger](verification/directional-shadows.md)).
The inherited PS3 constant-read-port violation is repaired and reviewed in
source: legacy/fill GPU reports and 1,162 fade readbacks match retained
baselines exactly ([repair ledger](verification/linear-material-constant-port.md)).
Native Windows runtime remains unverified; this repair is not yet installed. Replay admission has a ratified managed-buffer
feasibility boundary, with complete entry coverage and concurrency proof still
required before activation. Existing loading-phase evidence bounds the stall at 21.702 s: file opens and
mesh processing are large measured counter totals, but overlapping timers leave
exact wall-time attribution open ([analysis](reverse-engineering/loading-observations.md#run-48-bounded-attribution-of-the-save-load-interval-2026-09-15)). Completed runs remain in the
[run queue](verification/user-runs.md) and its archive.

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
- **Fade-band objects tremble under TAA (run 21, owner found; fixed, see above):** the "station
  section" in `screenshots/jitter1.png` is an asteroid in its distance-fade band
  seen through the hangar gap. Its blended colour pass is refused at the motion
  gate, the fade route binds it as a region and that region becomes the reactive
  mask, so the resolve returns the raw jittered sample every frame (measured
  shift = Δjitter, up to 0.9 px) while routed neighbours are reprojected
  ([asteroid-fog-temporal.md](reverse-engineering/asteroid-fog-temporal.md),
  "Run 49"). Fixed in the installed build: fade-band draws of reviewed pairs are
  routed with their own motion rows above 500 ‰ of the program's own fade
  fraction (hysteresis 100 ‰), masked current-only below; fixture resolved
  residual ≤ 0.07 px against a raw shift equal to the jitter; run 22 confirms
  in game ([linear-distance-fade-region.md](architecture/linear-distance-fade-region.md),
  "Fade-band route").
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
- **Docking-module darkening (owner found, run 22):** the station module goes
  black at range because the player ship's white point light (the only point
  light in the session, attenuation 1/(1+0.01d)) is culled per node by range:
  `g_nNumLightPoint` flips 1→0 on the ten module nodes between ≈1150 and ≈1350
  world units while the large body node keeps it. With the sun in front of the
  camera and no ambient term, the camera-facing module faces have no light and go
  black; the sun-lit body is unaffected. Witnessed on the run-51 pair (same LOD 0,
  same pairs, textures and constants; only the light count differs; module luma
  ratio 1.29 near/far, dark fraction 0.25 → 0.02). Native behaviour, hence vanilla.
  Earlier LOD-step and fade explanations are withdrawn. Fix direction: an
  ambient/fill term in the converted materials (one MAD per pixel, also lights
  black bays and night sides), ratified and merged; GPU qualification is pending.
  Widening point-light range is rejected; optional root-object admission follows
  the fill verdict and starts with disassembly.
  [station-material-distance.md](reverse-engineering/station-material-distance.md),
  "Run 22". `--lod-scale` is installed default-off and stays so: the user
  compared 3× against 1× and saw no visual difference (View Distance "Very
  High" already biases the LOD index); without the option nothing is patched
  ([lod-scale.md](architecture/lod-scale.md)).
- **Fade-band trembling:** fixed and accepted in run 22 (860 fading draws routed,
  zero holds, no trembling reported).
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
- **Native Windows:** C++ source cross-compiles, but the material sanitizer has a
  **known shader-validity blocker**: two distinct constant registers in one PS3
  instruction, observed in all 108 converted pixel programs. CrossOver accepts
  the shaders; native validity requires the separate repair now in progress. No
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
