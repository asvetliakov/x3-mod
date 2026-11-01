# FL UI publication exclusion and scene-destruction continuation

Offline study, 2026-10-28. Preferred FL EXE VAs; image SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Owning follow-up to [UI implementation](../../architecture/farnhams-legacy-ui-implementation.md),
[scene lifetime](ui-camera-lifetime.md),
[load readiness](ui-load-readiness.md), and
[concrete native callbacks](ui-render-exclusion.md).
No production edits or execution of game, Wine, native fixtures, build or install.

## 1. Can a healthy admitted render publish a HUD role before its next projection?

**The fixed native publication routes are excluded from an admitted render under
the existing close/load protocol and the typed recovery closure below.** The
reverse cones exclude ordinary update/load entry; the earlier window/audio/close
study closes those callbacks. This follow-up separately qualifies the previously
missing game allocation-recovery releases and texture demand-load frontier. The
conclusion relies on these actual registrations and API object provenance, not on
an absence of direct KC calls in `0x47b500`. Reader activation remains disabled.

| Publication | Native reverse cone and exclusion |
| --- | --- |
| New cockpit, `0x421b6c` | `0x421b60` has one direct caller, `0x4305be`, in KC native dispatcher `0x430540`. Its only absolute function-pointer publication is the VM native-command slot store at `0x41df2f` (operand at `0x41df32`). Jump table dispatch `0x430582` selects the create command. This is a synchronous KC command, not a renderer callback. |
| Restored cockpit, `0x41a673` | `0x41a400` has only save caller `0x42771d` and read caller `0x427868`. Read wrapper `0x427800` has sole caller `0x420e22` in cockpit load `0x420d30`, whose sole caller is `0x4798bf`. The existing complete load scope excludes native rendering throughout this branch. Save does not execute the publication store. |
| Target overlay, `0x4297d8` | `0x429780` has sole caller `0x42c299` in `0x42c240`; its sole caller is `0x42c386` in `0x42c330`; its sole caller is `0x4229c2` in cockpit update `0x421f50`; its sole caller is `0x41e5b0` in registry update `0x41e550`. The latter has sole caller `0x4782ca`, immediately **before** render call `0x4782cf`. At `0x4229bc`, ECX is cockpit+0x3bc, explaining the embedded overlay owner. |

No absolute DWORD target literals were found for the seven private constructors,
reader/update helpers checked by the verifier. The one positive native-dispatcher
literal is retained and typed, not discarded as an inconvenient indirect edge.
These are measured direct/literal censuses, not a proof against computed private
calls by another module.

The actual callback closure is the existing render-exclusion contract: window
input/activation and registered audio results publish state/ready tasks without
running KC; next-location changes are consumed only after the outer loop returns;
SC_CLOSE's synchronous NotifyLeave is deferred while native render/load depths
are nonzero; the qualified scene+0x54 construction/restoration flows leave its
callback null. Therefore those callbacks cannot execute the new-cockpit command,
run cockpit update again, or reach cockpit restore inside this pass. The game
allocation-recovery callback `0x47b500` needs the separate closure below; its body
having no direct KC call would not by itself establish this result. Deliberate foreign private
calls or installation of a new scene callback are outside this native profile.
No extra arbitrary foreign-call audit is a prerequisite to this conclusion.

The end-of-render playback path discussed below creates/restores ordinary native
renderer objects, but has no edge into any of these HUD ownership publications.
It runs after this invocation's last projection in any case. A scene's creation
or renderer-handle insertion alone does not publish a HUD role.

### Allocation recovery: actual objects, callbacks and finite closure

`0x47b500` calls engine reclamation `0x485c00` at `0x47b506`, then texture
reclamation `0x512410` at `0x47b50b`. Reuse the separately owned
[registry value-lifetime study](registry-value-lifetime.md) for
`0x485c00 → 0x4993b0`: usage marking and resource removal are distinct from
scene/camera removal. Its remaining indirect-release leaves are qualified here.
The guard `0x62de10` suppresses recursive allocation recovery; it is not a
window-message or general render lock.

| Receiver and provenance | Operation on the recovery path; callback consequence |
| --- | --- |
| Attachment+0x18 points to a 0x10-byte GPU-buffer record. `0x4e0792` publishes that record; `0x4e0799` loads it and `0x4e07fd` passes its first DWORD as output to the device's `CreateVertexBuffer` (+0x68). | `0x485a90 → 0x485a20 → 0x4e0eb0`; `0x4e0ec8` is the resulting **IDirect3DVertexBuffer9::Release**, followed by raw backing/record frees. It is not a scene-owned virtual destructor. |
| Mesh creation `0x4dbbdb` calls imported **D3DXCreateMesh**, output `[EBP-0x18]`. Addresses of material+0xc/+0x10 are saved at `0x4dba92/0x4dba9e`; mesh `GetIndexBuffer` (+0x38) and `GetVertexBuffer` (+0x34) fill those fields at `0x4dbc3f/0x4dbc4d`. Mesh itself is stored at +0x14 by `0x4dbc5b`. | `0x4de050 → 0x4dd180`: the +8 calls release typed vertex/index buffers and the mesh. The branch without an owning mesh releases the same buffer slots directly and updates byte accounting. No application callback argument is passed to these resource acquisitions/releases. |
| Effect records returned by `0x4db5b0` contain D3DX effect pointers at `+4 + variant*4`; `0x4db2c0` writes those slots through the output argument of imported **D3DXCreateEffect** at `0x4db49a`. Include and defines arguments are zero (`0x4db48c/0x4db48e`); state-manager registration follows at `0x4db53d` (+0x11c). | `0x4da490` keys map `0x61c890` by effect pointer and parameter handle, copying them into node+0x10/+0x14 (`0x4e9c1e/0x4e9c23`). `0x4da5fb` and `0x4da738` therefore call **ID3DXEffect::SetTexture(handle, NULL)** (+0xd0). Remaining map operations traverse/free scalar records; they do not execute KC. |
| Renderer obtains effect-array+4 at `0x4e1857`, saves it at EBP-0x18, and pairs **EndParameterBlock** (+0x128) results with that effect pointer: token/owner at material+0x34/+0x38 (`0x4e1d29/0x4e1d33`) and +0x2c/+0x30 (`0x4e38c6/0x4e38cd`). | `0x4dd33f/0x4dd363` (+0x130) are **DeleteParameterBlock** on the creating effect, not arbitrary callbacks or an effect pass. These calls release captured parameter storage/resources. |
| Display cache `*0x5aaf94`, device record+0x18, device at record[0]; already qualified by device creation. Texture helper `0x4f5710` creates a texture at `0x4f57ee/0x4f583e` (+0x5c), extracts level-zero surface at `0x4f588e` (+0x48), or creates an offscreen surface at `0x4f591c` (+0x90). Surface/texture are retained at texture-object+0x30/+0x34; +0x34 store is `0x4f59c7`. File-backed paths below supply the same interface types. | `0x510c10 → 0x4f59e0`: +0x38 at `0x4f5a39` is **IDirect3DSurface9::UnlockRect**; +8 at `0x4f5a5b/0x4f5a75` releases the surface/texture. `0x502e10`, `0x510800 → 0x5103b0` and `0x50b780` free CPU arrays/name storage. |

The effect operation identities agree with the documented
[ID3DXEffect interface](https://learn.microsoft.com/en-us/windows/win32/direct3d9/id3dxeffect).
The verifier independently derives the relevant x86 slot indices from local
MinGW `d3d9.h`, `d3dx9effect.h` and `d3dx9mesh.h`, recording their hashes. Method
identity follows the creation/output chain **and** signature, not offset alone.
No fixed DLL implementation address or private backend layout is required.

#### The display+0x1c receiver is a native state manager

`0x4f361a → 0x4d6260` receives the actual device and stores the returned wrapper
at display+0x1c (`0x4f361f`). The factory chooses a small base wrapper or cached
wrapper using device creation flags. Base constructor stores vtable `0x58f320`
at `0x4d630a` and device at wrapper+4 (`0x4d6310`); cached constructor
`0x4d5990` installs final vtable `0x58f2b0` at `0x4d59da`.

| Slot | Base / cached target | Actual behavior |
| --- | --- | --- |
| +0x58, ECX=this, no stack arguments | `0x4d55b0` / `0x4d5a90` | Clear device texture slots 0..9 via `IDirect3DDevice9::SetTexture` (+0x104). Cached variant additionally destroys its scalar state-map nodes and clears caches; direct calls are `0x4d65c0`, `0x412af0`, `0x521920`. |
| +0x1c, explicit stack this/state/value | `0x4d5690` / `0x4d5ca0` | Forward `SetRenderState` to wrapper+4 device (+0xe4), with optional scalar-map comparison/update. Map insertion uses normal allocator/error machinery, not a KC callback. |
| +0x20, explicit stack this/stage/texture | `0x4d56f0` / `0x4d5e70` | Forward/cache `SetTexture` to wrapper+4 device (+0x104); cached target contains one device call and no other call. |

Thus `0x4d9c2a` (+0x58) really is a **native thiscall extension**, while subsequent
+0x1c calls have COM-style explicit-this stack arguments. Treating both as device
methods would be an ABI/type error. This same typed wrapper is the effect's
registered state manager. Its relevant texture/state callbacks perform device
state operations and cache bookkeeping, not interpreter execution.

#### Mesh cleanup can demand-load a texture; this is not a free-only cone

`0x4dd219 → 0x512490 → 0x5113b0` is reachable when a referenced table texture
slot is absent. The exact demand-loader body is **`0x5113b0..0x5116b9`**.
`0x5116c0` begins a separate metadata parser and is not a continuation of this
function. A coarse byte-range scan extending into that parser gives a false
frontier. Likewise procedural constructor `0x510c90` ends at `0x510dec`.

The demand-loader has only these direct callees: scalar map lookup `0x482200`,
query `0x4db710`, procedural constructor `0x510c90`, string helpers
`0x50b670/0x50b780/0x50b890`, and texture dispatcher `0x510890`. No callback
pointer or script object is an argument. Query `0x4db710` follows display+0x18
then record[0], supplies a local **0x130-byte D3DCAPS9**, and calls device
**GetDeviceCaps (+0x1c)** at `0x4db73c`; it tests DeviceType==1 at `0x4db741`.
This +0x1c receiver differs from the state-manager receiver above. It is not a
script callback and is not GetDisplayMode (+0x20).

The procedural branch allocates a texture record, creates its D3D resources via
`0x4f5710`, and allocates CPU pixel buffers via `0x510470/0x5102c0`; failures use
`0x510c10`. It installs no user callback. The file branch follows the already
qualified [texture dispatch/resource selection](resource-callers.md):
DDS `0x4f52a0..0x4f5700`, JPG `0x4f6040..0x4f657f`, and TGA
`0x4f75f0..0x4f79a1`. The latter's local type switch is a fixed jump table at
`0x4f79a4`, not a callback. These readers pass byte buffers and dimensions to
D3DX image/texture/cube-texture/surface APIs (`0x550370/374/378/37c`) and copy
surfaces through `0x4f5c70`; their API argument lists have no application callback.
Returned texture/surface interfaces supply the virtual operations. No native
movie/media callback is registered on this path; the routines after the exact
JPG return boundary belong to other callers.

Their register-indirect EAX calls are the known recovery callback loaded from
`0x5aae24` under `0x62de10`; the verifier checks the concrete load/guard/test
pattern at **12** calls across image readers, surface transfer and effect
creation. During existing recovery the guard suppresses another invocation.
Ordinary allocation failure likewise uses the existing allocator contract;
default CRT new-mode is zero as established by the startup study. There is no
new native completion/interpreter callback to register or follow here.

Payload reader `0x505f40` has real indirect register calls too: EDX at
`0x5061c4/0x506228` is imported **gzgetc** (`0x5503f4`); EBP at `0x5062e6` is
imported **inflate** (`0x550400`). Its inflate initialization zeros zalloc,
zfree and opaque at `0x506263/267/26b` before `inflateInit2_` (`0x506277`).
Other compressed file calls resolve to gzread/gzseek/gzclose/inflateEnd; the
reader supplies no game allocator or script callback to zlib. Native open/mount
selection, string formatting and scalar conversion reuse the resource-resolver
contract, not the separate KC task interpreter.

This closes the **actual native callback frontier**, not an assertion that an
arbitrary replacement DLL cannot call game addresses. D3D/D3DX/zlib operations
may enter OS/runtime code; owner-window delivery is still governed by the
already-qualified input/audio/deferred-close protocol. No active render reaches
HUD publication or scene destruction through this recovery registration. Resource
and GPU backing objects **can** be reclaimed: the no-KC conclusion is not a
memory lease for callers retaining those objects. Native memory corruption,
foreign vtable replacement, a foreign private-address call, or an externally
installed callback is outside the fixed healthy-native profile.

### Concrete implementation consequence

For these fixed native routes, role-table allocation failure occurs outside an
admitted render (or inside a load which already blocks rendering). Retain the
committed coordinate generation, latch incomplete/quarantined, and deny the next
complete pass at `0x48b6d0`. Finishing the owner+4 native store before recording a
failed role does **not** leave a reachable current-pass projection in these flows.
The typed recovery closure excludes the specific active-pass OOM publication
counterexample for this profile; `state.quarantine()` is not an in-progress draw gate.

Keep a publication-during-render condition as an explicit unsupported-admission
witness in diagnostics/fixtures, rather than silently treating it as supported
quarantine or terminating merely because depth is nonzero. If a future supported
callback adds that route, it must supply pre-mutation exclusion at that callback
or a new continuation contract. The present proof does not authorize an arbitrary
mid-pass jump out of `0x48b6d0`.

In particular `0x48bdf2` is **not** a general abort landing pad: earlier native
frames can hold traversal state and the second camera pass temporarily replaces
camera+0x270 at `0x48bbe0`, restoring it at `0x48bc42`. Jumping from publication
or projection into that epilogue neither unwinds those frames nor restores their
state. Projection EAX=0 remains an ineffective draw gate. A mod-induced route
cannot be repaired by changing the scale factor or skipping only projection.

The existing publication spans/ABIs remain unchanged: `0x421b6c..71` and
`0x4297d8..dd` store EDI through ESI+4 then set flags with TEST; `0x41a673..78`
stores EAX through EDI+4 and sets ECX=ESI with an outgoing word already on stack.
Preserve full incoming GPR/FP/SIMD/LastError state around bookkeeping, replay once,
and preserve the native TEST result. There is no new per-projection work.

## 2. What does scene-destruction unwind mean for an enclosing render?

### Borrow scope is wider than the current scene, shorter than render depth

Render `0x48b6d0..0x48be30` copies active camera pointers into a temporary array
at `0x48b8a3`, consumes them at `0x48b91b`, and consumes the same array again at
`0x48bbc0`. It also retains scene/node iterators across native calls. The current
camera's attached scene is reread after deferred work at `0x48ba92`; late scene
iteration follows scene links at `0x48bde0`. Those objects are borrowed without
pinning. A scene different from the *currently drawing* scene can still own a
camera needed by a later array entry. HUD role membership is also unrelated to
native borrow membership: an untagged ordinary scene can be borrowed.

There is nevertheless an actual, ordinary scene destructor **inside render**:

| Edge | Meaning |
| --- | --- |
| `0x48bded → 0x48f570` | Render-end stream playback, after camera drawing and the final scene iteration. |
| `0x491778 → 0x48f280` | End-of-stream branch tears down playback-owned resources. |
| `0x48f4c9 → 0x4a5460` | Playback cleanup resolves its scene handle in engine+0x10 and destroys that scene. ECX comes from map-entry+8 at `0x48f4c2`. |
| Return to `0x48bdf2..0x48be30` | Only array-size/storage cleanup, accounting, saved-register/SEH restoration and RET remain. The sole call is raw array free at `0x48be0c`; no camera-array element, camera, scene or node is dereferenced. |

This route is a concrete counterexample to “destruction with render depth means
live renderer borrows.” Scene/camera addresses may still be stored in the array,
but its pointed objects are no longer used. Its raw allocation remains a cleanup
obligation. Do not drain deferred closes early at this tail: full native depth
still protects playback's own locals and its nested cleanup.

`0x4a5460` takes ECX=scene, no outgoing arguments, saves ECX/ESI/EDI and returns
plainly. It deletes children at `0x4a5472`, detaches cameras at `0x4a54e9` and
destroys them at `0x4a54f0`, then ultimately frees the scene at `0x4a556a`.
An exception can therefore leave partially mutated native state before scene
storage is freed. Retiring metadata is no lease and must never be rolled back.

### Native exception continuation is finite and inspectable

The native render SEH thunk `0x54e1e8` passes FuncInfo `0x59dcac` to
`0x521368`. Its metadata has magic `0x19930522`, maxState=1, unwind map
`0x59dcd0`, **nTryBlocks=0 and null try-block map**. The sole unwind entry is
`{-1, 0x54e1e0}`; that thunk destroys the temporary string at EBP-0x38 through
`0x417b60`. This is a cleanup record, **not a catch that resumes rendering**.
`0x48f570..0x491783`, `0x48f280..0x48f561` and `0x4a5460` install no native
FS:[0] handler. Once unwind crosses the destructor wrapper on this native tail
route, there is no native handler between it and the render wrapper which can
catch and continue the render body. Exceptions handled *inside* a callee before
crossing the wrapper do not constitute destructor-scope unwind.

The metadata does not arrange cleanup of the camera-array allocation on abnormal
exit. Nor does it complete the interrupted playback destruction. Those are
native abnormal-exit limitations; quarantine/restart must not be represented as
transactional native recovery, and mod cleanup must not walk damaged objects.

The current fixture's mode 10 enters destruction from the synthetic render body,
then raises the mode-3 exception. `catch_outer` installed its handler **before**
invoking render; its `RtlUnwind(f, ...)` target is outside both wrappers. Destructor
cleanup sees render depth nonzero only because unwind has not reached the outer
render scope yet. Killing there prevents an otherwise ordinary outward unwind.
It is not evidence of an in-render catch or a live borrowed scene (the synthetic
body supplies no real scene/camera borrow).

### Implementable cleanup policy

For this qualified native profile, use **retirement plus latched quarantine on
destructor unwind, then propagate the original exception**. Retire only the
scope's scene incarnation, finish that scope exactly once, keep other render
scopes/depths intact, and let each render wrapper release itself when unwind
actually reaches it. Preserve the exception and native handler state, including
FP/SIMD/GPR/LastError, return ContinueSearch, and never drain pending closes from
exception cleanup. Search/ContinueExecution is not unwind and must release
nothing. Keep restart required; normal later scope completion cannot clear it.

This replaces the provisional depth-only fail-stop with a qualified native
continuation rule, not a blanket “all destructor exceptions are recoverable.”
The distinctions an implementation/fixture must preserve are:

| Destroyed scene / continuation | Required result |
| --- | --- |
| Unrelated to every still-live native borrow; handler continues inside render | Retire/quarantine, preserve the unaffected render scopes. Nonzero depth alone supplies no reason to kill. No native positive route is asserted here. |
| Previously borrowed but render has reached its tail at `0x48bded` | No remaining scene/camera read in this render; ordinary destruction is valid. Outward unwind retains quarantine and releases scopes in order. |
| Borrowed scene; unwind target outside all renders which borrowed it | Propagate cleanup; no stale render resumes. The synthetic outer-catch fixture exercises this continuation shape. |
| Borrowed scene; catch would resume a frame that will use it again | Unsafe. Prevent destruction before mutation at the concrete originating callback (the qualified SC_CLOSE deferral does this). Neither retiring roles nor an exception-cleanup kill chosen from depth repairs it. The completed callback and typed recovery frontiers establish no such returning native scene-destruction route during live scene consumption. |
| Exception search resumes inside the destructor | Keep retirement active and render depth unchanged; native code still owns the scope. |

The x86 exception handler's `EXCEPTION_UNWINDING` bit does **not** reveal the final
catch/continuation. The fault CONTEXT is not an RtlUnwind target-frame oracle.
Do not infer “inside” from depth or “outside” from the flag. The native tail route
can rely on the qualified call/handler contract above; recovery releases the
resource/texture objects below and supplies no additional scene-destructor route; supporting foreign
interposed catches which violate it would require their actual continuation and
borrow evidence, not guessing from current HUD identity.

If explicit phase instrumentation is desired, the exact optional boundary is
**`0x48bded..0x48bdf2`**, a single relative call to `0x48f570`, with no stack
arguments or result consumed. Mark the current linked render scope's scene-
consumption phase finished before forwarding that call, but retain its render
depth until normal return/unwind. Preserve incoming state around marking and
actual outgoing state afterward. The full-text census finds no direct interior
edge and no HIGHLOW operand; the five bytes cross an aligned qword (offset 5), so
quiet group installation/rollback is required. This is a phase marker, **not an
abort**, a permission to drain closes, or proof that a nested outer scope has
finished borrowing. It is unnecessary for the fixed native no-catch policy but
provides an exact seam if the parent chooses explicit runtime phase diagnostics.

Required affected fixture changes are concrete: turn the outer-catch destructor
case into a successful outward-unwind/quarantine check; separately cover an
unrelated scene with an inner catch, a tail-phase scene with cleanup, retained
search scopes, nested outer/inner render release ordering, and the distinction
between an ordinary scene and an unborrowed scene. An actual borrowed-scene inner
catch is an unsafe-policy negative, not a justification to label the existing
outer-catch test fatal. These native wrapper fixtures were not run here.

## Evidence and limits

[verify_fl_ui_render_mutation.py](../../../verification/probe/verify_fl_ui_render_mutation.py)
passes **125 instruction anchors, two EH structures, 37 bounded regions / 7,258
instructions, 428,814 full-text instructions, six semantic mutations and seven
sequential continuation-policy cases** (measured). Mutation checks bypass the
hash gate and reject a render catch-table count, changed tail target, changed
VM registration, changed state-manager target, changed buffer-creation slot and
changed recovery callback provenance. Eight exact direct-call sets, six native
vtable slots and 12 recovery-indirect emitter patterns are checked; SDK interface
slots are independently derived and header hashes retained. The policy cases specify the distinction; they do not execute
SEH or certify foreign catch behavior. The finite reverse cones, existing window/audio callback publications and typed
recovery closure establish the supported-native no-route result. They are not a
whole-program indirect-call theorem or a guarantee for foreign private calls.

```sh
python3 verification/probe/verify_fl_ui_render_mutation.py --raw
python3 -m py_compile verification/probe/verify_fl_ui_render_mutation.py
```

Ignored raw listings, JSON summary and producing command are under
`verification/results/fl-port/ui-render-mutation/`. Reader activation remains
disabled. Production cleanup changes, affected x86 fixtures and native Windows/
CrossOver execution remain implementation acceptance, not results of this study.

Independent deep review (2026-10-28) reran the verifier and checked creation/output
provenance, native state-manager calling conventions, exact loader boundaries
and recovery callback registrations. No actionable findings remained within the
fixed native API/registration contract. This review does not qualify resource
leases, foreign reentry or production wrapper execution.
