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

## Status per item

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
