# Handoff: native-Windows portability fixes (paused 2026-09-12)

Worktree `/Users/asvetl/x3-mod/.claude/worktrees/agent-a0100a8066b313223`, branch
`worktree-agent-a0100a8066b313223`, branched from `main` at `28a003d`. Paused by
the orchestrator (account switch) before any source edit: the commit carrying
this note contains only this file. No game was launched; no Wine command ran
(the only Wine-dependent step, the shader generator, was not reached).

Baseline built here from the unmodified sources (`cmake -S . -B build
-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=Release`):
`build/d3d9.dll` SHA-256 `7a9d79b75d1cd9ce311a54b39635e557d72e516ebef37981739ef3a632e47fef`
(untracked; a copy at the session scratchpad `d3d9-baseline.dll`). Note the
review-26 runner rebuilds with `RelWithDebInfo --clean-first`, so its hash differs.

## Status per item (second pause, 2026-09-12 ~21:20; commit "Native-Windows fixes in progress (paused)")

Every source, fixture, runner and doc edit of the design below is in the
tree and the tree builds (`build/d3d9.dll` SHA-256
`4253b2022f14a39a0163f66ea4c03a494e13a95494b4d187807e2d62d7238d38` after the
first full build; rebuilt incrementally after the D3 rework, 17 exports).
Merge note: main's `b10d129` did not compile (`loading_trace.h:61` used
`ID3DXMesh` undeclared); `struct ID3DXMesh;` forward declaration added.

| Item | Status | Evidence / exact next step |
| --- | --- | --- |
| D2 vs_3_0 pass-through | **done** | `src/temporal/quad_vs.hlsl` → `quad_vertex_program_inc.h` (43 words, provenance `verification/results/quad-vertex-program.json`; generator `target` per shader, compile tool 4th arg, `validate()` accepts `0xfffe0300`; all nine existing bytecodes unchanged, only their `tool_sources` hashes moved), `quad_vertex_program.h`; TemporalPass/HdrPass/MotionOutput create one VS + declaration, bind them in `normalize`/`bind_quad_program`; sentinel + self-test programs are ps_3_0 now; `abi_check.cpp` slot 86. Twin `X3M_QUAD_FVF_SWITCH`/`X3M_FIXTURE_QUAD_FVF=1` in `build_temporal_pass.sh` and the seam set (temporal_pass.cpp recompiled into the seam). Temporal fixture: `QUAD_TWIN variant=stretch|sharpen|draw_copy identical=1` in both generations; motion partial: `seam-taa-quad-fvf` 164 checks (= `seam-taa-on`). |
| D1 copy mode | **done** | `MotionOutput::stretch_round_trip` (16 ColorFills, StretchRect to FP16 and back, exact bytes / FP16 ≤ 1/1024), `taa_copy=stretch|draw taa_stretch_query= taa_stretch_test=` on the device line, `motion_output_taa ... copy=`; `TemporalPass::initialize(..., sharpen, copy)`, `configure_copy`, `ensure_staging`, identity draws both ways, `Output::copy_result`; `format_conversion` refusal removed. Temporal fixture: `COPY_MODE draw_vs_stretch history_identical=1 display_max_code_difference=0`; motion partial: `seam-taa-copy-draw` 164 checks (`X3M_FIXTURE_STRETCH_FAULT=1`; the fixture's reference pass follows the same switch). Twin comparisons of the full suite not yet run. |
| D3 MSAA refusal | **done, unverified after rework** | The selector never latches a multisampled RT0 (`scene_boundary.h` `color()` requires `!msaa`), so the design's latch site never fires; the refusal now lives in `after_clear` on the AwaitInitialClear→Rejected transition (`main_msaa_`, `main_msaa_samples_`, `motion_output_msaa_refused device= frame= msaa= width= height=` once, gate 1, no jitter, `TaaSkip::Msaa=11` at frame end, `msaa=` frame field; cleared by a single-sampled latch or Reset). Fixture mode `msaa` (`X3M_FIXTURE_MSAA`, `CheckDeviceMultiSampleType`), runner case `seam-msaa` + `validate_msaa` (expects `latched=0 selector_state=9 taa_attempted=0 taa_skip=11`). The first partial run (before the rework) found no refusal line; the reworked DLL and seam are built but **the `seam-msaa` partial has not been rerun**. |
| W1 export table | **done (Wine run passed, runner fix unverified)** | `d3d9.def` 17 names; `loader.cpp` naked `jmp` forwarders (`X3M_FORWARDED_EXPORT`, resolver `x3m_resolve_export` logs `d3d9_export name= forwarded=` once via CAS on the slot) with `ret`/`ret $12`/`ret $20`/`ret $4` fallbacks; C++ `Direct3DCreate9On12[Ex]` with admission veto, `unproxied=` log, `Direct3DCreate9Ex` logs too. Host: `tools/analysis/pe_exports.py`, `verification/analysis/test_d3d9_exports.py` (3 tests pass). Wine: `d3d9_exports_fixture.cpp` + `build_d3d9_exports.sh` + `run_d3d9_exports.py`: the writable case's fixture passed **8/8 checks** (On12 forwarded a live factory, shim fallback result 0 popped 4, On12Ex `8876086a`), then the runner failed on its own `windows_to_bottle` (assumed `C:`; the worktree is on `Y:`): mapping through `dosdevices/<letter>:` fixed, **rerun pending** (also the read-only case). |
| W3 log directory fallback | **done (rerun pending)** | `initialize_log`: `%LOCALAPPDATA%\x3-modern-renderer\captures` (else `%USERPROFILE%\AppData\Local`), first line `capture_dir=<path> source=game|localappdata` (seen in every run since); README, `manage.py status` `log_directories`. The read-only runner case did not run yet (the runner aborted before it). |
| `engine_memory` summary line | **done** | `capture.cpp::engine_memory_line(phase, device, frame)` after `attach` (`phase=create`) and from `telemetry::summary` (`phase=summary`); `capture.h` declares it. Line format per the design; integers only. |
| FEX NaN bits | **done (X3 rerun pending)** | `EXPOSURE_BLOCKS` prints non-finite values as `0x%08x`; `parse_float` decodes `0x…`; bottles.md limitation 2 annotated. The `X3M_FIXTURE_BOTTLE=X3` motion rerun has not run. |
| Docs | **done, numbers pending** | platform-portability.md, audit status table, hdr-scene-path.md §5, temporal-integration.md, live-motion-route.md, motion-output.md section, README, bottles.md, status.md section. Suite counts in status.md are partly placeholders ("see the commit message"). |
| Suites | **partial** | Run: `run_temporal_pass.py` (fixture PASS 508/278/2, 386 samples; runner assertion updated from 468/228 afterwards — **rerun pending**), motion partial `seam-taa-on seam-taa-copy-draw seam-taa-quad-fvf` (164 checks each, pass; `seam-msaa` failed before the rework), `run_d3d9_exports.py` (fixture pass, runner mapping fixed), `check_no_x87.py` (no violations, 147 reachable functions), unittest discover (759 OK, 1 skipped), generator full regeneration (all bytecodes unchanged; `--check` **not yet run**). Not run: full `run_motion_output.py`, `temporal_run.py`, `run_ownership_integration.py`, `run_scene_capture.py`, `run_loading_trace.py`, `run_object_lifetime.py`, `run_object_trace.py`, the X3-bottle motion rerun. Interim result files under `verification/results/` (temporal-pass.*, motion-output-partial.*, d3d9-exports-*) were **not committed**; the next runs write them. |

Next step, in order, each `python3 verification/probe/wine_lock.py <cmd>` with
the game down: `run_motion_output.py seam-msaa` (partial; expect the refusal
line), `run_d3d9_exports.py`, `run_temporal_pass.py`, then the full chain
(`run_motion_output.py`, `temporal_run.py`, `run_ownership_integration.py`,
`run_scene_capture.py`, `run_loading_trace.py`, `run_object_lifetime.py`,
`run_object_trace.py`), generator `--check`, then `X3M_FIXTURE_BOTTLE=X3
run_motion_output.py`; fill the counts into status.md / motion-output.md, commit
"Native-Windows fixes D1–D3, W1, W3; engine_memory summary; FEX NaN bits
(pre-review 28)".

## Original status per item (first pause)

| Item | Status | Exact next step |
| --- | --- | --- |
| D1 format-converting StretchRect | not started (design below) | edit `src/renderer/temporal_pass.{h,cpp}`, `src/proxy/motion_output.{h,cpp}` |
| D2 vs_3_0 pass-through | not started (design below) | add `src/temporal/quad_vs.hlsl`, generator target support, `src/renderer/quad_vertex_program{_inc}.h`, then the three draw sites |
| D3 MSAA refusal | not started (design below) | latch site `motion_output.cpp` ~1609, gate 1 ~1823, `resolve_allowed` ~832 |
| W1 export table | not started (list and contract verified below) | `src/proxy/d3d9.def`, `src/proxy/loader.cpp` |
| W3 log directory fallback | not started | `src/proxy/capture.cpp::initialize_log` ~1285 |
| `engine_memory` summary line | not started | `capture.cpp` after `attach` (~1218), `telemetry.cpp::summary` |
| FEX NaN bits in the exposure fixture | not started | `motion_output_fixture.cpp` ~1654 (`EXPOSURE_BLOCKS`), runner `parse_float` ~1319 |
| Suites | none run | full chain under `wine_lock.py` after the edits (game must be down: `game_guard.game_running()` was `[]` at pause time) |

## Findings that fix the design (verified by reading the tree)

- The HDR emergency rung (`hdr_pass.cpp` self test stage `stretch`, ~705; ladder
  ~955) is already gated by `CheckDeviceFormatConversion` plus a live 4x4
  self test that demotes the rung on failure; D1 there needs no code, only the
  doc statement. The two D1 sites that need work are the TAA colour copy
  (`temporal_pass.cpp:269`) and the copy-back (`motion_output.cpp:738`).
- The identity write-back program `hdr_writeback_program()` (ps_3_0 `tex2D`,
  format-agnostic) is the copy program for both draw-path directions.
- The pass's `Vertex {x,y,z,rhw,u,v}` (stride 24) is already POSITION float4 +
  TEXCOORD0 float2: the vs_3_0 path keeps the layout and only changes the
  x/y values to clip space (`x = 2*xs/W - 1`, `y = 1 - 2*ys/H` with the same
  `-0.5` pixel shift: `{-1 - 1/W, 1 - 1/W}` x `{1 + 1/H, -1 + 1/H}`, z 0, w 1).
  Every proxy quad draws with the viewport equal to the full target, so no
  constant upload is needed; a pure pass-through VS suffices.
- vtable slots to add: `CreateVertexDeclaration = 86` (assert it in
  `verification/probe/abi_check.cpp`; 87/91 are already asserted).
- `run_temporal_pass.py` asserts exact check counts `(468, 228, 2)` and 386
  samples: adding fixture cases means updating those numbers.
- `run_motion_output.py` asserts `taa_reason in (ok, off)` (line 771); the
  `format_conversion` refusal disappears with D1 (it becomes the draw path).
- Export directory of CrossOver's `lib/wine/i386-windows/d3d9.dll` (parsed
  here, scratchpad `pe_exports.py`): 15 names — the seven `D3DPERF_*`,
  `DebugSetLevel`, `DebugSetMute`, `Direct3DCreate9`, `Direct3DCreate9Ex`,
  `Direct3DCreate9On12`, `Direct3DShaderValidatorCreate9`, `PSGPError`,
  `PSGPSampleTexture`. Windows adds `Direct3D9EnableMaximizedWindowedModeShim`
  and `Direct3DCreate9On12Ex` (17). Wine's `DebugSetLevel`, `PSGPError` and
  `PSGPSampleTexture` are unimplemented stubs (they call the spec stub
  reporter; no `ret N` visible), `DebugSetMute` is `ret` (0 args),
  `D3DPERF_SetOptions` `ret 4`. Our `d3d9.def` exports 11.

## Design decided (implement in this order)

1. **D2 first** (D1's draw path needs it): `src/temporal/quad_vs.hlsl`
   (`void main(float4 p:POSITION, float2 t:TEXCOORD0, out float4 op:POSITION,
   out float2 ot:TEXCOORD0){op=p;ot=t;}`), generator entry `quad_vertex`
   with a per-shader `target` (default `ps_3_0`; the compile tool takes the
   profile as an optional 4th argument; `validate()` accepts `0xfffe0300`),
   header-only `src/renderer/quad_vertex_program.h` (inline words +
   `QuadVertex`, `quad_declaration[]`, `quad_vertices(w,h,out)`). Each of
   `TemporalPass`, `HdrPass`, `MotionOutput` creates one VS + one declaration
   (both survive Reset), binds `SetVertexDeclaration` + `SetVertexShader`
   where it now binds `SetFVF` + `SetVertexShader(nullptr)`; save/restore
   already covers declaration/FVF/VS. Fixture-only twin path: under
   `-DX3M_QUAD_FVF_SWITCH` (add to `build_temporal_pass.sh` and the seam
   objects of `build_motion_output.sh`, recompiling `temporal_pass.cpp` into
   the seam set), env `X3M_FIXTURE_QUAD_FVF=1` selects the old XYZRHW path so
   a `seam-taa-quad-fvf` twin of `seam-taa-on` proves byte-identical presented
   frames and history readbacks (same mechanism as `SHARPEN_TWINS`), and the
   temporal fixture reruns its route/sharpen cases with both paths.
2. **D1**: `MotionOutput::attach` runs a 4x4 round trip per 8-bit format
   (A8R8G8B8, X8R8G8B8): 16 `ColorFill`s of a pattern into an RT surface,
   `BeginScene`, `StretchRect` to an FP16 RT-texture level and back into a
   second RT surface, `EndScene`, `GetRenderTargetData` of both, exact 8-bit
   compare and FP16 within 1/1024; both the adapter query and the test must
   pass for `taa_copy=stretch`, else `taa_copy=draw` (never a TAA refusal;
   log `motion_output_device ... taa_copy= taa_stretch_test=`). The pass gets
   `initialize(..., sharpen, copy)` (the identity ps) and
   `configure_copy(by_draw)`. Draw mode: same-format `StretchRect` of the
   8-bit main surface into an owned staging RT texture of the same format
   (what the game's own bloom copy does in-scene), one identity draw from it
   into the FP16 scratch, and after the resolve an identity draw of the new
   history into `color_surface` with `display_written=true` (the sharpen draw
   already covers `sharpen>0`), so `MotionOutput::resolve` skips its
   `StretchRect`. Wine keeps the stretch path bit-for-bit. Fixture seam:
   `X3M_FIXTURE_STRETCH_FAULT=1` (under `X3M_MOTION_OUTPUT_FIXTURE`) fails the
   self test; twin `seam-taa-copy-draw` vs `seam-taa-on` compared
   (exact or documented 1-code double-rounding differences).
3. **D3**: at the latch (`main_ = pending_.rt`), `main_msaa_ = main_.msaa != 0`;
   when set: no `ensure_target`, `history_.invalidate()`, one log line
   `motion_output_msaa_refused device= frame= msaa=`, gate 1 refuses every
   draw (no jitter), `resolve_allowed` skips, frame line gains `msaa=`; cleared
   with `main_ = {}` on Reset. Fixture mode `msaa` (env `X3M_FIXTURE_MSAA=2`
   sets `pp.MultiSampleType`, `CheckDeviceMultiSampleType` required), three
   plain frames, no readback; runner checks the line and `routed=0`.
4. **W1**: `d3d9.def` to the 17 names. `DebugSetLevel`, `PSGPError`,
   `PSGPSampleTexture`, `Direct3D9EnableMaximizedWindowedModeShim`: naked
   `jmp` forwarders (signature-agnostic; resolver logs `d3d9_export name=
   forwarded=` once) with `ret N` fallbacks when the backend lacks the export
   (0/12/20/4). `Direct3DCreate9On12(UINT, void*, UINT)` and
   `Direct3DCreate9On12Ex(UINT, void*, UINT, IDirect3D9Ex**)`: C++ forwarders
   that veto admission and log `unproxied=1`; `Direct3DCreate9Ex` gains the
   same log line. Host test `verification/analysis/test_d3d9_exports.py`
   (PE export directory of `build/d3d9.dll`, skip when unbuilt; parser from
   the scratchpad `pe_exports.py` moved to `tools/analysis/pe_exports.py`);
   Wine fixture `verification/probe/d3d9_exports_fixture.cpp` + runner: loads
   the DLL, resolves all 17, calls `D3DPERF_GetStatus` and
   `Direct3DCreate9On12` (forwarded; release the object) and the two
   fallbacks (`...Shim(FALSE)`, `...On12Ex` -> `D3DERR_NOTAVAILABLE`).
5. **W3**: `initialize_log`: `CreateDirectoryW` + `_wfopen` next to the DLL;
   on failure `%LOCALAPPDATA%\x3-modern-renderer\captures` (env var, then
   `%USERPROFILE%\AppData\Local`); first log line `capture_dir=<path>
   source=game|localappdata`; `capture_directory()` follows. Runner case: copy
   the fixture into a directory made read-only, find the log under the
   bottle's `drive_c/users/*/AppData/Local/x3-modern-renderer/captures`.
   `tools/manage.py status`/launch help and README name both locations.
6. **engine_memory line**: `engine_memory phase=create|summary device= path=direct|rpm
   reads= queries= hits=(reads-queries) rejected= rpm_calls= frame=` from
   `engine_memory::stats()` (guarded copy, integers only; no per-draw work).
7. **NaN bits**: `EXPOSURE_BLOCKS` prints each value with `%.9g` when finite,
   else `0x%08x` of its IEEE bits; `parse_float` decodes `0x...` via
   `struct.unpack('<f', struct.pack('<I', v))`. Production untouched.

Docs to update afterwards: `platform-portability.md` (close D1/D2/D3/W1/W3
with evidence), the audit's status column, `hdr-scene-path.md`,
`temporal-integration.md` (the `format_conversion` text at ~356),
`live-motion-route.md` (~371), README/launch log dir, `bottles.md`
limitation 2, and a `docs/status.md` bullet.
