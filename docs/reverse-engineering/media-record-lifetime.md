# Media record retirement and synchronous-call lifetime boundary

2026-09-20, derived static RE of X3AP.exe SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`.
Playback context: [media-cue-playback.md](media-cue-playback.md).
This independently reviewed static investigation performed no Wine/game execution,
build or installation. It qualifies no new hook.

## Outcome: STOP at any newly introduced synchronous COM boundary

No record-pinning or retirement-exclusion contract is established for added
Stop/Pause/seek calls. The engine retains raw record, media-object and destination
pointers across existing calls. That describes existing behavior, not a proof
that a longer/different synchronous call is safe. COM AddRef can retain the
particular interface; it cannot keep the `malloc`-owned 0x40-byte manager record
or 0xb4-byte media object alive, and it cannot prevent engine replacement or
state changes. A seek-only reentrancy guard would miss destructive entry paths.

Concrete boundaries are (1) the unresolved completion callback dispatch reached
by window deactivation, followed by raw-record writes; (2) a conditional window
close → synchronous shutdown → media destruction chain; and (3) the known
memory-recovery callback → destination retirement chain. No observed reentrant
free, runtime race, or returning use-after-free is claimed. Thread ownership and
COM-to-window-dispatch behavior for the actual backend remain unproved.

## Record and media ownership, exact frees and unlink order

The manager root is `*0x00606f44`; normal records have next at +0, prev at +4,
ID +0x10, callback context +0x14, callback table index +0x18, media +0x24,
cached destination object +0x28 and flags +0x2c. A zero next pointer marks the
walk terminator. Successful allocation links a record before playback.

Two concrete record free sites were identified:

- Unpublished allocation failure: `0x004981e3 → free(0x0050e1b0)` after the
  constructor returns NULL; it never entered the list.
- Linked retirement: `0x004984d0`, private ABI **ESI=record**, no stack arguments,
  plain RET at `0x00498549`. It uses EAX/ECX/EDX and condition flags as scratch,
  relies on callee-preserved ESI through callbacks/teardown, and never acquires
  a lock, increments a reference, or marks the record retiring. If +0x18 and
  +0x14 are both nonzero, it dispatches the registered completion callback at
  `0x00498501` with two caller-cleaned dwords `(context,1)`. Only afterwards does
  it clear +0x18/+0x14 (`0x00498506/0x0049850d`). It calls media teardown at
  `0x0049851b`. **Record remains linked and +0x24 is not nulled throughout both
  the callback and COM teardown.** Finally `0x00498525` sets prev->next=next,
  `0x0049852d` sets next->prev=prev, and `0x00498530` frees the record.

Exactly six raw E8 callers of `0x004984d0` were found and decoded:

| Callsite | Concrete path |
| --- | --- |
| 0x004971e8 | 0x00497190 subsystem clear, iterates all records |
| 0x004980e7 | 0x004980d0 manager shutdown, iterates all records |
| 0x004984b0 | manager update after pump return0 |
| 0x004986d7 | 0x004986b0 delete-by-ID, ECX=ID; requires matching non-NULL +0x24 |
| 0x00498dde | shared explicit-play seek/sample-run failure |
| 0x00498fd8 | speech-play failure |

Delete-by-ID is exposed by VM call `0x00499865 → 0x004986f0 → 0x004986b0`;
there is no busy/playing/refcount check before destruction. Clear/shutdown save
next before freeing current; this supports their own sequential iteration, not
nested removal of their saved next pointer. Manager shutdown additionally frees
the 0xfb0-byte root at `0x00498114` and clears `*0x00606f44` at `0x0049812d`.

The media object is separately malloc(0xb4) at `0x004cf4aa` (retry at 0x4cf4c2).
`0x004d1d40` takes **EAX=media**, preserves ESI/EDI with pushes/pops, calls audio
Stop, graph Stop at `0x004d1d6a`, SetState(STOP) at `0x004d1d7a`, releases
interfaces/filter resources, and frees the engine object at `0x004d1dd4`.
Its five E8 callers are record retirement `0x0049851b` and constructor cleanup
`0x004cfa62,0x004cfaca,0x004d0114,0x004d0162`. No raw E9 entry jumps or literal
address occurrences exist for either destructor (also none for delete-by-ID).
These exhaust the scanned E8/E9 and literal entry evidence; EB/Jcc entry
closure, arbitrary computed indirect transfers and external corruption are not
covered by those scans.

Video cleanup `0x004d1b70` releases sample at `0x004d1ba7`, source surface at
`0x004d1bb9`, then stream/filter interfaces, nulling fields only after each
Release. Blit failure separately releases/nulls surface and sample at
`0x004d178e/0x004d17a3`. Ordinary stop `0x00498810 → 0x004d1810` instead Pauses
and retains them; clearing playing bit2 is not a lifetime pin.

## Callers' live pointers and callback ordering

The manager saves EBP=next at `0x00498397`, retains EDI=current record, resolves
and stores destination without a reference at `0x004983cd..0x004983d1`, then
calls pump at `0x004983d9`. Both current and saved next are assumed usable after
return. Loop seek returns to the raw `[EDI+0x20]` read at `0x0049840f`; no relookup
or identity validation intervenes. Explicit play keeps ESI=record across seek
`0x00498d54`, subsequently uses `[ESI+0x24]` at `0x00498d66`, and retains it across
sample/run and the previous completion callback at `0x00498dbf` before writing
new callback state. Seek itself keeps ESI=media across COM and recovery calls.

Callbacks are selected from `*0x006085e4 + 0x34 + index*0x18`, after registry
presence/enabled/index<32/function checks. The inspected sites use caller-cleaned
`(context, status)` arguments. The callback targets and their complete reentry
closure were not established here. Calls precede record field clearing at
stop-all `0x0049834e→0x00498353`, manager finished `0x00498461→0x00498466`,
manager error `0x004984a3→0x004984a8`, stop-by-ID `0x0049888c→0x00498898`,
retirement `0x00498501→0x00498506`, and explicit-play replacement
`0x00498dbf→0x00498dc4`. Thus callback delivery is neither deferred nor protected
by pre-clearing ownership. This proves exposure; it does not prove a target
actually frees its delivering record.

Two concrete window entry paths matter to a COM call that dispatches messages:

1. Main WndProc `0x004d3620`, WM_ACTIVATE inactive arm calls `0x004982b0` at
   `0x004d36bd`; stop-all Pauses each playing stream, clears playing, dispatches
   callback at `0x0049834e`, then writes record state. WndProc→callback is proved;
   callback→delete remains unresolved. First explicit ID2 play may not yet have
   bit2, but replay/loop can, and other live records still receive callbacks.
2. SC_CLOSE calls `0x00401d60` at `0x004d3782`. Its fallback calls shutdown
   `0x00401dd0` at `0x00401dc5`. Shutdown conditionally invokes subsystem clear
   `0x00497190` at `0x00401eab`, which invokes the record destructor at
   `0x004971e8`; later it invokes manager shutdown at `0x00401fcd`. This is a
   concrete conditional window→free chain, but shutdown may terminate the
   process instead of returning to the suspended caller. It is not evidence of
   a returning UAF, nor proof that the proposed COM call delivers SC_CLOSE.

## Recovery and destination pointer validity

`*0x00608a00` is assigned `0x00406d80` at `0x004035b1` (the single direct absolute
store seen in the EXE disassembly). Seek calls it on E_OUTOFMEMORY at
`0x004d051c`; pump uses `0x004b8b60`, e.g. at `0x004d1557`, `0x004d160a`,
`0x004d16fc`. `*0x006090f0` guards recursive **memory recovery only**: ordinary
loads/stores set1 around the call, reset0 on normal return. It is not a record
lock, thread-owner check, busy count, or unwind-safe lifetime guard.

Known recovery calls `0x00406d8c → 0x004f5200`. That routine examines the same
16-byte texture slot array `*0x006069ac` used by media destinations. For slots
with +8 non-NULL, +0xc==0, flags+4 lacking0x20000000, it calls `0x004f38d0` at
`0x004f5251` with address of slot+8. This decrements the destination object's
+0x60 reference count at `0x004f38e7`; when <=0 it calls cleanup `0x004dcc70`,
which Releases surface+0x30 at `0x004dccf7` and nulls it, and finally frees the
0x64-byte destination object at `0x004f3926`. Slot+8 is cleared at `0x004f3943`
even if another reference kept the object alive. These are real existing
protections (slot condition, object reference count); media lookup/pump does not
participate in them in the inspected interval. Whether ID2's actual destination
has a protective slot flag/count is not observed.

Pump caches destination->surface+0x30 at `0x004d151b/0x004d1520` before sample
COM/recovery, then uses that cached surface at the `0x004d1735` copy call.
No AddRef appears between cache and use. This proves a conditional resource
retirement route across an existing synchronous operation; it does not prove
that its release predicates hold for a real playing target. Added calls must
not assume the cached object/surface remains current solely because the slot
was in bounds. Invalid slot bounds in manager also skip updating +0x28 rather
than clearing it. Reset remains unqualified; WndProc's reviewed body does not
directly call Reset, which is not a closure proof for helpers or COM dispatch.

## Hook boundary, unwind and missing evidence

The prior seek report's six-byte entry boundary and three caller discriminators
remain useful structural facts. No patch is authorized by this report. Entry
EAX=the 0x40-byte manager record; `0x004d043b` loads
ESI=[EAX+0x24], the 0xb4-byte media object. The caller-cleaned stack start-ms,
preserved EBP/EBX/ESI/EDI and caller's continued record accesses constrain any
future implementation. The
record destructor's ESI ABI is also distinct from media destructor EAX ABI.
The relevant seek/pump/retirement functions have no own SEH frame; the constructor
does, which is not an active-record pin. Callback guard resets and field nulling
are normal-return writes, with no finally/rollback mechanism in these bodies.
Preserving CPU/FP/LastError and rejecting nested hook invocations cannot by
itself repair free/replacement through uninstrumented retirement entries.

A conditionally safe register-only interval exists only if the calling thread
already owns a valid object and no foreign thread mutates it; that ownership
premise is not proved. No inspected existing protection supports extending it
across arbitrary new COM calls. To lift STOP requires a concrete owner/reentry
contract covering all retirement paths and media/destination identities, or
proof that the relevant calls cannot dispatch destructive work. Re-scanning by
ID or pointer alone cannot exclude replacement/ABA after malloc reuse. Any
future defer/free policy must account for current and saved-next records,
completion semantics, exception/unwind, shutdown, and Reset; it is design work,
not a consequence automatically supplied by a COM AddRef or a mutex.

## Reproduction and scope

Run `python3 /tmp/verify_x3_media_record_lifetime.py` from the repository.
It reuses project `verify_media_cue_site.decode/raw_call_sites`, reads original
EXE bytes, validates 18 gap-free ranges / 1,233 instructions / 3,508 bytes and
40 exact anchors, checks the direct caller sets, raw E9 targets and literal
function addresses, and emits/round-trips
`/tmp/x3-media-record-lifetime-check.json` (PASS). Raw disassembly stays local at
`/tmp/x3-media-lifetime-raw.txt` and existing
`/tmp/x3-media-full-disassembly.txt`. The documented Ghidra X3Render project was
empty and Voice project absent, so no decompiler-based callback closure was
produced; failures are local `/tmp/x3-media-lifetime-ghidra.log`. Existing owning
media note, prior seek report, `voice-startup-sequence.md` and
`game-callback-registration.md` supplied context, cross-checked at the cited
machine instructions. No runtime/native Windows lifetime qualification exists.

Independent review reproduced the verifier and checked four supplementary
ranges (550 instructions / 1,639 bytes) with zero original-EXE mismatches.
It confirmed the scoped lifetime boundary; native/runtime behavior remains open.


## Media completion callback registry and ordinary-return closure

2026-09-20. Derived static RE, original EXE SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`.
Follow-up to the retirement boundary above, with registry context in
[game-callback-registration.md](game-callback-registration.md). No Wine/game
execution, build or installation was performed.

### Outcome

For the **four non-NULL completion targets installed by the identified registry
writers**, completion synchronously updates result state and, for script tasks,
moves a waiting task to a ready list. It does not immediately run the VM or call
media delete/clear. The ordinary game-level call closure is closed at the known
CRT free endpoint; no unresolved game function-pointer call occurs in it.
This narrows the prior WndProc-deactivation → completion-callback → possible
media-delete concern: no such deletion edge exists in these handlers.

This is not blanket authorization for added COM calls. The separately proved
conditional SC_CLOSE/shutdown→media-free chain, COM reentrant stream-state
changes, memory-recovery destination retirement and unknown owner-thread
contract remain. No actual registry snapshot, 32 fixed runtime index assignments,
or general alias-proof of every possible registry write is claimed.

### Registry construction and writers

`*0x006085e4` points to the 0x1610-byte script/dispatch owner, not a separately
allocated callback array. Factory `0x004ab010` mallocs and zeroes it, then invokes
constructor `0x0049c9a0`. Constructor zeros 32 slot records in
`0x0049ced4..0x0049cef3` (stride0x18), sets next index +0x2c to1, installs the
script slot at index1 and advances +0x2c to2. Slot layout is:

| Offset from owner + index*0x18 | Established use |
| --- | --- |
| +0x30 | subsystem command/dispatch function |
| +0x34 | integer/status completion callback read by media manager |
| +0x38, +0x3c | other result callback forms, outside this integer completion path |
| +0x40 | name pointer |
| +0x44 | initialized zero auxiliary field |

Ordinary registrations read owner+0x2c, fill one slot, then increment that count;
there is no index bound check in generic registration `0x004ab0a0`. Its ABI is
ECX=owner, EDX=command dispatcher; stack arguments are name, integer completion,
other completion, other completion, followed by `ret 0x10`; EAX returns the
previous index. The completion pointer is read from incoming `[ESP+8]` at
`0x004ab0af`, stored at `0x004ab0b6`. It has exactly one raw E8 caller
`0x0040359b` and zero literal entry-address file occurrences. That caller pushes
`0x00404430` as the integer completion at `0x0040358c` and saves returned index
in `(*0x0057fc60)+0x418` at `0x004035a6`.

Whole-.text disassembly search found exactly ten indexed writes matching
`mov DWORD PTR [base + index*8 + 0x34],...`; each is a registry write after
index*3 scaling and owner lookup/known constructor owner. They are:

| Writer | Completion value | Index assignment |
| --- | --- | --- |
| 0x0049cf2e | 0x004a4910 | constructor index1 |
| 0x0041c90b | 0x0042d2e0 | next allocated index; saved at *0x0057fc64 |
| 0x004344d4 | 0x004604d0 | next allocated index; saved in subsystem+0x60 |
| 0x004ab0b6 | 0x00404430 from sole caller | next allocated index |
| 0x0041749d | NULL (EDX zeroed at0x41749b) | next allocated index |
| 0x0046a2f2 | NULL (EDI zeroed at0x469e84, preserved to write) | next allocated index |
| 0x00496f34 | NULL (ESI zeroed at0x496ec8) | next allocated index |
| 0x00499b7b | NULL (ESI zeroed at0x499b38) | media-command registration |
| 0x00499c96 | NULL (EDI zeroed at0x499bd1) | next allocated index |
| 0x004b11c9 | NULL (EDX zeroed at0x4b11c7) | next allocated index |

The media command dispatcher `0x004997c0` is stored at slot+0x30, whereas that
slot's completion +0x34 is NULL. It must not be mistaken for a completion target.
The allocator's all-zero slot0 plus constructor slot1 do not establish fixed
values for every later slot: registration order/optional subsystem creation
controls the remaining indices. The ten-write census is a reproducible encoding
search, not arbitrary-pointer-alias analysis. It excludes none of the possibility
of external patches, corrupted indices or writes with unrelated addressing forms.

### Four completion handler bodies and ABI

All four handlers take caller-cleaned dwords `(context, status)`, end in plain
RET, and preserve the nonvolatile registers they use. Status is the second
argument; the existing media sites pass1 for completion/interruption and0 on
selected failure paths. No handler has its own SEH frame. EAX/ECX/EDX/flags are
scratch; nested helper calls use their original private conventions.

- **0x00404430..0x004044bb:** context0 selects `(*0x0057fc60)+0x498` result and
  sets WORD+0x490=1. Nonzero context hashes into owner+0x4bc, sets located
  object's WORD+0x14 bit1, writes result at +0x1c. After optional old-value
  cleanup it stores type1 and the status dword. No VM/scheduler call.
- **0x0042d2e0..0x0042d312:** sets WORD `(*0x00608504)+0x2c=1`; optional cleanup,
  then type1/status at +0x34/+0x35. It does not use the first argument. No
  VM/scheduler call.
- **0x004604d0..0x00460551:** context0 updates `(*0x0060850c)+0x24` flag and
  +0x2c result; nonzero context hashes in owner+0x14, sets located object's
  WORD+0x84=1, writes +0x8c result. Optional cleanup then type1/status only.
- **0x004a4910..0x004a4970:** context is a script task key, looked up in the
  hash table at `*(*0x006085e4)`. If found, sets task WORD+0x20=1, replaces task
  typed value +0x28 with type1/status, then calls `0x004a4740` at0x4a4968.
  Missing task is a no-op; this is not a VM execution call.

`0x004a4740..0x004a47e5` acts only when task WORD+0x3a==2. It destroys/clears
old argument values, advances the stack index by the signed WORD argument count,
moves the completion value into the task's five-byte value stack, changes task
state+0x3a to1 at0x4a47b5, unlinks it, and links it into owner+0x12d8 at
0x4a47c9..0x4a47e0. The only outgoing calls are old-value cleanup0x4a8240.
**Task wakeup means list mutation, not immediate execution.** The separate
script-engine step `0x0049f770` reads the same owner+0x12d8 list at0x49f777 and
invokes interpreter0x4a26a0 at0x49f799. There is no edge to either function from
the completion closure.

### Cleanup closure, indirect control and lifetime interpretation

Shared `0x004a8240` handles typed-value destruction. The only indirect game
control transfer is its bounded seven-entry switch at0x4a8259: types8..14 map to
`0x4a8260,0x4a82f1,0x4a833a,0x4a8368,0x4a83bb,0x4a83ab,0x4a83ab`.
It decrements typed-object references and frees arrays/hash/string/value data;
container cleanup can recurse into the same function. Type9 invokes container
cleanup0x4a83e0, which recursively destroys key/value pairs. There are no
registered destructors, COM Releases, script execution, media calls, allocation
or memory-recovery callback in this reviewed game-level closure.

The closed set is the four handlers plus
`0x4a4740,0x4a8240,0x4a83e0,0x4efd30,0x49c970,0x4b8ab0,0x4ee360` (11 functions).
Its only external direct target is the already identified CRT `free` at0x50e1b0.
Generic deallocator0x4b8ab0 also contains IAT calls at0x4b8ad3/0x4b8ae3, but
both reviewed callers reach it with flags0x01000000 (0x49c988 and0x4a84c4),
so its flags bit0 (mask1) test at0x4b8ac2 selects ordinary free, not those branches.
No unresolved indirect game callback remains on the normal path.

Consequently media manager finished/error/retirement and WndProc stop-all
completion dispatch, **when selecting one of these valid installed targets with
well-formed context**, cannot itself synchronously execute
0x4984d0/0x4986b0/0x497190/0x4980d0 or resume the VM. The previous report's
unknown completion-target edge can be replaced by this narrower fact. Calling
stop-all still mutates media flags, graph state and callback ownership while the
outer seek/pump could hold live pointers. Callback-before-clear ordering is
unchanged. Typed cleanup may recurse/free memory and assumes its own valid
containers; no corruption, SEH callback or whole-CRT/OS exception closure proof
is implied. CRT free was treated as the existing allocator endpoint, not as an
unknown plugin callback, and no new hook is proposed.

### Reproduce

`python3 /tmp/verify_x3_media_completion_callbacks.py` from the repository:
PASS, 14 gap-free ranges (11 closure functions and three registration/scheduler
ranges), 628 instructions / 1,884 bytes, 32 fixed byte anchors. It validates the
four targets, closure edges, internal direct branches, switch entries, generic
registration's one E8 caller and absent literal pointer, and repeats the ten
indexed-writer census against the original EXE. It emits and round-trips
`/tmp/x3-media-completion-callbacks-check.json`; private instruction rows are at
`/tmp/x3-media-completion-callbacks-raw.txt`. Broader context used existing local
`/tmp/x3-media-full-disassembly.txt`; the switch-adjacent function was decoded
fresh from0x4a83e0 because whole-section linear decoding drifted through preceding
table bytes. No raw copyrighted instructions are added to the repository.

Independent review reproduced the verifier and checked 68 direct branch targets
and all seven cleanup-switch entries against decoded instruction boundaries.
The narrower identified-handler conclusion is cleared; the remaining lifetime
and runtime limitations above still apply.

## Transport reentry: published playback lacks post-COM cancellation checks (2026-09-20)

Targeted disassembly establishes a narrow construction property: allocator
`0x498140` calls the media constructor at `0x4981d3`, stores its returned media
pointer into record+0x24 at `0x4981dd`, then publishes the record through list
stores at `0x498265/268`. During construction, the known stop-all manager walk
cannot select that new record through the list. This is not a general lifetime
proof: manager-root shutdown, other aliases, destination recovery and foreign
thread ownership remain unresolved.

Published operations have a different contract. The inactive `WM_ACTIVATE`
arm clears global active at `0x4d36b7` and calls stop-all at `0x4d36bd`.
`WM_ACTIVATEAPP` instead calls input helper `0x4d4950` in the inspected WndProc
body; it does not directly call stop-all. Stop-all selects playing records,
Pauses at `0x4982f0`, then clears the playing bit at `0x498328` and delivers/clears
completion ownership. For ID2 flags8 it preserves media pump state +0xb0,
sample +0x14 and source surface +0x10. The playing flag remains set during Pause.

If a synchronous COM call dispatches deactivation, three distinct continuations
are possible from the examined instructions:

- Pump `Update` at `0x4d162b` can return pending and write +0xb0=2 at `0x4d16b1`
  without rechecking active/playing. Completion at `0x4d154b` can similarly
  write state4 and continue copying. This is a stale-state possibility, not a
  demonstrated use-after-free.
- Explicit replay seeks at `0x498d54`, then invokes sample/Run at `0x498d71` and
  sets playing/callback state after return, without an active/cancellation check.
  First play is skipped by stop-all while its playing bit is clear, but still
  lacks the post-call active check.
- Automatic loop seek at `0x49840a` ignores the seek result, reapplies its bound
  and continues. It does not share explicit replay's Run/reinstall behavior; a
  universal restore-Run policy would therefore change one of these contracts.

These are conditional instruction-order witnesses. Actual message dispatch
during the proposed COM calls is unobserved. They do not reopen the already
reviewed callback-to-VM question: the four known completion handlers only
update state or enqueue work. Holding COM references or preserving CPU state
does not by itself preserve the outer operation's cancellation semantics.

The engine Update call supplies flags1 and NULL event/APC/context. It does not
register an engine completion callback through those arguments; completion is
polled. This does not exclude backend message dispatch or worker activity.
No allocator Decommit was found in this scoped transport closure. Stop/seek
also do not provide an explicit pending-sample retirement or pump-state reset
contract. Adding Decommit/Stop therefore still requires pending reconciliation.

The seek routine already contains two arms: default `IMediaPosition` double
seconds at `0x4d04f4`, and flags0x20 `IMultiMediaStream::Seek` with start_ms×10000
at `0x4d055c`. The latter does not justify changing ID2's flags or silently
substituting a new interface. Both hold raw engine pointers over COM boundaries.

**Remaining boundary:** construction is isolated only from the specific list
walk before publication. Published transport still needs a concrete owner
lifetime, nested-state/cancellation and pending-sample contract. A guard against
nested entry into a new helper would not stop the existing WndProc's mutations.
No production hook or safe added COM interval is qualified by this pass.

Reproduction: `python3 /tmp/verify_x3_media_transport_reentry.py`; eight gap-free
ranges, 850 instructions, 2,455 bytes, 32 anchors and five direct-caller sets.
Local detail: `/tmp/x3-media-transport-reentry.md`; generated check record:
`/tmp/x3-media-transport-reentry-check-v2.json`; raw instructions stay untracked.
No game, Wine or native runtime execution was performed for this investigation.

Evidence erratum: review found the earlier pump range ended at `0x4d180d`,
splitting the ADD at `0x4d180c` into a pseudo-decode. The complete function ends
after RET at `0x4d180f` (exclusive `0x4d1810`). Both corrected verifiers now reject
dot-prefixed pseudo-decodes and `(bad)` instructions. The new transport witness
above uses that complete range. The earlier lifetime evidence is retained as
historical; its corrected reproduction is
`/tmp/verify_x3_media_record_lifetime_v2.py` with
`/tmp/x3-media-record-lifetime-check-v2.json`: 1,234 instructions, 3,511 bytes
and 40 anchors. This corrects extraction/counts, not the semantic lifetime
findings or the unresolved runtime boundaries.


## Cancellation continuations and request identity (2026-09-20)

Suppressing Run alone cannot represent cancellation in explicit play. At seek
return `0x498d59` and sample/Run return `0x498d76`, zero from either helper selects
destructor call `0x498dde`. Nonzero seek proceeds to sample/Run; nonzero
sample/Run commits start/end and playing at `0x498d83/89/8f`, calls the old
completion with status1 at `0x498dbf` if eligible, then installs incoming
context/index at `0x498dd2/5`. There is no third cancellation
result in these TEST/JZ pairs. These are static branch facts, not observed COM
message dispatch or a qualified hook.

Play `0x498c90` takes ten caller-cleaned dword arguments. Let S be entry ESP and
B=S−0x14 after the local slot and saved EBX/EBP/ESI/EDI. At B, incoming callback
index/context/media ID are +0x18/+0x1c/+0x20; loop is +0x3c. ESI is the record,
EDI start-ms and EBX end-ms before seek. The three direct callers
`0x49981f/0x499982/0x4f6668` clean 0x28 bytes. Both VM callers overwrite EAX
with1; the helper does not test it and supplies callback index/context0. No
boolean return contract for the outer play function is established.

| Continuation | ESP on arrival | Established behavior or required normalization |
| --- | --- | --- |
| `0x498d59`, after seek | B−4 | ADD ESP,4 precedes result test; one start-ms argument remains |
| `0x498d76`, after sample/Run | B | Result test precedes success commit |
| `0x498dc1`, after old callback | B−8 | Two callback arguments remain; playing was already committed |
| `0x498ce8`, allocation-failure dispatch | B | Uses incoming stack args/global registry; status0 callback at `0x498d12`; no record access/destruction |
| `0x498d17` or `0x498dd8` | B | Stack-only play epilogue; direct entry leaves incoming completion unreported |
| `0x4983de`, after manager pump | B | AX selects routes below; manager has the same B=S−0x14 frame size |
| `0x49840f`, after manager loop seek | B−4 | **Reads [EDI+0x20] before ADD ESP,4 at `0x498412`** |
| `0x4984be` | B | Stack-only manager epilogue; avoids current and saved-next accesses |

The alternative status0 block at `0x498de3` also lies after, rather than before,
the destructor call. Neither rejection block establishes valid registry/context
lifetime or the desired cancellation meaning. Bare epilogues pop saved registers
and the local slot, then RET; redirecting to them requires the exact stack depth.

The manager holds EDI=current and EBP=saved-next across pump `0x4983d9`, which
uses ECX=record/EAX=destination. AX0 leads to destructor `0x4984b0`; AX1 calls
position helper `0x4d0600` at `0x4983ed`, including another synchronous COM call
at `0x4d061c` or `0x4d0648`; AX2 selects loop seek or completion. Other AX values
skip to `0x4984b5`, which still dereferences saved-next. Thus no existing return
code is a lifetime-independent abort. Leaving the whole pass at `0x4984be`
avoids those accesses structurally, but needs an approved operation policy and
cannot undo writes inside the pump. Loop seek ignores EAX and normally reapplies
media+0x94 at `0x498420`, without Run or callback installation; a gate after its
stack ADD would already be too late to avoid the record load at `0x49840f`.

Incoming completion ownership exists only in the caller frame until
`0x498dd2/5`. Nested stop-all can therefore settle/clear the **old** request while
the incoming request remains uninstalled. Equal index/context identifies the
same receiver, **not necessarily the same logical request**: ordinary replacement
already sends old-status1 then installs a new request that may have the same key.
Old-status1 followed by incoming-status0 can be legitimate distinct-request
outcomes. Two invocations do not prove duplicate delivery; key equality must not
be used to deduplicate or suppress incoming completion. The reviewed handlers'
result/list updates do not by themselves establish caller request ownership.

Remaining obligations are a distinct cancellation outcome before post-COM
writes/continuations, per-operation identity, and an observed cancellation
transition (active=false alone misses deactivate→reactivate; first play already
has playing=false). Existing fields inspected here establish no monotonic
cancellation token. Callback policy needs caller ownership/admission semantics
before it can be ratified. These branch witnesses do not establish a cancellation policy; a host
harness merely mirroring them would not validate one.
The listed return addresses are instruction boundaries, not qualified patch
spans. CPU/flags/FP/MXCSR/LastError, interior entries, rollback/unwind, record/media
lifetime, shutdown and destination recovery remain separate obligations.

Reproduction: `python3 /tmp/verify_x3_media_cancellation_continuation.py`:
PASS, six gap-free ranges, 410 instructions / 1,091 bytes, 29 byte anchors and
53 direct branch-boundary checks; pseudo-decodes are rejected. Independent
review reproduced these counts and cleared stack/routing evidence, with the
receiver-versus-request clarification above. The compact
[check record](../../verification/results/media-cancellation-continuation-2026-09-20.json)
contains derived findings and provenance. Full local note, verifier, JSON and
raw rows use `/tmp/x3-media-cancellation-continuation*` (verifier prefix
`/tmp/verify_x3_media_cancellation_continuation.py`). No runtime cancellation,
backend dispatch, production hook or safe owner lifetime is established.


## SC_CLOSE has distinct shutdown and synchronous script paths (2026-09-20)

WndProc `0x4d3782` calls `0x401d60`. Its **fallback shutdown cannot normally
return to the suspended caller**: `0x401dc5→0x401dd0` can destroy media through
`0x401eab→0x497190` and `0x401fcd→0x4980d0`, then converges at
`0x40265e→0x50f1d3→0x50f0f1→0x50ef8d→0x50ef9b`, whose PE import is
`KERNEL32.dll!ExitProcess` (IAT0x5321d8). Shutdown has no RET or indirect jump;
all its direct jump targets remain inside its body. The recursive-shutdown guard
at `0x401e17` calls the same exit wrapper. SC_CLOSE supplies error code0, which
skips the special0x24 formatting path at `0x402002`.

The wrapper's apparent RET is not a returning shutdown contract: `0x50f1d3`
passes `(status,0,0)` to `0x50f0f1`; only a nonzero third argument selects its
returning epilogue at `0x50f1cd`. Zero reaches ExitProcess. CRT exit-list calls
at `0x50f16f` and fixed-table calls at `0x50efbf`, plus optional resolved
`mscoree!CorExitProcess` at `0x50ef8a`, occur first. Their ordinary returns still
lead to process exit. Exceptional/nonlocal behavior is unexamined, **not an
identified transfer or a new blanket blocker**. Shutdown's C++ FuncInfo0x5708ac
has five unwind states and zero try blocks; no local returning catch arm is
established. The normal-return conclusion conditions on ordinary intervening
returns and does not assert that every possible execution terminates.

The **handled-script arm is different**. The name at0x554f3c is `NotifyLeave`.
Existence lookup `0x49f730` receives EAX=DWORD[owner+0x4dc], ECX=`*0x6085e4`,
EDI=name, where owner=`*0x57fc60`. Call `0x401d9a→0x49f570` passes six
caller-cleaned dwords: `(DWORD[owner+0x418],0,DWORD[owner+0x4dc],name,1,0)`;
`0x401d9f` removes0x18 bytes. Its successful preparation path calls
`0x49f61d→0x49f430`, which links the task, then **directly calls interpreter
0x4a26a0 at0x49f4ac**, with ECX=script owner and one stack task argument.
This is synchronous execution before the close handler checks its result,
not merely completion-handler queue/list mutation.

Returning without shutdown requires lookup success, invocation AL!=0,
owner WORD+0x490==1, and a true conversion of typed owner+0x498 through
`0x4a8970`. The final JNZ `0x401dc1→0x401dca` selects POP EDI / RET at
`0x401dcb`, then WndProc resumes at `0x4d3787`. Failed predicates take fallback.
The close handler does not itself reset +0x490 before invoking the script, so
this predicate alone does not prove a fresh completion during this invocation.

Interpreter entry fetches the selected task's bytecode and dispatches through
remap0x4a4688/table0x4a4490 at **0x4a2707**. This is the unresolved selected-
script-body boundary; earlier allocation/error-helper closure is also unexamined.
Prior evidence exposes media dispatcher0x4997c0 and deletion
`0x499865→0x4986f0→0x4986b0→0x4984d0`, but **the selected NotifyLeave body has
not been shown to reach that deletion path**. Synchronous VM entry therefore
prevents a lifetime-preserving conclusion for the handled arm, while neither
actual media deletion nor a returning UAF has been observed. Fallback's eventual
process exit and the four completion handlers' non-VM closure cannot be applied
to this separate arm. This is not a whole-WndProc closure or a qualified close-
deferral policy; WM_ACTIVATE stop-all and destination recovery remain separate.

Independent review reproduced both local witnesses: shutdown
`python3 /tmp/verify_x3_media_close_return_contract.py` passes eight ranges,
782 instructions / 2,709 bytes, 20 anchors and 94 branch-boundary checks;
handled-arm witness embedded in `/tmp/x3-media-close-script-arm.md` passes
eight ranges, 390 instructions / 1,077 bytes and nine anchors. Both reject
pseudo-decodes and compare original EXE bytes. The
[compact result](../../verification/results/media-close-paths-2026-09-20.json)
records the distinct conclusions and local evidence paths/hashes. Detailed
shutdown note/check/raw rows use
`/tmp/x3-media-close-return-contract*`; raw rows remain local and untracked.
No game, Wine or native runtime execution, production change, safe owner
lifetime, or actual COM delivery of SC_CLOSE is established by this checkpoint.


## Window notification and input activation boundaries (2026-09-20)

Two additional WndProc paths now have bounded, independently reviewed engine
closures. Their direct work does not call the VM, media manager/destructors,
destination retirement/recovery or D3D Reset under the valid-object/registration
premises below. This narrows those paths; it is **not whole-WndProc safety** or
proof that external APIs cannot reenter it.

For `MM_MCINOTIFY` (0x3b9), `0x4d3729/31` requires lParam to match0x608b50 or
0x608b54; `0x4d3739` requires wParam1. With nonnull object/callback,
`0x4d374d` calls `[*0x606f40+0x10]` without arguments. Constructor0x4de060
allocates0x14 bytes and zeroes+0x10 at0x4de0c0; initializer0x499bc0 installs
**0x49c120 at0x499cdb**. The global is published at0x4de0c5 and again0x4033eb;
teardown frees its object at0x4de426 and clears the global at0x4de43f. This is
a separate object from the0x40-byte media record and completion registry.

Adapter0x49c120 uses separate owner`*0x6085dc`, index+0x10c and context+0x110.
For nonzero index/context, it conditionally calls `(context,1)` through
`*0x6085e4+0x34+index*0x18` at0x49c15e, then clears owner+0x114/+0x10c/+0x110
at0x49c169/173/17d. **Dispatch precedes clear.** Its plain RET requires no
arguments; the inner completion is caller-cleaned. This exact adapter rejoins
the already reviewed four completion targets, whose normal closure updates
state, wakes tasks and performs typed cleanup without synchronous VM/media/COM
or destination work. It is not a no-op. Index comparison is signed<32, so a
nonnegative valid index remains a premise. Known registration and live valid
owners/contexts are required; no arbitrary-alias writer or shutdown pin is proved.
The only encoded .text references to0x608b50/54 are the two comparisons: runtime
initialization through aliases/indexed bases is unresolved, so no notification-
unreachability claim follows.

Input helper **0x4d4950** takes one caller-cleaned DWORD and preserves
ESI/EDI/EBP; no meaningful uniform EAX result is established. WM_ACTIVATE calls
it at0x4d36a5 with `(LOWORD(wParam)==0)`; WM_ACTIVATEAPP calls at0x4d36fe with
`(wParam==0)`. **Neither branch inside0x4d4950 writes active0x608adc; the
surrounding WM_ACTIVATE handler writes that global.** The inactive WM_ACTIVATE
arm's media stop-all at0x4d36bd remains outside the input closure.

With a live input owner, the helper clears512 bytes at+0x210..+0x40f and several
input fields. Nonzero argument Unacquires up to three DirectInput devices.
Zero argument Acquires devices, can drain input data, rebuild force-feedback
effects through0x4d5290→0x4d4f20, or recreate a controller through0x4d5540.
Interface identity is supported by DirectInput8Create/IID_IDirectInput8A and
CreateDevice output provenance, checked against installed public headers.
EnumEffects callback0x4d4ef0 (RET8; registrations0x4d4fc5/0x4d51c4) copies a
16-byte GUID into context; EnumObjects callback0x4d5470 (RET8; registration
0x4d55fe) compares GUIDs and updates input flags. These concrete callbacks do
not call the VM or media paths. Device/effect Releases and recreation are real
synchronous COM work. Recreated controller publication precedes recursive
0x4d4950(0) at0x4d56b6; provider return/reentry is not guaranteed by this ordering.

Controller recreation can call **0x4d45b0's public WMI COM query** at0x4d55b6:
CoInitialize, CoCreateInstance for WbemLocator, ConnectServer,
CreateInstanceEnum, enumerator Next, object Get, proxy setup and Releases.
It examines `Win32_PNPEntity.DeviceID` for controller product IDs. Each Next at
0x4d46dd/0x4d4814 receives timeout0x2710 (**10,000 ms**) and count0x14;
multiple batches are possible. This is a per-call argument, **not a total time
bound or a no-message-pumping guarantee**. CRT search/parse helpers0x511b9c/
0x511c68 remain unexpanded. No callback, exception, API dispatch or media deletion
through those unexpanded boundaries has been observed or proved. Public COM
reentry remains an envelope obligation, not proof of actual nested dispatch.

Reused independent verification: notification verifier
`python3 /tmp/verify_x3_media_window_notify_closure.py` passes seven ranges,
232 instructions / 835 bytes / 17 anchors / 22 branch-boundary checks. Input
analysis passes thirteen ranges, 1,321 instructions / 4,169 bytes / 140 internal
branch-boundary checks; that count has **no separate fixed-anchor count**.
Both compare original EXE bytes and reject pseudo-decodes. Input caller/GUID
snippets were checked separately and are not included in those thirteen ranges.
The [compact record](../../verification/results/media-window-boundaries-2026-09-20.json)
records separate counts, local evidence paths/hashes and scope. Local detail is
`/tmp/x3-media-window-notify-closure.md` and
`/tmp/x3-media-input-activation-closure.md`; raw evidence stays untracked.
No production interception, owner lifetime/thread model, Reset safety, natural
message delivery or native runtime behavior is qualified by this checkpoint.


## Engine consumers of a prospective pending wrapper (2026-09-20)

**A constructor-only pending pointer substitution is insufficient.** The media
object is not opaque: after `0x4981d3→0x4cf460(id,flags)` returns, record+0x24
receives EAX at0x4981dd, then media+0x8c is read at0x498248 and copied into
record flags **before** publication0x498265/268. Original continuations also
write media+0x94 directly at0x498420 (loop),0x498d69 (explicit play) and0x498f6e
(speech). A compatible engine shell or explicit interception of these accesses
is required; arbitrary sidecar layout is invalid. This is consumer evidence,
not approval of a worker implementation or shell representation.

Beyond the established constructor/seek/Run/pump/position/stop/destructor/copy
seams, the bounded investigation found:

| Consumer | Concrete requirement |
| --- | --- |
| Rate setter0x498650, EDX=ID and one caller-cleaned integer | VM caller0x4998d6 reaches an unguarded media+0x70 position-interface dereference and put_Rate(+0x38) at0x498695 unless media flags0x20 reject it. No playing/readiness test exists. Owned objects need routing before this COM access and an explicit rate-result contract. |
| Inline stop-all0x4982b0 | Pauses through media+4/+0x74 at0x4982f0, then clears playing and delivers/clears completion. Hooking ordinary stop0x4d1810 alone misses it. NULL COM fields can skip Pause but cannot convey worker cancellation or stopped intent. |
| Volume0x4d0570, ECX=record/EAX=volume | Flags8 returns0 before COM; other routes use media+0x6c or+0x44. Five E8 callers include constructor calls0x498280/299, list updates0x4985fc/62f and script0x4999d2. A valid flags8 shell can retain this original rejection; shared audio semantics remain separate. |
| Construction/existence0x498730/0x4987a0 | If requested and eligible, NULL factory result reports0 at0x49876c, nonnull reports1 at0x498799, and existing record reports1 at0x498806. All require nonzero EDI request flag, live enabled registry, signed index bound and nonnull slot. They report construction/existence success, **not first-ready-frame proof**; pending admission and late failure need a separate semantic contract. |
| MOVI save/restore0x4988d0/0x498ad0 | Save copies ten record metadata dwords (+10,+14,+18,+1c,+20,+2c,+30,+34,+38,+3c), without record+0x24 (the media pointer), COM pointer or position getter. Restore clears saved playing bit at0x498b99, may reconstruct at0x498bae and reapplies destination metadata. New pending/rate/failure persistence is not thereby defined. |

The separate position helper has only one encoded E8 caller, **0x4983ed**.
Manager0x498370 passes its stack local at `[ESP+0x10]`, ignores helper EAX, then
jumps to next-record traversal. No manager instruction reads that output as data;
POP ECX at0x4984c2 finally discards the slot. This is an incidental synchronous
COM dependency to intercept for owned objects, not a found external position
comparison or callback input.

The **visible video clock obligation** is pump's positive-end check: position
query0x4d16f0, truncated seconds×1000, compare0x4d1720 and strict `>` at0x4d1726
before copy. Result2 drives loop seek or completion; physical EOF is separate.
A clock anchored on first readiness is not ruled out by the discarded getter,
but endpoint/pause/rate/seek and completion timing still need ratification. No
claim of equivalence to backend cold-start behavior follows from static bytes.

Shared routes remain material: seek callers0x49840a/0x498d54/**0x498f55** and
Run callers0x498d71/**0x498f76** include speech. Pump also E9-tail-transfers
at0x4d17cc to audio helper0x4d0700 with EAX=media; flags8 bypasses this audio end
processing. Function0x4d0680 is the manager-root allocator, not another per-media
consumer. Factory/helper interception must recognize genuinely owned objects;
flag8 alone includes other IDs and does not establish ownership. Inspected
external record-root users use ID/flags/destination metadata and existing helper
calls; no additional external surface/dimensions or clip-position reader was
found in that scope. Arbitrary aliases and computed entries remain unclosed.

Independent review reproduced
`python3 /tmp/verify_x3_media_async_wrapper_consumers.py`: **18 gap-free ranges,
680 instructions / 1,853 bytes / 24 anchors / 100 branch-boundary checks**,
16 direct E8 caller sets and the E9 audio tail. It checks original bytes, rejects
pseudo-decodes, and validates the discarded position local and serialized fields.
The [compact record](../../verification/results/media-async-wrapper-consumers-2026-09-20.json)
binds the local note/verifier/result paths and hashes. Full details/raw rows use
`/tmp/x3-media-async-wrapper-consumers*`; raw bytes remain local. This establishes
additional consumer seams, not asynchronous admission policy, worker/module or
engine lifetime, clock approval, complete entry closure or production hook spans.
No game, Wine, build or native runtime execution was performed.

## Early worker scheduling candidate on ordinary startup (2026-09-20)

The primary game factory call supplies a **pre-device, pre-main-loop scheduling
candidate**, conditional on that invocation reaching Direct3DCreate9 and returning
success. An arbitrary first successful proxy export call is insufficient. This
checkpoint qualifies static ordering only; no worker startup or hook was added.

The normal chain is EXE entry0x512ead → CRT0x512ccd → WinMain0x4d43b0
(call0x512e3f) → SEH wrapper0x402710 (call0x4d44d2) → startup0x402780
(call0x40273c). Its **factory-setup call0x402edc→0x4d8470** dominates both
window/device bring-up0x40332a→0x4f95d0 and main-loop call0x40373a→0x403840
in the bounded normal intraprocedural CFG, including the nine-entry phase
switch0x403818. The setup skips the factory if renderer root `*0x608b3c` is
already nonnull. When reached, call0x4d848f→thunk0x4faedc→IAT0x532314 invokes
the sole static `d3d9.dll!Direct3DCreate9` import with SDK0x20; its successful
export return precedes this chain's window/device creation and main loop.
Configuration/archive work already occurs before setup: no before-all-loading
or timing bound follows.

Bring-up continues0x4f960a→0x4dac90, CreateWindowExA at0x4dae0d, then
0x4db058→0x4d8f10 and factory-vtable+0x40 **CreateDevice at0x4d9642**.
The second encoded factory caller, **0x4dae76**, occurs after HWND creation
when the renderer's factory DWORD is null; a failed primary factory can permit
this later attempt. It is outside the proposed admission. Ordinary lost-device
handling0x4dac7d→0x4da960 instead invokes the existing device's vtable+0x40
**Reset at0x4daa0b**, with no direct factory/setup call in that inspected helper.
Teardown0x4f986a→0x4dbd50 can release/free the renderer and clear0x608b3c at
0x4dbfb0. Service startup must therefore be process-one-shot, not rearmed by
renderer nullness, Reset or later factory calls; arbitrary aliases/recreation
entries remain unclosed.

The proposed narrow guard captures actual **export-entry ESP=E**, before later
C++ frames: `[E]=0x4d8494`, `[E+4]=0x20`, `[E+0x14]=0x402ee1`. At setup entry
S, PUSH EBX/ESI/EDI at0x4d8477/78/79 consumes12 bytes; the allocator argument
is cleaned at0x4d8485; PUSH SDK plus CALL consumes8 more. Thus E=S−20, and
the JMP import thunk adds no frame. Saved EDI/ESI/EBX occupy E+8/+0xc/+0x10;
they are not guard keys. Preserve the one-DWORD stdcall ABI and four-byte
incoming-stack contract. Ancestor inspection requires bounded safe reading of
the actual 24-byte entry span, not a guessed current ESP or a 16-byte alignment
assumption. Exact image/site identity, successful result, non-reentered
one-shot admission and a startup window closed by the first device creation
attempt are **candidate obligations**, not an implemented/qualified guard.
CPU/LastError, safe-read, unwind and rollback behavior still require validation.

The identified call is on ordinary CRT/WinMain startup, with backend loading
returned before the proposed successful-export-return scheduling point. This
supports that narrow context rather than arbitrary export calls from DLL
initializers. The EXE has no TLS/delay-import directory; only the import thunk
references its D3D9 IAT slot in the scanned text literals. Encoded E8/E9/literal
entry scans are not computed-name, ordinal, indirect-entry, exceptional-control
or other-module loader-lock closure. The export's C++ signature and DllMain
comment alone cannot certify loader context. Schedule only: no DD/COM/HWND
creation on the export thread or DllMain, and no engine wait. Unknown/late callers,
failed startup and recovery must not start cold DD during flight. Worker/module
retention and shutdown remain separate obligations.

**Early request is not ready-before-flight proof.** Worker scheduling can lag
device creation or flight; no driver-contention or timing result follows from
these bytes. Next qualification needs guard accept/reject/one-shot evidence,
request/ready/device/actual-flight-phase timestamps and defined pending/fallback
behavior without an engine wait. No native Windows runtime behavior is claimed.

Independent review reproduced `python3 /tmp/verify_x3_media_worker_startup_boundary.py`:
**16 ranges, 2,231 instructions / 7,917 bytes / 29 anchors / 264 internal branch
boundaries, 12 encoded E8 caller sets**. It verifies original PE/import identity,
contiguous non-pseudo decodes, byte/boundary checks and scoped normal-CFG
ordering. The [compact record](../../verification/results/media-worker-startup-boundary-2026-09-20.json)
binds the corrected local note, verifier and result hashes. Full details are
`/tmp/x3-media-worker-startup-boundary.md`; raw disassembly remains local and
untracked. No production changes, game/Wine execution or build was performed.

## Owned rate and stop-all dispatch candidates (2026-09-20)

Two per-record spans can route a proven owned ID2 shell **before original COM
access**, while retaining original lookup/traversal and completion handling.
These are reviewed instruction/ABI candidates, not qualified production hooks
or a complete engine ownership contract. Identity and lifetime generation are
required; ID2 or flags8 alone cannot distinguish owned from unowned objects.

| Candidate | Incoming state and continuations |
| --- | --- |
| Rate0x498670, length10, end0x49867a | EAX=matched record, ECX=next link, EDX=requested ID, ESP=S=function-entry stack; signed input at[S+4]. Displaces MOV EAX,[record+24] (3 bytes) and TEST BYTE[media+8c],20 (7). Proven unowned replays both and resumes0x49867a with TEST flags intact. Owned local result can resume0x498697 with EAX=0 for success or negative for failure; existing tail returns Boolean EAX with plain RET. Passing Boolean0/1 to that tail would incorrectly report both as success. |
| Stop-all0x4982db, length6, end0x4982e1 | ESI=current playing record, EBP=cached next, EBX=0; ESP=T=S−16 after saved EBX/EBP/ESI/EDI. Displaces MOV EDI,[ESI+24] (3) and CMP [EDI+4],EBX (3). Proven unowned replays both and resumes0x4982e1 with CMP flags intact. Proven owned commits local cancellation/stopped intent, then resumes0x498322 at original T with live current/next records and preserved registers. |

Both displaced spans have no relative operands or scanned interior entry.
The rate span is entered by JZ0x498663; stop-all falls through from0x4982d9.
A five-byte rel32 detour requires whole-instruction spans10/6 respectively.
Entry detours at0x498650/0x4982b0 each have a five-byte first instruction but
lack the matched-record identity; replacing whole functions would duplicate
lookup or mixed-list traversal/callback behavior. The two-byte COM CALLs
0x498695/0x4982f0 are too late: rate already dereferenced media+0x70, while
null Pause fields skip the stop call entirely. No actual patch was installed.

Rate's sole encoded E8 caller0x4998d6 pushes the integer from script argument
+6 with ID in EDX; it pushes the returned Boolean at0x4998db and clears both
arguments at0x4998e3. This is a synchronous script result. Original rate rejects
media flags0x20, then FILDs the integer, multiplies float bits0x3727c5ac, and
calls position-interface put_Rate(+0x38) at0x498695. It has no callback or
post-COM record read. Owned rate follows the architecture's **full positive
signed domain1..INT_MAX**, with a real synchronous local clock transaction;
nonpositive input fails without mutation. Stale/unavailable ownership or failed
transaction are separate failures, not a new positive-rate cap or worker HRESULT.

Stop-all caches next at0x4982d6, then conditionally Pauses through media+0x74
at0x4982f0; flags8 skips its secondary audio transport. Owned dispatch must
invalidate its presentation/decode epoch without waiting for worker retirement
or relying on queue capacity to prevent stale presentation. Resume0x498322:
original clears playing at0x498328, conditionally calls completion0x49834e
with caller-cleaned `(context,status1)`, **then** clears callback index/context
at0x498353/356 and dereferences cached next at0x498359. The injected operation
must not notify, pump, unlink or destroy either record. Only playing records
are visited; admitted/preparing play must expose playing intent as designed.
Callback eligibility retains the original signed index<32 test without a lower
bound check; valid metadata remains a premise, and equal callback keys do not
identify one logical request.

Six stop-all direct callers are0x403878,0x40455c,0x404ced,0x407064,0x497bb6
and0x4d36bd. The last is WM_ACTIVATE inactive handling, after active-state clear.
These contexts do not establish all-thread ownership. Reviewed known completion
handlers update state/wakeup queues rather than synchronously executing VM/media
retirement, but **mixed-list stop-all still calls unowned COM and keeps current
and next record pointers across COM/callbacks**. Owned generations do not pin
those engine records. Provider reentry, handled-script/shutdown, destination
recovery, unknown aliases and exceptional/thread behavior remain separate
lifetime barriers. These two seams do not close the full integration envelope.

Only proven unowned takes original COM replay. Owned-but-stale/unsupported or
unavailable classification must not silently become unowned. A valid owned
rate rejection can use the failure tail; stop needs a safe local cancel before
continuing. Unknown live record/foreign-thread cases have no newly proved
fallback here. Failed hook qualification must prevent new owned admission;
unhooking requires no live owned shells or in-flight dispatch. Classification
and local updates must be bounded without callbacks, COM, message pumping,
recovery allocation or worker waits, and must not pass engine pointers to worker.

On **both owned bypass and unowned replay**, injected work preserves caller
x87 stack/control/status and MXCSR/XMM state, nonvolatile registers, four-byte
incoming-stack assumptions and LastError. Unowned replay restores all incoming
GPR/flags before displaced instructions; owned tails preserve their described
stack/register contract. No unqualified exception may escape into engine code.
Exact live bytes, patch ownership, quiescent writes/rollback, trampoline/module
lifetime, CPU/LastError tests, local transaction/cancel correctness and performance
remain implementation obligations, not results of this disassembly checkpoint.

Independent byte/ABI review and local rerun of
`python3 /tmp/verify_x3_media_owned_consumer_seams.py` pass **10 ranges,
228 instructions / 644 bytes / 37 anchors / 25 internal branch boundaries**.
Encoded E8/E9/EB/Jcc/LOOP scans into both full functions find26 actual references
(7 external E8 calls,19 internal branches), zero external interior entries and
zero whole-file absolute entry/interior DWORD literals. Three raw operand-byte
coincidences are rejected by containing-instruction boundaries. Computed entries,
other modules and arbitrary aliases are not closed by this census.
The [compact record](../../verification/results/media-owned-consumer-seams-2026-09-20.json)
binds corrected local note/verifier/result hashes and counts. Details and raw
rows remain under `/tmp/x3-media-owned-consumer-seams*`; no raw bytes are tracked.
No game/Wine execution, build, source edit or installation was performed.

## Source ID versus playback-instance identity (2026-09-20)

**Numeric ID selects both source and normal manager-record lookup; it is not
an independent scene-object instance handle.** In the inspected ordinary
serialized routes, same-ID requests reuse the first matching record, overwrite
its destination binding when requested and replace its playback operation.
Two visible animations may instead use different IDs or sample one shared
texture. Static VM argument loads do not identify which applies to the user's
two objects; this checkpoint does not change original replacement semantics.

Allocator0x498140 takes EAX=flags override and caller-cleaned stack ID. It
allocates a fresh0x40-byte record without deduplication, calls constructor0x4cf460
at0x4981d3, and passes the same ID used by constructor media+0 at0x4cf522 and
numeric `%05d.dat` formatting0x4cf6a2/a3/b5. Record+24 receives media0x4981dd;
publication0x498265/268 **precedes ID assignment at0x4982a3**. Its five encoded
callers are guarded by ordinary ID searches: create/existence0x49873a through
0x4987a0; MOVI restore0x498bae; play0x498cd8; separate speech0x498ef8; and
emitter/track0x4f6610. These support intended first-match reuse, not unconditional
uniqueness: constructor COM/recovery and publication ordering leave reentry
unclosed, and allocator has no unique-key reservation or late duplicate check.
No actual duplicate creation is observed. A duplicate would not acquire a new
addressable instance key through these ID-only APIs.

| Binding/play path | Established identity consequence |
| --- | --- |
| Emitter helper0x4f65f0 | ESI=ID, first stack argument=slot/kind; finds by record+10 only, sets bit4 at0x4f6639, overwrites the single record+30 slot at0x4f6641, then calls play0x498c90 at0x4f6668. There is no `(ID,slot)` lookup. |
| Script bind0x498550 | EDX=ID and caller-cleaned slot; overwrites record+30 at0x498578 and sets bit4. Sole caller0x4998f9 loads VM ID/slot. Unbind0x498590 clears only the same ID's matching current slot/bit4. |
| Manager0x498370 | For playing records with valid bit4/slot, resolves one table entry to record+28 at0x4983d1, then pumps once at0x4983d9. No per-record destination fanout was found. Multiple materials sampling that output remain possible and untraced. |
| Play0x498c90 | Ten caller-cleaned DWORDs: callback index/context, ID, six start/end components, option. Finds by ID only; seek0x498d54 and Run0x498d71 affect that record. Success replaces one start/end tuple and eligible old/new callback operation; no destination+28/+30 assignment occurs in the bounded successful body. |

Two same-ID calls therefore do not request independent playheads or retain two
destination slots. Distinct IDs can have distinct records/media/clock/callbacks,
although binding them to one destination can still overwrite that resource.
A worker session keyed only by source ID cannot create independently addressable
engine instances. Owned dispatch must retain record/shell identity and lifetime
generation; generations alone do not pin engine memory.

Relevant encoded callers: create/existence0x4987a0 has VM0x4997f3; play0x498c90
has VM0x49981f/0x499982 and helper0x4f6668. Helper0x4f65f0 has selector0x45c607
(Videos+14 ID, slot0x5a),0x460424 (Videos+14 and supplied slot), and descriptor
route0x4f6836 (table-entry+28 ID, signed WORD[outer descriptor+70] slot).
Descriptor+18 receives the source ID at0x4f6848, not a newly allocated instance.
Separate speech VM0x499849→0x498e30 shares the namespace but is not evidence
of another animated destination. Dynamic table/script values remain unknown.

The zero-override map includes other video-capable IDs1/3/800/10001 as well as2;
that does not establish the second animated source. **ID2-only ownership is an
initial known-source scope, not coverage of every legitimate second-record ID.**
Extension needs a validated source ID/path/stream-mode contract or qualified
whitelist, not all flags8 or speech/audio. If two objects share ID/output, retain
that original behavior unless separately asked to introduce independent instances.

Two precision corrections accompany this result. Existing ledger evidence has
failed ID2 attempts for kinds0x5a and0x520, including selector ID2; it contradicts
the older selector-wide audio-only generalization, now corrected in
[media-cue-playback](media-cue-playback.md). Those failures do not prove concurrent
successful records. Also, allocator's early OR4 at0x4981fe/210 is overwritten
by media flags copied at0x498254; it is not an unconditional final bit4 guarantee.
The explicit binding helpers above set bit4 afterward.

Minimum missing evidence is a finite identity burst during the already-required
two-animation observation: timestamp/frame/TID, caller, source ID/resolved path/
effective flags, found/created record and media with allocation generation,
play operation/bounds, old/new slot and destination identity/generation, and
retirement/stop. Correlate concurrently live successful records, not failed
attempts or reused pointers. If both resolve one destination, bounded correlation
of those two objects' materials to that texture is needed to establish sharing.
No new generic flight or production trace implementation is introduced here.

Independent review reproduced `python3 /tmp/verify_x3_media_instance_identity.py`:
**17 ranges, 748 instructions / 2,048 bytes / 43 anchors / 95 internal branch
boundaries**, nine encoded E8 caller sets, no scanned E9 entries or whole-file
literal references to those entries. The [compact record](../../verification/results/media-instance-identity-2026-09-20.json)
binds local note/verifier/result hashes. Detail/raw evidence remains local under
`/tmp/x3-media-instance-identity*`; no raw bytes are tracked. No global alias,
thread/reentry uniqueness, actual concurrent-source census or hook qualification
is claimed. No game/Wine/build/install/production changes were made.

## Owned copy: destination holds and retirement continuations (2026-09-20)

The texture-slot usage count is a recovery policy, not a general destination
lifetime pin. Helpers0x4f5180/0x4f51c0 take AX=signed slot, no stack arguments,
and increment/decrement slot+0xc at0x4f51a5/0x4f51e5. Recovery
0x406d8c→0x4f5200 first unbinds textures through0x4b9f70 (SetTexture(NULL), then
count decrement0x4b9fbe) and calls0x4b9660; only subsequently does it evict
nonnull slots whose+0xc is zero and whose flags lack0x20000000. The indirect
renderer-state call at0x4b966d and exceptional helper behavior remain unclosed.

Forced clear0x4f4b30 (raw E8 caller0x48b054), when the renderer factory exists,
releases/clears every nonnull slot+8 binding through0x4f4b71 regardless of+0xc,
then zeros+0xc at0x4f4b86.
Wrapper destruction remains conditional on its separate+0x60 count.
Table teardown0x4f4990 (raw E8 caller0x47112a) frees the array at0x4f4a26 and
clears0x6069ac at0x4f4a42/54 without a slot-hold test. Balancing an old hold
after clear/recreation could debit a new incarnation. In0x4f38d0, positive
wrapper+0x60 skips destruction, but **both nonnull paths clear the binding**
at0x4f3943. Nonpositive count invokes0x4dcc70→surface Release0x4dccf7 and
texture Release0x4dcd13, then wrapper free0x4f3926. Storage retention does not
preserve binding identity.

The prospective CPU-frame copy must resolve the current valid table/slot anew,
not trust record+0x28: the original invalid-index branch leaves that cached field
unchanged. Under a separately established live-storage/exclusion contract, take
a short-lived reference to the actual surface, then retain no engine wrapper or
record across external calls. [AddRef](https://learn.microsoft.com/en-us/windows/win32/api/unknwn/nf-unknwn-iunknown-addref)
retains the interface, not engine storage or request identity; it cannot repair
acquisition from an already freed pointer. Original copy0x4d0c40 is unsuitable
for direct reuse: it invokes memory recovery after source Lock0x4d0ca6 or
failed destination LockRect0x4d0d4b, retrying cached pointers. An owned immutable
CPU upload should not invoke those recovery retries or wait for decoding.

After each external boundary, including Unlock/Release, validate independently
live operation/binding/device generations before acknowledging a frame or
accessing engine storage. Successful locks require matching unlocks. This alone
does **not** resolve nested Reset: a retained default-pool surface can change
Reset success before post-call validation. Existing Reset is0x4daa0b. The
[public Reset contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-reset)
requires relevant references released beforehand and forbids D3D calls during
its window-message dispatch. Safe acquisition and copy-versus-Reset exclusion
or a compatible separately qualified deferral remain concrete integration
premises; a recursive mutex is not same-thread exclusion.

A precise local continuation is available. Manager0x498370 enters at ESP=S;
after local and saved-register pushes ESP=B=S−20, EDI=current and EBP=cached
next. Its5-byte pump CALL0x4983d9 enters a replacement with ESP=P=B−4 and
[P]=0x4983de. If an independently retained traversal token was invalidated,
remove that call return and reach **0x4984be with ESP=B**: POP EDI/ESI/EBP/EBX/
ECX; RET, without engine dereferences. Any ordinary pump result eventually
reads current or saved-next (0x4984b5), so synthetic success is insufficient.
The token must cover retirement of any cached list member, including unowned
records; this exit does not qualify every outer caller after world teardown.

Retirement observers must invalidate before linked destructor0x4984d0 dispatches
callback0x498501/COM0x49851b and frees0x498530; before root shutdown0x4980d0
frees0x498114; and before subsystem clear0x497190 cleans callback ownership at
0x4971aa→0x49ea80. The original destructor has not itself unlinked/freed the
record when invoking callback/COM; that ordering supplies no guarantee against
nested retirement. Deferring shell destruction does not pin its engine record.

The four previously reviewed completion bodies still only update result/wakeup
state on ordinary return. Keep eligible original dispatch on the engine thread,
with no worker-queued raw callback context. If adapter reentry permits a new
operation during dispatch, post-dispatch clearing must target the same live
operation; equal callback keys alone do not identify one request. Manager key
clears follow callbacks at0x498466/0x4984a8. In occupied stop-all, ESP=T=S−16,
ESI=current, EBP=cached next; **0x498362 with ESP=T** is its pointer-free epilogue.
It is not valid for partial-prologue stack states. Routing owned stop locally
still leaves unowned COM in mixed lists and its saved-next obligations.

Independent review cleared the local factual report and reproduced the witness:
**19 ranges, 939 instructions, 2,884 bytes, 56 anchors, 136 internal branch
boundaries; six E8 sets /12 sites**. The [compact result](../../verification/results/media-owned-copy-retirement-2026-09-20.json)
binds local note/verifier/result/raw hashes. Injection ABI/LastError/FP-state,
exception handling and complete entry/xref closure are not qualified by these
candidate continuations. Foreign-thread mutation, actual copy exclusion and
surface pool/caps remain unproved; no observed returning UAF or production
safety claim is made. No Wine/game/build/production changes occurred.
