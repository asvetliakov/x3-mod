# Ship steering, avoidance and gate transit: a fresh-eyes design review

Design note, 2026-10-15 (Fable 5.1, design agent), for the main session. The user asked for a review of the whole stack as a
whole (steering, avoidance, gate transit) with the question "what do we not handle, what is over-built, what should be
rewritten". Read-only; nothing implemented, nothing launched. State reviewed: main `12570c7b` plus the lane worktree
`agent-a819d8ebd8e7ac953` (round 1 `77f9e88e` committed, round 2 uncommitted: the lane obstacle row `lane_obst_why`,
`e_obst_row`, `ctl_e_obst`). Sources: [ship-steering-and-avoidance.md](ship-steering-and-avoidance.md) (amendments 4–14),
[ship-steering-scenario-audit.md](ship-steering-scenario-audit.md), [slip-aware-steering.md](slip-aware-steering.md),
[capital-gate-transit.md](capital-gate-transit.md), [gate-traffic-controller.md](gate-traffic-controller.md) (D.1–D.6, B.6–B.9),
[convoy-gate-transit.md](convoy-gate-transit.md) (§0–§11, the worktree copy), the Run173 handoff,
[ship-navigation-ai.md](../reverse-engineering/ship-navigation-ai.md) §10.12–10.13, user-runs rows 160–173, and the source
named below. Markers: **[m]** measured or read from code/rows in this session, **[i]** inferred, **[p]** proposed. Evidence:
[`verification/results/ship-steering/holistic-review/region_lines.sh`](../../verification/results/ship-steering/holistic-review/region_lines.sh)
→ [`region_lines.txt`](../../verification/results/ship-steering/holistic-review/region_lines.txt) (line counts per region, re-runnable
after a refactor).

Binding decisions not reopened here: capitals ignore ships under r/3; leader goes, escorts go, no waiting; the later of two
abreast escorts yields briefly, the passer never slows; player-owned AI ships are NPC-like, only the piloted ship is excluded;
every time bound is game time plus the SETA step; never launch; no Wine on this host.

## Recommendation

Fly Run174 on round 2 as planned and apply the kill criterion; change no design before it. The lane is the right shape (one
1-D law per gate on top of a certificate), and the remaining Run173 symptoms were geometry and a latch, not the concept. After
Run174, do three things in this order, each with a fixture check that fails before: (1) **delete what the defaults already
bypass** (the amendment-16 lane board, stage B's petals/sequencer, the capital-seq key, site W) and split
`ship_gate_ctl_core.h` / `ship_steering.cpp` along the ownership lines of §3; (2) **collapse the lane's speed laws** to one
gap law plus the lateral band, the escort reserve and the block stop, and make the hand-over "P2 not kept = no write" (§2);
(3) **model the engine paths the fixture still lacks** that a green run cannot exclude (§4), the 0x12 flight and the small-ship
column above all, before the next lane change. The stage-2 `wait` constant patch is the cheapest remaining span win and
should follow Run174 if the escort gate's tf-1 cost is visible. The (d2) echelon, a KC slot patch, box hulls for capital
pairs and a cruise-speed change stay off the list.

What this costs on the hot path: nothing new; §2's simplifications remove work (one law instead of two, one yield candidate
instead of three, no `refused` timer). Native Windows: the stack is in-process x86 on pinned sites; Runs 151–173 were native
**[m]**; nothing here adds an OS API or a site.

## Ratification (orchestrator, 2026-10-15)

Ranks 1–5 of §5 are ratified in that order, under the user's standing instruction of 2026-10-15 ("review the whole work;
improve/rewrite/refactor; do it, don't wait for me"): Run174 is built from round 2 only; the deletion (rank 2) and the split
(rank 3) follow as output-identical rounds with their own candidate; the single gap law and the no-write hand-over (rank 4) and
the four fixture engine paths (rank 5) each get a fail-before cast and two reviews before any install. Ranks 6–7 wait for
Run174's triage; ranks 8–9 are rejected as written.

## 1. Cases the stack does not handle (question 1)

"Matters" is for the user's gameplay as flown (Mayhem 4, capital convoys with escorts, crowds of 5–7 capitals at one gate,
SETA on most of the time, the player watching from the sector). Status: **H** = handled by design and cast, **P** = partly
(a rule exists, no cast or no flight row), **N** = not handled.

| # | Case | Where it would show in flight | Matters | Why, and status |
| --- | --- | --- | --- | --- |
| 1 | Multiple gates per sector; a convoy split across gates | Two lanes near each other are independent boards (`kGates` = 4 **[m]**); no cross-board certificate (§6.6) | low | Gates sit tens of km apart in stock maps; the script sends a convoy to one gate. A crossing pair of two boards keeps the engine's warn and the sphere avoidance: an eviction and a re-merge at worst. **P**: no cast; `x12_pairs.py` would show it as a non-member pair. |
| 2 | Arrival side: hulls emerging into the lane's path | A capital arrives +0.6 r up (D.4), 0x1e axis-parallel; a hull ≥ 0.99 r_gate rests **at P** until its script moves it (§10.10) | **medium** | The lane is ≥ 0.75 r + 100 m below the axis, so an arrival at P and a departure at Q clear by ≈ 500 m for census Titans **[i]** from D.2.1 numbers; the KC blocks our side 60 s after an arrival (head paces/stops). **N** for the next arrival into an occupied P: the arrival fan (§6.7) is not implemented, and what the arrived hull does before the next one appears is open (§8 item 11). With A6 unpatched, in-sector arrivals come 60–72 s apart **[m]**, which hides it unless a script parks the first hull. Flight row `ship_gate_launch` + member rows with the player in the destination sector settle it. |
| 3 | Mixed hull sizes: M6/TS/TM escorts with an M2 leader | Small escorts are not capital-coded, so not lane members; after the leader's launch they get their own order 4 to A = G + (0, −0.8, −1) r with the 0.25 r spread and the −3 r..0 window; Λ's column (l_c ≈ −0.84…−0.93 r U, R ≈ 0: `lane_q` aims straight down) runs through that lead-in **[m]** `lane_q`, amendment 10 | **medium** | Small ships avoid capitals (sphere, 1.75 C) and the engine warns them; the capital ignores them (r/3). Symptom: small escorts and trade traffic weave around the column on the last 3 km and stop→0x12→re-issue against members (the Run157 pattern, 29 of 39 small first-0x12 rows named the gate). D.5's mirror only moves the order-5 point B to the free side; the lead-in line and D.3's corridor projection (small escorts in formation) are stage-B text, not lane code (no `corridor` symbol in the lane **[m]**). **N** in the fixture: cast `small_escort` is an r 500 m *capital-coded* escort. |
| 4 | Leader dies / docks / jumps mid-lane | Dies: cast `leader_killed` green **[m]**. Docks in sector: 0x14 ends (parent not a sector, §10.12 item 3) → the escort leaves the board by `validity` after its gap → joins again on its own order. Jumps (JumpToSector): no order 5/8 at this gate; the escort's reserve runs from `t_l = now` (`lane_frame`: no `res_until`, leader not committed) → it crawls to d_end until its order 4 | low | Handled by the generic rules; the jumped-leader case costs the escort one reserve crawl (≤ 200 m at 10 m/s). **P**: dock/jump not cast. |
| 5 | Station traffic lanes and stations near a gate | A station on Λ within 15 km: the sphere avoidance detours 1.75 C (C = r_A + r_station + margin: several km for a 1 km hull) and `lane_on` drops beyond 1.5 km lateral (`kLaneOffM`) → the member becomes a joiner again; the engine's warn against the station was the Run173 residual the user named | **medium–high** | Round 2 item 1 suppresses the *engine's* warn when the path clears; the module's own sphere detour stays and is what the `parked` cast shows (185–191° turns, P2 not kept **[m]**). A 52 km hollow base that fails the part walk gets an 18.4 km fallback body (audit row 19): a lane inside it brakes every member to 10 m/s at d = C and reports unresolved on every call **[i]**, not flown. **P**: a lane-aware lateral shift within the Q window (instead of the sphere detour) is the open item; cast it with a station 8 km out on Λ and with a 30 km fallback body. |
| 6 | The player's ship crossing the lane or sitting in the gate | The piloted ship is an obstacle for every member (excepted from r/3; round 2's obstacle row never covers it) → sphere detour + engine warn → eviction → re-merge (2 s + step hysteresis) | low | Correct by the binding rule (safety over smoothness). At the disc, order 5/8 ignores warns and the pair filter skips ship–gate only; a capital on order 8 is moved kinematically through the player — vanilla behaviour, no regression. **H**. |
| 7 | Save/load with ships mid-lane | Boards are in-process: rebuilt by ETA on load; the written L_i persists (a valid point in front of the gate); a deferred `f+0x210` costs ≤ 2.5 s + step (§10.13 item 4); clock-reset branches clear caps and sticky orders **[m]** | low | **P**: `lane_point_lc_env` adopts "a non-lane field at L_i's depth inside the window" as ours **[i]** (final review item 4) — benign. An escort saved in its lost-leader wait (f+0x224 = 0, no leader on the board) has no deferral after load: its first order-4 tick tests the KC grid point, P2 at the grid depth, a longer order-5 leg (the pre-lane behaviour once). No cast; a unit exists for the clock wrap. |
| 8 | SETA changes mid-manoeuvre | Every bound is nominal + max(this, previous step) (`age_bound`, `note_step`) **[m]**; the trigger sphere inflates with tf, so under SETA order 5 ends on its first frame at d_end and order 8 runs from there (ends ≈ 0.4 km past the plane) | low | A toggle between frames changes which branch runs, not whether it is safe: both T_slot regimes are flown in the fixture (tf 1/6/10 per run). **P**: no cast toggles tf mid-run; the known residual is the escort gate binding at tf 1 only (median 11.4 s per pair **[m]**). |
| 9 | More than ~8 lane members | `kMembers` = 24 **[m]**; a 15 km Λ holds 6 census hulls at 2.5 km; the 7th+ queue beyond 15 km on the follow law. The decision budget: 24 members at tf 10 cost 551–807 µs worst per frame on the x86-64 host **[m]**; i686 unmeasured (Run173: `ctl_frame_us_max` 53 µs for 3 members **[m]**). Under the 1 ms ceiling (`ctl_budget_cut`) stale certificates pass pairs to the engine | **medium** | The user flies crowds of 5–7; Mayhem can stack more at a hub gate. Symptom at scale: evictions and turn-aways return under SETA 10 as verdicts age past 250 ms. **P**: the bench exists; the flight figure for ≥ 10 members does not. The §2 simplifications cut the per-decision work. |
| 10 | Two convoys at one gate from opposite sides | Opposite *sectors*: the partner block (60 s), cast `partner block` green **[m]**. Opposite *lateral* sides in one sector: both join Λ by ETA (cast `crossing`, 335–341 s **[m]**). From *behind the plane* (s > 0 far out): the own-gate row's segment to Λ's d_end point crosses the gate sphere → not suppressed → engine 0x12 → eviction and loop (stage-B "from behind" behaviour) | low | Ships inside a sector are in front of their gates except within ~r after arriving (the re-departure, cast green). **P** for the far-behind start: not cast. |
| 11 | Asteroids near the gate axis | A rock on Λ: sphere detour (C = r_A + r_rock + margin) and, until round 2, the engine's warn; a detour > 1.5 km lateral drops `lane_on` | **medium** | Mayhem sectors carry up to 732 rocks (game-scale §6); a 15 km × 2 km column crosses rocks in belts. Symptom: members weaving, followers' gaps measured along F only (a detouring predecessor keeps its `lane_s`) while the certificate holds the pair. Round 2 adds asteroid casts for the *warn*; the lateral-shift item of row 5 is the real fix. **P**. |
| 12 | Jumpdrive arrivals and departures | Arrivals take the launch site `0x00444170` like gate arrivals **[i]** (D.4; §10.10 Q2 names the post-pass launch for arrivals, the jump path not re-read); a jump *departure* takes the through slot without order 5/8 (§7): the head sees an unpredicted refusal, counted | low | The refusal path (brake to a stop at d_end, ≤ 60 s) covers it. **P**: the jump-in placement path is inferred. |
| 13 | Out-of-sector ships | No native objects, no hooks (audit row 16) **[m]**; the KC's own queue sends OOS convoys through one capital per 60–72 s | none in flight | **H** by construction; it is why in-sector arrivals are sparse (row 2). |
| 14 | The script's `SA_CollisionObject` → 0x12 at order-4 issue | `__EnterHyperspace` phase 0 starts 0x12 instead of order 4 when the warn bits stand **[m]** §7 table | low–medium | A warn fired in the escort's lost-leader wait (no decision of A on order 0; round 2's row covers that) puts the escort on 0x12 at the moment it should commit: eviction + re-commit. **P**: not modelled in the fixture (§4). |
| 15 | A refused warp-in slot (20 holders): `SA_SetSpeedLimit`, `SA_Idle` wander | `f+0x278` caps site B's write (§10.12 item 4); the ship is on 0x1d, steered, not a member | low | Only with > 20 capitals at one gate pair. **N**: no read of `f+0x278`, no cast; a `ship_gate_ctl_lane` row with `v_w` above the live speed would show it. |

Cases that are handled and cast, for the record: abreast merges, stragglers, re-departure, the slow hull, the leader killed, a
partner block, the P2-not-kept refusal, the overshoot, SETA 1/6/10 **[m]** scenario C.

## 2. Where the design is over-built, and the simpler equivalent (question 2)

**(a) Stage B kept byte-identical under the key off.** Petals, the lap law, release window, head skip, escort stage, B.6–B.9:
548 + 834 lines of `ship_gate_ctl_core.h` (210 lines mention petals; 36 lines name the petal, hold and lap functions), ~25 `Member` fields, and
7,026 lines of fixture (scenario T, B.3–B.9) **[m]** `region_lines.txt`. It runs only with `ship_steering_gates_lane = 0`. What
it buys: a rollback path that the user rejected in eight flights (rows 162–169: "still clunky", spans 361–2,170 s **[m]**).
What it costs: every lane change must keep `keyoff_diff` at 0, `Plan::petal/hold`, `Sb::hold`, `hold_law`, `rollout_target`
and the yield ladder's "25 m/s on a petal" branch stay in the shared rollout, and the full host run executes scenario T every
time. **Simpler equivalent [p]:** key off = stage A (the certificate, D.4 placement, D.5, the KC's own grid queue with stopped
capitals), which is also the handoff's kill-criterion fallback and is one-key, not yet wired. The DLL for rollback is a
*previous build* (`rollback-d3d9`), not a code path.

**(b) The lane's speed laws** (`Law`: join, lane, merge, follow, stop, pace, gate, hold). Merge and follow are the same linear
gap law with two gains (τ_m 4 s, τ_f 10 s), a `merged` latch with hysteresis, a `merge_done` row state, a join-loss credit
capped at g_req − 50 m, a settling-gap speed term and the beside-band on the raw gap (`lane_law`, review item 6 and its
deadlock history **[m]**). What the split buys: a 50.3 s merge becomes 42.7 s (τ_m 8 → 4 **[m]**); τ_m 8 s changed spans by 0 s.
**Simpler equivalent [p]:** one law `v_Y = min(v_lane, v_P + (gap_settle − g_req)/τ)`, τ = τ_f, floor 10 m/s, with the lateral
rule unchanged (hold the clearance band until the raw gap ≥ g_req − 100 m); "merge" becomes a row label (raw gap < g_req − 50 m
with a predecessor), not a state. Expected cost ≤ 8 s per merge **[i]** from the τ_m sweep; the fixture's merge bound decides.
`pace` binds only behind a partner block or a KC refusal (its comment and scenario C **[m]**) and is 8 lines: keep. `gate` (the
escort reserve) is a safety rule and stays; its tf-1 cost (median 11.4 s per leader→escort pair **[m]**) is the price of not
knowing when the script orders, and the stage-2 `wait` patch (λ ≤ 1 s) is the cheaper remedy than more reserve tuning.

**(c) The certificate's rollouts and yields.** The capsule sweep is the one independent safety check and its flown cost is small
(53 µs max frame, 3 members **[m]**): keep it. What is stage B's and now dead weight under the lane: the three-candidate yield
ladder (0.6 v, 25, 10 m/s; 25 m/s floor on a petal), `gap_yield`/`cap_in_force` (built because stage B wrote nothing on order 0;
the lane writes a hold on order 0), the petal/hold/inbound plans in `rollout`, `rollout_target`, and "front ship first"
coupling through the candidate rollouts. **Simpler equivalent [p]:** under the lane the law is the only speed source; a
yielder's single candidate is the law with the gap target raised by the sweep's margin (g_req + 50 m + k t), else the stop. One
rollout per conflict instead of up to three.

**(d) The hand-over and refusal logic.** `lane_site_b` has three write kinds (kept / unkept / hold), a `refused` flag after
5 s + step, `lane_refused_hold`'s 11 s poll bound, `p2_kept` from the P2 hook, `ctl_x_wait_s` accrual, and round 1's "unkept =
lane speed under the brake bound until refused, then brake to a stop" (an accelerate-then-brake on order 0). The root cause is
that the DLL cannot see the KC's grant, so it infers one from time. **Simpler equivalent [p]:** apply the brake bound from P2
always (it cannot bind on a granted slot: order 5 starts two frames after P2, §10.12 item 5 **[m]**; the bound only lowers the
speed when 11 s of travel would reach the trigger sphere), drop the `refused` timer to a counter, and on **P2 not kept write
nothing** (the vanilla stop: 0x1f rotates, order 5 starts from rest; ≈ 20 s once **[i]** for a 1.2 km leg at 5 m/s²). P2 not
kept is rare on the lane (nose ≤ 5.8° in every cast but `parked` **[m]**) and the accelerate-then-brake is the kind of motion
the user reads as "crawling".

**(e) Two gate boards for one thing.** The amendment-16 lane board (`ship_capital_lane_core.h`, 835 lines; `ln::run_pass`,
`verdict_sym`, `handover`, `mark`) runs only when `gates_capital_on_ && !traffic_on_` **[m]** `ship_steering.cpp` 1835, 3724,
3299; `ship_steering_gates_traffic` is builtin 1 since 2026-10-13 **[m]** schema. It has been dead in every flown build since
Run161 **[i]**, together with `ship_steering_gates_capital_seq` and its counters/rows (`ship_capital_lane`, `latch`, 2,197
fixture lines). The join predictor's `slow_join`/`fallback` latch (amendment 16/17) is reached only for a capital the board
refuses (`sb_member` false) **[m]** decide() 2021–2028: a fallback of a fallback.

**(f) Diagnostics that outgrew their reader.** `ship_steering.cpp` carries about 20 row/event structs **[m]** and a per-kind drop enum; the
summary prints the amendment-16 constants (`lane_gap_m`, `corridor_m`, `slow_join_*`) of a path that does not run. Not harmful,
but every triage script that parses them is a maintenance surface.

**Not over-built:** the steering law and the slip model (amendments 4–14; the fixture's calibrated plant is the reason the lane
could be designed on paper), the site set (A/B/E/P2/P4/P5 + `0x00444606`: no new site since stage A), the avoidance core, the
B.5 pointer liveness, and the SETA-aware `age_bound`.

## 3. Refactoring the source (question 3)

Measured sizes **[m]** (`region_lines.txt`): `ship_gate_ctl_core.h` 4,524 lines (board 547, stage-B geometry 548, lane
geometry/laws 375, rollout/sweep/decide/E rows 1,297, stage-B sequencer 834, lane frame/site B/steer 671, D.5 101);
`ship_steering.cpp` 5,203; `ship_steering_core.h` 1,686; `ship_avoidance_core.h` 1,439; the fixture 16,861.

**Split of `ship_gate_ctl_core.h` [p]** (namespace `ctl` unchanged; one header per owner, no behaviour change, the
`#include` order fixed by dependency):

| Unit | Owns | From |
| --- | --- | --- |
| `ship_gate_board_core.h` | `Sample/State/Member/Gate/Board/Index`, roles and `Why`, `enter/leave/locate/acquire_gate`, `validity`, `note_step/age_bound`, `state_from/hull_from/commit_fill/escort_fill` | 152–698 |
| `ship_gate_geometry_core.h` | the gate frame helpers, `q_point/q_axis`, `lambda5`, `line_carrot`, `dock_ramp`, `kc_grid_slot`, `lane_q`, `lane_lat_at/point_at/order_point`, `lane_point_*`, `lane_carrot`, `lane_join_*` | 699–780, 1247–1407 |
| `ship_gate_lane_core.h` | `LaneCtx`, `lane_reserve`, `lane_law`, `lane_refused_hold`, `lane_hold_o0`, `lane_frame` (order + laws), `lane_site_b`, `lane_defer_*`, `lane_steer`, round 2's `lane_obst_*` | 1408–1621, 3753–4423 |
| `ship_gate_certificate_core.h` | `rollout` (plans minus petal/hold), `sweep*`, `gap_scan`, `decide`, `try_yield`, `decide_order`, `frame_budget`, `e_certified/e_reason`, `sb_gate_why/e_gate_row` (the own-gate row), `note_warn/keep_through_avoid` | 1622–2823 |
| `ship_gate_arrival_core.h` | D.4 placement (`capital_offset`, `small_side_spawn`, `capital_at_plane`), `Arrivals`, `arrival_gate_row`, D.5 `funnel_active/small_b_*` | 2824–2918, 4424–4505 |
| `ship_gate_ctl_live_core.h` | unchanged | — |

Deleted in the same step (dead once the lane key is on by default and stage A is the off path): `PetalGeom/petal_geom/petal_at`,
`HoldState/plane_state/hold_law`, `lap_law/lap_speed`, `release_window`, `escort_stage_s`, `petal_pick`, `burst_keys/SeqTuple/
seq_tuple/seq_before`, `SbSeq/SbEvent`, `sb_frame`, `sb_steer`'s petal and funnel branches (keep its order-5 Λ5 branch, which
`lane_steer` duplicates), `Plan::petal/hold`, `Sb::hold`, `Member::{petal, ringed, unburst, esc_in10, burst_key, go_arounds,
res_o8_ms?, release_ms, plan_x_ms, plan_launch_ms, feas_ms, x_pred_ms, head_ms, infeas_ms, enter_d, v_feas, t_rel_ms, warn_stage}`
(each checked against the lane's readers before removal; `res_until/res_o8_ms` stay: the lane's escort gate reads them **[m]**),
`Gate::{rho, w_max, petal_ship, sb_go_arounds, sb_skips, head_t_ms, funnel_ship}`, the three-candidate ladder in `try_yield`.
Also delete `ship_capital_lane_core.h` and `lanes_` with `ship_steering_gates_capital_seq`, `lane_rows/lane_decision/
latch_row/join_row` and the amendment-16 counters; the site W stub (`x3m_ship_gate_warp_stub`, armed and inert since the
2026-10-13 correction **[m]**); the `JoinMode::slow_join/fallback` latch (keep `join_on_decision`'s `join` verdict only if P2's
`join_keep_verdict` still needs it for a non-member capital; otherwise the P2 rule becomes "member of the lane or vanilla").

**Split of `ship_steering.cpp` [p]:** `ship_steering_install.cpp` (stubs, `install_all`, `initialize/shutdown`, the key
parsing), `ship_steering_decide.cpp` (the site-A handler `decide()`, 780 lines today, with its gate branch extracted as
`gate_decide()`), `ship_steering_clamp.cpp` (site B: `clamp_apply`, `lane_clamp`, P2/P4/P5 handlers), `ship_steering_warn.cpp`
(site E: `e_verdict`, `e_record`, `ctl_e_fields`, `ctl_e_obst`, the warn_pre/engine_warn rows), `ship_gate_ctl_glue.cpp`
(`ctl_observe/commit/escort/frame`, `sb_frame_all` → `lane_frame_all`, `ctl_rows`, the arrival handler), `ship_dock_glue.cpp`
(`dock_*`), `ship_steering_rows.cpp` (row structs, counters, trace slots, the summary). The shared anonymous-namespace state
becomes one `struct Runtime` passed by reference, so each unit compiles alone and the fixture can include the glue it tests.

**Fixture:** scenario T and B.3–B.9 (7,026 lines) go with stage B; amendment 13–16 scenarios (3,923 lines) stay only where the
non-member capital path still exists. Keep scenario C, the unit checks of the kept cores, and the engine model.

**Order of work:** delete first (the key-off diff drops with it), then split (pure moves, `keyoff` replaced by a
"lane-on outputs identical" diff against the pre-split build), then the §2 simplifications each with its own fail-before check.

## 4. Engine paths the fixture's engine model still lacks (question 4)

The model today **[m]** (scenario C header, `Model`, the KC phases 0–6): the calibrated integrator with slip, the per-axis
controller, think ticks 0–49 ms, the KC's grid slots with the 60 s release, one through slot per pair with the direction
cooldown and the in-sector exemption, 1–11 s polls, release one frame after order 8, the trigger inflated by tf, the script's
re-issue gaps, the lost-leader order 0, λ ~ U(4.1, 16.9) s, 0x15 legs beyond 10 km, stage 2's λ, P2 → 0x1f → order 5 → P4 → order
8, the engine's own gate warn cone (round 1), the engine's warn between members (counted), a parked non-member through the
avoidance world, SETA steps 1/6/10 with 1.5–2 s hitches. Three flights failed on paths it lacked (memory `fixture-engine-model-gaps`;
Run163 the SETA step, Run168 the column, Run173 the gate warn). What it still lacks, with the symptom a green run would not exclude:

| # | Missing engine path | Flight symptom it would produce | Cheapest cast |
| --- | --- | --- | --- |
| 1 | **The 0x12 flight itself**: an evicted ship's turn-away trajectory, the 200 ms re-score, `SA_StopObject`, and the KC's re-issue of order 4 with a *new* grid slot after it. The fixture counts a 0x12 episode and leaves the ship on its plan | Run173's 172° turn-aways, E1's overshoot and the re-rank behind E2 came from flown 0x12 legs; a green run proves no episode starts, not what happens after one does (round 1's overshoot cast starts from the flown state) | fly the engine's avoid leaf (§3.3) for the episode's length, then re-issue order 4 at a free slot; assert the re-merge and the overshoot bound |
| 2 | The order-4 **container detour** (§10.12 item 2: within √5 r the leaf's point is replaced by a tangent for a side join) | a late joiner (re-departure, merge inside 2.5 km) steered at `S + r m̂` instead of its lane point: P2 not kept, a 0x1f rotate | the detour rule in the phase-0 leaf call (conditions a–e of §11.1) |
| 3 | Stations and asteroids as obstacles of a lane member, with the engine's close-range / box / cone warn (round 2 adds casts for the warn; the module's own detour on Λ exists only as `parked`) | the row-5/11 weaving; a fallback-body base braking the whole lane to 10 m/s | a station sphere + parts 8 km out on Λ; a 30 km fallback body enclosing Λ; a rock belt across the column |
| 4 | **Small (non-capital) ships at the same gate**: TS/M6 escorts, trade traffic on the lower arc with their lead-in, spread, D.5 mirror and the engine's warn against members | row 3: weaving and stop→0x12 legs of small ships among the column; the module's small-ship clamp interacting with members' E rows | a 1+2 capital convoy plus two TS escorts and a TS trader on order 4/5 to the same gate |
| 5 | `__EnterHyperspace` phase 0's `SA_CollisionObject` → **0x12 at order-4 issue** when the warn bits stand (§7) | an escort evicted at the moment it should commit; a re-commit after 2 s + step | set the warn bits in the model when any engine warn fired on the escort's order-0 frames; issue 0x12 instead of order 4 |
| 6 | The **arrival hull** physically at P during a lane pass (0x21 kinematics, 0x1e at 120 m/s, a hull ≥ 0.99 r resting at P), and the next arrival into an occupied P | a departing hull on order 8 passing within ≈ 500 m of a parked arrival; two arrivals overlapping (row 2) | scenario T's arrival model moved into C, with a second arrival 60 s later |
| 7 | **Mayhem's escort re-target** to a newly arrived M1 (round 2 item 2 adds the clean exit; the trigger itself is script text not re-read, §8 item 7) | an escort leaving the lane mid-merge and its follower's law seeing a new predecessor | a re-target event at a random lane moment (round 2's cast) |
| 8 | `f+0x278` (a refused warp-in slot's speed limit) capping site B's write; `SA_Idle` on 0x1d | row 15 | a 21st capital at the gate pair |
| 9 | The KC's `CollisionWarn` acting on **0x14/0x15** (open in §11; Run173 showed none) | an escort evicted from its trail by a warn the lane row does not cover | model the warn on escort orders once the listing settles it |
| 10 | A SETA **toggle mid-run** (tf changes the trigger sphere and the step bounds between frames) | a hand-over that straddles the two T_slot regimes; the reserve crawl binding/releasing | tf switched at the leader's P2 and at an escort's merge |
| 11 | **Stock (non-Mayhem) records** (1.2–3.6°/s capitals, audit row 8) | a join whose parabola needs D ≥ √(2 y R) at R = v/W ≈ 3–5 km: late merges, a fallback | one stock TL in the formed cast |
| 12 | The player's piloted ship and manual control near the lane | eviction and re-merge around the player; the obstacle row's refusal | a manual M6 crossing Λ at 5 km |
| 13 | i686 cost on the real DLL (the bench is x86-64 host); 144 fps and 12 seeds (deferred, §11 review item 7) | the 1 ms ceiling cutting decisions in a 10+ crowd under SETA (row 9) | the flight's `ctl_frame_us_max` with ≥ 10 members; the fixture's 144 fps variant |

Items 1, 3, 4 and 6 are the ones a green scenario C cannot speak for on the next flight; 2, 5 and 9 are listing-settled paths
that cost one branch each in the model.

## 5. Ranked recommendations (question 5)

| Rank | What | When | Why |
| --- | --- | --- | --- |
| 1 | Finish round 2 (the suppression geometry reviewed twice, as planned), build, install, **fly Run174**, apply the kill criterion | do now | The flight is the only test that includes the engine paths of §4; every design change before it delays the one measurement that decides stage 1 |
| 2 | **Delete** stage B, the amendment-16 lane board, the seq key, site W; make key-off = stage A; drop their fixture scenarios | after Run174 | Dead under the defaults (§2 a, e); removes the `keyoff` constraint and ~2,200 core (1,382 stage B + 835 lane board) + ~9,200 fixture lines **[m]** before any further lane work |
| 3 | **Split** the two large files along §3 (pure moves, output-identical diff) | after Run174, right after 2 | Every reviewer since B.1 has read 4,500 + 5,200 lines to check one law; the split makes the certificate and the lane reviewable alone |
| 4 | **One gap law + lateral band**; one yield candidate; hand-over "P2 not kept = no write", the brake bound from P2 without the `refused` timer (§2 b–d), each with a fail-before fixture check | after Run174 (fold the hand-over change into a round-2 follow-up if Run174 shows another crawl at d_end) | Removes the latches that produced Run173's crawl class; expected cost ≤ 8 s per merge **[i]** |
| 5 | **Fixture engine paths** §4 items 1, 3, 4, 6 (the 0x12 flight, stations/asteroids on Λ, small ships at the gate, the arrival hull) | after Run174, before the next lane change | The three failed flights were all missing-path failures; these four are the likeliest next ones |
| 6 | **Stage 2 `wait` patch** (the Mayhem template's 2–3 s → 0.1–0.3 s; hash-guarded loose file) | after Run174, if the escort gate's tf-1 cost is visible | The cheapest remaining span win (λ ≤ 1 s); the 10 km lift stays off unless stragglers beyond 10 km show |
| 7 | **Lane-aware lateral shift** for a non-member on Λ (move the member within its Q window / l_c, not the sphere detour), then the arrival fan | after 5 | Rows 5 and 11; the `parked` cast's 185–191° detour is the lane's worst remaining look |
| 8 | Cruise-speed change between gates (§10 item 1) | don't (until a formed convoy is seen arriving unformed on Run174+) | The user's question is open; stage 1 should leave convoys formed by itself |
| 9 | (d2) two-lane echelon with concurrent order 5; a KC through-slot patch (B/A5); box hulls for capital pairs (backlog 10); a sector path planner | don't | Each needs new KC state or a new site for a gain the lane already captures; the capsule sweep covers capital pairs |

## Hot path, cost, native Windows

No change is proposed before Run174. Items 2–4 remove work: one law evaluation per member per frame instead of two regimes
and a credit computation, one candidate rollout per conflict instead of up to three (a conflict decision ≈ 45 µs worst → ≈ 15 µs
**[i]** from D.1's per-rollout figure), no `refused` timer. The split is compile-time only. Native Windows: unchanged contracts
on the same sites, verified by `verify_ship_steering_sites.py` (20 checks **[m]** Run173 install); the game runs natively at
`C:\X3` and every flight since Run151 was native **[m]**.

## Verification that would prove the recommendations

* Rank 2: `keyoff_diff.py` retired; a new `laneon_diff.py` shows the lane-on stdout of scenario C identical before and after
  the deletion (host timing fields excluded); the full host run drops scenario T and the amendment-16 units; `check_no_x87.py`
  and the site verifier unchanged.
* Rank 3: the same identical-output diff across the split; each new header compiles alone with a one-line TU (the fixture's
  `-fsyntax-only` build per header).
* Rank 4: scenario C with the single law: assertion 2's merge bound (≤ max(40 s, the bang-bang minimum + 5 s)), assertion 7
  (0 episodes, 0 uncertified frames), the spans within 8 s of today's table **[i]**; cast `p2_miss` asserting no write on an
  unkept P2 and order 5 from rest within T_slot + 25 s.
* Rank 5: each new cast fails on today's core for the reason it models (the memory's rule: "add the path as a cast before
  changing the controller"), then the controller change, then green.
* Rank 6: `escort_wait.py` order 4 at ≤ 1.5 s after the leader's launch in flight; the fixture's stage-2 variant already exists.
* In flight (Run174 and after): `convoy_spans.py` ≤ the lane floor + 30 s per closed convoy; `x12_pairs.py` 0 member
  episodes; `ship_gate_ctl_lane` `w=` kinds; `ctl_frame_us_max` with the member count; `ship_gate_launch` rows on the arrival
  side when the player is there.

## Alternatives considered

| Alternative | Why it loses |
| --- | --- |
| Keep stage B as the key-off path "for safety" | It was flown eight times and rejected; the rollback that matters is the previous DLL. Keeping it costs every lane change a byte-identity constraint and doubles the fixture's host time. |
| Rewrite the controller from scratch around the lane | The certificate, board, liveness and arrival code are measured and flown; the lane sits on them. A rewrite would re-discover B.3 (SETA bounds), B.5 (pointer liveness) and B.9 (rollout drift). Delete-then-split keeps the evidence. |
| Replace the capsule sweep by a 1-D lane certificate (consecutive pairs by gap, others by lateral band) | Cheaper, but it would check the law against itself; the sweep is the one independent check and its flown cost is 53 µs. Simplify its candidates instead. |
| Fix row 3 (small ships) by making them lane members with their own spacing | They are not capital-coded, have their own engine arc and the r/3 rule; a second class of member would double the laws. Keep them on the engine's arc with the mirror and cast the interaction first. |
| Patch the KC (through slot, A6, the 10 km leg) now to shorten spans | Stage 2 by decision is the `wait` constant only; the rest changes every NPC convoy in the universe for a few seconds per convoy. |
| Read the KC's slot state to replace the refusal timer | KC VM memory, no documented layout; the brake bound from P2 needs no grant signal. |

## Unknown, and what settles it

| Unknown | Settles it |
| --- | --- |
| Whether Run174 shows a lane-caused turn-away (the kill criterion) | Run174's `gate_row=`/`obst_row=` rows per eviction |
| The i686 cost at ≥ 10 members under SETA 10 (host: 551–807 µs worst) | a crowd flight's `ctl_frame_us_max` and `ctl_budget_cut` |
| What an arrived capital does at P before the next arrival (§8 item 11) | `ship_gate_launch` + member rows with the player in the destination sector |
| Whether the single gap law keeps assertion 7 at g_req 500 m | scenario C with the law swapped (rank 4) |
| Whether `CollisionWarn` acts on 0x14/0x15 | a listing of the KC escort template's warn handling, or an escort 0x12 row on 0x14 in flight |
| The jump-in placement path (same launch site as gate arrivals?) | a listing of `JumpToSector`'s arrival against `0x00444170` |
| The 52 km fallback-body case near a gate | the first `ship_avoid_station` row with `root_r_m > 20000` within 20 km of a gate |
