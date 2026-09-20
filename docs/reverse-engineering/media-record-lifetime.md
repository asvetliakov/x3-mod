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
