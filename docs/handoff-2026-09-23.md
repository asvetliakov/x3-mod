# Handoff 2026-09-23 — Run 67 triaged, Run68 candidate, merged-LOD pilot tooled

Replaces the [2026-09-22 evening handoff](archive/handoff-2026-09-22-evening.md); its
operating rules still bind (never launch the game; every Wine command through
`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py`; one candidate owner; install
only with `tools/manage.py install --bottle X3 --dll-source <dll>`; native Windows is a
required, unverified target — [portability](architecture/platform-portability.md)).
Installed-build identity lives only in [status](status.md). Main is clean at `d2f883c9`
(26 commits today, production source last changed at `d2f883c9`). Run authority:
[user-runs.md](verification/user-runs.md).

## State

- **Installed:** Run68 DLL `39c8c70d…` from `d2f883c9` ([status](status.md),
  [qualification](../verification/results/run68-candidate-qualification.json),
  [install](../verification/results/run68-candidate-install.json)). Rollback Run67 `/tmp/x3-run67-candidate/build/d3d9.dll`,
  then Run66 and older.
- **Run 67 is flown and triaged** (run249 A, run250 B, run251 C, run253 C second sector);
  the evidence is in the ledgers linked below and under `verification/results/run249-band/`,
  `run250-draws/`, `run251-fog-shadow/`, `run253-footprint/`.
- **Run 68 is queued** ([user-runs.md](verification/user-runs.md)): A = the SETA approach
  with strict + band + the new exit reset (`--taa-sky-history-exit-px 0.25`); B = the stand
  with `--cull-census --object-bounds-log` and no lod-scale (ladder, body name, screen size
  and alpha-tested boxes per draw); C = fog at a shaft spot with the pass on and the
  Ctrl+Shift+F11 A/B toggle.

## Run 67 verdicts (all measured; ledger sections linked)

1. **SETA smear (run249)**: the band term worked where edges slide at 3 px/frame or more
   (91–100 % current-only, none dark). The residual is hull share acquired beside hull moving
   under 3 px/frame, then carried outward as the silhouette recedes; two thirds of the old
   "dark-sky" metric 3–12 px out is ordinary flickering sky (refined criterion: output below
   0.7 × the pixel's own 8-frame mean gives 27 % / 34 % genuine share against a 16–18 % far
   tail). Normal speed and the stopped pan were clean by the numbers; the chase camera
   translates during a "pan", so the band term is not inert there (harmless).
   [temporal-resolve.md](verification/temporal-resolve.md), "Run 249".
2. **Draw accounting (run250)**: no cullable bucket justifies the next per-node cull
   (38 of 391 draws at the stand, about 1 ms at 27 µs per draw); 55 alpha-tested draws per
   frame were unmeasured and are now logged. Frame time unchanged from run248.
   [engine-frame-time.md](architecture/engine-frame-time.md), "Run 250".
3. **Fog shadow pass (run251)**: on for the whole launch, no off comparison, +14 device
   calls per frame, no failures; the look needs the in-session A/B the next build carries.
   [volumetric-fog.md](verification/volumetric-fog.md), "Run 251".
4. **Cascade footprint 8 (run251, run253)**: every drop attributable to the footprint, none
   to the cap; no popping in two sectors; replay saving about 30–40 µs per frame; larger P
   saves well under 0.1 ms and starts refusing M5/M4-class shadows. **Accepted as the
   launcher default** (and the DLL fallback; `0` opts out).
   [directional-shadows.md](verification/directional-shadows.md), "Run 251".

## Merged since the previous handoff (all reviewed; hooks, resolve and GPU changes twice)

- **Strict-sky exit reset** `--taa-sky-history-exit-px P` (default off): a band pixel that
  accepts history above P px/frame of parallax marks its age negative; the next frame a
  strict-sky pixel that reads a mark resets at the blend, so the hull share leaves in one
  frame instead of decaying at 0.9. Two exact rewrites keep far_camera at 510 of 512 slots.
  Off path bit-identical on the temporal fixture; SETA_EXIT rows on age, far and far_camera
  (straight cast 0.118 → 0.000; a turning leg keeps up to 80 % of the cast on the side facing
  the motion, a design limit). [Design as built](architecture/seta-sky-hull-share-decay.md),
  [ledger](verification/temporal-resolve.md) "2026-09-22 exit reset".
- **Fog shadow-pass A/B toggle** Ctrl+Shift+F11 (off = the launch-off in-march path, grid
  kept) and per-frame grid fields on `volumetric_fog_frame`; pass fixture 78 checks and the
  route bridge's 15 `shadow_ab_*` checks pass ([fog-shadow-pass.md](architecture/fog-shadow-pass.md)).
- **Cull census**: `lods=` / `thr=` (LOD ladder from the model pointer captured at the exit
  site, read at Present) and `body=` (engine body table `0x00608518`); site verifier pins the
  writer sets (mutation-checked); Wine CPU fixture 101/0. `draw_accounting.py --ladder`
  names bodies with no coarse level or tiny thresholds
  ([lod-selection.md](reverse-engineering/lod-selection.md), [cull-census.md](verification/cull-census.md)).
- **Draw accounting**: `--object-bounds-log` logs alpha-tested routed draws too
  (`alpha_tested=1`, `stale=1`; extent reads queued behind caster reads).
- **Footprint 8 default**; **cleanup batches 3** (retired tests, 8,817 lines) **and 5**
  (GTAO/SSAO chain, 3,849 lines; `ps3_program_slots.h` keeps the shared slot counter);
  bridge baseline pinned to `6f16dbf6`; archive script rewrites moved links; tooling fixes.

## Merged-LOD pilot (started 2026-09-23 on the user's request)

- **Format**: the engine's binary body parser (`0x00481aa0`) is decoded; `tools/analysis/bob1.py`
  round-trips 1634 of 1635 installed bodies byte for byte
  ([body-format-bob1.md](reverse-engineering/body-format-bob1.md)). Bodies have LOD ladders;
  the waste is material groups: the coarsest records of the heavy ships still carry 30–38
  groups, one draw each. 684 bodies ship a single record.
- **Selection**: the engine walks the ladder from the last record down and takes the first
  with `s < trunc(T·f)`, then a tail subtracts one at View Distance Very High (this bottle),
  so the last record is never drawn in the main view and a two-record body always draws LOD 0
  ([lod-selection.md](reverse-engineering/lod-selection.md), "What the selection really does").
- **Names**: model id → body name through the engine table (verified against three saves);
  the run250 heavy bodies are `argon_TL` (104 draws over the two frames), `argon_M2`, `argon_M1`,
  `military_outpost_middleb` (`verification/results/run250-draws/stand_bodies_out.txt`).
- **Tool**: `tools/analysis/lod_overlay.py` adds one coarse record (copy of the coarsest
  level, groups collapsed to one material) into `addon/05.cat`, placed where Very High reaches
  it (before-last with `T = T_last`; single-LOD bodies get the record plus a pad copy);
  originals hash-guarded; nothing installed yet
  ([merged-lod-feasibility.md](architecture/merged-lod-feasibility.md), "Overlay tooling").
- **Open before the pilot flight**: the threshold for the stand's ships needs the per-node
  screen size from Run 68 B (they sit at LOD 0 at 60+ px; a single-material record there is a
  look test); the overlay slot override and the hide-at-coarsest interaction are proven only
  by a flight.

## Decisions (user, 2026-09-22/23)

- `--shadow-cascade-min-footprint 8` is the default (leave at 8; larger buys nothing).
- `--lod-scale` stays default-off until the merged-LOD pilot is tested.
- The merged-LOD pilot proceeds; the engine's Very High rule is left as is (a design choice,
  not a bug); an optional `--lod-very-high-shift off` patch was offered, not built.
- Strict sky history and the band term stay opt-in pending Run 68 A.

## Next steps

- **Triage Run 68 when reported**: A with `verification/results/run249-band/dark_vs_own_mean.py`
  (acceptance: the 3–12 px genuine share falls from 27 % / 34 % to the 16–18 % far tail, pan
  and normal-speed flicker unchanged) — clean means strict + band + exit become defaults;
  B with `python3 tools/analysis/draw_accounting.py <run dir> --ladder` (per model: ladder,
  body, screen size, draws) and the alpha-tested buckets — choose the pilot body and its
  threshold from it; C by the user's A/B look and the fps overlay both ways plus the
  `volumetric_fog_frame` grid fields — a win makes the pass the default.
- **Pilot overlay flight** after Run 68 B: `lod_overlay.py --dry-run` on the chosen body, then
  the install owner writes `addon/05.cat`; accept on the node's `lod` index and draw count at
  the same stand.
- **Cleanup batch 6** (dead TAA resolve variants) must keep one age variant for the exit
  fixture rows and rebase its hash gate on the exit-reset records.
- Carried over: `run_ownership_integration.py` configures with `Release`; bloom manifests'
  generator hash awaits a Wine `--check`; the two launcher host modules fail only while the
  game or a fixture holds the installer lock.

## Worktrees

`.claude/worktrees/agent-*` trees from today are merged and may be removed on request,
**except `agent-a2b067e4005541a7a`** (unmerged GPU timer). `/tmp/x3-lattice-capture-lifecycle`
and `/tmp/x3-lattice-payload-writer` are archived to `archive/lattice-capture-lifecycle-wip`
and `archive/lattice-payload-writer-wip` and may be removed; `/tmp/x3-fog-finite-banks` may go;
`/tmp/x3-run5x/6x-candidate` hold the retained DLLs.
