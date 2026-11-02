# FL modern and elevated chase camera: native pose contract

Bounded offline comparison, 2026-10-29. **The current camera module has one
native hook. FL has a matching final-pose seam and compatible mathematical
inputs, but different native function ABIs and relocated image addresses.**
Keep the spring/elevated geometry and the bounded anchor reader shared. Supply
an FL pose-site/address profile through the existing engine admission/patch
boundary. This is a static mapping, not an implemented FL camera, validated
trampoline, visual acceptance or native Windows verification.

Scope is `src/engine/ap/chase_camera.cpp`, `.h`, `chase_camera_native.h` and
`chase_camera_math.h`. The [AP camera architecture](../../architecture/chase-camera.md)
and [elevated geometry](../../architecture/elevated-chase-camera.md) explain the
existing behavior. Historical paragraphs in those notes retain earlier tuning;
the current source defaults below are authoritative for this comparison.
**True-forward central HUD placement belongs to `chase_lead`, and automatic
view restoration belongs to `chase_transition`.** Neither becomes supported in
FL merely by mapping the camera hook. Their distinct native sites and VM
contracts are outside this checkpoint.

## 1. Active hook, ABI and native ordering

All VAs below are preferred addresses at image base `0x400000`.

| Operation | AP | FL / established evidence |
| --- | --- | --- |
| Cockpit updater | `0x4205e0..0x4216dc` | `0x421f50..0x422fb0` (half-open). FL prologue realigns ESP to 16 then reserves `0xc4` bytes; EBX=cockpit from ECX at `0x421f5e`. |
| Incoming updater ABI | Stack argument `[EBP+8]` loaded into EBX at `0x4205ed`; callee `RET 4` | ECX=cockpit, no stack arguments, plain RET at `0x422fa1/0x422faf`. **Do not reuse AP's function-entry call signature.** |
| Sole surveyed updater caller | `0x41ce3e`, cockpit pushed at `0x41ce3d` | `0x41e5b0`, inside registry walk `0x41e550..0x41e633`; ECX receives the map value. The walk visits registered cockpit rows, not only the selected cockpit. |
| Loop ordering | `0x403f2f → 0x41cde0`, then `0x403f34 → 0x471f50` | `0x4782ca → 0x41e550`, immediately followed by `0x4782cf → 0x48b6d0` (complete render pass). The registry walk has that sole surveyed direct caller. |
| Camera-module hook | `[0x420e06,0x420e10)` | **`[0x4227ad,0x4227b7)`**, RVA `0x227ad`; EBX=cockpit. Whole instructions: four-byte compare of `[EBX+0x54]` with zero, six-byte equal branch to `0x422928` (RVA `0x22928`). |
| Stolen-span relocation | rel32 field at span+6; continuation `0x420e10` | Same field offset **6**; rebase branch to the loaded FL target and append jump to loaded RVA **`0x227b7`**. No absolute/HIGHLOW operand in the ten-byte span. |
| Patch atomicity | Five-byte jump crosses a qword | FL five-byte jump also crosses a qword (`site & 7 == 5`). Use the existing quiescent installation window and exact span ownership; no runtime plain-copy patch permission follows from the mapping. |

The FL pose is final before this seam: native basis multiplication stores to the
sector camera from `0x42250f/0x42252c`, or the verbatim branch stores the three
rows ending at `0x422556`. Native boom `cockpit+0x160` is applied afterwards;
its last position copy is **`0x4227a9`**. At `0x4227ad`, replacement position
and basis therefore precede camera-relative object coordinates: their first
camera load is `0x4227e3`, and the first result store is node+0xf0 at `0x422835`.
Overlay update `0x4229c2 → 0x42c330` and galaxy/dust camera copies
`0x422eb0..0x422ed8` follow. FOV can change later, at **`0x422cc5`**; the pose
handler samples the current FOV exactly as AP does, while a HUD projection
consumer needs the separate final-FOV dependency.

Seven surveyed direct edges target the **start** of the FL span:
`0x422141`, `0x422597`, `0x4225a6`, `0x4225af`, `0x4225b8`, `0x4225c1`,
`0x4225ca`. None targets its interior. The first can arrive with no reference
object: reaching the hook is not pose admission. Image-wide linear `.text`
decoding and the HIGHLOW census find zero direct interior edges, zero relocated
pointers into the span, and zero relocation fields inside it. Complete-region
and seam edges are independently compared with decoding from `.text`'s known
entry thunk. This bounds the reviewed native profile; it does not exclude
arbitrary computed private-address calls or third-party patches.

**ASLR matters:** clean FL sets `DYNAMIC_BASE` and has **38,327 HIGHLOW entries**
(measured). Resolve every native VA as admitted loaded-image base plus RVA,
including the default-plane slot `RVA 0x1a82f8`, active registry `RVA 0x1a82e4`,
and branch continuations. The updater RVA is `0x21f50`. Neither AP's fixed
VAs nor one uniform AP-to-FL delta is a valid address adapter.

## 2. Fields, private cameras and anchor selection

| Supplied object / field | FL evidence and module use |
| --- | --- |
| Cockpit `+0xc/+0x10` | Reference/follow object versus controlled/view object; native follow reads `+0xc` at `0x42241c`, and compares the pointers at `0x4224d7`. Reuse [own-ship identity](own-ship.md) and [input/lifetime classification](ui-input.md). The camera's bound predicate `view != 0 && view == ref` is a feature policy, not a universal player-object identity. |
| Cockpit `+0x58` | Sector/world camera, consumed at `0x4224be`, `0x4225d0`, `0x4227e3`. Its assignment is **`0x430738 → 0x4a2470`**, then **`0x43073d`** stores the resolved camera; a nonzero result receives cockpit+0x240 FOV at `0x43074a`. It is an externally resolved camera reference, not the owned HUD camera. |
| Cockpit `+8` | Separate HUD-scene camera. Its basis is constructed earlier at **`0x42211d`**, using vanilla cockpit+0xf0. The optional `scene_fix` correction is therefore applicable by the same relative rotation, but remains default off. Scene/camera birth, restore, attachment and destructor callbacks are already established in [HUD camera lifetime](ui-camera-lifetime.md). |
| Cockpit `+0xf0/+0x100/+0x110` | Three 16-byte-stride fixed-point relative basis rows. Current-frame regeneration in `0x423160` ends with `ECX=cockpit+0xf0` at `0x4244e3`, call `0x4244ef → 0x50d400`; updater calls this owner at `0x421f6d`. Native follow multiplies this relative basis with the selected native basis at `0x42250f/0x42252c`. |
| Cockpit `+0x130` / `+0x160` | Local follow offset / additional camera boom. The former is transformed at `0x42243c/0x42247e`; the latter is tested at `0x422578` and applied before the seam. Read the final native camera, do not synthesize a ship-class boom. |
| Cockpit `+0x150/+0x1c0/+0x1a0` | View mode / connect mode / flags. Mode=1 selects the internal path (`0x422151`). Connect modes 5/6 and flag mask `0x4` bypass normal basis composition (`0x4224dc..0x4224ed`); connect=3 has a separate view/reference condition (`0x4224c5..0x4224da`). The portable pipeline conservatively rejects every nonzero connect and flag mask `0x4`. View-mode writer at **`0x431abc`** receives EAX=cockpit, ECX=mode. |
| Cockpit `+0x1d8/+0x1e0/+0x1e4` | Aim-gun index / tracked object / tracking short. Aim gun read at `0x422c30`; tracked pointer and its native id+8 at `0x43106f/0x43107d`; tracking comparisons at `0x430f8b` (SI) and `0x430fc4` (4). Default combat tightness is zero; no new game acceptance of the optional lock-tightness behavior is claimed here. |
| Cockpit `+0x54/+0x1fc` | Current sector-space pointer / subsequently published sector identity. `+0x54` gates the stolen span; `+0x1fc` is cleared at `0x42293f` and populated at `0x4229a8` (alternate successful lookup joins this store). As in AP, the current seam precedes this update; the spring's sector-change identity can lag one visit. |
| Camera `+0x30/+0x34/+0x38`, `+0x40/+0x50/+0x60` | Signed integer position and 16.16 basis rows, with three values plus preserved fourth word per row. The [camera-buffer study](camera-buffers.md) independently establishes row-vector perspective/view conversion and the same native fixed-point convention. |
| Camera `+0x298/+0x300/+0x304` | Binary-angle FOV / fixed-point view-plane width and height; independently established by `0x4dec70`. Fallback loads `*0x5a82f8 +0x28/+0x2c` at `0x4dec8c..0x4dec94`. AP's `0x606f38` cannot be used. |
| Reference `+0x48/+0x50/+0x54/+0x70` | Type / simulation data / owner / render-root pointer. `0x459780` tests nonzero owner and owner type=1. When true the updater uses root+0x30 (`0x422441/0x422447`); otherwise root+0xb0 (`0x42246f/0x422483`). |
| Native diagnostic basis | `0x459fc0`: type=7 chooses data+0x870 (`0x459fc7/0x459fca`); other types choose root+0x40 (`0x459fd0/0x459fd3`). Non-base branch uses root+0xc0 at `0x422478`. Optional diagnostic failures must not reject an otherwise valid position anchor. |

AP's domain/basis helpers `0x44fe20/0x450520` receive the reference in **EAX**;
FL's `0x459780/0x459fc0` receive it in **ECX**, return EAX, and use plain RET.
The existing `read_native_anchor` template makes **no native helper calls**:
it reproduces these verified field selections through its bounded read callback.
All its selected fields and strides match FL. The required anchor must remain
the same domain as native follow; root+0xb0 cannot replace root+0x30 merely
because it looks like the latest renderer position.

## 3. Shared algorithm, writes and module dependencies

The eligible shared code is `chase_camera_math.h`: matrix conversion,
orthonormality/range refusal, rear-view geometry and hysteresis, critically
damped rotation/boom springs, elevated target construction, FOV compensation,
snap/coalescing, and conversion back to integer pose. The verified identity is
`B_camera = R_view * B_ship`; derive ship basis with
`transpose(R_view) * B_camera`, and write replacement `R_view` as
`B_replacement * transpose(B_ship)`. FL basis composition and renderer
interpretation support the same row-vector identity. No x87 imitation or new
engine call is needed. Use the project's SSE2 and four-byte incoming-stack build
contract for any new helper; native updater stack realignment does not remove
that callback requirement.

Current source defaults (measured source values) are rotation tau **0.28 s**,
position tau **0.38 s**, vertical offset **0.50**, pitch down **0.5 degrees**,
boom distance scale **1.05**, FOV compensation **on**, angular lag limit
**8 degrees**, positional lag fraction **0.10**, combat tightness **0**,
maximum dt **0.10 s**, and snap coalescing **3** applied frames. Elevated
geometry scales the native boom by `1.05 * clamp(0.75/tan(half_vfov),0.5,2)`;
the settled anchor screen fraction is `(1+0.50)/2 = 0.75` (inferred geometry).
These are existing feature choices, not FL runtime measurements. FOV and view
mode numeric/script behavior still need FL user acceptance; rear-view geometry
is deliberately not inferred from mode=258 alone.

After full admission, the module writes **84 bytes** (inferred from source):
12 position bytes, 36 camera-basis bytes, 36 cockpit-relative-basis bytes,
preserving row padding. Revalidate both writable owner spans before the first
write; no old address-only permission cache authorizes a reused object.
`scene_fix=1` additionally writes 48 bytes to the current HUD camera after its
own validation. The current read blocks are cockpit `0x200`, camera `0x310`,
reference `0x74`, and optional HUD camera `0x70`; these are read extents, not
claims about full FL class sizes (cockpit allocation is `0x8c0`).

The module publishes owned pose/write-count/snap generations for downstream
consumers. Its calls to `chase_lead::camera_context`, `chase_fire::camera_context`
and `chase_aim_trace::camera_context` are **dependency publication**, not their
native hook qualification. In particular:

* True-forward HUD anchor is retained/projected and written by `chase_lead`.
  The camera supplies a successfully written **bound** pose; unbound visits
  deliberately invalidate that context. The AP final-FOV and central-HUD seams
  in that module require their own FL study. `scene_fix` does not implement the
  five-node forward anchor. [HUD anchor design](../../architecture/chase-view-restore-and-hud-anchor.md).
* View restoration is observed here through `restore_observe_frame`, using the
  already-read cockpit fields. This module never writes `+0x150` or calls a VM
  view selector. Supported AP restoration is implemented by `chase_transition`
  outside the updater, as described in [engine-state restore](../../architecture/chase-view-restore-engine-state.md).
  Its lifecycle identity, input/VM call and failure handling must be qualified
  separately for FL. A bare FL mode write is not a restore implementation.
* The zero-view-object pose exception calls
  `chase_transition::active_control_cockpit`: FL's registry/selector/resolver
  layouts are already known, but the AP complete-lifetime/update-generation
  service cannot be carried over as an AP address binding. Until that dependency
  is qualified, the extra unbound phase has no FL admission proof.

**Activation dependency closure:** `chase_camera.cpp` cannot simply be enabled
with only the FL pose address changed. Its entry unconditionally invokes
`chase_lead::invalidate_pose`; its invalid/read-refused and inactive paths invoke
`chase_aim_trace`/`chase_fire` invalidation or invalid-context publication. Every
readable cockpit visit calls `chase_transition::restore_observe_frame`, and a
zero `+0x10` can call `active_control_cockpit`. Successful writes publish
`chase_lead::camera_context`, `chase_fire::camera_context` and
`chase_aim_trace::camera_context`; lead's owned update/lifetime association must
belong to FL before it can accept those values. Successful camera initialization
also calls **`chase_fire::initialize()`**, and shutdown calls its shutdown.
The edition owner must supply qualified FL implementations/admission for these
calls, or an explicit feature-disabled dependency boundary that performs no AP
native access and publishes no usable stale context. Such a partial boundary
would qualify only the pose feature, not forward HUD, fire, lead or restore.
Leaving AP-addressed dependencies reachable is not safe partial activation.
No downstream adapter is implemented or qualified by this note.

## 4. Lifetime, CPU boundary, reentrancy and performance

Reuse the established [own-ship](own-ship.md) cockpit publication,
registry removal-before-destruction, reference invalidation and restored-owner
ordering, and [native owner-thread/queued-location](ui-render-exclusion.md)
evidence. The normal native update runs on that owner before rendering. Its
borrow is the **current cockpit invocation and current native follow camera**;
no engine pointer must escape as permission for a later frame. `+0x58` is a
resolved external alias and `+8` has explicit native destruction callbacks.
Neither pointer is pinned by a readable page, owner-table membership or an old
active-control handle. The renderer complete-pass/material borrow begins after
this updater and cannot itself authorize these camera writes; use the supported
native owner-thread cockpit invocation and its qualified lifetime/update binding. Load/recreation, reference/view mismatch, invalidated
lifetime or missing current camera must produce a pipeline gap and vanilla pose;
selecting a new cockpit must not mix two clocks/springs. Existing observer
lifetime/thread services are the integration boundary; this mapping needs no additional
whole-engine framework, worker thread or per-draw registry walk.

Preserve every GPR, EFLAGS, original stack, LastError and x87/MXCSR/XMM0–7
through a future FL seam callback. EBX is positively identified as cockpit;
EBP and the aligned local frame remain native, and XMM0 contains the final
native copy on the boom path. Do not assume other registers dead across every
incoming branch. Incoming arithmetic flags are killed by the replayed compare;
its new flags must reach the displaced conditional branch unchanged. As in the
existing AP stub, save full state, establish a private empty/masked x87 state
and MXCSR `0x1f80` for the helper, restore native state, then replay the native
compare/branch exactly once. The seam is not a function-entry ABI boundary and
contains no SEH prologue to transplant.

Owner-thread execution alone does not imply non-reentrancy. The native location
setter queues future changes, while documented window dispatch and destructor
callbacks retain their already-reviewed scope. The minimal pose copy/math/write
must not invoke engine/VM/D3D operations or dispatch messages; in particular do
not inject restore from this hook, since a script can replace its cockpit.
Reuse current owner/update admission and deny unsupported nested/foreign use
before accessing or advancing the shared pipeline. The AP pipeline itself is
process-global and has no general recursive-state stack; stats locking is not
engine lifetime exclusion. Any future extra scope must unwind on foreign SEH,
with no stale pointer publication; the existing AP CPU RAII boundary alone is
not evidence of an FL foreign-exception trampoline contract. Native exceptions
in later engine calls must propagate normally after leaving the callback.

Frequency is **once per registered cockpit per ordinary loop traversal**
(inferred from the measured sole-call/registry loop), not once per draw and not
a measured Hz/FPS claim. Preserve AP's early inactive return before QPC/spring
advance. The adapter needs only edition-selected addresses/operands and the
existing bounded read/write/state path at this seam. No allocations, expanded
per-draw camera reads or render hook are required. Existing per-visit QPC,
read validation, stats locks, transcendentals and optional diagnostic reads are
the relevant performance baseline; measure the implemented FL callback rather
than inventing a static timing estimate. Device release/Reset must retain the
process-lifetime pose site as AP does; explicit teardown requires quiescence and
byte ownership, and failed install must roll back its single claim. The camera
handler itself needs no device.

## 5. Evidence and finite remaining checks

Reproduce without executing either game:

```sh
python3 verification/results/fl-port/chase-camera-contract/produce.py
python3 -m py_compile verification/results/fl-port/chase-camera-contract/produce.py
```

The producer uses the project PE32 reader, Capstone and shared FL
boundary/HIGHLOW utilities. Compact derived evidence is
`verification/results/fl-port/chase-camera-contract/contract.json`: **15
regions, 3,711 instructions, 61 operand anchors, 26 independently checked FL
region/seam edges, five rejected mutations** (measured). Mutations bypass image
identity checks and reject changed owner register, sector operand, branch target,
native basis offset and an injected direct interior edge. AP reference identity
is SHA-256 `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`;
FL is `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Hashes bind offline provenance, not the production executable-admission policy.
The result tree is ignored by default: preserve only this producer and compact
record when integrating; raw disassembly remains local/untracked.

Remaining implementation evidence is finite: loaded-base/span ownership and
rollback, both branch outcomes and all inbound start paths, complete CPU/LastError
preservation with four-byte incoming stacks, supported owner/nested refusal,
lifetime/address-reuse and device-recreation behavior. Compile/synthetic evidence
must distinguish Windows-compatible source from native Windows execution.
User-only FL flight acceptance must establish the actual rear view, raised
framing/boom scaling and smoothness across supported view changes; forward HUD
and restore additionally require their separately owned adapters. No production
code, game launch, Wine/native run, build, installation or commit was performed.

## 6. Verified 2026-11-02 against X3FL.exe

Re-checked on the installed `C:\X3FL\X3FL.exe` (SHA-256 `e7f3aad8…`, DllCharacteristics `0x8140`, 38,327
HIGHLOW fixups, no other relocation type; all measured) by `verification/probe/verify_chase_camera_site.py --edition
fl` (Capstone over the file, read-only; compact result `verification/results/fl-chase-camera/verify_fl_site.json`).
Every claim of sections 1–3 that the pose seam relies on holds; none had to be corrected. The implementation is the
FL branch of `src/proxy/chase_camera.cpp` (the scope line above names `src/engine/ap/`; the file is in `src/proxy/`) with its constants in
`src/engine/fl/chase_camera_sites_fl.h`.

| RVA / field | Fact | How verified |
| --- | --- | --- |
| `0x0227ad..0x0227b7` | `83 7b 54 00` `cmp dword [ebx+0x54],0` (4) + `0f 84 71 01 00 00` `je 0x022928` (6); rel32 at +6 | exact bytes; Capstone, whole instructions `[4, 6]` |
| `0x0227b7` / `0x022928` | continuation `xor ecx,ecx`; branch target `mov ecx,ebx; call 0x022fb0`, then `mov [ebx+0x1fc],0` at `0x02293f` | Capstone anchors |
| `0x0227a9`, `0x0227e3` | last native position copy `movups [ecx+0x30],xmm0`; first camera load after the seam `mov ecx,[ebx+0x58]` | Capstone anchors; both inside the 66-byte seam window |
| span relocations | no HIGHLOW fixup touching `[0x0227aa, 0x0227b7)` | `.reloc` census |
| inbound edges | exactly `0x022141 0x0225(97,a6,af,b8,c1,ca)` to the start, none into the interior (72,230 direct edges in `.text`); the raw rel8/rel32 scan of the updater (1,082 whole instructions, `0x021f50..0x022fb0`) has one hit, `71 01` inside the span's own `je` rel32 (an operand alias, no instruction start); sole updater caller `0x01e5b0` | linear-sweep census; raw byte scan classified against the decode |
| qword | `0x0227ad & 7 == 5`: the five-byte jump crosses the qword at `0x0227b0`; the 64 KB module base keeps it | arithmetic; `engine_patch::claim` takes the plain copy, install window only |
| EBX = cockpit | `mov ebx,ecx` `0x021f5e`; one store `mov [esp+0x1c],ebx` `0x021f63`; the other EBX writes (`0x02268d`, `0x0226bd`) lie in the loop `[0x02268d, 0x0226d8)` whose only exit falls through to `mov ebx,[esp+0x1c]` `0x0226d8` | linear decode with Capstone register-write sets; stack-depth equality inferred (the engine uses EBX as the cockpit after the seam on both branches) |
| plane slot `0x1a82f8` | `mov eax,[0x5a82f8]` at `0x0dec8c` (HIGHLOW at `0x0dec8d`, value preferred base + `0x1a82f8`), then `+0x28`/`+0x2c` | `.reloc` + Capstone; checked again at run time against the loaded base |
| cockpit `+0x08 +0x0c +0x10 +0x54 +0x58 +0xf0/+0x100/+0x110 +0x150 +0x1a0 +0x1c0 +0x1d8 +0x1e0 +0x1e4 +0x1fc` | the offsets the shared handler reads (and `+0xf0` rows it writes) | 43 Capstone field anchors (`FL_FIELDS` in the verifier), e.g. `0x0224f9` `[ebx+0x58]`, `0x022539..0x022556` the verbatim `+0xf0` rows to camera `+0x40/+0x50/+0x60`, `0x0224e1` `[ebx+0x1a0]`,4, `0x030fc4` `[eax+0x1e4]`,4 |
| camera `+0x30 +0x40 +0x298 +0x300 +0x304`; tracked `+8` | position, basis rows, FOV, plane W/H | anchors `0x0227a9`, `0x0224fe`, `0x03074a`/`0x0dec97`, `0x0dec76`, `0x0dec82`, `0x03107d` |
| reference `+0x48 +0x50 +0x54 +0x70`; owner `+0x48`; node `+0x30 +0x40 +0xb0 +0xc0`; data `+0x870` | the anchor selection of `chase_camera_native.h` | anchors `0x059780..0x059787`, `0x059fc0..0x059fd3`, `0x022441/0x022447`, `0x02246f/0x022478/0x022483` |

Run-time identity: before any write, three windows at base + RVA must match byte for byte: the seam
`[0x0227a4, 0x0227e6)` (FNV-1a `391b90a2`), the branch target `[0x022928, 0x02292f)` (`466a42e2`) and the plane load
`[0x0dec8c, 0x0dec97)` with its fixup zeroed (`2df2f1ce`) and equal to base + `0x1a82f8`; then `engine_patch::claim`
compares the ten span bytes again. Refusals: `executable_mismatch`, `invalid_base`, `unreadable`, `seam_mismatch`,
`target_mismatch`, `plane_slot_mismatch`, `plane_window_mismatch`, `late_claim`, `invalid_tunables`. After the
write: `readback_mismatch` (the five bytes do not read back as this module's jump, before the stub is chained) writes
the original five bytes back over whatever is there, inside the install window (the `engine_effects_fl` convention;
`engine_patch::restore` would refuse foreign bytes as not owned), and counts as `rollback=restored` only when all ten
span bytes then read back as the original; otherwise `rollback=failed` and the row says `installed=1
status=readback_mismatch`: the site stays registered for `shutdown()` and is inert (no stub chained; `installed()`
agrees with the row). `stub_failed`/`chain_write_failed`/`chain_failed` (the jump is ours) use `engine_patch::restore`.
The fixture injects the read-back fault as real writes into the synthetic span: one byte inside the jump →
`rollback=restored`, bytes inside and beyond it → `rollback=failed`.

Dependency boundary chosen (section 3's second option): in the FL build the handler compiles out every call into
`chase_lead`, `chase_fire` and `chase_aim_trace`. Since the chase view restore port (2026-11-02,
[chase-transition.md](chase-transition.md), "Verified 2026-11-02") it calls `chase_transition`'s FL binding for
`restore_observe_frame` and, as on AP, for the unbound-phase proof: a `+0x10 == 0` visit is admitted only when
`active_control_cockpit` holds (a complete witnessed lifetime whose cockpit-registry active handle, slot base +
`0x1a82e4`, maps to this cockpit; false while `chase_transition` is not installed). The FL object references
`chase_transition` only for those two functions, and none of the other three modules nor `object_trace` (checked with
`nm`); the install row says `unbound_phase=active_control dependencies=chase_transition`. The loader logs one row each,
`<module> installed=0 status=ap_only edition=fl reason=ap_only`. The handler calls no native function, so the FL
updater's `ECX`/plain-RET ABI is not used. The FOV is read from camera `+0x298` as on AP (no `fov.h` accessor needed).
Not verified: the pose in game on FL (not flown), native Windows game execution, foreign-SEH unwinding through the
callback.
