# Handoff 2026-09-24 (night, written before a session compaction)

Previous handoff: [archive/handoff-2026-09-24-early.md](handoff-2026-09-24-early.md). Rules: AGENTS.md, CLAUDE.md;
memory rules that matter tonight: no host load during flights, fog merges run the route bridge in the gate,
**no merges/commits on main while a candidate qualification runs from the checkout** (candidate-tree-frozen).

## Installed (bottle X3)

- **Run76 DLL `57a7830d…` from `02b34ace`** ([install](../../verification/results/run76-candidate-install.json), rollback Run75
  `56d6863e…` at /tmp/x3-run75-candidate/build/d3d9.dll). Carries: projectile cull exemption (bolts fixed, Run 76 B
  accepted), docked-load card z/cull admission + `volumetric_fog_card_states` diagnostic, `--fog-far-bins 40|24`
  (40 default; Run 76 D: 24 saves only 0.2–0.5 ms, stays off), fog sub-boundaries + repair census, fog hand-over
  fixes with sector-id transit identity, bolt footprint 3,12, TAA output-identical cuts (first round).
- **Game data:** `addon/05` overlay of the 22 flown-sector bodies with the bleed guard + share gate
  ([record](../../verification/results/lod-overlay-batch/install-run76b/install.json)). **A fleet-wide bake
  (`lod_overlay.py --batch --sync --install --replace --jobs 2`, all eligible vanilla bodies, ~1,000) was running
  detached (pid 82025, started ~00:55, log scratchpad/bake_fleet.log) when this was written**: it installs into
  `addon/05` at the end (aside + rename, rollback on exception). First thing next session: check
  `ps -axo command | grep -c '[l]od_overlay.py --batch --sync'` (0 = finished) and the log's summary lines
  (`bodies enumerated`, `eligible`, `refused by reason`, `draws below`, `light_bleed`, `wrote`); the tracebacks at the
  top of the log are orphaned workers of an earlier killed run, not this one. Then copy
  `addon/x3m-lod-batch-summary.txt` / `-bodies.txt` into `verification/results/lod-overlay-batch/install-fleet/`, write
  install.json (pattern: install-run76b/install.json, hashes via shasum), queue **Run 77 A** (fleet overlay: fly two
  or three sectors never flown before, look for oddities, F8 bursts) and commit. If the bake failed, the previous 22-body
  overlay is still in place (the tool moves the old files aside only at the end).
- **The user must not launch the game while the bake runs.**

## On main beyond the installed DLL (Run77 candidate inputs, all committed, HEAD 8ec6b781)

- c694e6df TAA stabiliser mask chain cut (byte-identical; ~0.2 ms inferred).
- 058fab39 fog card gate admits ALPHATESTENABLE 0|1 (Run 76 C measured atest=1 as the only differing state on the
  docked load; fixture + bridge green). **Needs a docked-load flight (Run 77 B)**.
- 8db9a386 D3D11 post-chain feasibility probe (negative on CrossOver: cross-API sharing carries no pixels; note
  docs/architecture/d3d11-post-chain-feasibility.md). 8ec6b781 TAA high-resolution design note, ratified order:
  mask three-draw split + first 5120×1440 flight → S1 depth-copy fold + S2 G32R32F lane (byte-identical) → S3 5-tap
  bilinear CR history, S4 half-res box, S5 half-res dilations (new references + flights).

## Agents running at compaction (their reports arrive as task notifications; worktrees under .claude/worktrees/)

1. **Fog step C** (`a779b12b03688cbf8`, implement-deep): `--fog-march-scale 2|4` quarter-res march variant, own
   fixture reference /tmp/x3-run77-fog-ref-scale4, needs_px repair measurement; acceptance incl. fog fixture, GPU sync
   fixture, route bridge. On report: review (Opus) → merge (3-way apply) → commit.
2. **Translation design** (`a4c40dacb68f7e2d4`, design/Fable): docs/architecture/d3d9-to-d3d11-translation.md, extended
   three times: section 6 = D3D11 vs D3D12 as targets (Vulkan out of scope: it does not work well under CrossOver, user steer); section 7 = engine-level trampolines vs API-boundary translation,
   GPU cost vs CPU per-draw cost, hybrid. On report: read the Outcome line, ratify/reject in a Decision section,
   commit the note; if "measure first", spawn the measurement it names (engine draw submission cost wined3d-D3D9 vs
   DXMT-D3D11 via the probe fixture).
3. **TAA S1+S2 + mask split** (`a76c0d3d17594a2db`, implement-deep): three-draw `taa_mask` split for the diagnostic,
   depth-copy fold (S1), lane RT2 → G32R32F (S2); must stay byte-identical on the temporal fixture. On report: review →
   merge → commit.

After those three land: post-merge gate (host suite, scratch build, x87, fog fixture, **route bridge**) with the tree
frozen, then the **Run77 candidate** from a detached worktree or with no merges during qualification (procedure
/tmp/x3-run76-candidate/run76-commands.md), install, queue Run 77: A fleet overlay sectors; B docked-load fog; C the
first **5120×1440** session with `--gpu-sync-timing` (TAA three-draw split, fog march scale 2 vs 4 A/B with
`--fog-march-scale`, far bins stay 40); D bolts sanity in a busy fight.

## Open questions / later

- Fog quarter-res look on depth edges (step C flight); TAA S3–S5 need new references; the busy-sector mask excess
  (0.9 ms) attribution from the three-draw split; 5120×1440 mask window sizes may need retuning on look grounds.
- Missiles stay under the small-part cull (no marker; large enough). Draw 9 of the bullet pair (separate alpha) still
  native. Wine fixtures sometimes hang at exit after a full PASS (fog pass, GPU sync): kill + rerun once.
- Texel-fallback bodies (teladi_M1) and mod trees (/tmp/x3-mod1 Mayhem, /tmp/x3-mod2) unflown; fog_families.py
  --install when a mod is installed. Linux decoder build unassessed beyond notes.
- user-runs.md: `archive_user_runs.py --check` reports 5 archivable table rows (55, 56, 57, 67, 68); the tool would
  also move the open block by its loose "Run 76" match, so archive rows by hand.
