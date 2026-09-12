# Temporal steps 1-3 review: depth target, jitter and the resolve at the bloom copy

Independent review of commit `18b7be9` (temporal steps 1 and 2: the R32F
current-depth target written by all 169 rows as `oC2`, the per-draw Halton
jitter with bit-exact restoration, the cut-detector data, the analyzer's depth
checks and the `TemporalPass` route inputs) and the uncommitted step 3 on top
of it (the resolve wired into the route behind `X3M_TAA`: `before_stretch`,
`taa_call`, the scene/query tracking and the query-object wrapper, the native
vtable option and cached `D3DSBT_ALL` block of the pass, the alpha passthrough,
the embedded resolve bytecode, launcher switches, fixture, runner, analyzer and
docs). The build reviewed here goes into the game with `X3M_TAA=1` next, so the
review weighed rendering, crash, hang and leak hazards first. Every suite below
ran on the final tree after fresh `build/` and `build-ownership/` rebuilds;
result files were queried with scripts, never read whole.

## Resolve placement and state (step 3)

`before_stretch` runs only when the selector is in `AwaitCopy` and a copy of
it advances to `AwaitBloomTarget` on the pending event (source = latched main
target, destination = same-sized color texture, full rects), so no other
`StretchRect` triggers it; the selector is trivially copyable (no heap
members), the probe reuses the sequence number `after_stretch` will consume,
and the real event is still fed afterwards. The main target is written only by
the final copy-back after `run` returned success and produced
`Output::color_surface`; every earlier failure (container, validation, copy,
draw, restoration) leaves it untouched, invalidates the history and logs
`motion_output_taa_failed` at most 16 times. `TemporalPass::run` captures the
cached `D3DSBT_ALL` block *before* `normalize` and applies it after the draw,
so shaders, constants, streams, indices, declaration/FVF, samplers, texture
stage states, render states and textures return to the application's values;
render targets, depth, viewport and scissor (not state-block state) are read
and restored explicitly in the correct order (RT0 before the others, viewport
and scissor after `SetRenderTarget`). The block's references are therefore
transient within the run; it does retain references to the application objects
bound at the copy until the next frame's `Capture` (one frame), and it is
released before every native `Reset` and at the final device Release, so it
never holds a default-pool object across Reset and never restores a released
object (it is captured again before each Apply). A lost device skips the
restoration (`lost()`), as before. No sRGB state leaks: `normalize` clears
`D3DRS_SRGBWRITEENABLE` and `D3DSAMP_SRGBTEXTURE` on s0-s6 and the block puts
the application's values back. The route's own copy-back and the pass's copies
go through the native slots, so neither the selector nor the shadow nor the
`SceneCapture` adapter sees them, and the application's copy observes the
resolved image.

## Re-entrancy, references, Reset and loss

The pass receives the original vtable and calls only numbered slots that
`abi_check.cpp` asserts (now including `SetTextureStageState` 67). Device
references are counted by probing `AddRef`/`Release` through the native slots
around every pass call; while such a call runs, `device_references()` reports
zero, so a child release re-entering the device `Release` hook cannot match the
final-release probe with a stale count. `release_resources` shuts the pass down
inside that guard and destroys it, so the destructor's second call never
probes a dead device. Reset ordering is release RT2/RT1, then the pass's
histories, scratch and block (`before_reset`), then the wrapper's native
Reset, then `after_reset(hr)`; a failed Reset keeps `reset_pending` and every
`run` is refused with `E_INVALIDARG` (logged, no spin). The query wrapper
keeps object identity (per-object private table on the same pointer), hooks
only slots 2 and 6 of objects the device's own `CreateQuery` returned through
the hooked vtable (no foreign object types; queries cannot predate the hook
because it is installed at device creation with the route), erases its entry
on the zero return of Release and drops an open BEGIN count then, and counts
only BEGIN-without-END issues, so EVENT queries never block the resolve.
`BeginScene`/`EndScene` are tracked after the application call and cleared on
Reset. The fixture's zero final device and factory references through the
wrapper with the pass allocated stand.

## Jitter and depth (step 1)

`apply_jitter` runs after gate 2 (`scene_bound()`: selector in Scene, RT0 and
depth are the latched pair, full viewport, no extra RT bound) for every draw
whose bound VS has a row and whose window is known, before gate 3 decides
routing; draws on other targets, with other viewports, while recording, and
outside Scene are never jittered. It writes `rows[0..3] += jx·rows[12..15]`
and `rows[4..7] += jy·rows[12..15]` from a copy of the shadow through the
native setter, and `after_draw` writes `shadow_.rows[window]` back, so the
device holds the application's bits again and the shadow never saw the jitter;
`history_.lookup_and_record` stores the shadow's rows, `c216.zw` is the zero
literal in `pixel[]`, and the sequence advances once per latched frame with
`jitter_previous_` taken before the advance, which is the pair `resolve()`
hands to the pass. Only rows 0 and 1 change, and the vertex variant's depth
export is `dp4` of the same position temporary against `c<matrix+2>` and
`c<matrix+3>` (mask `.xz` / `.yw`), so `oC2 = z/w` is the depth of the same
jittered raster position the device wrote; the RT2 write mask is set with the
target and restored in reverse order, and the sentinel fill writes both targets
in one draw. The per-draw cost of step 1 is two native constant writes for a
jittered draw plus the RT2 bind and write mask for a depth-routed one, no
allocation (`displacements_` is reserved once and never grows); step 3 adds
nothing per draw.

## Findings and fixes

1. **Medium, fixed** - `TemporalPass::normalize` left stage 0's fixed-function
   `D3DTSS_TEXCOORDINDEX` and `D3DTSS_TEXTURETRANSFORMFLAGS` alone. The
   resolve quad is pre-transformed with no vertex shader, and the Preview
   backend applies those states to it: with a hostile texture transform
   (scale 0.5, offset 0.25, `COUNT2`) and coordinate index 1 the pass
   fixture's `matrix routing` sample read 0.25 instead of 0.625. A game frame
   that leaves such state on stage 0 at the bloom copy would have resolved
   from remapped coordinates. `normalize` now resets both
   (`src/renderer/temporal_pass.cpp:142-143`; the block restores them),
   `abi_check.cpp:78` asserts slot 67, the fixture sets the hostile state
   before every run (`temporal_pass_fixture.cpp:157-158`; its own RHW
   rasters set the defaults like they set VS/FVF), and the suite stays at
   154/158 with the state comparison proving the hostile values come back.
2. **Low, fixed** - `X3M_TAA=1` without object history (`--object-trace
   --object-lifetime`) resolves every pixel current-only while the jitter
   still moves the raster, so the screen would only shimmer. `tools/manage.py:71`
   refuses `--taa` without both history options; the documented gameplay
   command already carries them (verified with `--dry-run`).
3. **Low, fixed** - the attach gate did not ask the driver whether
   `StretchRect` may convert between the 8-bit main format and FP16, which
   native D3D9 requires (`CheckDeviceFormatConversion`). `attach` now checks
   A8R8G8B8 and X8R8G8B8 to and from A16B16G16R16F
   (`src/proxy/motion_output.cpp:475-484`, `taa_reason=format_conversion`);
   Wine accepts unconditionally and the TAA runs still report `taa_reason=ok`.
   Recorded in `platform-portability.md` with the block's one-frame retention
   of application references.
4. **Low, fixed** - the `X3M_TAA_DEBUG` readback of the pre-resolve color
   created its system-memory copy as A8R8G8B8 regardless of the main target's
   format; `GetRenderTargetData` needs the exact format, so an X8R8G8B8 main
   target would have failed the readback and the analyzer's `taa_image`
   check. It now uses the latched format (`motion_output.cpp:309`).
5. **Documentation, fixed** - bench numbers in `temporal-integration.md` and
   `motion-output.md` updated to this run; the step-3 sections of both design
   documents and `temporal-resolve.md` record the stage-0 normalization, the
   conversion gate and the launcher requirement; `README.md` says `--taa`
   needs the history options.

Verified correct and left unchanged: the resolve placement, state capture and
restoration order, the failure semantics of `resolve()`, the reference probing
and re-entrancy guards, the query and scene tracking, the Reset protocol, the
jitter sign/scale and restoration, the depth export's row math, the
alpha passthrough (`current.a`, NaN to 1; FP16 round trip of 8-bit values is
exact within the truncating conversion, proven by the bit-identical no-history
frames), the identity clip matrix (RT1 alpha is 1 or -1, so the camera path is
never taken), the previous-jitter convention (added once by the resolve to the
producer's unjittered UV, `c216.zw = 0`), the cut verdict computed at the
depth unbind before the copy, and the analyzer's depth and `taa_image` checks.

Observations, not changed (design level):

- Scene draws that are jittered but not routed or not matched (unreviewed
  pairs, depth-disabled effects in Scene, gate 5/6 failures) resolve
  current-only through the sentinel while their raster still moves by the
  jitter, so their edges will show a sub-pixel crawl. This is inherent in the
  first visible TAA and the design accepts it; the gameplay comparison should
  look for it on particles and unknown programs.
- An application occlusion query left open across the bloom copy would skip
  the resolve every frame (`taa_skip=6`); the log makes that visible.
- On native D3D the cached block's one-frame references to application objects
  keep the device count above the final-release probe until the block is
  dropped; on Wine the application-level references are independent of it.
  A leak only at process exit, and only on the unverified native target.
- The pass's `run` refuses with `E_INVALIDARG` while a caller state block is
  recording; the route already skips before calling it (`TaaSkip::Recording`).

## Results after the fixes

`build/` (RelWithDebInfo, `--clean-first`, by `run_motion_output.py`) and
`build-ownership/` (Release, `--clean-first`, by `run_ownership_integration.py`)
were rebuilt on the final tree; every suite below ran in sequence on it.

| Suite | Result |
| --- | --- |
| `run_motion_output.py` | PASS: 26 runs (22 cases + 4 bench); production TAA 69 checks / 51 restorations, seam TAA 150 / 51, plain and through the wrapper; seam main target equals the reference resolve byte for byte in all 12 frames, 374 pixels changed by history (frames 1, 4, 7, 10, 11), `taa_image` differing fraction 0 in production and 0.0044 / 0.0229 / 0.0046 in seam frames 1 / 4 / 7; `taa_reason=ok` with the new conversion gate; bench 0.330 / 1.004 ms off / on at 1280×768 and 0.711 / 2.754 ms at 5120×1440 (0.67 and 2.04 ms for the resolve and copies, CPU-inclusive) |
| `run_temporal_pass.py` | PASS: 154 numerical / 158 state comparisons, 142 samples, hostile stage-0 texture transform and coordinate index added |
| `run_material_motion_structure.py` | PASS: 169 rows transformed, 169 with depth output, 1,691 check groups, 1,551,936 mutations, 4,394 row and 5,417 program perturbations (2 absent sites), 1,014 aliases, 2,535 absent pairs, release and ASan/UBSan |
| `run_material_motion.py` | PASS: Argon 1,428 checks / 82 configurations; rows 169 of 169, 21,103 checks, 44,280 samples; RT2 exact against the ZFUNC EQUAL replay (7,755,681 pixels), 2.6e-6 maximum z/w error, 0.0010 px UV |
| `run_ownership_integration.py` | PASS: 26 cases, all exit 0 |
| `run_ownership_integration_fallback.py` | PASS: fault `wrap_factory E_OUTOFMEMORY`, binaries unchanged |
| `run_scene_capture.py` | PASS: 36 scenarios, 4,908 checks, 16 samples, fresh build |
| `check_no_x87.py build/d3d9.dll` | PASS: 125 reachable functions, 0 violations |
| `python3 -m unittest discover -s verification/analysis` | 442 tests OK |
| `manage.py launch ... --taa --taa-debug ... --dry-run` | PASS: `X3M_TAA=1`, `X3M_TAA_DEBUG=1`, `X3M_MOTION_JITTER=1`, `X3M_OBJECT_TRACE=1`, `X3M_OBJECT_LIFETIME=1` in the printed environment |

Verdict: go for installing with `X3M_TAA=1` on the documented command; the
first gameplay run remains a user-managed comparison, and the sub-pixel crawl
on unrouted draws is the expected visible limitation.
