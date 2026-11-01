# FL primary frame and game phase diagnostic mapping

2026-10-30, offline static study. The existing **10 frame and 33 game stamp
roles have FL semantic counterparts**. Forty candidate spans fit the existing
plain-copy/one-rel32 tail shape after HIGHLOW rebasing; three endpoints need
additional adapter work. **None is qualified for injection by this study.**
No production source, native/Wine execution, game launch, build or install was
performed. Other stamp families, object tracing, sampling and D3D diagnostics
are outside this bounded study.

Input `/mnt/c/X3FL-orig/X3FL.exe` has measured SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
All addresses below are preferred VAs at base `0x400000`; actual FL profiles
must resolve admitted module base plus RVA. The existing
`src/engine/fl/engine_profile_fl.h` recognition windows do not authorize hooks
or global reads. Reuse the shared startup owner and FL identity, rather than
loosening AP-only `object_trace::executable_verified()` in
`src/engine/ap/{frame_phases,game_phases}.cpp`.

Owning AP contracts: [frame partition](../frame-loop-phases.md) and
[game recorder](../selection-frame-phases.md). Current source tables, rather than
the historical 23-site text, define the 33 game roles. Preserve current request
gates, thresholds and default behavior: disabled-by-default diagnostics are
intentional. In particular, game phases require their explicit flag or draw
trace; frame phases also serve perf/debug. No renderer enhancement or GPU
capability is a prerequisite for these CPU diagnostics.

## Reproduction and evidence boundary

Run `python3 verification/results/fl-phase-diagnostics/produce.py`.
The [producer](../../../verification/results/fl-phase-diagnostics/produce.py) and
[derived mapping](../../../verification/results/fl-phase-diagnostics/mapping.json)
bind all 43 role IDs to the current AP tables, validate 80 semantic anchors,
decode 5,096 instructions across ten gap-free ranges totaling 18,510 bytes,
check each span's instruction boundaries/local direct edges, identify HIGHLOW
operands, and retain span hashes and image dword-reference counts. Figures are
measured offline. Raw listings stay untracked under `/tmp/fl-phase-diagnostics/`.
The JSON result is `STATIC_MAPPING_PASS`, explicitly `hook_qualification=false`.

The direct-edge checks are local to each decoded containing routine. They do
not close external direct edges, arbitrary computed entry, jump-table entries,
third-party patches, thread liveness or exceptions. Those are acceptance work,
not inferred from an empty local interior-edge set. Two unaligned image dword
matches name `0x4b4f4f` inside the proposed seek-end span, at `0x55a7f3` and
`0x583be2`; neither source has a HIGHLOW relocation. Their pointer semantics are
unproven, so the mapping preserves the hits for follow-up instead of claiming
no image references.

## Frame stamps: same partition, different native operands

FL frame routine `0x48b6d0..0x48be31` has 554 decoded instructions and one
plain RET. Existing camera/compositor studies already establish its role:
[compositor](compositor.md),
[camera](chase-camera.md). No stamp callback needs native
register inputs. Restore all incoming state before replay; operands remain
live to native instructions.

| ID / role | FL start; length | Replay / native meaning |
| --- | --- | --- |
| 0 prologue | `48b6ee`; 5 | CALL `5122f0`, rel32 +1 |
| 1 scene update | `48b7cd`; 5 | CALL `48ad90`, rel32 +1; subsequent node-update traversal |
| 2 BeginScene | `48b84c`; 5 | Load renderer-owner slot `5aaf94`, HIGHLOW +1; BeginScene through +0xa4 at `48b859` |
| 3 views | `48b907`; 5 | XOR EAX,EAX; ADD ESP,0x10, after qsort `48b902` |
| 4 overlays | `48bbad`; 10 | XOR EDI,EDI; TEST EBX,EBX; JLE `48bc54`, rel32 +6 |
| 5 text | `48bd0c`; 5 | Load engine slot `5aa100`, HIGHLOW +1 |
| 6 scene end | `48bd9e`; 5 | CALL `4e7170`, rel32 +1 |
| 7 per-view setup | `48ba2f`; 7 | MOV ECX,ESI; CALL `4a4c80`, rel32 +3 |
| 8 per-view submit begin | `48ba51`; 6 | Load scene `[ESI+0x1c]`, then first layer into EDI from scene+0x4c |
| 9 per-view submit end | `48ba9b`; 6 | Load engine slot into EDX, HIGHLOW +2; loop layers ended |

FL's overlays span must include the near JLE because the preceding two
instructions occupy four bytes. Its condition remains native. View setup
passes ECX=ESI instead of AP's pushed view; the generic preserving stamp still
fits. The existing compositor call at `48b929` lies outside every candidate.
Apply the loaded-base delta to HIGHLOW fields when preparing expected bytes;
tail copies retain the actual already-rebased absolute values unchanged.
Existing rel32 tail relocation must preserve each loaded call/branch target.

## Game stamps: helper extraction and native request translation

FL main loop `477ba0..478688` has 690 decoded instructions. Normal backedge
`478591` reaches `477e80`. Clock/pump/channels/pending-VM/services moved into
`4788b0..478908`, called by the loop at `477ebf`. That helper has 21 decoded
instructions: pending VM is inline, and media service is a tail jump. Keeping
the same logical phase IDs requires physical sites in both routines; an
address-order assumption is invalid. This is static order evidence, not a
runtime thread-ID or helper-caller census.

| ID / role | FL start; length | Native contract / replay |
| --- | --- | --- |
| 0 loop setup | `477e80`; 10 | AND `[EAX+0xf8]`,~0x4000; EAX=config |
| 1 clock | `4788b1`; 5 | CALL `4cdaa0`, rel32 +1 |
| 2 pump | `4788b6`; 5 | CALL `4ec620`, rel32 +1 |
| 3 channels | `4788bb`; 5 | CALL `4b6210`, rel32 +1 |
| 4 pending VM | `4788c0`; 6 | MOV ESI,[`5aa174`], HIGHLOW +2; inline loop calls interpreter `4bed90` |
| 5 services | `478902`; 6 | POP ESI; JMP media manager `4b42e0`, rel32 +2 |
| 6 input | `477ec4`; 7 | TEST `[EBX+0x4a0]`,1; EBX=game context |
| 7 simulation | `4782c5`; 5 | CALL `416f40`, rel32 +1 |
| 8 cockpits | `4782ca`; 5 | CALL `41e550`, rel32 +1 |
| 9 render | `4782cf`; 5 | CALL `48b6d0`, rel32 +1 |
| 10 post render | `4782d4`; 7 | CMP [`5a8fe4`],0, HIGHLOW +2 |
| 11 detail | `4782ee`; 5 | CALL `4b2f00`, rel32 +1 |
| 12 presentation | `4782f3`; 7 | CMP [`5a8fe4`],0, HIGHLOW +2; normal Present path via `478336 → 500c50` |
| 13 tail | `47833b`; 6 | Load context slot `6243cc`, HIGHLOW +2 |
| 14 exit | `4785a1`; 9 | PUSH 0; PUSH 1; PUSH string; HIGHLOW +5; all normal shutdown entries converge here |
| 15 delayed begin | `42c4f6`; 5 | CALL `4273a0`, rel32 +1; ECX=cockpit, `[ESP]=3`, `[ESP+4]=target` |
| 16 delayed end | `42c4fb`; **4** | MOV ECX,[ESP+0x3c]; too narrow for existing five-byte claim |
| 17 acquisition begin | `427541`; 5 | PUSH -1; PUSH ECX; PUSH 1; ESI=cockpit, EBX=requested target; AP's EBP=0 guard is invalid |
| 18 acquisition end | `427588`; 5 | MOV EAX,2; both sound branches converge; `42756e` branches here |
| 19 cold begin | `4b64ba`; 5 | CALL `511130`, rel32 +1; resource pointer in ECX, format in EDX |
| 20 cold end | `4b64bf`; 5 | Store EAX to `[ESI+0x10]`; MOV ECX,EDI; EAX=result |
| 21 Present begin | `4f39e3`; 5 | MOV ECX,[EAX]; CALL [ECX+0x44]; raw device is `[ESP]` |
| 22 Present end | `4f39e8`; 5 | CMP EAX,0x88760868; EAX=HRESULT after stdcall argument cleanup |
| 23 input body | `477ef9`; 7 | CMP `[EBX+0x4d8]`,1 |
| 24 input after | `47815d`; 7 | TEST `[EBX+0x4a0]`,4 |
| 25 publisher begin | `4273a0`; 5 | MOVSX EAX,WORD [ESP+4]; ECX=cockpit, `[ESP+4]=mode`, `[ESP+8]=target` |
| 26 publisher end | `427621`; 6 | POP EDI/ESI/EBX; RET **8**; mode-3/null/invalid-mode convergence, other modes have earlier returns |
| 27 playback begin | `4b595c`; 5 | CALL speech `4b4e10`, rel32 +1; ECX=slot, EDX=context, four script payload words at ESP |
| 28 playback end | `4b5961`; 5 | ADD ESP,0x10; MOV EAX,EBP; capture returned EAX before replay |
| 29 create begin | `4b4ee2`; 5 | CALL `4b46b0`, rel32 +1; ECX=slot, EDX=context; stack id,flags=0,notify=0 |
| 30 create end | `4b4ee7`; 5 | ADD ESP,0xc; **short JMP** `4b4ef0`; existing tail cannot relocate it |
| 31 seek begin | `4b4f43`; 5 | CALL `4fe5f0`, rel32 +1; ECX=record, EDX=start; no stack argument |
| 32 seek end | `4b4f48`; 8 | TEST EAX,EAX; **short JE** `4b4fc8`; MOV EBP,[ESP+0x18]; existing tail cannot relocate it |

The pump predicates are active DWORD `5aaf34` and flags through pointer
`5a8478`, with bit `0x4000`. They replace AP `608adc`/`606f3c`; retain checked
reads and read-failure invalidation. The blocking inactive GetMessage path is
already established in [pause/background](pause-background.md).

Publisher layout still uses cockpit target +0x1e0, tracking word +0x1e4 and
view +0x10. FL mode 3 stores target at `4275ed`, tracking at `4275f3`, and calls
the named synchronous event at `427619`. The three saved nonvolatile words
put its pre-pop endpoint ESP at **entry ESP−12**, versus AP−16; native RET8
then consumes both arguments. Delayed entry mode must be read from stack and
target from the next word, rather than AP EAX/stack-word0. Preserve the saved
register snapshot until those narrow reads complete; retain copied opaque
values only, never game ownership or a deferred cockpit borrow.

The MOV speech sites are confirmed by the existing
[media-cue mapping](media-cue.md#2-complete-immediate-caller-sets-and-sector-discriminator).
They are distinct from general SND playback (`4b7120 → 4b6810`). Normalize
FL playback witnesses to AP's six copied words as slot/context from ECX/EDX,
then four payload words from ESP. FL create's flags come from `[ESP+4]`, not
EAX; at the immediate return ESP equals begin ESP before caller cleanup,
whereas AP's later endpoint expected begin ESP+4. Seek reads record/start
from ECX/EDX, not AP EAX/stack; its endpoint also has unchanged ESP.

## Remaining hook work and minimal edition adapter

The delayed endpoint cannot simply widen to eight bytes: native branch
`42c4ef → 42c4ff` enters that span's interior. Moving it to shared join
`42c503` would observe additional non-delayed paths, and the existing
`Core::end()` treats unmatched ends as invalidation; that move needs explicit
filtering evidence. Likewise, moving create-end to `4b4eec` and stealing five
bytes crosses the native `4b4eea → 4b4ef0` entry. The producer asserts these
negative witnesses, plus the rejected early-exit span `4785a0..4785a5` whose
interior `4785a1` is reached by `477c65` and `477e74`.

Keep the current bounded recorder, CPU bridge, thread/epoch policy, emitters,
reporting and transaction machinery shared. The minimal FL boundary supplies:

1. Cold edition/profile selection independent of object trace; resolved
   10/33 site packs, rebased absolute operands, and the two pump globals.
2. Immutable edition-specific request/witness decoding, publisher RET8 and
   expected-ESP values. Report actual selected sites and failure statuses.
3. A reviewed replay solution for delayed-end and the two short-branch
   endpoints, using the existing patch owner. Do not silently drop them from
   an all-or-none game group or advertise partial game-phase parity.

Root should route the unresolved injection/ABI/lifetime contract to
`disassemble_deep` before implementation. This note establishes instruction
facts, not CPU/LastError preservation, callback liveness or unwind safety.
All registers, flags, exact native ESP, x87/MXCSR/XMM and LastError are live
unless independently proven otherwise. Present's raw device is an identity
token only: retain the existing locked bridge, generation matching and
Reset/final-Release invalidation, without reading an engine device registry
from stamp callbacks. The publisher/event, VM, message dispatch and COM/media
calls may reenter or escape normally paired endpoints. Preserve bounded
same-owner rejection, reporting suppression and epoch revocation; do not infer
reentrancy safety from synchronous native ordering.

Acceptance before activation: complete direct/jump-table/relocated-entry
checks for the selected FL spans, safe replay for all three special endpoints,
preferred/rebased profile mutation refusal, all-or-none installation and
rollback, CPU/LastError/stack preservation across the actual FL ABIs, owner and
reentry rejection, bounded metadata reads and failed endpoints, Reset/release
epoch handling, and exact Present/frame attribution. Reuse unaffected shared
timeline evidence; add only FL binding and changed replay cases. The queue
owner selects host/cross-compilation and synthetic CPU fixtures; a user game
run later establishes delivery and diagnostic overhead. No GPU integration
fixture is required to validate this CPU-only mapping, and no live FL or
native Windows behavior is established here.

## Bounded endpoint and publisher reconstruction

2026-10-30 follow-up, same original EXE and preferred address convention.
[Producer](../../../verification/results/fl-phase-diagnostics/qualify_replay.py)
and [derived evidence](../../../verification/results/fl-phase-diagnostics/replay.json):
`python3 verification/results/fl-phase-diagnostics/qualify_replay.py` reports
`BOUNDED_REPLAY_STATIC_PASS`, still `hook_qualification=false`. This adds
instruction/operand and replay-target evidence; it does not activate hooks.

### Smallest physical change: pair stamps around the existing call

All three problematic ends immediately follow a five-byte direct call that
already owns the corresponding begin stamp. Retain that physical claim and
emit both logical stamps from its replay. **Do not claim the native endpoint.**
The proposed FL pack has 40 distinct physical claims for the same 43 logical
roles (10 frame, 33 game). This count is inferred from the current tables and
the three merged pairs; implementation must distinguish physical ownership
from logical stamp IDs rather than claiming one site twice.

| Logical begin/end | Physical VA / RVA; length | Native callee | End continuation; end ESP relative to begin |
| --- | --- | --- | --- |
| 15/16 delayed | `42c4f6` / `2c4f6`; 5 | `4273a0`, RET8 | `42c4fb`; +8 |
| 29/30 create | `4b4ee2` / `b4ee2`; 5 | `4b46b0`, plain RET | `4b4ee7`; 0 |
| 31/32 seek | `4b4f43` / `b4f43`; 5 | `4fe5f0`, plain RET | `4b4f48`; 0 |

Concrete replay recipe, using the existing preserving stamp body twice:
dispatcher → preserving begin stamp → `CALL rel32 native` → preserving end
stamp → `JMP rel32 native continuation`. The ten-byte generated call fragment
at arena address A is `E8 u32(native_loaded−A−5); E9 u32(end_stamp−A−10)`.
Each preserving stamp finishes with the existing state-neutral indirect jump;
its continuation is respectively A or the loaded native continuation. The
original five-byte span is a CALL, with its one rel32 operand at +1 and no
HIGHLOW fields. Native addresses and continuations use admitted image base +
RVA. The generated examples in `replay.json` contain newly generated adapter
instructions, not copied native instruction dumps.

The physical claim remains an ordinary existing `engine_patch::Site` claim.
Its default relocated tail is retained for lifetime/rollback but **must not
also run on the paired path**, which would call native twice. This needs an
edition-local chain binding owned exclusively by this diagnostic group; it
does not need a generic short-branch relocator or a new patch writer. Prepare
both stamps and their continuation slots before publishing the chain and
before enabling the group. A failed preparation remains covered by the
existing inactive-group rollback and retained-arena contract.

The patch is a JMP, so the begin sees exactly native pre-CALL ESP. Replay CALL
pushes exactly one return word; delayed RET8 removes it and both arguments,
whereas create and seek return with their caller arguments unchanged. The end
sees native return EAX, flags and ESP before any cleanup or TEST. Create
returns at `4b46f6/4b4728`; seek returns at `4fe70f/4fe745`. Seek aligns its
internal stack to 64 bytes at `4fe5f3` and restores ESP from EBP before both
returns; the adapter must not infer an aligned incoming game stack.

This leaves `42c4fb`'s four-byte MOV and branch entry `42c4ff` intact, including
`42c4ef → 42c4ff`. It also leaves create's ADD ESP/short JMP and bypass join
`4b4eec → 4b4ef0` intact, and seek's TEST/short JE/MOV intact. No short branch
is copied to the arena. Additional untracked arrivals at native continuations
therefore execute native code without an unmatched delayed-end stamp.

Ordinary relocated CALLs already expose an arena return PC to their callee.
The paired publisher call likewise exposes A+5, not `42c4fb`; publisher witness
`caller` must record that actual opaque PC, or explicitly normalize only this
known generated return PC to the original continuation for reporting. Do not
silently assert that `[entry ESP]` still equals the original game PC. The
adapter neither changes a preexisting return slot nor introduces an extra
wrapper return word. A chained consumer at the same physical site would need
separate composition evidence; exclusive ownership is part of this recipe.

There is an existing return-PC consumer: FL media-cue classification
(`src/engine/fl/media_cue_fl_sites.h`, shared `media_cue_core.h::classify`)
expects speech wrapper return `4b4ee7` at allocator ESP+0xc. Replaying create
exposes its generated A+5 instead, including with a conventional relocated
CALL tail. Consequently speech would classify as `other` unless the adapter
registers/normalizes that **exact known generated return identity** for this
consumer. Broad arena-address acceptance is not justified. This is a required
integration fixture and an open implementation decision, not proof of full
observer equivalence. The selector-only refusal discriminator `46612b` is a
different unmodified call; no default selector-policy change is established.

### Incoming edges and publisher witness

Measured follow-up: 3,976 instructions across eight gap-free targeted ranges;
five selected physical spans have **zero interior direct-control candidates**
in an executable-text byte-pattern scan, and zero interior image dword
references, aligned or unaligned. The scan includes E8/E9 and near Jcc across
the text section and short Jcc/JMP/LOOP candidates whose displacement reaches
the span. It conservatively retains unknown boundaries; none occurs here.
This supplements, rather than substitutes for, the decoded containing-routine
boundaries and exact span hashes in the evidence.

The two overlay jump tables at `42f0a8/42f0b8` each have four entries, bounded
by `CMP EAX,3; JA` at `42c3f7/42c3fa` and `42c677/42c67a`. Publisher's five
entries at `427628` are bounded by `CMP EAX,4; JA` at `4273aa/4273ad`; mode 3
lands at `4275bb`. Playback's 14 entries at `4b5bb0` are included because it
calls the speech routine. All 27 entries are HIGHLOW operands, valid decoded
instruction starts and outside the five stolen interiors. These are the
only indirect JMPs in the four decoded containing routines; speech has none.
This closes these conventional absolute switch-table entries, not arbitrary
computed entry, third-party code or patch-time thread liveness.

The former seek-span dword matches are inside NUL-terminated `.rdata` ASCII
names `ERROR_GLOBAL_ONLY_HOOK` and `ICON_MENU_LOGBOOK`, at `55a7f3` and
`583be2`; the coincident bytes are `OOK\0`. Neither has a HIGHLOW relocation.
They are string bytes, not established code-entry pointers; the chosen recipe
also leaves their apparent target `4b4f4f` untouched.

Publisher entry `4273a0` reads the signed low 16 bits of `[entry ESP+4]` into
EAX, independently of incoming EAX. ECX is the cockpit; target is
`[entry ESP+8]`; `[entry ESP]` is the actual caller PC. Save EBX, ESI and EDI
at `4273a5/4273a6/4273a9`, preserve receiver in ESI at `4273a7`, and read target
at saved ESP+0x14 in mode 3 (`4275bb`). Endpoint `427621` has three POPs then
RET8 at `427624`: expected endpoint ESP is entry ESP−12. All four native
returns (`427425/4274c2/4275b8/427624`) are RET8; modes 0/1/2/4 can return
earlier and must not create an unmatched tracked mode-3 token.

The FL witness adapter should check native low-word mode semantics before
copying metadata, read caller/mode/target from that immutable entry snapshot,
and use checked overflow bounds for all stack and cockpit offsets. Retain
only opaque copied values. A null target reaches the common end without a
cockpit dereference; keep that path valid even with an invalid cockpit value.
For a nonnull target copy previous target (+0x1e0), tracking word (+0x1e4,
mask the existing four-byte read or use a checked two-byte read), and view
(+0x10). At end do no cockpit read: match exact saved endpoint ESP and use
saved native EAX as an opaque outcome. FL EAX after named dispatch is not a
proven publisher Boolean return contract.

### Reentry, lifetime and acceptance before implementation

Mode 3 stores target/tracking at `4275ed/4275f3` before calling `4bb500` at
`427619`, and cleans its eight event argument words at `42761e`. The wrapper
can tail-jump at `4bb59d` to `4bb470`; that path publishes the task at
VM+0x1434 (`4bb4ea`) and calls interpreter `4bed90` synchronously at `4bb4f0`.
The event can therefore run script work before publisher-end, including
nested publication/media operations. Calls may also fail dispatch and return
without entering the interpreter. This does not establish which scripts
reenter or their runtime frequency.

Reuse the shared recorder: only LoopSetup admits the process-lifetime owner;
foreign-thread stamps are rejected, reporting reentry is suppressed and
revokes the epoch, reads fail closed, nesting is capped at eight, and lifecycle
epoch changes revoke pending work. Same-owner native nested calls are allowed
within that bound. `target_end` already ignores joins unless top kind and
exact expected ESP agree, including filtered nested mode-2/invalid-mode
arrivals while another publisher is open. A mode-3 null target is tracked by
the existing entry rule; other untracked joins are ignored. Nested mode-3 publishers use
separate LIFO entries. Delayed remains its existing strict begin/end pair;
its end now runs only on its own normal CALL return. Overflow, bad nesting or
abandoned work invalidates rather than completing another call. Do not add
an unrelated recorder or make the native synchronous event a no-reentry claim.

No unwind/SEH/longjmp contract is established. Nonlocal escape can skip the
paired end and leave a diagnostic token until existing mismatch/epoch/next
loop revocation. The generated arena has no demonstrated exception-handling
contract; neither native unwind preservation nor immediate cleanup on an
escape is claimed. Native-state equivalence here is for ordinary returns.

Before implementing/activating, add focused fixtures for: (1) the paired
chains at preferred/rebased module bases and widely separated x86 arenas,
with one native call and exactly two stamps, delayed RET8 versus the two plain
RET cases; (2) exact native ESP/result/GPR/EFLAGS, DF, XMM0–7, x87/MXCSR and
LastError before and after each callback, including four-byte incoming stack
alignment and both seek outcomes; (3) bypass entries at `42c4ff`, `4b4eec` and
`4b4fc8`, with unchanged cleanup/flags/results and no spurious end; (4) FL
publisher mode-3/null/other modes, exact −12 endpoint pairing, nested tracked
mode 3, filtered nested mode 2/invalid modes, event failure and synchronous
nested media, foreign/reporting reentry, overflow and epoch revocation;
(5) mutation refusal for profile/call/table/ABI anchors and all-or-none group
rollback at each claim, stamp emission, continuation publication and arena
finalization failure, with retained replay lifetime. Include skipped-end
diagnostic revocation without claiming native exception unwinding.
Include media-cue speech classification through the generated create-return
PC, with the unchanged sector/other discriminators, before enabling both
features together.

The producer currently checks 18 generated replay decoding/target cases
(three pairs × two module bases × three arenas); these are measured static
checks, not executable CPU-preservation fixtures. Retain shared startup
quiescence and install-window closure; atomic patch bytes alone do not prove
no thread is executing a stolen span. Reuse unchanged CPU/recorder/lifecycle
evidence and add these FL bindings. No production patch, game/native/Wine
execution, overhead measurement or Windows runtime qualification occurred.

## Primary frame pack: shared-installer static qualification

2026-10-30, separate from the three paired game-call recipes above. The ten
frame spans have an implementable **ordinary SiteSpec profile** for the
existing shared frame installer. No special replay writer is needed. This is
static profile qualification; activation still requires the scoped executable
fixtures below. The primary game group remains pending, including its exact
transactional media-cue return-PC alias; this frame pack neither establishes
that alias nor requires game-phase activation.

Run `python3 verification/results/fl-phase-diagnostics/qualify_frame.py`.
[Producer](../../../verification/results/fl-phase-diagnostics/qualify_frame.py)
reuses the committed 10-site mapping and bounded replay/edge helpers;
[profile evidence](../../../verification/results/fl-phase-diagnostics/frame-profile.json)
retains SiteSpec fields, instruction operands/sizes, original span hashes,
incoming sources and replay hashes. It emits no copied native byte arrays.
Measured result: `FRAME_INSTALLER_STATIC_PROFILE_PASS`, ten spans, 554
instructions over the same complete 1,889-byte frame routine, and 60 tail
checks (ten sites × two module bases × three widely separated x86 arenas).
The artifact deliberately keeps `hook_qualification=false`: no FL executable
CPU fixture, game delivery, patch-time liveness or native Windows test ran.

The original frame table in this note remains the profile's logical order.
Every row has `ret_pop=0`; lengths and rel32 offsets are unchanged from the
mapping. Resolve address as admitted image base + recorded RVA. Expected
bytes must be the admitted FL instruction bytes with **only** these three
HIGHLOW dwords adjusted by loaded-base minus `400000` before verification:

| ID | Instruction VA; operand offset | Preferred global operand; global RVA |
| --- | --- | --- |
| 2 BeginScene | `48b84c`; +1 | `5aaf94`; `1aaf94` |
| 5 text | `48bd0c`; +1 | `5aa100`; `1aa100` |
| 9 submit end | `48ba9b`; +2 | `5aa100`; `1aa100` |

These are native absolute operands, **not additional callback global reads**.
The generic frame callback consumes no native register/stack arguments and
borrows no renderer, view or engine object. Native BeginScene proceeds through
the owner slot, +0x18 and its first pointer before vtable+0xa4 (`48b851..859`).
Native view submission reads `[ESI+0x1c]` then scene+0x4c at `48ba51/54`.
Preserve these live operands; do not add speculative metadata reads.

The shared tail copies the actual loaded bytes, retaining rebased absolute
values. Its one declared rel32 is adjusted to the original loaded target.
Prologue/scene-update/scene-end are five-byte CALLs (field +1); view setup is
`MOV ECX,ESI; CALL 4a4c80`, length seven, field +3. FL's setup has no pushed
view argument; AP's six-byte PUSH/CALL fixture is insufficient for this ABI.
Overlays is `XOR EDI,EDI; TEST EBX,EBX; JLE 48bc54`, length ten, field +6. The
near JLE is the final displaced instruction and fits the patch engine's
existing one-rel32 branch contract. Its taken path leaves the tail directly;
its untaken path uses the tail's appended JMP to `48bbb7`. Views replays
`XOR EAX,EAX; ADD ESP,0x10` at the exact native ESP after qsort `48b902`.
Do not move cleanup before the stamp or replace FL's register assignments
with their AP counterparts.

As with the shared AP tail, displaced CALLs expose their arena return PC to
native callees; no original-return-PC identity promise is made. This frame
study establishes no additional return-PC consumer analogous to game's known
media-cue classifier. A consumer discovered during integration would require
its own exact contract rather than broad arena acceptance.

Measured incoming-edge closure for this bounded stock image: each local entry
source equals the committed mapping, no executable-text direct-control byte
candidate reaches a stolen interior, and no image dword names any selected
span byte (including starts, aligned or unaligned). The text scan includes
near CALL/JMP/Jcc plus short JMP/Jcc/LOOP candidates, retaining unknown-boundary
hits rather than assuming they are harmless; none remained at these spans.
No selected fixup straddles a span boundary. The complete routine has no
indirect JMP or jump table and one plain RET at `48be30`; the existing
compositor claim `48b929..48b92e` is disjoint from all ten spans. This settles
the routine's direct and conventional absolute jump-table entry cases.
Arbitrary computed entry, foreign code and third-party patches remain outside
that claim. Preflight must still reject changed bytes.

The image-wide direct-control scan finds one opcode candidate targeting frame
entry `48b6d0`, verified CALL at main-loop `4782cf`; no image literal names that
entry and no direct self-recursion occurs. This supports the normal native
ordering, not proof that callbacks or third-party code can never reenter.
Seven core stamps advance monotonically, with forward skips permitted (the
no-view route reaches text directly). View stamps accumulate each actual
setup/submit route; skipped setup paths must not be synthesized as views.

Reuse the actual shared frame policy, not the game recorder's different
policy: Present-begin admits one process-lifetime owner thread; pre-admission
and foreign-thread engine stamps are counted and ignored. A repeated/backward
core stamp drops that frame; subsequent stamps are unmatched until Present
return restarts it. The three per-view timestamps are single accumulators,
not a nesting stack. The current frame tracker has **no reporting-busy or
lifecycle-epoch gate of its own**. Do not describe nested per-view reentry or
reentry during summary reporting as suppressed by a nonexistent gate. Normal
reentry through frame entry produces a repeated core marker and drops the
sample; arbitrary reentry directly into a view marker is not established safe.
These are existing shared-policy limits, not a reason to invent an FL-only
recorder. Keep frame sampling usable with primary game diagnostics disabled.

CPU/LastError contract: reuse `game_phases::emit_stub(33+frame_id)` and
`x3m_game_phase_enter`; the logical frame offset remains 33 independently of
how many distinct physical game claims eventually exist. The stub saves
EFLAGS/GPRs and XMM0–7, clears DF for C++, then restores them before the tail.
The C++ boundary's `PreserveCpuState` saves/restores x87, MXCSR and LastError,
sets default arithmetic state for the callback, and realigns its four-byte
incoming stack. Incoming native flags/registers are conservatively live at
every stamp; restore them all rather than relying on AP flag-liveness claims.
No callback may execute after tail replay and before its native continuation.
This preserves qsort cleanup, native load results and the overlays condition
under the existing normal-return envelope.

Native frame SEH registration is visible at `48b6e1`, restoring FS:[0] at
`48be26` before its normal epilogue. Observing that native setup does not give
generated arena callbacks an exception-unwind contract. No assertion of
unwind transparency, exception cleanup or delivery following nonlocal escape
is made. Thread quiescence is likewise an install-owner prerequisite: retain
the shared startup install window and immutable executable admission; neither
atomic patch bytes nor an empty incoming-edge set proves no live instruction
pointer is inside a span.

Implementation acceptance is bounded to the FL binding and changed span
shapes. Extend the existing synthetic frame body/installer fixture with FL's
XOR EAX/qsort cleanup, seven-byte ECX setup CALL, six-byte EDI layer load and
ten-byte overlays near JLE (both outcomes). Supply fixture-owned absolute
globals at all three HIGHLOW fields; compare native and hooked GPR/EFLAGS,
exact ESP, DF, XMM0–7, x87/MXCSR and LastError with a hostile callback and
four-byte incoming alignment. Verify one native execution per CALL, bypass
entries at every mapped incoming start, no-view forward skips, repeated core
drop/restart and owner/early/foreign handling. Exercise the real preserving
bridge and normal recorder path, not only generated target arithmetic.

For the actual ten-site installer, verify preferred/rebased expected bytes,
refusal on changed opcode/length/rel32/global-fixup/profile identity, and
all-or-none rollback for claim/emitter/continuation/finalization failures.
Retain the existing arena lifetime and restoration-debt contract and report
actual resolved addresses. Reuse unchanged tracker/window, Present placement,
CPU-envelope and transaction evidence; no broad GPU qualification or unrelated
stamp-family census is required. A later user run establishes FL delivery and
diagnostic overhead. There is no unresolved instruction/replay case among
these ten selected spans; activation and the stated reentry/unwind limits
remain distinct from this static profile result.

The binding adds no stamp-time allocation, metadata validation or lock: owned
stamps keep the existing thread check, one QPC and bounded tracker updates.
The shared full CPU envelope remains the dominant candidate overhead, paid
per delivered stamp; this offline study supplies no game-time measurement.

## Primary frame implementation binding

2026-10-30 follow-up: `src/engine/fl/frame_phase_sites_fl.h` now supplies the
qualified ten-site pack to the shared `frame_phases.cpp` transaction. Its
cold binder requires recognized FL 1.3 identity and a bounded aligned module
base, resolves every RVA, and rebases only the three HIGHLOW dwords listed
above. The actual loaded bytes still undergo complete preflight before any
claim. Successful installation publishes the existing `active` flag only after
all ten preserving chains are installed; AP's executable gate is unchanged.
The logged addresses/lengths/rel32 fields identify the selected pack. No other
stamp family, game group, recorder or native-object metadata reader is added.

Implementation checks and the frozen synthetic native fixture are tracked in
[the frame verification ledger](../../verification/farnhams-legacy-frame-phases.md).
The fixture's bounded `--fl-frame-only` mode exercises the changed FL operand
and replay shapes through the real shared CPU bridge, and the same normal
recorder with primary game diagnostics disabled. Synthetic execution, actual
FL delivery and the unqualified unwind/quiescence/reentry limits above remain
separate evidence boundaries.

The corrected bounded synthetic mode subsequently passed **798 native Windows
x86 checks with zero failures** (parent queue, measured 0.573 s). The ledger
retains its passing receipt and the initial fixture flags-input failure; the
production profile/bridge did not change between those attempts. These results
qualify the bounded normal-return replay and CPU/LastError envelope exercised
by the fixture. They do not prove live FL delivery, game overhead, exception
unwinding or patch-time quiescence.

## Primary game implementation binding

2026-10-30 follow-up: `game_phase_sites_fl.h` now resolves all 33 logical
roles, with **30 physical claims**. The three unsafe native ends remain
unpatched. Their exclusive begin sites instead publish preserving begin →
one generated native CALL → preserving end → native continuation chains,
using the existing patch owner, retained tails and CPU bridge. The shared
recorder, nesting/owner/epoch policy and frame offset 33 remain unchanged.
Cold selection binds the FL pump globals and one immutable FL witness decoder;
AP keeps its original executable gate and decoder semantics.

Media cooperation records two exact generated return identities: the paired
CALL's A+5 and the ordinary claim tail's tail+5. The latter remains reachable
through the claim dispatcher before paired-head publication, including failed
publication with restoration debt. Both normalize only to the corresponding
original speech return. Aliases are ready before paired publication and remain
alive with retained arena code after rollback. The exact original/patched
five-byte owned CALL witness permits media's later cold anchor admission;
any unrelated or changed bytes still refuse. No initialization-order change,
return-slot rewrite or broad arena-address classification is introduced.

The [game verification ledger](../../verification/farnhams-legacy-game-phases.md)
records 40 host tests, all 30 spans' image-wide interior checks, 110 cold ABI/
switch/dispatch windows, x86/CPU audits, exhaustive transaction faults and the
parent-owned **701-check native synthetic pass**. It preserves the initial
receipt-parser defect and capture-only correction, plus the later 17-window
admission-only delta and reused unchanged replay evidence. The subsequent
ordinary-tail restoration-debt correction has actual-helper host fault coverage
and current x86 compilation; the frozen native pass does not cover that delta.
Actual FL delivery,
game overhead, unwind behavior and patch-time quiescence remain separate.
