# Handoff 2026-09-24 (written 2026-09-23 evening; authoritative)

Previous handoff: [archive/handoff-2026-09-23-late.md](handoff-2026-09-23-late.md) (operating rules
and closed decisions still bind). Read [status.md](../status.md), [goals.md](../goals.md), user-objective.md.

## State

- **Installed:** Run72 DLL `c17792a9…` from `dcf3728b` ([install](../../verification/results/run72-candidate-install.json));
  rollback Run70 `/tmp/x3-run70-candidate/build/d3d9.dll`. Game data: the merged-LOD **batch** overlay of the flown
  sectors in `addon/05` (19 bodies, 561 → 55 drawn groups, 124 MB; [record](../../verification/results/lod-overlay-pilot/install.json)).
  No `x3m/fog-families.bin` (vanilla needs none).
- **Queued:** Run 73 A ([user-runs](../verification/user-runs.md)): the batch overlay in a busy sector on the Run72 DLL.
- **On main since Run72 (committed):** GPU sync timing (`--gpu-sync-timing`, opt-in; event queries work on the X3
  bottle, timestamp queries do not), music keep with alt-tab skip_all + Patch D and the trace coexisting (`--music-keep`
  still opt-in; `--music-trace`), executable identity without a file hash (`apply_laa.py` unnecessary: the EXE is already
  LAA), fog families data-driven (`tools/analysis/fog_families.py`, loader in the DLL), batch overlay mode with mod
  support and the text-body reader (text bodies refused until the parser follows the engine's loader rules;
  [body-text-loader.md](../reverse-engineering/body-text-loader.md)).
- **Staged on main, not yet committed (waiting on the last review-fix rounds):** the fog hand-over (R1 readiness
  step, R2 cold far fill with a whole-atlas latch, R3 transit prefill from the object-list tail, docked parent walk;
  all default on under the stored range; [fog-handover.md](../architecture/fog-handover.md)) and the bolt footprint
  (`--bolt-footprint 3,8` default; [bolt-footprint.md](../architecture/bolt-footprint.md)). Gate on the merged tree was
  green (host suite 242 modules / 2,494 tests; x87 671; fog pass fixture PASS with the HANDOVER row).
- **In flight (agents):** fog agent (R3 low fixes), text-body agent (engine-loader semantics for parse_text, then
  lifting the three text refusals).

## Next steps

1. Merge the two deltas, rerun the gate, commit fog hand-over + bolt footprint together (they share capture.cpp /
   motion_output), build the Run73 candidate from clean main (same procedure as Run72:
   `verification/results/run72-candidate-qualification.json`, `/tmp/x3-run72-candidate/run72-commands.md`; add
   `run_gpu_sync_timing.py`, the fog family fixture, the fog pass fixture and the site verifiers), install, queue Run 73:
   A = batch overlay busy sector (can fly now on Run72), B = Run73 defaults: two gate transits into fogged sectors
   (`volumetric_fog_handover` / `volumetric_fog_prefill` lines: lead_ms, confirmed/discarded, whole-atlas cost),
   docking (`volumetric_fog_docked`), `--music-keep` alt-tab (expect `mode=skip_all held=0`, no replay,
   `music_keep_active` transitions, trace `reason=ok`), the corvette's bolts in third person while holding fire
   (`bolt_footprint … timed_draws= us=` line, bolt_center.py on the burst, first person as the no-change control),
   C = `--gpu-sync-timing` one short stand flight at 1920×1080 with the host idle (per-pass GPU table).
2. Text bodies: merge the engine-rule parser, lift the refusals, rerun the census (expect a small gain only: most of
   Mayhem's text winners are scenes).
3. Fleet: after Run 73 A is clean, run the full `--batch` (default `--jobs` is RAM-derived, ~2 workers here; ~40 min
   for ~1,020 bodies) and install; per-tile scale / span clamp for the `texel_floor` bodies is the later fix.
4. Mods: when the user installs Mayhem into the bottle, run `fog_families.py --install` and the batch `--sync`
   (the overlay moves to the next free slot; the old slot is retired to an inert catalogue: engine acceptance of a
   retired slot is unverified until a launch).
5. Handoff hygiene: `docs/handoff-2026-09-24.md` is this file; archive it when the next one is written.

## Rules added today

- Game-running check: `verification/probe/game_guard.game_running()` or `ps -axww -o comm= | grep -c 'X3AP.exe$'`;
  never `ps -o args= | grep X3AP` (a shell wrapper matched itself and delayed an install).
- Never symlink directories into the user's mod trees (`/tmp/x3-mod1`, `/tmp/x3-mod2`); synthetic roots copy files
  (`make_mod_root.py` refuses existing destinations and symlinked parents). Five loose Mayhem files were lost that way;
  the user re-extracts if needed.
- Merging a worktree whose base predates other merges: use `git merge-file` with the common base per shared file,
  never copy a shared file verbatim (a verbatim copy dropped the fog switches from tools/manage.py once).
- Design decisions target 1920×1080 (the LOD metric is resolution independent; atlases are sized for the display).
- The shipped X3AP.exe is already large-address-aware: do not run `4gb_patch.exe`; identity = structure + site bytes.

## Open questions carried

- The ~0.5 s rewind heard on alt-tab under mode=paused (skip_all should remove it; unflown).
- The engine's acceptance of a retired (inert one-member) catalogue slot.
- Per-tile scale for the four low-texel stations; text bodies' three refusals pending the parser rework.
- GPU per-pass cost at 1080p (Run 73 C).
