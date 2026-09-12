# Contract for conditionally replacing the bloom compositor

2026-09-13; architecture prerequisite, **not implemented or game-tested**.
This supplements and narrows the “one-line test” in
[compositor-and-glow.md](compositor-and-glow.md) §7.4. The glow bit is a user
preference gate; it is not evidence that a replacement reached the back buffer
or preserved the engine's continuation state.

Evidence: current `scene_hook.cpp/.h`, `capture.cpp::scene_end_signal` and
`MotionOutput::scene_end_hook`; targeted read-only Ghidra 12.1.3 inspection of
the existing `/tmp/x3-ghidra-research/X3Render` project. The executable identity
is the one in [compositor-and-glow.md](compositor-and-glow.md), preferred base
`0x00400000`. No game, Wine command, build or installation was run. Raw output
is local under `/tmp/x3-bloom-skip/`; only derived findings are recorded here.

## Boundary, ABI and preference reads

The frame routine `0x00471f50` sorts its views, then invokes `0x004c4750`
at most once, immediately before the first view whose signed layer at
`view+0x29c` exceeds `0x11`. The five bytes at `0x004721b1` are
`E8 9A 25 05 00`. The continuation at `0x004721b6` marks the frame's compositor
done; `0x004721bb` reloads EAX from `[ESI+0x270]` for the environment-map test.
Do not bypass that continuation or move the decision to Present: subsequent
views, per-view colour overlays, the special UI view and text still run.

The compositor takes no stack arguments and ends with plain `RET` at
`0x004c4f60`; its result is unused here. A successful replacement can return
to the existing CALL return address without stack cleanup. The fallback must
tail-jump to the original with exactly the original stack and registers. The
current trampoline preserves integer registers/flags with `pushfl/pushal`,
and the signal preserves x87 state, MXCSR and LastError. A conditional version
must retain those protections and the four-byte incoming stack contract.
In particular, a C return value in EAX is lost by `popal`: carry the decision
in invocation-local scratch and branch before restoring registers/flags on
two separate exit paths. Do not put the decision in a process-global bool.

Retain executable identity, exact call displacement/site bytes, patch ownership,
rollback and quiescent install/uninstall validation. Also validate the nearby
layer test and continuation when admitting a new executable. Do not infer a
new ABI from Ghidra's incomplete prototypes. The stream-cache decompiler, for
example, misidentified its saved stride as a return address; assembly confirms
the actual five-argument stdcall setter and `RET 0x14`.

Use unambiguous pointer chains, with safe reads and a refusal on failure:

| Value | Verified chain / site |
| --- | --- |
| Settings | `settings = read_ptr(0x00606f34)` |
| Player glow preference | `read_u8(settings + 0x100) & 0x80`, test `0x004c4770` |
| Renderer | `renderer = read_ptr(0x00608b3c)` |
| Game device record | `record = read_ptr(renderer + 0x18)` |
| Original glow capability | `read_u8(record + 0x94) != 0`, test `0x004c478a` |
| Device interface used by the compositor | `read_ptr(record)`, load `0x004c47c9` |
| Effect state manager | `read_ptr(renderer + 0x1c)` |

The old prose expressions omit/misplace dereferences; do not copy them as C.
Re-read live option/capability values at this boundary, rather than once at
startup. The original additionally returns without drawing for absent scene/glow
textures or the quad vertex buffer (`0x00608a64/68/6c/74`), and can fail
acquiring the effect/technique/surfaces. It does not precheck the quad
declaration at `0x00608a70` before using it at `0x004c4d43`.
A future replacement should use its own explicit supported-device/resource
gates. Availability of the original game's textures does not prove availability
of the replacement, nor need those textures become a permanent prerequisite
for an independently supported replacement.

## What the continuation actually inherits

The successful original uses `D3DXFX_DONOTSAVESTATE`, so it does not restore a
general device-state snapshot. The table combines executable control flow
with the inspected stock `bloom.fb` DEFAULT technique described in
[compositor-and-glow.md](compositor-and-glow.md); effect-derived final state
is not established by executable identity alone. Its known output is more than
an image:

| State | Original successful path | Replacement requirement / evidence limit |
| --- | --- | --- |
| RT0 | Main surface again, selected by the last pass | Restore the same main surface identity; finish FP16 redirection before returning. |
| Viewport | `{0,0,main.Width,main.Height,0,1}` via `0x004c6300` | Establish this explicitly for a successful skip, after RT restoration. Incoming viewport restoration alone is insufficient. |
| Depth surface | Incoming surface restored at `0x004c4f14` | Restore the saved depth identity, including a valid null binding; release temporary references. |
| Vertex declaration | Game quad declaration via manager slot `0x60` | Replacement must leave no private declaration/cache disagreement. Exact incoming restoration is the proposed contract; see the remaining per-view qualification below. |
| Stream / indices | Quad stream 0, offset 0, stride 24; null indices via manager slots `0x64/0x68` | Restore all state touched by replacement, including stream state changed by UP draws. Do not imitate these bindings by raw device calls while leaving the manager's cache unchanged. |
| Shaders, constants, textures, sampler/render state | Residue of the final effect pass; three effect texture parameters subsequently cleared | Restore replacement mutations to coherent incoming values. Clearing effect parameters is not proof that all device sampler slots have been unbound. No original effect is entered by a skipped invocation. |
| Blend | Captured final pass: enabled, ONE / INVSRCCOLOR, ADD; depth test/write off | This is original residue, not the replacement's desired blend equation. Late fixed-function overlays explicitly set their own source/destination blend; complete per-view inherited-state equivalence remains a verification item. |
| Software vertex processing | Set TRUE at `0x004c4c6e` when `read_u32(read_ptr(record+4)+0x6d8)==2`; not restored | Cannot silently omit this branch for a supported mode. First implementation must either reproduce the required final setting through public D3D calls with validated device-mode mapping, or decline the replacement for this game mode. |

The `0x004be520` camera helper unconditionally calls `SetViewport(view+0x278)`
at `0x004be646`; a null/invalid viewport causes failure, not a guaranteed new
viewport. The caller `0x0047c840` independently tests the pointer before Clear.
Consequently “every later view sets a viewport” is insufficient to drop the
main-sized viewport postcondition. Late overlay/text routines themselves do
not select RT0, a depth surface or a viewport.

Targeted continuation findings:

- Each ordinary subsequent view calls `0x0047c840` before its geometry. The
  environment-map branch is evaluated after the compositor boundary; it calls
  `0x004b9660` before entering the excursion. There is no universal reset
  immediately after the compositor for the ordinary-view path.
- The second loop invokes colour overlay `0x004c53d0` after camera setup when
  `view+0x77c` is nonzero, temporarily clearing view flags `0x30`. A separate
  special UI-view path also performs camera setup/geometry and this overlay.
- `0x004c53d0` tests the alpha byte of the colour arriving in EAX. When nonzero
  it sets ZENABLE false, alpha blend true, SRCALPHA/INVSRCALPHA, fixed-function
  texture-stage state, null VS/PS, FVF `0x44`, and draws a triangle strip with
  `DrawPrimitiveUP` (stride 20). It then calls `0x004b9660`.
- Text `0x004c5830` establishes null VS/PS, FVF `0x104`, ZENABLE false, alpha
  blend true, texture/sampler/stage state and SRCALPHA/INVSRCALPHA for its first
  UP strip; its second strip uses ONE/ONE. It then unbinds texture 0 and calls
  `0x004b9660`. Neither routine relies on the bloom quad declaration, stream or
  final source/destination blend for its own draw. They do not set every render
  state, so this is not proof of complete state independence.

## The effect state manager is part of the contract

The pure-device manager memoizes declaration (`0x004b5060`, object `+0x2090`),
indices (`0x004b50a0`, `+0x2094`), VS/PS (`+0x2098/+0x209c`) and the last
stream tuple (`0x004b51b0`, `+0x20a0..+0x20ac`). Constant, texture and sampler
shadowing is covered in [constant-uploads.md](constant-uploads.md). These are
game structures, not Wine layouts; they are evidence, not an invitation for a
new renderer to write private cache offsets.

`0x004b9660` calls manager slot `0x58`, then installs a set of default render
states. The pure implementation `0x004b4dd0` clears multiple caches and constant
shadows and unbinds texture stages 0–9; the base implementation `0x004b4910`
unbinds those textures. This is **not** a transparent restore operation and
should not be inserted indiscriminately at the scene-end hook. Its existence
after UP overlays explains how those routines repair their own direct-device
mutations, but does not protect the ordinary views that precede them.

Recommended approach: keep the game manager untouched, execute replacement work
through public D3D operations, and restore exactly the incoming state for every
state category the replacement touched, with explicit exceptions: RT0 must
be the main surface and viewport must cover the full main target, even when
the incoming viewport differs. Depth retains its incoming identity. The
admitted software-VP mode also follows the boundary postcondition above. Restoring
only shaders is insufficient; declaration, stream, constants and texture state
can also make cached later setters skip real work. A state block alone must not
be assumed to cover target/depth/software-VP bindings or to prove restoration
succeeded. Use the existing renderer's shadow/restore contract and audit its
actual coverage before admitting bloom.

## Ownership and a per-call commit decision

Today `capture.cpp::scene_end_signal` broadcasts a void notification to every
entry in `devices` while holding its recursive mutex. Each `MotionOutput`
independently decides whether its selector is in Scene; `hook_scene_end` only
records that the boundary was consumed. `resolve_hdr`/`end_redirect` have no
commit result exposed by this callback. Neither a counter increment nor
successful allocation/resolve means bloom reached the main image.

A future bool/enum return must have one eligible owner selected **before any
bloom commit**, by matching the engine device interface read above to the
capture device. The current `devices` map is keyed by the exact hooked
`IDirect3DDevice9*`; it has no general device-canonicalization helper. Use an
exact pointer match for this verified engine record, or explicitly compare
canonical `IID_IUnknown` identities through documented COM APIs if wrapped
interfaces require it. Surface descriptor/resource equality is not a device
identity test. Reject ambiguous matches. Do not OR results from the broadcast, choose map order, or let another live
device's scene flag authorize skipping this engine call. Preserve mutex/lifetime
discipline and refuse ambiguity, unmatched interfaces, reset/destroyed devices,
reentrant calls and wrong frame/scene phase. Include device identity, reset
generation and frame/boundary generation in any ticket; consume it exactly once
within this synchronous call. Other devices may retain notifications if needed,
but cannot own or mutate this replacement decision.

Suggested decision states are `RunOriginal` (default) and
`ReplacementCommitted`. Publish the latter only after all of these are true:

1. This exact call's owner, user glow preference, engine/profile gate and
   replacement capability/resource admission are valid.
2. The intended resolved FP16 scene and this frame's bloom have been generated;
   the display transform including bloom has actually written the main surface.
   A fallback image without bloom is not a bloom commit.
3. Redirect unwind, main/depth restoration, full-main viewport and all required
   state/cache-coherence checks succeeded; no replacement effect/pass remains
   active and no error is pending from restoration.

Prepare bloom in scratch before the main write. On a failure before commit,
retain the existing no-bloom write-back fallback and execute the original.
If a bloom-inclusive main write succeeded but a later restoration failed,
simply returning `RunOriginal` risks applying original glow on top of the
replacement. Provide a recovery path that rewrites the main surface from the
retained no-bloom scene and restores bindings before fallback. If recovery also
fails, neither exit is a verified clean continuation: record and disable the
feature under the existing device-failure policy. Do not call that a commit.
Keep this failure distinction explicit in implementation and fault tests.

## Remaining implementation gates and focused verification

Static analysis establishes the ABI, ownership gap, direct late-UI setup and
cache hazards. It does **not** prove that restoring the incoming state instead
of all original bloom residue is pixel-equivalent for every later material
effect. Before enabling skip, compare the first ordinary late-view draws and
both fixed-function paths with/without original execution, including states
that those paths do not explicitly set (for example alpha test, blend op,
colour masks, fog and sRGB write). Resolve concrete gaps with targeted material
effect/pass inspection; do not assume the UI's own SRCBLEND setters cover them.

Required fixture cases: both trampoline exits with full CPU/stack preservation;
glow off; unmatched/two-device ownership; duplicate/reentrant signal; no Scene;
reset generation mismatch; bloom/tonemap/write-back/restore failures; cache
coherence when a following manager setter receives the same incoming shader,
declaration, stream or texture; non-main incoming viewport and null depth;
software/mixed-VP admission or explicit refusal. A successfully skipped frame
must have one image commit and zero original compositor calls. A fallback must
have zero committed replacement bloom and exactly one original call.

Batch resulting diagnostics into one build for user-controlled runs covering
ordinary flight, late HUD/text/overlays and a menu glow toggle. No additional
game launch is authorized here. Public D3D source/cross-compilation is the
Windows compatibility target; native-Windows behavior remains unverified.

Performance: decision/option reads occur once per boundary, never per draw;
avoid scanning devices after an owner has been identified for the call. Do not
perform image readbacks or private-manager resets for validation. Reuse state
storage/resources, measure boundary GPU/CPU cost and separate it from game FPS.

## Reproduce the additional targeted inspection

Use the command in [compositor-and-glow.md](compositor-and-glow.md#reproduce)
with `-readOnly -noanalysis` and `X3CameraState.java`, writing outside the repo.
The additional bounded specs used here were:

```text
dec:00471f50 dec:004c53d0 dec:004c5830 dec:0047c840 dec:004be520
dec:0047c640 dec:004c4750 dec:004c6300 dec:004b9660
ptr:00562afc:28 ptr:00562a8c:28
dec:004b4dd0 dec:004b4910 dec:004b5060 dec:004b51b0 dec:004b50a0
ins:004b51b0 ins:004c53d0 ins:004c4750
range:004721a8:14 range:004c4770:22 range:004c6300:35
```

No shader asset bytes or decompiler output belong in the repository.

Independent Sol/xhigh review checked the source and targeted listings. Its
viewport-exception, null-resource-gate, device-identity and stock-effect
provenance clarifications were applied; remaining admission questions above
stay open.
