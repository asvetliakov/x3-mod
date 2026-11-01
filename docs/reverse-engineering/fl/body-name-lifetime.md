# FL body-name publication and retirement

2026-10-29; static RE at source checkpoint `aba7a07a`. This note supplies the
native ID/name validity input for the existing 42-name lens-flare bitmap policy.
It adds no production code, shader change, PE build, fixture execution, Wine run,
game launch or install. Addresses are preferred VAs at image base `0x400000`;
FL integration must use the admitted loaded base plus RVA.

The [engine-body input note](engine-body-inputs.md) owns table
grammar and numeric/name mappings. [Resource callers](resource-callers.md)
owns the name getter. The [cull-chain contract](cull-chain.md)
owns the node ID and bitmap-consuming seam. Existing engine identity belongs to
[registry binding](registry-binding.md); admitted render/load/close
belongs to [load readiness](ui-load-readiness.md),
[render exclusion](ui-render-exclusion.md) and
[render mutation](ui-render-mutation.md). Reuse those contracts.

## Evidence and result

Run [verify.py](../../../verification/results/fl-body-name-lifetime/verify.py):

```sh
python3 verification/results/fl-body-name-lifetime/verify.py
```

The [compact result](../../../verification/results/fl-body-name-lifetime/verification.json)
records two candidate scope spans, 76 operand assertions, six exact direct-call
edge sets, 16 negative controls, seven independently decoded regions and two
synthetic relocated address maps. The producer uses the project's PE mapper,
objdump parser and instruction-aware hook checker. Six changed-operand controls
redecode altered bytes without relying on whole-region hashes; other controls
cover truncated spans, interior controls and actual premature-publication
counterexamples. Neither input nor synthetic machine code is executed.

Measured input: `/mnt/c/X3FL-orig/X3FL.exe`, 2,804,736 bytes, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Hashes are provenance, not an admission policy. Ghidra 12.1.4 independently
corroborated the function ownership and direct references using the existing
read-only `FLRegistry/X3FL.exe` database. Raw output stays outside the checkout
under `~/.cache/x3m-port/fl-body-name-local/`. Reproduce a bounded context query
from the main checkout:

```sh
~/ghidra_12.1.4_PUBLIC/support/analyzeHeadless \
  "$PWD/verification/results/fl-port/registry-writers/ghidra" FLRegistry \
  -process X3FL.exe -readOnly -noanalysis \
  -scriptPath "$PWD/tools/analysis" \
  -postScript X3FunctionContext.java /tmp/fl-body-name-context.local.txt \
  4866e0 486c60 487100 486450 487740 486280 48efa0 48f570
```

The finite direct routes support **one registration publication scope plus one
bulk-restore invalidation scope**, reusing existing factory/load/final-teardown
ownership. They do not require an observer for each name free or a generic
observer bus. Complete computed-writer/foreign-entry closure is not asserted;
the exact static limits and remaining implementation checks are below.

## Native records and normal commit points

Manager is the engine under `*0x5aa100`; fixed count `+0xb4`, dynamic count
`+0xb8`, records `+0xbc`, stride `0x1c`, separately allocated name `record+0xc`.
The ordinary initialized fixed count is 11,000. Pointer/header equality is not
a generation: reset, restore and allocator reuse can preserve all three values
while changing the ID/name relation.

| Operation | Established publication/replacement/free contract |
| --- | --- |
| Constructor/factory | Constructor zeros `+0xb4/+0xb8/+0xbc` at `0x48278e/798/7a2`. Factory publishes engine globally at `0x48a5f1`, then fixed=11000, dynamic=0, table allocation at `0x48a618`; BodyData init is called at `0x48a643`. Global visibility precedes completion. |
| Name setter `0x4866e0..0x4867ad` | ECX=manager, stack `(id, string-object*)`, **ret 8**. ID is mapped and bounded before touching a record. String object length `+0x10`; capacity `+0x14 >=16` selects its heap pointer, otherwise inline bytes. Empty length stores null at `0x486745`. Nonempty input allocates/copies NUL-terminated bytes, then stores the resulting pointer at **`0x4867a2`**. Allocation failure stores null too. |
| Registration `0x486c60..0x486ee9` | ECX=manager, stack `string-object*`, **ret 4**, integer ID in EAX. ECX is saved/restored by native code. Empty/NULL tokens, numeric conversion and duplicate-name hits do not insert a new name. The named insertion path increments dynamic count at **`0x486d8a` before allocation/name commit**, may replace table at `0x486dd9`, initializes record fields, calls setter at `0x486e79` or `0x486ed2`, then returns the ID. Seven normal returns; reallocation failure decrements count and returns -1. |
| BodyData init `0x487100..0x487416` | Loads manager globally, resets record names to null at `0x487182` during the full initialization loop, reads BodyData and calls setter at `0x487372`. One callee-cleaned stack word (**ret 4**); factory pushes that word at `0x48a63c`. Do not model this as a stack-neutral no-argument native call. |
| Dynamic reset `0x486450..0x4865aa` | ECX=manager, no stack args, plain ret. Frees each dynamic name at `0x4864a6`, clears name and adjacent metadata, reallocates/shrinks the records through CRT realloc at `0x486541`, publishes returned table and resets dynamic count to zero. Both success and null-result paths replace `+0xbc/+0xb8`; neither requires a changed address. Fixed names are retained. |
| Restore `0x487740..0x487a20` | Uses global manager and one stack stream pointer, **ret 4**. Calls reset, reads dynamic count directly into `manager+0xb8` through the reader's destination alias, reallocates records and writes table at `0x4877d5`. For each nonempty saved name it publishes its allocation at **`0x48782a` before reading bytes**, fills them at `0x487848`, and appends NUL at **`0x48785c`**. It later reloads scene/group state, which can register further body names, then returns EAX=1 unconditionally on its normal tail. |
| Full body free `0x486280..0x486361` | Uses global manager, no stack args, plain ret. Frees all fixed/dynamic names at `0x4862d4`, clears their record pointers, then frees the table at `0x486348`. **It does not clear table pointer or either count afterward.** The global/header can still describe freed storage. |

The setter never frees the previous name pointer on either store path. A
replacement is nevertheless a logical retirement of the old name binding;
the old allocation's physical survival does not preserve its semantic identity.
Do not retain that old pointer or infer unchanged names from readable memory.
The setter's two committed stores are useful inspection seams, but adding two
store hooks is unnecessary when their finite callers are already covered.

Registration's count increase is not a committed new row. Likewise a restore
record's nonnull pointer is not a complete name. A Present poll or publication
at either earlier store can expose a partially registered row. A successful
registration return is late enough to sample the actual bounded record name;
the caller has not resumed to consume EAX yet. Sample that record, not merely
the requested string: allocation failure can replace the requested name with
null, whose effective name is the numeric default. Numeric registration can
return an invalid integer without creating any slot; validate mapping/range.

Restoration's EAX=1 alone does not certify names or allocation success. There
is no null-result guard between its table store and the later record work.
Keep the consumer disabled on unvalidated counts, null/unreadable storage,
abnormal exit or unresolved mutation. This is consumer refusal, not a proposed
repair to the native reader or allocator failure behavior.

## Finite route coverage and existing owners

The global linear `.text` census and Ghidra agree on these direct call sets.
There are zero unaligned literal pointers to the six target entries in
`.text/.rdata/.data`; that bounded negative does not exclude computed aliases.

| Target | Complete surveyed direct callers | Existing ownership implication |
| --- | --- | --- |
| Name setter `0x4866e0` | `0x486e79`, `0x486ed2`, `0x487372` | Both registration routes, or BodyData initialization. |
| BodyData init `0x487100` | `0x48a643` | Existing factory scope covers initialization and its calls. No extra init observer follows from this image. |
| Dynamic reset `0x486450` | `0x487754` | Entirely inside restore. A restore scope covers frees, table movement, count reset and same-address reuse. |
| Restore `0x487740` | `0x48f1ee`, `0x48f5d9`, `0x49363f` | The last is inside renderer loader `0x493510`, reached by existing full-load scope `0x479430 → 0x47984c`. The first two are playback routes, not that enclosing load. |
| Full free `0x486280` | `0x48a968` | Final engine teardown `0x48a950`, reached from fatal cleanup at `0x476411`. It precedes render-registry destruction `0x48abad`. |
| Playback update `0x48f570` | `0x48bded` | Native render tail, after the last lens/node/camera work. |

There are **46 direct calls to registration**, including startup/type loading,
scene loading and group restoration. The count is static, not dynamic cost.
It is not correct to restrict registration to initial resource enumeration.
An actual conditional render-time route is:

`0x48b6d0` render → call `0x48bded` to playback `0x48f570` →
call `0x48f5d9` to restore `0x487740` → call `0x487a0d` to group loader
`0x4881a0` → body registration (for example call `0x488258`).

Playback's stream marker and loaded group data determine whether these calls
actually register names. This is a reachable native route, not a measured
per-frame registration or an invented allocator-to-render callback. Initial
playback `0x48efa0` also calls restore at `0x48f1ee`; its direct caller is
VM dispatch site `0x4b1e19`.

The existing common owner in `ui_scale_fl.cpp` holds the complete `0x479430`
load scope, complete `0x48b6d0` render scope, and deferred close protocol.
`object_lifetime` supplies owned engine/registry epochs and rejects in-flight
factory/load/mutation state. Factory initialization must use that existing
provisional/completed distinction; do not adopt `*0x5aa100` as a new owner.

Reuse common load/factory changes to retire the bitmap and build one baseline
after committed readiness. Reuse final-teardown/close exclusion: deferred close
cannot free these records while healthy native render borrows remain; normal
fatal teardown does not return to another admitted render. No per-name-free
observer is needed for that covered path. The registry observer alone is too
late to authorize a **body-table dereference during teardown**, because body
free occurs first. The cached bitmap is owned scalar data; it must not trigger
native reads until fresh common admission and owner identity are available.
Foreign direct cleanup calls, arbitrary nonlocal recovery from fatal cleanup,
or an unobserved replacement writer remain outside the admitted native routes.

Playback is the concrete exception to using only common load generations.
The same admitted render can restore the table at its tail and return with no
full-load generation change. Pointer/count equality also fails when realloc
returns the same address and the new record count matches the old count.
One restore dirty boundary covers this exception and its nested reset. Do not
add a separate observer for every free, pointer store or saved-name byte copy.

## Minimal publication boundary and hook ABI

The smallest next integration is feature-local and uses existing owners:

1. At admitted render entry, clear the enabled flag first, obtain the existing
   common borrow stamp and fresh owned engine binding, and compare copied
   incarnation/load/name-revision state. Rebuild the bitmap once when needed,
   validate completion, then enable it. Retain no native table/name pointers.
2. Wrap registration as one mutation scope. Disable bitmap use before forwarding
   native registration, then on normal committed return sample the returned
   ID's actual effective name and update owned bitmap state before the native
   caller resumes. A single name replacement must **clear an old matching bit**
   as well as set a new one. Do not rely on the old address, old count or the
   registration request string. Factory/bulk-restore nesting keeps publication
   disabled until the enclosing owner is complete.
3. Wrap restore once: retire the previous name revision before its reset/free,
   keep the bitmap disabled through its nested registrations and byte reads,
   and mark one rebuild needed on normal completion. The next admitted render
   entry builds from the complete table. On abnormal completion keep it
   invalid. Common load/factory invalidation already handles their outer scopes.

For this finite image, those two new scopes cover all three setter callers
and the direct restore writer. An independent writer-closure review should
assess the computed/alias limitations before production admission. No new
general-purpose observer bus, per-frame table poll, resource-ID list or shader
variant is required by these findings.

| Candidate scope | Plain-copy entry span | Native continuation / normal return |
| --- | --- | --- |
| Registration | `0x486c60` (RVA `0x86c60`), 8 B, `515356578b7c2414` | Tail `0x486c68`; seven original **ret 4** paths; caller-visible integer EAX and actual output state must survive post-bookkeeping. |
| Restore | `0x487740` (RVA `0x87740`), 6 B, `558bec83e4f8` | Tail `0x487746`; original **ret 4** at `0x487a1d`; exact one-word stream stack contract. |

Both spans start/end on independently decoded whole instructions, contain no
relative control or absolute operand, and have zero direct/literal interior
references in the bounded census. Both start at qword offset zero; startup
installation/rollback exclusion is still required. The producer checks code
mapping at alternate bases `0x10000000` and `0x50000000`. Four selected absolute
manager loads/stores have HIGHLOW relocations. The root is zero-filled virtual
data; relocation evidence qualifies address mapping, not runtime root contents.

At registration entry ECX is input manager and `[entry ESP+4]` is a borrowed
string object. Native prologue saves ECX/EBX/ESI/EDI before interpreting it.
At restore entry `[entry ESP+4]` is the borrowed stream; ECX is not the manager
contract, because native code loads the global at `0x48774b`. Preserve the
original ESP, nonvolatile registers and actual return cleanup. A wrapper may
not accidentally pop the stream twice or turn either function into cdecl.

Use the existing qualified whole-call transport: preserve GPRs/EFLAGS,
XMM0–7, x87 stack/control/status and exact MXCSR around bookkeeping, with
four-byte incoming stack support. Preserve incoming LastError before native
entry, then capture and restore the native **output** LastError around committed
sampling. Restoring only the incoming error after the entire call would change
native behavior. These selected functions have zero x87 instructions, but
registration/restore reach allocator, CRT and reader paths; that is not a light
envelope qualification for their transitive call graph. Reuse the existing
FNSAVE/FRSTOR transport rather than assuming FXRSTOR preserves live FEX tags.

Registration and restore install no native FS:[0] frame in their complete
decoded bodies; BodyData initialization does. Allocator recovery and file-read
paths are known callback/failure surfaces from the existing notes. Keep
publication disabled across the whole native call and any legitimate nested
registration. Reentry is not proven impossible. Bookkeeping itself must be
bounded, allocation/log/callback-free and must not let a C++ exception escape.
Foreign unwind/search/continuation must use the already qualified scope
transport; normal-return-only cleanup or GNU RAII is insufficient. Never
re-enable the bitmap merely because a pointer/header still matches after an
abnormal exit. No exception was executed in this RE.

## Before-cull timing and cost

Existing `x3m_fl_ui_enter` first admits/publishes the common render scope, then
calls `x3m::fl_render_enter`, before forwarding the original native render
prologue. That is an existing integration point for a pending bitmap refresh.
It precedes lens preparation `0x48bc78`, lens publication `0x48bc87` and the
shared native cull walker at **`0x48bc96`**. Therefore a refresh there can finish
before the first cull in the next admitted invocation. Present/`begin_frame`
timing is not a substitute: render-tail playback can invalidate names after
that frame's cull work and without changing the next frame's table tuple.
There is no further scene/camera/node dereference after playback returns at
`0x48bdf2`; reuse the existing render-tail proof, including its cleanup-depth
and exception limitations.

The current AP refresh code in `lens_flare_cull.cpp` polls table headers and
mapped dynamic names at Present; its FL branch explicitly leaves enabled=0.
These facts do not make AP polling an FL publication authority. Reuse the
existing 42-name ASCII comparison and bitmap arithmetic, replacing only the
native ownership/publication input. Sample/copy at most eight bytes per
candidate short name under the finite native borrow; do not call the allocating
native string getter. Preserve null-name numeric-default behavior and the
existing 32768-ID bitmap range; invalid/unsupported IDs stay uncullable.

Numeric alias IDs deserve explicit care: the native getter uses the requested
ID when formatting an unnamed slot. For example ID 9061 aliases slot 61 but
formats `v\09061`, whereas canonical ID 61 formats `v\00061`. A named
override uses its stored name for either ID. A canonical slot inverse is not
proof of every alias's effective name. This task does not enlarge the shared
policy or claim alias coverage; do not add bits by guessing equivalence.

Hot-cost pass, inferred from source/control flow: keep the per-node path as
the existing integer bitmap test. Common render entry needs an owned epoch/dirty
comparison, not a table scan each frame. Full baseline work scans the current
fixed/dynamic rows only after committed factory/load/restore; registration
updates one effective ID synchronously and compares against the fixed 42-name
set. All table/name pointers are discarded before returning. No new per-node
string lookup, allocation, native getter, image verification, lock or registry
walk follows from the contract. Actual baseline frequency, registration cost,
FL flare coverage and native Windows/runtime behavior remain unmeasured.

## Implementation checkpoint

The feature-local owner now has source implementation of the two reviewed
whole-call scopes and fixed startup/common/factory/load/close integration.
Policy, scoped publication, source-derived cost and host/native fixture limits
are recorded in the [body metadata ledger](../../verification/farnhams-legacy-body-metadata.md).
The bounded native route assumptions above remain binding; owned scalar cache
publication does not establish arbitrary foreign mutation or computed-writer
closure, and does not enable the outer renderer/family gate.
