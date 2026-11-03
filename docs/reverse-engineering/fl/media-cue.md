# Farnham's Legacy media allocator, caller discrimination and record reader

2026-10-29. Read-only static study of the clean FL and AP originals. No game,
Wine command, DLL build or production patch was run. Addresses are preferred
x86 VAs, independently located; there is no common AP→FL address shift.

**Outcome.** FL `0x004b4080` is the counterpart of AP `0x00498140`, but its
inputs are **ECX=id, EDX=constructor flags**, with no stack argument. The record
and list fields needed by the existing bounded music reader survive. All
allocator calls pass through one notification wrapper, so the AP five-entry
return-address classifier cannot be installed unchanged. The sector path has
inlined the old helper; its kind `0x5a` is assigned after creation, rather than
passed at the AP stack offset. The shared value-only cache and ID2 skip policy
can be retained behind an FL adapter. Clean FL and AP ID2 assets are identical.
This establishes a port contract, not an implemented or runtime-qualified port.

Producing command:

```sh
python3 verification/results/fl-port/media-cue-contract/reproduce.py
```

The [producer](../../../verification/results/fl-port/media-cue-contract/reproduce.py)
rereads PE bytes, decodes complete bounded routines with Capstone, checks
reviewed instruction semantics, scans every raw `.text` offset for direct
callers/incoming hook edges, queries only ID2 resource keys and rejects 13
in-memory mutation controls. The [summary](../../../verification/results/fl-port/media-cue-contract/summary.json)
contains hashes, counts and addresses, without raw disassembly or media bytes.
Hashes identify the evidence inputs; they are not an executable admission rule.

| Measured input | Bytes | SHA-256 |
| --- | ---: | --- |
| `/mnt/c/X3FL-orig/X3FL.exe` | 2,804,736 | `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8` |
| `/mnt/c/X3AP-orig/X3AP.exe` | 2,153,984 | `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` |

Both are PE32/i386 with image base `0x00400000`. All instruction and resource
counts below are measured static evidence. Proposed ownership and integration
rules are explicitly recommendations. The current AP default is the
unconditional ID2 video skip in `src/engine/common/media_cue_core.h`; the older
default-off introductory wording in [media-cue-playback.md](../media-cue-playback.md)
describes the optional trace/cache and does not supersede that source policy.

## 1. Allocator ABI, boundaries and failure

FL `0x004b4080..0x004b41a4` (exclusive end) decodes gap-free: **292 bytes,
91 instructions**, followed by `int3` padding to `0x004b41b0`. Two plain `ret`
instructions occur at `0x004b40f6` (failure) and `0x004b41a3` (success).

The private register ABI is `record* create_media_record(ECX=id, EDX=flags)`;
EAX returns a linked record or zero. No stack argument is consumed or popped.
`0x004b4083` copies EDX to EDI; `0x004b4085` copies ECX to EBX. EBX, ESI and
EDI are saved/restored on both returns; EBP is untouched. ECX, EDX and flags are
scratch. EAX on entry is dead. This is compatible with a two-register fastcall
shape but does not establish a public C++ signature or decoration.

`0x004b4087..0x004b408c` calls `0x004d8fc0` with ECX=`0x40`, EDX=0. That
helper uses malloc (`0x00531395`), one recovery callback/retry, allocation
accounting, and zero-fill (`0x00521920`). Unlike the AP allocator's visible
body, FL explicitly handles a null record allocation: `0x004b4095` branches
directly to the zero-result epilogue, without freeing null or decrementing
uncredited counters.

For a non-null record, zero incoming flags select the same ID map as AP:
IDs 1/3→flags8 and initial kind `0x42`; ID2→flags8 and initial kind `0x3d`;
IDs 800/10001→flags8; IDs 810/811→`0x100`; IDs 101..999→`0x110` unless one of
those exceptions applies; others→0. Nonzero EDX bypasses the map. The unsigned
range check at `0x004b40ba..0x004b40c7` implements the inclusive 101..999 range.

`0x004b40ca..0x004b40ce` passes ECX=id, EDX=effective flags to graph constructor
`0x004fda20`; `0x004b40d3` stores its EAX into record+`0x24`. On zero result,
`0x004b40da..0x004b40f6` frees the still-unlinked record, subtracts `0x40` from
`0x005aae20` and `0x005aaa18`, then executes `xor eax,eax; pop edi; pop esi;
pop ebx; ret`. Nothing is inserted into the owner list on either failure arm.

The constructor decodes gap-free over `0x004fda20..0x004fe5ee`: **3,022 bytes,
910 instructions**. Its ordinary failure tail is `0x004fdd77`→media destructor
`0x004ffd70`→`xor eax,eax` at `0x004fdd7e`→FS/stack restoration→plain return
at `0x004fdd92`; null object allocation also reaches the zero tail. Success
returns EDI in EAX at `0x004fe5dc`, returning at `0x004fe5ed`. Its own direct
absolute writes are only the three FS:[0] SEH-registration/restoration sites;
this does **not** assert that its callees, COM providers or recovery callback
have no side effects.

An entry refusal with `xor eax,eax; ret` therefore reproduces the failed
allocator's result, stack cleanup and defined arithmetic flags: ZF=1, PF=1,
CF=OF=SF=0; AF remains undefined in both paths. Successful allocations credit
live bytes `0x005aae20`, class-zero live bytes `0x005aaa18`, cumulative bytes
`0x005aaa14` and cumulative allocation count `0x005aae1c`; freeing unwinds the
live pair. Refusing skips these transient allocations and cumulative credits,
and intentionally skips graph/provider activity. Full transitive side-effect
equivalence to a real failed COM graph has not been proved.

## 2. Complete immediate caller sets and sector discriminator

The AP reference has the five direct allocator callers from the existing note:
`0x0049873a`, `0x00498bae`, `0x00498cd8`, `0x00498ef8`, `0x004f6610`.
FL has **one** direct allocator caller: `0x004b46be`, returning to
`0x004b46c3`, inside wrapper `0x004b46b0..0x004b4729` (49 instructions).
There is no raw dword reference to the allocator, wrapper or constructor entry
anywhere in the image. The every-byte relative-branch scan finds only the
reviewed allocator call targeting its entry and no edge into its hook span.
This establishes the immediate direct-entry set; it is not a proof against
every conceivable computed indirect target.

The wrapper receives completion slot in ECX and context in EDX, plus three
caller-popped stack arguments `(id, constructor_flags, notify)`. It saves ESI
and EDI, loads flags/id at `0x004b46b6`/`0x004b46ba`, then calls the allocator.
It optionally reports failure/success through the VM callback table. The
allocator's result is tested before notification; a refused call takes the
same notification arm as a normal zero result.

| Classification | FL enclosing path | Wrapper call | Wrapper return visible at allocator `[ESP+0xc]` |
| --- | --- | --- | --- |
| query/create-and-notify | `0x004b4730`, VM call `0x004b5913` | `0x004b475d` | `0x004b4762` |
| savegame MOVI restore | `0x004b4a50`, load call `0x0047985c` | `0x004b4b45` | `0x004b4b4a` |
| script play-by-id | `0x004b4c50`, VM calls `0x004b593a`/`0x004b5a4a`, helper play `0x0051390c` | `0x004b4ca4` | `0x004b4ca9` |
| speech | `0x004b4e10`, VM call `0x004b595c` | `0x004b4ee2` | `0x004b4ee7` |
| other media helper | `0x00513890`, callers `0x0046a420`/`0x00513b30` | `0x005138bc` | `0x005138c1` |
| sector selector, helper inlined | selector `0x00465030`, creation `0x0046611d..0x00466126` | `0x00466126` | `0x0046612b` |

The wrapper has exactly those **six** direct call sites. The save path's MOVI
tag is checked at `0x0058dd68`. Speech resolves its range through
`0x004b5830` and populates record+`0x34/0x38/0x3c`; its separate wrapper return
keeps it outside selector caching. No new speech observer is needed.

At allocator entry the actual stack is:

| Offset | Meaning |
| --- | --- |
| `+0x00` | immediate return `0x004b46c3` |
| `+0x04`, `+0x08` | wrapper's saved EDI, saved ESI |
| `+0x0c` | wrapper caller return, from the table above |
| `+0x10` | wrapper id argument, a second copy of incoming ECX |
| `+0x14` | wrapper constructor-flags argument, a second copy of incoming EDX |
| `+0x18` | wrapper notify argument |

Thus FL selector scope is precisely `[ESP]==0x004b46c3` **and**
`[ESP+0xc]==0x0046612b`. The sector restart reads `[sector+0x178]`, indexes
Videos at `*0x005a8450` with stride `0x30` (`0x004660d8..0x004660de`), and
loads media id at +`0x14` (`0x004660f0`). It pushes `(id,0,0)` and zeroes both
completion registers before the wrapper call. On creation failure it re-scans
the list and exits this restart arm without playback. Only a matching linked
record reaches `0x004662f2`, setting flag4 and **kind `0x5a` at `0x00466303`**,
then calling play at `0x00466313`.

Kind `0x5a` is consequently established by this static path, not a value at
allocator `[ESP+0x10]`: that slot is the id. An FL adapter should supply the
proven kind only after matching both return discriminators. Generic helper
calls remain `other`; if later required for diagnostics, their saved initial
ECX exposes kind at allocator+`0x28` and helper caller return at +`0x2c`.
The current sector path never traverses that generic helper.

## 3. Record/list layout and lifetime for the existing reader

`*0x005a84b4` is the media owner, allocated as `0xfb0` bytes at
`0x004b3fb0..0x004b3fbe`. The owner's +0 link points to the first record or
the embedded sentinel at owner+4. Initialization writes sentinel.next=0 and
sentinel.prev=owner at `0x004b3fce..0x004b3fd8`.

The AP reader's traversal shape survives with the owner global changed:
`owner=load32(0x005a84b4); node=load32(owner);` visit records while
`node && load32(node)!=0`, advancing through node+0. A null owner means no
subsystem. The sentinel must never be treated as a full `0x40`-byte record.

| Record offset | Measured FL use |
| --- | --- |
| `+0x00` / `+0x04` | next / previous link; insertion `0x004b4151..0x004b4160`, removal `0x004b44e3..0x004b44f0` |
| `+0x10` | id, written `0x004b419e`, matched by caller list scans |
| `+0x14` / `+0x18` | completion context / slot; used before retirement and assigned by play at `0x004b4da8`/`0x004b4dab` |
| `+0x1c` / `+0x20` | play start / end values, assigned at `0x004b4d4d`/`0x004b4d54` |
| `+0x24` | owned media object; graph return at `0x004b40d3` |
| `+0x2c` | flags; graph+`0x8c` copied at `0x004b4141`; bit2 playing, bit1 loop, bit4 kind-managed, `0x80` music volume class |
| `+0x30` | cue kind; ID defaults and later helper/selector assignment |

Success links the zeroed record, applies optional volume classes, clears
playing bit2 at `0x004b4197`, writes id and returns the record. Media object
size `0xb4` and flags+`0x8c` are established by the constructor. This study
does not qualify every media-position reader, VM, save hook, music stop/seek
site, active/focus flag or background-mode input needed by the full music
adapter.

FL retirement `0x004b4490..0x004b450b` preserves its record in ESI, dispatches
completion when context/slot are nonzero, tears down media through
`0x004ffd70`, **then** unlinks both neighbors and frees the record. Thus list
membership alone does not pin a record through callbacks or COM teardown.
Owner teardown `0x004b4010..0x004b4077` walks/free records, frees owner and
zeroes the owner global at `0x004b406b`; another cleanup walk exists at
`0x004b30ee..0x004b3110`. Reacquire the owner and validate record/id/media
identity for later use as the existing music reader does. That bounded walk
does not establish cross-thread safety or make added synchronous calls safe.
The same limitation is discussed for AP in
[media-record-lifetime.md](../media-record-lifetime.md); FL callback closure has
not been reconstructed by this task.

## 4. Minimal gate, CPU boundary and reentrancy

The minimal rel32 entry patch is **5 bytes**, four whole instructions:
`push ebx; push esi; push edi; mov edi,edx`. Continuation is `0x004b4085`.
No instruction in the span is relative control flow, so replay is a plain
copy, at the original game ESP. All raw `.text` encodings targeting its
interior were scanned: **zero hits**. All allocator branch targets land on
decoded boundaries. The allocator has **zero explicit ESP memory operands**:
it never inspects its return slot or stack arguments. Replacing its immediate
return address for normal outcome observation is therefore compatible with
this body. Calls/returns and register saves still use the stack normally.

Incoming flags are dead; the allocator overwrites them before any consumer.
The proceed arm must restore ECX/EDX and ESP so the displaced instructions
capture the real parameters. The refusal arm must preserve callee-saved
registers and use `xor eax,eax; ret`, with no stack-pop immediate. The existing
AP gate machine-code envelope already saves EFLAGS/EAX/ECX/EDX and XMM0..7;
its exact-ESP replay and return trampoline shape can be shared **after the
existing return-flag defect is corrected**. Independent review found that
`emit_gate` reserves the return slot with `sub esp,4` before `pushfd`, saving
the subtraction flags rather than allocator flags. The existing CPU fixture
cleans its argument with `add esp,4` before observation and misses the defect.
Use flag-neutral reservation and capture flags before caller cleanup in its
focused fixture. FL currently tests EAX immediately after the wrapper returns,
so this does not establish a demonstrated FL behavior regression. Keep the
LastError/MXCSR boundary, SSE2/x87-free handlers and four-byte incoming stack
support. This is source-level compatibility reasoning, not an executed FL
stub fixture; the shared-envelope correction is a separate implementation
checkpoint. The correction now passes independent source and native review:
902 checks with zero failures, with the original emitter reproducing the flag
defect. See the [media ledger](../../verification/media-cues.md).

The **AP handler/frame interpretation cannot be reused unchanged**: its
`f->id` is allocator+4 (a saved EDI in FL), `f->eax` is not constructor flags,
the AP classifier tests immediate rather than wrapper returns for four
classes, and `slot_10` is not kind. The return handler's `f->id` consistency
test also reads a saved register in FL. A profile adapter must recover id from
saved ECX (or the verified wrapper id copy), flags from saved EDX, and pair
returns by the substituted return-slot address and pending id. No different
negative-cache implementation is required.

Neither unlocked list walks nor the direct entry set establishes absence of
reentry. Allocation helper `0x004d9057..0x004d906a` loads and calls the
registered recovery callback `*0x005aae24`; COM construction/rendering also
crosses external code. Default ID2 refusal should retain its immutable,
API-free rule before owner admission, including foreign/reentrant entries.
Optional tracing/cache must preserve bounded pending depth, owner checks and
safe pass-through on unsupported nesting. Constructor SEH normal-return paths
are present; a nonlocal exception escaping a substituted call can bypass its
return observer. No unwind-cleanup contract for those pending entries is
established here. Do not infer a reentry or exception guarantee from the
allocator itself having no SEH frame.

## 5. ID2 policy parity, port shape and open qualification

The existing all-caller default rule is ID2 **and incoming flags 0 or 8**.
FL ID2 defaults to effective flags8 exactly as AP. Explicit EDX8 bypasses the
map but constructs the same stream flags; any other nonzero incoming flags
must keep passing through. FL constructor+`0x8c` stores flags, conditionally
adding `0x40` only for bit`0x10`. At `0x004fdaef..0x004fdb1c`, bit`0x10`
clear takes the primary-video AddMediaStream branch. At
`0x004fdb22..0x004fdb29`, bit8 set bypasses audio AddMediaStream. The primary
video/audio stream GUIDs at `0x00550b94`/`0x00550b84` match AP
`0x00532b94`/`0x00532b84`; this is video-only dispatch evidence independent
of matching flag constants.

FL formats `%05d.dat` (`0x00590b84`), probes `addon2\mov\%s`
(`0x00590b90`) first, then its configured movie format. Startup
`0x00509e95` installs table `0x005a5ca0` at config+`0xc8`; table+`0x90`
points to `mov\%s` at `0x0058b16c`. The constructor uses that field at
`0x004fdc4f..0x004fdc67`. AP's corresponding default table is `0x0057c008`,
installed at `0x004ecab2`, with table+`0x90` also `mov\%s`.

For the clean originals, neither edition has a loose edition-directory ID2
override, and querying all mounted numbered CAT namespaces finds **zero**
matching ID2 members (**18 FL**, **17 AP** catalogues). Both effective default
loose files are `mov/00002.dat`, **533,575,370 bytes**, SHA-256
`401e4192e150a848621d60666b2946e9bfa725a17c5a28e231b7cd55098e4c76`.
Their MPEG sequence header measures **512×512, frame-rate code3 (25 fps)**.
No media payload is extracted or stored in the report. A modified runtime
configuration, selected mod or replacement loose asset is outside this clean
snapshot; the existing AP skip policy already keys by id/flags rather than
asset hash. There is no discovered new FL caller obligation to create ID2 on
failure: all six paths have explicit no-record failure handling.

**Recommended port:** retain the shared default ID2 policy and optional
fixed-capacity negative cache, pending stack and trace ring. Add the FL entry
spec and an FL value adapter for register arguments, wrapper return
classification and statically proven selector kind. Move/share the policy
where architecture owns it; do not duplicate the cache. Change only the owner
global for the bounded record reader after its caller/lifetime constraints are
satisfied. Full music feature hooks remain a separate qualification task.

The source hot path remains scalar: immutable id/flags comparison before API
calls; inactive trace/cache passes without clock or list traversal. Selector
classification needs two return-address comparisons and one already-proven
stack-slot read; it does not need Videos/sector lookup on every attempt.
Optional cache retains the existing 32-slot bounded search. Record walks stay
event-driven with the existing 4096-node limit, not per draw or per frame.
These are source cost bounds; no FL timing or game-FPS measurement is claimed.

Before production activation, affected host/CPU fixtures must cover the FL
register inputs, exact ESP replay, all six wrapper returns, refusal flags,
return-slot pairing, foreign/reentrant admission and rollback. Resolve any
exception-unwind policy before claiming trace-return observation is robust to
nonlocal exceptions. An FL runtime user capture and native Windows behavior
remain unverified. Neither lack of those measurements nor this documentation
changes production defaults by itself.

Independent static review reproduced the retained summary exactly and accepted
the ABI, hook span, caller discrimination, failure handling and ID2 resource
contract, subject to the shared return-flag correction above and the stated
runtime/reentry limitations. No production adapter or runtime acceptance follows
from this note alone.

## 6. Shared adapter and FL video consumer (2026-10-29)

The subsequent source adapter moves `media_cue.cpp`, its public header and its
value-only core to `src/engine/common/`. AP site data remains in
`src/engine/ap/media_cue_sites.h`; FL's explicit register/caller/range binding
is `src/engine/fl/media_cue_fl_sites.h`. This is an implementation checkpoint;
runtime qualification and installation are separate. AP retains its existing
`object_trace::executable_verified()` admission. FL requires the recognized
FL 1.3 profile and eleven exact code windows read at initialization, resolving
preferred addresses against the recognized loaded base. No runtime resource
scan, media payload hash or per-call code validation is added.

FL default skip reads saved ECX/EDX. Optional classification requires the
immediate wrapper return and then compares the wrapper caller at original
ESP+`0xc`; only the proven selector supplies kind`0x5a`. Original ESP+`0x10`
remains the wrapper id argument and is never interpreted as kind. Normal
returns pair the exact substituted slot address with its pending id; AP's
stack-id consistency check is preserved only for AP. The shared LEA return-slot
reservation preserves outgoing flags before PUSHFD. The fixed-capacity cache,
owner admission, nested pending depth, failed-clock and overflow behavior stay
shared; no record/list reader or music feature is added.

**Video observer contract.** The FL counterpart of AP's CPU video copy is
`0x004fed10..0x004ff578` (exclusive end): **2,152 bytes, 620 instructions**,
decoded without gaps through the last `ret`. The next pump begins at
`0x004ff580`; its only direct call to the consumer is `0x004ff7c6`, passing
ECX=`[media+0x10]` (source DirectDraw surface), EDX=the destination D3D9 surface.
The consumer saves those two values separately. It queries the source through
vtable+`0x64`, queries destination description through +`0x30`, then calls
**destination LockRect at `0x004fedf0`**, vtable+`0x34`, with a local locked
rectangle, NULL rectangle and flags0. Its return PC is **`0x004fedf3`**.
After copying, **destination UnlockRect at `0x004ff556`**, vtable+`0x38`, returns
at **`0x004ff559`**; the distinct source is then unlocked through +`0x80`.
The shared optional observer admits the FL consumer's range, rebased at cold
initialization, and therefore includes both destination return PCs. It neither
uses AP's addresses for FL nor disables the diagnostic.

The [bounded producer](../../../verification/results/fl-port/media-cue-adapter/reproduce_re.py)
and [summary](../../../verification/results/fl-port/media-cue-adapter/re.json)
check the reviewed allocator contract, consumer operands and all eleven
whole-instruction binding windows, rejecting five mutation controls. Every
consumer-local branch targets a decoded boundary. The raw external interior
scan finds three byte patterns, each inside a decoded pump operand rather
than at an instruction boundary; these are recorded rather than reported as
zero raw hits. No absolute entry reference exists. This is targeted static
entry evidence; computed indirect entry closure is not proved.

The observer retains no surface or game record and changes no D3D state. It
borrows the ownership event's native surface for the existing cadenced GetDesc;
Reset and resource lifetime behavior are unchanged. There is no new per-draw
work. The default gate adds a scalar immutable ABI selection before the
existing id/flags check, with no API, clock, allocation, lock or list walk on
refusal. Optional cache lookup remains bounded to 32 entries and pending depth
to four; no performance timing or game FPS delta is inferred from these bounds.

**Escaping exceptions.** An exception/nonlocal exit bypassing the substituted
return leaves its pending entry until a later same/higher entry or an observed
outer return reclaims it. A new deeper call can still regard that abandoned
entry as nested and exhaust the four-slot limit; exhaustion passes unobserved.
The host fixture exercises these bookkeeping transitions. It does not exercise
Windows SEH unwinding across an actual substituted return, and no guarantee of
arbitrary escaping-exception recovery is claimed. No unwind framework or new
thread/lifetime policy is introduced. Normal observed returns, default refusal
and exact CPU/LastError preservation have authored FL-shaped x86 witnesses;
the cross-build record does not itself establish runtime acceptance.

## Port (2026-11-03)

**Outcome:** the default ID2 skip and the optional trace / negative cache / video witness run on FL through
[`media_cue_fl.cpp`](../../../src/engine/fl/media_cue_fl.cpp) and
[`media_cue_sites_fl.h`](../../../src/engine/fl/media_cue_sites_fl.h), providing `media_cue.h` in place of the AP module;
the value-only policy (`media_cue_core.h`) is AP's, unchanged. Compile-time edition: the fl target links the FL
module, the ap target is untouched. This supersedes the section 6 layout (`src/engine/common/`), which was never
merged.

- **Site.** `base+0x0b4080`, the five bytes `push ebx; push esi; push edi; mov edi,edx` claimed (one lock cmpxchg8b)
  with the two-arm gate in front. REFUSE (`ECX` = 2 and `EDX` 0 or 8, any caller, before any API or state) answers
  `xor eax,eax; ret`; PASS replays the displaced bytes at the game's ESP. With the trace or the cache on, a proceeded
  call is observed by return-slot substitution; the trampoline reserves its slot with `lea esp,[esp-4]`, so the
  outgoing EFLAGS are the allocator's (the section 4 correction, FL encoder only).
- **Classification.** Only the wrapper's own return `base+0x0b46c3` is classified, by the wrapper caller's return at
  `[esp+0xc]` (six callers); the selector `base+0x06612b` supplies kind `0x5a`, every other class prints `kind=none`.
  A return pairs by its slot address alone (FL keeps no stack id to cross-check).
- **Admission.** X3FL.exe identity (`fl::module_base()`), seven identity windows (allocator, wrapper, selector creation
  and kind store, the video consumer's head and destination lock/unlock calls; FNV-1a with the HIGHLOW dwords zeroed
  and each bound to `base` + its target, [`music_windows_fl_inc.h`](../../../src/engine/fl/music_windows_fl_inc.h)
  generated by [`generate_windows.py`](../../../verification/results/fl-music/generate_windows.py)), the six wrapper
  call sites and the allocator's one caller, all before anything is emitted or written; the jump read back; a failure
  restores the bytes. Late claims are refused. No unload path on FL (as `cull_small_parts_fl.cpp`).
- **Rows.** `media_cue_mode … edition=fl`, `media_cue_site index=0 address=… rva=0b4080 … edition=fl`, and the trace /
  window rows with `edition=fl`.

Verified (measured, 2026-11-03): [`verify_media_cue_sites_fl.py`](../../../verification/probe/verify_media_cue_sites_fl.py)
PASS on `/mnt/c/X3FL/X3FL.exe` (22 checks: windows, 91-instruction allocator with no ESP operand, register ABI, the
claimed span, caller sets, selector, video range, AP refusal); the native fixture
[`fl_media_cue.cpp`](../../../verification/probe/fl_media_cue.cpp) 20 checks, 0 failures on the real relocated bytes
(28 of 28 window changes refused; refusal flags, registers, DF, XMM7, x87 CW, MXCSR and LastError; observed-return
flags equal the native allocator's; cache scope; rollback; late window). Not verified: not flown; native Windows
game behaviour; Windows SEH unwinding across a substituted return (section 6, unchanged).
