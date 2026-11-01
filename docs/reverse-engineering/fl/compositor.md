# FL compositor, Clear roles and camera-consumer admission

Offline bounded study, 2026-10-28, `/mnt/c/X3FL-orig/X3FL.exe`, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Preferred VAs use base `0x400000`; runtime descriptors require admitted loaded
base plus RVAs. This extends the [camera publication contract](camera-buffers.md)
and [bounded renderer sampling plan](../../architecture/farnhams-legacy-engine-profiles.md#bounded-renderer-sampling-plan-after-value-lifetime-qualification).

**The FL scene-end seam is `0x48b929 → 0x4e6770`.** Ordinary background/scene
roles can be assigned from successful Clear events on a proven main pair,
correlated with the current owned camera activation. Native origin alone is
insufficient. In particular, cube Clear occurs **before** face-camera publication;
the auxiliary activation suppresses Clear and draws a screen-space tint. Neither
may populate a main/background camera latch. The contracts below are implementable
without another broad native scheduling audit; actual D3D identity and FL shader
coverage still need the finite runtime witness specified at the end.

## Evidence and reproducibility

```sh
python3 verification/probe/verify_fl_compositor.py
python3 -m py_compile verification/probe/verify_fl_compositor.py
```

[Verifier](../../../verification/probe/verify_fl_compositor.py), measured: **14
regions, 1,866 instructions, 121 operand anchors, 30 independently checked region
and seam edges, ten rejected mutations**. Eight mutations bypass region hashes
and exercise named anchors; a ninth bypasses hashes and anchors and changes the
Clear branch's mask, which the semantic interpreter rejects. The tenth redirects
a CALL to a seam-interior byte. An interpreter follows the actual bounded Clear
integer branch over **128 camera/config/viewport combinations**, plus **128
auxiliary-mask cases**, all of which suppress Clear. A separate role model tests
two accepted and eight denied examples; it is a specification, not a replacement
for the production selector or an executed hook.

Raw listings, `report.json`, producing scripts and `reproduce.sh` remain ignored
under `verification/results/fl-port/compositor/`. No shader bytes or raw
copyrighted disassembly are tracked. Direct-edge/HIGHLOW censuses establish the
named native cone, not arbitrary computed private-address callers. The cube
helper type is bound to imported `D3DXCreateRenderToEnvMap` at IAT `0x550344`;
public method layouts were checked against local MinGW `d3dx9core.h` and `d3d9.h`.

## 1. Complete pass and actual phase transport

Complete pass `0x48b6d0` collects active camera pointers, excluding the special
engine+0x64 scene, then sorts the array at `0x48b902` using comparator
`0x48ae80`. The comparator orders **camera+0x29c ascending**. In the sorted
loop, `0x48b920` tests layer against **17**, and `0x48b929` invokes compositor
once before processing the first camera with a greater layer. Byte EBP-0xd is
set at `0x48b92e` and suppresses later calls. No camera above that threshold
means no call at this seam; do not invent a Present-based equivalent.

The boundary denotes the native partition before later-layer cameras, not a
semantic assertion that every later draw is text or every earlier draw is world
geometry. Qualify main scene by actual Clear/depth/target history. The compositor
call occurs even when its glow-option check will immediately return; therefore
its entry can end a proven scene with glow disabled. A signal outside an admitted
Scene remains diagnostic and must not resolve another device's image.

| Native origin | Actual behavior and admitted role |
| --- | --- |
| `0x48ba43 → 0x495670` | Ordinary sorted camera activation. ECX=context, outgoing stack camera. Callee loads camera into ESI, passes EDX=camera to `0x4ded90` at `0x495687`, then calls Clear helper `0x4db750` at `0x49568e`. Only this origin, before the compositor boundary, can supply the initial-background/main-scene Clear correlation. It can also be late-layer or partial-view; actual role is conditional. |
| `0x48bbef → 0x495670` | Second traversal for nonzero camera+0x77c. Saves camera+0x270, applies `& 0xffffffcf` at `0x48bbdc`, activates, calls `0x4e7310` with ECX=camera/EDX=+0x77c, then restores flags at `0x48bc42`. Clearing bits 0x10 and 0x20 suppresses the Clear helper on every tested combination. This is an auxiliary tint, not another scene latch. |
| `0x48bc87 → 0x495670` | Engine+0x64 special scene after the sorted/auxiliary loops. It submits at `0x48bca9` and may also call tint at `0x48bcc9`. It is a separate late scene, never the main-camera latch in this contract. No assumption about a particular menu/HUD asset is needed. |
| `0x48b9f6 → 0x4977c0` | Cube rendering, using `0x4defc0` six times with face 0..5. Its target path and ordering below exclude it from main/background latch requests. |

The tint is identified by its own instructions: `0x4e7316` rejects zero ARGB
alpha, the color is copied into four vertices, depth testing is disabled at
`0x4e7439` (render state 7), shaders are cleared at `0x4e74d5/0x4e74e0`, FVF
**0x44 (XYZRHW|DIFFUSE)** is set at `0x4e74eb`, and `0x4e74ff` issues
DrawPrimitiveUP(triangle strip, 2, vertices, stride 20). Screen-space vertex
coordinates use the camera rectangle. This establishes the auxiliary overlay
behavior without guessing from the callsite name or image appearance.

## 2. Clear arguments and camera timing

`0x4db750` takes **EDX=camera**, not ECX. It rejects a null camera+0x278
viewport pointer. Let C=camera+0x270 and G=config (`*0x5a83b0`)+0xec:

1. It enters Clear construction only if C has bit 0x10 or `(C & 0x24)==0x24`.
2. D3DCLEAR_TARGET is set when C has bit 0x10.
3. D3DCLEAR_ZBUFFER is set when `(C & 0x24)==0x24` and G has bit 0x400.
4. D3DCLEAR_STENCIL is set when C has bit 0x800 and G has bit 0x200, but only
   after passing step 1. Stencil permission alone cannot enter this path.
5. Zero computed flags skips the call. Otherwise `0x4db7f8` invokes device
   vtable +0xac with **Count=0, rectangles=null, flags=ESI, color assembled from
   camera bytes +0x2ac/+0x2b0/+0x2b4, Z=1, stencil=0**. Its failure returns zero.

The ordinary setter publishes P/V, then SetViewport, then returns to the Clear
helper. **`0x495670` ignores both setter and Clear-helper return values.** A native
activation or helper return is therefore not evidence that viewport/Clear worked.
Use the proxy's actual completed HRESULT and actual call-time bindings/viewport.
The current camera record must already exist before this Clear; no late native
P/V reread is needed. Stencil-inclusive flags 7 or 6 do not satisfy the existing
selector's exact 3/2 pattern and must fail that profile, not be silently masked.

There is a second direct Clear-helper caller, **`0x4a5b02`** inside camera-list
walker `0x4a5ae0`, invoked at `0x500cc7`. It does not activate a camera first.
Its Clear cannot establish a new camera role; outside the ordinary activation
scope it must invalidate/refuse correlation. This finite extra edge is included
in the census rather than treating every call from `0x4db7f8` as activation.

Cube owns `*0x5aae58` (ID3DXRenderToEnvMap, produced by `0x4e611e`) and
`*0x5aae5c` (cube texture, CreateCubeTexture at `0x4e61aa`, with fallback calls
`0x4e620e/0x4e6277`). BeginCube is called at `0x4e8089`; each loop calls
**Face(face, D3DX_FILTER_NONE=1)** at `0x497820`, then **Clear(0,null,3,0,1,0)**
at `0x497842`, **then** publishes that face camera via `0x49784d`. End(1) is
`0x4e810f`; its error path attempts RT/depth restoration at `0x4e8171/0x4e818a`.
The Face and Clear results are not checked by this loop. Even the BeginCube
caller tests `EAX > 0` at `0x4977fe`, which does not reject a negative HRESULT.
Thus neither entering the cube function nor assuming helper success proves its
actual bound target. Track cube scope before its first device operation, revoke
main eligibility immediately, observe its actual device events, and require
successful restoration/known bindings before any later main correlation.

## 3. Compositor target flow and scene-end hook ABI

The compositor **`0x4e6770`** is a no-stack-argument procedure; it gets the device
from `*0x5aaf94 → +0x18 → [0]`. It defines its working registers internally,
returns with plain RET and has native FS:[0] exception setup. The sole direct
caller is `0x48b929`; no surveyed relocated pointer publishes the entry or seam.
The callsite is one five-byte relative CALL, with no interior incoming direct
edge or HIGHLOW operand. No prologue needs relocation when redirecting it.
The caller's ESI still names the next camera, EBX bounds the loop, EBP names its
SEH frame, and the continuation writes the done byte then reloads camera flags.
Preserve every GPR, EFLAGS, LastError and full FP/SIMD state around observation;
do not reuse the AP address table or presume its light signal helper preserves
all FL SIMD liveness. Forward the original exactly once. For pre/original/post
transport, retain the native return/error state even though this caller ignores
its scalar result. Keep per-invocation scope and foreign-SEH unwind cleanup;
ContinueExecution must not pop a live scope. Quiescent installation/removal,
exact bytes/ownership checks and full rollback are mandatory.

| Stage | FL evidence and actual arguments |
| --- | --- |
| Early admission | `0x4e6795` tests config+0xf0 bit0x80; device-record+0x80 and globals 0x5aae88/84/74/50 must be nonzero. Effect lookup names `bloom`, technique `DEFAULT`. Early return is possible without any image operation. |
| Save incoming surfaces | GetRenderTarget(0) at `0x4e6849`, output EBP+0x30; GetDepthStencilSurface at `0x4e6897`, output EBP+0x34. Device pointer saved at EBP+0x10. |
| Begin copy phase | SetDepthStencilSurface(null) at `0x4e68c3`. Three GetSurfaceLevel(0) calls `0x4e6af8/0x4e6b24/0x4e6b50` obtain scene/glow1/glow2 surfaces into EBP+0x2c/+0x28/+0x24. |
| Scratch preparation | ColorFill(surface,null,0) at `0x4e6b78/0x4e6b88/0x4e6b98`. |
| Scene source copy | `0x4e6c63` is StretchRect(**saved incoming RT0**, null, **scene-map level 0**, null, filter=2/LINEAR). Actual source is not automatically the swap-chain back buffer. HRESULT is checked with bounded retry/recovery. |
| Effect pass loop | Begin at `0x4e6cdc` uses flags 1/DONOTSAVESTATE; pass count comes from the effect. GetPass/GetAnnotationByName/GetString select `RenderColorTarget0`; names map to the three obtained surfaces. Missing annotation/failed string query selects saved incoming RT0 at `0x4e6d66`. |
| Target and quad | SetRenderTarget(0,target) at `0x4e6d72`; target-sized viewport helper at `0x4e6d7b`; DrawPrimitive(TRIANGLESTRIP,0,2) at `0x4e6d89`. These results are not checked by the loop. |
| Exit | Depth restored at `0x4e6e42`; retained surfaces released. RT0 and viewport are not unconditionally restored. Their final state depends on the executed effect targets and successful calls. |

The three slots are **scene map 0x5aae88, glow1 0x5aae84, glow2 0x5aae74**.
Creation requests A8R8G8B8 render-target textures at `0x4e638b/0x4e6414` and the
following matching creation, with scene dimensions from the display and integer
halves for glow. These requests establish intent, not actual successful resource
identities. Unknown target annotation is unsafe to classify: the search only
updates EBP+0x1c on a match (`0x4e6d51`); no-match uses its previous value at
`0x4e6d56`. Its first value was the effect pointer, so do not call no-match a
saved-RT fallback. The FL effect's pass count, compiled hashes and pass-state
semantics have **not** been established by this EXE study. Four AP bloom hash
pairs are not an FL acceptance profile merely because names match.

This seam is suitable for an admitted pre-compositor resolve independent of
stock glow execution. It does not authorize skipping/replacing the compositor
or assuming its outgoing state; those remain separate feature contracts.

## 4. Implementable role adapter and consumer read plan

Use the [existing complete-pass and native callback closure](material-submit.md#3-actual-borrow-readiness-writers-and-reentrancy),
including [load admission](ui-load-readiness.md),
[close deferral](ui-render-exclusion.md) and
[recovery/continuation rules](ui-render-mutation.md).
No new arbitrary-foreign-call audit is needed. Required mod-owned state is:

* A complete-pass token with observer/load/renderer incarnation, owner thread,
  nesting, phase (sorted/auxiliary/special/cube/compositor), and active device ID.
  Native origin wrappers distinguish `0x48ba43`, `0x48bbef`, `0x48bc87` and
  the full cube call `0x48b9f6`; cube scope begins before Face/Clear. Their ECX
  and stack-camera transport is explicit. `0x495670` and `0x4977c0` both return
  **RET 4**; a wrapper must reproduce that cleanup exactly once, unlike the
  no-argument compositor. These additional origin wrappers need their own
  install/SEH fixture qualification; this note's proposed patched seam census
  covers the compositor span, not an already-built origin adapter.
* The [owned canonical camera records](camera-buffers.md), keyed
  by actual camera/context and activation generation, plus separate latched
  background and scene copies. Publish the current per-pass record at the
  camera seam; retain its copied values after setter return for subsequent
  material draws. Nested records must not overwrite the outer record. Never
  retain native pointer dereference permission as part of a role token.
* Successful public device events: current actual RT0/depth IDs, viewport,
  additional MRT status, device Reset generation, and source-content epochs.
  Associate the camera's SetViewport event with its publication generation;
  associate the subsequent Clear with that **same ordinary activation scope**.
  A failed/missing SetViewport, foreign Clear caller, token mismatch, nested
  device ambiguity or changed generation invalidates correlation.

The ordinary origin before the barrier is only a **candidate**. At the first
eligible successful full Clear(flags=3, no rectangles, Z=1), require the actual
main pair: the existing selector's format/sample/full-viewport conditions plus
positive device/swap-chain identity (or the existing HDR redirect's verified
logical-main mapping). Public GetBackBuffer/resource identity can establish the
main surface; dimensions or COM addresses alone cannot. On acceptance, copy that
activation into `camera_background_`. After at least one successful background
draw on the same pair, the first eligible successful full Clear(flags=2) on
that pair accepts `camera_scene_`. It may be another camera or another activation
of the same camera. Never require pointer equality between background and scene.
Reject a third depth epoch, unsupported write, unknown binding or another camera
that changes the selected projection mid-scene without a qualified phase.

Feed the current strict [scene selector](../scene-boundary-selector.md) complete
application events; do not silently discard cube/offscreen events to make its
pattern match. Its current frame-wide behavior may reject a frame containing
cube work. A future partitioned main-stream adapter may begin only after a
positively classified offscreen prefix has completed and restored known main
bindings; it needs its own gap/alias tests before ignoring any events. Until
that exists, **refusal is the implementation policy**, not automatic relatching
from an arbitrary later Clear. This is a finite adapter extension if captures
show an offscreen prefix in required FL frames.

At compositor entry, require an unambiguous same-device pass token, the accepted
Scene state, matching current main RT/depth/viewport and camera generation, and
no previous boundary for that pass. Signal only that owner. The native device
loads in the compositor substantiate the relationship; avoid a new unleased
global-pointer walk inside a broadcast listener. Device identity should already
be bound by the admitted activation's observed native SetViewport/Clear calls.
Cross-check actual compositor copy source/destination later against that owner;
its disagreement invalidates history. Glow-off can have hook-only completion.
Without the qualified native hook, a fallback boundary requires the exact
successful depth-unbind/copy source contract; a generic same-size StretchRect is
not sufficient. The AP four-pass structural fallback remains disabled until its
FL shader/resource profile is separately qualified.

`MotionOutput::after_clear` currently calls `read_camera(false)` on initial
Background and `read_camera(true)` on Background→Scene. Its FL implementation
must consume the correlated **owned** record described above. `scene_bound()`
already requires the main pair/full viewport and no scene-end signal; extend
FL admission with matching origin/camera generation. TAA reprojection, shadow
candidate/depth/cascade construction, far-fade m00 and small-parts cull m00/m11
then consume the scene copy. A missing record clears validity for every consumer;
there is no fallback to global P/V, last frame, another device or the cube camera.
A small-parts threshold retained for the next frame must also be invalidated on
camera incarnation/projection change before it can affect that next camera.

Canonical P/V is distinct from the **actual current clip rows**. Existing
`apply_jitter` copies four shader-shadow rows at the reviewed profile register,
adds NDC jitter times row 3 to rows 0/1, uploads them for the draw and restores
the unjittered rows afterward. Preserve that source for FL current/previous
motion history and use actual submitted rows for private/orthographic depth laws;
activation P[10]/P[14] defaults do not replace per-draw terms. A role match alone
does not qualify FL shader constant registers or transformed variants. Required
FL pair/profile and depth-only prepass coverage must be verified before jitter:
an unjittered depth writer mixed with jittered geometry violates the shared-depth
contract. Keep canonical camera m20/m21 unmodified and apply the same jitter
only in the renderer's effective raster/reprojection inputs. Unknown projection
or shader coverage denies that draw/feature instead of editing global P.

## 5. Finite runtime evidence and acceptance

No production changes or runtime observer were installed. Static analysis cannot
supply actual D3D object lifetime IDs, successful viewport/target results, loaded
FL effect hashes or the gameplay camera list. The smallest useful **single
consolidated observer** records:

1. Origin/complete-pass/activation tokens, camera/context identity, layer/flags
   copied under the same admitted borrow, and canonical P/V hashes or values;
   cube face is explicit. No delayed camera pointer reads.
2. Ordered native SetViewport/Clear/RT/depth/copy events with actual HRESULT,
   actual binding lifetime IDs and descriptors, main swap-chain identity,
   Reset/content generations, full viewport and additional MRT absence.
3. Native compositor pre/post token; its source copy; glow setting outcome;
   every bloom pass's actual shader pair, input texture and target; scene draws'
   reviewed matrix-register windows and depth-writer/jitter coverage counts.

A user-controlled ordinary-flight capture must show one matching background
Clear/activation, one matching main-depth Clear/activation, scene submissions
with the scene token, then one same-owner compositor boundary before later
cameras. Include glow off, cube-enabled work if observed, a partial-view/menu
transition and Reset/recovery in the same planned diagnostic cycle where
available. Missing or failed paths remain denied; do not infer native Windows
behavior from a CrossOver capture. The observer needs no readbacks or per-draw
heap scans: fixed-size records, cached resource identities, bounded event capture
and constant-shadow rows suffice. Measure its cost separately from FPS.

Before enabling, synthetic fixtures must cover every refusal above, no-argument
compositor and RET-4 origin ABI, nested scopes/SEH, exact native forwarding,
rollback, wrong/two-device ownership, failures after intended RT/viewport writes,
and Reset/history invalidation. The static Clear interpreter/role examples here
are not those hook or GPU fixtures. No Wine/game execution, build, install,
production patch, commit or shader extraction occurred in this study.

Independent review accepted this static contract and its verifier with no
actionable findings: 14 regions, 1,866 instructions, 121 anchors, 30 edges,
10 mutation negatives, 128 Clear cases and 128 auxiliary cases. Runtime
correlation and adapter qualification remain outstanding as described above.
