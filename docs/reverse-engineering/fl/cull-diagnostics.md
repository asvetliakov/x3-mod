# FL cull census and LOD-switch diagnostic contract

Offline study, 2026-10-30. Preferred VAs use base `0x400000`; integration uses
the admitted loaded base plus RVA. Input is clean `/mnt/c/X3FL-orig/X3FL.exe`,
2,804,736 bytes, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Hashes identify evidence, not a new admission rule. **No production edit, build,
CPU execution, Wine, attach, game launch or installation occurred.** AP remains
usable while this task reads separate offline files.

The existing [census ledger](../../verification/cull-census.md),
[AP selector](../lod-selection.md), [FL cull chain](cull-chain.md),
[FL LOD binding](lod-runtime.md),
[small-prop inputs](small-props-inputs.md), and
[body-name lifetime](body-name-lifetime.md) retain their ownership.
This note maps the active diagnostic and its `X3M_LOD_SWITCH_LOG` dependency;
it does not reopen retired renderer features.

## 1. Reproduce and strength of evidence

```sh
python3 verification/results/fl-cull-diagnostics/produce.py
```

The [producer](../../../verification/results/fl-cull-diagnostics/produce.py) uses
the project PE mapper, Capstone and the project's independent objdump parser.
Its [compact receipt](../../../verification/results/fl-cull-diagnostics/contract.json)
records measured static counts: **407 complete selector instructions, 466 CFG
edges, 39 exact operand anchors, 41 hash-independent negative controls, three
independently decoded regions, 12 copied-tail relocation cases and 72 decoded
transport interpretations**. The whole `.text` decode surveys **72,241 direct
edges**. No raw listing, native instruction dump or PE copy is emitted.

The controls change real instruction operands, a branch destination into the
saved span and a synthetic stub's argument displacement. The last control
fails the decoded argument contract, not a byte-hash gate. The split-entry
control changes unanchored branch `0x495fc2` to the valid
instruction boundary `0x495f2a`; its exact rejection reason must be
`interior_edge`, after operand and complete-CFG checks succeed. Interpretation
models integer stack/register transport and callback clobbers; it does not
execute x86 or qualify a compiled handler, actual rollback, unwinding or native
Windows behavior. Exact-span agreement with objdump is independent of the
Capstone decoder. Computed entries, alternative x86 decodes and foreign patches
remain outside the finite direct/raw/literal surveys.

## 2. Measurement and common exit

Selector **`[0x495e50,0x496446)`** is thiscall, ECX=node, first stack word=camera,
second stack word=propagated distance flag; both normal returns pop eight bytes.
The ordinary scene walker calls it at `0x497745`, and it recursively calls itself
at `0x495f37`. The cull-chain note establishes its complete branch graph and
call boundaries. Native recursion over children begins after the exit seam.

| Contract | AP existing | FL measured |
| --- | --- | --- |
| Measure claim | `0x47d258`, 6 B | **`0x496182`, 10 B**, RVA `0x96182` |
| Displaced measure operations | Load threshold | Store EAX to `[ESP+0x10]` (4 B); load EAX from `[ESI+0x1dc]` (6 B) |
| Measure continuation | `0x47d25e` | **`0x49618c`**, TEST EAX,EAX |
| Node / native measure | EDI / ESI | **ESI / ECX** |
| Final pixel metric `s` | `[ESP+0x2c]` | **EAX or `[ESP+0x18]`**, before displaced store |
| Focus-scaled D | EBX / `[ESP+0x10]` | **EDI**; current `[ESP+0x10]` still holds D at entry |
| Camera | EBP / stack | **EBX and `[EBP+8]`** |
| Common exit claim | `0x47d528`, 6 B | **`0x495f27`, 6 B**, RVA `0x95f27` |
| Displaced exit operations | Child head, compare | ESI=`[ESI+0xc]` (3 B); compare DWORD `[ESI]` with zero (3 B) |
| Exit continuation | `0x47d52e` | **`0x495f2d`**, JE `0x495f43` |
| Final selected record | node `+0x14c` | **node `+0x150`** |
| Model ID | node `+0x140` | **node `+0x144`** |

At measurement, the native width-scaled metric is final in ECX, and EAX plus
the duplicate local contain the final pixel metric `radius*640/D`, including
the native near-distance sentinel and zero-quotient clamp. The displaced store
must execute once. Reading `ESP+0x10` as `s` before that store records **D**, a
semantic error. The native width comes from `[*0x5aa100+0x5c]`; the integer
helper call is `0x496133 -> 0x412b50`, with caller cleanup at `0x496138`.
Camera focus comes from `camera+0x298`. The environment-map arm at `0x4961ca`
later zeros only ECX and can clear renderability; keep the recorded measure
timing consistent with AP, including `culled_other` for that later action.

`0x496182` has no incoming direct branch: it follows `0x49617e`. At the exit,
six direct branches target exactly `0x495f27`: `0x495fc2`, `0x495fe3`,
`0x4960b4`, `0x4960c1`, `0x496431`, `0x496441`. Fallthrough from `0x495f24`
also enters it. There are zero surveyed direct, raw-rel8/rel32 or unaligned
literal-DWORD targets inside either displaced span, and neither contains a
HIGHLOW relocation. The incoming ordinary calls to the complete selector and
absence of external direct interior calls reuse the cull-chain evidence.

The natural threshold load at `0x496186` is a valid six-byte boundary but its
five-byte redirect crosses an aligned qword. Moving four bytes earlier permits
the existing atomic writer: `0x496182 % 8 = 2`, and 2+5 <= 8. **The exit has
offset 7 and remains a plain write with today's writer.** It needs the existing
startup/quiescent execution exclusion for installation, partial rollback and
restoration. Pinning the DLL keeps callbacks alive but does not make that
write atomic. Do not claim concurrent dynamic-detach restore safety. Moving
the exit back to `0x495f24` is unsafe: six branches would enter the new saved
span's interior. No new writer or lifecycle framework is justified here.

## 3. Stack, live state, exceptions and reentrancy

After the aligned prologue, site ESP is `align_down(EBP,8)-0x28`; legacy entry
alignment is only four bytes. Camera extraction must use **EBP+8**, not a fixed
ESP argument offset. All local offsets above are at the untouched site ESP.
For example, saving EAX/ECX/EDX, then pushing camera and D, requires a
`[current ESP+0x2c]` load of the original duplicate `s` at ESP+0x18. The decoded
measure witness passes cdecl `(node=ESI,measure=ECX,s,D=EDI,camera=[EBP+8])`,
removes 20 argument bytes and restores the three volatile registers.

Measurement must preserve **ECX, EAX, EDI, ESI, EBX, EBP, ESP** until the shared
tail; it saves EDX too. EAX is live as the displaced store's input, unlike AP's
threshold-load seam. Incoming arithmetic flags are dead: the first native
consumer follows `TEST EAX,EAX` at `0x49618c`. Exit must preserve original ESI
until the callback receives the node, then let the tail replace it with the
child-list head. EDI is camera on all native exit arrivals, including the
`0x495f24` reload and `0x496396` final-selection reload. EAX/ECX/EDX can reach
leaf-return paths and are preserved; the displaced CMP regenerates the flags
used by `0x495f2d`. Control flags must remain unchanged.

Preserve XMM0–7, MXCSR, the incoming x87 stack/control/status and LastError.
The selector has no x87 instructions; that does not prove empty incoming x87
state. Reuse the census's integer-only, no-exceptions compiled handler boundary
and its four-byte-stack support. A TLS/Win32 admission call or a new native
reader cannot silently be added to that light envelope. Any such transport
needs the existing qualified full-state boundary or a separately audited
integer-only implementation; generated FL code and its compiler audit remain
unperformed. The test callback deliberately clobbers volatile registers, but
is an interpreter action, not a compiled preservation fixture.

The exit precedes each node's child recursion, so ordinary depth-first traversal
fits the existing single pending entry and ancestor stack. The selector's
model call `0x496222 -> 0x4a1a40` is a **mutable resource loader**, not a pure
getter: its prologue installs FS:[0], and its miss/recovery paths allocate and
call other subsystems ([resource recovery evidence](dust-cleanup-timing.md)).
Neither a no-throw nor an arbitrary-no-reentry proof follows. Unwinding between
measurement and exit must leave an incomplete row, disarmed/reseeded through
the existing render/Reset lifecycle; no held lock, new native lease or cleanup
scope is added by these integer callbacks. A nested/foreign render cannot
inherit an outer sample's validity merely because the node token matches.
Reuse actual current pass/thread admission and invalidate unavailable partial
diagnostic data. No exception or reentrant callback was executed in this work.

## 4. Model provenance, ladders and labels

FL model lookup takes **ECX=body ID**, returns EAX and uses no stack argument at
this callsite. At `0x496227`, its result replaces the native metric in local
`[ESP+0x14]`. Those are the slot's only two explicit writers in the selector:
`0x496142` stores the metric, `0x496227` stores the model. **EBX is a signed
LOD count/index**, first loaded from model+0x10 at `0x496246`; it is not AP's
model pointer. Applying AP's `EBX == model_slot && EBX != D_slot` witness in FL
would discard or misidentify useful ladders. The old D slot becomes `s` and can
be modified again by adaptive LOD scaling; use the measurement's owned D.

For a **matched measured row that exits kept** (`flags_out & 2`), the slot is
lookup output. The producer walks all measure-to-exit paths before publication:
the sole route to the child seam without `0x496227` is `0x496217 -> 0x495f24`,
which follows the explicit clear at `0x49620e/0x496211`. Thus kept completion
requires publication in the native selector graph. This is an intraprocedural
proof under the same admitted stable native fields, not arbitrary callback or
foreign-writer closure. A null result gives unknown ladder. Culled rows may
have either pre-lookup metric or post-lookup model in that slot; without a
stronger provenance witness, record no model pointer for those rows. All rows
retain their useful metric, verdict, ID and final LOD data. Early exits before
measurement remain `unmeasured`, as AP does.

Native model layout is reusable: record-pointer array **+0xc**, signed count
WORD **+0x10**, per-record switch DWORD **+0x34** (`0x49630c`, `0x496246`,
`0x496372`). Keep the existing cap of eight thresholds, negative/zero count
semantics and unknown formatting. Raw thresholds differ from the effective
runtime threshold after registry factor `+0x750`; the row's `thr`/`T_pad` remain
raw record values, matching AP's meaning.

Body manager root is **loaded base+0x1aa100**. Counts **+0xb4/+0xb8**, slots
**+0xbc**, stride **0x1c**, name **+0xc**, fixed count **11000** and ID mapping
are already qualified by the body-input/name-lifetime notes. Null names retain
the existing requested-ID numeric default, including aliases; arbitrary names
are not inferred from a canonical numeric alias.

**Acquire optional enrichment while a current existing native borrow is live.**
One exact ladder acquisition point is the census exit callback, before replay
of `0x495f27`, for the matched kept row above: model lookup has returned, native
LOD/model reads have completed and no further call occurs before this seam.
Bounded header/record copying there can populate the existing per-frame ladder
cache with owned values; retain no permission to dereference the model later.
The matching current node/camera/engine identity and complete-pass admission
must still be valid. Unavailable/malformed/stale data yields unknown without
losing the core row. Readability by itself is insufficient.

For body labels, reuse the existing body-metadata publication and retirement
boundaries. Its current public snapshots expose classes/prop flags, **not all
body names**; the optional engine-body resolver names only its supported body
set. Neither supplies arbitrary labels today. A bounded label copy for an
actually measured ID is possible in that same admitted census interval, using
the existing current manager/name revision, mapping and page-bounded name
semantics, and keeping only the formatted owned suffix in the existing census
cache. Alternatively extend the existing cold body publication's optional
diagnostic output. This is enrichment of the current cache/owner, not another
name lifecycle or per-draw resolver. The implementation choice belongs to the
integrator; no new lifetime hook is established by this study.

Present may log owned data and compare the existing tracker. It may **not**
simply rebase AP's `engine_memory::read` enrichment: render-tail playback at
`0x48bded` can restore names before Present, and full body free does not clear
the table header. A page being committed/readable cannot establish matching
identity. Keep supported kept rows, counters and switch events even when their
optional `body`/`T_pad` labels are unavailable; do not disable the complete
diagnostic because one label cannot be acquired. Useful supported labels should
be copied in the valid interval, not replaced universally with unknown.

## 5. Minimal edition binding, coexistence and cost

Retain one census ring, one tracker and existing `initialize/install_at`,
`begin_frame`, `present`, `reset`, `shutdown` ownership. A startup-selected
binding supplies the two windows/spans, extraction encoders, node model/LOD
offsets and body global. AP offsets and its existing pointer witness remain
unchanged. FL needs the ten-byte measure span and different exit model
normalization; changing only AP's executable check is insufficient. Reuse
recognized FL identity, loaded-base resolution and existing common lifecycle.

`X3M_CULL_CENSUS=1`, `X3M_LOD_SWITCH_LOG=0..4096`, debug default cap 16, the
8192-entry bound, 16384-slot/16-probe tracker, per-view key, previous-frame/model
reseed and Reset behavior all stay shared. Direct `X3M_LOD_SWITCH_LOG` alone
currently reports `census_off` unless census/debug is enabled; the launcher
implies census. Active config schema rows 632–636 remain in scope. No schema,
option, verdict or log-row family is retired. `culled_prop` remains the join to
the existing draw-path `note_culled_prop`, without another material hook.

The measure `[0x496182,0x49618c)` and exit `[0x495f27,0x495f2d)` claims are
disjoint from shared cull `[0x4961d3,0x4961dc)`, station LOD context
`[0x495e82,0x495ea1)`, material LOD context `[0x4e56b1,0x4e56c5)`, material
whole-call hook `[0x4e7142,0x4e7147)` and existing body registration/restore
scopes. They read final effects of the existing cull chain and patched LOD
policy. The root `flag31` ancestry diagnostic still describes the actual root
flag; it does not certify that the station-distance patch is off. Own/target/
prop classification already belongs to the material and body-metadata adapters.

Source-derived cost: disabled/unrequested installs nothing; installed disarmed
sites pay one JMP-dispatch trip and byte test per reached site, or two of each
for a node that reaches both. Early-return paths can skip measurement or both
sites. Armed frames write bounded ring fields, resolve at most 16 ancestors, and update
integer counters, with no per-node allocation or logging. LOD-switch mode arms
these paths every frame and scans kept entries at Present with up to 16 probes.
The current AP fixture measured roughly 8 ns/node additional armed handler cost
and 4.02 ns/tracked node for scanning (2026-09-24 ledger); these are **AP fixture
measurements, not FL predictions or game FPS**. The FL transport and optional
safe enrichment have no measured execution cost. The existing 256-slot direct
caches can collide and cause repeated copies; they are not a guarantee of one
read per distinct model/ID. Moving bounded copies into the valid interval adds
real armed-path work, especially under always-armed switch logging, and must
be measured before claiming parity. Keep labels demand-driven and shared;
no per-node registry walk, loader call, new lock or second census follows.

Remaining integration acceptance: compiled CPU/LastError preservation, actual
startup/rollback/restore exclusion (particularly the plain exit write), genuine
nested/exception denial, owned enrichment lifetime and cost, frame/view joins,
FL emitted rows and native Windows execution. This static contract does not
claim any of those have run.

Open implementation decisions remain the supported admission transport at the
light integer boundary, an exact pending-entry pass/view match under the current
borrow, and the immutable diagnostic body-metadata demand with a name-revision
key for cached labels. Existing node-only pending matching and page readability
do not decide those contracts. The integrator must select and qualify them
before admission; this study implements none of them.

## 6. Ratified integration baseline

Design follow-up, ratified by the parent on 2026-10-30; source/note inspection
only. Use the existing full-envelope admission and bounded owned enrichment
described below; measure their cost before adding another admission mechanism.
No production code, build, CPU fixture,
Wine, attach, game launch or install is part of this follow-up. The required
change is safe owned enrichment and exact admission/matching. A new fast
admission mechanism or tracker scheduling change is an optional optimization,
not a prerequisite justified by measured FL cost.

### Existing borrow and recommended baseline

The existing borrow really covers these selector seams. The common owner wraps
native render `0x48b6d0`; its `0x48ba4c -> 0x497720 -> 0x497745` scene path reaches
the selector within that scope. `ui_scale_fl.cpp::x3m_fl_ui_enter`, kind 15,
publishes the common render scope before `body_metadata_fl::render_enter`, the
renderer callback and native forwarding. The lens traversal is likewise under
that complete pass. Reuse this owner and its normal/unwind cleanup.

`fl_render_admission::current()` is **not** integer-only: it uses `TlsGetValue`
and saves/restores LastError with Win32 calls. `current_depth()` calls it again.
`object_lifetime::fl_owned_binding()` takes the existing metadata lock.
`capture.cpp::fl_render_entry_body()` takes the capture mutex and requires
renderer/TAA admission. `navigation_borrow::Copy` explicitly excludes render
callbacks. None of these facts can be hidden behind a light-callback declaration.

**Recommend the existing full CPU-state envelope around each armed FL handler
for the first qualified implementation**, followed by the current common
admission API. Reuse the qualified `ui_scale_fl_x86.cpp` FNSAVE/FRSTOR plus
explicit XMM0–7/MXCSR save/restore pattern, including LastError and control
flags/DF preservation; do not substitute FXRSTOR on FEX. Disarmed paths keep
their existing dispatch/test cost. The source-specific integer-only census
compiler/audit boundary remains useful for the collector, but does not exempt
the API-bearing outer callback from preservation.

A narrow `fl_render_admission::current_outer(BorrowStamp*)`, implemented beside
`current`, may call `current` once then check existing render depth equals one.
That avoids calling TLS twice without changing authority. It must inherit TLS
health, common/UI completeness, load/drain and current-scope checks. It returns
only the existing stamp; it grants no durable native-memory lease.

This baseline deliberately does **not** achieve zero Win32 calls per armed node.
It avoids per-node observer locks, registry walks and cold validation in the
core collector; bounded optional enrichment uses those existing owned checks
only when a copy is actually attempted. Measure the remaining API/full-state
cost before deciding whether an additional admission transport is warranted.

### Exact binding, matching and startup ordering

Keep one ring, tracker and existing census lifecycle. A startup-selected census
binding supplies AP's existing encoders/offsets or FL's ten-byte measure and
six-byte exit spans, `+0x144` model ID and `+0x150` final LOD. The shared cull
policy binding is not itself this diagnostic binding. Preserve the plain-write
exit's startup/quiescent installation and rollback restriction.

FL measure passes `(node=ESI, measure=ECX, s=EAX, D=EDI, view=[EBP+8])`; FL exit
passes `(node=ESI, view=EDI, model_slot=[site ESP+0x14])`. Extend the single
pending value with `BorrowStamp`, view and census-frame generation, in addition
to node/index. Admission must succeed before native field reads. An exit
completes a row only when node, view, all three stamp fields and frame match.
Only that matched row with `flags_out & 2` can supply its FL model slot; EBX is
never the FL model witness. Culled/unmatched rows keep an unknown ladder.

At existing common entry/finish boundaries, census-specific fixed calls clear
pending/ancestry on pass change and invalidate partial diagnostic history on
nested entry or abnormal completion. A restored outer borrow cannot revive a
pre-nesting pending row. A refused foreign callback must not clear owner-thread
pending data or read native fields. Preserve incomplete measured rows as such;
suppress switch processing and clear tracks for an invalidated frame, so the
next valid frame reseeds. Present/Reset retain their existing clear/disarm roles.
No new native hook or subscriber mechanism is needed.

Freeze census/debug/switch demand in `fl_startup.cpp` after config load and
before common preparation. Diagnostic demand requests existing common/lifetime
coverage independently of motion output, TAA, UI, lens or occlusion defaults.
Preserve direct switch-only `census_off` behavior. Add
`body_metadata_fl::request_census_startup(bool)` before its request freeze for
the existing registration/restore coverage. Prepare common/lifetime/body before
census; roll census back before its dependencies, retaining existing recovery
debts. Do not use the renderer/TAA-dependent capture callback as this gate.

### Owned enrichment, without another lifetime system

Inside the admitted exit callback, before replay, a requested bounded copy uses
the proposed `current_outer` and existing `fl_owned_binding`/`fl_owned_current`
validation.
Copy node/camera handles at `+0x28` within that finite interval, and require the
same pass and owned binding before/after copying. No metadata lock spans native
reads. Reuse documented `ReadProcessMemory` bounded-copy behavior from
`body_metadata_fl.cpp::Read`: failed or partial reads yield unknown.

Copy the ladder header, at most eight record pointers and their `+0x34` values.
Preserve signed-count, null-array and partial/unknown semantics. The existing
256-slot ladder storage holds owned sample values, with row slot/fill-serial
references. A conservative first implementation may copy at most 256 demanded
exit samples per frame, counting failures too, and keep excess rows unknown.
Do not reuse a model-pointer cache hit across later loader calls without proving
resource immutability/invalidation: equal pointers and a common pass alone are
insufficient. No retained native pointer may be followed at Present.

Add `body_metadata_fl::copy_census_label(expected_pass, id, out)` in the current
owner. It checks active/depth/foreign-poison/exhaustion, captures the existing
name revision and fresh common/owned engine binding, maps only the requested ID,
copies the actual table/slot/name and rechecks identities/revision before
returning the formatted owned suffix. Reuse the 256-byte page-bounded NUL search,
63-character output cap, and requested-ID default for a null name, including
aliases. It must not require classification bitmap validity or falsely publish
`Snapshot.valid=true` without that bitmap's baseline. Diagnostic-only copying
needs no 11,000-slot classification scan: it obtains fresh authority for its one
bounded copy. Existing registration/restore entry already changes revision and
blocks use while depth is nonzero; retirement and poison remain authoritative.

Keep the existing 256 body slots, keyed by complete copied metadata domain,
name revision and requested ID. Bound attempts including failures/collisions to
256 per armed frame. Row references include a fill serial so replacement cannot
mislabel an older row; an evicted/exhausted/failed reference formats unknown.
Retain historical owned values for logging even if native names retire later;
retirement withdraws future native-read authority, not the truth of the earlier
copy. No native model, slot-array or name pointer is retained for later reads.

For this first baseline, capture or enabled switch logging constitutes demand;
copy only admitted rows and stop at the bounded budgets. Keep `track_observe`
and switch emission at Present, with the existing 16-probe/first-visit-wins,
previous-frame, model-change and per-view semantics. Switch-only stable frames
may therefore incur bounded copies that produce no emitted event. Count and
measure that overhead explicitly. Moving tracking to exit or adding an early
event probe is optional later work, not a condition of the safety port. Useful
known ladders/labels must be demonstrated; universally returning unknown is not
acceptance. Missing optional data never removes core rows, counters or events.

### Alternatives, concurrency, cost and native Windows

A proposed no-call accessor could publish the current owner's stack reservation
interval at common entry, obtained with documented VirtualQuery, then compare
the actual site ESP before reading owner-confined state. **Do not implement it
as this baseline.** A global enabled flag or stamp alone is not foreign-thread
proof. Range publication would need atomic 32-bit bounds, publish/withdraw
ordering, proof that mixed/stale bounds cannot admit another live stack, and
safe expiry before any scope storage is reused. A foreign reader must never
race non-atomic scope/metadata reads. Nested scope restoration, TLS failure,
unwind, fiber/stack switching and overflow need explicit refusal semantics.
The existing TLS API already supplies thread separation; a range check is not
an already-qualified replacement merely because different stacks usually have
different addresses. Private TEB/TLS offsets and unaudited emutls paths are
rejected. Optimizing this unmeasured diagnostic cost does not justify a new
lifetime assumption or publication framework.

Inferred baseline cost is two full-state/admission callbacks for a node reaching
both sites, integer row/ancestor work, the existing Present tracker scan and at
most 256 ladder plus 256 label copy attempts per armed frame. Admission adds
Win32 calls; enrichment adds existing bounded identity/lock/read work. Nothing
allocates or logs per node. The AP ledger's approximately 8 ns/node armed
increment and measured 4.02 ns/tracked-node scan are historical AP fixture
results, not FL cost predictions. No FL runtime overhead has been measured.

Native Windows uses the same documented Win32 TLS/ReadProcessMemory APIs,
qualified x86 hook ABI and CPU preservation. No Wine-private export/layout is
required. This is Windows-compatible design, not verified Windows execution;
record implementation/runtime gaps in the portability note when integrated.

### Bounded implementation acceptance

Extend existing census/common/body/startup fixtures. Future focused host command:

```sh
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_cull_census verification.analysis.test_fl_ui_scale verification.analysis.test_fl_body_metadata verification.analysis.test_fl_startup_install
```

That command was not run for this note. Required cases are:

- Diagnostic-only startup with renderer/UI/lens/occlusion off; exact claims and
  startup order, dependency refusal, reverse rollback and plain exit-write debt.
- Compiled emitted stubs at all four-byte entry-stack residues; live nonempty
  x87, nondefault FP controls/status, all XMM registers, MXCSR, LastError,
  DF/control flags, GPRs, ESP and exact single replay on core/copy/refusal paths.
  Audit the integer collector with `check_no_x87.py` and the explicit envelope
  separately; do not mistake an interpreter witness for compiled qualification.
- Real foreign-thread, nested-pass, same-node/different-view or pass, TLS
  failure, recovery/unwind and Reset witnesses: no invalid native reads or stale
  completion, no inherited pending row and no false switch after reseeding.
- Kept-model versus culled metric slot; zero/negative/over-eight ladders; malformed
  and partial reads; page-edge/nonterminated names; aliases, same-address name
  replacement and restore before Present; collisions, fill serials and exhausted
  budgets. Mutate/unmap sources after the copy and verify unchanged owned output
  and zero Present-time native reads. Demonstrate both useful known enrichment
  and unchanged core rows/events when enrichment is unavailable.
- Preserve tracker outcomes for views, repeated visits, gaps, model changes,
  Reset and cap overflow. Measure disarmed, armed core without enrichment,
  successful/failed bounded copies, collision saturation and stable switch-only
  frames separately. Record full-envelope entries, API/lock/copy counts and cold
  metadata work as well as runtime. Diagnostic timings are not game FPS.

The parent owns later cross-compilation and serialized fixture execution.
The full-envelope baseline and bounded enrichment policy are ratified;
actual FL cost and native Windows execution remain open.
No new stack admission or tracker relocation is proposed for this checkpoint.
