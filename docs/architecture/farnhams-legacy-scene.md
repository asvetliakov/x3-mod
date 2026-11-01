# FL scene-end hook and compositor owner (step 5)

`src/engine/fl/scene_hook_fl.cpp` and `compositor_owner_fl.cpp` replace ap's `scene_hook.cpp`/`compositor_owner.cpp`
([build](farnhams-legacy-build.md)); facts: [compositor](../reverse-engineering/fl/compositor.md), [owner](../reverse-engineering/fl/compositor-owner.md).

## Site (`tools/analysis/fl_scene_hook_constants.py` → `scene_hook_fl_inc.h`, `--check`)

| What | RVA | Bytes / operand | Checked |
| --- | --- | --- | --- |
| Boundary window in the complete pass `0x8b6d0` (sole caller `0x782cf`) | `0x8b920` | `83 be 9c 02 00 00 11 7e 09 e8 42 ae 05 00 c6 45 f3 01 8b 8e 70 02 00 00` | capstone: cmp [esi+0x29c],0x11; jle 0x8b932; call; mov byte [ebp-0xd],1; mov ecx,[esi+0x270] |
| Compositor call | `0x8b929` → `0xe6770` | rel32 = target − (site + 5); one qword (offset 1) | the target's only E8 caller; no relocation in the window; both branches into it land on `0x8b932` |
| Compositor entry | `0xe6770` | `55 8d 6c 24 c8 64 a1 00 00 00 00 83 ec 38` | 553 instructions to `0xe6e87`, three bare `c3`, no jump out, no `[ebp+≥0x3c]`/`[esp+≥0x40]` operand: no stack argument |
| Glow anchor | `0xe6790` | `a1 <cfg>; test byte [eax+0xf0],0x80; je` | disp32 relocated → config slot `0x1a83b0` |
| Owner anchors | `0xe67a2`, `0xe67ec` | `a1 <renderer>; push ebx; mov ebx,[eax+0x18]`; `mov ebx,[ebx]` | disp32 → renderer slot `0x1aaf94`; camera activation `0xded96..0xdeda8` uses the same chain |

Done byte `[ebp-0xd]` (cleared `0x8b8eb`, tested `0x8b917`): ≤ 1 call per pass, **none** without a camera above layer 17.
Owner read: `renderer = *(base+0x1aaf94)`, `record = [renderer+0x18]`, `device = [record]`; no manager word, so FL
`same()` compares the three (header fence; AP objects unchanged). Reads and the glow test (`*(base+0x1a83b0)+0xf0 &
0x80`, AP `+0x100`, used by `compositor_glow_enabled`) refuse any base not admitted.

## Install and CPU state

`initialize()` (FL block of `load_backend`, after `object_lifetime`; `X3M_SCENE_HOOK`, default on with the route):
`fl::verified()`, window (`callsite_`/`target_`/`boundary_mismatch`), entry (`entry_mismatch`), anchors at the live
base (`glow_`/`owner_anchor_mismatch`), install window (`late_claim`), pin, optional bridge bind
(`bridge_bind_failed`), `engine_patch::claim_call` (one `lock cmpxchg8b`, rollback; unbind if the bytes are not
ours), then `compositor_owner::admit(base)`. Paths: with `compositor_binding()` (bloom + route + HDR AgX) the call goes
to the shared `x3m_compositor_bridge_entry` (full CPU/SEH pre/original/post, original on the bridge stack — legal
because the compositor takes no stack argument); otherwise to a trampoline `pushfl; pushal; cld; call signal;
popal; popfl; jmp original` whose signal holds XMM0-7 (movups) and `PreserveCpuState` (LastError, FNSAVE/FNINIT …
FRSTOR, MXCSR) around `capture.cpp::scene_end_signal`. The compositor then runs on the caller's stack with the
caller's registers, EFLAGS and FP/SIMD state and RETs to `0x8b92e`. Built without SSE/MMX/exceptions, 32-bit atomics.

## Frames without the call, bridge assumption, consumers

No compositor call means no hook entry and no StretchRect copy (on FL the only one is inside the compositor): the
frame gets no TAA resolve, the history is invalidated and HDR is written back unresolved at Present, as on AP (same
layer-17 test). The hook counts such frames as `frames_without_call`. **Assumption:** the bridge replaces the native
glow with the AP bloom commit with no FL shader-profile or `bloom.fb` check (flown with high shader quality and stock
`bloom.fb`; medium/low quality or a modified `bloom.fb` is unverified); the runtime guard is `validate_boundary`
(`bloom_prepare`/`bloom_commit reason=boundary`, no draw). `composition_scene_owner` can now be true on FL, so linear
emission, distance fade and screen emission draw when their options are on; none is in the user's config or FL-verified.

## Rows for the flight

`fl.scene status=active base=… site=… target=… path=bridge|signal bridge_requested=… renderer=… write=atomic` once;
every `X3M_FL_SCENE_LOG` s (`fl_scene_log`, default 10, 0 off; at Present) `fl.scene_sample … frames entries
frames_without_call frames_multi_call nested compositor_active owner_reads owner_failed owner_last_failure glow
renderer` (`nested` counts bridge entries only: the signal path tail-jumps into the compositor and never sees its
return). Evidence and expected values: [ledger](../verification/farnhams-legacy.md), "Step 5".
