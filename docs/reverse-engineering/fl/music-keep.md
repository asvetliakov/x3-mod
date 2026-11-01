# FL music retention, save pump and queue contract

Offline, 2026-10-29. **Outcome:** playback retention through focus loss,
pause and save has the same policy inputs and media ownership as AP. The
native ABI differs enough to require FL entry/call shims. AP's thunk bytes,
stack indices and delete helper cannot be reused by changing addresses.
The existing four holds, `SavePump`, track queue and load-settling policy
remain the appropriate shared policy. No reachable difference established
here requires a new owner, load observer or state framework.

This note completes the bounded playback-retention/save mapping and identifies
exact remaining queue qualification below. It does **not** qualify installed
hooks or audible FL behavior. No production source, game, Wine/native fixture,
build, install or commit was touched. The combat-alert adapter is outside scope.

Inputs, measured SHA-256:

| Input | SHA-256 |
| --- | --- |
| `/mnt/c/X3AP-orig/X3AP.exe` | `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` |
| `/mnt/c/X3FL-orig/X3FL.exe` | `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8` |
| FL `addon2/03.cat`, member `addon2/L/x3story.obj` | `dbb732716e1acab6b8cc7f87f5d31324fb4764dfc22d1cec808749a4f45ec7b9` |

Addresses are preferred VAs at `0x400000`; FL absolute operands need the
loaded module delta and HIGHLOW validation. Hashes identify evidence, not a
production renderer prerequisite. Reproduce with:

```sh
python3 verification/results/fl-port/music-keep-contract/reproduce.py
```

The [producer](../../../verification/results/fl-port/music-keep-contract/reproduce.py)
and [compact record](../../../verification/results/fl-port/music-keep-contract/summary.json)
retain derived semantics/counts/hashes. The producer validates 150 FL semantic
anchors, six independent AP ABI anchors, twelve complete routine spans, seven
whole-instruction candidate spans, nine direct-caller sets and five native
name/case bindings. It refuses 158 native memory mutations (150 anchor changes,
seven injected interior edges and one extra stop-all caller), plus five KC
method mutations. Raw listings remain under `/tmp/fl-music-keep-contract/`,
untracked. These are analysis checks, not a complete installer acceptance gate.

## 1. Active hook families and live state

The allocator, 0x40-byte record, 0xb4-byte media object and sentinel list are
already established in [media-cue](media-cue.md#3-recordlist-layout-and-lifetime-for-the-existing-reader).
Owner `*0x5a84b4`, record offsets id `+0x10`, context `+0x14`, slot `+0x18`,
media `+0x24`, flags `+0x2c` are unchanged. Flags 1=loop, 2=playing,
0x40=DirectSound path and 0x80=music volume class retain their AP meanings.

| AP role | FL span, half-open | Whole instructions / ABI |
| --- | --- | --- |
| T1 stop-all entry `0x4982b0` | `0x4b4210..0x4b4217` | 7 B: PUSH EBX; load owner into EBX. No arguments, plain RET. Copy/rebase the absolute operand at `0x4b4213` (HIGHLOW). Continuation `0x4b4217`. |
| A stop classifier `0x4982db` | `0x4b423e..0x4b4245` | 7 B: MOV EDI,[ESI+0x24]; CMP [EDI+4],0. ESI=current record, EBX=next record, caller return at entry-site ESP+0x0c (three pushes: EBX/ESI/EDI). |
| C explicit-play seek `0x498d54` | `0x4b4d26..0x4b4d2b` | One 5 B CALL `0x4fe5f0`. **ECX=record, EDX=start ms**, no stack argument. ESI also retains record. |
| D manager status `0x4983d9` | `0x4b434a..0x4b434f` | One 5 B CALL `0x4ff580`. ECX=record, **EDX=[record+0x28]**; AX is consumed, no stack argument. |
| E explicit-play Run `0x498d71` | `0x4b4d42..0x4b4d47` | One 5 B CALL `0x4ff8b0`. **ECX=record**, no stack argument; EAX success tested immediately. |
| T2 shared play entry `0x498c90` | `0x4b4c50..0x4b4c55` | Five one-byte PUSHes: ECX/EBX/EBP/ESI/EDI. Entry ECX=slot, EDX=context; eight caller-cleaned words: id, start min/sec/ms, end min/sec/ms, loop. ESI=task at script callers. |
| T3 shared stop entry `0x498810` | `0x4b47a0..0x4b47a5` | One 5 B owner load into EAX, HIGHLOW at `0x4b47a1`. Entry **ECX=slot, EDX=context, [ESP+4]=id**; ESI=task at script caller. Plain RET consume, caller pops one word. |

All seven spans have zero raw relative encodings targeting their interiors
(measured every-byte E8/E9/short/near conditional/loop edge scan). A/C cross
an aligned qword (offset 6); D/E occupy offset 2..6; T1/T2/T3 start at offset
0. A must not inherit AP's six-byte or aligned-write assumption. T1/T3 tails
need relocation-aware stolen operands. This file evidence does not qualify
write atomicity, rollback, chain coexistence or a live installation window.

A's vanilla tail replays both displaced instructions so its JE at `0x4b4245`
consumes their CMP. `keep_running` loads EDI and skips native Pause/Stop to
`0x4b4285`: clear flag2, deliver `(context,1)`, clear slot/context. `skip_all`
goes directly to `0x4b42c4`: EBX is already the next node, its next link is
compared with zero and the loop resumes at `0x4b4230`. ESI/EDI are dead on that
exit, and must not be replaced by AP's EBP-next-node contract. Preserve the
native nonvolatile registers; save EAX/ECX/EDX/EFLAGS around injected handlers
and restore on forwarding exits. Added handlers retain the existing
`LightCallBoundary` MXCSR/LastError contract and four-byte incoming stack rule.

C returns synthetic EAX=1 to skip a same-record start-0 seek. A combat-resume
position must replace **EDX**, not AP's `[ESP+4]`. Forward paths tail-call the
seek with native ECX/EDX restored. The play checks EAX at `0x4b4d2b`, writes
positive end time into media+0x94, then reloads ECX=record before E. E's synthetic
EAX=1 must leave the following native state setup intact (`0x4b4d4b..0x4b4dab`):
record start/end, playing/loop flags and new completion binding. ECX/EDX/flags
are scratch at these call boundaries; no live XMM input is consumed by the
integer call setup or following success tests. The seek itself uses SSE2
conversion for DirectShow; the new shims need no x87 arithmetic.

At T2 the id is stack word1, not AP word3; slot/context are registers, not
AP's first two stack words. With the existing four-dword saved handler block,
FL T3 id is block[5], slot is saved ECX/block[1], context is saved EDX/block[0],
return is block[4]. A consumed T2/T3 restores entry ESP and returns without
replaying stolen instructions. Trace and keep/queue still need one shared
claim per entry, including either install order and partial rollback.

## 2. Callers, pause/focus behavior and natural completion

The exhaustive direct E8 stop-all census is six calls (all decoded in the
producer); the corresponding AP policy labels survive:

| FL call → return | Role / shared policy |
| --- | --- |
| `0x4ec822 → 0x4ec827` | WM_ACTIVATE inactive: `skip_all` with D, paused fallback without D |
| `0x478a38 → 0x478a3d` | save: `keep_running` when pumping is bound for DirectSound; otherwise paused |
| `0x47b7ca → 0x47b7cf` | X2_SetPause native: paused DirectSound hold |
| `0x479461 → 0x479466` | load: vanilla, clear holds/queue and close KC settling gate |
| `0x4b3b31 → 0x4b3b36` | P_Leave: vanilla |
| `0x477be2 → 0x477be7` | session start: vanilla, close KC settling gate |

Status `0x4ff580..0x4ff8a7` is 807 B / 266 instructions. Active DWORD
`0x5aaf34` is cleared before deactivation's stop-all at `0x4ec81c`; when inactive,
input flags `[*0x5a8478] & 0x4000` control background processing. At
`0x4ff59e` a clear bit jumps to `0x4ff631`, returning EAX=2 without querying
media. This is AP's false-ended condition. D can answer 1 for the existing
`skip_all` hold under exactly that condition. The caller reads only AX and
reads a discarded position when it is 1; 2 re-seeks a looped record or clears
playing and fires completion at `0x4b43de..0x4b4422`. No new focus state is needed.

The pause wait remains unpumped: the known
[pause loop](pause-background.md#1-pause-entry-key-loop-and-button-behavior) at
`0x478807..0x47885c` calls only `0x4ec620` (input/messages) and `0x4eccd0`
(key), not media update/pump. The same reason AP withdraws a running pause keep
applies to FL: keep a paused record for the script's same-id replay; do not
leave a streaming queue orphan running through this wait. Same-id queue
handback and ordinary pending-change termination remain shared policy.

FL explicit play has four direct callers: sector selector `0x466313`, script
MOV_PlayMovie `0x4b593a` (return `0x4b593f`), MOV_PlayMovieFrom `0x4b5a4a`
(return `0x4b5a4f`) and helper `0x51390c`. **The selector caller is additional
to AP's three.** It already reaches the shared entry, so the current rule
"any other music play ends the queue" covers it; do not filter to the script
caller set. StopMovie has two direct callers: script `0x4b598e` (return
`0x4b5993`) and helper `0x513954`. AP's separate station-screen stop caller
has no additional FL E8 equivalent in this census.

## 3. Save streaming, Run skip and gzip ownership

FL pump `0x4fe7d0..0x4fed0f` is 1,343 B / 458 instructions, with zero x87
instructions. **ECX=media**, no stack arguments, plain RET; EBX/EBP/ESI/EDI are
saved/restored, EAX/ECX/EDX/EFLAGS volatile. Unlike AP's partial-AX drain return,
FL defines full EAX as 0 failure, 1 streaming, 2 drained/stopped; keeping AP's
zero-extended AX result is valid. Its native owner is status's tail jump at
`0x4ff862`, preceded by flags checks excluding bit8 and requiring bit0x40.

The shared pump gates are unchanged: media flags+0x8c, stream+4,
DirectSound buffer+0x44, buffer state+0x48 (1 stopped/re-arm, 2 playing,
4 draining), current/write positions+0x4c/+0x50, source state+0x64 and
position interface+0x70. The head sees state1/source1 and calls buffer Stop,
zeros positions and SetCurrentPosition(0). Run `0x4ff8b0..0x4ffa64` writes
state1/source1 at `0x4ffa01/0x4ffa08`; E therefore must skip it immediately
after C preserved a running save episode. Drain calls Stop at `0x4fece7`,
sets buffer state1 and write position0, returns2. Queue-mode return2 must
clear record flag2 as AP already does, preventing the manager from rearming it.

The fallback pause counterpart is **`0x4b41b0..0x4b4203`**, ECX=record,
83 B / 31 instructions, no arguments, ESI preserved. It Pauses via media+0x74
vtable+0x20 and Stops buffer+0x44 vtable+0x48, then sets state1/source1.
Two real differences from AP `0x4d1810`: FL clears record flag2 at entry, and
returns EAX=1 even when a native Pause/Stop HRESULT is negative. The ordinary
save hold has already had flag2 cleared by A's bookkeeping, so this does not
change the shared hold/pump fallback policy. Queue-mode early stop must retain
its existing rule of not invoking this fallback. Do not interpret the FL
helper's 1 as proof of successful COM pause.

| Save I/O witness | FL contract |
| --- | --- |
| gzwrite | zlib1.dll IAT `0x5503f0`; call `[IAT]` at `0x506871`, **six** bytes; cdecl `(file,data,unsigned size)` |
| native close wrapper | `0x506940..0x50697a`; ECX=file object, no stack args; tests/clears open flag, pushes exactly one native handle |
| save close call | `0x4791fc..0x479201` CALL wrapper, file at `[EBP-0x84]` |
| wrapper's gzclose call | `0x50695d..0x506963` CALL `[0x550408]`, **six** bytes |
| import-entry stack | `[ESP]=0x506963`, `[ESP+4]=handle`, `[ESP+8]=0x479201` |

The existing observer's immediate return plus word above the cdecl handle
continues to discriminate the normal save close exactly; change the compared
pair to `(base+0x106963, base+0x079201)`. No AP import thunk exists to relocate:
FL calls its IAT directly. File wrapper `0x506940` has just two E8 callers,
the save above and `0x514c6a`; the remaining direct gzclose paths remain excluded.

**One close owner.** The already-ported
[`loading_hooks_fl.cpp`](../../../src/engine/fl/loading_hooks_fl.cpp) owns six
read-buffer imports including gzclose, and forwards through shared `gz_buffer`.
It does not expose a save observer or own gzwrite. AP
[`loading_trace.cpp`](../../../src/proxy/loading_trace.cpp) deliberately skips
`gz_buffer_row` on FL, which includes gzclose; it can find gzwrite, but its
`bind_save_observer` cannot report both rows live. In addition, AP music keep's
binding compares AP slot addresses. Consequently enabling the AP observer
unchanged cannot arm FL save pumping. Extend/reuse the existing FL import owner
and bind its close notification plus the FL gzwrite route as one capability;
never claim a second competing gzclose slot. Run the observer before the real
write and before buffer close/unregistration, then preserve
`gz_buffer::close` unregister-before-real-close and native return/error behavior.
No second read-ahead cache or new file-lifetime controller is indicated.

Reuse AP cadence: sample every 1,024 writes, pump at most every 250 ms on the
arming thread, bound an episode to 120 s and the replay Run skip to 2.5 s.
These are policy constants, not measured FL throughput. The status hook runs
per playing record per manager update; the save wrapper runs per write but
its idle path remains one active-flag load/branch. Added native pump calls
are at most four/second while saving; lookup walks remain event/sample work,
not per draw. Retain the bounded 4,096-node validation and four-hold cache.

## 4. Ownership, reentrancy and failure limits

Records/media are raw engine allocations; neither list membership nor COM
AddRef pins them. Retirement `0x4b4490..0x4b450b` calls completion, clears the
binding, tears down media at `0x4b44de → 0x4ffd70`, **then** unlinks both
neighbors and frees the record at `0x4b44f3`. Media teardown releases its COM
interfaces and frees the 0xb4-byte native object. The update walk saves next in
EDI before status/COM and retains current in ESI; play retains ESI across
seek/Run and the prior completion. Reacquire list/id/media identity for any
later policy event; never dereference cached media after free or blindly retain
it across a load.

Fixed callback closure can be reused from
[render-exclusion](ui-render-exclusion.md#activation-and-audio-completion-publish-results):
VM construction publishes `0x4c1a40` at `0x4b8d44`; it stores a task result
and calls ready-list insertion `0x4c1880` at `0x4c1a9b`, without interpreting
KC. The other registered result target `0x478910` also publishes a result.
Thus stop/completion alone does not recursively run the requesting task.
This closes a specific callback frontier, not every COM/recovery boundary.

FL seek and pump retain AP's recovery hazard: E_OUTOFMEMORY retries can invoke
registered `*0x5aae24` under guard `0x62de10`. Pump has explicit indirect
recovery calls at `0x4fe9d9` and `0x4feb77` and calls recovery helper
`0x4d9200` on other retries. COM can dispatch messages; deactivation can reenter
stop-all, and SC_CLOSE can synchronously execute NotifyLeave/KC as the existing
[close analysis](ui-render-exclusion.md#sc_close-executes-notifyleave-synchronously)
establishes. Do not free the queued record from alt-tab T1 inside a manager
walk; retain AP's deferred free at Present. Do not describe an arming-thread
check as a record pin, or add an unproved cross-thread COM/pump worker.

Save/load parents install native SEH; close on normal save return is established,
but exception/early cleanup may use other close sites. Those must not satisfy
the normal-close pair. Existing timeout, identity refusal and next-Present
fallback remain shared AP limits. This study does not prove that all exceptional
saves resume bytecode or retain the same record. Borrow validity across actual
COM message dispatch, native CPU/LastError preservation and return/unwind
transparency need the affected hook fixtures and user capture when implemented.
No new FL-only state framework follows from these shared limitations.

## 5. Queue/dock controls established and exact remaining work

FL native MOV registration (`0x4777ad..0x4777b7`) binds dispatcher `0x4b58e0`
and names `0x5a61a8`; jump table `0x4b5bb0` binds Load=opcode0, Play=1,
Free=2, Stop=3, PlayFrom=5. At the script dispatcher ESI is the calling task;
its id at +8 is passed in EDX. Play uses ECX=slot plus eight stack words and
pops 0x20 at `0x4b593f`; Stop and Free use ECX=slot, EDX=task id and one id
word, popped at `0x4b5993`/`0x4b597a`. A consumed stop/play must preserve
its native caller's pending-result protocol, not synchronously complete a
parked play.

FL story build1636047691 (CODE 2,731,291 B) has the five methods below **equal
to clean AP stock modulo CODE relocation and named native/call operands**.
This is measured static method equivalence, not inferred from matching names.

| Method | FL entry / measured instruction count | Queue operand witnesses |
| --- | --- | --- |
| `200::PlayMusicWithFade` | `0xf16f` / 202 | Stop call `0xf237`, IP `0xf23c`; Play call `0xf2da`, IP `0xf2df`; m17 assignment after stop `0xf270`; Load144 `0xf292` |
| `200::LoopMusic` | `0xf35e` / 9 | callself PlayMusicWithFade returns `0xf36c` |
| `200::ChangeTrackMusic` | `0xf3ae` / 11 | forked LoopMusic call returns `0xf3c3` |
| `200::MusicFadeOut` | `0xf0a0` / 38 | Stop returns `0xf0ff`, excluded from deferral |
| `200::StopMusic` | detected / 9 | same fade/stop policy as AP |

The same frames imply the AP six depths (old18, loop return13, new11,
fade10, loop9, change return7) and stack index-19; this is an inference from
method equivalence plus the native five-byte cell model, not a measured FL
frame capture. Native interpreter `0x4bedbf..0x4bedce` reads task IP+0x1c,
index+0x18, forms index*5 and adds task cells+0x14. At native dispatch it
writes index/IP at `0x4c0751/0x4c0759`; task+0x20 must be1 at `0x4c0793`
or it suspends. This supports the existing park/completion protocol.

`150::WarpToSector` entry `0x19cfc` has the corresponding fork/conditional
window at `0x19d97` and continuation `0x19dab`, skipping StopMusic(500).
An overinclusive **every-byte** CODE scan finds zero absolute branch encodings
into its six-byte interior; no method entry is inside. Native JMP handler
`0x4bfb21/0x4bfb23` reads the stored operand and adds VM.CODE+8. Loader stores
build+0x143c at `0x4b90af`, CODE size+0x10 at `0x4b9122`, CODE pointer+8 at
`0x4b913b`. These are specific FL counterparts of AP's three KC witnesses;
the little-endian runtime window and writable-page gate still need validation.

Dock policy also survives the script comparison: `150::DockingAllowed` calls
ChangeTrackMusic(14) at `0x1ae3b`; EnvironmentToMusicTrack at `0xf639..0xf644`
returns `2001 + SE_Random(4)` for a non-sector environment (2001..2004 inferred
from Random's usual exclusive upper bound). PlayMusicWithFade re-reads the
requested-id cell after the stop, allowing the current same-id substitution.
Do not invent an FL docking-id list or separate queue state machine.

Remaining queue qualification is bounded and concrete:

1. **Free ABI:** AP ECX-only delete `0x4986b0` has no callable equivalent in
   this FL path. FL `0x4b4640..0x4b46a4` inlines search and calls retirement
   at `0x4b4698`, with ECX=slot/EDX=context and stack id. It completes the
   requester even on a miss and has no reliable bool result contract. A small
   FL free shim may use this entry with invalid slot32/context0 and the key
   stack argument, after validating/clearing the queue binding; or use a bounded
   id lookup followed by ECX-record retirement. Qualify the chosen shim and its
   result through the existing pre/post lookup semantics before wiring queue
   handback. Do not pass a key as ECX to `0x4b4640`.
2. **Live story reader:** validate FL task capacity+0x10/end-pointer allocation,
   VM class/native-name layout used by `story_detect.h`, loader operand-swap
   widths and memory-form fork window. Known targets are loader CODE stores
   above, interpreter entry `0x4bed90`, fork handler `0x4c03db`, native handler
   `0x4c06f8`, JMP `0x4bfb21`, MOV registration `0x4777ad` and table `0x5a61a8`.
   Reuse existing detector/settling cache; these file offsets are witnesses,
   not permission to hard-code one story's IPs. Add the five-byte caller/record
   ABI adapters before claiming queue or docking support.
3. **Load ordering:** retain T1 load/session gate closure at returns
   `0x479466`/`0x477be7`. The save loader reaches VM restore at
   `0x47983c → 0x4bcb10`; full initial-session call ordering and story reload
   completion must be bound to the same two-stable-observations rule. Reuse the
   existing edition-neutral Present notification described in
   [music-alert integration](music-alert.md#5-existing-load-marker-and-synchronous-entry-integration-clarification).
   Its once-per-process cadence marker has the same AP limitations; there is
   no FL-specific native marker to find or new load owner to construct.
4. **Runtime acceptance:** queue deletion/handback and FL cell tags/depths,
   gate defer/park/release, second-request collapse, save and dock continuity,
   pause and focus fallback, and native Windows behavior remain unexecuted.
   Build/cross-compilation alone cannot settle audible or native behavior.

## 6. Static queue closure follow-up (2026-10-29)

This closes the static questions in §5 items1–3. It supplements the accepted
initial contract; its producer/record are separate so the initial evidence
history is unchanged:

```sh
python3 verification/results/fl-port/music-keep-contract/queue_followup.py
```

[Producer](../../../verification/results/fl-port/music-keep-contract/queue_followup.py),
[compact record](../../../verification/results/fl-port/music-keep-contract/queue_followup_summary.json).
Measured offline: **127 semantic anchors, 129 anchor/dispatch mutation refusals,
two additional semantic guard mutation witnesses, eleven decoded regions**;
all256 file operand widths and all256 memory width/handler-admission cases
match the shared decoder model. The source cache witness confirms the existing
8-frame cadence and observation invalidation. No production edits, compiler,
native/Wine execution, game, install or commit were used for this follow-up.

### 6.1 Task capacity and end pointer

FL `0x4be850..0x4be8f8` allocates the0x40-byte task. Its one stack argument
is requested cell capacity: load at `0x4be8ab`; minimum32 at `0x4be8b2`,
otherwise round up to the next multiple of32 at `0x4be8bb..0x4be8c4`.
It stores the rounded **cell count**, not bytes, at task+0x10 (`0x4be8c9`),
multiplies by5 (`0x4be8cc`) and allocates through `0x4b8a10` at `0x4be8d3`.
The returned allocation base plus5*capacity becomes task+0x14 at
`0x4be8e2..0x4be8e7`; index+0x18 starts at0 (`0x4be8db`). On successful cell
allocation, valid cells are end+5*i for `-capacity <= i <= -1`. The established index-19 and
six depths18/13/11/10/9/7 fit the minimum allocation. The task id+8 is the
registry id written earlier at `0x4be8af`. Only the task-header allocation has
an explicit null check (`0x4be865/0x4be867`) and returns zero on that failure.
Cell allocator `0x4b8a10` can return zero (`0x4b8a24/0x4b8a26 → 0x4b8a68`),
but the task constructor does not test that result: `0x4be8d8..0x4be8ee`
still forms end=5*capacity and returns the nonzero task pointer. Nonzero end
and plausible capacity therefore do not establish a cell allocation.

The allocation helper and interpreter evidence do not pin a task across later
scheduler changes. Reuse the current fault-free task read, capacity limit,
negative-index bounds and per-stop frame/tag checks. No FL task owner or new
frame format is needed.

### 6.2 VM, class, method and native-name layout

FL native method resolution at `0x4bb310` establishes the shared reader layout:

| Input | Exact FL witness |
| --- | --- |
| VM class count+0x1c / records+0x20 | `0x4bb364/0x4bb367`, passed to `0x4cd180` at `0x4bb36b` |
| signed-key binary search,0x38-byte records | `0x4cd194..0x4cd1a9`: mid*56, signed JL, advance+0x38 |
| record+8 class pointer | `0x4bb391`; loader assigns self at `0x4b91c8` and `0x4b9929` |
| class method count+0x28 / rows+0x2c | `0x4bb3a3/0x4bb3aa`, passed to `0x4cd1d0` |
| class parent+0x14 | `0x4bb3bc`; loader parent binding at `0x4b993a` |
|16-byte method rows; entry+0 / name offset+4 | `0x4cd1ec`, `0x4cd1f1`; resolved entry read at `0x4bb3ed` |
| decoded strings+0x14 / size+0x18 | `0x4bb150`: unsigned offset bound at `0x4bb16b/0x4bb16e`, base+offset at `0x4bb197/0x4bb19a` |
| method name order | `0x4cd200..0x4cd225`: byte comparison and carry-derived sign; unsigned lexical order, as the shared reader uses |
| native names at VM+0x40+g*0x18 | `0x4b9ff0..0x4b9ff6`; NULL-terminated command pointer arrays scanned at `0x4ba03c..0x4ba044` |
| native group search1..31 | initial1 at `0x4b9fdf`, bound32 at `0x4ba058`; first matching group/index stored as two u16 words at `0x4ba06a/0x4ba06d` |

The loader reserves an extra zero-initialized class record: it increments the
file count at `0x4b9184`, stores the count+0x1c at `0x4b918f`, and begins filling
at records+0x38 (`0x4b91b6..0x4b91c8`). The complete record array is sorted with
stride0x38 at `0x4b9900..0x4b990d`. For this object,315 file classes become316
records including the reserved one. Its null class pointer is compatible with
`story_detect::method_ends`, which explicitly skips null class pointers.
The RAM model includes that record; it does not assume the observed count as
a production constant.

All listed `story_detect.h` layout constants survive. Only the VM global's
edition binding changes to `*0x5aa174`; group ids and command ids remain
run-time lookups by name. Do not replace that lookup with a fixed MOV group
number. Existing bounds, method-parent traversal, duplicate-name checks,
read budget, retry cache and loaded-object key remain shared.

### 6.3 Operand conversion, widths and memory fork

Native conversion is `0x4b9d80..0x4ba0a4`: dispatch byte table `0x4ba0c4`,
case table `0x4ba0a4`, indexed by opcode-5 through0x88. It swaps16-bit and
32-bit operands to little endian, resolves opcode82's STRG name to the
(group,command) pair above, and tail-jumps at `0x4ba09f` to optimizer
`0x4ba150`. Its operand widths match the existing file decoder for every
opcode. Variable78 is `11+4*u16` bytes;79 is `9+8*(u16+u16)` bytes.
The native vectorized loops reverse each variable instruction's32-bit entries;
this does not change their instruction boundaries.

The optimizer's first walk dispatches through `0x4ba548/0x4ba51c`.
Fork6f/end70 take its no-write default; native82 and calls86/88 take the
four-byte operand advance at `0x4ba3fd`, also without modifying their heads.
Its second walk uses `0x4ba604/0x4ba5d0`, indexed by opcode-5 through0xa7;
branch32/33/34 can thread their destination at `0x4ba470..0x4ba4c5`.
The follow-up checks every derived memory width plus interpreter admission
from `0x4c17cc/0x4c15d4` against the shared width model, including fused
opcodes and the special end70. No FL-specific opcode-width table is needed.

The RAM-only converted/optimized object ends exactly at CODE length
**2,731,291 bytes**, with **929,541 instruction starts** and all **8,803 method
rows** aligned (measured model counts). All six queue methods resolve through
its VM/class/name tables to the established offsets; StopMusic resolves to
`0xf382`. The fork at `0x19d97` remains6f followed by33 with the
**little-endian** destination `0x19dab`. Native JMP reads that operand and adds
VM.CODE as established in §5. Its child remains StopMusic(500), and the
six-byte memory window retains the exact footprint expected by the shared
patch. No four-byte LE word in the modeled WarpToSector method points inside
that window. Native memory values and actual writable pages still need the
ordinary installer checks; the model is not a heap capture.

**Opcode0a fidelity, precisely scoped.** FL `0x4b9df4..0x4b9dff` reverses the
entire eight-byte scalar operand: it swaps the DWORD positions as well as each
DWORD's bytes. The reusable offline `story_vm_image.py` model reverses each
DWORD independently. The follow-up corrects its two0a operands in RAM before
checking the model; it does not modify that shared helper or the accepted
initial record. Production `story_detect` reads already-converted native CODE,
assigns0a width9 and skips its opaque eight-byte operand. Its queue signatures
inspect fork/native/call heads at instruction starts; queue cells contain
integer ids/fade/loop and return offsets. Consequently this offline numeric
representation difference affects neither production queue discovery nor
patch construction, and **requires no production correction**. Any future
offline test that interprets0a scalar values must use the native full-eight-byte
order; expanding the queue port into a VM conversion rewrite is unwarranted.

### 6.4 Chosen free shim and wrapper callback exclusion

Use the existing native MOV_FreeMovie wrapper **`0x4b4640`**, with
**ECX=32, EDX=0, one caller-cleaned key word at [ESP+4]**. A small integer
cdecl shim must push the key, set the two native registers, call the wrapper,
pop exactly4 bytes and ignore native EAX. Do not declare it as ordinary
Microsoft fastcall: the native body ends with plain RET and does not clean
its stack id. Preserve the existing engine-call CPU/LastError boundary and
nonvolatile-register contract.

Before calling, reacquire the first linked eligible record with this key and
nonzero media and preserve the binding semantics applied by the existing
`QueueOutcome`. The free shim must not add a binding clear. The wrapper scans
by id with a nonzero-media test at
`0x4b4658`; on a hit it calls ordinary retirement at `0x4b4698`, then rejoins
the same result path as a miss. At `0x4b467d` it compares the saved slot EDI
with32; signed **JGE at `0x4b4680`** exits before the function-pointer load and
indirect result callback `0x4b468f`. Slot32 therefore excludes requester
completion on both hit and miss. Context0 is defensive; it does not substitute
for the slot guard.

Retirement independently reads record slot/context and can complete its binding
at `0x4b44c4`. Ordinary end outcomes use `wake` or `clear`; `queue_apply`
delivers the requested wake, then zeros+0x14/+0x18 before `free_key`, so its
retirement zero guard at `0x4b4498` goes straight to teardown `0x4b44d7`.
That suppression is required only where the existing outcome requests it.

**Same-id handback duplicate-id refusal requires retirement completion.**
`queue_on_stop_all` constructs `restore=true`, optionally `free_id`, and leaves
`wake=false, clear=false`. `queue_apply` first frees the unplayed old-id record
(its context is already zero), then tests whether another old-id record remains.
If none remains, it restores O's id and keeps its parked binding. If one remains,
it frees private P from the `restore` branch, retaining O's slot/context: native
retirement must wake the parked task. This happens before the common wake/clear
block, and the handback outcome requests neither flag. Blanket clearing in the
new shim would remove that wake and can strand the task.

The producer traces those actual source assignments and `queue_apply` order.
Its CFG includes both valid cases: cleared bindings suppress record completion;
an intact parked binding, with active VM, valid nonzero slot/context and nonzero
completion function, reaches `0x4b44c4` before media teardown. Blocking that
completion node makes teardown unreachable under those guards. A blanket-clear
control removes the required completion and a source-outcome mutation adding
`clear=true` is rejected. Slot32 still excludes only the wrapper's requester
callback in every case. Changing JGE to JG exposes that wrapper callback at32;
reversing the two retirement zero guards exposes completion in the suppression
case. These two native semantic mutation witnesses are distinct from the policy
regression control and individual opcode-anchor checks. Ordinary COM
Stop/Release/free calls remain reachable; no callback-free teardown is claimed.

There is no reliable native BOOL result. Derive success from the existing
pre/post linked-id lookup, including handback's post-free duplicate-id refusal;
never use native EAX to decide that a record disappeared. A missing owner/record
or refused read takes the current lost/stale fallback. Calls remain at the
existing queue events, with native free once per requested deletion and bounded
lookups already required by the queue. This choice adds no allocator, registry,
lock, second owner or new deletion framework. The previously documented COM,
recovery and alt-tab deferred-free limits remain in force.

### 6.5 Initial-session and restored-story ordering

Session function entry is `0x477ba0`. Its decoded prefix through
`0x477be2 → stop_all` has no branch: the return `0x477be7` therefore closes
the existing KC gate before either session arm. The saved-game arm calls
`0x479430` at `0x477c52`; the initial-story arm calls `0x4b8e20` at
`0x477d15`. Load entry `0x479430` likewise has no branch before its own
`0x479461 → stop_all`, return `0x479466`. That precedes VM restore
`0x47983c → 0x4bcb10`, which reloads the story through
`0x4bcc27 → 0x4b8e20`. Thus both initial-session load and saved-story restore
reach the same story loader after a gate-closing stop, including a VM pointer
that remains unchanged.

The common story loader writes build/CODE size/CODE pointer at
`0x4b90af/0x4b9122/0x4b913b`, then calls conversion/optimization at
`0x4b9a1e → 0x4b9d80` before its normal return. Parent restore can continue
filling saved VM state after that call; it is not a separate new music-load
completion signal. Keep the existing `KcPatch::load_begin()` behavior: discard
the prior observation and marker, rather than resetting a new controller.
Two consecutive due observations eight frames apart must agree on VM, CODE,
size, build, detection outcome, window offset and bytes; a changed observation
restarts that agreement. Load-begin does not itself reset the cadence countdown.
The existing Present marker can admit the next due observation, with its
accepted once-per-process limitations. Neither the marker nor two observations
is a mathematical proof that arbitrary foreign reentrant load code cannot
pause between writes; this is the same established AP policy and no reachable
FL-specific difference requires strengthening it here.

The static layout, free ABI and native ordering gaps are closed. Implementation
still needs the affected FL shims, relocated admission/rollback and shared gzip
observer extension, followed by the scoped runtime checks from §5 item4.

## 7. Common Load hook and music admission (2026-11-01)

The third direct gameplay log reports `music_keep reason=bytes_mismatch`,
`armed=0` and a disabled queue despite requesting both. The reviewed music
profile includes RVA `0x79430..0x79466`, witnessing the load caller's path to
stop-all. Common UI preparation precedes music preparation and replaces the
first five bytes at `0x79430` with its owned Load-entry jump. The original
instructions are `push ebp; lea ebp,[esp-0x28]`; the original call to stop-all
at `0x79461` remains outside the replacement. The exhaustive current music/UI
census measures 81 regions × 21 sites and exactly this five-byte overlap.

`ui_scale_fl::read_music_startup_code` reconstructs only those copied original
bytes for the cold music profile. It requires the matching prepared common
owner and thread, no prior load/render scope, no active/backlogged scope,
the exact recorded live entry jump, dispatcher and adapter head, original tail
and continuation, and restored site protection. A matching jump without a
prepared owner stays raw and fails the existing hash. An owned but changed
jump/chain/protection or outstanding preparation failure refuses the read.
The profile's full region digests and HIGHLOW validation remain unchanged;
no exception is made for an arbitrary detour or changed neighboring byte.
Successful cold rollback removes reconstruction authority while preserving
ordinary raw admission, even though engine_patch retains its tail identity.

The loading import owner accepts this reader as a cold callback, and shared
music preparation supplies it explicitly. The callback adds no runtime music,
Present or draw work. Host instrumentation measures 483 code reads plus one
page query for the full profile, versus 479 raw reads; the four extra reads
validate the single overlapping jump chain. No native instructions or hook
ABIs changed. Native data-only mapping proves actual common preparation,
profile admission, tamper rejection and complete-image rollback; it does not
establish audible pause/focus continuity. Producers and compact checks are
in the [music verification ledger](../../verification/music-keep.md#fl-common-load-admission-correction-2026-11-01).
