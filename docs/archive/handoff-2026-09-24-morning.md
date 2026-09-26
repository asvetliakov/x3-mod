# Handoff 2026-09-24 (morning, pre-compaction)

Previous handoff: [archive/handoff-2026-09-24-night.md](handoff-2026-09-24-night.md). Rules: AGENTS.md, CLAUDE.md;
memory rules that matter: no host load during flights, fog merges run the route bridge in the gate, the tree is frozen
only while a qualification agent runs from the checkout (candidates build from a detached worktree), mod trees read-only.

## 07:00 update (supersedes the sections below where they differ)

- **Installed: Run78 DLL `d4ba9f05…`** (56,270,679 B) from `ee3bbf88` at 06:58 ([qualification](../../verification/results/run78-candidate-qualification.json),
  [install](../../verification/results/run78-candidate-install.json)); rollback Run77 `268db207…` (exit crash). Carries the exit fix,
  scale 4 default, output dither (`--hdr-dither on|off`), TAA S3 (`--taa-history-taps 5|16`). Fleet overlay 611 bodies
  ([install-fleet2](../../verification/results/lod-overlay-batch/install-fleet2/install.json)). **Run 78 A queued** (user-runs.md).
- Landed on main since 04:40: fleet2 record b2e3e474; dither 3d9e4145; **shader slot budget** measured 7a4264a4/8651a230
  and the rule in AGENTS.md (512 is a floor, plan against 32768, no fallback program set, 0dee63cb); **lifted-cap TAA plan**
  ratified cad979ef (docs/architecture/taa-plan-lifted-slot-cap.md: A' in far_camera + exact rest read + fetch sharing,
  then S4; ceiling 2,048 slots); S3 ee3bbf88; summaries re-pinned 1c51213a; route bridge harness fix 026d24ac
  (shadow A/B at spacing 2, motes reset 5 + 1 at spacing 4); Run78 record 24e41c0f; install f309559f.
- **Agent in flight:** A' implementation (implement-deep, worktree agent-a0442d1e9f65d92f2): region hold in the resolve
  dropping mask x/y, `--taa-region-hold on|off`, exact rest read restored, per-tap HDR weighing, RESOLVE_BUDGET at 2048,
  cap logged at device creation, new references. On report: review (Opus + Fable second, GPU/look code with new
  references), merge, commit; needs a flight (Run 79).
- Merged worktrees kept for now: agent-aff3f80d9067fb785 (dither), agent-a5b01ee9bcf70b679 (S3), agent-a3e7804f8bae015ed
  (bridge fix); safe to remove with `git worktree remove`.

## Installed (docs/status.md is authoritative)

- **DLL Run77** `268db207…` from `bc47873b` ([qualification](../../verification/results/run77-candidate-qualification.json),
  [install](../../verification/results/run77-candidate-install.json)); rollback Run76 `57a7830d…` at
  `/tmp/x3-run76-candidate/build/d3d9.dll`. Known: **crashes on game exit** (our engine-memory reader copies a freed
  engine block; fix on main c45c5dc0, unflown); gameplay unaffected.
- **Game data:** fleet overlay across `addon/05` + `addon/06` (591 bodies, [record](../../verification/results/lod-overlay-batch/install-fleet-split/install.json)).
  A rebake with the new baker (two refusal classes lifted, 33aba284) was running detached at compaction (pid 63938,
  started 04:10, log scratchpad/bake_fleet3.log, ~45 min): it replaces 05/06 at the end and refuses if the game is up.
  Slot 06 is still unproven in flight (no slot-06 body was in view in run287): Run 77 A2 asks for one burst at a Terran
  dock / trading station / equipment dock.

## Main beyond the installed DLL (HEAD 483411b6 + this handoff)

- c45c5dc0 engine memory reader: no read from a freed engine block at exit (reviewed twice; in-game exit unverified:
  expect no fault and one `engine_memory_read_refused` row).
- 483411b6 **fog march scale 4 is the default** (Run 77 C: fog_march 8.99 -> 2.68 ms at 5120x1440; user accepted in
  C2); 2 the opt-out; fixture 52 gates, look hashes re-pinned; route bridge to run in the gate.
- 33aba284 baker: dominant-slot rule relaxed, NULL diffuse as a black solid tile (inferred), planet_haze/asteroid kept
  groups, split_TL/teladi_M6/lostcolony_energy/teladi_trading_station now bake; d1b7924c DXVK smoke test; 3cded458
  RE distance-fade; d3480fec thin-geometry design (ratified A' + S4 after S3, B as a vote); f604a3f8 GPU backend A/B.

## Agents running at compaction (reports arrive as task notifications)

1. **Output dither** (`aff3f80d9067fb785`, worktree agent-aff3f80d9067fb785): ±0.5-code static IGN dither at the 8-bit
   write (agx.hlsl, agx_sharpen_ps.hlsl, identity write-back), `--hdr-dither on|off` default on, agx_reference test,
   records regenerated natively. Cause: Run 77 C2 rings = 8-bit output contours moving with auto exposure
   ([triage](../../verification/results/run291-293-rings/)). On report: review (Opus), 3-way merge, commit.
2. **TAA S3** (`a5b01ee9bcf70b679`, worktree agent-a5b01ee9bcf70b679): 5-tap bilinear Catmull-Rom history in every
   resolve variant, `--taa-history-taps 16|5` default 5, slots before/after, new references, s3_identity.py. On report:
   review (Opus; Fable second review since it is GPU/look code with new references), merge, commit; needs a flight.
3. Bake pid 63938 (above). Watcher: Monitor bh4c9di71 (may have expired; check `ps` for the pid and the log's
   `wrote`/`running` lines).

## Next steps, in order

1. Land the dither and S3 (reviews, merges). Then the **Run78 gate**: run_host_suite.py, scratch build, x87, temporal
   pass (new S3 references), motion output, fog fixture (52 gates, scale-4 default + s2 variant), **route bridge**
   (fog default changed), GPU sync, cull, site verifiers, dry runs; candidate from a detached worktree at the gate
   commit (pattern: run77-candidate-qualification.json); install; records run78-candidate-*.json.
2. Queue **Run 78 A** at 5120x1440, one session, several launches: dither on vs `--hdr-dither off` at the fogged spot
   (rings gone?); default scale 4 with no `--fog-march-scale`; exit the game normally (no fault, one
   `engine_memory_read_refused` row); one F8 burst at a slot-06 body (Terran dock / trading station / equipment dock);
   `--taa-history-taps 16` vs default 5 look A/B on the lattice stand; optional `--fog-far-bins 24 --gpu-sync-timing`
   (run294 gave -0.4 ms p50 / -2 ms p95 without timing; 40 stays default). Run 77 D (bolts in a busy fight) deferred.
3. Then the TAA plan: A' (region hold through the age target, drops the two dilation draws), S4 (half-res box), B
   (per-draw thickness flag as a vote; measure in the CloneMesh-destination Unlock observer); the **fade routing**
   (treat fade draws above an alpha threshold as pixel owners for RT2: docs/reverse-engineering/distance-fade.md;
   then remove the sentinel stabiliser after one pan flight); the same-sector reload keeping the density cache
   (optional, 0.6 s).
4. Later: TAA S5 moot after A'; texture_unresolved bodies need an engine lookup trace (Khaak names under tex/true/);
   adeffects g_TexMatrix animation untraced; NULL-diffuse placeholder colour untraced; D3D9->D3D11 translation
   closed no-go on measured numbers (d3d9-to-d3d11-translation.md), DXVK blocked by MoltenVK (a newer MoltenVK is
   an app-bundle change the user must decide; never run Wine from a copied CrossOver bundle: Gatekeeper dialog).

## Runs

- Completed today: 77 A (run287, accepted), 77 B (run288, docked fog closed), 77 C (run289/290), 77 C2 (run291-293,
  no bursts taken; scale 4 accepted), run294 (far bins 24 at scale 4). Open: 77 A2, 77 D (deferred).
- Stale worktrees: keep agent-a0d5b30b56e14bd37 and agent-a2b067e4005541a7a; the two live ones above.
