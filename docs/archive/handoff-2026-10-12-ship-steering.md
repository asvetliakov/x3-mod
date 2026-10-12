# Handoff 2026-10-12: ship steering, avoidance and capital gate transit (session compaction)

Written at the user's request before compacting the main session. `docs/status.md` line 3 is the current-state
line; this file carries what the orchestrator needs to resume mid-stream.

## Installed and flown
- Run158 installed in native `C:\X3` (main `734ec237`, DLL `011b348c…`), rollback `C:\X3\rollback-d3d9\d3d9.dll.run157`
  (and `.run156`, `.run150` = release 1.2.1). User's `x3m.ini`: `ship_steering = 1`, `ship_avoidance = 1`, `ship_trace = 1`,
  `ship_trace_max = 4`, `perf = 1`, `debug = 1` (gates and `ship_steering_gates_capital` default on).
- Run 158 A flown and triaged (`verification/results/ship-steering/run158-triage/`, commit `3582dbc5`): user says "better";
  slot-0 capital 0 s in the engine's avoid order (Run157: 524 s), joined at 100 m/s; gate 1 (3 capitals) clean; gate 2
  (5 capitals) stalled: capital-vs-capital avoidance at adjacent grid points (666 m pitch vs C 1.9–2.6 km) + the join
  fallback latching (3 of 5 at 25 m/s) + fallback approaches evading the gate itself; no KC slot queue involvement.
- Replay conformance on the Run158 trace (`61eeb790`): lag residual p95 0.00°; open model gaps: speed-law "held below des"
  on order 4 (26,861 steps), order-5 roll (2,804).

## In flight when compacted
- Design agent (Opus) writing amendment 16 "capital crowd at one gate" into `docs/architecture/capital-gate-transit.md`
  (agent id a99e0b6e6c9330ca5; may have finished: check the note for "## Amendment 16"). Decisions already given to it:
  lane rule (different lanes = exempt from detour and E warn with no speed/spacing condition; same lane = trailing
  capital brakes to follow, no sideways detour; crossing lanes = full avoidance), fallback re-evaluated every second
  (un-latch), the gate-warn row extended to fallback approaches, the remaining `pass_not_resolved` evasions classified.
- Next after ratification: fresh `implement-deep` (Opus; Fable agents were twice cut off by spurious API safeguard
  errors this session) in a worktree on main, then one `review` (Opus), commit, merge, clean build in a detached
  worktree `/tmp/x3-run159-candidate`, install into `C:\X3` with rollback `.run158`, records
  `verification/results/run159-candidate-{build,install}.json`, status line 3, user-runs row 159 A, commit.

## User decisions (binding)
- Capitals ignore ships smaller than a third of their radius whatever those ships do (engine "large" list does not
  override); small ships avoid capitals. Exceptions kept: manual player ship, docked/attached, masked classes, ships on
  dock legs 5/0x1e/0x20 (safety call by the orchestrator, user informed).
- Formation membership remembered (20 s, native id `[obj+0x08]`, same-frame drops on leader loss/role change/player).
- Gate queue script patch (clearance test) deferred until a flight shows real slot contention (none yet).
- Path planning (item 3) deferred: `docs/architecture/sector-path-planning.md`.
- Fixtures must simulate the real engine: calibrated model + `ship_trace` + `ship_trace_replay.py`; thresholds from
  measured numbers; every flight is replayed.

## Scenario backlog (from `docs/architecture/ship-steering-scenario-audit.md` + flights)
1. Ships evading their own dock station on order 4 (TP 56 s in Run157): station analogue of the gate row; needs a
   steering-off baseline. 2. Capitals jumping in together (exit spread is non-capital only). 3. 52 km bases on the
   sphere fallback. 4. Asteroid sectors vs the 1024 snapshot cap. 5. M7 gate path. 6. Stock records (all flights on
   Mayhem). 7. Combat not yet watched. 8. Save/load mid-approach. 9. "held below des" model gap.

## Process notes
- Two reviews (Opus + Fable) per hook change paid off every time; run them in parallel.
- Rotate implementer agents before their context limit (memory note `rotate-implementer-agents`).
- Ghidra: `~/ghidra_12.1.4_PUBLIC`, project `~/x3-ghidra-research` X3Render, `flock /tmp/x3-ghidra.lock`.
- Install procedure: memory note `wsl2-native-windows-host` (tasklist check, rollback copy, cmp, JSON records).
- Key docs: `docs/architecture/ship-steering-and-avoidance.md` (amendments 1–14), `capital-gate-transit.md`
  (amendments 15–16), `slip-aware-steering.md`, `ship-steering-scenario-audit.md`; ledger
  `docs/verification/ship-steering.md`; RE `docs/reverse-engineering/ship-movement.md`, `ship-navigation-ai.md`
  (§10 gates, §10.9 hole), `gate-queue-script.md`, `game-scale-and-ship-classes.md`.
