# FL collision census and query-phase boundary contract

Offline reconstruction, 2026-10-30. This covers the remaining active
`collide_narrow_census` and `collide_query_phases` dependencies, including the
developer diagnostic rows in the schema 632–636 work scope. No production
adapter, EXE change, build, native execution, Wine operation, process attach,
installation or game launch is part of this checkpoint. The accepted FL 1.3
image is read from disk while AP may be running.

The census has **four active sites**, despite older comments referring to
three sites and the initial mapping brief referring to two. All four are
claimed by the current AP initializer; omitting the entry counters would lose
actual traversal attribution. The query-phase diagnostic adds one root-call
redirect and uses the existing memo boundary for its outer timer.

## Inputs and reproducible evidence

Run from the repository root:

```sh
python3 verification/results/fl-collision-diagnostics/produce.py
python3 -m py_compile verification/results/fl-collision-diagnostics/produce.py
```

The [producer](../../../verification/results/fl-collision-diagnostics/produce.py)
uses the project's `compare_fl_engine.py` PE32 reader/Capstone decoder. Its
[derived record](../../../verification/results/fl-collision-diagnostics/contract.json)
contains addresses, complete-body hashes, instruction/return/branch inventories,
relocation operand identities, checks and modeled cases, with no game bytes or
raw listings. It writes only that JSON. Both commands pass.

| Input | Bytes, measured | SHA-256 |
| --- | ---: | --- |
| `/mnt/c/X3AP-orig/X3AP.exe` | 2,153,984 | `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` |
| `/mnt/c/X3FL-orig/X3FL.exe` | 2,804,736 | `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8` |

All following addresses are preferred VAs at base `0x400000`. A binding uses
recognized loaded base plus RVA, including every engine global and the n7
trampoline's displaced absolute operand. PE HIGHLOW operands must be normalized
by subtracting the load delta while retaining their preferred identity. The
producer relocates complete body copies by `+0x130000`, normalizes 109 operands
across six nonoverlapping bodies, and reproduces all six original hashes. This
is a static relocation model, not execution at another base.

Existing owning contracts remain
[collision memo](collision-memo.md),
[box cull](collision-box.md),
[native SAT](collision-sat.md) and
[AP sector collision](../sector-collide.md). The existing FL memo fixture evidence
in the [memo ledger](../../verification/collide-memo.md) qualifies its implemented
six-word/SSE boundary and scatter replay separately; it does not qualify these
new diagnostic bindings.

## 1. Exact sites, coverage and loaded layout

| Role | AP site → target | FL site → target | FL continuation / replaced span |
| --- | --- | --- | --- |
| n5 accepted all-pairs bracket | `0x45d665 → 0x48ac80` | `0x46734f → 0x4a6270` | `0x467354`, one 5-byte CALL |
| n6 boolean mesh-query counter | `0x48a9a5 → 0x47f1b0` | `0x4a6395 → 0x4982d0` | `0x4a639a`, one 5-byte CALL |
| n7 recursive BVH entry counter | `0x4e2530` | `0x51a6c0` | `0x51a6c5`, one 5-byte absolute MOV |
| n8 triangle-leaf entry counter | `0x4e2190` | `0x51a260` | `0x51a266`, **6 bytes / 3 instructions** |
| Query-phase root descent bracket | `0x4e2956 → 0x4e2530` | `0x51b04d → 0x51a6c0` | `0x51b052`, one 5-byte CALL |

The mapping is semantic, following the all-pairs accepted-pair path, the
boolean model query, the root-transform setup and its recursive SAT/triangle
work. FL inlines AP n5's output clearing into the pair caller before the
redirected call. At `0x467317/0x467321` it clears both physics `+0x190` words;
at `0x46733d/0x467348` it clears both 16-byte blocks at `+0x180`. The proposed
n5 timer starts **after** those clears, so its interval differs slightly from
AP's wrapper interval. Moving the hook earlier would require a larger and more
intrusive hook without demonstrated attribution value. Report the FL interval
as native pair traversal after output initialization.

The full local pair traversal `0x4a6270..0x4a664c` has 294 decoded instructions,
21 direct branch edges and six plain returns at `0x4a631b`, `0x4a645b`,
`0x4a64d3`, `0x4a6565`, `0x4a659e`, `0x4a664b`. The single n5 call brackets
all its normal returns, including model/extent rejection, contact and recursive
child scans. Its two recursive calls are `0x4a6475/0x4a64ae`; they need no
separate brackets. The accepted-pair caller's complete 1,177-instruction body,
224 direct branches and four returns are decoded, with its existing three jump
tables and full CFG qualification reused from the box-cull contract.

n6 counts the boolean mesh attempt at `0x4a6395`, including attempts later
answered by memo. The same mesh target has another encoded caller at
`0x4a5ff7`, used by distance mode; that is outside n6's AP-equivalent scope.
Do not add a hook there merely to make the mesh counter cover all modes.
The n6 target has two returns, `0x4982f2/0x498553`; their no-model and completed
query paths both preserve ordinary caller cleanup.

FL n7 has exactly four encoded entry calls: three recursive sites
`0x51a866/0x51a9f5/0x51aac8` and the sole external root at `0x51b04d`.
AP has four recursive sites, so its five-caller assertion does not transfer.
Descent has three returns, `0x51a6e6/0x51a776/0x51aad7`, and 17 direct branch
edges. n8's sole encoded caller is `0x51a76a`; its single return is `0x51a6bc`.
Leaf has 232 decoded instructions and 11 direct branch edges. The root query
has 301 instructions, three direct branches and two returns:
`0x51ab45` (unbuilt model, no descent) and `0x51b05f` (completed descent).
All local direct branch targets are verified decoded boundaries. The JSON
retains their exact inventories.

For every proposed span, bytewise `.text` rel32 scans and all file-backed
sections' abs32 scans find zero references into its interior; the six decoded
bodies have no direct short branch into an interior either. Entry scans verify
the n7/n8/mesh caller lists above and no absolute entry pointer. This conservatively
covers encoded references, not arbitrary computed indirect entry or third-party
patches. Claim ownership and cold exact-byte admission remain mandatory.

| State | FL preferred address / layout |
| --- | --- |
| Contacts, loaded by displaced n7 MOV | `0x5aa0a0`; HIGHLOW operand at `0x51a6c1` |
| Stop on first contact | `0x61b980`; absolute operand at `0x51a6ca` |
| Flags / cap | `0x5a9044` / `0x5aa0e0` |
| Executed SAT visits / triangle tests | `0x5a9048` / `0x5aa0d0` |
| Root state | T `0x61b988..990`, R `0x61b994..9b4`, scale `0x61b9b8` |
| Object fields | flags `+0x40/+0x44`, class/subtype `+0x48/+0x4a`, physics `+0x70`, radius `+0xa4` |
| Physics census fields | position `+0x30..38`, saved words `+0xb0..bc`, transform words `+0xc0..e8`, flags `+0x12c`, **model index `+0x144`** |

`0x4a635a/0x4a6371` load model indices from physics `+0x144` before the
resource lookup `0x4a1a40`; AP uses `+0x140`. That offset must change in the FL
key reader and its model/hash inputs. The other listed offsets are retained
from the established FL object/box/memo contracts and the actual traversal
loads. The contiguous 11-word transform stamp is the existing diagnostic's
conservative byte stamp, not a promise that all 11 words are matrix elements.
Keep its comparison semantics, ring capacity 256 and shared formatting. A small
layout parameter to the shared reader suffices; no per-pair edition test or
new registry lookup is needed.

## 2. ABI, liveness, stack, lifetime and exceptions

At n5, EDI is object A and EBX object B; ECX=`[EDI+0x70]` is physics A and
EDX=`[EBX+0x70]` physics B. **ESI is already `physics A +0x190`**, not an object.
The redirected CALL pushes a return slot over four caller-cleaned words:
`radius A`, `radius B`, `&physA[0x190]`, `&physB[0x190]`. The decoded pre-window
`0x46730e..0x46734f` verifies the source registers, radii and output addresses.
A pre-handler therefore receives objects as EDI/EBX, rather than AP's EBX/ESI.
The FL callee aligns its private stack to 16 at `0x4a6273`, preserves the usual
EBX/EBP/ESI/EDI contract, and returns integer EAX. Caller `0x467354` drops
16 bytes, `0x467357` TESTs EAX, and `0x467359` branches on `EAX <= 0` to
`0x4673ae`. Preserve native output registers even if that immediate continuation
uses only EAX; no argument repushing is needed.

At n6, ECX/EDX are physics A/B. Six stack words at native callee entry are
body A, body B, flags=2, cap=1, tolerance float=0, minimum pointer=null.
`0x4a6380..0x4a6395` decodes their exact construction; EDI is zero there.
The native callee does not consume an incoming XMM argument at this boundary;
it builds the six-word/SSE root query itself. Caller first reloads ECX at
`0x4a639a`, drops 24 bytes at `0x4a639e`, then TESTs EAX at `0x4a63a1`.
The counter stub can remain `INC [owned counter]; JMP native mesh target`.
It changes no GPR, stack or floating register.

n7 entry takes node A in ECX and B in EDX; stack words are R, T and scale.
The displaced instruction reads contacts into EAX, not visits. Emit its
**loaded** absolute address, then continue to `SUB ESP,0x30` at `0x51a6c5`.
n8 takes nodes in ECX/EDX and no stack arguments. Its displaced prologue is
`PUSH EBP; MOV EBP,ESP; AND ESP,-8`. A six-byte claim and `+6` continuation are
required; AP's five bytes split AND. Resume at `SUB ESP,0x44` (`0x51a266`).
A counter stub of `INC; displaced prologue; JMP continuation` is 17 bytes for
n8, versus AP's 16. n7 is 16 bytes; n6 is 11 bytes, before any allocator padding.
These are inferred encoder sizes, not emitted production artifacts.

Incoming arithmetic flags are dead before any consumer at all four entries:
n5 overwrites them with AND at `0x4a6273`, n6 with SUB at `0x4982d4`, n7 with
SUB at `0x51a6c5`, n8 with AND at `0x51a263`. The n5/n6 return continuations
also overwrite arithmetic flags before conditional use. INC preserves CF and
changes other arithmetic flags; those changes are killed on each path above.
For AND, AF is architecturally undefined and the replay makes no AF-value claim.
DF and non-arithmetic state must remain untouched by plain counter stubs.

At the query-phase root call, ECX is root A, EDX root B. Three caller-cleaned
words are local R (`pre-push ESP+0x8c`), local T (`pre-push ESP+0x5c`), scale.
The scale slot is filled at `0x51affa`, T pushed at `0x51afff`, R at `0x51b014`.
`0x51b052` drops **12**, not AP's 20 bytes. The exact-stack return-substitution
transport in the existing root thunk is argument-count independent: consume the
caller's return into its guarded slot, CALL descent at the same ESP, preserve
native outputs, then jump to `0x51b052`. Do not repush either AP or FL arguments.
The root hook covers only this external invocation; recursion reaches the
native entry/n7 counter normally and must not acquire additional timing slots.

The six decoded local bodies contain no x87/MMX instruction. This does not
prove an empty incoming x87 stack: counter stubs leave it untouched; handlers
must use the existing integer/SSE2, four-byte-incoming-stack contract and their
qualified state envelope. n5's existing pre/post save path retains GP, flags,
XMM0–7; LightCallBoundary preserves MXCSR and LastError, contingent on the
compiled no-x87 reachable-helper audit. Preserve the engine's **post-call**
MXCSR/status and XMM outputs around the post-handler. The root phase thunk can
reuse the qualified full GP/XMM/x87/MXCSR envelope, including its equal-rounding
check before FNSAVE and divergent-mode bypass. That transport still needs FL
native qualification; the static absence of engine x87 does not waive it.

Pointers are borrowed only during the synchronous pair/query. The census stores
numeric addresses and copied words, not retained engine references. Its shared
ring/previous-frame comparison does not extend object lifetime. The engine reads
these physics/model blocks during the same call, but that does not independently
prove safe cross-thread access or resolve the inherited model ABA assumption
in the memo note. The larger pair routine calls extent/resource/transform
helpers outside the pure memo closure; this study does not claim that entire
transitive path has no callback or fault.

Reentrancy gates remain essential. Reentered/foreign diagnostic roots must
forward to native code without replacing a saved return slot; phase invalidation
must retain the shared owner/reset/foreign generations and Present abandonment.
A raised exception or unmasked SSE fault may bypass the synthetic return; never
publish that query or pair sample. Existing memo/phase busy state is rejected
until the owner consumes the abandoned work at Present. The current census's
byte busy flag has **no Present/reset recovery**: unwind past n5 leaves it set,
so later pairs take nested bypass until reinstall. Its try-lock protects rings,
not the unguarded pending slot or a concurrent test/set of that byte. AP's
single collision-thread observation is not proof of FL thread ownership. Reuse
its existing diagnostics without claiming lock-free multithread safety; qualify
normal/nested/foreign/unwind behavior explicitly before FL release. No new
exception interception or engine-global ownership mechanism is established here.

## 3. Counter semantics and shared memo coexistence

The n7 entry count and native `0x5a9048` visit count differ. A stop-on-contact
or cap rejection returns through `0x51a6e6` before the visit increment at
`0x51a714`. That invocation contributes one n7 entry and zero native visits.
Likewise, native triangle counter `0x5aa0d0` is reset per query and replayed on
memo hits. A hit that replays nine triangles executes zero triangle leaves;
reading that replay as nine executed leaf entries would be false attribution.
Keep the existing census's monotonic instrumentation deltas for actual work.
A minimal faithful binding therefore retains n7/n8 unless an already-qualified
shared counter transport produces exactly those same executed-entry events.

n5/n6 remain outside the memo boundary. An accepted pair still gets one bracket,
and its boolean mesh attempt one n6 count, while a memo hit adds zero n7/n8
entries. Query phases time misses, verifies and ineligible native dispatch only;
hits perform no QPC reads. An unbuilt model can return before root descent;
its outer dispatch time remains valid with zero descents and zero sampled engine
counters, rather than stale counters from an earlier query. Visits, triangles and
contacts for eligible dispatch use the FL addresses above, through a cold-selected
word binding; the current phase recorder hardcodes AP globals in `detail::end()`.

The claim spans are mutually disjoint and disjoint from FL memo
`0x49853a..53f`, box P1 `0x46723f..245` and box P2 `0x466859..85f`.
FL SAT remains native, with no replacement claim. The root phase call targets
the descent **entry**, so a co-installed n7 entry counter runs once for the
root and again for each recursive invocation; the phase wrapper times that
entire traversal and its tiny census overhead. That is intended measurement of
the enabled configuration, not a claim that instrumentation has zero cost.

Disjoint claims do overlap **validation bodies**: n5 lies in the existing box
contract's broader caller body; n7/n8 and the root call lie in memo's descent,
leaf and query pins. The memo's production `profile::accepts()` currently checks
all 14 complete bodies, with no diagnostic holes. A later clean-body equality
check after these sites are patched will refuse correctly. Do not weaken hashes
by adding unconditional holes or perform whole-body validation on the hot path.

The minimal ordering is: recognize/build/load-base once; perform and retain all
required clean-image cold qualifications (reuse existing memo's 14-body pins and
box's established windows, adding only the pair-site ABI windows/body); prepare
all claims/emitters while quiescent; publish the mutually disjoint site claims
in the startup transaction. Reuse the existing claim registry, module pinning,
rollback/cache-protection debt and restore retry machinery. If the startup
architecture admits an optional diagnostic after another owner has written,
normalize only registry-owned, byte-verified patches back to their verified
original bytes for cold admission, or validate before the first owner writes.
Blindly masking installed bytes or rehabbing arbitrary third-party changes is
not an admission contract. Failure restores only claims acquired by that owner
and retains failed-restoration ownership; unload restores callers before freeing
reachable emitters. No install ordering or new patch code is implemented here.

## 4. Minimal binding plan, performance and acceptance limits

Reuse shared ring/summary/annotation recorders and the phase accumulator/window,
rather than duplicating them in FL. Select one FL site/layout/word profile at
cold admission. The census needs object-register selection EDI/EBX, model offset
`0x144`, FL call/entry windows and six-byte n8 replay. Existing parameterized n5
exact-stack transport and n6/n7 counter encoders have the right shape once their
operands are loaded; n8 needs a small prologue parameter/encoder extension.
No additional recursive call hooks, lifetime registry walk or per-call body
validation is needed.

For phases, enable the existing common memo `Binding.phases` only after the FL
phase recorder/root claim is ready; it is currently explicitly false. Keep the
qualified FL memo's six-word normalization, ECX/EDX/XMM2 input preservation and
MXCSR context eligibility. The existing shared lookup/store already expose all
begin/end/foreign/reset/Present seams. Add LastError-preserving lookup/store
wrappers (the AP phase wrappers already express that policy), since clocks can
change it. The existing small FL memo thunk can remain the ABI transport if
reachable helper audits establish its required x87/MXCSR policy; a copied AP
memo thunk would capture the wrong registers/argument block. The single root
phase thunk is the reusable full-envelope/exact-stack path with FL loaded target.
Parameterize counter addresses in the recorder and root target cold once.
Retain the 300-frame paired `query - descent` reduction, invalid-frame exclusion,
mode/error discards and startup 129-pair median clock calibration.

Inferred added census work: two QPC reads and bounded key copies per accepted
pair, 11-byte mesh counter transport, 16-byte BVH transport and 17-byte leaf
transport. Entry counters add one store per invocation, so their actual cost
scales with traversal; n7 includes early stop/cap invocations. Keep no clock,
allocation or helper call in recursive counters. Ring sorting/previous-frame
comparison remain bounded to 256 entries and run at Present, with existing
try-lock/defer behavior. Cold hashes and relocations do not belong in this path.

The unchanged AP query-phase ledger retains measured fixture overhead of
**+1,485.2 ns/query** on the first accepted exact-stack run and **+1,424.8 ns/query**
on the final artifact-bound run; neither is an FL estimate. The existing FL memo
FP-context performance audit and its native/rebased fixture results also remain
in the memo ledger unchanged. This study measures no timing, speedup or FPS.
Four QPC reads per ordinary tracked query (outer/root begin and end), plus the
state envelopes, make phases a deliberate opt-in diagnostic. Memo hits retain
zero clock reads. Off must retain no diagnostic claim and no clock access.

Measured offline checks: 12 decoded argument-formation cases, 1,024 decoded
entry-prefix replays across four incoming stack residues and all 64 arithmetic
flag combinations, 12 exact-stack return-substitution models, 109 relocation
normalizations and ten negative controls. The prefix interpreter fails on an
unmodeled instruction. It executes the decoded engine slices for argument
formation and the first flag-killing prologues; the guarded return substitution
is a separate integer stack model, not execution of a thunk/helper or complete
collision body. The replay excludes architecturally undefined AF after AND.
The counters/clock/Windows APIs and exception transport are not emulated.

The ten controls cover a branch into the six-byte leaf span, a second external
root call, AP's split five-byte leaf span, AP's wrong object registers, AP's
five-word root cleanup, AP's wrong model offset, AP's absolute counter identity,
entry-versus-eligible visit semantics, replayed-versus-executed triangle work,
and a changed leaf body. The two semantic counter witnesses are instruction/
source-derived scenarios, not captured FL calls. No unchanged AP whole suite,
DLL build or Wine fixture was needed for this documentation checkpoint.

Required implementation acceptance remains bounded: actual emitted FL claims
and prologues at preferred/rebased bases; normal/early/contact/recursive exits;
all four caller stack residues; GP/XMM/live x87/MXCSR/LastError parity including
unmasked faults and divergent rounding; memo miss/hit/verify/ineligible;
foreign/reentry/abandoned-return/Reset; disabled/no-clock operation; disjoint
co-installation, late/competing refusal, partial install rollback and failed
restore retry. Reuse the named AP and FL memo fixtures' unchanged qualification
rather than relabeling it FL diagnostic proof. Native Windows runtime and
CrossOver FL fixture/game behavior, actual FL owner thread and instrumentation
cost remain open. No feature acceptance or installed-build status changes here.

## 5. Census owner, callback frontier and bounded abandonment recovery

Follow-up, 2026-10-30, restricted to census activation. This resolves the
ordinary native scheduling question without adding thread APIs, TLS or locks
to n6/n7/n8. It does **not** prove all private-address invocations single-threaded
or make the existing census byte a universal synchronization primitive.
The producer's new `ownership` section measures 20 reverse-entry reference
sets, 37 decoded direct CALLs, four additional decoded regions and two native
C++ unwind descriptors. Five new mutation/semantic negatives pass. The prior
site/replay evidence is retained unchanged.

### Actual owner and other traversal entry paths

The exact normal owner chain is:

`0x477a9a → main 0x477ba0 → 0x477ed2 → sector driver 0x443b10 →
0x443b42 → collision pass 0x466de0 → n5 0x46734f → pair 0x4a6270`.

Each of main, driver and collision-pass entry has that single encoded caller
and zero absolute entry pointers in the backed image. The driver and main have
no computed CALL/JMP. The collision pass's three computed jumps are its already
qualified bounded class switches, not asynchronous dispatches. It has no computed
CALL; its six explicit method-dispatch calls to `0x4bb500` are outside the
n5 native pair invocation. This identifies the native startup/game owner as
the normal n5 caller, rather than relying on AP's observed thread.

The same main invocation calls cockpit walk at `0x4782ca → 0x41e550` and full
render at `0x4782cf → 0x48b6d0`; ordinary presentation follows the latter.
Both walks have the sole encoded main callers shown. Reuse the established
[window/owner contract](ui-render-exclusion.md#1-native-owner-thread-and-queued-location-changes),
[loop scheduling](loop-phases.md#3-abi-native-esp-cpu-liveness-and-exceptional-paths),
[cockpit chain](chase-camera.md) and
[native command transport](material-submit.md#native-command-scheduling-closure).
Their native VM dispatch is synchronous on the executing owner: ready-list
publication is not a worker which starts the interpreter. This is source/binary
scheduling evidence, not a measured FL thread ID or proof about injected code.

n6/n7/n8 are shared by more than the n5 bracket. The complete surveyed native
entry census is:

| Entry | Decoded direct callers / ordinary source |
| --- | --- |
| Pair `0x4a6270` | P1 `0x46734f` **and `0x467cb8`**; flare narrow `0x4a4534`; helper `0x4a69c2`; self-recursion `0x4a6475/0x4a64ae` |
| Distance collision `0x4a61a0` | Cockpit boom `0x4226a5`; swept scan `0x466a87`; P1 `0x467cec` |
| Distance recursion `0x4a5ef0` | `0x4a623d` from distance collision and self `0x4a6099` |
| Mesh transform `0x4982d0` | Distance `0x4a5ff7` and n6 `0x4a6395` |
| Root query `0x51aae0` | Memo boundary `0x49853a` only |
| BVH `0x51a6c0` | Root `0x51b04d`, self `0x51a866/0x51a9f5/0x51aac8` |
| Triangle leaf `0x51a260` | `0x51a76a` only |

The additional outer paths resolve as follows, rather than being left as
unexplained collision entry points:

- Flare: render `0x48bb96 → 0x48aeb0`, then `0x48af0a → 0x4a3fd0`,
  `0x4a42ba → 0x4a43d0`, `0x4a4534 → pair`. Each intermediate entry has
  exactly that encoded caller. Reuse the [sun-occlusion](sun-occlusion.md)
  probe/cache contract. It shares the frame owner but is outside n5 pair timing.
- Cockpit: main `0x4782ca → 0x41e550`, walk `0x41e5b0 → 0x421f50`,
  boom `0x4226a5 → distance collision`. This is the established registered
  cockpit walk before render, with no worker handoff.
- Swept scan `0x466660` has three callers: P1 `0x466ea1` and native fire
  `0x44f56d/0x44fb32`. Native fire `0x44e310` has four callers
  `0x46bb81/0x46bb9f/0x46bbe4/0x46bc03`, all in the native command dispatcher;
  reuse the [fire contract](chase-fire-aim.md) and synchronous
  VM transport above. Fire calls can occur through an admitted method callback;
  owner-thread evidence alone therefore does not waive reentry handling.
- The remaining pair caller `0x4a69c2` is inside `0x4a6820`, whose sole caller
  is `0x466d34`. That is in helper `0x466c00`, whose sole encoded caller is
  native-command site `0x46dee7`, again synchronous native-command transport.

Every one of these 20 entry scans finds zero absolute entry pointers; every
reported rel32 reference decodes to a whole five-byte CALL with the expected
target. No literal callback publication of a collision, cockpit, flare, main
or driver entry was found. Absence of an entry literal cannot exclude a callback
which computes a private address, jumps to an unexamined interior, replaces a
vtable or switches stacks. Those are explicit unsupported-profile limits,
not reasons to add `GetCurrentThreadId` or TLS to every native leaf.

### Concrete callback and exception limits inside n5

The n6 target and the pinned root/descent/leaf/SAT arithmetic closure do not
call a scheduler, Windows API or indirect callback. n5 is wider: its model-index
lookups at `0x4a6360/0x4a6377 → 0x4a1a40` may demand-load a body. This path
allocates at `0x4a1b64 → 0x4d9120`, parses at `0x4a1ca2 → 0x49ae00` or
`0x4a1d24 → 0x49db00`, and calls media service at
`0x4a1d4b → 0x4b42e0`. The loader's 335 decoded instructions have no computed
CALL, but that does **not** close its transitive callbacks.

Media service has real result callbacks at `0x4b4416/0x4b4462`: its target comes
from `VM + requester_type*24 +0x34`, then receives `(context,1)`. Its earlier
indirect calls at `0x4b436f/0x4b4382` are media-interface operations. The existing
[audio completion closure](ui-render-exclusion.md#2-window-callback-endpoints)
qualifies the fixed native result callbacks as state/ready publication rather
than immediate interpreter dispatch. The existing
[typed allocation-recovery closure](ui-render-mutation.md#allocation-recovery-actual-objects-callbacks-and-finite-closure)
accounts for native D3D/D3DX resources and possible owner-window delivery;
it is not evidence of no callback at all. An externally registered requester
or foreign private call remains outside that fixed native contract.

The pair and driver themselves have no local SEH frame. Model lookup installs
handler `0x54e419`, loading FuncInfo `0x59df98` and jumping to native C++
handler transport `0x521368`. Its descriptor has magic `0x19930522`, ten unwind
states and **zero try blocks**. Main installs `0x54d872`, loading FuncInfo
`0x59cea4`, with one cleanup state and **zero try blocks**. Its only active
cleanup surrounds early allocation (`0x477e14` sets state 0;
`0x477e2a` restores -1 before the flight loop); its cleanup frees that allocation
through `0x54d860`. Thus neither descriptor is a catching frame that restarts
collision or promises another Present. The producer checks the actual metadata,
not the mere presence of FS:[0] stores. The loader's failure branch at
`0x4a1e26` is ordinary control flow, not proof of a catch/restart policy.

A continued exception may resume the same native pair, in which case keep its
pending state. An unwind past the pair bypasses its synthetic post and abandons
the diagnostic sample. No pointer is retained for cleanup: the pending record
contains copied words/numeric addresses. Never report that abandoned record as
a completed pair or repair native collision outputs. There is no established
healthy-native catch/retry event which justifies clearing busy at arbitrary
future n6/n7/n8 entry. Absence of a later owner event must remain fail-closed.

### Smallest supported acquisition and recovery boundary

Use n5's existing pre/post boundary for the pending slot. Establish/check the
already recognized owner **before** touching pending state or adopting busy;
if a thread-ID observation is needed it belongs here once per pair (the current
pre-handler already calls `GetCurrentThreadId`), not per mesh/BVH/triangle.
For the fixed native owner/callback profile above, global plain recursive
counters need no extra per-leaf API, TLS or lock. Their events remain actual
executed calls; rings retain their existing nonwaiting publication guard.
Foreign private entries and modified callback tables do not gain support from
this conclusion and must not be advertised as safely attributed.

There is a smaller conservative recovery option than a new SEH wrapper or new
frame hook: save the **native n5-entry ESP and its incoming P1 EBP**, before any
injected helper frame changes either. The decoded P1 prologue is
`push ebp; mov ebp,esp; and esp,-16; sub esp,0xb4; push ebx/esi/edi`.
n5 then pushes four native arguments and its return slot. On the same admitted
owner, a later n5 entry with **both the same saved EBP and the same entry ESP**
cannot be ordinary nested execution of that still-live P1 invocation: a nested
ordinary CALL/P1 prologue establishes a lower, different frame. The prior pair
activation has ended, so its unclosed sample may be counted abandoned, dropped,
and busy cleared before admitting the new pair. A new invocation reusing the
same stack addresses is equally safe: the previous activation is already gone.
This proof uses the ordinary native x86 call stack, not an alternate-stack or
fiber entry contract.

If either frame value differs, keep nested/unresolved bypass. Do not infer
abandonment from elapsed time, a frame counter, a different object pair, or a
foreign thread with coincident numeric values. Same-slot recovery is deliberately
conservative: it can leave diagnostics suppressed after an unwind to a changed
outer frame. It does not claim complete exception recovery. A proven existing
normal outer-loop/render marker could additionally retire such an abandoned
slot: `0x4782cf → 0x48b6d0` has the sole main caller above, outside the pair
activation on the supported normal graph. An arbitrary device Present is not
interchangeable with that specifically admitted boundary. No extra hook is
needed to express same-slot recovery, and this study does not require enabling
loop diagnostics merely to acquire a new cleanup point.

The producer models this bounded decision with four stack residues using the
actual decoded P1 prologue: four same-slot abandon/readmit cases, four
owner-mismatch refusals despite equal numeric slots, four no-event fail-closed
cases, and four deeper nested-frame refusals. These are integer/decoded-stack
models, not native SEH tests. Native acceptance must distinguish continued
exceptions (retain pending), actual unwind (never publish), nested calls
(bypass without clearing outer state), same-slot later admission (discard stale
sample and proceed), and no qualifying later boundary (remain bypassed).
This is the smallest supported recovery contract established here; a universal
SEH unwinder, lifetime framework or per-leaf thread instrumentation is neither
proved nor necessary for this bounded diagnostic scope.

## 6. Census source binding and scoped qualification (2026-10-30)

The FL census adapter now selects the existing shared recorder and key reader
at cold startup. The four claims retain the sites above; n8 claims six whole
bytes and its 17-byte counter transport replays `push ebp; mov ebp,esp;
and esp,-8` before returning to `0x51a266`. n6/n7 remain 11/16-byte integer
counter transports. The AP encoder bytes and its light callback path are
unchanged. The FL n5 thunk uses the shared diagnostic full CPU envelope,
selects EDI/EBX from saved native registers, supplies native entry ESP and P1
EBP, restores the exact native argument stack, and preserves native output
state around post collection. Matched rounding is required before FNSAVE;
callbacks run with local default FP controls and preserve LastError. Divergent
input rounding forwards without clocks; divergent output rounding retires the
pending sample without publication or a post clock.

Owner capture is at the synchronous EXE seam `0x51eaa2`, before CRT
`0x51e935 → 0x51ea26 → WinMain 0x4ed5c0 → 0x4ed6dd → 0x476b40 →
0x476b68 → startup 0x476bc0`. That same startup calls main at
`0x477a9a → 0x477ba0`; §5 then reaches P1/n5. The exact normal-control-flow
basis is the [startup note](startup.md#normal-startup-cfg-not-address-ordering)
and [owner note](ui-render-exclusion.md#1-native-owner-thread-and-queued-location-changes),
not a first-collision thread adoption. FL n5 checks that owner before pending
access; equal native ESP **and** P1 EBP on that owner drop an unclosed sample
and admit a new one. Different/deeper frames remain bypassed; no qualifying
later event leaves the sample pending. Present does not clear this state.
Synchronous nested calls remain included in global executed-work counters,
while their n5 bracket is bypassed. The fixed native entry/callback domain in
§5 remains a support condition: a foreign direct private leaf call while n5 is
busy cannot be detected by its thread check and is **not safely attributed**.
Modified callback tables, alternate stacks and arbitrary private entries are
unsupported; the plain counters are not universal thread synchronization.

`prequalify_startup` checks the unchanged memo closure plus complete pair/P1
bodies, normalizing HIGHLOW operands, before any overlapping startup owner
writes. `prepare_startup` retains that quiescent proof and still compares exact
own-site ABI windows immediately before claims; there are no unconditional hash
holes or hot-path body checks. The startup owner must publish census after memo
qualification/publication, and reverse that order during rollback. The shared
installer now retains arena-finalization debt from encoder finish, entry-tail
emission or chain publication even if code sites restore, and refuses unrelated
arena debt without adopting it. Restoration of all owned sites precedes arena
finalization; failed obligations remain retryable. Emitters and DLL references
remain process-lived as in the existing patch contract.

Measured host evidence is in
[`verification/results/fl-narrow-census/`](../../../verification/results/fl-narrow-census/):
`produce.py` reproduces two derived pins; `audit.py` compiles/disassembles five
isolated objects without linking or executing PE code. It finds zero x87
instructions in the shared collector and exactly seven explicit x87 transport
instructions in the FL thunk. Two host bases (`0x400000`, `0x530000`) pass the
actual production clean profile and emitted counter checks; six changed-body
controls are refused at each base. The owner/key/encoder harness has 41 checks
per base, including four stack residues. The existing ten AP host tests and
four FL tests pass, with no skips. This is cross-compilation and host/static
qualification, **not native Windows behavior**.

The existing CPU fixture has been extended, but has not been linked or run in
this checkpoint. It adds 36 state-parity scenarios across four stack residues
and nine FP/result modes, deliberate clock-helper x87/XMM/LastError clobber,
zero-clock disabled/input-refusal cases, output-mode discard, owner/nested/
foreign/same-slot decisions, a nonlocal native stand-in escape, unrelated and
owned arena debt (finish/entry/chain), partial-claim rollback and restore retry.
Its native FL stand-in uses the mapped call/entry windows and ABI; it does not
execute the full game collision bodies. Native SEH/continued exception and real
unmasked-fault transport, co-installed memo miss/hit behavior, rebased native
execution, FL/CrossOver game behavior, actual thread observations and measured
instrumentation cost remain unqualified. Diagnostic timings must not be read
as game FPS. No DLL, installation, game launch or feature acceptance is claimed.

Census fixture review correction (same checkpoint): the synthetic caller now
places its continuation below all six n6 words and JMPs to the native CALL site,
so the 24-byte native cleanup reaches that continuation. The previous extra
CALL inserted a return over the first argument. Emitted-object stack replay
checks four residues and rejects that extra-return negative in all four;
native fixture assertions check the return slot, all six words, entry ESP and
fully restored caller ESP on every state scenario. Five focused FL host tests
pass after this correction; production source is unchanged and native execution
remains pending. The portability gap is also tracked in
[platform portability](../../architecture/platform-portability.md#2026-09-20-collision-query-timing).
