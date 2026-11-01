# FL camera publication and owned matrix snapshots

Offline bounded study, 2026-10-28. Image `/mnt/c/X3FL-orig/X3FL.exe`, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Addresses are preferred VAs, base `0x400000`; runtime descriptors require RVAs
and the admitted loaded base. Owning plan: [bounded renderer sampling](../../architecture/farnhams-legacy-engine-profiles.md#bounded-renderer-sampling-plan-after-value-lifetime-qualification).

**Copy view and perspective at camera publication, before the following device
call: 128 bytes into owned storage.** FL's pointer slots are **view `0x5aae68`**
and **perspective `0x5aae78`**. They are global renderer scratch buffers, not
camera-owned allocations. A late readable-page check cannot establish which
camera their contents describe: orthographic instance setup overwrites the view
with identity, and draw setup changes perspective depth terms. Noncube activation
also has three distinct complete-pass origins; it does not imply the main view.
This note qualifies offline native evidence and an adapter contract, not an
implemented hook or activated FL consumer.

## Evidence and reproducibility

```sh
python3 verification/probe/verify_fl_camera_buffers.py
python3 -m py_compile verification/probe/verify_fl_camera_buffers.py
```

[Verifier](../../../verification/probe/verify_fl_camera_buffers.py): measured **20
regions, 2,042 instructions, 104 operand anchors, 44 independently decoded region
and seam edges, ten rejected negatives**. Nine mutation cases bypass region
hashes and exercise publication/transport anchors; the tenth targets an operand
byte inside the main seam and exercises the inbound-edge census. These are
static/anchor checks, not execution of an adapter. The specification model tests
four denials before copy, two owned 128-byte snapshots, five identity/generation
mismatches and separation of canonical versus jittered data.

Raw disassembly, root discovery, `report.json`, supplemental producing scripts
and `reproduce.sh` remain ignored under
`verification/results/fl-port/camera-buffers/`. The verifier emits derived counts
and hashes; no executable bytes are included in tracked evidence. Direct-edge
and HIGHLOW censuses bound this fixed native profile. They do not prove absence
of arbitrary computed calls into private addresses or replacement third-party
vtable implementations. Reuse the established healthy native callback closure.

## 1. Layout, ownership and replacement interval

| Slot | Meaning and native evidence |
| --- | --- |
| `0x5aae64` | World matrix; allocation stores `0x4d9f48/0x4d9f7d`, instance writes `0x4de760/0x4de9e6`. |
| `0x5aae60` | Related world/basis scratch, separate 64-byte allocation at `0x4d9fd2/0x4da007`; not a camera latch input. |
| `0x5aae68` | View: 64-byte allocation request `0x4da01a`, allocator `0x4da01f → 0x4d8fc0`, publication `0x4da024`. |
| `0x5aae78` | Perspective: 64-byte request `0x4da036`, allocator `0x4da03b → 0x4d8fc0`, publication `0x4da040`. |
| `0x5aae4c` | Orthographic-path projection, allocation `0x4da05e`; rewritten through ESI from `0x4de765`. Not the perspective slot. Initialization alone gives it perspective-template contents; its role is established by the later branch. |

Initialization `0x4d9d10` has one surveyed direct caller, `0x4777c8`; its AL
result is tested at `0x4777cd`. Null allocation exits never confer readiness.
The paired view/perspective buffers are freed at `0x4db65e/0x4db688`, then their
slots cleared at `0x4db674/0x4db69e`. The sole surveyed entry to `0x4db600` is
the tail jump `0x4a5a49` in renderer cleanup `0x4a5a00`, whose sole caller is
`0x47640c`; engine destruction follows at `0x476411 → 0x48a950`.

The complete direct slot-reference census has **13 view and 16 perspective
references**: their only direct slot writes are initialization and cleanup.
There is no surveyed relocated pointer-to-slot escape outside those instruction
operands, or relocated function-pointer publication of the named owner/setter
entries. This supports **renderer-lifetime allocations**, with no per-camera
replacement or demonstrated Reset replacement. It is not permission to retain
a dereferenceable buffer pointer through teardown, failed initialization, or a
new renderer incarnation. Invalidate on these events and device Reset even if
the numeric pointer stays the same. Recovery's qualified resource cleanup does
not reach the global matrix free owner; reuse its typed closure, not a new
whole-engine audit.

For the consumer implementation, AP `camera_state.cpp` currently uses slots
`0x608a38/0x608a40` and caches readability by address. `object_trace.cpp` uses
AP world/basis slots `0x608a44/0x608a48` for optional diagnostics. None of those
addresses, cached permissions or AP layouts carries over to FL.

## 2. Actual identity and publication

Ordinary setter **`0x4ded90`** receives **ECX=scene/context, EDX=camera**, with
plain RET and no stack arguments. `0x4deda3/0x4deda5` save them in ESI/EBX.
Caller `0x495670` takes camera at original stack+4 (EBP+8), moves it to EDX at
`0x49567c` and calls the setter at `0x495687`. Complete-pass origins pass the
actual camera and context, rather than looking up a last/current global camera:

| Origin | Identity/provenance |
| --- | --- |
| `0x48ba43 → 0x495670` | ESI camera; ECX=camera+0x1c context; ordinary per-camera render loop. |
| `0x48bbef → 0x495670` | Same transport; separate follow-up for camera+0x77c, with temporary camera flags. Must have a distinct activation generation. |
| `0x48bc87 → 0x495670` | ECX=engine+0x64 special scene, ESI its selected camera. Distinct pass provenance. |
| `0x48b9f6 → 0x4977c0 → 0x49784d → 0x4defc0` | Cube path: ECX=context in EBX, EDX=camera in ESI, stack+4=face pushed from EDI at `0x497848`; caller removes four bytes at `0x497852`. Loop bound `0x4978bf` is six faces. |

Cube setter saves **ESI=context, EDI=camera**, uses EBP+8 for face
(`0x4deffd`), and also returns with plain RET. Its camera address remains the
same across faces: camera address/handle alone is insufficient history identity.
The material study establishes camera+0x28 as registered handle. Prefer the
observer's existing incarnation/serial binding and the actual camera token here;
do not add an unrelated camera/registry pointer walk to the 128-byte copy.

Both setters call **`0x4dec70`**, ECX=output matrix and EDX=camera. It computes
projection from camera+0x298 fixed-point angle and camera+0x300/+0x304 dimensions
(fallback display dimensions when nonpositive), then returns output in EAX.
P[11]=1, P[10]=`0x3f800019`, P[14]=`0xc0c00026`; the other template terms
establish the ordinary row-vector perspective form. Ordinary setter publishes
four rows at `0x4dedc4..0x4deddb`; cube publishes at `0x4df003..0x4df01f`.

Ordinary view initializes identity then transposes camera fixed-point basis
+0x40/+0x50/+0x60 into the matrix (scale 1/65536). Translation is negative
camera position (+0x30/+0x34/+0x38) dotted with those basis columns, multiplied
by context+0x2c. Its final store is **`0x4def9f`**, V[14]. This is the same
row-vector convention consumed by `camera_reprojection.h`; no AP field alias is
needed. Cube builds the face view at `0x4df0c1 → 0x4e7f20`, then publishes all
four rows ending **`0x4df0ed`**. `0x4df0f1 → 0x4e7e50` converts that view back
to fixed-point camera basis; the complete conversion helper contains zero CALLs
and reads the view, writing only its supplied output. Camera basis stores follow
at `0x4df103/0x4df10c/0x4df115`.

## 3. Snapshot seams, ABI and callback obligations

| Candidate span | Exact boundary and live values |
| --- | --- |
| **`[0x4defa4,0x4defaa)`** | One six-byte `push [ebx+0x278]`, directly after final ordinary view publication. EBX=camera, ESI=context, EDI=device; EAX=view pointer; XMM1 holds last translation. Resume `0x4defaa`, which loads the device vtable, pushes device and invokes SetViewport at `0x4defad` (+0xbc). |
| **`[0x4df0f1,0x4df0f6)`** | One five-byte CALL to `0x4e7e50`, directly after final cube view publication. EDI=camera, ESI=context, EDX=stack output for conversion, EBP+8=face; ECX=view pointer. Copy before forwarding the conversion call and replay it exactly once. Continuation consumes the converted stack basis. |

Neither seam contains a HIGHLOW operand or has a surveyed direct branch into
its interior. The cube displaced CALL needs an adjusted relative target in a
trampoline; the ordinary displaced PUSH must occur once with original EBX and
the original native stack layout. A five-byte jump over the six-byte instruction
requires owning the entire six-byte span and its final padding byte. Neither
span includes a native SEH prologue. Setter entries realign their own stack
(main to eight, cube to sixteen); a helper still needs the project's four-byte
incoming-stack ABI and separate aligned save storage.

Preserve all GPRs, EFLAGS, LastError and FP/SIMD state, including MXCSR/x87
control/state, across observation. Main continuation kills arithmetic flags
with `neg` after SetViewport, and cube helper overwrites flags during conversion;
that does not grant a general helper permission to clobber live FP/SIMD state.
Forward original stack words and registers, preserve native results and error
state through exit bookkeeping. Use exact loaded-image/span ownership checks,
quiescent install, complete two-seam rollback and quiescent uninstall. These are
requirements for future assembly/fixtures, not a tested new trampoline.

The copy is **call-free**: under positive complete-pass/owner admission, load the
two slots (eight image bytes), reject null, copy 64+64 heap bytes, and publish
only owned data. No allocation,
logging, waiting, D3D, registry enumeration or helper callback may intervene in
that native read interval. A page-readable cache is neither admission nor writer
exclusion. Admission includes the [material borrow prerequisites](material-submit.md#3-actual-borrow-readiness-writers-and-reentrancy),
[load/full-pass readiness](ui-load-readiness.md),
[native callback/close deferral](ui-render-exclusion.md), and
[recovery/continuation closure](ui-render-mutation.md). Those cover
returning native callbacks before the setter, as well as destruction later in
the pass. No buffer read occurs in exception cleanup.

Publication is a **canonical camera activation**, not proof of successful D3D
viewport setup. Main calls SetViewport after the copy and converts its result
to an EAX boolean. Cube first calls GetRenderTarget at `0x4df12e` (+0x98), can
invoke allocation recovery at `0x4df162`, retries at most once, calls
`0x4e82e0` on success, and releases the surface at `0x4df18e`. The surface helper
calls GetDesc at `0x4e82f3` (+0x30), then SetViewport at `0x4e8338` (+0xbc).
Cube's final `test esi,esi / sete al` at `0x4df193/0x4df196` tests the saved
GetRenderTarget result, **not SetViewport**. Never certify viewport success from
cube EAX. A consumer needing an effective target/viewport must also match the
proxy's successful device state and compositor target generation; failures or
missing correlation deny that consumer.

Use per-invocation owned records: a nested activation must not overwrite the
outer record used by nested material/D3D observers. A reentrant/native writer
may change global buffers after the copy without changing it. Restore the
outer *record scope* on return, but do not pretend that restores native matrices
or device state: require matching current activation/viewport generations again.
Any enclosing setter/pass wrappers must remove exactly their record on foreign
SEH unwind; Search/ContinueExecution leaves it intact. A C++ destructor alone
is insufficient. Deny nested out-of-profile admission before any read. Do not
hold a lock across the native viewport/recovery calls. This uses the existing
supported owner/callback contract, rather than demanding exclusion from arbitrary
foreign private calls.

## 4. Mutable matrices, private projections and consumers

The global buffers are not immutable until the next activation:

* Orthographic instance projection `0x4de570`, node+0x130 bit 0x200, loads
  orthographic slot at `0x4de765` and **overwrites global view with identity**
  at `0x4de79a..0x4de7ba`. The orthographic matrix is then rewritten for that
  instance. Material multiplication selects world*view*orthographic at
  `0x4e4399..0x4e43b2`, versus the perspective branch at `0x4e43c0`.
* Ordinary instance setup restores default perspective depth at
  `0x4deb78/0x4deb7f`. Material wrapper writes P[10]/P[14] at
  `0x4e7065/0x4e7075`, alternate values `0x4e7106/0x4e710d`, or defaults
  `0x4e711e/0x4e7125`. Two effect paths also restore depth at
  `0x4dfffe/0x4e0005` and `0x4e0c82/0x4e0c89`.
* The same perspective builder also has callers `0x4e8f59/0x4e9135` producing
  **stack-local matrices**, with copies at `0x4e8f70/0x4e913d`. Their existence
  does not republish global camera state and does not qualify all submissions
  as using the latched perspective.

Keep canonical P/V and actual submitted matrix rows separate. In particular,
P[10]/P[14] from this activation snapshot describe activation defaults, not
every draw's depth law. FL per-draw depth, orthographic and private-projection
routing must use submitted shader-constant shadow rows plus explicit draw
classification; the 128-byte camera copy does not replace that. The extra
world/basis/view/projection reads in `object_trace::current(..., true)` remain a
separate diagnostic snapshot boundary. Do not silently enable them through this
latch or follow pointers copied by the material snapshot.

For future jitter, preserve an unjittered canonical owned snapshot and a separate
effective copy stamped with the same camera/pass and jitter generation. Apply
jitter only through the qualified perspective submission path and make its
corresponding current/previous shader matrices agree. Later engine activation
will overwrite native P; orthographic and stack-local projections need explicit
policy. This study does not qualify an FL projection-write/jitter hook.

`MotionOutput::read_camera` currently latches scene/background on selector Clear
phases; TAA, shadow candidate/depth paths, light-map far fade and small-parts
culling consume it. FL `camera_state::read` must obtain **only a matching owned
record**, with no native-memory fallback. Required key: observer/renderer
incarnation, load generation, complete-pass generation, scene identity, camera
identity/serial, activation generation, origin kind, and cube face where present.
A matching material scope supplies camera identity; the compositor supplies
scene/background/target role. Cube never fills the ordinary scene latch. Another
ordinary camera, auxiliary activation, special scene, stale pass or Reset must
invalidate rather than reuse the last record. Diagnostic pointer values are
identity labels only and never become dereference permissions.

## 5. Remaining finite acceptance

The native buffer lifetime/publication question is resolved for the supported
complete-pass closure; no new whole-engine scheduling audit is required. The
**specific missing consumer edge** is FL compositor correlation from the three
ordinary origins and cube origin to actual selector Clear/target phases. That
must distinguish main scene, background, auxiliary and special scene before
`MotionOutput::read_camera` can accept a record. Viewport success also requires
the actual proxy result/target generation, especially on the cube path above.
These are explicit connected-renderer prerequisites, not permission to guess
from a noncube flag or matching dimensions.

Implementation still needs the stated hook/state-preservation, nested scope,
foreign-SEH, rollback and Reset fixtures; admitted failure paths must leave zero
usable stale records. Measure the implemented once-per-activation 128-byte copy
and consumer lookup, with no per-draw allocation, locking through D3D or repeated
page validation. No production patch, build, Wine/game run, install or commit was
performed. Windows-compatible source and synthetic verification will not by
themselves establish native Windows runtime behavior.

## Independent review

2026-10-28: independent deep review accepted the static contract without
actionable findings. The verifier passed 20 regions, 2,042 instructions, 104
anchors, 44 boundary edges and ten negatives. Targeted inspection confirmed
allocation/free ownership, camera/context transport, publication stores, matrix
conventions and the cube viewport-result limitation. Actual adapter execution
and compositor/consumer correlation remain separate implementation requirements.

## Ordinary raw-input extension for Background proof — 2026-10-28

The existing ordinary publication at `0x4defa4` additionally owns **16 bytes**:
three signed raw camera-position words at `camera+0x30/+0x34/+0x38` and the
single float context scale at `context+0x2c`. The copy uses the already supplied
EBX camera and ESI scene/context, equality-checked against the current admitted
complete-pass scope. It does not follow a camera/context pointer or obtain read
authority from registry membership. Camera bounds extend through `+0x3c`, context
bounds through `+0x30`; cube retains its previous read set and leaves the new
`transform_inputs_known` field false.

Native provenance is explicit: `0x4deda3/0x4deda5` preserve the supplied ECX/EDX
in ESI/EBX. Position loads include `0x4deee2`, `0x4deee7`, `0x4def07`; scale
multiplications occur at `0x4def18`, `0x4def54`, and finally `0x4def90`. The final
view translation store is `0x4def9f`, immediately followed by the existing
publication seam. There are **zero calls** between those position reads and
publication, and no intervening writes to either supplied-owner register. These
are the same live objects just consumed by native camera construction under the
qualified complete-pass borrow. Existing before/after admission, request epoch,
owned lifetime binding and camera serial checks still govern publication. The
fields are owned activation-time values; they do **not** qualify later mutable
material-time transforms or submitted constants by themselves.

Reproduce the bounded native operand/transport checks with:

```sh
PYTHONPATH=verification/probe python3 verification/probe/verify_fl_background_camera_inputs.py
sh verification/probe/build_fl_camera_adapter.sh
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_fl_camera_adapter
```

The new producer checks **11 anchors**, zero callback instructions and supplied
register preservation; derived output is
`verification/results/fl-port/background-transform/camera-input-provenance.json`.
The compiled publication audit still finds **zero intervening native-copy calls**.
Record zero initialization occurs before owned-binding acquisition, keeping a
compiler-emitted initialization `memset` outside the copy interval. No hook span,
assembly instruction replay, CPU/LastError envelope or rollback logic changes.
The existing 512-byte scope-storage bound still compiles. Cost is four scalar
loads and 16 owned payload bytes per ordinary activation, with no per-draw reads,
allocation, locks or page checks.

Native adapter execution and independent review are recorded in the
[camera adapter ledger](../../verification/farnhams-legacy-camera-adapter.md).

## Fixed cull dependent retirement — 2026-10-29

The existing camera dependent retirement now has one private fixed helper:
scalar cull retirement, followed by the existing optional sun callback. It is
called at the same three existing sites: activation advance, mismatched scope
finish, and invalid/abnormal finish. There is no new callback registration or
subscriber registry. Cull retirement checks the startup-selected FL binding
and zeros only the threshold and upper words: no floating-point arithmetic,
logging, native camera reads, allocations or locks. It operates even when sun
is off and retains diagnostic node counts until their existing frame boundary.

Normal valid origin return intentionally retains both the owned camera record
and current cull thresholds: the native walker/selector follows that return.
The exact native ordering and its failed/absent-Clear limitation are documented
in [the cull-chain note](cull-chain.md#6-scene-projection-publication-precedes-selection--2026-10-29).
The separate any-thread invalidation request remains atomic-only; its existing
admitted owner consumption invokes retirement. This does not grant a foreign
thread authority to mutate render-thread cull state.
