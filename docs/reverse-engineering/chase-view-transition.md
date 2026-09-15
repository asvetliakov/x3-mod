# Chase view across sector transitions: command origin

2026-09-13, bounded read-only continuation after checkpoint `c7eaf91`.
The [run18 analysis](../verification/run18-camera-loading.md) shows a 44.905 s
load interval with no active cockpit, followed by internal mode 1 and then
manual return to chase mode 258. There are zero hook failures. This establishes
safe hook reentry and a native view selection change, not its script cause.
Docking remains untested. No automatic view restoration is implemented here.

The [framing follow-up](../architecture/elevated-chase-camera.md#transition-reset-verified-native-sites-and-remaining-proof)
lists the native view, boom, connect, camera, sector and object setters. The
successful view-mode assignment remains `0x0042e742`: EAX is the resolved
cockpit and ECX the requested mode. Native `INS_CockpitSetViewMode` is command
`0x30` in dispatcher `0x0042d340`. Its only state change is `cockpit+0x150`;
there is no transition or user-input classification at that assignment.

## Save-load path also writes view mode directly

A third concrete writer is **`0x00419e06`** in cockpit serializer
`0x00419430`: on load, EBP is the new cockpit, EDX is the decoded saved value,
and the instruction assigns `cockpit+0x150`. This bypasses the script mode
command. On save, the same routine serializes the current mode, boom, connect
state and associated camera fields. The load chain is:

`0x00404cc0` → `0x0041f720` (`INS ` section) → constructor `0x0041f8d0`
at `0x0041f83b` → `0x00425e20` (`ICOC` section) → `0x00419430` at
`0x00425e6e`. The save wrapper is `0x00425d60`, serializer call `0x00425d7f`.
A successful load then registers the reconstructed cockpit by its saved handle.
The direct-displacement scan had listed `0x00419e06`; tracing the constructor's
second caller now identifies that store as cockpit state rather than an
unrelated structure. A dispatcher-only trace would miss this cause.

Destructor `0x0041ffc0` has two direct routes: command free at `0x0042d3fd`,
and deleting-destructor `0x0041ccf0` at `0x0041ccfb`. Constructor and destructor
observations must cover their shared actual boundaries, not only script
allocation/free commands. Connect setter `0x00422cd0` also has native updater
callers `0x004224a1` and `0x00422532`, in addition to script call
`0x0042dfce`; these can refresh cinematic state without a new script request.

Run18 does not record which writer supplied mode 1. Its load instrumentation
makes deserialization a concrete alternative to a transition script request,
not proof that saved state was the cause. Restoration on every load would also
change deliberate save loading, and is not justified by the current evidence.
The next bounded diagnostic must distinguish the two actual mode writers and
correlate them with the load/cockpit lifetime, before choosing a transition gate.

## Proven command provenance without a global VM trace

The native KC interpreter is `0x004a26a0`. Its native-call operation is opcode
`0x82`. The interpreter decrements the opcode, reads a byte from
`0x004a4688 + opcode - 1`, then dispatches through `0x004a4490 + index*4`.
For `0x82` the table index is 88 and target is `0x004a3880`; `0x83` instead
maps through index 89 to the return handler at `0x004a3986`.
At `0x004a3880` it reads a 16-bit native group and a 16-bit command ID from the
runtime CODE stream. Before the indirect call at `0x004a3907`, it stores the
next CODE-relative instruction offset into the task at `0x004a38e1`.
The callback arguments, in native stack order, are:

1. The VM field `+0x1454`.
2. The executing task.
3. Command ID.
4. Argument count.
5. Pointer to the marshalled arguments (five bytes per tagged value).

For the cockpit dispatcher, these are available through its original EBP:
`+8`, `+0xc`, `+0x10`, `+0x14`, `+0x18`. In particular, the task is directly
available at the **actual mode-write site**; no global per-opcode hook or guessed
backtrace is needed. The generic native return address is `0x004a3909` for
all these commands, so that address cannot identify transition versus input.

| Runtime structure | Proven field | Diagnostic meaning |
| --- | --- | --- |
| VM `*0x006085e4` | `+8` | Runtime CODE base used for interpreter fetches and relative jumps. |
| Task | `+0x1c` | Next CODE-relative instruction offset, set immediately before native dispatch. The five-byte native opcode begins at offset minus five. |
| Task | `+0x3c` | Current dispatch context, changed by script calls and restored by returns; not a method-table row. |
| Resolved method-table row (distinct from task context) | `+0` | Method entry CODE offset, copied to task `+0x1c` on script entry. |
| Task | `+0x14`, `+0x18`, `+0x10` | Value-stack top/base pointer, signed five-byte-cell index and allocated cell capacity. Bounds must be validated before scanning. |
| Task | `+0x22`, `+0x24` | Native argument count and marshalled argument pointer. |

Capture the instruction offset only after checking arithmetic/range validity
and reading the expected runtime opcode, native group, and command ID. This
also detects a caller that did not enter through the verified native VM path.
Do not identify code by transient argument-buffer addresses or the native
return PC. The on-disk `x3story.obj` is compiled/encoded data; its raw offsets
and operand bytes have not been equated to this runtime CODE representation.

Script return records are also statically identifiable. `0x004a8620` writes a
tag-3 return offset; `0x004a8640` writes the adjacent tag-10 saved context,
including a legitimate null context. The return operation (opcode `0x83`)
scans for tag 10 followed by tag 3 at `0x004a39b3`/`0x004a39b8` and restores
task `+0x3c` at `0x004a3a7b`. It adds the saved return offset to CODE base at
`0x004a3a74`/`0x004a3a77`. The direct-call path pushes these values at
`0x004a3d14`–`0x004a3d35` and sets the new context at `0x004a3d54`.

The distinction matters: script entry `0x0049f330` loads the resolved method's
entry from `[EDI]` and writes task `+0x1c` at `0x0049f3cc`, but writes the
distinct ESI context to task `+0x3c` at `0x0049f3db`. Its first word is not a
proven CODE offset. The source diagnostic therefore reports raw `context`,
`context_word0`, and up to four `context_returns` pairs with
`context_return_count`, rather than the previous misleading method/entry/ancestry
names. Return offsets retain CODE range/read validation; nonnull context
pointers must be aligned and readable. A null saved context remains admissible.
Scanning stays bounded to 64 cells with explicit truncation/invalid-read flags.
These are candidate return records, not a reconstructed source call stack.

This corrects diagnostic interpretation only; it does not restore the camera or
fix a selection stall. The host fixture covers non-CODE context data, null saved
contexts, unreadable contexts, invalid return offsets and the existing bounded
stack/lifetime cases. No allocations, additional hooks or larger scan are added.

## Native input reaches the same VM

Native input processing `0x00410100` invokes the named script method `Input`
through `0x0049f570`; examples include calls `0x004101d2`, `0x004106c5`,
`0x004107a9`, `0x00410869`, `0x004109aa`, and `0x00410b37`.
It invokes `InputAction` through `0x0049f4c0` at `0x00411da6`; another action
source is `0x0040f850`, call `0x0040f9b2`. The native wrappers resolve a method,
marshal integer arguments, and enter a task through `0x0049f430`, which calls
`0x004a26a0` synchronously at `0x0049f4ac`. `Input` and `InputAction` are broad
input routes, not proven camera-only events. Raw key codes or the presence of
any recent input are insufficient to identify the player-view command.

The native camera dispatcher offers no second direct hotkey-only view setter.
Its `INS_SetExternalView` table entry `0x4e` reaches the default no-op result
path in this executable; it is not a discovered safe higher-level restore API.
The existing `x3story.obj` extraction has no usable camera source names.
Static native analysis therefore identifies where to observe precise script
origins, but has not identified the runtime CODE offsets responsible for
sector-reset versus deliberate view requests. Do not install a persistent
mode force or claim a transition-only ticket is proven from these findings.

## Consolidated bounded diagnostic proposal

This is a proposal for the next existing combined user run, not a request for a
new run or authorization to install speculative camera behavior. Combine the
following with the [lead-marker samples](chase-lead-reticle.md); retain one
shared monotonically increasing event number, QPC time, camera-update serial,
thread, and explicit read-validity bits.

- Observe selected camera command requests at a validated dispatcher boundary:
  allocation/free, sector/camera/ref/view-object selection, active-control
  selection, view/target angles, boom, offsets, duration, ChangeView, connect
  mode, mode, tracking and target-body enable. Skip all other command IDs before
  expensive CPU-state capture/reads. Record tagged argument values, command
  origin, and the current known cockpit/ship/camera identities. Label requests
  as requests; do not report a failed lookup as an applied state change.
- Observe successful mode assignments at `0x0042e742` (script) and
  `0x00419e06` (deserialization) separately, recording source, old and requested
  modes, resolved cockpit, native handle and VM provenance where applicable.
  Retain repeated mode writes when their method/return provenance changes.
- Observe actual constructor `0x0041f8d0` and destructor `0x0041ffc0`, covering
  both script and deserialize/deleting-destructor callers. Record successful
  registration and the native handle when available. A reused address must
  receive a new local lifetime generation. Observe actual connect setter
  `0x00422cd0` (ESI cockpit, EAX requested mode), retaining its caller and
  subsequent camera snapshot. Never infer a new lifetime from a missed frame.
- Reuse the existing admitted-camera reader for before/after state on changes
  in active cockpit, native mode/connect, sector, ship, camera, boom or target.
  Add a late-camera sample after final FOV for the lead projection study. Keep
  the pre-load, no-active, first internal and first chase snapshots; this avoids
  thousands of repeated inactive-frame records during loading.
- Lead records should include admission-reason bits, target/main-gun identity,
  native solver success/point and icon state, same-sector owner checks,
  early versus final camera pose/FOV/viewport/plane, native and proposed pixel
  coordinates, and whether final coordinate publication was possible. Bound
  moving-pose examples separately from state changes: a small first set and at
  most one periodic sample per second, never every frame.

Use fixed storage: a bounded first/last transition window and a bounded sparse
lead sample ring, with dropped/truncated counters. Flush through the existing
periodic telemetry path, outside injected callbacks; no callback file I/O,
allocation, per-draw work or unbounded maps. CPU/GPR/flags/x87/SSE/LastError and
four-byte incoming-stack preservation remain mandatory. Refusals must carry a
reason and validity bits, not silently substitute zero identities/coordinates.
A changed FOV/pose alone must not flood the change-only transition ring.

The first result needed is the actual writer source: loaded state or a script
request. For the latter, distinguish reset and manual origin tuples, including
any common helper ancestry. Then establish the native
rear-view setup order and completion boundary. A one-use restore can be armed
only for a previously admitted chase selection, consumed after an identified
transition, and cancelled by a proven player view request, ship/lifetime
replacement or unsupported cinematic state. If origin tuples do not separate
those paths, use the observed input-root task provenance to select a further
static study; do not broaden the restore gate based only on elapsed time.

## Local reproduction and limits

Same installed X3AP.exe identity as the [external-camera study](external-camera.md).
Private output is `/tmp/x3-camera-study/chase-vm-origin{1,2,3,4,5}.txt` and
`chase-input{1,2,3}.txt`, plus `chase-lifetime-origin.txt`,
`chase-load-native.txt`, `chase-cockpit-load.txt`, and
`chase-mode-serialize.txt`, produced with the existing read-only
`X3CameraState.java` workflow. Relevant selections: `ins:004a26a0`,
`dec:004a8620`, `dec:004a8640`, `dec:0049f430`, `dec:0049f4c0`,
`dec:0049f570`, `data:0055537c`, `data:00555e00`, `ins:0042d340`, `dec:00419430`,
`dec:0041f720`, `dec:00425e20`, and constructor/destructor/connect references.
No engine bytes, decompiler output, production changes, game/Wine execution,
build, installation or commit belong to this study.

## 2026-09-14 provenance opcode correction

The original note and diagnostic guard incorrectly added one to a decompiler
switch label, overlooking the second dispatch table. Direct installed-EXE
instruction and table verification establishes the mapping above. The local
stdlib-only reproduction is `/tmp/x3-selection-vm/verify_opcode_dispatch.py`;
it checks the fetch/increment/decrement bytes at `0x004a26d6`, both dispatch
loads at `0x004a2700`, and native call/return bytes at `0x004a3907`. The examined
EXE SHA-256 remains `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`.

The source guard now accepts `0x82`; the host positive witness uses it and the
negative witness rejects VM return `0x83`. Both focused transition tests pass,
and an independent reviewer reproduced the actual PE mapping. Command, native
group/handler, PC and stack bounds remain unchanged. The separate context
correction above removes the false method-entry check. Neither expands the scan
or adds hooks. These repair kind-6 origin/context-return diagnostics only;
it changes neither camera behavior nor target-lock callbacks and is not a
selection-stutter fix. Installation awaits the next combined candidate.

## 2026-09-15 run24 gate reconstruction (snapshot run56)

This supersedes the earlier lack of decoded script names and the assumption
that a gate preserves cockpit/ship pointers. It supplies evidence for amending
[restore item 1](../architecture/chase-view-restore-and-hud-anchor.md#item-1-restore-the-external-view-after-a-gate-jump-or-jumpdrive),
not an implemented or fully qualified restore contract. No jumpdrive save is
required to establish the gate failure below. No game, Wine, build or install
was run for this study.

### Measured writer and ordering

Local log `/tmp/x3-bottleX3-run56/session-20260915-053827-212.log` is
11,671,031 bytes, SHA-256
`089cbc44c1b343c5cdf74b61169f4e0853b0d17c7e4b0e5263e952e156a4b0e9`.
All 43 events are contiguous, on thread 216; 39 windows report zero dropped
records and zero origin read refusals. The kind counts are 2 constructor
entries, 2 completions, 1 destructor, 8 changed updater snapshots, 12 script
mode assignments, 1 deserialize assignment and 17 connect requests. Updater
snapshots are change-suppressed, not a count of rendered updates.

| Event | Established state before intercepted instruction |
| --- | --- |
| 20 | Generation 1, cockpit `3e128308`, mode 258, connect 0, ship `0efae310`, sector `19663cd0`, active handle 3. |
| 22 | Script reapplies 258 at native `42e742`, `next_pc=f0794`; outer recorded return is `fec03`. |
| 23 | Actual destructor `41ffc0`, return caller `42d402` (script free). Old cockpit still mode 258. Registry removal already happened, hence active handle 0. |
| 24–25 | Script constructor caller `42d397`, new cockpit `6fda6b68`, generation 2, initialized mode 0. No deserialize writer between these and the gate reset. |
| 27 | Native `42e742` writes **0 → 1**, `next_pc=f0794`, new native ship `3e067be0`, sector still 0. This is not 258 → 1 in one lifetime. |
| 29–30 | Update serials 7046/7047: first sees sector 0; next sees `5ed0ae30`. Active handle is now 2, mode 1. The sector publication occurs inside the earlier updater, after the existing pose seam. |
| 32 | Reapplies 1 through `f0794` after sector publication; outer recorded return `fdf28`. |
| 35–43 | Manual internal selection then external selection: native stores at `f0c63` followed by `f0794`; event 43 is mode 258 in generation 2. |

QPC frequency is 10,000,000. Destructor→constructor is 5.171201 s;
constructor→initial mode store 2.401 ms; initial mode store→first updater
32.906 ms; first→second updater 125.148 ms; event30→reassertion32 560.460 ms.
These are observed intervals, not a guaranteed settle time. A two-update delay
cannot prove that later reassertions have finished. Initial save loading reaches
kind 7 at event3; the gate recreation does not.

### Script cause, rather than just its shared native writer

The decoded installed archive identity and loader representation are established
in [selection-native-vm.md](selection-native-vm.md#recovering-actual-compiled-method-names).
Static operands are endian-converted and native names resolved in place by
`49e1a0`; `49e4f0` optimizes in place. CODE offsets remain usable, while raw
native operands are not runtime group/command IDs. The following mappings match
the observed valid native mode opcode and recorded return offsets. Whole runtime
method bodies/save-dependent patches were not captured; static branch attribution
beyond those witnesses remains conditional on that binding.

- `f0794` follows `INS_CockpitSetViewMode` at `f078f` in class `25e`
  `StartMonitor` (entry `f00ee`). `f0c63` follows the same command at `f0c5e`
  in `SelectMode` (`f07cc`). This names two different script operations.
- Event27's four candidate return records map to `Show` (`efbbb`), class
  `25d.StartMonitor` (`edb1b`), `OpenLayout` (`e7b2d`), then
  `RestartAllMonitors` (`edc98`). They are bounded candidate return records,
  not an independently reconstructed full call stack; `origin_flags=4` means
  an additional pair exceeded the four retained pairs, not an invalid read.
- **The reset decision is inside `RestartAllMonitors`:** at `edc8c` it calls
  the main monitor's `SelectMode(1)` before `OpenLayout` at `edc93`.
  `SelectMode` assigns script monitor variable 0 at `f0c4b` even when no
  native cockpit exists; the subsequent native store is conditional on its
  handle. Therefore a native `f0c63` event need not exist for this reset.
  `StartMonitor` later creates the native cockpit and applies that script mode.
- Event22 and event32 both descend through `Show → UpdateVisibility →
  SetLeftOffset`, with class `280.Close` (`fec03`) before recreation and
  `280.Open` (`fdf28`) after it. They establish UI-driven reapplication, not
  a dedicated transition-complete signal. `f0794` is also seen on save load
  and manual view selection.
- `RestartAllMonitors` also has static callers in `B3DReload`, `QuickWarp`,
  `RunPlayerTrade`, `__StartInHangar` and `ChangePlayerShipTo`. Its presence
  alone cannot authorize crossing a cockpit lifetime.

### Geometry: the omitted fields matter

The installed snapshot reader records only boom `+130` (always `(0,0,0)` in
these events), not `+160..168`, `+a8..b0`, or view lock `+120`. Thus
`snapshot_valid=255` does **not** prove those fields unchanged.

Static `StartMonitor` sets up newly created mode 1 at `f02d0..f0329`:
`INS_CockpitChangeView(0,0,0)` at `f02f9`, zero view position at `f0316`,
and zero camera offset at `f0324`. The ordinary external branch computes an
object-size-based distance and supplies a negative longitudinal offset at
`f0598`; it also changes angles and view lock. Native dispatcher case `2b`
writes target angles (`42df0b` onward), case `2f` writes boom, and case `36`
writes camera offset (`42e1e9` onward). Constructor writes zero at `41fbb3`
(angles), `41fbe8` (mode), `41fbfd` (camera offset). Static geometry is therefore
not equivalent between fresh internal and rear view. Runtime numeric deltas
remain unmeasured. The custom chase handler's own pose construction does not
make native/script state equivalence established. No geometry writes are
recommended by this study; preserve this as a prerequisite/design decision.

### A discriminating warp boundary and identity sources

Class `96.StartGateWarp` (`160ab`) and `StartSectorWarp` (`16154`) both call
`WarpToSector` (`1635e`), at `16144` and `161f5`. Its ordered path includes
`StopAllMonitors` (`16617`, return `1661c`), player/controller `LeaveSector`,
conditional old-sector deactivate, `SA_CleanUpObjects` (`1666c`),
`SA_FreeAllBodies` (`16673`), destination activation, player/controller
`EnterSector` or `WarpEnterSector`, then `RestartAllMonitors` (`1671f`, return
`16724`). The direct-call graph includes gate traversal `__FlyToNextSector`
(`b7318`) and `JumpToSector` (`bd9ec`, `bdc3a`, plus StartSectorWarp branches).
This supports static shared machinery for jumpdrive; it does not verify a
jumpdrive run, its selected branches, or every warp geometry variant.

The existing destructor entry is a useful **observation candidate**, without a
global interpreter hook. For native caller `42d402` only, inherited EBP still
belongs to `42d340`; task is `[EBP+c]`, command `[EBP+10]=1`, argument block
`[EBP+18]`. Script free at `f0085` has `next_pc=f008a`. Its expected four
return pairs are `efbff` (`Show` after `StopMonitor`), `edba0` (manager
`StopMonitor` after `Show`), `edbe3` (`StopAllMonitors`), `1661c`
(`WarpToSector`). Ship switching instead reaches StopAllMonitors with outer
return `c6eee`. No destructor origin was recorded in run56, so the expected
four-pair witness must be measured before it becomes authorization. Bounded
scanning must refuse missing, ambiguous, invalid or out-of-capacity chains.

| Source | Established native/script layout; limits |
| --- | --- |
| Native ship `+94` | `SA_GetEventObject`, SA command 4, dispatcher `460630` table `4690f0[4] → 4607dc`: lookup by native body ID through `43a4f0`, then `4607f4` reads `[EAX+94]`. This is the script event-object ID, distinct from native ship pointer and native `+8` ID. |
| Global player ship | `VM=*6085e4`; `VM+20` is the static class-descriptor array. Its first descriptor supplies globals through `+c`; five-byte cell **9** is the player ship script value. Cell **8** is the player/controller object. `Show` compares monitor ref-object variable `11` with global9 at `efb62..efb69`. |
| Global cell bounds | Global-read opcode `0e` executes at `4a2816`: `*(VM+20)`, then `+c + 5*index`. Loader `49d030` allocates `descriptor+1c` cells; descriptors are 0x38 bytes, sorted by ID, and `descriptor+8` points to itself. `49f1e0`/`4b06f0` prove bounded static-class lookup using `VM+1c` count and `VM+20` array. |
| Dynamic script identity | Negative ID resolution at `49f1e0` uses `VM+12d0` hash table, key `-id-1`; rows are next/key/context. Context `+0` is its ID, `+4` reference count, `+8` live class descriptor, `+c` variable cells. `4a8640` retains a context only when `+8` is nonzero. Do not retain or increment engine references from a diagnostic. |
| Warp/killed state | Resolve static class `96`; its variable 3 is returned by `IsWarping` at `16203`, and variable 6 by `GetKilled` at `13c82`. `0f`/`16` variable accesses use context `+c + 5*index` (`4a283c`, `4a290d`). Warp state is set to 1 at `160b9`, `16162`, `16362`; it also takes value 2 at `16496`, returns to 1 at `165f4`, and normally clears at `16b40`. This is a phase value, not a unique transition serial. |

Cell tags, scalar payloads, counts, arithmetic, readable spans, class identity
and current registry membership must all be validated before treating these
as identity. Query live roots each time; copy only scalar IDs and numeric
geometry into diagnostic/ticket storage. A pointer or ID alone is not proof
against reuse. This log contains neither global9 nor ship+94, so their equality
and continuity across this gate remain **unmeasured**. Static ordinary ship
replacement writes global9 through class `192.SetPlayerShip` at `e5da1`;
`ChangePlayerShipTo` calls it at `c6f0a`, after stopping monitors and before
restarting them. Death clears/replaces the player ship through the same setter
and marks killed state. Save restoration/newgame may reconstruct or reuse IDs;
no cross-session persistence guarantee was established.

### Proposed constrained ticket and unresolved prerequisites

1. Arm only from an admitted rear-chase update in the active player cockpit.
   Keep the current lifecycle/ship-pointer checks within that lifetime. A
   matching script ID must additionally bind native ship+94 to global9 and
   monitor ref-object variable11; retain numeric mode/geometry only.
2. Transfer authorization across exactly one destruction/recreation **only**
   after a verified warp destructor witness above, live class96 warp phase,
   matching player identity, and a local one-use serial tied to the VM/session
   epoch and executing task identity. Preserve the prior generation solely as
   a scalar check. The now-unregistered cockpit is still alive on destructor
   entry; never require an active handle there, and never dereference it after
   native destruction. An arbitrary destruction cancels the arm.
3. Bind the new cockpit only after successful constructor completion and a
   matching reset writer; require active player ownership, fresh native
   pointers/IDs, matching live script identity, valid destination and supported
   connect/lock state at completion. Event27 precedes activation/sector
   publication, so that store alone is too early. The five-level `16724`
   restart ancestry is beyond the current four recorded pairs; do not silently
   substitute a partial chain. Record full selected bounded proof or carry a
   validated task ticket. Task pointers themselves may be reused.
4. Cancel on script player selection (including `SelectMode` while no cockpit
   exists), foreign or additional lifetime transitions, global9 change,
   killed/leave state, deserialization, VM/session reconstruction, task abort
   or invalidation, unsupported cinematic state, identity/provenance refusal,
   and bounded expiry. State2→1 or elapsed time is not authorization. Native
   mode diagnostics miss a script-only SelectMode, so cancellation needs that
   script state observation or a proved input-method boundary.
5. No complete restore implementation is yet supported by these facts:
   runtime warp-free ancestry and script identity continuity are missing, as
   are a proved VM/task epoch/abort invalidation boundary and the chosen
   strategy for synchronizing script mode with restored native mode. A bare
   `+150=258` leaves script variable0 at 1, and UI visibility can overwrite it
   again. Numeric geometry telemetry (`+a8..b0`, `+130..138`, `+160..168`,
   `+120`) belongs in the same future consolidated diagnostic as these identity
   fields, not a separate requested user run.

Hook boundaries remain whole-instruction: destructor `41ffc0` has a 7-byte
SEH prologue prefix; mode store `42e742` is one 6-byte instruction,
`89 88 50 01 00 00`, EAX=cockpit, ECX=requested mode, EBP=dispatcher frame.
The store leaves flags unchanged, and the continuation performs VM result
marshalling; preserve all incoming registers/flags and CPU/LastError state.
At destructor entry ESP+4 is cockpit, ESP is native return, and dispatcher
EBP is valid only on the verified command caller. Native registry removal at
`42d3f7` precedes this call; actual free is at `42d403`. Constructor/destructor
have SEH paths; an entry observation never implies successful completion.
The interpreter is reentrant (`4a3760` recursively enters `4a26a0`) and scripts
can yield via TI calls. No C++ lock, borrowed object or assumed native stack
span may survive native execution/yield. Same-task pointer equality alone is
not an exception/reentrancy/lifetime contract. Existing bounded callback storage
and no-allocation/no-engine-call policy should be retained; future identity
walks need fixed limits and no per-draw work. The native layouts are backend
independent; native Windows behavior remains unverified.

Local reproduction: `python3 /tmp/x3-run56-chase-analysis/analyze.py` validates
43 events, the named script-call boundaries, PE opcode/table/field bytes and
JSON round trip, and writes `summary.json` / `timeline.json` there. Read-only
`X3CameraState.java` outputs `native-{identity,global,player,sa,body}.txt` and
selected script listings remain local/untracked. The EXE SHA-256 is unchanged
from the earlier study. This documentation-only checkpoint did not require
fixture execution or a production build.

## 2026-09-15 consolidated gate identity diagnostic

Source diagnostic following run56; no restoration ticket or engine-state write.
It retains the existing nine sites, whole-instruction spans, emitted stubs,
CPU/LastError boundary and installation/rollback groups. At destructor entry,
only return caller `42d402` admits inherited EBP to the existing native-opcode
walker, with dispatcher command **1** and matching runtime opcode operands.
Other destructor callers perform no dispatcher-frame provenance read. Mode
assignments still require command `30`. The 64-cell stack bound is unchanged;
six candidate return/context pairs are now retained, and overflow/refusal flags
remain explicit. Existing `context_returns` carries the first four; the new
same-event detail row carries pairs five and six. No partial chain authorizes
anything.

### Read contracts and observable report

`chase_transition_detail event=N` supplements the corresponding existing event:

- `geometry_valid`: angles `+a8..b0` = 1, offset `+160..168` = 2,
  lock `+120` = 4. Boom `+130..138` remains in the original row, under
  `snapshot_valid & 128`. These are raw signed 32-bit geometry words and a
  raw unsigned lock word, not converted angles/distances. Partial/unknown
  cockpit lifetimes have no geometry reads; destructor entry samples while
  the tracked cockpit is still live, irrespective of its removed registry entry.
- `identity_valid` / `identity_refused` use bits VM root 1, native ship/script
  scalar 2, global9 player 4, global8 controller 8, monitor ID 16, monitor
  mode variable0 32, monitor ref-object variable11 64, class96 warp variable3
  128, class96 killed variable6 256, player script-context membership 512,
  controller membership 1024, native ship's script-context membership 2048.
  Neither bit means unavailable/unattempted; refused means an attempted
  read/contract failed. Valid scalar zero is separately observable and never
  counts as a live player context. These bits establish bounded observations,
  **not player ownership, identity continuity, an epoch or an atomic snapshot**.
- `cell_tags` order is player, controller, mode, ref-object, warp, killed.
  Only tag 1 is interpreted as an integer; unexpected tags/payloads remain
  diagnostic data with their validity bit clear. Integer production is
  established by `4a8600` (`4a8613` stores tag 1) and literal handlers such
  as `4a277e`; tag 3 return-PC cells and tag 10 contexts are not integers.
- `script_classes` gives resolved class IDs for native script, player,
  controller and monitor. A resolved foreign monitor class is logged but its
  monitor/mode/ref validity is refused; only exact class `25e` admits those
  variables. Monitor context comes fresh from a verified native task origin,
  never from a retained pointer or a global interpreter hook.

The reader follows the layouts established above and rechecks roots each event.
Static descriptors are contiguous 0x38-byte rows, with count at VM+1c and base
at VM+20; selected rows must be inside that array, aligned to a row, self-point
at +8, have nonnegative class IDs and bounded variable counts at +1c. The
loader's temporary globals descriptor is ID 0 and becomes the sorted first
row (`49d030`); global reads require that exact first-row identity. Static
class96 resolution mirrors the signed binary search at `4b06f0`.

Dynamic context IDs resolve through VM+12d0 with unsigned key `~id`, matching
`49f1e0`. The returned context must contain that ID at +0 and a live registered
class descriptor at +8. Before ship+94 is read, ship+8 must resolve to the exact
ship through `*60850c` +14's native-body map (`43a4f0`, `4607dc..4607f4`).
Both map readers check power-of-two bucket counts, readable rows and pointer
arithmetic, scan at most 32 links, and refuse duplicate matching keys or any
unfinished chain. Caps are conservative diagnostic limits: 4,096 classes
(13 binary-search steps), 65,536 cells/class and 65,536 hash buckets. Every
selected five-byte cell is checked against its owning class's variable count
and a checked readable span. No engine call, refcount change, allocation or
pointer retained for later dereference is introduced.

Geometry and identity supplements run **after existing event suppression**.
Animated geometry does not create events and no new registry walk runs per
unchanged updater or per draw. The existing first16/last32 window bounds the
extra detail rows to 48 per report; its dropped counter still describes events.
Consequently this records continuity only at admitted events: it cannot detect
all script-only selections while no cockpit exists or define cancellation.
The expected gate report is a kind2 `f008a` free with return prefix
`efbff,edba0,edbe3,1661c`, followed by fresh native pointers but equal valid
native/global9/monitor-ref script IDs, then reset/restart geometry and mode.
The fifth restart pair can now expose `16724`. These are expected observations,
not predeclared successes; refusal, truncation or mismatches remain evidence.

### Scoped source evidence and remaining work

Host checks: 128 lifetime/provenance/timing checks, 29 identity checks,
8 checks on actual extracted record/suppression code, and the existing
29 callback-invalidation checks. The ordinary synthetic identity snapshot uses
51 checked reads; 100 unchanged updates add **zero** supplement reads.
Commands: `PYTHONPATH=verification/probe python3 -m unittest
verification.analysis.test_chase_transition verification.analysis.test_chase_transition_sites`
(10 tests); `python3 verification/probe/verify_chase_transition_sites.py --exe
"$HOME/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe"`
(nine real sites PASS); `python3 verification/probe/build_chase_transition_cpu.py`
(MinGW compile and CPU audit PASS). The audit keeps GetLastError first and
SetLastError last, with FNSAVE/FRSTOR, MXCSR and emitted XMM/GPR/flags preservation
unchanged. The CPU fixture adds `--cpu-only` to avoid its unrelated stub benchmark.
No DLL build, Wine execution, install or game launch occurred in this subtask;
CPU fixture runtime and native Windows behavior remain unverified here.

Owner-only CPU follow-up from the integrated checkout (after its build):

```sh
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py \
  '/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine' \
  --bottle X3 --no-update --workdir "$PWD/build/verification/chase-transition" \
  "$PWD/build/verification/chase-transition/chase_transition_cpu_fixture.exe" --cpu-only
```

Record bottle name, WineArch, FEX_X87REDUCEDPRECISION and WINEMSYNC with that
result. Runtime warp/identity/geometry evidence, VM/session/task abort boundaries,
script-only selection cancellation and script/native mode synchronization remain
prerequisites to restoration. No task-pointer-as-epoch assumption was introduced.

### Independent review and owner CPU execution

Independent source/evidence review passed with no blockers. Main ran the
CPU-only command above under the single X3 Wine lock: 18 stubs, 334 checks,
zero failures, exit 0, 2.057 s with negligible lock wait. No benchmark or game
ran. The [compact record](../../verification/results/chase-gate-identity.json)
binds actual source, fixture, build audit, report, bottle/emulation and timings.
This verifies synthetic callback preservation, not live field continuity.
The consolidated candidate still needs one gate reproduction to supply those
observations; jumpdrive remains unverified without a separate request now.

## 2026-09-15 run25 identity and restoration boundary (snapshot run60)

**Ratified 2026-09-15 (orchestrator):** the contract below (script-assignment
mutation at `4a3ffd`, seven spans, arm/pending/consume state machine and the
cancellation set) is the implementation contract for `--chase-view-restore`.
`/tmp/x3-run60-gate/verify.py` passes against the current EXE and log.
Jumpdrive stays gameplay-unverified; no further telemetry-only run is required.

The 12,909,476-byte log in `/tmp/x3-bottleX3-run60/`,
`session-20260915-070402-216.log`, has SHA-256
`6e899e03a2797f0338a395c6b1d7e1206410799474c5982e021d7a0ddc6647a2`.
All 69 events have matching detail rows and thread 220. The captured gate now
proves the formerly missing destructor/restart ancestry. Loading attribution
belongs to its owning note and is not repeated here.

### What the validity masks actually establish

`identity_valid=399` means VM root, native body/script scalar, global player,
global controller, warp and killed values succeeded. It does **not** validate
a dynamic context. `identity_refused=3696` means monitor/mode/ref and all three
dynamic membership checks failed. Without monitor provenance the refusal is
3584 (the three membership checks); constructor rows use valid397/refused1536
because no native body was sampled. Across the log there are 21/41/7 rows of
these respective refusal patterns. Consequently **`script_mode=0` is unread
placeholder data**, as are monitor-ref zero and the zero class IDs. The raw
monitor ID is read before its later membership check fails. Do not infer a
new script-mode behavior from those zeroes.

The reader's dynamic lookup matches native `49f1e0` structurally. The static
class and integer-cell checks also agree with the executable. The source,
however, imposes a 65,536-bucket cap not present in native hash growth:
`4efbf0` grows when occupied count reaches bucket count, and `4efeb0` reallocates
and rehashes the selected bucket array. The player ID's key is `10587`
(66,951), but allocated ID is not live count, so this does **not** prove that
run60 exceeded the cap. The trace omits the failed subcheck and raw bucket count;
the precise dynamic-refusal cause cannot be reconstructed from it. Raising a
cap and claiming a measured repair would be unjustified. If the general reader
is repaired, a bounded 32-link/single-bucket reader can validate arithmetic and
power-of-two size without a small population cap; no scan scales with bucket
count. Preserve duplicate/cycle/read/descriptor checks and report subreasons.

A restoration callback need not use a fresh global hash search to establish the
lifetime of its **current executing monitor context**. The native interpreter
already supplies that borrowed context and is about to access its cells. At a
proved interpreter seam, validate the current context ID, nonnull live class
pointer, registered static descriptor identity `25e`, variable bounds and cell
tags directly, together with task/context coherence. `4a8640` retains call
contexts, `4a8240` frees a retained context only when reference count and live
class are both zero, and the script member-store handler itself obtains cells
from that current context. This is a scoped executing-context contract, not a
claim that the failed registry checks passed. It is unsuitable for an arbitrary
retained pointer or an asynchronously sampled inactive context. Player ownership
can be bound independently by the **validated native body registry** plus
native `+94 == global9`, both of which succeeded in this run, the exact warp
sequence, and the cancellation epochs below. No player-context dereference is
needed for this minimal restore.

### Received gate witness and geometry

- Event25: destructor `41ffc0`, caller `42d402`, CODE `f008a`; candidate return
  prefix is exactly `efbff,edba0,edbe3,1661c`, followed by the terminal zero
  return; `origin_flags=0`. Old cockpit `3f996ed8`, generation1, mode258,
  native ID `8db`, player/native-script ID `fffefa78`, controller `ffff6eaf`,
  raw monitor ID `ffff6eae`, VM `03b609c8`, task `705aa128`.
- Event26/27: constructor/completion of `72206bd0`, generation2; no native
  identity exists yet. Event29's native body ID is now `415`, while the valid
  native-script/global-player/controller scalars remain unchanged.
- Event29: initial mode0→1 writer `f0794`, same task `705aa128` and current
  monitor context `199dee90`; complete return prefix
  `efbbb,edb1b,e7b2d,edc98,16724`, then terminal zero, flags0. Activation and
  destination publication follow (events30–32); UI reapplication34 happens
  with warp0 and a different task, as in run56.
- Geometry validity is 7. Old offset `(0,0,-17404)` and view lock1 become
  `(0,0,0)` / lock0 on recreation and reset; angles and boom remain zero.
  Manual rear selection restores the old offset/lock values. These are valid
  measured fields, not values inferred from the refused script-mode reader.

Thus the exact gate path and native/global identity continuity no longer need
another diagnostic run. The old same-generation policy still cannot implement
this gate, and copying native mode alone still loses script/native consistency.

### Recommended mutation: the existing script assignment, before allocation

Allow `RestartAllMonitors` to execute once, but replace the existing integer
**source value of its `SelectMode(1)` assignment** with 258. The narrow point is
CODE `f0c4b`, immediately before the interpreter writes monitor variable0.
The argument was copied to the expression stack at `f0c48`; altering that live
copy does not fabricate a task, invoke an extra script method, or retain an
engine reference. Require monitor native-handle variable1 == 0. In this case
`SelectMode` branches around its entire native geometry block at `f0849` and
reaches `f0c48`; native allocation happens subsequently in `StartMonitor`.
The engine then reads persistent mode258 and constructs its own fresh rear
geometry. This also fixes later UI reapplications naturally. Do not use this
mutation with a nonzero native cockpit handle: geometry may already have been
set up for the original argument on that path.

**Optimization matters:** on-disk `16 <index16> 24` (member copy then discard)
is rewritten in place by `49e4f0` to `94 <index16> 24`. Global `15/24` becomes
`93/24`. Native dispatch tables map 94→`4a4027` and 93→`4a3ff0`; both converge
at **`4a3ffd`**, before destination-cell calculation. Hooking only `4a290d`
(the unoptimized 16 handler) would miss the installed optimized script.

At `4a3ffd`: EAX is destination context (current object for 94, first/global
class descriptor for 93), ESI is five times the cell index, EBX is the live
source tagged cell, EDI is CODE operand start, native ESP+18 is VM,
ESP+20 is current context, and interpreter `[EBP+8]` is task. Validate actual
runtime optimized opcode, index and discard byte at the expected PC; unrecognized
code refuses. The six displaced bytes are `03 70 0c 80 3e 08`:
`ADD ESI,[EAX+c]; CMP byte[ESI],8`. Their flags are live in the following
conditional cleanup. Restore GPR/flags before replay. Original code then moves
the source tag/payload to the destination, clears the source tag, advances EBX
by5 and EDI by3, and dispatches normally. Changing only `[EBX+1]` from1 to258
preserves original assignment/native execution counts. Require source and
current destination tags1, source payload1, current monitor mode258, and a
checked writable four-byte source span before this single mutation.

The live interpreter stack must be bounded using **EBX**, task stack top+14 and
capacity+10, with five-byte alignment and overflow checks. Task+18 and +1c are
published at native calls/yields and can be stale at this internal opcode seam;
do not reuse the native-dispatch provenance walker unchanged. The required
current return prefix is `edc91` (SelectMode's caller) then `16724`
(RestartAllMonitors' caller), with validated contexts and terminal root. Reuse
fixed 64-cell bounds; ambiguous/missing/truncated proof cancels.

### Minimal state machine and cancellation

- Arm from an actually admitted rear chase update (258, connect0, controlled
  reference/view == player, complete active cockpit), with native body/script
  and global-player/controller identity checked. Store only scalar identities,
  old lifetime token and local epoch/serial, not transferable native pointers.
- Transfer to pending only at the now-measured warp destructor prefix above,
  matching the arm's cockpit lifetime and player identity, live warp1 and
  killed0. Capture current monitor ID, current task pointer **as opaque key**,
  task ID+8, thread and local epoch. An arbitrary destructor clears the arm;
  a second destruction clears pending. No registry-active-handle requirement
  applies after native registry removal at `42d3f7`.
- Consume at the assignment seam only for the same task/thread/epoch, exact
  reset return prefix, same fresh current monitor identity, unchanged valid
  global player/controller IDs, warp1/killed0, monitor-ref variable11 == player,
  native-handle variable1 == 0, prior persistent mode258 and requested1.
  Clear pending immediately before the one source-payload write. Failure of
  any proof clears it without writing. The subsequent constructor belongs to
  ordinary engine execution; it must not be used as a guessed completion timer.
- The same shared store seam cancels on **any other SelectMode assignment for
  the armed/pending main monitor**, including repeated same-valued selections
  while no native cockpit exists. This covers both dynamic and direct calls:
  the static asset has six direct SelectMode callers in OpenMonitor, SetTracking,
  TrackPrevNext, NotifyClick and SetZoomAbsolute. A dynamic-call-only hook would
  miss them. A selection before pending invalidates the arm.
- Cancel at global-player setter CODE `e5da1` (cell9), controller setup `83a03`
  (cell8), and nonzero killed stores `13b48`/`13b62` (class96 cell6); all are
  optimized stores reaching the same seam. This catches A→B→A changes rather
  than merely comparing the final identity. Current object/class/slot/runtime
  code must match; malformed proof cancels conservatively. Additional unknown
  global8/9 writes must not preserve pending; treating any store to those
  global slots as cancellation is conservative and cheap while pending.
- Normal task completion and task abort invalidate before their callbacks/free;
  VM construction, content clear and save **load** advance a local epoch and
  clear arm/pending before native work. Deserialization kind7 remains an
  additional cancel. Thus same-address VM/task/ID reuse is insufficient.
  Keep a bounded update/time expiry only as cancellation, never authorization.

### Concrete lifecycle and exception boundaries

| Native entry | Whole-instruction span | Contract |
| --- | --- | --- |
| `4a3ffd` | 6: `03 70 0c 80 3e 08` | Shared optimized member/global move; ABI above. |
| `4a2260` | 5: `53 8b 5c 24 0c` | Normal task result/completion; original ESP+4=VM, +8=task. Invalidate before optional result callback and free. |
| `4a2420` | 5: `53 8b 5c 24 0c` | Abort task; same arguments. Called by interpreter error and context-task cancellation `4a2520`. |
| `49c9a0` | 6: `53 33 db 89 5e 04` | VM constructor, ESI=VM. Cancel/advance epoch before fields and map allocation. |
| `49ea80` | 8: `83 ec 08 55 8b 6c 24 10` | VM content clear, original ESP+4=VM. It can refuse while tasks are active; cancellation even on refusal is conservative. Clears tasks, contexts and classes without relying exclusively on task-free wrappers. |
| `4a0880` | 7: `6a ff 68 c8 00 53 00` | Actual VM save-load reader; VM comes from `*6085e4`. Called at `40507b` inside `404cc0`. `49f930`, called by `404530`, is the save writer and is **not** this load boundary. |
| `52f298` | 5: `b8 f4 e5 56 00` | Interpreter-specific C++ EH adapter, then tail-jump to CRT handler `51305e`. Any invocation can conservatively bump an atomic cancellation epoch before original handling. No exception-layout decoding or lock acquisition is necessary. |

Task allocation `4a21e0` inserts the new task in VM's task map via `4a220a`
and stores returned ID at `4a220f` (task+8). ID allocator `4efcc0` wraps at
`7ffffffe` and skips live keys; it does not promise never-reused IDs. The
lifecycle boundaries, live task-map lookup and local epoch therefore matter.
The EH adapter provides cancellation for native unwinding that bypasses ordinary
script completion; it must use a minimal **lock-free** signal so an exception
inside another observer cannot deadlock on that observer's lock. It replays the
original MOV/tail-call path, preserving CPU/LastError and exception arguments.
No observer lock or borrowed context survives native execution or a script yield.

These seven new spans are ordinary instruction copies; the shared write site
has no displaced branch and both optimized incoming paths reach its start.
The original nine transition boundaries remain useful. With the new option off,
do not claim/patch the new sites or alter interpreter operands; launcher/DLL
default stays off. On the hot shared store path use a tiny prefilter for relevant
runtime operand addresses/armed state before full CPU capture or checked reads;
there is no per-draw work. Full ownership/provenance checks occur only on selected
view/identity transitions. Implementation acceptance needs actual-emitter CPU,
flag/LastError, rollback and native-byte tests; same-valued/direct script selection,
A→B→A identity changes, yield/reentry, both task terminations, VM/load reuse,
EH cancellation, bad tags/stack bounds and exactly-once stack movement belong in
the focused fixture. These are implementation checks, not a request for another
telemetry-only user run. Gameplay/native-Windows acceptance remains distinct.

Local evidence: `/tmp/x3-run60-gate/proof.json` verifies the seven exact PE spans,
optimized dispatch destinations, 69 event/detail pairs, gate ancestry, identity
scalar agreement and geometry change. Raw targeted output is confined to that
local directory. The current log cannot retrospectively reveal failed dynamic
lookup subchecks; the borrowed current-context contract above avoids inventing
those readings. No production source, Wine/game execution, build or install was
performed in this reconstruction.

### Implementation (2026-09-15, `--chase-view-restore`, default off)

Source: `src/proxy/chase_transition_restore_core.h` (portable state machine,
seam decode, consume/transfer proofs, bounded live-stack walk),
`src/proxy/chase_transition.cpp` (the seven `restore_specs` sites, prefilter
stub, handlers, install/rollback, report), launcher `--chase-view-restore`
(`X3M_CHASE_VIEW_RESTORE=1`). Verification is in
`docs/verification/chase-cpu-boundary.md`.

Site table state: all seven spans of the lifecycle table are claimed as plain
whole-instruction copies only when the option is on and the base transition
set installed; a refused or failed site rolls the earlier ones back, and the
late window refuses. `verify_chase_restore_sites.py` also proves no direct
branch in `.text` targets a span interior and the `4a4027 → jmp 4a3ffd` /
`4a3ff0` fall-through convergence. The nine diagnostic sites are unchanged.
Kinds: 0 store `4a3ffd`, 1 complete `4a2260`, 2 abort `4a2420`, 3 VM
construct `49c9a0`, 4 VM clear `49ea80`, 5 VM load `4a0880`, 6 EH adapter
`52f298` (lock-free atomic epoch bump only, then the original MOV/tail path).

Mechanics as ratified: arm from an admitted rear-chase update (mode 258,
connect 0, view == ref ship, active registry handle, native body registry
membership, native `+94 == global9`, valid controller/warp/killed scalars;
dynamic-membership bits are not required, as measured); transfer to pending
only at the `42d402` destructor with the exact `efbff,edba0,edbe3,1661c,0`
prefix, matching arm cockpit/generation, player/controller scalars, warp 1,
killed 0 and a borrowed current context of class `25e`; consume at `f0c4b`
only for the same thread/task pointer/task ID/epoch, `[ESP+20] == EAX ==
task+3c`, opcode `94` index 0 discard `24`, same monitor ID, cells 0/1/11 =
258/0/player, unchanged globals, warp 1/killed 0, source tag 1 payload 1,
live stack prefix `edc91,16724,0` walked from EBX (64-cell bound, five-byte
alignment, capacity), then a VirtualQuery-writable check; pending is cleared
before the single 4-byte write of 258 into `[EBX+1]`. The displaced
`ADD ESI,[EAX+c]; CMP byte [ESI],8` replay after POPAD/POPFD, so their flags
are fresh for the following `jb`.

Prefilter (before any XMM/x87 capture, under saved flags): mode word 0 skips;
mode 1 (armed) admits only the five published operand addresses
(`code + pc + 1` for `f0c4b`, `e5da1`, `83a03`, `13b48`, `13b62`); mode 2
(pending) additionally admits `[EDI-1] == 93` with ESI 40/45 (global slots
8/9). Idle cost measured at ~0.01 µs per store versus ~0.54 µs for a full stub.

Deviations and assumptions, with reasons:

- PC convention: the contract's CODE offsets are taken as opcode-byte
  addresses, so EDI at the seam (operand start) equals `code + pc + 1`. If the
  convention were off by one, the published operand addresses never match
  EDI, the prefilter never admits a store, nothing is written, and the
  `chase_view_restore_state` line shows `arms` advancing with `seam_calls==0`.
- Foreign monitors: while armed, a SelectMode assignment whose current
  context validates as class `25e` with variable11 != player is ignored;
  while pending, admission is by monitor ID only (the identity captured at
  the destructor prefix), so a secondary monitor viewing the player cannot
  reach the consume proof. `RestartAllMonitors` visiting other monitors first
  therefore cannot defeat the ticket; any malformed context cancels.
- Deserialization kind7: the existing mode-load observer (`419e06`, kind 7,
  present only with telemetry diagnostics) clears arm/pending and advances the
  epoch like the VM boundaries; the save-load reader `4a0880` is the site that
  is always claimed with the option. Same-valued and direct selections of the main monitor
  cancel (armed) or fail the consume proof (pending) as ratified.
- Bounded expiry: pending is cancelled after 600 cockpit updates (about ten
  seconds at 60 fps) as a cancellation-only bound; the run60 gate needed no
  update between destructor and reset.
- Arm re-attempts: one identity walk per cockpit lifetime and per re-entry
  into mode 258 (a seam cancellation resets the attempt), never per frame.
- Task termination with an unreadable task argument clears pending
  conservatively; killed stores at the two PCs cancel unless they are a
  well-formed zero store (`94`, index 6, ESI 30, current object == class-96
  context, tag 1 payload 0).
- `IdentityReader` and the provenance walkers now take the VM/registry roots
  as parameters (defaults unchanged) so the fixture can alias them; production
  values are the same globals as before.

Gameplay acceptance (a gate run with the option on, reading the
`chase_view_restore_state` counters) and native Windows execution remain
unverified.

### Run65: consume refusal on the ref cell

Run 26 (`/tmp/x3-bottleX3-run65/session-*.log`) armed three times, transferred
twice at the measured warp destructor prefix and reached the store seam twice
(`arms=3 transfers=2 seam_calls=2 consumed=0 writes_failed=0`), refusing both
times with `last_refusal=10` (`refuse_ref_cell`) and `cancels[10]=2`
(`cancel_proof`). Because `seam_consume_proof` is ordered, every earlier check
passed on the real gate path: epoch/thread/task/task-ID, `[ESP+20] == EAX ==
task+3c`, opcode `94` index 0 ESI 0 at PC `f0c4b`, borrowed context of class
`25e`, monitor ID equal to the one captured at the destructor, **cell0 == 258**,
**cell1 == 0**, unchanged global player/controller, warp1/killed0. Only
`cell11 == player` failed. The source and live-stack checks sit after cell11 and
stay unmeasured; both are now established statically below.

Static decode uses the same asset and loader contract as
[selection-native-vm.md](selection-native-vm.md#recovering-actual-compiled-method-names):
`addon/04.cat:L/x3story.obj`, decoded SHA-256
`ed5786a0c603802d5735fb36e0332d7c732b981d2b64dcee4174890284faff7a`, 280 CLAS
classes, 1,812,069 CODE bytes, 669,066 instructions with `49e1a0` widths, all
6,818 method entries aligned. Opcodes used below: `01/02/03/04` push 0/1/2/3,
`05/06/07` push byte/word/dword, `0d/0e/0f` load local/global/member, `10`
array index, `14/15/16` store local/global/member, `24` discard, `2b`
instantiate class, `32/33/34` jmp/jt/jf, `82` native command, `83` return,
`85` call by name, `86` call on class, `88` call on self, `6e` frame entry.
The `0d`/`14` slot index is relative to the current expression top, so the last
pushed argument is index 4 at depth 0; at depth 1 the single argument of a
one-argument method is index 5. Class `25e` has 41 variables, so cell indices
in the bytecode are the runtime cell indices the optimized `94` store uses.

**Monitor creation through `SelectMode(1)`, with offsets.**

| CODE | Bytes / operation | Effect |
| --- | --- | --- |
| `e7681` | `0x25d::Init` entry, `SE_ArrayAlloc` at `e768b` | Allocates the monitor array into class `25d` member2. |
| `e7751`–`e7794` | `LOADL i`; `PUSHB 20` (`i==0`) or `PUSHB 60`; `SE_ReadText`/`SE_SPrintf` name | Builds the three constructor arguments for monitor `i`. |
| `e7795` | `04 06 025e 2b 85 00000081` = `PUSH 3`(count), `PUSHW 606`, instantiate, `DYNCALL "Create"` | The only instantiation of class `25e` in the asset; loop runs `i=0..2` at game start. |
| `e779f` | `LOADL i`; `LOADM 2`; store element | Publishes the new monitor as `monitors[i]`. |
| `ef863` | `0x25e::Create` entry (3 args) | `STOREM 0 = 0` (`ef867`), `STOREM 1 = 0` (`ef86c`). |
| `ef8a0` | `0d 0005 16 000b` = `LOADL 5; STOREM 11` | **cell11 = the middle constructor argument = 20 for the main monitor, 60 for the two side monitors.** |
| `ef8b6`/`ef8bd` | `LOADL 4; STOREM 15` / `LOADL 6; STOREM 16` | cell15 = the name string (read by `GetName` at `efc5d`), cell16 = the monitor number (`IsActive` at `efc6f` compares it with `GetActiveMonitorNum`). |
| `ef8c4` | `01 16 0011` = `PUSH 0; STOREM 17` | cell17, the ref-object cell, starts unset. |
| `edb2b`–`edb34` | `LOADG 9; PUSH 1; PUSH 0; LOADM 2; INDEX; DYNCALL "SetRefObject"` in `0x25d::StartMainMonitor` | **cell17 of `monitors[0]` is a direct copy of global cell9, the player ship.** `0x25e::Show` compares `LOADM 17` with `LOADG 9` at `efb62`. |
| `effad`/`effee` | `0x25e::SetRefObject`: `LOADL 4; STOREM 17` with Remove/AddEventListener | The only other store to cell17 in the asset. |
| `16617`/`1661c` | `0x96::WarpToSector` calls `0x25d::StopAllMonitors` | Warp teardown; `edbde`/`edbe3` then `edb9b`/`edba0` reach `0x25e::Show`. |
| `efbfa`/`efbff` | `Show` calls `0x25e::StopMonitor` | `Show` clears cell17 only when `SE_ObjectExists` fails (`efaee`, `efb0f`). |
| `f0085`/`f008a` | `INS_CockpitFree`, then `f008b` `PUSH 0; STOREM 1` | Frees the native cockpit and zeroes cell1. cell0 is not touched. |
| `1661d`–`1671a` | `LeaveSector`, `SA_CleanUpObjects`, `SA_FreeAllBodies`, `EnterSector`, `SetPos` on `LOADG 9`, `WarpEnterSector` | No write to global8/9 and no monitor cell write; the player ship survives the warp. |
| `edc85`–`edc8c` | `02 02 01 0f 0002 10 85 000053ef` = `PUSH 1`(source), `PUSH 1`(argument count), `PUSH 0`, `LOADM 2`, `INDEX`, `DYNCALL "SelectMode"` | **`monitors[0].SelectMode(1)`; the requested value is the literal `PUSH 1`, tag 1 payload 1.** Returns to `edc91`, then `OpenLayout` at `edc93`/`edc98`. |
| `f07cc` | `0x25e::SelectMode` entry, one argument | `arg==8` prologue skipped; at `f0832` `LOADM 1` is 0, so `f0849 JF f0c48` branches around the whole native geometry block. |
| `f0c48` | `0d 0005` = `LOADL 5` | Copies the argument cell (tag 1, payload 1) to the expression top, the cell EBX addresses at the seam. |
| `f0c4b` | `16 0000 24` (`94 0000 24` after `49e4f0`) | The hooked store `cell0 = requested`, then discard. |

Cell0 is written only at `ef867` (Create), `ef944` (Destruct), `f0c4b`
(SelectMode) and `f41dd`/`f42f7` (`SwitchViewCameraTypeTo`); none of them is on
the warp path, so **258 genuinely persists to the seam**, as measured. Cell1 is
written only at `ef86c`, `f008c` (the zero above) and `f01ca` (`StartMonitor`).

**Answers.** cell11 is not a ref cell: it is a camera priority constant, read
only by `StartMonitor` at `f0215` as `B3D_CameraSetPri(cell11, camera)` and by
`ShowDust`/`ShowSpace`/`SetAllViewPorts`. It is assigned exactly once, inside
the constructor during `0x25d::Init` at game start, and never again anywhere in
the asset, so at `f0c4b` it holds **20** on the main monitor. The script Monitor
object is not recreated by a gate (only the native cockpit is), which is why the
monitor identity captured at the destructor already matched. cell0 is 258 and
the requested source is the literal 1, both as assumed.

**Corrected consume predicate.** Keep every check that already passed and
replace `integer_cell(...,11,ref) && ref==player` with:

- `integer_cell(...,16,number) && number==0` — the main monitor, a compile-time
  constant from `Create`'s first argument; cheap and statically provable.
- `cell(...,17,tag,ref) && ref==player` — the ref object, which
  `StartMainMonitor` copies verbatim from the same global cell9 the proof
  already validates. Compare the 4-byte payload and accept the tag that global
  cell9 carries (tag 1 as measured in run65) or a heap tag, recording the
  observed tag in a distinct refusal subreason rather than folding it into
  `refuse_ref_cell`.

Deferring the ref check to `SetTracking` is not viable: its `SelectMode` call at
`f1b5e` runs only when cell1 is nonzero and cell0 == 4, so it is off the gate
path entirely. `OpenLayout`'s `SelectMode(0)` at `e7b9f` applies only to
monitors at or beyond the layout count, so the main monitor keeps the written
258 until `0x25d::StartMonitor` → `Show` → `StartMonitor` → `INS_CockpitSetViewMode`
at `f078f`; the run25 event29 prefix `efbbb,edb1b,e7b2d,edc98,16724` is exactly
that chain and corroborates the decode.

**Risks.** The cell17 tag is inferred, not measured: if the cell were rejected
for its tag the ticket would refuse again, so the tag must be reported, not
assumed. `Show` legitimately clears cell17 when the referenced object stops
existing, which would refuse safely rather than write. This is the on-disk
static asset; save-dependent patched bodies were not captured, and the
optimizer only rewrites these same opcodes in place, leaving index and discard
byte unchanged. Prose elsewhere calling cell11 the "monitor-ref cell" is wrong
and should be read as cell17. Reproduction scripts (local, untracked):
`/tmp/x3-run65-script/kc.py`, `d2.py`, over `/tmp/x3-camera-study/x3story.obj`.
No game, Wine, build or install was run for this study.
