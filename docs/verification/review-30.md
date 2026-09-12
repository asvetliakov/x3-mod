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
`game_guard`). The initial session paused before the chain completed. The remaining chain
and independent artifact audit finished on 2026-09-13; the table distinguishes
fresh runs from unchanged-source evidence reused after verification.

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
   Root converted `docs/status.md`'s pending section into the completed review-30
   checkpoint after the suite chain below passed.

## Suite results — completed 2026-09-13

The continuation started from documentation-only HEAD `f4d2384`, whose production
sources are the WIP checkpoint `1c8322e`; the earlier low fixes above were already
included. Production remained frozen. The only new code changes in this review
are temporal **fixture/runner** progress markers, a larger total runtime budget,
and timeout child cleanup with five host controls. Root independently reviewed
those changes. Root's concurrent goal/status documentation commit `8ec83d0`
changes no compiled source.

Every fresh Wine command ran through `wine_lock.py --holder review30`, one at a
time, with no game or other fixture at admission. No game was launched. Native
Windows remains untested. Existing green evidence was reused only after checking
its current source map, retained executable and raw report/trace hashes; it was
not relabelled as a fresh run. Rebuilt shared fixture paths can replace older
executables, so Steam loading/motion binaries were retained under the untracked
`verification/probe/build/review30-retained/` directory and per-case directories.

| Suite | Bottle | Final result |
| --- | --- | --- |
| `run_motion_output.py` | Steam | Reused current-source PASS: 97 cases, all exit 0, 26 benchmark cases. All 97 traces and 194 case binaries rehashed. |
| `run_temporal_pass.py` | Steam | Reused current-source PASS: 508 numerical checks, 278 state restorations, two generations, 386 samples. Six `QUAD_TWIN` results identical; both `COPY_MODE` results history-identical with zero differing display bytes. |
| `temporal_run.py` | Steam | Fresh PASS: 78/78 samples, two generations and Reset, exit 0, 180-second budget; approximately 65.2 seconds from runtime start to final report write. Source/executable unchanged; report and stderr hashes recorded after closure. |
| `run_ownership_integration.py` | Steam | Reused current-source PASS: 26 cases, all exit 0; 145-source initial map and all 52 raw report/trace hashes verified. |
| `run_scene_capture.py` | Steam | Reused current-source PASS: 4,908 checks, 16 samples, 36 scenarios. |
| `run_loading_trace.py` | Steam | Fresh PASS: 86 ABI/IAT + 123 mesh + 35,984 adjacency cache-off + 36,041 cache-on = 72,234 checks. Both 2,000-mesh random sweeps equal, zero mismatches. |
| `run_loading_trace.py` | X3 | Fresh PASS: the same four inventories and 72,234 checks; both 2,000-mesh sweeps equal, zero mismatches. |
| `run_d3d9_exports.py` | Steam | Fresh PASS: writable and read-only cases, 8 checks each; 17 exports, game/profile capture-directory paths. The profile log was copied into results before the runner removed its bottle-local original. |
| `run_object_lifetime.py` | Steam | Reused current-source PASS: 574 checks, 80 backend calls. Seven source hashes, executable and raw report match. |
| `run_object_trace.py` | Steam | Reused current-source PASS: 166 checks, 120,017 backend calls. Seven source hashes, executable and raw report match. |
| `run_gz_buffer.py` | X3 | Fresh full PASS: 735,876 checks, default 10-million-call timing workload, zero failures. |
| `run_resource_reader.py` | X3 | Reused full, non-quick PASS: 4,707 checks including 20 cursor sources; 14 current source hashes and executable match, retained raw output reparses to the stored report. This is bounded fixture evidence, subject to the open review-31 limitations below. |
| `run_mesh_adjacency_cache.py` | Steam | Fresh PASS: 767 checks, source/native/executable stability verified. |
| `run_mesh_cache_hook.py` | Steam | Fresh PASS: six cases, 1,714 / 2,003 / 2,011 / 2,189 / 2,673 / 2,681 checks, total 13,271. |
| `generate_rigid_motion_pixel.py --check` | Steam | Fresh PASS: all ten shaders reproduce exact bytecode, including the 43-word quad VS. |
| `run_motion_output.py` | X3 | Fresh PASS: 97 cases, all exit 0, 26 benchmark cases; 234 seconds wall time. Quad twin, draw-copy twin and MSAA refusal all pass. |
| `check_no_x87.py build/d3d9.dll` | host | Fresh PASS on the final DLL: 196 reachable functions, zero violations. |
| `unittest discover -s verification/analysis` with `PYTHONPATH=verification/probe` | host | Fresh PASS: 836 tests in 34.547 seconds, including the five new timeout-cleanup controls. |
| `pe_exports.py build/d3d9.dll` | host | Fresh audit: 17 names, ordinals 1–17, no forwarders, machine `0x014c`. |

On **both bottles**, the motion quad twin retains identical RT1/RT2 readbacks
(16 files) and histories (8 files). The draw-copy twin retains identical histories
and 49,152/49,152 identical presented pixels across twelve frames, maximum code
difference zero. The final audit also compares the retained history/present files
and the quad twin's motion/depth files directly. The MSAA case refuses before
routing, with `msaa=2`, no motion target and the named skip reason.

### Temporal timeout diagnosis and correction

The quiet 60-second retry reproduced the inherited timeout after 39 passing
samples and `RESET PASS`. Instrumentation then localized substantial CPU work to
`D3DXCompileShader` on the 17,853-byte production shader. The fixture compiles it
once per generation; this is not a failing numerical assertion or proof of a
Reset deadlock. A second diagnostic attempt timed out at 60 seconds while its
Wine child continued and eventually appended all 78 samples **after** the runner
had already recorded timeout. That late raw PASS was not accepted: the summary
and growing output were not a valid finished evidence pair.

The reviewed correction raises only the total runtime budget to 180 seconds;
all 78 samples, both generations, Reset and exit-zero checks remain required.
The runner also retains its Wine lock while a timed-out child drains. It selects
only `temporal_resolve.exe` with the exact final shader argument, refuses a
pre-existing matching fixture, allows 15 seconds to exit, then rechecks PID and
command before TERM and, after a further ten seconds, KILL. It never targets a
Wine server, launcher or game and does not release the lock while a matching
child remains. Report/stderr hashes are computed after closure. Host controls
cover unrelated processes, path-prefix collisions, natural completion, PID reuse
and both signal stages. The final 180-second run exited normally; timeout cleanup
was not needed. Failed diagnostic attempts remain superseded evidence, not passes.

## Final artifact binding and limits

The final full proxy DLL rebuilt by the **new X3 motion run** is:

`d648594bf346f8ccc8d5e476bcc26345d16741974f76c5b3769017075712e825`

The earlier Steam motion DLL `ef190bcb…` is retained as its own run's binary;
identical current production source maps bind the two builds. PE build identity
and differing fixture rebuild timestamps must not be mistaken for source drift.
The independent [artifact audit](../../verification/results/review30-artifact-audit.json)
records source/report/native/binary hashes, both motion case inventories and raw
twin comparisons. Separate retained host records cover
[836 tests](../../verification/results/review30-host-tests.txt),
[no-x87](../../verification/results/review30-no-x87.json),
[exports](../../verification/results/review30-exports.json), and
[ten shader regenerations](../../verification/results/review30-shader-check.json).

Root also extended `.gitattributes`' existing exact-byte report rule to nested
`verification/results/**/*.txt` and `**/*.log`. Independent review confirmed
that otherwise `core.autocrlf=input` would normalize the X3 reports and break
their recorded raw hashes. `git check-attr` confirms `text` is unset for both
top-level and bottle-specific reports; this metadata change requires no runtime
rerun.

The [independent adjacency/reader review](review-31-adjacency-reader.md) remains
open and belongs to the next checkpoint. The passing bounded loading suite does
**not** erase the known Steam SSE2 competing-normal adjacency discrepancy,
registry-type mismatch or unqualified arithmetic domain. Likewise, the retained
reader fixture does not establish correctness of its ignored final cursor-seek
failure, original LastError input, shifted diagnostic format, or permissive parser
cases. No such fix is included here and this review does not authorize a general
fast-mode parity claim. Keep those optimization paths opt-in and require their
separate fixes/review and user-managed verification before fast-mode acceptance.

Native Windows still needs a real run. The half-pixel guard-band note and the
format-21-only named MSAA refusal in the source review remain unchanged design
limits. D1/D2/D3/W1/W3 have documented-API reasoning plus the named Preview
fixtures, not native-Windows runtime certification. The fixture timings measure
synthetic work; they are not game FPS or a new loading-time claim.

**Verdict:** review 30's frozen native-Windows fixes and regression checkpoint are
accepted with the explicit review-31 opt-in-path limitations above. The previously
paused suite chain is complete. No commit or install was performed by this
reviewer; root owns the checkpoint commit, installation, status update and later
user-managed game tests.
