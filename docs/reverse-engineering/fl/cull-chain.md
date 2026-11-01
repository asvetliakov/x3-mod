# FL shared cull/LOD chain contract

Bounded offline reconstruction, 2026-10-29. Preferred VAs use base `0x400000`.
Input: clean `/mnt/c/X3FL-orig/X3FL.exe`, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
**[s]** means measured static evidence; **[i]** means inference or implementation
requirement; **[u]** means unestablished. No production source edit, PE build or
execution, Wine, game launch or installation. This qualifies a bounded native
binding contract; it does not claim an implemented or runtime-qualified FL hook.

**Retain one shared claim and the existing three-consumer chain.** FL changes
the displaced span, register assignment, stack locals and model field. It does
not require another dispatcher, writer, lifetime owner or policy layer. In
particular, FL has **two different size metrics**: the live native measure in
ECX and the pixel-policy metric at site `[ESP+0x10]`. Substituting ECX for both
would change existing AP policy.

Owning policy and previous findings:
[AP LOD/cull selection](../lod-selection.md),
[FL engine effects §3](engine-effects.md#3-size-rejection-and-far-records),
[AP occlusion site](../engine-side-occlusion-cull.md),
[FL material fields](material-submit.md#2-exact-finite-field-set),
[FL body registration](engine-body-inputs.md), and
[FL shared render admission](../../architecture/farnhams-legacy-shared-render-admission.md).

## 1. Reproduce and evidence limits

```sh
python3 verification/results/fl-port/cull-chain-contract/produce.py
```

The [producer](../../../verification/results/fl-port/cull-chain-contract/produce.py)
uses the project's `tools/analysis/inspect_x3.py` PE32 reader and Capstone;
[contract.json](../../../verification/results/fl-port/cull-chain-contract/contract.json)
contains derived facts and source hashes only. Measured [s]: **14 region hashes,
81 instruction/operand anchors, 407 complete selector instructions, 466 CFG
edges, 21 rejected mutations, 12 tail/redirect/restore relocation simulations,
180 native-verdict and 180 forced-cull interpretations**. Mutations bypass region
and image hashes and exercise actual operand/boundary assertions. The selector's
entire intraprocedural graph is reachable; every branch and fallthrough has a
complete instruction boundary. Calls are modeled as returning to their next
instruction, with recursion separately identified; this is not a proof of every
callee's effects.

The decoded `.text` census contains **72,241 direct control-flow edges**. It finds
only `0x495f37` (self recursion) and `0x497745` (scene walker) targeting selector
entry, no external direct edge to its interior, and no decoded edge into
`(0x4961d3,0x4961dc)`. A separate byte-position rel8/rel32 candidate scan finds
zero window-interior targets; an unaligned DWORD census of the entire image
finds zero window-interior addresses. These finite scans do not exclude computed
targets, alternative x86 decodes or third-party patches [u]. The synthetic
relocation checks move only copied tails/redirects, including two image deltas;
they do not claim loader rebasing or execute code. Raw instructions remain local
and untracked.

## 2. Complete selector and native reject continuation

Selector **`[0x495e50,0x496446)`** receives ECX=node, first stack argument=camera,
second=propagated fixed-distance-LOD flag. It saves EBP/EBX/ESI/EDI, aligns its
local stack to eight bytes, and returns with **RET 8** at both `0x495ed0` and
`0x495f49` [s]. The self call pushes propagated flag and camera at
`0x495f33/34`; the scene walker pushes zero and camera at `0x497740/42`.
The scene walker receives ECX=scene/context and one camera stack argument.

| Selector region [s] | Behavior and control-flow consequence |
| --- | --- |
| `0x495e50..0x495f4b` | Initialize selected LOD `+0x150`; clear transient `+0x130 & 0x180000`; hidden/parent-dependent/behind-eye checks. Early returns or shared child loop. Child loop `0x495f24..49` reads node `+0xc`, visits next `+0`, recursively calls selector. |
| `0x495f4c..0x495f58` | If `node+0x12c & 0x4000002 == 0x4000002`, return before the shared site. This bit is in **+0x12c**, distinct from JET's bit 26 in **+0x130**. |
| `0x495f5a..0x495fe7` | Camera-inside/model test, frustum call `0x495fd3 → 0x4e9000`; rejection clears renderable bit 2 and goes to children. |
| `0x495fe8..0x4960c6` | Aggregate-radius helper `0x4a39b0` (RET 4), distance calculation via SSE and `0x412a80`, view-distance check. Model `<0` or renderable bit clear goes to children. |
| `0x4960c7..0x4961d2` | Focus-scaled depth, native and pixel metrics, transient flag classification and environment-map native-measure zeroing. |
| `0x4961d3..0x49621b` | Own/parent threshold, minimum-measure rejection and bit-26 escape, detailed below. |
| `0x49621c..0x496441` | Model lookup `0x4a1a40`, adaptive LOD factor, propagated distance ladder or metric ladder, view-quality adjustment/clamping; publish `+0x150` and renderable flags; return to child loop. |

**Shared native cull target is `0x4961f3`, not the child loop.** Its AND clears
`node+0x12c` bit 2, then `0x4961fa → 0x496201` reloads the flag word into EAX.
If bit `0x4000000` is clear, it stores the cleared word and jumps through
`0x496217 → 0x495f24`, preserving recursive child visitation [s]. If bit 26 were
set, `0x49620c` could reach model/LOD selection and later `0x496425` re-arm bit 2.
Under stable native fields, that escape is excluded at this site: the
bit-26/renderable early exit above and the later renderable gate at `0x4960c1`
both dominate the site; no intervening **selector instruction** sets bit 26.
The producer checks this intraprocedural fact and the relevant operands. It
does not establish field immutability across earlier calls. In particular,
`[0x4e9000,0x4e94dd)` has 296 decoded instructions, no direct non-stack store
and **ten calls**, including model lookup, four D3DX imports and a device
vtable call at `0x4e91d9` (GetViewport slot +0xc0). Model lookup can enter
resource work; no blanket no-reentry/no-throw contract follows. The exact
native reject path intentionally retains the escape if the flag changes.
Thus the claim “reject skips LOD” depends on stable native fields/current
admission, not merely dominance or readable pages [s/i/u]. AP has the same
escape and corresponding early exclusion (`0x47d0a9`); preserving its native
path preserves common behavior without adding a new bit guard.

Do not jump directly to the child loop or clear unrelated flags. The existing
native path preserves behavior for all shared consumers, including loaded nodes,
children and environment-map views. Offscreen/frustum-rejected nodes never
reach this site; this contract does not close the separate node-nozzle sourcing
gap identified in the engine-effects note.

## 3. Shared entry, stack and CPU state

| Binding fact [s] | AP | FL |
| --- | --- | --- |
| Site / replay span | `0x47d2a2`, 5 bytes | **`0x4961d3`, 9 bytes** |
| Displaced outputs | ECX=parent, TEST parent | **EAX=parent; EDX=own `+0x1d8`** |
| Tail continuation | `0x47d2a7` | **`0x4961dc`**, TEST EAX,EAX |
| Native reject target / child loop | `0x47d2c3` / `0x47d528` | **`0x4961f3` / `0x495f24`** |
| Node / native measure | EDI / ESI | **ESI / ECX** |
| Pixel-policy metric at unmodified site ESP | `+0x2c` | **`+0x10`** (duplicate at `+0x18`) |
| Camera extraction | site `[ESP+0x28]` | **`[EBP+8]`**; EBX also holds camera here |
| Model / selected LOD | node `+0x140/+0x14c` | **node `+0x144/+0x150`** |

The nine bytes are two complete loads: `EAX ← node+0x18` (3 bytes),
`EDX ← node+0x1d8` (6 bytes). They contain no address-relative operand to fix up.
Two explicit site-entry branches are `0x4961b9` and `0x4961c8`, plus fallthrough
from `0x4961cc`. The tail must replay both loads **once**, then continue at
`0x4961dc`; the subsequent native TEST regenerates status flags. An AP five-byte
tail would split the FL second load [s].

**Chain ABI:** EAX and EDX are scratch before the replay. **ECX is live**, and
must survive every non-culling stub and any far-handler call. ESI=node,
EDI=focus-scaled depth, EBX=camera, EBP=frame and ESP/native locals must retain
their incoming values on continuation. No chain stub gets a return address;
it is entered by JMP and forwards by JMP through its next slot. Intermediate
stubs must not replay the displaced loads on a continue path: let the single
shared tail do that. A reject may replay the native parent/max-threshold block
using EAX/EDX, as AP currently does, then JMP `0x4961f3`. EAX/EDX are overwritten
before use on its actual child-loop path; preserving the native replay shape
avoids introducing an unnecessary policy difference [s/i].

Incoming arithmetic status flags are dead: both the continue TEST and reject
AND overwrite them before any consumer. Control flags must remain unchanged.
The selector has **zero x87 instructions** and performs SSE arithmetic, but that
alone does **not** prove an empty incoming x87 stack. Keep x87, XMM, MXCSR and
LastError preserved. Existing lens/occlusion stubs use integer instructions
only. Keep the finite far-copy body integer-only; fresh FL admission is a
separate transport/state-preservation obligation. The current admission query
calls GetLastError/TlsGetValue/SetLastError and copies BorrowStamp, so calling it
inside the existing no-Win32/no-SIMD helper unchanged is not qualified. Audit
and fixture its actual CPU-state boundary before admitting the FL far route.
Save volatile EAX/ECX/EDX around that cdecl call, pass `(node=ESI,
measure=ECX,camera=[EBP+8])`, remove 12 argument bytes and restore exact ESP.
Native legacy callbacks only promise four-byte incoming alignment; shared C++
helpers retain the project's stack-realignment flags [i].

After the native prologue and balanced helper calls, site
`ESP = align_down(EBP,8)-0x28`; deriving camera from a fixed site-ESP slot would
therefore depend on EBP's alignment. **Use EBP+8.** The pixel metric remains a
local site-ESP offset and must be adjusted for temporary pushes. The helper's
stack argument at `0x495fe8` is consumed by `0x4a39b0`'s RET 4; the argument at
`0x496132` is removed by `0x496138 ADD ESP,4` [s].

The pass recurses but the site executes before entering each node's child loop.
Stateless stubs and bounded copies need no recursion guard. A pure far copy
makes no native/COM/VM call, cannot recursively enter the selector, and must
not retain per-invocation scratch globally. Existing frame buffers/counters are
owner-thread state; keep current pass/view identity and capacity rules. Any
unexpected owner-thread mismatch denies new sampling before native reads;
it does not become a writer-exclusion proof. On foreign unwind, existing
full-pass admission cleanup owns closure; these stubs must add no new held
lock, scoped native ownership or cleanup obligation. This study does not
qualify a generated FL handler's exception behavior [u].

## 4. Metrics, markers and finite reads

At `0x496113/19`, `*0x5aa100` supplies a native reference width at manager
`+0x5c`. Helper `0x412b50` takes ECX=radius, EDX=that width, one stack argument
D, and returns signed integer `radius*width/D` (zero divisor returns zero).
The result is saved at `[ESP+0x14]` and loaded into ECX at `0x49617a`; near,
invalid-for-size or `+0x12c&0x400` paths substitute `0x7000000` [s]. Separately,
`0x496155..0x496182` computes `radius*640/D`, saturates at `0x7000000` for
D<640, substitutes 1 for a zero quotient, and stores it at **`ESP+0x10/+0x18`**.
D is max of signed node view-position components `+0xf0/+0xf4/+0xf8`, scaled by
camera `+0x298 / 0x4000` when below `0xfffffff`, matching AP's focus law [s].
Thus shared pixel-to-threshold, projection focus derivation and scene latch
policy remain reusable; the FL adapter supplies correct owned camera
projection/focus data. Do not read a last/current native camera at Present.
Environment-map `0x4961ca` zeros **only ECX**, retaining the pixel metric, also
as AP does with its separate native/pixel metrics.

The native threshold remains signed `max(own+0x1d8,parent+0x1d8)` when parent
exists: positive threshold with ECX below it, or ECX<1 without +12c bit 26,
rejects. Existing `engine_culls` takes **ECX**, not the pixel local. The
180 interpreted native cases include null parent, negative/zero/positive
thresholds and measures; the forced-reject cases all reach children with bit 2
clear when the dominating bit-26 invariant holds [s].

| Consumer | Exact FL read / policy boundary |
| --- | --- |
| Small parts / dock parts | Pixel local `ESP+0x10`; model `+0x144` only in dock range; flags `+0x130` only below the selected threshold. Text and binary inline-body arithmetic at `0x4ae169..17b` / `0x4ad62d..639` retains `local+(cut-1)*100000`; this supports the existing dock-ID range rule, not a new FL asset census. |
| Projectile exemption | Class-0 dispatch maps to `0x449aee`; `0x449b1d` stores `0x20800000`, common root publication `0x44a807..0d` ORs it into `root+0x130`. Guard `0x44a6b9/6bc` restricts that publication arm to class 0. A class-7 subtype `0x113` assignment at `0x449f7d..96` is **not** another node marker: generic publication `0x44a824..857` bypasses the sole local-word read at `0x44a80a` and only publishes +12c. Keep the established bit exemption policy; this is not a census of every writer. |
| JET / far capture | Classifier `0x43deb3` sets `+0x130 |= 0x4000001`; SMALLJET `0x43dede/0x43dee8` sets bit 26 and `+0x1d8=5`. Distinct from +12c bit 26. Keep the existing JET pair, RCS/SMALLJET admission, `engine_culls`, caps and deduplication; use FL model `+0x144`. |
| Lens-flare bitmap | Read node `+0x144`; use EAX/EDX scratch, preserving live ECX. Lens scene `[*0x5aa100+0x64]` reaches the **same** walker/selector through `0x48bc8c..96`; preparation/publication precedes it at `0x48bc78/87`. Body-table offsets `+0xb4/+0xb8/+0xbc`, stride `0x1c`, name `+0xc` are established by the FL body-input note. Name resolution is outside the node hot path. The existing AP/Mayhem 42-name set is policy, not independently proven complete FL flare coverage. |
| Occlusion table | Hash ESI node token, require actual camera `[EBP+8]` to match published view, compare FL model `+0x144`, frame stamp and node view-position `+0xf0/+0xf4/+0xf8`. Use EAX/EDX scratch; preserve ECX. Keep existing bounded four-probe table, position window, retest and counter policy. FL's actual ledger/submission node/view agreement still needs adapter/runtime acceptance; AP's measured 100% join is not an FL measurement. |

**Finite far-copy read set:** node parent `+0x18`, handle `+0x28`, scale
`+0x70/+0x80/+0x88`, effective position `+0xb0..b8`, basis-x `+0xc0..c8`,
basis-z `+0xe0..e8`, flags `+0x12c/+0x130`, model `+0x144`, threshold
`+0x1d8`; optional parent's threshold `+0x1d8`; camera handle `+0x28` and
context **token** `+0x1c`. Existing FL material/engine-effects notes establish
these field meanings. The current node and camera are the native selector's
synchronous arguments; native own/parent threshold loads immediately follow
the seam. No call lies between the seam and these native loads. A finite,
call-free copy here can reuse that exact borrow under **fresh existing**
`fl_render_admission::current` and owned node/camera identity binding [s/i].
This names the required admission, not a proven pure-helper implementation:
its query transport and CPU-state preservation remain to be qualified. It does
not authorize an arbitrary pointer chase or bind a stale frame token.
Foreign writer exclusion and ready owner admission remain the existing
renderer contract, not a property inferred from readable pages [u/i].

The copied node, parent and context addresses remain identity tokens. A later
far-to-plume conversion must use the matching **owned context scale** from
camera publication, not dereference saved camera+0x1c or context+0x2c. A later
parent-radius read also needs an independently valid borrow or an owned value;
this hook does not grant it. The current 72-byte AP `Raw` therefore needs only
its existing FL input adapter/owned camera association, not an unrestricted
later native read. The root/list/no-stage fallback gap is unchanged. Denied,
stale, overflowing or unwound samples must leave no newly published partial
record; no new native sample is a valid failure outcome [i].

## 5. Minimal binding, installation and performance

Keep shared policy and chain ownership in their current logical modules. Bind
a small AP/FL site description: verified surrounding native window, entry,
saved span, native continuation/reject target, node/native-measure extraction,
pixel local, camera argument and model offset. FL native stub encoders map AP's
two scratch registers to **EAX/EDX**, node to ESI and preserve ECX; this avoids
extra per-node saves for the lens and occlusion consumers. It is a fixed edition
binding selected before install, not a per-node edition switch [i].

Existing `engine_patch::SiteSpec` supports 16 saved bytes. FL uses length 9,
RET-pop metadata 8 and rel32 offset 0. **The claim writes only five redirect
bytes**, `[0x4961d3,0x4961d8)`, inside aligned qword
`[0x4961d0,0x4961d8)`: existing atomic CMPXCHG8B path applies. The remaining
four native bytes stay in place but are unreachable through that redirect.
Existing rollback and restore likewise write only the original first five
bytes, so no nine-byte plain writer is needed. Tail size is nine copied bytes
plus five-byte JMP to `0x4961dc`; simulated addresses include distant arena
locations and alternate image deltas [s].

Reuse startup quiescence, DLL pin, exact local/marker checks, emitted arena
lifetime, readback/protection/flush debt rules, one push-front chain and one
owned restore. A failed first chain insertion restores vanilla; a later
insertion failure leaves the already live head. Shutdown disarms consumers
before the owner restores; do not free arena entries an active thread could
still execute. The latest installed stub runs first (existing normal order:
occlusion, lens, small parts, native tail). A culling consumer bypasses later
ones as today. Preserve that observable interaction and the single claim even
when small-parts pixels are off but another consumer requests the site [s/i].

Measured source bounds [s], not FL timings: disarmed consumer cost remains one
integer compare/branch plus chain jump; no new allocation, lock, per-node
string scan, body registration or registry lookup is required by the policy.
Whether fresh admission needs a bounded CPU-state envelope remains an
implementation question, not established zero cost. Lens uses its existing bitmap; occlusion at most four probes. Far
copying occurs only for selected below-threshold JETs and uses the existing
capacity 1024, not every node. Publish projection thresholds and identity-bound
owned camera association outside the per-node path; reject absent admission
without page walks. The FL tail adds one native load relative to the AP saved
span, but that load was already in the native continuation [i]. Actual FL
performance, generated-stub CPU/LastError/SEH checks, install failure/rollback
execution, Reset/recovery, all consumer chain orders and native Windows behavior
remain acceptance work. No additional production mechanism follows from this
static contract.

## Independent review (2026-10-29)

Independent review reproduced the compact record exactly and accepted the
bounded static contract after qualifying the admission-query CPU boundary above.
No emitted-stub execution, full chain integration or performance acceptance is
implied. The existing native rejection path and callback/reentrancy limitations
remain part of the implementation contract.

## 6. Scene projection publication precedes selection — 2026-10-29

The successful correlated FL scene Clear can publish the small-part and dock
thresholds before this view's native selector runs. The finite ordinary path is
`0x48ba43 -> 0x495670`, then matrix setter call `0x495687 -> 0x4ded90`, then
Clear-helper call `0x49568e -> 0x4db750`. The helper calls D3D9 Clear through
vtable `+0xac` at `0x4db7f8` with count zero, null rectangles, flags in ESI,
depth 1 and stencil zero. Only after ordinary origin `RET 4` at `0x4957a2`
does its caller reach `0x48ba4c -> 0x497720`, whose `0x497745` call enters
selector `0x495e50` and eventually shared cull `0x4961d3` [s].

The [targeted producer](../../../verification/results/fl-port/cull-projection/verify_ordering.py)
and [derived record](../../../verification/results/fl-port/cull-projection/ordering.json)
check five whole-instruction regions (**171 instructions**), **31 operand/edge
anchors** and **31 refused anchor mutations**, using the clean image hash above.
This adds no raw disassembly or bytes to version control. The helper tests the
Clear HRESULT; the ordinary origin ignores the helper result and proceeds with
its other native work. The helper can also return without issuing Clear.
Therefore absent or failed Clear leaves the cull thresholds disarmed, even
though native later reaches selection [s/i].

The integration uses existing cull policy and width ownership: successful scene
completion converts the already validated owned P[0]/P[5] to focus and applies
both configured thresholds immediately. FL frame begin disarms/count-resets;
there is no AP live-camera or registry fallback. The camera's fixed dependent
retirement zeros the two scalar cull words before its optional sun callback.
Next activation, unsupported nesting, invalid/abnormal return and consumed
invalidation requests retire the projection; normal valid origin return retains
it for the selector shown above. Invalid/non-scene compositor completion and
Reset also disarm. No cull stub, saved span, CPU envelope, shared claim, rollback
or arena lifetime changes. Lens bitmap publication and occlusion's owned name,
ancestry and node/view-position join remain separate acceptance work.

## 2026-10-29: bounded gain-zero lens startup qualification

The early FL transaction now selects the existing lens consumer before Engine,
independently of renderer/TAA/UI/Engine demand. The adapter authenticates the
15-byte traversal at `0x48bc8c`, 10-byte walker/selector call at `0x497740`,
6-byte ESI model read at `0x49621c` and existing 50-byte cull window. Read-only
inspection of `/mnt/c/X3FL-orig/X3FL.exe` matches every span and whole-instruction
boundary: the lens call reaches `0x497720`, whose selector call reaches
`0x495e50`; `[*0x5aa100+0x64]` and node `+0x144` remain the established inputs.
The absolute manager operand is rebased; relative CALL operands are unchanged.
Native synthetic-image preparation verifies both `0x18000000` and `0x28000000`
loads, rejecting unrebased globals and changed spans. Actual emitted selector
execution preserves live ECX, stack/ABI, x87/SIMD/MXCSR and LastError in both
consumer orders and with lens alone. Shared owner rollback/debt behavior and
its five-byte atomic write are reused; no native private API was introduced.

The effective FL Lensflares table has the same decoded hash as the previously
inspected table (`c98373778d2046e78f74d5fcc0a891e966bafc981ac1e15f58b9b5c94ea2b0a3`).
Across all 35 authored groups the existing policy covers 37/56 names and
188/220 rows; groups 0–8 have 36/36 names and 103/103 rows covered. Nineteen
names are outside policy, including the unresolved resource name `v\10791`.
These remain unclassified rather than expanding the set from asset membership
alone. Complete FL runtime flare coverage still needs a flight census. The
[feature ledger](../../verification/sun-occlusion.md#2026-10-29-fl-gain-zero-lens-cull-prepared-before-exports)
and its source-bound producers retain the counts, hashes and limitations.
