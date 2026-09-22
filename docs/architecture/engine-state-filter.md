# Engine-side state filter: decision note

2026-09-16. Question: with the per-draw state traffic and its redundancy now
measured, should the proxy cut it on the engine side (a filter on the game's
`ID3DXEffectStateManager`, or an interception in the D3DX pass application),
or accept the frame? Inputs are the run 31/32 ledgers in
[sampling-profiler.md](../verification/sampling-profiler.md) ("Run 31 session
A (run89)", "Run 32 session A1 (run91)"), the per-call benchmark
`verification/results/bottle-X3/state-hook-benchmark-run32.json`, the
frame-loop study [frame-loop-phases.md](../reverse-engineering/frame-loop-phases.md)
§2 and the fast-path note [state-call-fast-path.md](state-call-fast-path.md)
(a), (d), "Hybrid unhook". Byte facts below were re-read from the bottle's
`X3AP.exe` (`fdbf3418…`) with `i686-w64-mingw32-objdump` and a section-table
script; nothing was run under Wine.

## Decision

**C, with one measurement: do not build an engine-side state filter now.**
The arithmetic below bounds everything a filter can remove at about 0.3–1.0 ms
of the 37.3 ms busy frame, because the device-side cost of a state call under
this bottle is 11–15 ns and there are 63,311 of them. The 23.3 ms of game code
between hooked calls is therefore not device calls; it is the D3DX pass-apply
machinery and the engine's own per-object work, in an unknown ratio. The single
measurement that splits them (four accumulating stamps in the pass loop of
`0x004c0150`, below) decides whether option B is worth designing further.
Option A stays on the shelf as a fully specified design; it is not worth its
hazard list for ≤ 1 ms.

## Measured inputs (run91 busy window `frame=4200`, 300-frame window)

- `dt_p50` 37.32 ms, `draws_p50` 1,006, `state_calls_p50` 63,311 (63 per draw);
  `views` 32.1 ms, hooked draws incl. native 7.75 ms, `gap_draw_p50` 23.26 ms
  (23.1 µs per draw of code outside hooked calls), `scene_update` 65 µs.
- `state_top`: `set_sampler_state` 31,954, `set_render_state` 18,826,
  `set_texture` 4,566, `set_ps`/`set_vs` 1,006, `set_vs_constant_f` 1,003,
  `state_other_p50` 973. SetTextureStageState is not hooked and not counted.
- Redundancy within the shadowed subset (cumulative rs/ss/tex over the window):
  `state_redundant` 2,754,453 / 1,590,735 / 534,520 of `state_shadowed`
  2,902,118 / 1,601,271 / 1,329,862 = 94.9 % / 99.3 % / 40.2 %. Per frame that
  is 9,674 / 5,338 / 4,433 shadowed calls, of which 9,182 / 5,302 / 1,782 are
  proven no-ops (~16,300 of 63,311). The shadow covers the 16 indexed render
  states plus WRAP0–15 (`motion_output.cpp` `shadow_index_scan`) and, per
  sampler stage, SRGBTEXTURE and MIPFILTER; the other ~9,150 render-state and
  ~26,600 sampler-state calls per frame are unmeasured. `redundant_top`
  7 ZFUNC, 25 ALPHAFUNC, 24 ALPHAREF, 14 ZWRITEENABLE, ~292k each, i.e. one
  redundant write per draw per state: the per-pass apply loop re-issues them.
- Batchability 5.0 % instancing candidates, 6.0 % material-sortable: no
  proxy-side instancing or sorting.
- Per call, X3 bottle, medians (`state-hook-benchmark-run32.json`):
  native SetRenderState 14.7 ns, SetSamplerState 11.0, SetTextureStageState
  11.4, SetTexture 16.2; production (hybrid unhook) SetRenderState 10.9,
  SetSamplerState 10.8, SetTexture 122.7 (hooked), SetVertexShaderConstantF 75.2.
  The benchmark alternates values (`i * 2654435761`, `1 + (i & 3)`), so 14.7 ns
  is Wine's value-changing path; the redundant path is at most that.

## Cost ceiling for any engine-side filter

If every one of the 63,311 calls were free: 18,826 × 10.9 + 31,954 × 10.8 +
4,566 × 122.7 + ~4,000 × ~75 ≈ 0.21 + 0.35 + 0.56 + 0.30 = **1.4 ms**, and a
filter cannot elide the 1,006 shader sets or the constant uploads (they carry
per-draw data), so ~1.1 ms is the practical ceiling. The 23.3 ms gap already
contains the ~50,800 unhooked SetRenderState/SetSamplerState calls at ≈ 0.6 ms;
the remaining ~22.7 ms is executed elsewhere.

## Option A: filter on the game's state manager (shelved, fully specified)

Mechanism. The game's manager (vtable `0x00562a8c`, 21 slots, `.rdata`) holds
the device pointer at `[this+4]`; slots 3–10 and 12–20 are identical 21-byte
stdcall thunks `mov eax,[esp+4]; mov eax,[eax+4]; mov ecx,[eax];
mov [esp+4],eax; mov eax,[ecx+disp]; jmp eax` followed by `int3` padding
(re-read: `0x004b49f0` SetRenderState `+0xe4`, `0x004b4a50` SetTexture
`+0x104`, `0x004b4a30` SetTextureStageState `+0x10c`, `0x004b4a10`
SetSamplerState `+0x114`, `0x004b4a70` SetVertexShader `+0x170`, `0x004b4bd0`
SetVertexShaderConstantF `+0x178`, `0x004b4ab0` SetFVF, `0x004b4a90`
SetPixelShader `+0x1ac`, `0x004b4c30/50/70` PS constants; slot 11 SetNPatchMode
`0x004b4bb0` differs, it loads its float with `fld`). The cheapest install is
not an entry trampoline but four byte-verified dword stores into the vtable
slots 7, 8, 9, 10 (`engine_patch::write_code` on the `.rdata` page inside the
install window, expected value = the thunk address), replacing each with a
proxy stdcall function that reads `[this+4]` for the device. An entry
trampoline would work too (first two instructions are 7 bytes, no branch), but
the slot store displaces nothing and needs no arena tail. Per call the filter
does a bounds check, one table compare and `ret 0xc`/`ret 0x10` with `eax=0`;
estimate 3–5 ns under FEX. Removed per elided call: the thunk (6 instructions),
the device vtable dispatch, Wine's d3d9 entry and, for SetTexture, the proxy's
hooked path (122.7 ns). Not removed: everything D3DX does to decide to issue
the call.

Expected saving (busy frame): proven no-ops 9,182 × ~10 ns + 5,302 × ~8 ns +
1,782 × ~118 ns ≈ 0.34 ms; unmeasured render/sampler states at an assumed
90–95 % redundancy (same apply-loop origin) add ≈ 0.28 ms; SetTextureStageState
is uncounted (estimate 0–0.1 ms). **≈ 0.6 ms, range 0.3–1.0 ms, 1–3 % of the
frame.** The per-call estimates for the unmeasured parts are estimates; the
range brackets the redundant-path cost of wined3d (unmeasured, ≤ 14.7 ns).

Hazards and mitigations, if it were built:
- The game's own direct device writes bypass the manager (17 SetRenderState
  callsites, all in the fixed-function overlay routines; the cached texture
  setter `0x004b9ed0` calls the device SetTexture directly) and so do the
  proxy's `native<>` writes and per-draw restores, state-block `Apply`,
  `EndStateBlock`, Reset and device loss. The filter's shadow must be fed by
  the device-level hooks (SetRenderState/SetSamplerState are unhooked in
  production; they would have to be re-hooked, costing back part of the
  saving) or invalidated wholesale at every bypass site: `resync_shadow`,
  `after_reset`, the route's restore failures, BeginScene. An invalidation
  marks all entries unknown so the next write forwards.
- Recording: while any `BeginStateBlock` is open the filter must forward
  everything (a recorded call must reach the block regardless of device
  value); run87/run91 saw no state blocks from the game, but D3DX records
  blocks unless `D3DXFX_DONOTSAVESTATE` is passed, and the game does pass it
  (`0x004c1ebe`), so only the proxy's own blocks matter.
- Get* readers (the hybrid unhook reads `GetRenderState`/`GetSamplerState` per
  draw) see device state; the filter only skips writes whose value already
  equals device state, so the readers are unaffected as long as the shadow is
  exact. `X3M_STATE_SHADOW=1` must agree by construction: same table, same
  compare; a mismatch counter (`filter_disagree`) in the diagnostic build.
- SetTexture: same-pointer elision is refcount-neutral (the runtime AddRefs
  the new and Releases the old only on change), but the pointer may be a
  freed-and-reallocated texture; the shadow must be cleared on the proxy's
  texture-release observation or compared by pointer plus creation epoch.
- Native Windows: the runtime filters redundant state on a non-pure device,
  so A changes only the call count there; it uses documented Win32
  (`VirtualProtect`) and a hash-gated EXE patch like the frame-phase sites.
- Verification would be: `run_game_phase_cpu.py`-style fixture with a fake
  manager object and the four slots (install, refuse on byte mismatch,
  rollback, elision counts, forwarding on recording/invalidation); the
  motion-output seams unchanged; in the diagnostic build `state_redundant`
  drops to ~0 while `draw_pairs`, `cutout_pairs` and the per-frame draw count
  stay identical; felt FPS in a session without `--frame-timing`.

A loses because its ceiling is ~1 ms and its hazard surface is the whole state
shadow, which the hybrid unhook just removed from the hot path.

## Option B: intercept the D3DX pass application (contingent on the measurement)

What loads. The game directory holds Microsoft's `d3dx9_37.dll` (3,786,760
bytes, imported by the EXE, IAT `0x00532324` `D3DXCreateEffect`); the bottle's
`system32` also has Wine's builtin `d3dx9_37.dll`, and no `DllOverrides` entry
touches `d3dx9_37` ([bottles.md](../verification/bottles.md) line 161). Wine's
default order for a non-core DLL is native then builtin, so the Microsoft DLL
is expected to load; **not runtime-verified** (no session log records loaded
modules). One `GetModuleFileNameW(GetModuleHandleW(L"d3dx9_37"))` line at
first Present in the diagnostic build settles it.

Portable form. No byte patch into d3dx9: wrap the `ID3DXEffect` the game
receives. Either IAT-patch the EXE's `D3DXCreateEffect` slot (game-EXE-bound,
same class as the existing trampolines) or trampoline the game's single
`SetStateManager` callsite `0x004bb07e` (`call edx`, slot 71). The wrapper is
a documented COM interface (`d3dx9effect.h`), so it is native-Windows
compatible and independent of which d3dx9 is loaded. What it can skip: the
game brackets every sub-mesh with `Begin(&passes,1)` `0x004c1ebe`,
`BeginPass(i)` `0x004c3ffe`, the draw `0x004c403c`, `EndPass` `0x004c4047`,
`End` `0x004c4066`. `BeginPass` re-applies the pass's whole state block
(hence the ~292k-per-window ALPHAREF/ALPHAFUNC/ZFUNC/ZWRITEENABLE writes);
consecutive draws with the same effect, technique and pass could be coalesced
into one open pass with `CommitChanges` between them (documented: applies only
changed parameters). That removes the apply loop itself, which A cannot.
Hazards: the game's direct device writes between draws (the cached texture
setter, overlay routines), interleaved effects, `SetTechnique` changes,
parameter blocks (`ApplyParameterBlock` ×2 per draw), and any D3DX-internal
dirty tracking that assumes `BeginPass` ran. None is designable until the
measurement says how much time the apply loop owns. If it is < 3 ms, B is not
worth it either; if it is ≥ 10 ms, B is the only lever on this frame short of
drawing fewer objects.

## What the ~23 ms plausibly is, and the one measurement

Candidates, per draw (23.1 µs): (1) D3DX `BeginPass`/`EndPass`/`Begin`/`End`
— walking the pass's state assignments, evaluating them, dispatching ~60
manager thunks; (2) the game's ~75 effect parameter writes per sub-mesh
(`SetInt`/`SetVector`/`SetFloat`/`SetBool`/`SetMatrix`, `GetParameterByName`
by name, `IsParameterUsed`, `ApplyParameterBlock`) — all inside d3dx9 but
issued by the engine; (3) the engine's own per-node work in `0x004c0150`
(`0x3fa3` bytes, 332 calls), `0x004c4fc0`, the world-matrix and bounds calls,
and the O(n²) queue sort `0x0047e620` (which sits in `view_submit` but before
the first draw of each layer). All three run as emulated x86; none is in a
D3D call. The measurement: four accumulating mid-function stamps in the pass
loop, same contract as the frame-phase sites (`engine_patch` claim, ≥ 5
displaced bytes, no relative branch in the span, `pushfl/pushal` marker):

| Name | Address | Bytes | Meaning |
| --- | --- | --- | --- |
| `pass_begin` | `0x004c3ff0` | `8b 44 24 74 8b 13` | loop head; incoming `jb 0x4c405b` lands here |
| `pass_applied` | `0x004c4000` | `8b 44 24 28 8b 48 14` | `BeginPass` returned |
| `pass_drawn` | `0x004c403e` | `8b 13 8b 82 08 01 00 00` | the draw returned |
| `pass_end` | `0x004c4049` | `8b 44 24 74 83 c0 01` | `EndPass` returned |

`pass_begin→pass_applied` is D3DX apply time (including every manager thunk
and device call), `pass_applied→pass_drawn` the engine's draw path (the
class behind vtable `+0x148`, then the hooked `DrawIndexedPrimitive`),
`pass_drawn→pass_end` the `EndPass`. `view_submit` minus the sum of the three
is the engine's per-object setup plus the sort plus (2). At ~1,006 passes per
frame the four QPC stamps cost ~4,000 × 62 ns ≈ 0.25 ms, within the diagnostic
budget. Before claiming: Ghidra confirmation that no other edge targets the
interiors of the four spans, flag liveness across `0x004c3ff0` (the loop's
`jb` consumes flags before the head; the `cmp` at `0x004c4050` is after
`pass_end`), and the relative-replay check in a `verify_pass_phase_sites.py`
sibling of `verify_frame_phase_sites.py`. If (2) needs its own number, a
fifth stamp at the sub-mesh loop head `0x004c0223` separates parameter setup
from the pass loop.

## RE work needed before implementation

- For the measurement (recommended next step): **done** for the spans —
  [effect-pass-loop.md](../reverse-engineering/effect-pass-loop.md) validates
  all four as proposed (`verification/probe/verify_pass_phase_sites.py` PASS)
  and corrects two premises: there is no `CommitChanges` dispatch in
  `0x004c0150` (the apply interval is `BeginPass` alone), and the stamps are
  per-draw (~4,024 dispatches, 0.6–1.3 ms with an accumulate-only stub, not
  0.25 ms with the shared one). Still open: the module-identity line for
  `d3dx9_37`.
- For A (only if ever revived): confirm slots 7–10 of `0x00562a8c` are the
  only manager methods D3DX calls on the material path (slots 12–20 exist and
  forward too), confirm `[this+4]` is the device on every constructed manager
  (`0x004b4886`/`0x004b48dd`), and confirm the `.rdata` page permission and
  install-window timing (the effect-load path runs after `Direct3DCreate9`).
- For B: the `ID3DXEffect` vtable slots the game uses (9, 26, 30, 52, 59, 62,
  63, 64, 66, 67, 71 per frame-loop-phases §2) against `d3dx9effect.h`, and
  whether `CommitChanges` reaches the manager for constants only.

## Native Windows position

C changes nothing. The measurement is a hash-gated EXE patch like the
frame-phase sites (documented Win32 only, unverified natively, entry in
[platform-portability.md](platform-portability.md) when installed). A would be
the same class. B in its portable form is a documented COM wrapper and an IAT
patch of the game EXE; a byte patch into a Microsoft DLL is excluded.

## Unknowns

- The wined3d cost of a redundant state write: **measured 2026-09-22**, 10.1 ns
  for SetRenderState, 10.9 SetSamplerState, 10.5 SetTextureStageState, 15.9
  SetTexture same pointer, against a 9.5 ns no-op vtable call
  (`state-hook-benchmark-elision.json`; state-call-fast-path.md "Elision
  revisited under FEX"). A's saving sits at the low end of the 0.3-1.0 ms range;
  there is no per-call emulation boundary (wined3d is i386 PE under FEX).
- Which `d3dx9_37` loads: one log line, above.
- The split of the 23 ms between D3DX and engine code: the four stamps, above.
- SetTextureStageState traffic: uncounted because unhooked; the pass stamps
  make it irrelevant unless A is revived.
