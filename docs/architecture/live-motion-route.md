# Live same-draw motion route

Design record, 2026-09-12. The project direction changed: per-pixel motion is
produced by the game's own material draws through transformed shader variants
and a second render target, not by deferred geometry replay. The replay
admission, execution-scope and geometry-lease machinery stays in the tree as a
numerical reference but is **not** a prerequisite for this route. This document
defines the live integration boundary that the
[strategy](motion-output-strategy.md) left open after the
[detached prototype](../verification/material-motion.md).

## Why replay infrastructure is no longer on the critical path

Replay needed exclusive device access, retained vertex/index storage, finite
position evidence and a second rasterization with exact depth equality. A
same-draw variant needs none of that: the application's own draw already binds
the geometry, states and depth test. The only proxy work is ordinary
application-thread device calls made inside the existing draw hook, which the
capture mutex already serializes. Concretely, the route requires:

| Need | Mechanism | Existing pieces |
| --- | --- | --- |
| Substitute a shader pair for one draw | `SetVertexShader`/`SetPixelShader` around the native draw, then restore | private vtable hooks in `src/proxy/capture.cpp` |
| Supply previous rows and pixel ABI constants | `SetVertexShaderConstantF(252,…,4)`, `SetPixelShaderConstantF(216,…,2)`, restore only if the application had written those ranges | new setter hooks that shadow constant ranges |
| Bind RT1 and restore | `SetRenderTarget(1, motion)` before, `SetRenderTarget(1, nullptr)` after | scene-boundary adapter must ignore our own calls |
| Own RT1 across Reset/device loss | release before native `Reset`, drop on device release, recreate lazily | capture `reset`/`release_device` hooks |
| Cross-frame object identity | `(load_epoch, registry_epoch, node_serial, camera_serial)` from the lifetime observer | `object_trace`, `object_lifetime`, `MotionHistory` key |

Nothing here writes game vertex/index buffers, retains application COM
references across frames, or executes while the application is inside another
device call. Windows and CrossOver Preview both remain targets: every call is a
public D3D9 method, and the engine hooks are game-executable hooks, which the
user explicitly allows.

## Frame flow

```text
Present (frame N-1 succeeded)  → history.commit(): current rows become previous
Clear color+depth on main RT   → selector latches main/depth; motion RT fill draw
                                 writes the invalid sentinel (0,0,0,-1) to RT1
background draws               → no motion (not yet eligible; sentinel stays)
depth-only Clear               → selector enters Scene
material draws (Scene phase)   → eligible pair + known node scope:
                                 lookup previous rows, bind variant + RT1, draw,
                                 restore; record current rows under the key
StretchRect / bloom / overlays → selector leaves Scene; no routing
Present                        → commit or invalidate history; optional readback
```

Eligibility is decided per draw, fail-closed, from the following gates. Any
failed gate draws the original pair with no RT1 bound, exactly as today.

1. Feature switch `X3M_MOTION_OUTPUT=1`, device caps checked once:
   `NumSimultaneousRTs >= 2`, `MaxVertexShaderConst >= 256`,
   `D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS`, A32B32G32R32F render-target support
   with the main color format, and a passing one-time mixed-format MRT self test.
2. Scene phase: `SceneCapture::collecting_scene()` is true and RT0 is the
   selector's main color surface with its D24X8 depth bound. Selection runs every
   frame in this mode, not only in requested capture frames.
3. Shader pair: the currently bound VS and PS are both originals that have a
   registered variant, and the pair is one row of the reviewed profile table
   (16 class A/B/C pairs; see "Pair keying" below).
4. Draw state: alpha blend off, alpha test off, sRGB write off, Z enable and Z
   write on, COLORWRITEENABLE 15, no instancing on stream 0, integer constant
   i0.x in [0, 8] for profiles with a relative light loop.
5. Object scope: `object_trace::current()` and `object_lifetime::current()`
   return a known node and camera lifetime for this draw.
6. History: the previous frame committed, the same load/registry epochs and
   dimensions apply, and the key below matched exactly one previous entry that
   no other draw in this frame has consumed.

Gates 1–4 alone still permit drawing the variant with mode `c217.x = 0`, which
writes the invalid sentinel for the draw's pixels. That is deliberate: covered
pixels of an eligible material without history must not keep stale values from
the sentinel fill, and it lets the route be exercised before history exists.

## History key and previous rows

The key is `RigidDrawKey` from `src/renderer/motion_history.h` with these
fields populated: object and camera lifetimes (serials), load and registry
epochs folded into `draw_domain`, node and camera handles, model and LOD, vertex
and index buffer allocation identities, stream offset, stride, declaration
identity, topology, first index, primitive count, base vertex and vertex range.
The stored value is the four actually submitted rows `c24–27` (or the
profile's matrix register) at draw time, never a recomposed W·V·P
([camera numerics](../reverse-engineering/camera-numerics.md)).

Lookup happens during the frame against the sealed previous table while the
current table collects. Duplicate keys in the previous frame poison that key.
A duplicate in the current frame consumes the previous entry once; the second
draw gets no history. Both cases produce the sentinel rather than a guess.

Constant rows are read from a shadow maintained by hooking
`SetVertexShaderConstantF`, not by `GetVertexShaderConstantF` per draw. The
shadow also records whether the application has written `c252–255` or PS
`c216–217` since the last restore, so the route restores those ranges only when
the application depends on them.

## Jitter

Not part of this step. When TAA jitter is added it will be applied in the same
hook by adding the sub-pixel clip offset to the submitted rows
(`row0 += jx·row3`, `row1 += jy·row3`) for every scene draw, and the previous
rows stored in history stay jitter-free so `c216.zw` carries the prior jitter.

## Verification

The first live checkpoint is diagnostic: in requested capture frames the route
reads RT1 back after the scene phase and writes it beside the capture log, and
the capture records per-draw route decisions (gate that failed, key match,
constants used). Offline analysis compares the live motion against the CPU
projection of the stored previous rows for the same pixels, using the same
convention the detached fixture verified. Color must remain bit-identical to a
run with routing disabled for the same scene. No temporal consumer reads the
output until that comparison passes on user-managed gameplay captures.

Integration tests run the real DLL under Wine with the synthetic device fixtures
in `verification/probe/` and check: capability refusal paths, sentinel fill,
variant substitution with restore, constant shadow/restore, Reset with RT1
owned, device release, and that non-eligible draws are untouched. The same
runs repeat under the ownership wrapper (`X3M_OWNERSHIP=1`, alone, with the
copy-depth/scene-depth path and with the admission monitor), because the
gameplay run needs the wrapper for object lifetime and the route's native
slots are then the wrapper's forwarders: the route must leave the wrapper's
depth epochs, scene/state-block flags and admission untouched, order its
target release before the wrapper's Reset, hold exactly one logical device
reference per owned object so the final-Release probe still matches, and
observe no loss the application would not observe. The analysis and results
are in [motion-output verification](../verification/motion-output.md)
("Ownership wrapper interaction"), together with the documented gameplay
diagnostic command.

## Coverage plan

Two denominators appear in the evidence. The key validation uses the 24
gameplay frames selected by the runtime boundary rules (9,001 scene draws);
the profile study uses a clear-segment approximation over all 28 captured
frames (11,493 scene-segment draws). Against the profile denominator, the
Argon pair alone is 19.0% of scene draws and the 16 transformable pairs are
97.6%; against the gameplay denominator the Argon pair is 24.2%, between 0%
and 41% per frame.

The [key validation](../reverse-engineering/motion-history-key.md) shows the
full key above matches 99.97% of keyable scene draws across adjacent frames
with no in-frame duplicates; dropping buffer identity produces ambiguous
sub-mesh splits. The transformer is table-driven for classes A, B and C from
[motion output profiles](../reverse-engineering/motion-output-profiles.md):
16 pairs (35.0% + 21.9% + 40.7%). The remaining 2.4% of scene-segment draws
(four direct-clip bloom pairs, five SM1/SM2 pairs) has no row and keeps the
sentinel. Background and planet draws before the scene's depth clear,
particles, stardust, overlays and GUI also keep the sentinel, so the temporal
resolve rejects history there. The inventory comes from one capture session;
pairs first seen in other sectors are refused at gate 3 and show up in the
per-frame gate histogram, after which the generator can classify them.

## Implementation (checkpoint B1, 2026-09-12)

Delivered as a diagnostic route: the motion target is produced and read back,
no temporal consumer reads it. Verified only through the synthetic fixtures in
[motion-output verification](../verification/motion-output.md); gameplay
captures are the next step.

### Files and switches

| File | Role |
| --- | --- |
| `src/proxy/motion_output.{h,cpp}` | Per-device route: variant registry, state shadow, motion target, capability self test, sentinel fill, gates, substitution/restoration, history, diagnostics |
| `src/renderer/motion_row_history.{h,cpp}` | Pure in-frame previous-row table (lookup against the sealed previous frame while collecting); `MotionHistory` stays untouched as the replay reference |
| `src/renderer/material_motion.{h,cpp}` | Table-driven transformer, `material_motion_vertex_variant` / `material_motion_pixel_variant` (each stage is created separately by the game); the pair function remains for the detached fixtures; `material_motion_reviewed_pairs` is the profile table |
| `src/renderer/motion_output_profiles.h` + `motion_output_profiles_inc.h` | Row struct, class enum and the generated 16-row table (classes A, B and C) with compile-time consistency checks; see [material-motion-prototype.md](material-motion-prototype.md) |
| `src/proxy/capture.cpp` | Hook installation, state block wrapping, refcount-aware release, per-hook calls into the route; `X3M_MOTION_OUTPUT` parsing |
| `src/proxy/scene_capture.{h,cpp}` | `describe_surface` shared with the route |
| `tools/manage.py` | `--motion-output` (history needs `--object-trace --object-lifetime`; otherwise sentinel-only) |

`X3M_MOTION_OUTPUT=1` enables the route. Without `X3M_OBJECT_TRACE=1` and
`X3M_OBJECT_LIFETIME=1` gate 5 never passes and every eligible draw writes the
sentinel (mode 0); the selector, fill, substitution and restoration still run.

### Pair keying

Eligibility is keyed by the exact **pair**: gate 3 passes only when the bound
VS and PS fingerprints appear together in one table row
(`material_motion_pair_reviewed`), never on a VS alias alone. Variants,
however, are created **per original program**, one VS variant per VS object
and one PS variant per PS object, at creation time, because the game creates
the two stages separately and a VS such as `53a0a641107ed76c` or
`4944d81dfe531b37` serves four reviewed pairs each. This is correct only if
every row sharing a VS uses the same VS-side splice (output register,
TEXCOORD index, offsets, constant base), so that the one variant links with
each row's PS variant; the same holds for a PS shared by rows. Inspecting the
table: the four `53a0…` rows all use o6/TEXCOORD4, the four `4944…` rows all
use o7/TEXCOORD5, and no PS appears in two rows. Rather than rely on that
incidentally, `motion_output_profiles.h` proves it with a `static_assert`
over the generated table (`motion_output_profiles_consistent`), so a
regenerated table that broke the agreement would fail to compile instead of
mislinking; the registry code in `motion_output.cpp` then needs no per-pair
variant map, no extra device objects and no per-draw work beyond the existing
row lookup. If a future table needs different VS registers for different
pairs of one VS, the scheme to adopt is a per-pair VS variant keyed by
`(vs, ps)` in the registry; the static_assert marks exactly that point.

The route itself hard-codes two more table facts: the constant shadow
captures the one clip-row window c24–27, and gate 4 applies the one bound
`i0.x` in [0, 8]. A second `static_assert` in `motion_output.cpp`
(`rows_match_shadow`) requires every row's `matrix_register` and light-loop
fields to equal those, so a regenerated table with another matrix register
would fail to build rather than route draws whose rows the shadow never saw.

### Hooked vtable slots

Installed only when the switch is on and the device passed the capability gate
at attach, in addition to the existing hooks; a device the route refuses keeps
the plain table and pays nothing per setter call. Every index is asserted
against the SDK layout in `verification/probe/abi_check.cpp`.

| Slot | Method | Purpose |
| --- | --- | --- |
| 47 | SetViewport | viewport shadow for the selector's full-target check |
| 59 / 60 / 61 | CreateStateBlock / BeginStateBlock / EndStateBlock | state blocks get a private vtable (slots 2 Release, 5 Apply); recording suspends the shadow, EndStateBlock and Apply resynchronize it from the public getters |
| 87 / 89 | SetVertexDeclaration / SetFVF | declaration identity (element hash) and POSITION0 layout |
| 92 / 107 | SetVertexShader / SetPixelShader | bound program identity and registered variant |
| 94 / 96 / 109 | SetVertexShaderConstantF / I, SetPixelShaderConstantF | rows c24–27, i0, application writes to c252–255 and PS c216–217 |
| 100 / 104 | SetStreamSource / SetIndices | stream-0 and index allocation identities |
| 30 / 31 / 34 / 35 / 39 / 115 / 116 | UpdateSurface, UpdateTexture, StretchRect, ColorFill, SetDepthStencilSurface, patches | complete selector event stream (previously only with scene-depth capture) |

Native slots the route calls itself (never the hooked table): 6, 8, 9, 23, 28,
32, 36, 37, 38, 39, 40, 41, 42, 47, 48, 57, 58, 75, 76, 83, 87, 88, 89, 90, 91,
92, 93, 94, 95, 97, 100, 101, 103, 105, 106, 107, 108, 109, 110; the release
hook's reference-count probe uses native 1 and 2 (AddRef/Release).

The hot setter hooks (shaders, the three constant setters, viewport) use
`LightCallBoundary` (MXCSR and last error only) with a plain lock: their own
code on both sides of the native call is integer/SSE memory work, so the
legacy caller's x87 state is untouched by construction, and
`verification/probe/check_no_x87.py` proves the absence of x87 opcodes on
every function those hooks reach in the built DLL. The stream/indices hooks
(resource private-data identity, which logs on first sight), the declaration
and FVF hooks (foreign getters) and the state block hooks keep
`CpuCallBoundary`.

### Scene recognition and per-draw cost

The route owns its own `SceneBoundarySelector`, fed from the shadow rather than
from `SceneCapture`, because the capture adapter runs only in requested frames
with the ownership wrapper and hashes shader bytecode per draw. Per ordinary
draw the route costs two `GetRenderState` calls (Z enable/write) while the
selector is in Background or Scene; a candidate draw (gates 1–3 passed) adds
four render-state getters, one stream-frequency getter, the object observers
and one history lookup. No getter fetches shader bytecode or shader objects on
the routed path, no heap allocation occurs per draw (the history reserves its
tables once), and no state block is created.

### Sentinel fill and restoration

The latching Clear schedules the fill; it runs inside the next draw hook so it
is always within the application's scene on Windows and Wine. The fill draws
one XYZRHW strip with an embedded ps_2_0 (`def c0, 0,0,0,-1; mov oC0, c0`)
into the motion target as RT0 with RT1–3 and depth unbound. Saved before and
restored after, in this order: RT0, RT1–3, depth, viewport, scissor, FVF or
declaration (whichever the application used), vertex shader, pixel shader,
stream 0 (DrawPrimitiveUP clears it), and the render states ZENABLE,
ZWRITEENABLE, ALPHATESTENABLE, ALPHABLENDENABLE, CULLMODE, FILLMODE,
COLORWRITEENABLE, SCISSORTESTENABLE, STENCILENABLE, FOGENABLE, SRGBWRITEENABLE,
CLIPPLANEENABLE. Nothing changes if the initial state query fails.

A routed draw sets, and `after_draw` restores in reverse: COLORWRITEENABLE1,
RT1, pixel shader, vertex shader, and the reserved constant ranges only when
this draw set them and the shadow has seen the application write them (state
block Apply marks them written conservatively). The fixture compares every one
of these before and after each fill and each routed draw.

### Failure behavior

- Capability gate or self-test failure: route disabled for the device, one
  `motion_output_device` line with the reason; nothing else changes.
- Motion target allocation failure: no fill or routing until the next Reset.
- Partial application of a routed draw: already-set state is undone and the
  original pair draws; counted as `apply_failures`.
- Restoration failure: counted, logged at most 16 times per device, the frame's
  history is still committed only if the fill succeeded.
- Reset: target released before the native call, history/selector invalidated,
  shadow resynchronized after success; variants survive.
- Final device Release: owned variants and target each hold a device reference,
  so the release hook probes the count and drops them first when only the
  caller's reference remains, preserving the application's zero return.

### Not covered

Jitter, any temporal consumer, gameplay captures, the SM1/SM2/bloom programs
outside the table, instanced or user-memory draws,
MSAA targets, Direct3D9Ex, native Windows
execution (cross-compiled only), and the measured cost of the setter hooks in
the game (each still takes the capture mutex and the admission entry; the
CPU-state boundary is the light one described above). Shader registry entries
are keyed by object address and replaced on reuse, never removed: a variant
whose original the game destroyed stays alive (one device reference each)
until that address is reused or the device is released.

## Engine constant-upload facts that the route depends on

The [constant upload disassembly](../reverse-engineering/constant-uploads.md)
shows that every shader/constant setter the game issues comes from its two
`ID3DXEffectStateManager` implementations inside `BeginPass`; `CommitChanges`
is never called, and no game code writes VS constants at or above c216 or PS
constants above c23. Two consequences are load-bearing:

- The pure-device state manager memoizes the last VS/PS pointer it forwarded.
  The route must therefore restore the application's shader pointers after
  every routed draw, or the next pass silently keeps rendering with the variant.
- The reserved constant ranges are never written by the game, so the
  conditional restore of c252–255 and c216–217 is a safety net, not a per-draw
  cost. The integer register i0 is written every draw and stays shadowed.

The setter hooks are on the per-draw path but shallow: at most about five VS
float writes, one integer write and a few PS writes per material pass.
