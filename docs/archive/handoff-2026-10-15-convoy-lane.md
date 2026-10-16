# Handoff 2026-10-15: convoy lane stage 1 flown (Run173), fix round 1 committed in a worktree, round 2 in flight (session compaction)

`docs/status.md` line 3 is the current-state line; this entry carries only what a fresh session needs to resume.

## Installed and flown
- **Run173 installed** in native `C:\X3` (main `785ae270`, DLL `e93696ae…` 59,776,039 B; `x3m.ini` has `ship_steering_gates_lane = 1`
  appended, the previous ini kept as `C:\X3\x3m.ini.run172`); rollback `C:\X3\rollback-d3d9\d3d9.dll.run172` (with `x3m.ini.run172`),
  or `ship_steering_gates_lane = 0` for stage B (Run169 behaviour) on the same DLL. Run165's DLL `abd17f60…` must never be flown.
- Engine trails are done and accepted (Run 172 A): ribbon anchored inside the plume, none on the piloted ship (`engine_ribbons_own_ship`).
- **Run 173 A completed** (row in `docs/verification/user-runs.md`; triage `verification/results/ship-steering/run173-triage/`, the log
  is gitignored): clean session, span 785 game s (design 185-235). The user saw the leader crawl at ~12 m/s before the gate and the
  escorts circle. Measured: (A) a 116 s KC through-slot refusal shown as a crawl because site B latched the speed after a hand-over whose
  P2 did not keep (nose 22°); (B) the lane join aimed at a fixed point on the line (s = -3.8 km) so ships flew ~20° off-axis, the forward
  ray crossed the gate disc and the ENGINE's own 0x12 against the gate fired at ~6.9 km (outside site E's 6 r own-gate suppression):
  6 evictions, turn-aways, E1 overshot the plane and was ranked behind E2 (10 m/s merge, 32.6 s stop, 6 km outbound turn). No stage-B
  fallback ran. The fixture missed it because its engine model had no gate-disc warn (added in round 1).

## Design and decisions (binding; `docs/architecture/convoy-gate-transit.md` §0 ratification)
- One departure lane per gate; the user's car-merge rule verbatim (the ship reaching the merge point later yields until one spacing
  behind; the passer never slows; a brief stop is fine); teleport rejected; no DLL-issued gate orders (the transfer is script-driven);
  stage 1 without script patches, stage 2 (x3story.obj A1 60 s → 10 s block; the escort template wait) only if the flown latency is still
  visible; player-owned AI ships are lane members like NPCs (only the manually piloted ship is excluded; no owner read); no cruise-speed
  change between gates.
- The user's answers on residual causes (2026-10-15): capitals already ignore ships < r/3 (done); the remaining engine evasions
  against stations/asteroids/mid-size ships for a lane member whose path clears them should be suppressed → round 2 item 1; a script
  re-target of an escort to a new leader is correct behaviour, only the lane exit must be clean → round 2 item 2; slot block and merges
  accepted as designed; the 180° turn of a re-departing ship accepted; SETA must keep working (all bounds game-time + step; fixture at
  tf 1/6/10).
- **Kill criterion** (proposed by me, not objected): two more flights. If Run174 still shows a ship turning away from the gate for a
  reason the rows attribute to the lane itself (not an obstacle, the script re-target or the slot block), stop the lane and ship a
  "stage A only" mode (the certificate + the script's grid queue: stopped capitals waiting, one through at a time; a one-key change, not
  yet wired). If only script-owned waits remain, do stage 2 and call it done.

## Work in flight when compacted
- Worktree `/home/asvetl/x3-mod/.claude/worktrees/agent-a819d8ebd8e7ac953` (branch `worktree-agent-a819d8ebd8e7ac953`), based on main
  `dd07e95e`:
  - **Round 1 committed as `77f9e88e`** (tangent parabola join meeting Λ at 15/8 km; site E own-gate suppression for lane members with
    `gate_row=lane|roll`; the hand-over never latches an unkept speed, brakes to a stop at d_end on refusal, `ctl_x_wait_s` from the
    refusal; overshoot rejoins as head; stop law release at T_stop + step and counted; re-commit hysteresis; `ship_gate_ctl_defer` row,
    `gate_row=` on warn pre-rows, gate frames in the summary; fixture engine model with the gate warn; casts run173, run173_15km,
    overshoot, p2_miss). Lane 1902/1902, full 12423, capital 6765, seq-off 12333, b9 45; key-off diff 0; 165 fail-before.
  - **Round 1 review (Opus) found, must be fixed before any build**: (1, high, Terran ring r 3207 m) the hand-over branch suppresses the
    engine's gate warn inside the bounding sphere with no clearance test (200 m of the final + nose 8°: a hull can be ~470 m outside the
    hole); (4) before d_end the "path clears the gate" test never discriminates (134,446 lane / 0 roll / 9 guard frames); (10) on the
    Terran ring Λ's d_end point lies inside the sphere so the far-out 0x12s return; (2, 3, 8, 9) overshoot ranking with a follower inside
    the return zone (Run173's case), `over` set for ships that started behind the gate, unbounded block, P2-not-kept with a follower at
    spacing; (5, 6, 7, 11-13) sphere premise false (outer 1.026 r), snapshot nose instead of live, one-sided freshness, assertions only in
    one cast, comments/metrics.
  - **Round 2 agent running on that worktree** (brief: items 1-3 = obstacle suppression for lane members against stations/asteroids/
    mid-size ships with casts; clean escort re-target exit with a cast; round-1 open flight risks (follower in the return zone, the follow
    gap covering a predecessor's full stop at a refused hand-over, shorter overshoot leg, bounded stops); addendum items 4-6 = the review
    findings above incl. one geometric clearance test for every lane suppression (live position/velocity, hull capsule swept through the
    ring hole: h 0.663 r, slab, outer 1.026 r, `gate_hole.txt`, §10.9), a Terran-ring cast in every variant, overshoot/block fixes, the
    assertion coverage). It commits per item in the worktree and updates §11 + the ledger. If it is gone after compaction: `git log`
    the worktree, read §11 "Open" and the ledger's last "Lane fix round" entry, and continue with a fresh `implement-deep` on the same
    worktree from the first unfinished item.

## Next steps (in order)
1. Round 2 report → one review (Opus; Fable too for the suppression geometry, it is safety) → fixes → squash-merge onto main
   (regenerate `tools/config/generate.py` if the schema conflicts) → clean build `/tmp/x3-run174-candidate` → gates (0 warnings,
   `check_no_x87.py`, `verify_ship_steering_sites.py --exe /mnt/c/X3/X3AP.exe` 20 checks, the 5 focused unittest modules, the engine
   discover modules) → install Run174 (rollback `d3d9.dll.run173`; the lane key stays on) → records, status line 3, Run 174 A row,
   `archive_user_runs.py --check`, commit. The user flies a convoy session like Run173 (--debug, SETA as usual).
2. Triage Run174 with the round-1/2 rows (`ship_gate_ctl_lane/merge/rewrite/defer`, `gate_row=`, `obst_row=`, the lane summary);
   apply the kill criterion.
3. Then: stage 2 script patches if the script latency/block is the visible residual; the arrival fan (§6.7); the abreast variant
   (engine allows two capitals on order 5/8, §10.12 item 1); the i686 24-member cost (flight `ctl_frame_us_max`).

## Process notes (binding)
- AGENTS.md / CLAUDE.md; never launch the game; no Wine on this host (the engine-ribbons/effects Wine record-hash tests stay red,
  accepted); every time bound game-time + frame step (memory `seta-aware-time-bounds`); rotate implementers near their context limit
  (memory `rotate-implementer-agents`): they commit WIP in the worktree and write §11; two reviews for hook/write-path/safety changes;
  resume killed agents with SendMessage; install procedure per memory `wsl2-native-windows-host` (tasklist check, cp rollback, cp, cmp,
  JSON records). The fixture's `keyoff_diff.py` vs `9f764d69` must stay at 0 lines (stage B byte-identical under the key off).
- RE facts the lane relies on: `ship-navigation-ai.md` §10.12 (f+0x240 read live per tick; 0x14 ends one frame after the leader's
  order 8; order 0 zeroes speed, site B's write stands; KC resumes one frame after an order; owner int16 [obj+0x3c]==10; two capitals may
  be on order 5/8 at a gate) and §10.13 (f+0x210 re-plan stamp: 15 SA writers; signed compare; the deferral safe when based on the game
  clock). The engine's own gate warn: a lane member ~1 km below the axis is inside the cone from ~7 km regardless of the join.

## Overnight continuation (2026-10-15, autonomous; the user asleep, standing instruction "finish, review the whole work, improve/refactor, don't wait")

Done, in order (main): round 2 finished (two implementers), reviewed (Opus round; Fable suppression geometry), fixes applied,
squash-merged as `564b3df8`; **Run174 installed** (`71118dd7`, DLL `052b76cb…`, rollback `d3d9.dll.run173`, lane key on; Run 174 A
open); holistic design review ratified (`a8e38093`, `ship-steering-holistic-review.md`, ranks 1–5); engine paths disassembled
(`7cfd0fd6`, ship-navigation-ai.md §10.14: the box test is a distance test, M6/TM/M8 are tested, warn on 0x14/0x15, jump arrivals
through the gate launch, Mayhem re-target scripts); **stage B deleted** (`bf12aaa2`: key 1 = lane (now the schema/launcher default),
0 = stage A only; lane-on output identical; reviewed on Opus, findings applied). In flight when this was written: the source split
(rank 3, worktree agent-ab040198cf619261b, plus the DLL-side default flip of the lane key) and the fixture engine paths (rank 5,
worktree agent-a33727fb5cfd876aa: §10.14 model corrections, the 0x12 flight, stations/asteroids on Λ, small ships at the gate, the
arrival hull). Decision: Run174 stays installed for the user's flight; the deletion/split build becomes Run175 only after Run174 is
flown (lane-on identical; a second DLL before the flight would confound the triage). Open after these: rank 4 (one gap law, no-write
hand-over), behind_start (a commit from behind the gate plane), the parked cast's 30 s horizon, the arrival fan.

Later the same day (main, in order): the source split merged (`358c0f06`; the DLL's lane default 1), a Run175 candidate gated from
it (`b8695eb1`, not installed); the fixture engine paths merged (`305aa2b6`: the §10.14 warn model, the flown 0x12, statics on Λ, small
ships at the gate, the arrival hull; nine casts parked); §12 design (Fable) ratified (`71b1536d`); round A (one gap law + band,
one yield candidate, no-write hand-over, the arrival pair: `fd3f67d1`), round B (small ships, the lateral profile with the shift
off, the enclosing-fallback rule: `d2871285`), the x87 fix (`c6b4a88b`), round C (the loop-back join after a flown 0x12:
`e7a7120c`). Still parked: arrival_parked (the fan), lane_station, lane_belt, behind_start. In flight: the review-fix round for
rounds A–C (worktree agent-a8149e66014f6f765; Opus + Fable findings: the arrival's a_lin at the launch hook, the arrival stop's
bound in the rollout, the small-ship hold inside a warn zone and at its bound's end, the enclosing-fallback size rule, rows for
the launch hook's first decision and the loop, §11 parked numbers on the final tree). Then: rebuild the Run175 candidate from
the result, gate it, and install it only after the user's Run174 flight is triaged (or if the user asks for the newest build).

## 2026-10-16: Runs 174–177

Run174 (lane fix rounds 1–2): departures clean, an arriving group that turned back circled (our out-leg, a board leave on our
own detour). Run175 (the overnight set + the arrival round; two sessions): arrivals clean, a late escort's grid miss, the width
fallback, lane laws along F on far-off escorts. Run176 (the Run175 round): departures clean, the return trip collapsed on the
gate-mouth hold's rest point. The user's question ("is predictive avoidance causing it, disable near gates?") answered no with
evidence; the user chose simplicity: **the minimal arrival mode** (§13, measured: with our cap and lead-in the mode was worse
than vanilla on the return-trip cast; without them identical, 300/300): an arrival ordered back through its gate within 900 s is
vanilla for the approach, departures byte-identical. **Run177 installed** (`b54c0689`, DLL `76252ce0…`, rollback run176; Run 177 A
open): the flight judges departures unchanged (kill_criterion.py) and whether vanilla arrivals look acceptable. Parked: the funnel
far bound (escorts far out governed by the lane: two bounding rules measured worse), rt_cross, statics, behind_start, formed_size2.
Open diagnostics: the 4–5 ms handler chunk on a ship's first call after a gate (per-phase cycles now logged), the 1↔33 gate
effect nodes (parts=/effects= now logged). Memory: prefer-simplicity.

## 2026-10-16 (later): Runs 177–178

Run177 flown: the user saw nothing wrong; departures unchanged (471.7 / 254 / 267 s, 0 stops/turn-aways); the arrival mode was
not exercised (no return trip within the window); one far-out defect (escorts inherited by an arriving leader 75 km behind them,
held at the floor by the leader-first rule; a 0x12 pair; a 615 s loop; launched in 706 s). The user asked whether 900 s is too
long: agreed the distance condition (a return trip only while inside the gate's 15 km commit sphere) → merged (`bdeef915`).
The Run177 far-out round was attempted twice on branch `worktree-agent-a4700e7f0d9b84b1b` (kept, not merged: a departure-side
regression risk in bench24 and a fixture formation-model gap: the formed casts start escorts in a column beyond 15 km, which the
engine formation model collides) → parked (§11 Open). **Run178 installed** (`1170bb01`, DLL `9d87fea2…`, rollback run177;
Run 178 A open): Run177 + the distance condition, otherwise byte-identical. Next: the user flies normally; a return trip at a gate
exercises the arrival mode; the parked items wait for a flown reason (far-out re-led escorts; two arrivals overlapping; statics;
behind_start; formed_size2; rt_cross). The host-timing bench (1 ms third-worst) flakes on this host for base builds too.
