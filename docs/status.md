# Project status

Updated 2026-09-10. **Iteration 3 consolidated diagnostics are verified and installed. The overall renderer
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
  62 analysis tests and compile-time ABI guards pass.
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
- User turning capture: eight complete frames, 762 successful draws, 45/45 shader
  matches. Camera convention holds through all six adjacent turns (max residual
  2.67e-7). Unique resource/range candidates include changed world transforms;
  repeated keys and reordered draws prohibit naive object matching.
- All 550 named point-light shader-stage observations have explicit count zero;
  stale float array entries are not active light evidence. See turning-camera.md
  and turning-lights.md in reverse-engineering.
- Baseline lifetime probe proves persistent native resources can prevent device
  teardown. Canonical logical COM ownership is required before depth/history.
- User reports double cursor after alt-tab and requests loading-time investigation.
  Both are tracked; flip presentation has not been shown to fix cursor behavior.

- Instruction inspection of all 11 observed vertex shaders confirms direct matrix
  position paths without positional shader animation. Five material pixel shaders
  clamp vertex lighting/emissive RGB before the target, so FP16 alone is insufficient.
  See `docs/reverse-engineering/position-shaders.md`.

## Concrete next work

1. Use the completed 0.3 session to guide scene-depth preservation and batched
   follow-up work. All 12 captured frames are complete (3,131 successful draws).
   The same depth allocation is cleared between background, main scene and
   overlays; scene depth must be used/preserved before its post-bloom clear.
   See [station-session passes](reverse-engineering/station-session-passes.md).
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

The installed DLL is diagnostics 0.3 and matches `build/d3d9.dll`, owned checksum
`71f59c8e6422d5bbf2f55c116e2c0388026d956ba45a3a03eee53c4d85a232a6`. See
`docs/verification/iteration-03.md` for the command, coverage and test evidence.
The user supplied two four-frame turning bursts from 0.2; all captured draws
succeeded and camera/light/motion analysis is complete. The combined 0.3 session
is also complete; the user reported docking during it, without a timestamp that
locates docking within the captured bursts. All 2,914 named point-light count
observations are zero. Camera reconstruction error remains below 1.41e-7.

Texture helpers took 16.382 seconds and inflate 7.109 seconds in observed flushed
totals. An 89.092-second presentation gap remains incompletely attributed;
sampling/disassembly identifies an uncovered mesh adjacency/cleaning/optimization
path. See [loading observations](reverse-engineering/loading-observations.md).
No loading speedup is implemented.

The user confirmed a game cursor and macOS arrow at different positions, persisting
after focus changes. Win32 focus/clipping/hiding restore in the trace, but native
cursor state was not sampled. See [cursor observations](reverse-engineering/cursor-observations.md)
for a scoped synthetic investigation; no cursor fix is deployed.

An independent canonical D3D9 ownership layer passes 370 baseline / 431 wrapped
fixture checks with matching shared HRESULTs and output mutations. It releases
renderer-owned resources before Reset and native device teardown. It is not yet
enabled in the installed DLL; see [ownership source](../src/ownership/README.md)
and [verification](../verification/probe/ownership.md). Generated fragments use
`*_inc.h` per the user's editor preference.

The original shader interpolation fixture passes 96/96 numeric samples across
32 cases and Reset. On this backend SM3 COLOR0 preserves values above one into
FP16; SM2 COLOR0 clips before interpolation. Explicit pixel-shader saturation
still clips both paths. See [HDR varying verification](verification/vertex-color-hdr.md).
This supports targeted SM3 material changes once the FP16 scene path exists;
it is not a game HDR implementation.

## Experimental 0.4 checkpoint (not installed)

The ownership layer is connected to the loader behind `X3M_OWNERSHIP=1` in the
separate `build-ownership/` build. All 12 actual-DLL integration cases and a forced
adoption-failure fallback pass. Child-induced final device/factory releases now
reach the capture hooks; repeated device address reuse leaves no stale contexts.
Stencil states and depth selection status are included in consolidated capture
diagnostics. See [integration verification](../verification/probe/ownership_integration.md).

Opt-in automatic D24X8-to-INTZ substitution passes 758 API/lifetime checks and 48
numeric samples, including preservation before a destructive depth clear and
three allocation-failure rollback cases. **It is not ready for gameplay:** depth
copies between INTZ and ordinary D24X8 fail where the original copies succeed,
and stencil handling during stateblock recording is unresolved. See
[automatic-depth experiment](verification/auto-depth.md). Investigation of a
compatible RESZ copy path continues before choosing the production depth route.

The bounded original mesh-adjacency reuse fixture passes 1,218 checks and complete
downstream mesh parity. It demonstrates a synthetic speed benefit for repeated
identical meshes, not a game loading improvement; actual repetition/cost remains
unmeasured. See [mesh preparation](verification/mesh-preparation.md).

A standalone temporal resolve shader and GPU verification are in progress;
camera/object-motion routing and gameplay TAA remain incomplete.

No game was launched by the agent. No visual enhancement has been enabled.
Commit each completed logical checkpoint.
