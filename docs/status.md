# Project status

Updated 2026-09-10. **Iteration 2 capture inputs and numeric depth/camera probes are verified. The overall renderer
modernization objective is not complete.** No HDR/TAA/AgX/material/clustered-lighting
visual enhancement is enabled yet. See the [full user objective](user-objective.md)
and [roadmap](architecture/roadmap.md).

## Completed

- Reversible app-local 32-bit D3D9 proxy, loader/export forwarding, identity-preserving
  per-object interception, bounded capture, F8 trigger, shader dumps and hashes.
- Preview-only installer/launcher/rollback tooling; EXE, archives and bottle
  configuration preserved. User test setting: 1280×768 windowed (was 5120×1440
  borderless). No unused launcher is intentionally left open.
- Independent FP16/depth/D3D11-scRGB capabilities; baseline/proxy smoke passes;
  31 analysis tests and compile-time ABI guards pass.
- Static archive/PE analysis, targeted Ghidra renderer map, shader index and CTAB
  register mapping, documented separately in `docs/reverse-engineering/`.
- Animated menu capture: 690 draws; user-assisted flight: two complete 122-draw
  frames. Exact archive matches for all 47 shaders recorded in flight session.

## Iteration 2 additions

- Capture v2: typed I/B/F state, per-device frame keys, resource allocation IDs,
  stream/index metadata, draw parameters/results, and texture/surface relationship.
  Actual synthetic traces verify stateblock restoration and resource recreation.
- Camera factorization: 105 draws/frame fit the same multiplication convention;
  three camera coordinate regimes prohibit a blanket single-camera assumption.
- INTZ numeric rendering/sampling: 16 pixel checks pass across 8-bit/FP16 outputs
  and reset. This is sampleable synthetic depth, not game depth substitution.
- Common shader point-light array decoded as eight pos/color/atten structures.

## Concrete next work

1. Finish analyzing the received eight v2 turning frames for typed light count and
   draw-slice continuity. Allocation identity is not engine object identity.
2. Map camera conventions and object identity across controlled motion; correlate
   world/WVP/view-inverse values to depth and projection. The capture has useful
   names/registers, not yet validated motion vectors.
3. Identify complete scene/transparent/HUD boundaries. Bloom is at flight draws
   95–98, but substantial effects rendering follows. Never use a blanket final
   bloom boundary or gui2d PS-only heuristic.
4. Validate sampleable scene depth and a reversible jitter experiment, then temporal
   reprojection/rejection. TAA is a core target.
5. Separately preserve pre-clamp FP16 lighting and choose a GPU-native HDR output
   route; the current 8-bit scene cannot produce true HDR by output conversion alone.
6. Continue the remaining roadmap features once these inputs are verified.

## Test coordination

The user requested notification for future launches that need more than the menu,
and will handle launching/loading gameplay scenes. Do not repeat autonomous full
game launch attempts. Close any unneeded launcher immediately. Current game can
be left to the user; it contains their chosen test scene. F8 writes captures under
`X3/x3-modern-captures/`. Detailed capture causes a diagnostic hitch by design.

## Useful artifacts

- `verification/results/game-flight-capture-summary.json`
- `verification/results/game-menu-capture-summary.json`
- `verification/results/shader-registers.json`
- `verification/results/graphics-capabilities.txt`
- `verification/results/d3d9-smoke-proxy.txt`
- `docs/verification/iteration-01.md`

Raw shader bytes remain local beside X3. The large generated archive shader index
was `/tmp/x3-shader-index.json`; regenerate with `tools/analysis/index_shaders.py`
if missing. Raw game logs are also beside X3, not redistributed source assets.

The installed DLL is capture 0.2 and matches `build/d3d9.dll`. See
`docs/verification/iteration-02.md` for checksum and checks. The user supplied two four-frame turning bursts; all captured draws succeeded.
A still-running game retains its startup DLL until restart. No visual enhancement
has been enabled. Commit each completed logical checkpoint.
