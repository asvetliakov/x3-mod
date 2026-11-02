# FL shared cull/LOD chain contract

Bounded offline reconstruction, 2026-10-29. Preferred VAs use base `0x400000`.
Input: clean `/mnt/c/X3FL-orig/X3FL.exe`, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
**[s]** means measured static evidence; **[i]** means inference or implementation
requirement; **[u]** means unestablished. No production source edit, PE build or
execution, Wine, game launch or installation. This qualifies a bounded native
binding contract; it does not claim an implemented or runtime-qualified FL hook.

> **2026-11-02:** the sites below were re-derived from the live image; see
> [Verified 2026-11-02 against X3FL.exe](#verified-2026-11-02-against-x3flexe) for
> the per-module port table and the status of each claim in this older text.

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


## Verified 2026-11-02 against X3FL.exe

Input: the live `C:\X3FL\X3FL.exe` (SHA-256 `e7f3aad8…01dfbb8`, the image the
2026-10-29 text calls `/mnt/c/X3FL-orig`, which no longer exists) and
`C:\X3\X3AP.exe` (`fdbf3418…`) for the AP twins. Static only; no Wine, no game.
The 2026-10-29 producers (`fl-port/cull-chain-contract/produce.py`,
`fl-port/cull-projection/verify_ordering.py`) are no longer in the tree, so
their counts above cannot be re-run; this section replaces them with one
producer.

```sh
python3 -I verification/results/fl-cull-chain/produce.py          # writes contract.json
python3 -I verification/results/fl-cull-chain/produce.py --check  # 0 when it is current
```

The [producer](../../../verification/results/fl-cull-chain/produce.py) reuses the
`Image` class of the engine-effects producer (`inspect_x3.py` PE reader, `.reloc`
parse, Capstone). It asserts 109 FL and 11 AP whole-instruction anchors. It hashes
15 windows and compares every AP window that has a source array byte for byte
with that array (`cull_small_parts_core.h` window, marker and JET writer,
`terran_lod_sites.h`, `lod_occlusion_sites.h`, both `cull_census_core.h` windows).
Over the selector's CFG (407 instructions, every one reached) it computes
reaching definitions for the seven GPRs and the stack-slot writers at each
site, with a forward ESP-depth pass that must agree at every join. It also runs
a linear direct-edge census of `.text` (72,230 edges), a raw rel8/rel32
byte-position scan and an unaligned-dword scan of the whole file for every
patched span. [contract.json](../../../verification/results/fl-cull-chain/contract.json)
holds RVAs, offsets, counts and hashes only. Every figure in this section is
**measured** from the bytes unless it is marked [i]. AP has no `.reloc`; FL has
38,327 HIGHLOW fixups (DYNAMIC_BASE), so every FL address below is an **RVA**
(VA − `0x400000`) that a hook adds to the running module base.

### FL beside AP

Window identity = window bytes with relocated dwords zeroed, FNV-1a 32 / SHA-256
(full digests in contract.json).

| AP claim (module) | AP VA | FL RVA | FL window RVA, len | Relocs in FL window | FL FNV-1a | FL SHA-256 prefix | Write, qword |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Shared cull site: small parts, dock, far jets, lens, occlusion-engine | `0x47d2a2` (5-byte span) | `0x0961d3` (9-byte span: `mov eax,[esi+0x18]` + `mov edx,[esi+0x1d8]`), continuation `0x0961dc` | `0x0961c5`, 55 (AP `0x47d294`, 56) | none | `9df9cf9e` | `e4339bc9587acfcd` | jmp rel32 `[0x0961d3,+5)` in qword `0x0961d0`: **atomic** |
| Engine "culled" instruction | `0x47d2c3` | `0x0961f3` `and dword [esi+0x12c],~2; jmp 0x096201` | size verdict `0x0961d3`, 73 (AP 74) | none | `8f680dbc` | `59a638249270ff5c` | – |
| Model read after the verdict | `0x47d2ec` (+0x140) | `0x09621c` `mov ecx,[esi+0x144]` | `0x09621c`, 6 | none | `41372e13` | `be924c44b50003fc` | – |
| Terran bit-31 reader (terran_station_lod) | `0x47d01c` `je` → `jmp` | `0x095e96` `cmovl eax,ecx`; edit byte `0x095e98` `c1→c0` = `cmovl eax,eax` | `0x095e82`, 31 (AP `0x47d012`, 17) | none | `0e8e0d62` | `d6c77e017dc61e1e` | 1 byte in qword `0x095e98`: **atomic** |
| LOD-0 occlusion gate (lod_occlusion) | `0x4c34f7` `jne rel32` → 0 | `0x0e56c0` `jne rel8 0x0e5731`; edit byte `0x0e56c1` `6f→00` = `jne 0x0e56c2` | `0x0e56b1`, 20 (AP `0x4c34e7`, 31) | 1 at `0x0e56bc` → `0x1a9014` (placeholder global) | `3105ccaa` | `87db655b2b327743` | 1 byte in qword `0x0e56c0`: **atomic** |
| Census measure site (cull_census) | `0x47d258` (6) | `0x096186` `mov eax,[esi+0x1dc]` (6); alternative `0x096182` (10-byte span) | `0x09617a`, 22 (AP `0x47d248`, 31) | none | `b37f0e2b` | `3529aeda4d83f997` | `0x096186`: crosses `0x096188`, **install window**; `0x096182`: qword `0x096180`, **atomic** |
| Census exit site (cull_census) | `0x47d528` (6) | `0x095f27` `mov esi,[esi+0xc]; cmp dword [esi],0` (6) | head `0x095f24`, 11; tail `0x09642e`, 24 (AP `0x47d519`, 27) | none | `2787f851` / `e07654a9` | `1717005dae339d7c` / `507b45838f62b3bb` | crosses `0x095f28`: **install window** |
| Projectile marker store | `0x4401ae` | `0x049b1d` `mov dword [ebp-0x20],0x20800000` | `0x049b1d`, 7 (AP 8) | none | `7fd6e34b` | `26bb6a72e5467347` | – |
| Projectile marker OR | `0x44123b` | `0x04a807` `mov eax,[esi+0x70]; mov ecx,[ebp-0x20]; or [eax+0x130],ecx`; class guard `0x04a6b9/0x04a6bc` | `0x04a807`, 12 (AP 13) | none | `147c2006` | `8cb6cc7c7c1b4edb` | – |
| JET flag writer | `0x434708` (`[esi+…]`) | `0x03deb3` `or dword [ecx+0x130],0x4000001` | `0x03deb3`, 10 | none | `4d34a219` | `d2a7a221955f9809` | – |
| Dock cut id arithmetic, text / binary | `0x4920f8` / `0x491528` | `0x0ae169` / `0x0ad62d`: `local + (cut−1)·100000` | `0x0ae169`, 20 / `0x0ad62d`, 15 | text: 1 at `0x0ae171` (string push) | `b8cf9dc8` / `c577a9ee` | `12b069c2411d5480` / `231cf02ec4a609b0` | – |
| Lens scene walk | `0x472466..0x472476` → `0x47e780` | `0x08bc8c` `mov ecx,[*0x5aa100]; push esi; mov ecx,[ecx+0x64]; call 0x097720` | `0x08bc8c`, 15 | 1 at `0x08bc8e` → `0x1aa100` | `c09fb95a` | `c2a03ff5c34d00a2` | – |
| Walker → selector call | `0x47e7a5` | `0x097745` | `0x097740`, 10 | none | `8524dc9a` | `18e77a30f9fc7b7f` | – |
| Terran bit-31 writer | `0x441644` | `0x04ab6a` `or dword [edx+0x12c],0x80000000` (the only such OR in `.text`) | – | – | – | – | – |

Callers: selector `0x095e50` has exactly two direct callers, its own child loop
`0x095f37` and the walker `0x097745`. Walker `0x097720` has three: the sector scene
`0x08ba4c`, the lens scene `0x08bc96` and `0x09786e`, a third view whose routine
issues its own Clear through `vtable+0xac`. AP's walker `0x47e780` also has three
(`0x47226b`, `0x472471`, `0x47e8c2`); that `0x09786e` is AP `0x47e8c2`'s twin is
[i]. The selector's callees are shared widely: model lookup `0x0a1a40` has 59
direct callers, measure helper `0x012b50` 39, radius cache `0x0a39b0` 35, frustum
`0x0e9000` 1. Every claim is therefore a selector-site claim, never a callee
claim.

### 1. Shared cull site `0x0961d3` (hook-site suitability)

- **Boundaries.** Two complete loads of 3 + 6 bytes, then `0x0961dc test eax,eax`.
  Entries to `0x0961d3` are exactly the `je` at `0x0961b9`, the `jge` at
  `0x0961c8` and the fall-through from `0x0961cc`. No direct edge, raw rel8/rel32 candidate or
  unaligned dword lands in `(0x0961d3, 0x0961dc)`. The span has no relocation
  and no relative operand. The engine's culled instruction `0x0961f3` has no
  direct entry (fall-through only, as AP `0x47d2c3`), so a stub reaches it by
  `jmp`.
- **State at the site** (reaching definitions; ESP = `align_down(EBP,8) − 0x28` on
  every path):

  | Value | FL | AP (site `0x47d2a2`) |
  | --- | --- | --- |
  | Node | ESI (only writer `0x095e5e mov esi,ecx`) | EDI |
  | Native measure `r·W/D` (W = `[*0x5aa100+0x5c]`, helper `0x012b50`) | **ECX, live** (writers `0x09617a`, `0x0961ca` = 0 in env-map views) | ESI |
  | Pixel metric `s = r·640/D` (≥ 1, or `0x7000000`) | `[ESP+0x10]` (only writer `0x096182`), copy `[ESP+0x18]` (`0x09617e`) | `[ESP+0x2c]` |
  | Native measure, stack copy | `[ESP+0x14]` (`0x096142`) | – |
  | D (focus-scaled view depth) | EDI (`0x0960cd/d8/e3`, `0x09610d`) | EBX / `[ESP+0x10]` |
  | View (camera) | EBX (only writer `0x0960d5 mov ebx,[ebp+8]`) = `[EBP+8]` | EBP = `[ESP+0x28]` |
  | Local distance flag / propagated flag | `[ESP+0x1c]` / `[ESP+0x24]` byte | `[ESP+0x18]` byte (one slot) |
  | Dead at site | EAX, EDX (written by the displaced loads), EFLAGS (`0x0961dc` TEST, or the culled AND, writes them before any read) | EAX, ECX, EFLAGS |

  The selector has no x87 instruction. Whether the incoming x87 stack is empty
  is not established; the stubs must stay integer-only, as on AP.
- **Culled verdict path.** Replay the native max-threshold block on the scratch
  registers, then jump to the engine's own instruction:
  `mov eax,[esi+0x18]; mov edx,[esi+0x1d8]; test eax,eax; je +; mov eax,[eax+0x1d8];
  cmp eax,edx; cmovg edx,eax; jmp base+0x0961f3`. ECX is left untouched. From there
  the native path is `0x0961fa → 0x096201`: a set `+0x12c` bit 26 escapes to LOD
  selection `0x09621c` exactly as AP `0x47d2d1` does; otherwise the path stores
  and goes `0x096217 → 0x095f24` (children still visited). After the culled AND,
  ECX is dead (`0x09621c` reloads it, the child loop overwrites it, the thiscall
  return clobbers it). On every continue path the stub must preserve **ECX**,
  which is read at `0x0961ef/0x0961fc`. The continue tail replays the 9 bytes
  once and jumps to `base+0x0961dc`.
- **Write.** Five bytes in the aligned qword `[0x0961d0, 0x0961d8)`, so the
  existing `lock cmpxchg8b` path applies. The four remaining span bytes stay as
  they are and become unreachable.
- **Reentrancy.** The site runs before the node's child loop. The recursion
  (`0x095f37`) re-enters the selector only after the stub has left by `jmp`, so a
  stateless stub never nests [i]. All three walker callers are frame-render
  paths. That they run on one thread, as on AP, is [i].

### 2. Terran station LOD `0x095e98`

The root path `0x095e82..0x095e9d` tests the incoming flag (`test al,al; jne`) and
the parent (`cmp [esi+0x18],0; jne`). It then runs `cmp dword [esi+0x12c],0;
movzx eax,al; cmovl eax,ecx` (ECX = 1 from `0x095e69`) and stores EAX to **both**
the local flag `[ESP+0x1c]` and the propagated byte `[ESP+0x24]`. AP has one slot.
`0x09628a cmp byte [esp+0x1c],0; je 0x096354` selects the metric loop over the
fixed-distance ladder (`0x096295..0x0962e0`, constants identical to AP's
`0x47d36d..0x47d3b8`). Changing ModRM `0x095e98` from `c1` to `c0` decodes as
`cmovl eax,eax`, 3 bytes, with the same boundaries. On the root path AL is 0
(the `jne` at `0x095e84` already left for a non-zero flag), so EAX stays 0, both
slots receive 0, and every node selects by size. The edit writes no flags, and
the CMP's flags are not consumed elsewhere. No edge lands inside
`0x095e97..0x095e98`, the window has no relocation, and the one byte lies in
qword `0x095e98`. The only bit-31 writer is `0x04ab6a` (AP `0x441644`). The
AP save-load reader `0x47a005` was not re-located on FL [u].

### 3. LOD occlusion `0x0e56c1`

`0x0e56b4 cmp dword [eax+0x150],0` (EAX = node from `[ebp-0x1c]`, LOD +0x150)
precedes `0x0e56bb mov eax,[0x5a9014]` (placeholder entry, relocated) and then
`0x0e56c0 jne 0x0e5731` (placeholder bind `push -1; push [eax+0x34]`). The
LOD-0 path `0x0e56c2` stores the placeholder at `[ebp-0x2c]` as its fallback,
the way AP stores it at `[esp+0x18]`. `0x0e5731` has exactly one direct entry,
this `jne`. Rel8 `0x6f → 0x00` makes both outcomes fall through to `0x0e56c2`.
Flags are dead there (the first flag reader after `0x0e56c2` is behind
`0x0e56cb test eax,eax`), so registers and stack match a native LOD-0 arrival.
No edge or dword lands in the rel8 byte. The window's one fixup at `0x0e56bc`
must be zeroed (or rebased to `base+0x1a9014`) before comparison. The
material-routine extent and its other callers were not re-derived [u].

### 4. Census sites

- **Measure** `0x096186` (twin of `0x47d258`): ESI = node, ECX = native measure
  (live; `0x096190 cmp ecx,eax`), `[ESP+0x10]` = s, EDI = D, EBX = view. The FL
  stub pushes `(node ESI, measure ECX, s [ESP+0x10+pushes], D EDI, view EBX)`.
  AP's slot offsets do not carry over (FL `[ESP+0x10]` is s, not D). The
  six-byte span crosses qword `0x096188`, so it needs a plain write inside the
  install window. Moving the site to `0x096182`, which displaces
  `mov [esp+0x10],eax` too (10 bytes, no relative operand, no entry into either
  span), keeps the jump in one qword. At `0x096182` s is still in EAX and
  `[ESP+0x18]`.
- **Exit** `0x095f27` (twin of `0x47d528`, the child-loop head): entered from
  `0x095fc2/0x095fe3/0x0960b4/0x0960c1/0x096431/0x096441` and by fall-through from
  `0x095f24` (itself entered from `0x095f0b`, `0x096217`). Nothing enters
  `0x095f28..0x095f2c`. ESI = node. EDI holds the camera on every path that
  reaches the exit (`0x095f24`, `0x095f5a`, `0x096396`, `0x0963f2`); the `0x095f24`
  path loads it there. LOD `+0x150` and bit 2 of `+0x12c` are final. **The AP
  model-pointer rule does not port:** EBX holds `LOD count − 1` or a caller value,
  and `[ESP+0x14]` holds the model pointer (`0x096227`), the native measure
  (`0x096142`) or a stale word, depending on the path. An FL exit stub must
  resolve the ladder from `+0x144` outside the pass, or capture `[ESP+0x14]` only
  on the `0x096227` path. The span crosses qword `0x095f28`, so it needs a plain
  write inside the install window, and no atomic alternative keeps the node in
  ESI.

### 5. Node layout (FL selector ESI accesses vs AP pass EDI accesses)

Counted memory operands. FL: `+0x12c` ×14, `+0x130` ×4, **`+0x144` ×3**, **`+0x150`
×15**, `+0x18`, `+0x1c`, `+0x1d8`, `+0x1dc` ×1 each, `+0xa0` ×3, `+0xb0/b4/b8`,
`+0xf0/f4/f8` ×2. AP: `+0x140` ×3, `+0x14c` ×17, and no `+0x144/+0x150`. Hence:
parent `+0x18`, children `+0xc`, flags `+0x12c/+0x130`, radius `+0xa0`, thresholds
`+0x1d8/+0x1dc` and view position `+0xf0..+0xf8` are **unchanged**. Model id
**`+0x140 → +0x144`** and selected LOD **`+0x14c → +0x150`**. The body manager
`*0x5aa100` (AP `*0x608518`) is confirmed as the measure's width source (`+0x5c`,
`0x096113/19`) and as the lens-scene root (`+0x64`, `0x08bc8c`). Its body-table
fields (`+0xb4/+0xb8/+0xbc`, stride `0x1c`, name `+0xc`) are the engine-effects
note's §4 result and were not re-derived here.

### 6. Per-module judgement

1. **cull_small_parts** (small parts, dock ports, projectile exemption, far-jet
   hand-off): **portable with a changed stub.** The policy is unchanged; the
   site, registers and offsets are new. Compare `s` at `[ESP+0x10]` (plus pushes)
   rather than `[ESP+0x2c]`. Use EAX/EDX as scratch and never ECX. The dock id
   is `[esi+0x144]`; the marker and JET bits are `[esi+0x130]`. Pass the far-jet
   call `(node ESI, measure ECX, view EBX)`. Replay as in §1. The marker
   writers `0x049b1d/0x04a807`, the JET writer `0x03deb3` and both dock
   formulas are present with the same constants. The **small-props** rule is
   not part of this stub: on AP it is a draw-hook skip
   (`cull_small_props_core.h`) with no engine site. Moving it into the pass
   would be a new stub, not a port. It would need a prop-body bitmap like the
   lens set and a size proxy, because the engine's `s` overstates split
   turret props about 9× (AP run375, `+0xa0` radius).
2. **terran_station_lod**: **portable with a changed patch**, the one-byte
   ModRM edit of §2 (AP edits a `je`). The `size`/`distance` setting semantics
   carry over.
3. **lod_occlusion**: **portable with a changed patch**, the one-byte rel8 edit
   of §3 with the fixup normalised. It removes no draw: `all` adds occlusion
   binds and lookups on LOD > 0.
4. **cull_census** (read-only): **portable with changed stubs and a changed exit
   handler** (§4). One or both sites need the plain install-window write unless
   the measure site moves to `0x096182`.
5. **lens_flare_cull**: **portable with a changed stub.** It reads `+0x144`,
   uses EAX/EDX for the bitmap index, and the AP stub's `mov ecx,eax`/`shr ecx`
   must move to EDX. It then takes the same culled path to `0x0961f3`. The lens
   scene reaches the same selector (`0x08bc96 → 0x097720 → 0x095e50`).
6. **occlusion_engine_cull**: **portable with a changed stub, and not yet
   usable.** Compare the view against EBX (or `[EBP+8]`) instead of
   `[ESP+0x28]`, and move the probe register from ECX to EDX. Model `+0x144` and
   position `+0xf0..+0xf8` are unchanged. Its table comes from the draw-level
   occlusion cull, which FL does not have yet.
7. **engine_far_jets**: claims no engine site. It is the small-parts stub's
   callee, and its inputs map as in item 1 and the old §4 read set (model
   `+0x144`).

### 7. Expected draw saving on FL, ranked

FL heavy frames, measured (`fl-run18-triage/draw_census/summary.txt`, run 19
C/D): 1,742 draws, 825 opaque routed and 906 blended object draws, in station
sectors with 10–13 stations each. AP figures are measured
([cull-small-parts.md](../../verification/cull-small-parts.md),
[occlusion-cull.md](../../architecture/occlusion-cull.md),
[sun-occlusion.md](../../verification/sun-occlusion.md)). Every FL figure is [i].

| Rank | Module | AP measured | FL expectation [i] |
| --- | --- | --- | --- |
| 1 | cull_small_parts 2 px | busy station view 901 → 477 draws (−47 %), ~31 → ~42 fps (Run 43 B) | the heavy FL frames are the same class (station-dominated), so several hundred of 1,742; unmeasured |
| 2 | lens_flare_cull (gain 0 only) | 115–181 flare draws per frame, 150–176 nodes culled | same order when the user runs gain 0; nothing otherwise |
| 3 | dock ports 12 px | 159/27/27 draws, 2.4–3.7 ms at carrier views (Run 110 A) | only with carriers in view; stock cut-scene ids assumed present on FL [u] |
| 4 | occlusion_engine_cull | 67–91 hidden sub-part draws at close capital views; ~10 µs per skipped draw | 0 until the FL draw-level occlusion cull exists |
| 5 | small props (new stub) | 76 of 82 prop draws predicted (render-only on AP) | battle views only; needs new policy |
| 6 | terran_station_lod | not a draw cut: stock ladders cut triangles (station coarsest/LOD0 draws p50 0.83) | small, Terran stations only |
| 7 | lod_occlusion, cull_census | no draw saving (quality fix / diagnostic) | none |

### Status of the 2026-10-29 claims

| Claim | Status |
| --- | --- |
| §1 producer, its 14 hashes / 81 anchors / 466 CFG edges etc. | **unverified**: the producer is gone; replaced by this section's producer |
| §1 72,241 direct edges; only `0x495f37`, `0x497745` enter the selector; nothing enters `(0x4961d3,0x4961dc)`; zero raw/dword interior hits | **confirmed** (this sweep counts 72,230 edges; same conclusions) |
| §2 selector `[0x495e50,0x496446)`, ECX node, camera + flag args, RET 8 at `0x495ed0/0x495f49`, pushes `0x495f33/34`, walker `0x497740/42` | **confirmed** |
| §2 region table (`+0x150` init, `+0x130 & ~0x180000`, bit-26 early return `0x495f4c..58`, frustum `0x495fd3 → 0x4e9000`, `0x4a39b0` RET 4, model `<0`/bit 2 gate `0x4960ad/c1`) | **confirmed** |
| §2 cull `0x4961f3 → 0x496201`, bit-26 escape `0x49620c`, `0x496217 → 0x495f24` | **confirmed** |
| §2 `0x4e9000` 296 instructions, ten calls, GetViewport `+0xc0` | unverified (not re-read) |
| §3 binding table (9-byte span, EAX/EDX outputs, continuation `0x4961dc`, reject `0x4961f3`, ESI/ECX, `[ESP+0x10]`/`+0x18`, `[EBP+8]`/EBX, `+0x144/+0x150`) | **confirmed** |
| §3 entries `0x4961b9`, `0x4961c8`, fall-through `0x4961cc`; ECX live; flags dead; site ESP `align_down(EBP,8)−0x28`; helper argument cleanup | **confirmed** |
| §3 "zero x87 does not prove an empty x87 stack" | **confirmed** (0 x87 instructions; incoming state unknown) |
| §4 `*0x5aa100+0x5c`, helper `0x412b50` (ECX radius, EDX width, one stack arg, 0 on zero divisor), `[ESP+0x14]` → ECX, `0x7000000` paths, `s` at `+0x10/+0x18`, D law, env-map zeroes ECX only | **confirmed** (helper body read only to its zero-divisor return) |
| §4 consumer table: marker `0x449b1d`, OR `0x44a807..0d`, guard `0x44a6b9/6bc`; JET `0x43deb3`; lens `[*0x5aa100+0x64]` via `0x48bc8c..96`, prep `0x48bc78/87`; dock `0x4ae169..17b`/`0x4ad62d..639` | **confirmed**; **added**: the JET OR is `[ecx+0x130]` on FL (`[esi+…]` on AP) |
| §4 class dispatch `0x449aee`, class-7 subtype `0x449f7d..96`, SMALLJET `0x43dede/0x43dee8`, far-copy read set | unverified here (SMALLJET: engine-effects note) |
| §5 five redirect bytes in qword `[0x4961d0,0x4961d8)`, atomic path | **confirmed** |
| §5 chain order, rollback, arena and cost statements | design statements; not re-checked |
| §6 `0x48ba43 → 0x495670`, then `0x48ba4c → 0x497720 → 0x495e50` | **confirmed** for the three calls; Clear helper internals `0x4db750/0x4db7f8` unverified |
| 2026-10-29 lens: 15-byte traversal `0x48bc8c`, 10-byte call `0x497740`, 6-byte model read `0x49621c` | **confirmed** (traversal carries one fixup, `0x48bc8e → 0x5aa100`); "existing 50-byte cull window": bounds not stated, not reproduced; Lensflares table hash not re-read |
| (implicit) the walker has the sector and lens callers | **corrected/added**: three direct callers, the third `0x49786e` |

### Unknown

- Runtime: none of the stubs or edits has run on FL. That the threading and
  render-thread-only pass match AP is [i].
- The FL equivalents of AP's save reader `0x47a005`, the material routine's
  callers, and the dock cut-scene bodies 9013/9014/9098/9099 in FL's assets.
- The FL draw saving of every module (§7) is inferred from AP flights. A flown
  FL census with a small-parts stub is the first measurement.

## Proxy culls' engine reads (2026-11-02)

The two draw-hook culls (small props, `cull_small_props_core.h`; GPU occlusion of ship sub-parts,
`occlusion_cull_core.h`) patch nothing: per scene draw they take the scope node from `object_trace::scope_node()` and
read engine memory through `engine_memory::read`. On FL the gate is `object_trace::scope_available()` (= `active()`:
X3FL.exe identity, the material-submit site and target bytes and the engine anchor matched, redirect installed). Every
FL read, with its source; VAs at the preferred base, production adds the RVA to the module base:

| Read | AP | FL | Source of the FL value |
| --- | --- | --- | --- |
| Node model id | `+0x140` | **`+0x144`** | `object_trace_fl_inc.h` `node_model` (generated, operand RVA `0x0968e1`); §5 |
| Node parent / handle | `+0x18` / `+0x28` | same | `object_trace_fl_inc.h` `node_parent` (RVA `0x0a5809`), `node_handle` (RVA `0x0a22a8`) |
| Body manager slot | `*0x608518` | **`*(base+0x1aa100)`** | `object_trace_fl_inc.h` `engine_slot_rva`, its anchor checked by `initialize()`; getter `0x4867c1` |
| Body table: fixed `+0xb4` = 11000, dynamic `+0xb8`, slots `+0xbc`, stride `0x1c`, name `+0xc` | same | same | getter `0x4867c0`: `0x4867e9/f3/f9/0x486803/09/10/12`; factory `0x48a5f7` |
| Id → slot: `<1000` id, `<20000` id−9000, else fixed+id−20000, bounded by fixed+dynamic | `0x46df60` | same | getter `0x4867c7/d3/db/e3`, bound `0x4867ff` |
| Cockpit registry slot, registry `+0/+0x10`, map `{buckets,capacity}`, link `{next,key,value}` | `0x608504` | **`base+0x1a82e4`**, same layout | `sector_background_fl_inc.h`, anchors checked by `sector_background_fl::admitted()` |
| Cockpit camera `+0x58` (read by `object_capture::target`) | same | same | `sector_background_fl_inc.h` `cockpit_camera` (RVA `0x022e42`) |
| Cockpit reference object `+0xc`, its render root `+0x70` | same | same | `fl-engine-effects/own_ship_offsets.py` (6 anchors) |
| Selected target cockpit `+0x1e0` | same | same | command load `0x43106f`, setter store `0x4275ed`, invalidator `0x427672/0x42767a` |
| Target object id `+8`, render root `+0x70` | same | same | `0x43107d`; solver `0x4506d8` |
| Descriptor → mesh part `+0x40` box (capture-only diagnostic row) | AP | **not read** | FL's ECX descriptor is not established as a mesh part ([small-props-inputs.md](small-props-inputs.md) §4) |
| Engine-side skip ledger: view position `+0xf0..+0xf8`, stub | AP | **off** | `occlusion_engine_cull` stays AP only (§6 item 6) |

Reproduce (static, read-only, the live `C:\X3FL\X3FL.exe`, SHA-256 `e7f3aad8…01dfbb8`):

```sh
python3 -I verification/results/fl-proxy-culls/offsets.py > verification/results/fl-proxy-culls/offsets.json
```

[offsets.py](../../../verification/results/fl-proxy-culls/offsets.py) decodes 19 whole instructions with Capstone and
checks mnemonic, displacement, immediate and scale: **19 / 19 pass** (measured,
[offsets.json](../../../verification/results/fl-proxy-culls/offsets.json)). Together with the generated
`object_trace_fl_inc.h` / `sector_background_fl_inc.h` constants and `own_ship_offsets.py`, every engine read of the two
culls on FL has a decoded FL source; none is carried over from AP by assumption. Not established here: the lifetime of
the target and own roots between the registry walk and the ancestry walk is the AP contract (validated reads, handle
confirmation, fail closed to `unresolved`), not the owned-copy contract of [small-props-inputs.md](small-props-inputs.md)
§1–2 [i].
