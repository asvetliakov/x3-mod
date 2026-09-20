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
