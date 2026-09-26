# Handoff 2026-09-18 (late night)

Short current handoff; [status](../status.md) is the authoritative summary, the
[goals table](../goals.md) the acceptance state, [user-runs.md](../verification/user-runs.md)
the run queue. Previous handoffs: [archive/handoff-2026-09-18-night.md](handoff-2026-09-18-night.md),
[archive/handoff-2026-09-18-evening.md](handoff-2026-09-18-evening.md).

## Where things stand

- **Installed:** run42 candidate `1a5dd46c…` from main `903be726`
  ([build](../../verification/results/run42-candidate-build.json),
  [install](../../verification/results/run42-candidate-install.json)); rollback run41
  `b6ea8569…` in `/tmp/x3-candidate-Iv6Z6G/rollback`. Install only through
  `python3 tools/manage.py install --dll-source <dll>`.
- **Run 42 is read** (run129 A, run130 B, run131 C, run132 D; outcomes in the run
  table). No run is queued; run 43 is drafted in user-runs.md §43 and waits for the
  run43 candidate.
- **User configuration:** original hull shading; light-map gain 4 (F4), effects and
  guide lights 2 (F6); five cascades 2048², linear receiver depth, slope 0.2, K 1.5;
  FPS overlay Ctrl+Alt+F7; View Distance stays Very High. Units 1 m = 5 u.

## In flight: four unmerged worktrees (do not delete)

Each worktree's work is saved as one **WIP commit on its own branch** (2026-09-18: `22f50663` shimmer, `1b64eb90`
cull, `05d5a0d9` collide, `8786b088` trims), not merged to main; review against `git diff main...<branch>`
after merging main in. A `PROGRESS.md` scratch file sits at the root of the shimmer and cull worktrees: delete it
before the checkpoint merge. All session agents are finished; none can be resumed from a new session.

All under `.claude/worktrees/`; each has (or should have) a `PROGRESS.md` at its root
if its agent was interrupted. The session hit Fable rate limits three times; agents
cannot be resumed from a new session, so a new session starts a fresh agent on the
same worktree path with the brief below and the PROGRESS.md.

| Worktree (branch `worktree-<name>`) | Change | State | Next |
|---|---|---|---|
| `agent-a09c0d5a8c13a7257` (base 8ab858b2) | Fade route for modules behind the camera plane (`origin_distance` refused w ≤ 0: run130 leg 2) + **overlay arm** (a node's translucent sub-mesh, zwrite 0, routed after the same node's routed draw: the distant "glowing windows" shimmer) + `unmatched=<reason>` on route rows; fixtures `seam-taa-fade-route-behind`, `-overlay` | Opus review done; all 7 fixes applied (witness = same frame + next draw index + same node + same lifetime serial; `seam-taa-fade-route-foreign` refusal case; arm off under linear materials; latch cleared at Reset; record `verification/results/bottle-X3/fade-route-cases.json`, seven cases pass, seam DLL 8e2dd29e) | delete `PROGRESS.md` → **Fable second review** (draw admission) → merge main in → merge |
| `agent-a3761c08ffa8afc7e` (base 8ab858b2) | `--cull-small-parts <px>`: engine_patch trampoline at `0x0047d2a2` sending nodes under the px threshold to the engine's own size-cull path `0x0047d2c3`; fixture replays 1,214 run131 census rows natively (2 px → 403 draws flip, 4 px → 458, nothing else) | Opus review and **Fable second review done, nothing blocking in the trampoline** (no cross-frame state on the cull path; consumers of the renderable bit are render-side except one script occluder list `0x00488aef`/`0x004886a0`; claims disjoint; rollback sound). Opus fixes applied (verifier 18/18). **Decision needed before merge/flight (Fable finding 1):** the 97 nodes / 403 draws that flip at 2 px are mostly *whole distant objects* (89 of 97 carry body flags, radius up to 61,759 at D 2.6M–20M, one node 33 draws), so 2 px removes objects up to ≈ 4 px across together with their light-map glow and bloom, which the user likes. Options: (a) exempt root nodes (`+0x18 == 0`) so only sub-parts are culled (small saving); (b) instead of culling, force the coarsest LOD for nodes under N px (33 draws → 1, keeps the speck and its glow; site near the LOD adjust `0x0047d48b` / lod_scale mirror `0x0047d44b`) — likely the right lever; (c) fly as is and let the user judge; **(d) recommended after discussing with the user: cull only whole bodies under the threshold and leave sub-parts alone** — the 89 body nodes are 500–4,000 km away and read as a dark speck or a faint dot (the user: "a whole 2 px station is not visible anyway, glowing station parts of a few px are"), they carry nearly all of the 9.5 ms, while the 8 sub-part nodes are the visible glowing details of nearby stations; risk = pop-in on approach at the threshold (run 43 B checks it), fallback (b). Add the model → object-type join to `tools/analysis/cull_census.py` so the next census names the bodies. Also: with `--shadow-caster-retention` a culled static caster keeps casting from the retention store (document); `m00` latch follows zoom with one frame lag and its log line is capped at 16; `camera_state::reset()` unreachable with only this option on; docs say verifier 16/16 (now 18/18); the antenna sentence in lod-selection.md is likely inverted. | **User decision 2026-09-18: fly an A/B instead of choosing.** Add `--cull-small-parts-scope all\|bodies` (default `bodies`; `bodies` = only nodes with no parent, `+0x18 == 0`, the test the displaced instruction already performs; `all` = the current behaviour) with a Fable implement on this worktree: fixture asserts on the run131 rows that `bodies` flips exactly the body-flagged subset and `all` the full 97 nodes / 403 draws, census `culled_small` rows carry the scope; then the doc fixes → short Fable re-review of the stub delta → merge. Coarsest-LOD stays the fallback if pop-in bothers the user |
| `agent-aae4564ee5361c29e` (base 3e43ce58) | Per-routed-draw attribution (`run_route_bench.py`, `routebench` fixture mode: proxy-only 9.7 µs/draw in game; plain route 7.5, ownership wrapper +2.0, depth lease +1.9, telemetry-draw +1.5) and two trims (lease multistream verdict from the declaration hook; c216 upload skip) | Opus review done; fixes applied: the inert c216 skip is removed, the remaining lease trim is 0.1–0.2 µs/draw (noise level), witness `route-trim-pretrim-comparison.json` (10 cases, all_equal) | **Fable second review** → merge; low priority: the value is the bench and the §2.2 attribution the design note links to |
| `agent-a675417908b640a66` (base 60214b44) | `--collide-box-cull`: integer bounding-box early-out at P1 `0x0045d58e` / P2 `0x0045cc7c` in the sector collide all-pairs loop ([RE](../reverse-engineering/sector-collide.md)); per-frame pair/reject counters; run129 measured the routine at 26 ms/frame in the user's 24 fps area | **implemented, unreviewed**: stubs at P1/P2 jump to the engine's continue labels `0x0045df90`/`0x0045ce07` on a box reject; class-7 pairs always take the engine path; the stub rejects only when every \|d\| < 2³⁰ because the engine's `0x0052b5d0` ends in CVTTSD2SI and does not reject a distance ≥ 2³¹ (gap in the RE note's §6, corrected in its new §10); site verifier 29/29 incl. disjointness from 119 claimed sites; CPU fixture 38 checks, 445,882 P1 + 146,064 P2 pairs identical patched vs unpatched on layout-preserving copies of the engine's pair tests (the EXE loop cannot run in-process), all box rejects inside the engine's reject set; host model 10.7 M pairs, 0 counter-examples; bench −8.6 µs per 1,000 rejected pairs, +4.2 µs kept; counters as a `collide_census` line per 300 frames and `collide_census_frame` on F8 frames (none in the option-off flight: use `p1_pairs_p50` from the on flight); clean build, no-x87 539/0, lifetime 674/0, ownership pass | Opus review → **Fable second review** (trampoline; check the x87/flags liveness claims and the 2³⁰ guard) → merge by hand: the worktree predates main's commit of `sector-collide.md` (4e8a6c8a) and carries its own copy with §10, and edits `engine-frame-time.md` §2.1; its claim scan does not know the `cull_small_parts` site `0x47d2a2` (no overlap) |

Design note [route-per-draw-cost.md](../architecture/route-per-draw-cost.md) is committed **for ratification** (not yet read by
the orchestrator): lever 1 ownership-wrapper bypass for the seven value-only calls plus dropping the FNSAVE shell on
the lease's lock views (≈ 1.6–1.9 ms of the 8.05 ms proxy cost at 830 draws), lever 2 a hook-free lazy RT mode
(≈ 1.4 ms), lever 3 conditional (≈ 0.4 ms). It links to the §2.2 update that exists only in the trims worktree.

### Agent outcomes already processed; residual open issues and review focus

The final reports of this session's agents are folded into the table above. Do not trust them blindly: after
merging main into a worktree, re-run its acceptance checks (named in its ledger section) before the review.
Residual issues the agents reported, and the focus for each pending review:

- **Shimmer fixes** (Fable second review): the overlay arm has no linear-materials fixture (hence disabled there);
  strict adjacency refuses an overlay that follows a non-routed draw of its node (will show as
  `unmatched=overlay_node` in run 43); the Reset clearing of the last-routed latch is not exercised by a fixture;
  `test_fade_region.py` was not re-run after the last review edits (16 OK before). Review focus: the overlay's
  motion rows use the overlay's own WVP; RT2 stays masked so the depth history and shadow lane never see it; the
  witness cannot go stale across a node free/realloc; cost of the extra state reads on refused draws.
- **Cull small parts** (implement the scope option, then Fable re-review of the delta): Fable findings still open
  in the docs: retained static casters keep casting when culled (`--shadow-caster-retention`), the script occluder
  list `0x00488aef`/`0x004886a0` loses culled nodes, the `m00` log line cap of 16, `camera_state::reset()`
  unreachable with only this option on, verifier count 18/18 in lod-selection.md, the antenna sentence. The
  scope test must not change the displaced instruction's flags on the continue path.
- **Collide box cull** (Opus review, then Fable): verify independently the liveness claims (EAX/ECX/EDX/EDI dead at
  P1 and `0x45df90`; EAX/ECX/EDX/ESI dead at `0x45ce07`; EFLAGS dead; x87 stack untouched because both spans precede
  the first FILD), the 2³⁰ guard against the engine's CVTTSD2SI overflow behaviour, that class-7 pairs always take
  the engine path, and that the layout-preserving synthetic copies of the pair tests in the fixture are pinned by
  bytes. The counters exist only with the option on. The worktree's claim scan predates the `cull_small_parts`
  site `0x47d2a2` (no overlap; re-run the verifier after merging main).
- **Per-draw trims** (Fable second review, low priority): only the lease trim remains (0.1–0.2 µs/draw, noise
  level); the comparison against the committed 2026-09-15 record ran on an intermediate build; the design note
  links to this worktree's §2.2 update, so merge it before or with the note's ratification.
- **Design note** `route-per-draw-cost.md` (ratify): assumes the seven value-only calls never return a
  device-loss code (unverified; Windows unverified); lever 2b (lease borrowing retention references) is closed
  until the retention release order is traced; two unknowns need a flight: run lengths of consecutive routed draws
  and whether the game ever writes `COLORWRITEENABLE1/2`; ≈ 2,100 wrapper Releases per frame at scene end are
  unmeasured.
- **Run 42 telemetry gaps** noted by triage: loading a second save left no `loading_phase` marker; `frame_phases`
  and `motion_output_frame` have no self-cost field.

Merge order: shimmer fixes, cull stub, collide patch, trims (they touch
`motion_output.cpp`/`capture.cpp`/`manage.py`/`CMakeLists.txt`; merge main into each
before its final review run). After every merge that adds struct members run the
snippet-mock modules (`test_motion_wrap_states`, `test_linear_material_live`,
`test_motion_hdr_scene`): they drifted three times this session.

## Then

1. Run43 candidate: Opus build agent, clean main, same record shape as
   `run42-candidate-build.json` (full host suite, all motion-output cases incl.
   fade-route, sun-share live, cull census + cull small parts + collide fixtures,
   lifetime, ownership, state-hook benchmark, §43 dry-runs); main session installs
   via `manage.py`, install record, status/goals/handoff.
2. Run 43 (drafted §43): A collide A/B in the 24 fps area (`--loop-phases`, 60 s off,
   60 s `--collide-box-cull`, then a collision sanity fly); B `--cull-small-parts 2`
   and `4` at the run117 ≈ 900-draw view with a popping check on approach and one F8;
   C shimmer check at the solar plant and the distant object.
3. From run 43: pair count → decide the broadphase rewrite (sort-and-sweep calling the
   engine's pair body) or SSE2 inner loop; cull px default; then the per-draw design note.

## Findings of run 42 (full numbers in the ledgers)

- 24 fps area = sector collide `0x0045d250`, 26 ms/frame flat, 96 % of pre_render:
  an unguarded all-pairs loop with a full x87 sqrt per pair
  ([sampling-profiler.md](../verification/sampling-profiler.md) "Run 42 A").
- Busy station: 403 of 901 draws under 2 px (9.55 ms), survivors have zero per-node
  thresholds ([cull-census.md](../verification/cull-census.md) "Run 42 C").
- View Distance High buys ≈ 1.5 fps; LOD lever closed.
- Shimmer: fade route fixed leg 1; leg 2 = module behind the camera plane; distant
  glow = the node's translucent sub-mesh (zwrite 0) refused by the opaque chain;
  light-map gain off does not stop it; an unrouted-draw census found no other class.
- Audit of linear-material gates: no second functional defect.

## Decisions (do not reopen without a reason)

- As in the night handoff, plus: light-map gain 4 default; guide lights on the
  effects key and gain; LOD bias closed; lazy RT mode not default (re-installs the
  light setter hooks, net loss); collide fix is the box early-out first, a broadphase
  rewrite only if the pair counters demand it; small-parts cull default-off until run 43.

## Housekeeping

- `/tmp/x3-bottleX3-run117…132` preserved sessions; `/tmp/x3-candidate-*` rollbacks
  (`-JPyWEO` and `-gHUSU7` superseded).
- 62 older worktrees (unmerged/dirty, 4.5 GB) predate this session.
- Scratch tables of this session (triage outputs) are in
  `/private/tmp/claude-501/-Users-asvetl-x3-mod/e335fc62-471f-4c4a-89c9-cfcd2ddb149f/scratchpad/`
  and may not survive; every number that matters is in a ledger.
