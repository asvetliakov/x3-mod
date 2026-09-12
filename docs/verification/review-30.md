# Review 30: native-Windows fixes D1–D3, W1, W3 on the merged tree

Review of main after the merge `20683cc` of the native-Windows fixes branch
on top of the review-29 commit `a34c389` (`git diff a34c389..20683cc --stat`:
69 files, 27 outside `verification/results/`). The branch implements the
[handoff](handoff-windows-fixes.md) against the
[native-Windows audit](../architecture/native-windows-audit-2026-09-12.md):
D2 (one vs_3_0 pass-through and declaration per pass instead of XYZRHW +
null VS), D1 (attach-time `StretchRect` round trip deciding
`taa_copy=stretch|draw`, the draw copy path), D3 (MSAA refusal by name),
W1 (17-name export table, naked `jmp` forwarders, `Direct3DCreate9On12[Ex]`),
W3 (`%LOCALAPPDATA%` log fallback), the `engine_memory` summary line and the
exposure fixture's IEEE-bits printing. Read: `src/proxy/loader.cpp`,
`capture.cpp`, `motion_output.{h,cpp}`, `src/renderer/temporal_pass.{h,cpp}`,
`hdr_pass.{h,cpp}`, `quad_vertex_program.h`, `src/temporal/quad_vs.hlsl`,
`tools/analysis/pe_exports.py`, the generator/compile-tool diff, the two
fixtures, the three runners and the documents named in item 7. Native Windows
stays untested; every statement about it is documented-D3D9 reasoning, and
the fixture evidence is CrossOver Preview (Steam and X3 bottles). No game was
launched; every Wine command ran under `wine_lock.py --holder review30`, one
at a time, after the user's own game runs had ended (the chain waited for
`game_guard`). The review was paused by the orchestrator before the chain
completed; the suite table records exactly what ran.

## Checklist

1. **D2, the vs_3_0 quad reproduces the XYZRHW convention.**
   `quad_vertices` (`src/renderer/quad_vertex_program.h:43-48`) puts the strip
   at x ∈ {−1 − 1/W, 1 − 1/W}, y ∈ {1 + 1/H, −1 + 1/H}, z 0, w 1. Through
   D3D9's viewport transform (`x_s = (x_ndc + 1)·W/2 + X`, `y_s = (1 −
   y_ndc)·H/2 + Y`) that is x_s ∈ {−0.5, W − 0.5}, y_s ∈ {−0.5, H − 0.5}:
   exactly the pre-transformed quad every pass drew before
   (`quad_vertices_xyzrhw`, `:54-58`), so every pixel centre i lands on
   texel centre (i + 0.5)/W as before. The equality holds only for a
   viewport of {0, 0, W, H, 0, 1}, and every bracket sets that before its
   draw (`temporal_pass.cpp:166` in `normalize`, `hdr_pass.cpp:416-417`
   copy draw, `:457-458` meter chain with the level's own `dw×dh`, `:523-524`
   MRT draw, `motion_output.cpp:1377-1378` `draw_quad`); `SetRenderTarget(0)`
   between the draws resets the viewport to the full target, which is the
   same rectangle. The vertices are computed per draw from the run's width
   and height (`TemporalPass::quad` `:121-125`, `HdrPass::quad` `:500-504`,
   `draw_quad`), so a Reset to a new size needs nothing; the vertex shader
   and declaration are not pool resources and survive Reset by contract
   (the three `before_reset` paths release only histories, scratch, staging,
   state block and targets; `TemporalPass::shutdown` `:183`,
   `HdrPass::shutdown` `:201-204` and `MotionOutput::release_resources`
   `:284` drop them). State: the temporal pass restores through its
   `D3DSBT_ALL` block (vertex declaration, FVF, vertex shader and stream
   sources are vertex state of that block; `DrawPrimitiveUP`'s stream-0
   reset was already covered), `MotionOutput::SavedState` and
   `HdrPass::SavedState` capture and restore `fvf`, `declaration`, `vs` and
   stream 0 explicitly (`motion_output.cpp:237-247`, `hdr_pass.cpp:175-190`).
   Clip state is normalised in all three brackets (`D3DRS_CLIPPLANEENABLE`
   0: `temporal_pass.cpp:142`, `hdr_pass.cpp:135`, `motion_output.cpp:176`),
   so the game's user clip planes cannot cut the clip-space quad the way
   they never cut a pre-transformed one. `SetFVF` remains only inside the
   `quad_fvf_` branches of `bind_quad_program`/`normalize`, and
   `quad_fvf_requested()` is `constexpr false` unless `X3M_QUAD_FVF_SWITCH`
   is defined, which only `build_temporal_pass.sh` and the seam objects of
   `build_motion_output.sh` do (`CMakeLists.txt` defines `WIN32_LEAN_AND_MEAN
   NOMINMAX` only). Version gates: `TemporalPass::initialize` (`:192`) and
   `HdrPass::attach` (`:781`, `vs_version`) refuse below vs_3_0; the route
   fails closed with `reason=quad_shader` when the creation fails
   (`motion_output.cpp:1060`). The sentinel and self-test programs changed
   only their version token (`def`/`mov oC*` encode identically). Bytecodes:
   the branch adds `quad_vertex_program_inc.h` (43 words) and touches no other
   `*_inc.h`; the nine manifests changed only their `tool_sources` hashes
   (`bytecode_sha256` unchanged), and the generator `--check` below
   recompiles all ten equal. Fixture twin evidence, reproduced below: temporal
   `QUAD_TWIN variant=stretch|sharpen|draw_copy identical=1` in both device
   generations, motion `seam-taa-quad-fvf` equal to `seam-taa-on` in colour
   hashes, history files, RT1/RT2 readbacks and check counts. *Note for the
   orchestrator (design, not changed):* the half-pixel overhang leaves the
   strip's outer vertices outside the clip volume by half a pixel while the
   temporal and HDR brackets draw with `D3DRS_CLIPPING` FALSE; on D3D9 that
   is resolved by the hardware guard band (every DX9-class part reports one
   in `D3DCAPS9::GuardBand*`), and the fixture proves it bit-exact on the
   Preview backend. The documented alternative, a −1…1 quad with the half
   texel folded into TEXCOORD0, gives the same (i + 0.5)/W per pixel and
   stays inside the clip volume; it would change nothing in the evidence but
   is a design choice, so it is left as is.
2. **D1, copy mode.** `MotionOutput::stretch_round_trip`
   (`motion_output.cpp:1164-1232`): per 8-bit format (A8R8G8B8, X8R8G8B8)
   two 4×4 render-target surfaces, one FP16 RT-texture level and three
   system-memory readback surfaces; 16 `ColorFill`s of distinct ARGB
   patterns, our own `BeginScene`/`EndScene` bracket around the two
   `StretchRect`s (attach runs outside the application's scene, at
   `CreateDevice`), `GetRenderTargetData` of all three, exact byte compare
   (RGB only for X8R8G8B8, whose alpha is undefined) and an FP16 decode
   with denormals and infinities that must sit within 1/1024 of v/255; every
   object is released and no pipeline state is touched. It runs once per
   device, only with TAA on (`:1092-1105`), and the decision fails closed:
   `taa_copy_draw_` is true unless the adapter query and both formats pass
   (`:1103`). The two directions tested are exactly the production ones
   (RT surface → RT-texture level, level → RT surface) with the production
   formats; the residual difference is 4×4 offscreen RTs standing in for
   the game's full-size main target, which D3D9's `StretchRect` rules do not
   distinguish. False-pass risk on Wine: wined3d blits any conversion, so
   both bottles take `taa_copy=stretch` and the resolve stays bit for bit
   what it was (the runners assert `taa_copy=stretch taa_stretch_test=pass`
   on every TAA case except the faulted twin). Draw path
   (`temporal_pass.cpp:251, 303, 316-318, 361-368`): a same-format
   `StretchRect` of the input into an owned staging RT texture
   (`ensure_staging` `:229-239`, re-created on a format change, default
   pool, released with the histories before Reset, refused as an input
   alias `:262-264`), the identity draw with RT0 = scratch and s0 =
   staging inside the scene bracket after `normalize`, and after the
   resolve the identity draw of the new history into the caller's 8-bit
   surface with `display_written` (the sharpen draw takes precedence);
   the failure policy is the sharpen draw's (resolve stands, display falls
   to the caller's copy-back, a lost device fails the run). If that
   fallback copy-back then fails on a device that refused the conversion,
   the frame keeps the game's own image and `taa_copy` in the frame line
   records it — a degrade, not a hazard. `MotionOutput::resolve` skips its
   copy-back on `display_written` and reports `sharpened` only with a
   sharpen (`:739`). The pass now holds four device references after its
   lazy initialisation (resolve, copy, VS, declaration; five with the
   sharpen), asserted by the runner (`TAA_BASE_REFERENCES`). The HDR path's
   copy rungs are untouched: the `hdr_pass.cpp` diff changes only the quad
   binding, the emergency `StretchRect` rung keeps its
   `CheckDeviceFormatConversion` gate plus live 4×4 self test. Evidence
   below: temporal `COPY_MODE draw_vs_stretch history_identical=1
   display_max_code_difference=0`, motion `seam-taa-copy-draw` history
   identical to `seam-taa-on` and presented frames exact.
3. **D3, MSAA refusal.** The refusal sits in `after_clear` on the
   AwaitInitialClear → Rejected transition (`motion_output.cpp:1755-1768`):
   `pending_.rt` known, identity, multisampled, A8R8G8B8, sized. RT1/RT2 are
   created only in the latch branch (`ensure_target` after `main_ =
   pending_.rt`, `:1769+`), which the selector never enters for a
   multisampled RT0 (`scene_boundary.h:135` requires `!s.msaa`), so no
   texture is allocated and no `SetRenderTarget(1|2)` can ever pair a
   single-sampled texture with a multisampled RT0; `target_surface_` stays
   null and gate 1 (`:1992`) refuses every draw, so no jitter constant or
   projection write happens (`jitter_active_ = false` as well).
   `resolve_allowed` (`:861`) skips with `TaaSkip::Msaa` = 11 before any
   other reason; `before_present` (`:2509`) reports 11 instead of 2
   (`NotReached`) when nothing attempted; the frame line's `msaa=` carries
   the sample count while refused and 0 otherwise; the state clears on a
   single-sampled latch (`:1775`) and in `before_reset`. The HDR redirect
   keeps its own `refused_msaa`. The log line is once per refusal episode.
   Note: like the selector, the refusal names only A8R8G8B8 (format 21)
   targets; a multisampled X8R8G8B8 RT0 is rejected silently as before.
   Evidence: `seam-msaa` (`X3M_FIXTURE_MSAA=2`, `CheckDeviceMultiSampleType`
   required for back buffer and depth) — one `motion_output_msaa_refused
   device=1 frame=0 msaa=2 width=64 height=64`, three frame lines `msaa=2
   routed=0 jittered=0 taa_skip=11 gate1=draws`, no `motion_output_target`.
   Cosmetic, fixed: the fixture's `run_msaa` comment said "the route latches
   the frame" while the runner asserts `latched=0 selector_state=9`
   (`motion_output_fixture.cpp:1799-1803`).
4. **W1, export table and calling conventions.** `pe_exports.py` on the
   rebuilt `build/d3d9.dll`: 17 names, ordinals 1–17 in name order, machine
   `0x014c`, no PE forwarders; on CrossOver's `i386-windows/d3d9.dll`: the
   15 names the handoff lists (no shim, no `On12Ex`). The C++ exports link
   as `_D3DPERF_BeginEvent@8`, `_D3DPERF_EndEvent@0`, `_D3DPERF_GetStatus@0`,
   `_D3DPERF_QueryRepeatFrame@0`, `_D3DPERF_SetMarker@8`,
   `_D3DPERF_SetOptions@4`, `_D3DPERF_SetRegion@8`, `_DebugSetMute@0`,
   `_Direct3DShaderValidatorCreate9@0`, `_Direct3DCreate9@4`,
   `_Direct3DCreate9Ex@8`, `_Direct3DCreate9On12@12`,
   `_Direct3DCreate9On12Ex@16` (`nm` of the DLL), i.e. the compiler emits
   the documented `ret N` for each. The naked forwarders
   (`loader.cpp:112-145`): `jmp *slot`; the resolver saves flags and all
   registers, calls the cdecl `x3m_resolve_export` (`:103`, `entry()` then
   the fallback, CAS-published, logged once), restores and jumps through the
   slot, so any convention and argument count pass through and a missing
   backend export lands on the fallback without a crash (the fixture's shim
   call proves `result=0 popped=4` twice). Disassembly of the fallbacks in
   the final DLL: `PSGPError` `ret $0xc`, `PSGPSampleTexture` `ret $0x14`,
   shim `ret $0x4`, `DebugSetLevel` `ret $0x4`. **Finding (low, fixed):**
   `DebugSetLevel`'s fallback was `ret` (0 bytes) against the documented
   4-byte cleanup (the D3D9 SDK import library's `_DebugSetLevel@4`; the
   handoff took 0 from Wine's argument-less spec stub, and the reviewer's
   signature list says 4) — `loader.cpp:142`, `test_d3d9_exports.py`,
   platform-portability.md and the audit table now say 4. The path is
   practically unreachable (Windows and Wine both export the name, so the
   forwarder resolves), but a wrong `ret N` there would corrupt a caller's
   stack. `Direct3DCreate9Ex` (`:191-211`): forwarded, `d3d9_export
   name=Direct3DCreate9Ex forwarded=1 unproxied=1` on an escape, admission
   vetoed (`UnobservedRoute`); the returned object is the backend's own
   `IDirect3D9Ex`, so a game path using it gets a working native device
   with no proxy hooks and no enhancements (`hook_direct3d` is reached only
   through `Direct3DCreate9`). `Direct3DCreate9On12[Ex]` (`:214-237`)
   follow the documented signatures and the same veto. Cosmetic, not
   changed: `log_export` records the first call only, so an `On12` first
   call that returns null followed by a later success leaves `unproxied=0`
   in the log while the veto still fires; and `run_d3d9_exports.py` ties the
   `forwarded` field to the object's presence for `On12`, which holds on
   Wine (an object is returned) but would trip on a backend that exports
   the name and returns null.
5. **W3, log directory fallback.** `initialize_log`
   (`capture.cpp:1312-1354`) is wide throughout (`GetModuleFileNameW`,
   `GetEnvironmentVariableW`, `CreateDirectoryW`, `_wfopen`), so a non-ASCII
   user name reaches the file system correctly; the fallback creates the
   two levels `x3-modern-renderer` and `captures` under `%LOCALAPPDATA%`
   (else `%USERPROFILE%\AppData\Local`), and `capture_directory()` follows
   for the readback files. **Finding (low, fixed):** the first line logged
   the directory with `%ls` through `vfprintf` in the CRT's "C" locale,
   which fails the conversion (and with it the whole line) on a character
   it cannot represent — exactly the non-ASCII user-name case the fallback
   exists for; the line is now UTF-8 through `WideCharToMultiByte`
   (`:1344-1354`). Cosmetic, fixed: the 64 KB `wchar_t[32768]` stack
   buffer of the env read became a two-call heap read guarded against a
   variable changing length in between. Path length: base + ~70 characters,
   no `\\?\` prefix — a profile path beyond about 190 characters would fail
   both `_wfopen`s and the proxy would run without a log, as before this
   branch. The read-only runner case (0555 directory; the log found under
   the bottle's `AppData\Local\x3-modern-renderer\captures`, nothing written
   into the directory, `source=localappdata`) and `manage.py status`'s
   `log_directories` are as described; the writable case proves
   `source=game`.
6. **`engine_memory` summary line.** `capture.cpp:1245-1246` emits
   `phase=create` from `hook_device` unconditionally, so it is present in a
   plain `--direct` run; `phase=summary` comes only from `telemetry::summary`
   (`telemetry.cpp:65`, telemetry on). `engine_memory::stats()` takes the
   reader's guard once and copies five integers (`engine_memory.cpp:120-125`);
   the line (`capture.cpp:1444-1451`) is `%llu` throughout, `hits = reads −
   queries` clamped at 0, `frame` falling back to the reader's epoch when the
   device's frame is 0 (creation). No per-draw work; `configure()` re-reads
   `X3M_ENGINE_READS` at each device creation, idempotently.
7. **Documents.** platform-portability.md closes D1/D2/D3/W1/W3 against
   named fixture evidence (`seam-taa-copy-draw`, `COPY_MODE`,
   `seam-taa-quad-fvf`, `QUAD_TWIN`, `seam-msaa`, `run_d3d9_exports.py`) and
   keeps the "no successful Windows run is claimed" closing item; the
   audit's status table, README's launch text (both log directories and the
   `capture_dir=` line), `motion-output.md`'s new section,
   `temporal-integration.md`, `live-motion-route.md`, `hdr-scene-path.md`
   §5 and `bottles.md` limitation 2 match the code. The one number the
   documents disagreed on (`DebugSetLevel` 0 vs 4) is fixed above.
   `docs/status.md`'s "Pending review 30" section is still to be converted
   into a checkpoint bullet of the latest-checkpoint section (structure kept)
   once the chain below is complete — TODO for the continuing agent.

## Suite results

**Paused (2026-09-13 ~00:36, orchestrator's session end): the chain ran
five suites and was stopped; the rest is TODO for the continuing agent.**
Host-only steps ran on the reviewer's clean rebuild of the merged tree plus
the fixes above (`cmake --build build --clean-first -j4`, 0 warnings,
`build/d3d9.dll` SHA-256 `5a5bb1d6028c78999290a2f919e5798f8beb4bfc2dae5c63f571a76613564f82`).
The Wine suites ran under `wine_lock.py --holder review30`, one at a time,
bottle Steam, each started only after `game_guard` reported no game and no
source had changed for five minutes; the motion runner's own
`RelWithDebInfo --clean-first` relink produced `ef190bcbf84b8088378872e05bbfa77be437636a57b9c77a4db0e21377fd903f`,
which is what `build/d3d9.dll` holds now. Caveat: two other agents were
editing the same checkout during the chain (uncommitted
`src/proxy/resource_reader*`, `mesh_adjacency_fast*`, `loading_trace.cpp`,
their fixtures, tests and docs), so every runner build from about 23:55 on
includes their in-progress sources; one motion run was aborted by its own
"sources changed during run" guard for that reason and was rerun once the
tree was quiet. The user launched the game three times during the review;
the runners refused as designed and the chain waited.

| Suite | Bottle | Result |
| --- | --- | --- |
| `run_motion_output.py` | Steam | PASS (`{"passed": true}`): 97 cases, 97 `exit=0`, 26 bench cases; `seam-taa-quad-fvf` identical to `seam-taa-on` (16 readback files, 8 history files, 164 checks each, `quad=xyzrhw_fixed_function`); `seam-taa-copy-draw` `history_identical=true`, presented `identical=true max_code_difference=0 exact_fraction=1.0`, 164 checks; `seam-msaa` `motion_output_msaa_refused device=1 frame=0 msaa=2 width=64 height=64`, 5 checks; 6.8 min wall |
| `run_temporal_pass.py` | Steam | PASS: `RESULT PASS numerical=508 state_restorations=278 generations=2`, 386 samples; `QUAD_TWIN` stretch/sharpen/draw_copy `identical=1` in both generations (6 lines); `COPY_MODE` ×2 `history_identical=1 display_max_code_difference=0 display_differing_bytes=0` |
| `temporal_run.py` | Steam | **timed out twice** (`timed_out=true exit_code=null timeout_seconds=60`, 39/39 sample checks of the first generation passed, `reset_passed=true`, one generation read) while another agent's builds and fixture loaded the machine — the same symptom the handoff recorded; the fixture is unchanged by this branch. TODO: rerun on a quiet machine (or raise the runner's 60 s budget, an orchestrator call) |
| `run_ownership_integration.py` | Steam | PASS: 26 cases, all `exit=0` |
| `run_scene_capture.py` | Steam | PASS: 4,908 checks, 16 samples, 36 scenarios |
| `run_loading_trace.py` | Steam | TODO (not reached; the fixture build `build_loading_trace.sh` compiles on the current tree, see below) |
| `run_d3d9_exports.py` | Steam | TODO (not reached; the branch's own record `verification/results/d3d9-exports-summary.json` says 8/8 + 8/8 on the pre-review tree, before the `DebugSetLevel` and `capture_dir` fixes — rerun required) |
| `run_object_lifetime.py`, `run_object_trace.py` | Steam | TODO |
| `run_gz_buffer.py`, `run_resource_reader.py` | X3 | TODO (another agent was running `run_resource_reader.py` on X3 under the lock with holder `reader` during this review) |
| `run_mesh_adjacency_cache.py`, `run_mesh_cache_hook.py` | Steam | TODO |
| `generate_rigid_motion_pixel.py --check` | Steam (fxc under Wine, locked) | TODO |
| `X3M_FIXTURE_BOTTLE=X3 run_motion_output.py` | X3 | TODO |
| `check_no_x87.py build/d3d9.dll` | host | PASS on `5a5bb1d6…`: 195 reachable functions, 0 violations |
| `unittest discover -s verification/analysis` (`PYTHONPATH=verification/probe`) | host | 800 tests OK (32.8 s; the three `test_d3d9_exports.py` tests included, with the `ret $4` expectation) |
| `pe_exports.py build/d3d9.dll` | host | 17 names, ordinals 1–17, no forwarders, `0x014c`; fallbacks disassembled `ret $0x4 / $0xc / $0x14 / $0x4` |
| `cmake --build build` and `verification/probe/build_loading_trace.sh` | host | both compile on the tree as left (00:36), including the other agents' uncommitted `loading_trace.cpp` `<cpuid.h>`/`__get_cpuid` edit (the fixture build the orchestrator saw failing was a mid-edit snapshot; this GCC 16.2 `cpuid.h` defines `__get_cpuid`) |

The chain script (`review30-chain.sh` in the reviewer's session scratchpad)
ran the suites in the order of the task; the continuing agent should restart
from `temporal_run.py` and finish the list above, then fill this table.

## Open / for the orchestrator

- Native Windows remains unverified: D1's round trip, D2's clip-space quad
  and W1's forwarders are documented-D3D9 reasoning plus Preview evidence.
  The first Windows run should confirm `taa_copy=` on the device line, one
  `d3d9_export` line per forwarder actually called and `capture_dir=`
  `source=game` from the Steam directory.
- The `D3DRS_CLIPPING` note of item 1 and the format-21-only refusal of
  item 3 are design choices, unchanged here.
- Installing this review's build into bottle X3 is the orchestrator's call
  (not done here; the installed build is the review-29 `4abd56b3…`).

**Verdict (interim, review paused):** the five items read as designed —
the clip-space quad reproduces the XYZRHW half-pixel convention exactly
under the full-target viewport every bracket sets, the round trip fails
closed to the draw copy, the MSAA refusal precedes any RT1/RT2 allocation,
the export table and every `ret N` match the documented signatures after
one fix, and the log fallback is Unicode-clean after one fix. Two low
findings (`DebugSetLevel` fallback popping 0 instead of 4 bytes; the
`capture_dir` line dropped on a non-ASCII path) and two cosmetic ones (a
64 KB stack buffer, a stale fixture comment) are fixed in the tree,
uncommitted. Five suites are green on the merged tree with the fixes
(motion output with the three native-Windows twins, temporal pass with the
quad twins and copy modes, ownership integration, scene capture) plus the
host checks; `temporal_run.py` timed out twice under machine load and the
remaining eleven runs are TODO. **Not yet ready for the checkpoint commit:**
finish the chain, then convert `docs/status.md`'s "Pending review 30"
section into a bullet of the latest-checkpoint section (structure kept,
counts from the table above), and commit only the review-30 files
(`docs/verification/review-30.md`, `docs/architecture/platform-portability.md`,
`docs/architecture/native-windows-audit-2026-09-12.md`, `src/proxy/loader.cpp`,
`src/proxy/capture.cpp`, `verification/analysis/test_d3d9_exports.py`,
`verification/probe/motion_output_fixture.cpp`, `docs/status.md`'s review-30
part, and the regenerated `verification/results/` records of the suites
above) — the other agents' uncommitted files in the same checkout are not
part of this review.
