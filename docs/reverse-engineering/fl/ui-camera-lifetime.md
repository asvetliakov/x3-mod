# FL UI projection: cockpit HUD-scene lifetime

Offline contract study, 2026-10-27. Owning decisions remain with the
[FL port](../../architecture/farnhams-legacy-port.md). Extends
[UI input](ui-input.md) and reuses the reviewed
[object lifetime study](object-lifetimes.md), particularly its
registry, allocation recovery and foreign-unwind limits. No production changes,
game/Wine execution, builds, installs or commits. All addresses are preferred
VAs in the fixed FL EXE; this is not an admitted hook implementation.

## Result: classify the borrowed camera's scene

The smallest established role boundary is **the cockpit HUD scene**, not the
active cockpit's cached camera pointer. Track scene identities when native code
assigns them to cockpit `+4`, retire them before scene destruction, and at the
existing pixel-projection seam compare **`[EBX+0x1c]`**, the current borrowed
camera's attached scene, against this mod-owned role set. The proposed lifetime
profile needs **three lifecycle sites** in addition to the existing projection
site. It needs neither the connected renderer/object observer nor a cockpit or
engine registry walk on each draw.

This excludes all `0x200` instances in every owned cockpit HUD scene. It does
not affect the perspective branch or mark sector/dust/background scenes as HUD
merely because a cockpit references their cameras. It is deliberately broader
than an active-cockpit camera equality test: simultaneous cockpit HUD scenes,
and additional cameras rendering the same HUD scene, receive the same real-pixel
policy. This follows the ownership/rendering evidence below; visual acceptance
for multiple monitors remains outstanding. It does not promise to classify a
new, independently allocated script scene as native HUD merely because someone
copies native HUD nodes into it.

## 1. Publication, selector and aliases

| Flow | Concrete evidence and ordering |
| --- | --- |
| New cockpit | `0x4305a6 → 0x420ec0` constructs the 0x8c0-byte owner. `0x4305be → 0x421b60` creates its HUD scene/camera. Only afterwards `0x4305cd → 0x50cfe0` publishes the owner in the cockpit registry, then `0x4305dc` stores its handle at owner+0. |
| New HUD scene | `0x421b65 → 0x4a4a30`; EDI=result; `0x421b6c` writes owner+4. Camera allocation/store follows at `0x421b91/0x421b96`; attach is `0x421bc1 → 0x4a5860`. |
| Restored scene | Renderer load `0x47984c → 0x493510` creates saved scenes first: restored handle at `0x4937fb`, engine+0x10 insertion at `0x493837`, scene-list publication at `0x49385d`. Cockpit load is later, `0x4798bf → 0x420d30`. |
| Restored owner | `0x420e22 → 0x427800 → 0x41a400` (call `0x427868`) resolves the saved scene handle through engine+0x10 and writes EAX to owner+4 at **0x41a673**. It resolves/stores camera+8 separately at `0x41a6b5/0x41a6ba`. Owner insertion follows successful loading at `0x420e33 → 0x50cf30`. |
| Current cockpit selector | Registry constructor zeroes +0x10 at `0x41dff0`; registry restoration writes it at `0x41dda8`; native setter stores a validated handle or zero at `0x431fd0`. These selector changes do not create/destroy HUD scenes. |
| Exposed aliases | `INS_CockpitGetCamera` returns the camera's render handle at `0x430645`; `INS_CockpitGetScene` returns scene+0x14 at `0x43068a`. Native visibility code attaches the owned camera at `0x4306d2`. Generic scene/camera APIs can therefore reach these objects independently of owner teardown. |

The restored owner+4 seam is on the read branch of `0x41a400`; its two surveyed
direct callers are `0x42771d` (save) and `0x427868` (restore). It does not fire on
the save branch. New initialization and restored pointer fixup both need
coverage; observing only `0x421b60` misses loaded games. Successful renderer
subsection completion precedes cockpit role restoration and is not a suitable
point to assume the role set is complete for the loaded UI.

**No-draw ordering across restoration is not proved.** The established sequence
is renderer-load call `0x47984c`, scene-list publication inside that call at
`0x49385d`, later cockpit-load call `0x4798bf`, then each restored cockpit's
role binding at `0x41a673`. It establishes publication before role binding,
not exclusion of a draw between them. Reentrant rendering from load callbacks,
loading-screen work and foreign-thread rendering have not been excluded over
that interval. A restored native HUD scene can therefore be visible to a scene
lookup before its role is known; an absent tag cannot yet mean ordinary UI.

A qualified **load/readiness barrier is an additional activation requirement**,
including subsequent loads while scaling is already active. Early installation
and observing every binding do not alone close this temporary completeness gap.
The minimal next proof is the load caller's admission boundary: establish when
scene-consuming render/projection calls are excluded, including nested callbacks,
and when all cockpit role fixups have completed before admission resumes; cover
failure/unwind exits as well as successful return. Alternatively qualify an
explicit read barrier over that interval. Neither barrier is established here,
and choosing one must respect the coherent coordinate-group boundary in section
6 rather than disabling projection alone. No whole-engine lifetime reconstruction
is needed to state or test this specific ordering obligation.

Attachment itself writes `camera+0x1c=scene` at `0x4a5897`; detachment clears it
at `0x4a5927`. Scene destruction also clears it directly at `0x4a54e9` before
calling the camera destructor. Reading the **current** attached scene avoids
tracking these attachment writers or retaining a stale owner-camera pair.
Reattaching an existing camera to an ordinary scene removes the scene-based
exemption naturally; reattaching it to a tagged HUD scene restores it. A cloned
or secondary camera attached to the same HUD scene is covered without a new
camera birth hook. A distinct scene clone needs its own cockpit+4 binding to
receive this role; names, copied flags and copied handles are not evidence.

Two tempting shortcuts fail concrete negative checks: `B3D_SceneSetName` changes
scene+0x28 at `0x4b1366`, so the allocator's "Cockpit Scene" label is mutable;
`B3D_CameraSetFlags` replaces camera+0x270 at `0x4b117d`, and the renderer itself
temporarily clears bits 0x30 at `0x48bbdc/0x48bbe0`. Neither is an immutable
ownership marker. The scene's default scale 0.01 is shared by the allocator and
does not identify a cockpit either.

## 2. Scene destruction, address reuse and failure ordering

Retire the role at entry **0x4a5460**, before forwarding any native work. This
function removes child nodes through `0x4a3300` (`0x4a5472`), removes and destroys
attached cameras through `0x4a4750` (`0x4a54f0`), invokes a scene-owned virtual
callback (`0x4a552f`), removes the scene handle from engine+0x10 (`0x4a5544`),
unlinks the engine scene list, releases the name, and finally frees the 0x58-byte
scene at `0x4a556a`. The callback and nested child/camera destructor callbacks
occur **before** final scene free; retirement at the registry-remove call would
be too late to describe the entire destructive interval.

The normal cockpit teardown reaches this same destructor at `0x421997`, after
detaching/freeing its +8 camera. Generic script scene free reaches it at
`0x4b0170`; engine scene cleanup reaches it at `0x48a847/0x48a863`. The complete
linear census records all seven direct callers in the probe result. There is no
need to duplicate camera retirement coverage from the renderer observer: camera
address reuse cannot affect a scene-keyed set.

For scene-address ABA, remove the active scene role **before** any destructor
callback/free and keep a scoped retiring identity until the original returns or
unwinds. A callback observing an otherwise native-valid borrowed dying scene
must still get the native-HUD exemption; it must not become an ordinary scalable
scene halfway through destruction. This retiring state is not a memory lease.
Do not publish a new HUD role for that same scene address while its destructor
is active: this can be a reentrant saved-handle lookup before handle removal,
not a new allocation. Reject that event as loss of tracking completeness.
After normal destruction finishes, a different scene reusing the same address
has no role until its own observed cockpit binding. Returning from free must
never re-add the old identity. A destructor wrapper keeps only the address
value for exit bookkeeping and never reads the freed scene again.

Failure ordering does not justify rolling back an observed role:

- Failed normal scene allocation frees its partial scene at `0x4a4b37` and
  returns zero at `0x4a4b4d`, before the owner+4 binding hook. Ignore a zero
  binding. If camera allocation fails after a nonzero scene binding, the scene
  still exists and retains its native HUD provenance.
- A later constructor helper failure or owner restore failure/unwind does not
  prove that the bound scene was freed. The latter already belonged to the
  renderer's restored scene registry before owner decoding. Keep its role until
  actual scene destruction; owner insertion is not the scene's lifetime start.
- The selected renderer loader has **zero direct CRT-free calls**; early failure
  returns with partial engine state rather than locally freeing the just-restored
  scene. This negative does not exclude cleanup in callees or later error paths.
  Known normal cleanup routes use the scene destructor above.
- A foreign unwind through scene destruction leaves partially mutated native
  state. Retire the role permanently, invalidate completeness, and finish the
  retiring-scope bookkeeping through a qualified unwind path. Do not infer that
  normal wrapper return or a successful later allocation repaired it.

The existing lifetime study already qualifies ordinary map layouts and explains
why registry checks are not memory leases. No such lookup is added here. Unknown
indirect/raw-free aliases remain outside the bounded static proof: scene allocator
failure and scene destructor final free are the established normal free sites.
The recommendation does not convert a linear xref census into proof against
arbitrary mods that free engine storage through other means.

## 3. Renderer borrowing and the projection read

The ordinary loop updates cockpits at `0x4782ca → 0x41e550`, then renders at
`0x4782cf → 0x48b6d0`. Rendering iterates **all active engine scenes**, not the
current-cockpit selector: scene activity is tested at `0x48b883`, its camera
list read at `0x48b889`, and admitted raw camera pointers are copied into an
array at `0x48b8a3`. Sorted camera iteration reads that array at `0x48b91b`.
The ordinary scene draw uses camera+0x1c at `0x48ba6a`, calls scene traversal
`0x4978e0` at `0x48ba7f`, then the deferred traversal `0x497680` at `0x48ba8d`.
Node traversal calls `0x4968c0`, whose projection call is `0x496fb9`; the deferred
projection call is `0x4976a0`. Both pass the current borrowed camera explicitly.

This is **borrowing, not pinning**. No reference-count increment or lifetime
lease is established by the camera array or these calls. D3D calls and allocation
recovery elsewhere in the frame can invoke code; the frame-level pointer array
does not make a once-per-Present owner-camera snapshot safe.

Inside the selected pixel projection interval `0x4de570..0x4de920`, there are
**zero call instructions**. Entry copies camera to EBX at `0x4de57c` and already
reads its scene at `0x4de583`. At scale seam `0x4de91b`, EBX remains that camera,
EDI is the instance and ESI is P. A single additional DWORD read `[EBX+0x1c]`
uses the same currently borrowed object; lookup then touches mod-owned memory
only. Do not dereference any retained scene/owner/camera pointer from a prior
frame. The native-valid-current-camera contract is inherited from the original
projection call, not supplied by this role tracker.

**Do not read scene from `[ESP+0xc]` at the seam.** The early scene spill at
`0x4de586` is overwritten with the display-record pointer at `0x4de7d1`.
This is a verified stack-slot alias, not an optional liveness assumption.

Known normal update/render ordering supports a same-thread admission model, but
does not prove all engine writer threads are confined. Qualify the publication,
destruction and rendering thread relationship before activation, or provide
equivalent exclusion for the complete lifecycle bookkeeping interval. A private
table lock alone neither pins engine objects nor prevents a delayed foreign
publisher from adding a role after retirement. A detected foreign-writer event
must invalidate completeness; it is not an ordinary unknown scene. Do not hold
the role-set lock while forwarding native calls or callbacks.

## 4. Separate native scenes: target overlay and tooltip

The three lifecycle sites above cover cockpit+4 scenes. Native target overlays
also own a separate scene, while tooltips are ordinary scalable UI. A scene
being created by native cockpit code is therefore insufficient to assign the
real-pixel HUD exemption.

Target-overlay constructor `0x429780` creates its scene at `0x4297d1`, stores
it in overlay-record+4 at **0x4297d8**, allocates its camera at `0x429800` and
attaches it at `0x429826`. Its only surveyed direct constructor caller is
`0x42c299`; the resulting record is stored at cockpit+0x7a4 by `0x42c2a9`
(EDI=cockpit+0x3bc, store displacement 0x3e8). The record retains its cockpit
at +0. Restoration destroys the old record through `0x42787e → 0x429870`
and clears cockpit+0x7a4; it is rebuilt lazily. Destruction already reaches the
common scene destructor at `0x4298ff`.

This target scene belongs to the native target/pick coordinate domain: the
[input study](ui-input.md) traces its camera in native hit testing
at `0x42bae0`, using real display dimensions. Its updater mirrors cockpit-camera
projection data at `0x429aa6..0x429b3b`. That coordinate ownership is separate
from the question whether its current instances enter pixel projection: the
updater `0x429a40..0x42b64f` has **zero direct Flags2 (+0x130) operands**;
ordinary node allocation initializes Flags2=0 at `0x4a22d6`, and the inspected
body setter adds 0x20, not 0x200. Root/child/body flags have not been exhaustively
traced. An exemption is inert for perspective-only nodes, so no observed 0x200
draw is claimed or required merely to assign this native scene its role.

For a full native target-scene exemption, add its binding seam
**0x4297d8..0x4297dd** (5 bytes, store 3 + test 2): ESI=overlay record,
EDI=scene or zero. Replay the store and `test edi,edi`, preserve the same full
CPU/FP/LastError contract as new cockpit binding below. It has no HIGHLOW
operand, fits one aligned qword and has no surveyed direct interior edge.
Reuse the common retirement hook; no extra camera lifetime machinery is needed.
This is one additional publication site beyond the three-site cockpit+4 core.

Tooltip scene creation/store is `0x42fff5/0x42fffc`; the per-cockpit tooltip
object lives at +0x894. Its node gets Flags2 `0x5600` (including 0x200) at
`0x430133`. KC ShowTooltipAtCursorWithDelay supplies cursor-derived coordinates;
native dispatch `0x4322d1 → 0x42fe00` passes them to the tooltip object. Keep
this scene in the ordinary scalable domain. Blanket exemption of native-created
scenes would misclassify it.

There is a concrete additional UI-port requirement: tooltip edge clamping in
`0x42fe00` reads **real display width at 0x42ff32** (`movsx esi,word [edx+4]`)
and **height at 0x42ff58** (`movsx ecx,word [edx+6]`). It compares these against
incoming x/y plus tooltip extent and writes clamped layout coordinates at
`0x42ff6c/0x42ff75/0x42ff87/0x42ff8f`; drawing stores node x/y at
`0x4301a6/0x4301ae`. With a virtual KC cursor and scaled projection those bounds
also need virtual dimensions. These are future adaptation sites, not admitted
five-byte detours: full displaced-span/liveness qualification is outside this
scene-lifetime task. This finding does not require a tooltip lifetime exemption.

## 5. Minimal hook/read contract and cost

| Purpose; exact span [start,end) | Inputs, replay and preservation |
| --- | --- |
| New scene binding `0x421b6c..0x421b71` (5; 3+2) | ESI=owner, EDI=scene or zero. Replay owner+4 store, publish nonzero scene identity before more native calls, replay `test edi,edi` so the null branch consumes native flags. Preserve all GPRs, SIMD/FP, stack and LastError around bookkeeping. No owner pointer is retained. |
| Restored binding `0x41a673..0x41a678` (5; 3+2) | EDI=owner, EAX=resolved scene or zero; one outgoing `push 1` is already on stack. Replay owner+4 store and `mov ecx,esi`; preserve EAX/EDI/ESI, the existing outgoing stack and other state. Record the identity without dereferencing the returned scene. No role update on save branch. |
| Scene destruction `0x4a5460..0x4a5465` (5; 1+1+1+2) | ECX=scene, no stack arguments, ordinary `ret`. Enter retirement before the displaced pushes/move and original body. Preserve actual post-call CPU/FP/LastError on normal return; qualify foreign-unwind scope exit. Callback work must not throw across the game frame. |
| Existing projection `0x4de91b..0x4de920` (5) | On the pixel branch only, read current scene from EBX+0x1c, look up live/retiring native-HUD role, exempt matching scene; otherwise apply ordinary anchor scaling **only while completeness is known**. Replay EAX=1; preserve live ESI/EDI/EBX/EBP, SIMD and flags conservatively as in the input note. |

There are no HIGHLOW operands inside these four spans. Their VAs and any helper
targets still require loaded-module-base/RVA resolution. The new-binding jump
starts at qword offset 4, so its first five bytes cross the aligned qword; the
other three fit. Whole-instruction spans and the negative interior-edge census
do not authorize concurrent installation. Install/rollback requires a qualified
quiet early boundary before any HUD-scene binding can be missed. Activation with
already existing unobserved scenes needs a separately qualified baseline; an
empty table must not be called complete.

Cost requirement: **one extra engine DWORD read per pixel projection**, followed
by an expected constant-time lookup of mod-owned scene identifiers. Publication
and retirement happen per scene, not per draw. A growable structure allocated
on publication is preferable to inventing a supported-cockpit-count limit;
reserve/grow outside native forwarding, never allocate or perform engine walks
in the projection lookup. Synchronization and actual lookup cost need the
eventual implementation's performance pass; no game timing is inferred here.
The tracked scene role does not depend on GPU resources and must not be cleared
blindly on D3D Reset while CPU scene objects remain alive.

## 6. Completeness faults and exact remaining integration boundary

Publication allocation failure, an unsupported thread/reentrant mutation,
unqualified unwind or a missing startup baseline invalidates the role set.
If bounded storage is selected, overflow has the same meaning; later available
capacity does not restore completeness. **Do not silently scale an untracked
native HUD scene or use an arbitrary camera fallback.** Reject initial scale
activation when incomplete.

For a fault during active scaling, all coordinate sites form one group: screen
sizes, deltas, cursor stores, native pick/aim, map labels/picks and projection.
Setting only projection to identity is wrong, and setting every factor to 1
does **not** repair existing virtual KC cursor/clipping/menu layout. A coherent
outer UI boundary with layout/clipping/cursor resynchronization, or an explicit
session restart, is required before the group can change state. This study has
**not identified or qualified that relayout/reset boundary**. The parent owns
the activation/recovery design; the probe reports a `gate-required` condition
instead of pretending to implement it. Recovery must establish a complete fresh
baseline or restart tracking before any scene exists, not merely clear an error
bit or wait for another Present. Until this boundary and wrapper/thread admission
are implemented and checked, the three-site strategy remains an implementable
lifetime/read contract with explicit activation prerequisites, not an enabled
FL UI feature.

## Verification

[verify_fl_ui_camera_lifetime.py](../../../verification/probe/verify_fl_ui_camera_lifetime.py)
uses the project's PE reader/Capstone and verifies the reviewed FL EXE SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Measured: **80 instruction-semantic checks**, **5 whole-instruction spans**
(four cockpit-core/projection spans plus target-overlay publication),
**428,814** linear-decoded instructions, **zero** direct edges into those span
interiors, **7** direct scene-destructor callers. Eight sequential policy
witnesses exercise simultaneous owners, camera reassignment/secondary cameras,
scene-address reuse, callback rebinding, late failure/unwind, null bindings,
publication allocation failure and a foreign writer. These demonstrate necessary
bookkeeping distinctions, not native execution or concurrent wrapper correctness.

```sh
python3 verification/probe/verify_fl_ui_camera_lifetime.py --raw
python3 -m py_compile verification/probe/verify_fl_ui_camera_lifetime.py
```

The local reproducer, parsed summary and raw bounded listings are ignored under
`verification/results/fl-port/ui-camera/`. No raw listings or game bytes are
tracked. Native Windows/CrossOver behavior, multiple-monitor visuals, destruction
callback exercise, foreign-unwind forwarding and the coherent UI recovery
boundary remain unverified. No whole-renderer prerequisite is introduced.

## Independent review

Bounded deep review found no blocker and reproduced 80 semantic checks, five
spans, eight policy witnesses, 428,814 linear instructions and zero direct
interior edges. Targeted inspection confirmed the restored-binding stack state,
scene destructor ABI/retirement ordering, projection camera access, target-overlay
scope and tooltip clamp sites. Compilation and five relative links passed.
The review identified a metadata omission: the generated `open_contracts` now
explicitly includes the load/readiness barrier already required above. No
activation, runtime hook or concurrent-wrapper qualification is implied.
