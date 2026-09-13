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
| Task | `+0x3c` | Current script method record, changed by script calls and restored by returns. |
| Method record | `+0` | Method entry CODE offset, added to VM CODE base on script entry. |
| Task | `+0x14`, `+0x18`, `+0x10` | Value-stack top/base pointer, signed five-byte-cell index and allocated cell capacity. Bounds must be validated before scanning. |
| Task | `+0x22`, `+0x24` | Native argument count and marshalled argument pointer. |

Capture the instruction offset only after checking arithmetic/range validity
and reading the expected runtime opcode, native group, and command ID. This
also detects a caller that did not enter through the verified native VM path.
Do not identify code by transient argument-buffer addresses or the native
return PC. The on-disk `x3story.obj` is compiled/encoded data; its raw offsets
and operand bytes have not been equated to this runtime CODE representation.

Script return records are also statically identifiable. `0x004a8620` writes a
tag-3 return offset; `0x004a8640` writes the adjacent tag-10 method reference.
The return operation (opcode `0x83`) itself scans for tag 10 followed by tag 3
when unwinding an invalid method. A bounded diagnostic may retain up to four
such pairs from at most 64 validated cells, recording method-entry and return
CODE offsets, with explicit truncation/invalid-read flags. This is useful if
both view selections call a shared camera helper; a current command PC alone
then need not identify its initiating caller. These are observed candidate
return records, not permission to interpret arbitrary tagged stack values as
fully reconstructed source call stacks.

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
group/handler, method, memory bounds and ancestry gates remain unchanged. There
is no added runtime work. This repairs kind-6 diagnostic origin/ancestry only;
it changes neither camera behavior nor target-lock callbacks and is not a
selection-stutter fix. Installation awaits the next combined candidate.
