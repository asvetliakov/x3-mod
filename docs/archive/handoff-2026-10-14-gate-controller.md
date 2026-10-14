# Handoff 2026-10-14: gate traffic controller B.5 installed as Run166, Run166 triage in flight (session compaction)

Written at compaction. `docs/status.md` line 3 is the current-state line; this entry only carries what a fresh session
needs to resume. All user decisions and amendments are in the notes named below.

## Installed and flown
- **Run166 installed** (main `f0fb46ad`, DLL `f789f531…`, records `verification/results/run166-candidate-{build,install}.json`);
  rollback `C:\X3\rollback-d3d9\d3d9.dll.run164` (Run165's DLL `abd17f60…` crashed the game: never fly it). The Run166 flight
  ended cleanly (no exception, `ctl_live` resets 0, budget cuts 0, 9 releases, 1 eviction); its log is at
  `verification/results/ship-steering/run166-triage/x3m.log` (gitignored), Run 166 A row open in `docs/verification/user-runs.md`.
- Controller stages A, B, B.2–B.5 and docking amendment 18 are in `docs/architecture/gate-traffic-controller.md` (D.0–D.12,
  §A.1, §B.1–B.5 incl. "B.5 Pointer liveness audit") and `docs/architecture/docking-approach.md`; ledger
  `docs/verification/ship-steering.md`; the flight triages are under `verification/results/ship-steering/run15x..166-triage/`
  (each has `rows.sh` and the scripts the next triage copies forward: `members.py`, `hold_wait.py`, `funnel_idle.py`,
  `window_runs.py`, `transits.py`, `sb_seq.py`, `e_evict.py`, `small_gap.py`, `x12_real.py`, `game_steps.py`, `seta.py`).
- Measured on the 5-capital crowd (Run165): intended serialization, 0 s excess wait, one launch per 116 game s, crossings ≤ 1°,
  0 member 0x12; escorts wait ~500 game s for the script's order 4 after a leader reassignment (script-owned); holders spent
  168 game s outside their release window with the funnel free (the lever we own).

## In flight when compacted
- Triage agent on the Run166 log (brief: per-reason holder seconds, the release-window phase per release, savings estimates for
  (a) a two-sided window per lap, (b) pacing the holder to `t_free`, (c) a smaller petal). Its report lands under
  `verification/results/ship-steering/run166-triage/`. If no report file exists, re-run the triage with the same brief.

## Next steps (in order)
1. Read the Run166 triage; pick the span change (expected: pace the holder to the predicted free time, or a two-sided window);
   write it as "### B.6 Span tuning" in the controller note (orchestrator or a design agent on Opus), implement with a fresh
   `implement-deep` on Opus in a worktree, review (Opus; Fable too if it touches a hook or a write path), squash-merge, clean
   build in `/tmp/x3-run167-candidate`, gates (0 warnings, `check_no_x87.py`, `verify_ship_steering_sites.py --exe
   /mnt/c/X3/X3AP.exe` 20 checks, focused tests incl. `test_ctl_boundary_reads`), install as Run167 with rollback
   `d3d9.dll.run166`, records, status line 3, user-runs row, `archive_user_runs.py --check`, commit.
2. Still open: 9 reported SETA fixture rows (late launch 35–42 s, rare member evasions); the one-active-sector assumption;
   backlog items in `docs/architecture/ship-steering-scenario-audit.md` "Later" (52 km bases, asteroid cap now 2048, M7 gate
   path, stock records, combat, save/load, box-hull capital pairs item 10); level 2 of the controller (DLL issues order 5 +
   loose `L\x3story.obj` clearance patch) only if the script's slot/wait is still the visible clunk.

## Process notes (binding)
- Reviews: one Opus review per change; two (Opus + Fable) for hook/write-path/memory-safety changes. Fable and Opus agents were
  both killed once by an API access outage (`oauth_org_not_allowed`): resume them with SendMessage, do not respawn.
- Rotate implementer agents before their context limit (memory `rotate-implementer-agents`); fresh agent per stage.
- Install procedure: memory `wsl2-native-windows-host` (tasklist check, cp rollback, cp, cmp, JSON records).
- Never launch the game; no Wine on this host; the user flies and reports; SETA is on 65–98 % of their frames, so every
  time bound must be game-time and frame-step aware (B.3).
- User decisions: capitals ignore ships < r/3 on every order (dock legs included); controller design levels per the note;
  snapshot cap 2048; carrier docking is any dock-capable ship ([obj+0x40] & 0x100000), not M1 only.
