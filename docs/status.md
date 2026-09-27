# Project status

The single current-state file (updated 2026-09-26). Rules: [AGENTS.md](../AGENTS.md). All goals were
marked completed on 2026-09-26 by the user's decision ([goals](goals.md)). The agent never launches the game.

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Run99 = DLL SHA-256
`1adabd0360a76b13018926f2505f6ab71d37e64963ce0fa0622ed2dea52439c4` (57,341,744 bytes, RelWithDebInfo, unstripped; not a
release build), built once from clean main `063fc8c1` in the detached worktree `/tmp/x3-run99-candidate/src`
(retained: `/tmp/x3-run99-candidate/build/`). Installed 2026-09-28 03:30
([qualification](../verification/results/run99-candidate-qualification.json): 0 warnings, x87 audit 0, config and
shader checks, host suite 274 / 2,847 / 0, temporal-pass record reused (`passed: true`, 15 LUMA_LOCK rows), dry runs
132 / 133 variables differing by `X3M_TAA_LUMA_LOCK` only; [install](../verification/results/run99-candidate-install.json)).
The game-directory `x3m.ini` is still the 0.5.2 template (the new commented `taa_luma_lock` entry arrives with the next
release install). The bottle's `cxbottle.conf` keeps the two GStreamer variables (`b06979d1…`).

Rollback chain: Run98 `d6f6b47a…` at `/tmp/x3-run98-candidate/build/d3d9.dll` (release 0.5.2 stripped, accepted in
Run 98 A), then Run97 `6e0bda57…`.

Run99 carries, beyond Run98, the **TAA luminance lock**, opt-in `--taa-luma-lock 16` (`taa_luma_lock = 16,0.25,3` in
`x3m.ini`; needs the thin region and the far stabiliser; [design](architecture/taa-luminance-lock.md), ledger
[temporal-resolve.md](verification/temporal-resolve.md) "Section 10/11 build"): a far pixel whose residual against
the reprojected history flips sign on two consecutive frames locks for 16 frames at the far weight, carried across pans
by a sub-texel offset; every other far pixel runs at the base weight, so distant hull plates should pan sharp while
struts stay held. Off = the shipped program bit for bit. Also on main since Run98: the baker strut widening as an
opt-in `--widen` flag (default off, parked), the far-weight A/B ledger (run351/352), and the CrossOver Preview note
for starting `x3m-regenerate.exe` (FEX drops the exe name for `C:\X3\x3m-regenerate.exe`; start it by its Z: path).

## Main beyond the installed build

Nothing: main `063fc8c1` is the installed commit (documentation follows it).

## Run queue

Run 99 A queued (2026-09-28): the lock A/B on the run351 stand at 5120x1440, default vs `--taa-luma-lock 16`, F8 at
rest, mid-pan and after the pan: [run queue](verification/user-runs.md). The far stabiliser default stays 0.985 until
the lock is judged; run351 (0.95) has no user verdict yet, run352 (0.90) shimmered thin lines.

## Open items

- From Run 91 A: bullets behind distant objects = the game's early bullet copy overpainted by later opaque draws while
  the additive route brightens both copies (fixed: Run94 composites the bolt back over held far/thin pixels, accepted in Run 94 A at W 1); station blur under a pan =
  history weight (0.85 accepted in Run 92 A; the rotation-aware weight stays opt-in and off after Run 93 A: the user prefers blur to shimmer).

- Native Windows runtime behaviour is unverified; the source cross-compiles, gaps are tracked in
  [platform portability](architecture/platform-portability.md).
- Config design steps 3 (typed values per family) and 4 (generated inventory tables) are still to come
  ([config-file.md](architecture/config-file.md)).
- MetalSharp: `~/.metalsharp/sharp-library/library.json` is absent, so `prepare_metalsharp.py --check` fails
  ([experiment](verification/metalsharp-experiment.md)).
- `x3m-regenerate` has never run on a real mod tree (synthetic trees only;
  [LOD overlay for mods](architecture/lod-overlay-mods.md)).

History: handoffs, status history and completed runs are in [docs/archive/](archive/).
