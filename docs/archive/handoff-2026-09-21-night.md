# Handoff 2026-09-21 (night) — Run60 installed, Run 60 queued

Replaces the [morning handoff](archive/handoff-2026-09-21-morning.md), whose
operating rules, user preferences and closed decisions still bind (never launch
the game; every Wine command through `X3M_FIXTURE_BOTTLE=X3 python3
verification/probe/wine_lock.py`; one candidate owner; install only with
`tools/manage.py install --bottle X3 --dll-source <dll>`; native Windows is a
required, unverified target). Installed-build identity lives only in
[status](status.md). Main is clean at `a659e2a8`. No agent, Wine process or
build is running. The run authority is [user-runs.md](verification/user-runs.md).

## State

- **Installed:** Run60 DLL `aaa8abb2…` from `39c98242`
  ([qualification](../verification/results/run60-candidate-qualification.json),
  [install](../verification/results/run60-candidate-install.json)). Rollback DLLs:
  Run59 `/tmp/x3-run59-candidate/build/d3d9.dll`, Run56 `/tmp/x3-run56-candidate/build/d3d9.dll`.
- **Waiting on the user:** Run 60, two sessions (commands in user-runs.md).
  A: forward flight with/without SETA, approach flash with
  `--taa-unmatched-static node`, pan/roll regression, blur preference `0.97` vs
  `0.94,1`, clean quit. B: stored fog (`--volumetric-fog 0.03
  --volumetric-fog-cards replace --volumetric-fog-range stored`): range, ramp-in,
  same layout on re-entry/reload, FPS on/off, hitches, clean quit.
- **On main, not in the installed DLL:** `a659e2a8` depth source for the camera
  gate from the RT2 lane (243 slots, audit 95/547/0 on a scratch build). Rides the
  next candidate.

## What happened today (decisions and evidence, newest last)

1. B2b lattice packet writer merged default-off. Shader-shadow restoration
   lifetime fix merged (scoped getter refs). B2a Capture accounting was merged
   then removed (it broke the production link); it stays in the preserved
   worktree. **Upload-payload diagnostic is held by user agreement** after the
   [design review](architecture/lattice-approach-review-2026-09-21.md).
2. Lattice crawl: root cause is the thin-region gate closing under camera
   motion. Camera-relative gate + 7×7 box clip, accepted by the user for pans in
   Run 59 (run207/run208) and made the default. Forward flight needed a depth- and
   translation-aware camera path (installed in Run60). **Roll residual is
   physical** (gate fully open, residual 0.03–0.1 px); recommend accepting.
   Owning note: [taa-lattice-crawl.md §32–§32.4](architecture/taa-lattice-crawl.md).
3. User sees strut blur while moving (expected resampling cost of W 0.97, clip
   off). Tentative preference `--taa-thin-region 0.94,1`; **do not change the 0.97
   default until they confirm on Run60**. A speed-eased weight is the fallback
   idea if neither suits.
4. Approach flash: history key includes the node LOD word, mesh and buffers, so
   an LOD step re-keys all draw groups of a node for one frame. run209 shows 43
   single-frame events of exactly 22 unmatched draws (one panel instance has 22
   groups); LOD cause unconfirmed until a flight logs `motion_unmatched_static`
   lines. Check those lines' `projection_x/y` for private-projection draws.
5. Fog: stored-density route reopened after the user accepted the images; gates
   rescaled to half a display code. Checkpoints 1–4 merged; design amended to
   session-stable placement (record index + recipe). Deep lifetime review found a
   quit hang and a use-after-free, both fixed with fixtures. Owning notes:
   [integration](architecture/fog-density-runtime-integration.md),
   [ledger](verification/volumetric-fog.md). Proposed, not done: release the
   legacy atlas in stored mode; GPU cost is unknown until the user's FPS report.
6. Microsoft Defender quarantines synthetic PE test images
   (Wacatac.C!ml false positive); fixed by decoding headerless code windows in
   memory (`e0e85021`). Treat any `objdump: Operation not permitted` /
   vanished temp file as Defender.
7. Process rule adopted: run `check_no_x87.py` on a production build after every
   merge that touches production source (the first Run60 candidate from
   `f646f84f` failed the audit and was never installed). Scratch build line:
   `cmake -S . -B <dir> -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake
   -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPython3_EXECUTABLE=/usr/bin/python3`
   (the Homebrew python lacks NumPy).
8. Seam runner now pins the old diagnostic cut bounds; `seam-taa-camera-on` had
   been failing since the Run57 defaults (fixture drift, production correct).

## Next steps after the user reports Run 60

- Triage both run directories (`triage` agent; replay tool now has the exact
  camera path). Decide the thin-region weight default from their preference.
- If the approach flash is gone and the log confirms LOD re-keys, make
  `--taa-unmatched-static node` a default (launcher + native fallback), following
  the Run57/Run59 default pattern.
- Fog: act on FPS, ramp and layout observations; then the deferred evidence gaps
  in the fog ledger (shafts with the stored march, all fourteen families' chroma
  in flight, releasing the legacy atlas).
- Next candidate carries `a659e2a8`; full host suite + audit + the ten actual-DLL
  cases used for Run60, plus `run_temporal_pass.py` if the mask changes again.

## Worktrees

Preserve (uncommitted or evidence): `/tmp/x3-lattice-capture-lifecycle` (B2a +
passing lifecycle fixture), `/tmp/x3-lattice-payload-writer`,
`/tmp/x3-fog-finite-banks`, `/tmp/x3-run59-integration`,
`/tmp/x3-run60b-integration` (and the halted `/tmp/x3-run60-integration`).
The twelve `.claude/worktrees/agent-*` trees are fully merged into main through
snapshot commits and can be removed with `git worktree remove` when convenient;
nothing in them is pending. No cleanup was performed.
