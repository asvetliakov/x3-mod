# Handoff 2026-10-08 (session compacted mid-task)

State at hand-off, all committed on main (HEAD 0e21425b + this file):

- **Installed:** Run138 DLL `850342c0…` (59,403,525 B) from clean `937b8ab3`, bottle X3, DXVK backend. The user's
  `x3m.ini` says `occlusion_cull = off`; main now has `builtin='off'` for that option (not yet in an installed build).
  Rollback Run136 `d03e597e…` at `/tmp/x3-run136-candidate/build/d3d9.dll`. No run is open.
- **CrossOver Preview.app** carries MoltenVK 1.4.2 + the marked Gcenx PR #20 DXVK 1.10 (`a5005d1f…`); originals and
  `RESTORE.sh` in `~/crossover-preview-backup-2026-10-08/`; candidate DLLs in `~/x3-dxvk/`. A CrossOver update silently
  restores both files (the backup's `SHA256SUMS` check failing is the signal). `cxbottle.conf` has `CX_GRAPHICS_BACKEND = dxvk`
  by the user's hand. No `dxvk.conf` (async rejected: pop-in). The Wine crash-dialog watchdog (`pkill winedbg --auto` every 5 s)
  was started ~15:30 for 8 h; restart it if fixture crash dialogs appear.
- **Closed today (do not reopen without new evidence):** shader warm-up/precompile (per-state pipelines cost like the first
  compile; user: no flight-recorded list, no shipped DXVK cache); per-draw plate cull (hull pieces are split by material);
  pipeline_key diagnostic row; proxy-side draw-call culling on DXVK (same-build A/B = parity; forwarded draws are nearly free).
- **In flight:** a `disassemble` agent investigating an engine-side occlusion cull: apply the batched cull's "hidden last frame"
  answer at the engine site the dock-port cull hooks (Run110 saved ~3 ms at a carrier view by skipping the node visit). It writes
  `docs/reverse-engineering/engine-side-occlusion-cull.md` and ends with a feasibility verdict + the smallest flight-testable
  build. Next step after its report: if feasible, `implement-deep` (Fable) builds it behind `X3M_OCCLUSION_CULL=engine|on|off`
  (or similar), review, Run139 candidate (the usual record shape: `verification/results/run1NN-candidate-build.json` +
  `record_binding.py`; the build agent regenerates engine-effects/seam-engine-light records from the committed tree), install,
  queue Run 139 A. If a dead end: record in the occlusion-cull ledger and status, close the line.
- **Ledgers/notes written today:** `docs/architecture/d3d9-to-d3d11-translation.md` (DXVK/MoltenVK, pipeline cost),
  `docs/architecture/hdr-scene-path.md` + `docs/verification/hdr-scene-path.md` (readback double-buffer, lock gate),
  `docs/architecture/capture-format.md` (band readback), `docs/architecture/engine-light.md` + ledger (per plate, cap 72,
  dropped cull), `docs/architecture/occlusion-cull.md` + ledger, `verification/results/run13N-dxvk-triage/`.
- **Open small items:** fog families reported stale by the launcher since Run132 (TBackgrounds.txt changed Sep 29;
  `tools/manage.py fog-families --install --replace` is the suggested fix, user's call); two fixture runners pass several
  `--env` arguments to CrossOver's wine (may honour only the first; the smoke runner joins them); the DXVK fixture processes
  crash at teardown (pre-existing); the user's x3m.ini lacks the new `occlusion_cull*` lines (defaults apply).
