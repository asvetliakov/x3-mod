# FL partial-sun visibility and lens draw bracket

Offline contract, 2026-10-29. Image `/mnt/c/X3FL-orig/X3FL.exe`, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Addresses are preferred VAs at `0x400000`; a production descriptor must use RVAs
and the admitted loaded base. This study does not implement or qualify the FL
partial-sun override, fractional lens gain or wide-FOV collector fix. The existing
gain-zero cull is a separate feature; current installed-build state belongs only
in `docs/status.md`.

**The correct two FL seams are probe CALL `0x48af0a → 0x4a3fd0` and lens draw
CALL `0x48bcb7 → 0x497680`. Both use register arguments, unlike AP.** The existing
lens cull authentication at `0x48bc96 → 0x497720` covers selection preparation,
not the draw traversal. Retargeting AP's addresses alone is insufficient.

## Evidence and reproduction

Run from the checkout root:

```sh
python3 verification/results/fl-sun-occlusion/reproduce.py
```

The [producer](../../../verification/results/fl-sun-occlusion/reproduce.py) uses the
project PE reader/identity and boundary verifier, Capstone, the existing effective
FL resolver and AP asset resolver. Its [summary](../../../verification/results/fl-sun-occlusion/summary.json)
records **18 regions, 1,994 region instructions, 107 operand anchors, 42 decoded
region/seam edges, four rejected mutation controls** (measured). Overlapping
regions are counted independently, not as unique code. The direct caller census
finds one probe caller and three traversal callers; no surveyed direct branch
enters the three proposed seam interiors, and none contains a HIGHLOW operand.
Relative CALL targets must be recomputed if replayed from another address.
Adjacent contexts and other absolute image operands still require relocation.

These are linear-decode boundary/caller checks and targeted inspection, not a
whole-image decompilation, full CFG proof or exclusion of arbitrary computed
private calls. Raw listings stay in
`~/.cache/x3m-port/sun-occlusion/`; no raw code, geometry or shader payload is
tracked. No build, PE/native/Wine execution, game launch, installation or commit
was performed. Reuse the healthy native owner/callback closure established by
[material submission](material-submit.md#3-actual-borrow-readiness-writers-and-reentrancy),
[load readiness](ui-load-readiness.md),
[render exclusion](ui-render-exclusion.md) and
[mutation/recovery](ui-render-mutation.md).

## 1. Probe call, early gates and side effects

FL `0x48aeb0` corresponds to AP `0x4715d0`. It retains its current probing view
in EBX, walks ordinary holders except `engine+0x64`, and visits active owner views
whose signed layer `+0x29c` is at most the probing view's layer. Owner EDI and
record ESI are live in that traversal. At `0x48af00` a zero record visibility word
skips the call. `0x48af06/08` supply **EDX=probing view, ECX=record**;
`[0x48af0a,0x48af0f)` is one five-byte relative CALL. The continuation tests EAX
and, when nonzero, writes literal zero to `record+0x30` at `0x48af13`.

Target `0x4a3fd0` has **two register arguments, no stack arguments, plain RET**.
This is a fastcall-shaped private engine convention; do not cast it to AP's
`__cdecl(record,view)` or issue caller cleanup. It aligns its own temporary stack
to 16 bytes and saves EBX/ESI/EDI/EBP. The site itself guarantees only four-byte
incoming alignment. Unlike AP, the caller's EBP is the holder cursor, not a
constant zero. EBX is the probing view, ESI the record, EDI its owner view; all
four are live across the call. EAX is the boolean result. ECX/EDX and incoming
arithmetic flags are dead; `test eax,eax` defines the flags consumed by the
branch. Preserve native EAX for forwarding paths. A shim must restore EBP's
value and exact native ESP even if using an internal frame pointer.

The three gates have the same ordering and integer semantics as AP:

| FL evidence | Result |
| --- | --- |
| `0x4a3ffa`, `[*0x5a83b0 + 0xec] & 0x8000` clear | EAX=1, hidden |
| `0x4a4012..0x4a4051`, signed inclusive rectangle: `x+0x8000` versus view `+0x290/+0x294`, `0x8000-y` versus `+0x288/+0x28c` | Outside returns EAX=0, visible to this probe |
| `0x4a4057/61`, view `+0x270 & 0x08000000` | EAX=1, hidden |

The complete prologue/gate interval is `[0x4a3fd0,0x4a4063)`, **147 bytes**,
FNV-1a64 **`0x1abcc319ce5e4445`** (measured). FL configuration offset is **`0xec`**,
not AP `0xfc`; its pointer slot RVA is `0x1a83b0`. Signed wrapping for the screen
coordinate offsets and the rectangle-before-hidden-flag order must be retained.
The prologue first reads view+0x1c and engine+0x6c; it performs no native-memory
writes before these gates. An eligible replacement can answer without those
otherwise unused scene/probe-node reads.

After the gates, native code projects the record back into the probing camera
and mutates the shared body-195 probe at `engine+0x6c`: scale, position,
orientation/basis and transform flags. It lazily builds the view's 256-word
candidate cache at `+0x37c`, null-terminating with a count capped at 255
(`0x4a4259..0x4a42ad`, recursive builder `0x4a42e0`). It then calls narrow phase
`0x4a43d0`. That helper resets collision scratch at `+0x180/+0x184/+0x188/+0x18c`
on **both** the probe and candidate before `0x4a6270` (`0x4a44ce..0x4a4534`).
Thus bypassing the probe skips more than a boolean computation. It does not
itself write `record+0x30`; the caller owns that write.

The per-view candidate cache is reset immediately before this probe pass at
`0x48bb7e`; only the native probe/builder use it on the surveyed path. The shared
body-195 node also serves the cockpit boom. FL `0x422616` obtains it, rewrites
basis/position (`0x422632..0x422671`) and sets its scale before every sweep
(`0x422695`). This independently confirms that the cockpit does not require the
flare probe's previous pose. Collision scratch is initialized by the flare narrow
phase on each actual test. No need to reproduce those mutations in the visible
replacement has been identified in this supported renderer/boom closure.
This is not a whole-engine claim that unrelated private callers can never inspect
collision scratch. Ineligible calls must always execute the native function
exactly once, preserving its side effects. Diagnostic native comparison must
also run it at most once and keep its answer when fallback is selected.

**FP and errors.** FL native probe includes SSE **moves**, e.g. `movups xmm0` at
`0x4a4221`; copying AP's claim that this function never touches SIMD is false.
No x87 or MXCSR-setting instruction appears in the selected probe/builder/narrow
regions. The replacement decision can remain integer-only, with no SSE
arithmetic, CRT math or MXCSR changes. Any helper envelope must leave native
FP/SIMD state intact; the lens pre/post path needs full x87/XMM/MXCSR preservation.
LastError has no consumer adjacent to either seam, but helper/OS/COM work must
preserve the caller's incoming error and the native target's output error as
appropriate. Preserve EFLAGS around helpers and clear DF for C entry; resume
native with its original input state. No native SEH registration occurs in the
probe's own prologue. A native fault remains a foreign exception, never a
replacement hidden/visible answer. Publish no half-copied latch on a read fault;
explicit abnormal cleanup must retire any scope or state already entered.

## 2. Record, camera rectangle and source size

The FL collector retains AP's lens-record offsets and shape. Allocation at
`0x497259` requests **0x70 bytes**, `0x4972dc..0x49730b` links it to the owner
view, and destruction `0x48b5b3..0x48b5db` frees **13** body slots and the record.

| Field | FL evidence |
| --- | --- |
| Next/backlink `+0/+4`, owner `+8`, source `+0xc` | `0x4972dc/df`, intrusive link stores through `0x49730b`; owner list `+0x2a0/+0x2a4/+0x2a8` |
| Accumulator `+0x10`, visibility `+0x30` | Allocated zero at `0x4972e2`; body update `0x48aff6..0x48b012` changes by ±100 and clamps 0..200 |
| Screen x/y `+0x20/+0x24`, z `+0x28` | Stores `0x4974fb/0x49753b`, z=0x8000 at `0x49753e` |
| Size `+0x34`, group `+0x38` | `0x49746c`; source `+0x1a0` copied at `0x4972e9/ef` |
| Instantiated body slots `+0x3c..+0x6c` | Destruction initializes count 13 at `0x48b5b8` |

The same view fields apply: flags `+0x270`, top/bottom `+0x288/+0x28c`,
left/right `+0x290/+0x294`, full binary-angle FOV `+0x298`, layer `+0x29c`,
normalized scale pair `+0x300/+0x304`. Collector admission requires source
`+0x12c & 0x20000000` and owner view `+0x270 & 0x100` (`0x4970d9/e9`).
If either explicit scale is nonpositive, **both** native scales come from
`[*0x5a82f8 + 0x28/+0x2c]` (`0x497205..0x497230`, probe independently
`0x4a416a..0x4a4187`). These are normalized fixed-point scale values, not pixel
sizes; do not substitute camera/context world scale. A new sample may leave the
derived radius unknown and use the existing configured radius, avoiding a new
mode-global borrow. AP's diagnostic scale fallback is not an FL pointer.

FL uses the same half-NDC 16.16 screen frame, x right/y up, independently fixed
by the collector stores and inverse probe tests:

`u = 0.5 + x/65536`, `v = 0.5 - y/65536`.

The collector uses view FOV LUT tan/cot and the explicit/fallback scales. Its
screen-size path has the same source size `+0x70`, distance `+0xf8`, shaping term
`+0x1b0`, rectangle mean extent and accumulator factor `/200` as AP. FL emits
rounded fixed-point multiplies (`add 0x8000; adc; shrd`); the shared floating
footprint is a derived estimate, not a bit-exact emulator of those intermediates.
The configured absolute disc radius remains an explicit proxy policy. AP's
measured always-saturated sun size is **not** a new FL flight measurement.
Record lifetime is bounded by collection/removal/destruction, not by Present;
retain owned values and generation-stamped identity labels only.

## 3. Actual draw traversal and exception-safe bracket

The lens block is ordered:

1. `0x48bc69 → 0x48af40`: update/create lens body nodes.
2. `0x48bc78 → 0x494b00`, `0x48bc87 → 0x495670`: propagation and lens camera activation.
3. `0x48bc96 → 0x497720`: selector preparation, including `0x497745 → 0x495e50`.
4. Layer preparation `0x48bca9 → 0x4978e0`, sort `0x48bcb0 → 0x4975c0`.
5. **`0x48bcb7 → 0x497680`: synchronous body draw traversal.**
6. `0x48bcc9 → 0x4e7310`: separate script-controlled 2D overlay from engine `+0x6304`.

The draw seam `[0x48bcb7,0x48bcbc)` is a single five-byte CALL, immediately after
`mov edx,esi` at `0x48bcb5`. Target `0x497680` takes **EDX=view, no stack
arguments, plain RET**, preserving EBX/ESI/EDI and leaving EBP untouched. There
is no caller stack cleanup. The internal `push ecx` at `0x49769b` supplies
padding, not another traversal input: instance helper `0x4de570..0x4deb8e`
reads its sole incoming stack parameter at `[ebp+8]` (`0x4de57c`), never the
extra `[ebp+0xc]` word. ESI is the lens view and live in the continuation;
EBP is the frame routine's frame pointer, and EBX/EDI remain preserved. EAX is
dead at this site, reloaded at `0x48bcbc`; ECX/EDX are replaced at
`0x48bcc1/c3`. Incoming/result arithmetic flags have no consumer before later
native definitions. A replacement must nonetheless preserve complete input and
native output state through bookkeeping, rather than relying on incidental
scratch-register availability.

The traversal walks engine `+0x40`; `0x4976a0 → 0x4de570` prepares an instance,
then `0x497702 → 0x4e6ef0` calls the material wrapper. Its main call is the
already owned material seam `0x4e7142 → 0x4e0f30`, followed by ordinary return
at `0x4e7161`. This is the established synchronous native material submission
path, so pre/native/post at the lens **callsite** encloses those lens-body
submissions. The extra 2D overlay is outside it. No new deferred engine draw
queue was identified. The traversal has no indirect CALL; its material/D3D
closure does have native callbacks and failure/exception possibilities.

Direct callers of `0x497680` are `0x48ba8d` (ordinary), `0x48bcb7` (lens) and
`0x4978ab` (**cube face**, not an independently identified FL Clear helper).
Hooking the entry would affect all three. Bracket only the lens callsite.

The existing no-stack-argument compositor transport's CPU/foreign-SEH pattern
is suitable for this ABI, with a dedicated fixed binding/entry. Its current
singleton binding must not be repointed while another compositor invocation is
live. This does not require an observer registry. Keep per-invocation bracket
state and restore only the outer record scope after nested returns. Unsupported
nesting is denied before reads/changes. Do not let a nested draw or callback
inherit an ambient `bracket_open` flag without matching the owning device/pass.

Install must be quiescent, exact-image/profile gated and atomic as a transaction:
validate both call contexts, gate interval and entries, publish stable targets,
claim both sites, undo the first on second failure, retain arena/debt on failed
rollback, and restore only owned bytes on quiescent uninstall. Preserve full
GPR/EFLAGS/x87/XMM/MXCSR/LastError with four-byte incoming stack compatibility
and separate aligned storage. Pre exceptions do not run the native target;
native exceptions do not run normal post. Explicit foreign-SEH cleanup releases
references and closes only this bracket, invalidates readiness and restores
ambient cleanup FP/error state; Search/ContinueExecution preserves the scope.
Present is a final stale-state guard, not sufficient unwind cleanup. Reset,
load/owner loss, failed restore and device destruction revoke readiness before
resource release. No native pointer read or D3D restoration through invalid
objects during unwind, no lock retained across native/D3D calls.

## 4. Main identity and reuse boundaries

Reuse the already admitted **correlated Scene** camera key delivered by
`capture.cpp::fl_complete` to `MotionOutput::fl_camera_complete`, including
camera serial, complete-pass/load/observer/device/Reset/request generations.
At the eligible probe require the supplied EDX token to equal that main camera;
use the live EDI owner already carried by `0x48aeb0`, require record+8==EDI,
owner!=probing view, and owner flags `+0x270 & 0x400000`. A sector-camera bit or
matching dimensions alone does not establish main identity. No AP cockpit-global
or registry walk is needed. The record and both view operands have just been
consumed by native code under the existing full-pass borrow; use a bounded
call-free scalar copy and existing owned identity metadata, not page-readability
as a lease. Apply cheap main-view/owner inequality tests before owner payload
reads. Ineligible calls tail-forward unchanged, without allocation, logging,
D3D, generic dispatch, per-draw locking or native registry enumeration.

**Do not call `camera_state_fl::current()` at lens begin and reinterpret its
camera as the main view.** The lens activation at `0x48bc87` has replaced it and
retired current-camera dependents. Retain a separate owned, per-pass main-source
record at the established Scene correlation, validate it against the compositor's
same-frame finished Scene/target/depth domain, and consume it only under the
matching admitted lens invocation. It is a source value record, not permission
to reread the old camera. Do not weaken `camera_state_fl::read` to accept stale
activations. This explicit Scene-source-to-lens edge is implementation work.
Similarly, readiness across frames must key record/source/owner identities and
load/device epochs, not a bare record address that can be recycled. Existing
owned node/camera identity metadata can supply serials; do not add a second
lifetime observer. A missing identity or scene/depth correlation means vanilla.

| Reusable shared part | FL-specific work |
| --- | --- |
| Integer `eligible`/`decide` semantics and ordered gates | Register-ABI probe adapter, FL config+0xec, live-owner scalar borrow, correlated main key |
| `Ready`, `Hold`, scalar latch and footprint/jitter/smoothing math | Generation/identity wrapper and same-frame Scene-source-to-lens admission; no bare-pointer persistence |
| Body/blend classification, shader bytecode lens variant and visibility shaders | Actual submitted FL VS matrix-register and blend admission; no assumption every material is additive |
| Public D3D visibility pass, lens depth reference/restore and gain blend policy | Match the FL completed main RT2/depth generation and draw scope; explicit foreign-SEH cleanup and Reset retirement |
| AP host models and bounded failure policies | FL machine-code forwarding, relocation, rollback and state fixture; synthetic coverage remains separate from game behavior |

Shared renderer code is program-independent where it discovers/validates actual
shader structure. Authored first-nine bodies and their payloads match AP, but
this does not prove a live FL shader pair, number of suns, partial visibility,
clipped-core classification or draw blend law. Reuse `SunOcclusionPass`, lens
variant and gain state restoration after their existing capability checks;
do not claim FL runtime coverage from asset hashes. Native Windows must use the
same documented APIs; static/cross-compile evidence is not native Windows play.

## 5. Wide-FOV collector fix is a separate changed span

FL has the same horizontal truncated-bound failure opportunity. At
`0x497370..0x497380` it multiplies the previously computed rounded tan*z/2 by
normalized width, adds rounding, shifts the low part and also shifts EDX.
The patchable complete span is **`[0x49737c,0x497385)`, nine bytes**:
`SHRD EAX,EDX,16; SAR EDX,16; CMP ECX,EAX`. The branch at `0x497385` is JGE to
`0x497550`. AP's six-byte `SHRD;CMP` descriptor/stub tail cannot be copied.

A future saturating correction may reuse AP's arithmetic policy before replaying
**all three** FL instructions. EDX:EAX is the rounded product; ECX holds |x|/2.
For signed EDX>=0x8000 set EDX:EAX to 0x7fff:ffffffff so the replayed SHRD gives
INT_MAX. Otherwise replay unchanged. SAR EDX must occur exactly once; CMP flags
must reach the original JGE unchanged. Native then recomputes EDX for the y test,
so the corrected high word is not an extra retained input. ESP and its locals,
ECX, callee-saved registers, FP/SIMD and LastError stay untouched with a call-free
emitted stub. The earlier tan*z/2 can still overflow; this is the same bounded
policy limit as AP, not a general fixed-point rewrite. The separate seam has no
surveyed interior direct edge or relocation. Its 9-byte ownership/rollback and
positive/nonoverflow/negative/threshold arithmetic cases need their own fixture.

## 6. Nineteen names outside the cull policy

The effective FL resolver selects an authored Lensflares payload with exactly
the AP hash `c98373778d2046e78f74d5fcc0a891e966bafc981ac1e15f58b9b5c94ea2b0a3`:
**35 groups, 220 rows, 56 names**. TSuns is also byte-identical to AP,
SHA-256 `23bf07ef8efd04f101b4486acf299cd5ebd136957cc182802d13a58668593214`:
**11 entries**, authored groups **0..8 only**. This extends, rather than changes,
[the startup coverage result](cull-chain.md#2026-10-29-bounded-gain-zero-lens-startup-qualification).
All first-nine rows are covered by the current policy. None of the 19 excluded
names occurs in these authored TSuns groups (measured).

| Excluded body names (numeric suffix) | Authored groups | Interpretation from the identical table comments |
| --- | --- | --- |
| 00031, 00094, 00096, 00098, 00132, 00224 | 9 | Old standard lensflare. Body 31 also serves a separate in-scene sun fallback; name alone is unsafe sun admission. |
| 00083 / 00087 | 24 / 23 | Blue/red launch lens effects |
| 00084 | 25, 31 | Green launch effect and green wormhole; reused across roles |
| 00136 | 12 | Teladi engine effect |
| 00143 | 14, 18 | Split engine/red laser |
| 00182 | 10, 16, 17, 22, 30 | Paranid/Xenon/rocket engine, yellow laser, warm yellow streak-sun; reused across roles |
| 00183 / 00184 | 13,20 / 15,21 | Argon/bright-blue laser and Boron/dark-blue laser |
| 00586 | 19 | Green laser |
| 00751, 00775 | 26 | Explosion glow, comment marks it unused at authoring time |
| 10791 / 12010 | 32 / 34 | Test groups; 10791 is absent in both effective trees |

All **18 existing** excluded body payloads resolve identically in AP and FL,
including binary bodies 00586/12010; `v\10791` is missing in both (measured).
Group 30 has a sun-themed comment and group 31 a wormhole comment, but neither
is a new FL group or selected by the authored TSuns table here. These are
AP-equivalent groups outside the existing intended sun policy, with mixed-role
reuse that argues against automatic expansion. This is an offline stock-table
comparison, not a FL runtime constructor/map/script census. Mods or dynamic
creation may select other groups. Leave the 42-name policy unchanged unless a
separate intended-policy decision and live identity evidence justify a change.

## 7. One bounded implementation brief and remaining acceptance

Implement one FL sun adapter, reusing the current fixed common/camera/lifetime
and renderer integration:

1. Add exact FL profile descriptors for the two register-ABI CALLs and gate hash;
   add a dedicated no-stack-argument, foreign-SEH-safe lens bridge and an integer
   probe adapter. Stage startup demand for gain/visibility independently of
   unrelated UI configuration. Preserve AP routing and existing gain-zero owner.
2. Publish an owned main Scene source key at the existing successful correlation.
   Copy eligible probe scalars under the live full-pass borrow, with cheap native
   input tests first. Reuse shared pure decisions/readiness with explicit
   record/source/owner serial and epoch invalidation. Keep bounded main-source
   values across the expected lens activation; retain no native-memory lease.
3. At the qualified lens call, bind the exact device/completed Scene depth domain,
   execute the existing visibility pass and gain route, and close or abnormally
   retire this invocation. No general observer, per-draw allocation/lock, AP
   global fallback, broadened name policy or shader-layout guess.
4. Keep the wide-FOV nine-byte correction as a separately gated profile claim in
   the same reviewable feature slice, using its already understood arithmetic
   policy and complete FL replay. If deferred, report it explicitly as unsupported.

The owner should choose affected host checks and one joined native synthetic
fixture rather than an unrelated broad suite. The fixture must execute the
actual emitted adapters at two relocated bases, assert caller stack/register
transport and all GPR/EFLAGS/x87/XMM/MXCSR/LastError input/output preservation,
forward native exactly once, and cover second-claim failure/rollback debt,
foreign/nested denial, Search/ContinueExecution versus unwind, Reset/load/device
revocation, generation/identity mismatch, zero/multiple records, ordered gates,
missing completed Scene depth and lens activation between source and bracket.
Measure eligible/ineligible probe and once-per-lens overhead; keep the native
payload copy call-free. Restore-failure and read-fault cases must publish zero
usable stale state and never let an open bracket mark unrelated draws.

Static contracts are established above. Still unmeasured are the emitted FL
adapter behavior/cost, actual partial-sun eligibility and clipped-core shader
classification, blended draw coverage, multiple-sun behavior and user flight
appearance/loading cost. The existing native owner/callback closure is reused;
these finite adapter/domain checks do not require a new whole-engine observer
or scheduling reconstruction. Synthetic success will not constitute native
Windows gameplay or user FL flight acceptance.

Independent consequential review accepts this offline contract. It reproduced
all receipt fields except command and elapsed time, independently checked the
policy/resource comparison and verified the relative links and heading anchors.
No actionable mismatch was found; emitted adapter and live acceptance remain
the separate requirements above.

## 7. Completed-main source across later registered camera activations

The compositor seam is once-only **before** the first sorted view whose layer
is greater than 17 (`0x48b917..0x48b92e`). The loop then continues with that view:
ordinary activation is `0x48ba43`, or the flagged cube branch calls
`0x48b9f6`. The ordinary view's sun probe is later at `0x48bb96`, unless its
`+0x270 & 0x1000` excludes it. A second loop optionally activates each view with
nonzero `+0x77c` at `0x48bbef` and draws its script overlay at `0x48bbfc`.
Only afterwards does the dedicated lens activation run at `0x48bc87`.
Consequently, preserving a completed main source only during `special` origin
cannot follow the normal native ordering: the first late ordinary activation
would revoke it before the lens call.

The bounded producer
[`inspect_order.py`](../../../verification/results/fl-sun-adapter/inspect_order.py)
checks these exact predicates and direct call targets against the qualified FL
image and emits
[`order.json`](../../../verification/results/fl-sun-adapter/order.json), retaining
no native instruction bytes. Native ordering is measured; the private-depth
ownership argument below is source reviewed.

The source edge may retain only the separate finished main result across those
existing registered ordinary/cube/auxiliary/special camera scopes in the same
outer full-pass borrow, request, selected device and frame. Generic current
camera, material, Scene, shadow and TAA authorities still retire normally. A
later published camera key must agree on engine/map, observer/load/registry,
device/Reset and request epochs; a different view or Scene label is expected.
The lens consumer still requires its exact special callsite/key and the saved
main output/depth resource domain. Clear, failure, foreign/reentrant pass,
Reset, resource replacement and Present refuse the retained result.

`MotionOutput::scene_end_hook` restores private MRT bindings before setting
`hook_scene_end`. Both `scene_bound` and `fl_coverage_owns_draw` then refuse new
Scene/coverage routing for that frame. The native late views and script overlays
therefore cannot write the proxy-owned `depth_surface_` through these routes.
The completed-main snapshot is a resource/content result, never permission to
read its old native camera or to restore stale generic camera authority.

The activation's `0x49568e → 0x4db750` helper can issue an application Clear at
`0x4db7f8` (device slot 43): it requires nonzero view `+0x278` and color flag
`0x10`, or the `0x24` view flags with config `+0xec & 0x400`; config `0x200`
and view `0x800` can add stencil. The auxiliary loop clears flags `0x30` before
activation (`0x48bbdc/e0`), disabling its ordinary color/depth Clear. The
implementation keeps Clear as a hard source refusal. Which late ordinary or
special view flags occur in flight remains unmeasured, so retention coverage
through a later application Clear is not claimed.

A bounded default-initializer check narrows the latter uncertainty. Lazy special
scene setup `0x48ad90` calls view factory `0x4a4560` at `0x48adc3`, then attaches
the result with `0x4a5860` at `0x48addb`. The factory zeroes all `0x790` bytes at
`0x4a45d6..0x4a45e3`; its bounded direct-write census through return `0x4a4722`
contains no `+0x270` flag store. Attachment's sole direct flag write is OR 1 at
`0x4a58a7` when the scene is active. Thus this initializer sets none of the Clear
request bits above. `inspect_order.py` authenticates those anchors and the
constructor/attachment direct-write census. This is not a census of later
runtime mutations, loaded/modded flags or all late ordinary views; those remain
flight evidence needs and Clear remains a hard refusal.

## Verified hook sites and ABI (2026-11-01)

Image `/mnt/c/X3FL/X3FL.exe`, SHA-256 `e7f3aad8…dfbb8` (the same bytes the sections above
name as `X3FL-orig`, which is no longer present). Producer:
[`fl_sun_occlusion_constants.py`](../../../tools/analysis/fl_sun_occlusion_constants.py), which
decodes every claim below with Capstone as whole instructions, raises on any difference and writes
[`sun_occlusion_fl_inc.h`](../../../src/engine/fl/sun_occlusion_fl_inc.h) (17 anchors, 3 slots,
every value an RVA or offset). Host test:
[`test_fl_sun_occlusion_constants.py`](../../../verification/analysis/test_fl_sun_occlusion_constants.py)
(6 tests; the anchor mirror accepts each span relocated to `0x400000`, `0x4d0000` and `0x10000000` and
refuses every single-bit flip, a foreign base and an unreadable span; 16 byte mutations and the AP
image are refused). Reproduce: `python3 tools/analysis/fl_sun_occlusion_constants.py --check` and
`PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_fl_sun_occlusion_constants`.
Addresses below are RVAs. Raw listings were kept in the session scratchpad only.

**Probe CALL, RVA `0x8af0a → 0xa3fd0` (measured).** The walk `0x8aeb0` (AP `0x4715d0`) takes the
probing view in **ECX** (`mov ebx,ecx`), not on the stack; its only direct caller is `0x8bb96`
(after the cache reset `view+0x37c = 0` and the `view+0x270 & 0x1000` skip). Window
`[0x8af00,0x8af1a)`: `cmp [esi+0x30],0; je; mov edx,ebx; mov ecx,esi; call; test eax,eax; je;
mov dword [esi+0x30],0`. Target ABI: **ECX = record, EDX = probing view**, no stack argument
(no `[ebp+d]`, d ≥ 8, in its 220 instructions), RETs at `0xa4011` and `0xa42db`, both bare `c3`.
EAX is written before it is read in the straight-line prologue, ECX and EDX are read: two register
arguments, EAX the boolean (1 = hide). The target saves EBX/ESI/EDI/EBP and clobbers ECX/EDX.
At the site EBX = probing view, ESI = record, EDI = owner view, EBP = holder cursor, all live after
the return; ECX/EDX are dead; the continuation's `test eax,eax` defines the consumed flags. The
caller pushes four registers and does not align: the site guarantees four-byte ESP alignment
only. The CALL is one five-byte instruction; no base relocation touches it, no rel32 in `.text` and
no branch of the walk or pass lands inside it, no relocated word points at it or its target, and it
is the target's only direct caller. `0x8af0a % 8 = 2`, so `engine_patch::claim_call` writes it
with one `lock cmpxchg8b` at any 64 KB-aligned base. **Hook suitability: claimable.** A thunk must
keep ECX/EDX for the forwarding path (save them around the C decision, which receives them as
cdecl arguments), return EAX 0/1 with a plain `ret` and no stack cleanup, preserve EBX/ESI/EDI/EBP,
and forward with `popfd; jmp [target]` with ESP at the return address and ECX/EDX as on entry.

**Record+0x30 on FL is AP §13(c) unchanged (measured).** The probe target and its body contain no
store at displacement `0x30`; the caller writes literal 0 at `0x8af13` when EAX ≠ 0 (AP stores
EBP = 0 at `0x47163c`). Other writers: per-pass reset of every record of every holder view
`0x8b830` (= 0), collector `0x97549` (= 1). Readers: the walk's skip `0x8af00` and the accumulator
`0x8aff0` (±0x64, clamped 0..0xc8) in the body update `0x8af40`, which runs after every probe and
before the lens draw in the same pass. So the answer still reaches the picture only through
record+0x30 and the two-frame ramp.

**Early gates (measured; correction to the brief).** `[0xa3fd0,0xa4063)`, 147 bytes, file FNV-1a64
`0x1abcc319ce5e4445` (equals the fl-codex figure), masked (two slot words zeroed) `0x3318806b0f9a9fdf`.
Before the gates the prologue only reads view+0x1c and root+0x6c into locals. Gates, AP order:
config `[*0x1a83b0 + 0xec] & 0x8000` clear → EAX = 1 (`0xa4006`); signed inclusive rectangle on
`x+0x8000` against view+0x290/+0x294, then `0x8000−y` against +0x288/+0x28c, outside → EAX = 0
(`0xa42d3`); view+0x270 & `0x8000000` → EAX = 1. AP's override **does** replicate all three
(`core::decide` after `eligible`), which is why AP compares its 148-byte span whole; FL must
replicate the same three, reading the flags word at **+0xec** (the AP core constant
`config_video_flags = 0xfc` must not be used on FL). Everything after the gates (projection, body-195
probe pose, candidate cache, narrow phase) is skipped by an answer, as on AP.

**Lens CALL, RVA `0x8bcb7 → 0x97680` (measured).** Lens block `0x8bc54..0x8bcce`: root+0x64 holder
with +0x18 & 1, ESI = holder+0x1c (lens view; no instruction of the block writes ESI before the
site), body update `0x8af40`, propagation `0x94b00`, activation `0x95670`, selection `0x97720`,
layer `0x978e0`, sort `0x975c0` (EDX = view), **`mov edx,esi; call 0x97680`**, then
`mov eax,[root]; mov ecx,esi; mov edx,[eax+0x6304]; call 0xe7310` (2D overlay, outside the bracket).
Target ABI: **EDX = view** (`mov ebx,edx`), no stack argument (no ESP-based operand), never touches
EBP, one bare RET `0x97714`, 47 instructions, saves EBX/ESI/EDI. ECX is read only by `push ecx` at
`0x9769b` as the second word for the cdecl instance helper `0xde570`, which reads only `[ebp+8]`
(the view): an unread padding word. After the return EAX, ECX and EDX are written before any read;
ESI is read. It walks root+0x40, the draw list selection filled for the active (lens) scene, and per
entry calls the instance helper and the material wrapper `0xe6ef0`: **every selected lens body of
every record**, not one record's chain, and **no record pointer is in a register** at the call.
Per-record attribution must therefore come from the probe latch (the single eligible record), as on
AP, whose site also passes only the view. Direct callers of `0x97680`: `0x8ba8d` (ordinary view),
`0x8bcb7` (lens), `0x978ab` (cube face): hook the call site only. **`0x8bcb7 % 8 = 7`: the five bytes
straddle a qword**, so `claim_call` takes its plain-copy path; claim it only inside the install
window, before the render thread runs the pass. **Hook suitability: claimable**; the bracket thunk
saves ECX and EDX before `begin` and restores both before `call [target]` (EDX is the argument; ECX
only keeps the padding word identical), opens the bracket before and closes it after the original,
with EFLAGS saved and DF cleared around the C handlers, and returns with a plain `ret`.

**Record and view layout (measured): the AP core's offsets hold.** Record 0x70 bytes (`0x97259`):
next +0, prev +4, owner +8, source +0xc, accumulator +0x10, x +0x20, y +0x24, z +0x28 (= 0x8000),
visible +0x30, size +0x34, group +0x38. View: flags +0x270, rect top/bottom/left/right
+0x288/+0x28c/+0x290/+0x294, fov +0x298, layer +0x29c, records +0x2a0 (tail +0x2a4, tailpred +0x2a8),
scale +0x300/+0x304 with fallback `[*0x1a82f8 + 0x28/+0x2c]` when either is nonpositive, cache +0x37c.
The host test checks each against `src/proxy/sun_occlusion_core.h`. Only the config flags word
moved (+0xfc → +0xec); slots are RVAs `0x1aa100` (root), `0x1a83b0` (config), `0x1a82f8` (mode).

**Thread and order (measured unless marked).** The complete pass `0x8b6d0` has one direct caller,
`0x782cf` in the main loop, the thread that also issues the frame's D3D work (the scene hook's
thread). Inside it: record reset `0x8b830` < sorted-loop head `0x8b917` ≤ compositor CALL `0x8b929`
(once, before the first view with layer > 17) < probe walk `0x8bb96` (each view, after its draw)
< loop back edge `0x8bba7` < auxiliary overlay loop < body update `0x8bc69` < lens CALL `0x8bcb7`.
Both seams run inside the loop/pass that owns the compositor, so the main view's probe precedes the
compositor if the main view's layer is ≤ 17 (inferred from the scene-hook design, not checked
here), and every lens draw follows the compositor CALL, as on AP. If no view above layer 17 exists
in a pass, the compositor does not run in that pass and the lens draw has no preceding write-back
in it. Reentrancy: the walk, the pass and the probe target have one direct caller each; indirect
re-entry is not excluded by this census (as for the scene hook).

**Settled against the sections above.** All fl-codex RVAs, the register ABIs, the 147-byte FNV,
the record layout, the EBP-is-holder-cursor difference and the padding word are confirmed. New: the
lens CALL straddles a qword (non-atomic patch), the walk itself is ECX-ABI, and AP's override does
replicate the three gates (FL needs +0xec). Not checked here: the absence of x87/MXCSR instructions
in the probe closure, the 9-byte wide-FOV span (§5) and the cube/Clear findings (§7).
