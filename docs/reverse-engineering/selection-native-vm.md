# Selection stalls: synchronous script and native media paths

Run 27 proves a long synchronous target-notification route, but does not identify
its costly descendant. The native and compiled-script study below also finds a
concrete input-side route into the same notification. No gameplay behavior
change is justified yet. The research used an isolated worktree from `c3732c8`.
The bounded diagnostic extension is now integrated on main and independently
source-reviewed; its root-owned X3 CPU qualification is recorded below. This
qualification did not launch the game or build/install a production DLL.

## Runtime boundary

The reproducible bounded join is `/tmp/x3-run27-selection-phases.json`, produced
by `/tmp/analyze_run27_selection_phases.py` from the 245,885,736-byte Run 27 log
(SHA-256 `3df61e84408b0297c8c7ec102aa8b6466b6f4b665e6f36adedc2ca26b49f9585`).
It finds 14 active-player target changes and 12 immediate retained slow frames:
11 dominated by broad phase 6, one by phase 8. Nine dominant spans exceed 400 ms.
The selection criterion is kind 3, target-valid bit 0x40, a changed target after
excluding the first observation per (cockpit, generation), with clears retained;
all 14 also have active-player valid bit 1. Snapshot QPC is taken after checked
reads, not at the actual native target store. Subsystem log order is not time order.

The strongest join is event 62, cockpit `6fefc0f0`, target `3dee22f0`,
QPC `10595573753837`: the delayed mode 3 call starts 992.9189 ms later and lasts
458.8469 ms inside noncapture frame 20235 (485.0117 ms). Its phase 8 interval lasts
459.4922 ms with 450 ms thread CPU. The nested call accounts for 99.86% of phase
wall time, but has no independent CPU stamp. A separate pending-VM frame begins
290.416 ms after that call and lasts 477.931 ms with 470 ms CPU. None of these values
identifies a native command, script branch, resource, or codec.

Phase 6 remains `[403b09,403f2a)`: it includes registry/sector/object work,
input/control/script dispatch, an explicit input-wait branch, and optional save
work. Calling the entire interval an Input-method duration would overclaim.

## Recovering actual compiled method names

The installed EXE SHA-256 is
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`.
The existing local STORY extraction was independently matched byte-for-byte to
`addon/04.cat:L/x3story.obj`, DAT extent offset 10,102,687, length 2,312,979,
after the archive's XOR 0x33 decode. Decoded asset SHA-256 is
`ed5786a0c603802d5735fb36e0332d7c732b981d2b64dcee4174890284faff7a`.
This binds the static asset to the current installed archive, not a live task's
runtime body or save-dependent patch state.

Loader `49d030` supplies the missing representation contract. STRG is decoded
backwards: each byte after the first is `(~source[i]-source[i-1]) &255`; the first
is `~source[0]&255`. Resolver `49f180` returns a bounds-checked STRG-relative name.
CLAS contains big-endian class and method records; method rows carry CODE entry,
name offset, locals and argument count. The complete parse consumes 146,100 CLAS
bytes and finds 280 classes. Instruction widths derived from `49e1a0` consume all
1,812,069 CODE bytes (starting at offset 1), yielding 669,066 instructions; all 6,818
distinct method entries align. `49df40` resolves inheritance and sorts methods;
`49e1a0` converts operands and resolves native names; `49e4f0` rewrites instructions
in place. Raw on-disk native operands are names, not runtime group/command IDs.
Runtime CODE and task witnesses are still required before attributing an actual
call to these static names. Older notes saying names were unavailable describe
the earlier undecoded inspection, superseded by this loader-based study.

## Shared input and delayed-lock route

Native publisher `425a10`, mode 3, stores mode at cockpit+1e4 and target at+1e0.
With a nonnull view object it calls `49f4c0` at `425c71`, requesting class 0x96
`NotifyTargetLock` with target/view object IDs. After this synchronous call its
remaining work is argument cleanup and return. The delayed caller `42a45d` is
already timed by the installed phase diagnostic.

There is a second proved mode 3 caller: native command `INS_CockpitSetTracking`.
At `42dd1f` it resolves the cockpit ID; a nonzero target ID resolves through
`43a4f0`, then `42dd61` pushes the target pointer, `42dd62` sets EAX = 3,
`42dd67` sets ECX = cockpit, and `42dd69` calls `425a10`. Zero target instead uses
mode 0 at `42dd45`. This input-side mode 3 call is not the installed delayed-call
site and therefore need not emit the existing nested kind 0 event.

The compiled method graph supplies an actual target-cycling route to that command:

| Class and method | CODE entry | Relevant descendant |
|---|---:|---|
|0x96 TrackNextTarget|154dc|TrackPrevNext, or active-monitor TrackPrevNext|
|0x96 TrackPrevNext|14ecc|Object searches/distance ordering; at 15475 calls SetActiveMonitorTracking|
|0x25d SetActiveMonitorTracking|eeb2a|Dynamic SetTracking at eeb38|
|0x25e SetTracking|f1b20|INS_CockpitSetTracking at f1b49; conditional SelectMode and UpdateVisibility; TI_Interrupt|
|0x96 NotifyTargetLock|17925|Tracking notification at 17932; target-event notification at 1793f|
|0x25d NotifyTargetLock|eef2e|Reference/control checks, speech/tips branches, SetTargeted/ware-known state|
|0x133 NotifyTargetEvent|1aae26|Cue dispatch through table/randomization helpers|

Dynamic receiver identity and taken branches were not recorded in Run 27. The
static route establishes convergence, not that all 11 phase 6 stalls used this
specific hotkey or monitor class. Native Input wrappers also synchronously use
`49f570 →49f430 →4a26a0`; `49f4c0` reaches the same interpreter. Both resolve
methods through `49f330`, including binary name search and task allocation.
The tracking notification has a conditional TI_Interrupt, and the monitor
SetTracking/TrackPrevNext bodies interrupt too. This makes later queued work
structurally possible; it does not identify the later477.931 ms task.

## Cost-bearing descendants, with limits

The tracking NotifyTargetLock body conditionally reaches class 0xc8 Targeted,
Speak, or SpeakArrayWithPriority, and CheckTargetTips. The latter can invoke
class 0x277 DisplayGameTip. The separate class 0x133 cue route traverses and
randomizes event tables. Target-monitor SelectMode performs native camera/view
operations. No branch is excluded by the current trace.

Speech helpers converge on class 0xc8 SpeakArrayWithPriorityAndFaceAndDuration
(entry c66f), which formats arrays, queries voice duration, shows subtitles and
calls SpeakWithPriorityAndNoise (c8ce). That body manages task/voice queues,
can interrupt/delay, queries stream/start/length, and can call MOV_PlayMovieVoice
at CODE cc80. Names establish functionality, not a measured cost.

The MOV table at `57a344` is registered by `499b30` with native dispatcher `4997c0`.
Its command IDs and direct native paths are:

| MOV command | Native path | Observed implementation |
|---|---|---|
|11 GetVoiceStart|499a23→499710|Two hash-table lookups, returns metadata|
|12 GetVoiceLength|499a68→499780→499710|Same lookup, returns metadata+4|
|13 GetVoiceStream|499a8e→4997a0→499710|Same lookup, returns metadata+8|
|6 PlayMovieVoice|49982f→498e30|Metadata; existing-stream search or creation; seek; start|

These metadata lookups contain no resource-read/decode call. Playback is more
substantial: a missing stream can cause `498140→4cf460`, allocation, COM creation,
media-path resolution and opening/rendering. `4cf460` uses
CLSID_AMMultiMediaStream `49c47ce5-9ba4-11d0-8212-00c04fc32c45` and
IID_IAMMultiMediaStream `bebe595c-9a6f-11d0-8fde-00c04fd9189d`; these match the
local MinGW `amstream.h` declarations. Its interface+0x40 call is OpenFile.
An existing stream still goes through `4d0430` (seek paths) and `4d1870` (sample
preparation/start paths) synchronously. These calls may include COM/filter work
outside the game's archive reader; small archive-reader counters do not bound
it. No Wine-private dependency or codec implementation assumption is needed to
observe the native command boundary. No current measurement proves playback
was entered or expensive, so preloading, suppressing or replacing it is premature.

## Correct native opcode contract

The actual two-level dispatch at `4a26d6/4a2700/4a2707` subtracts 1 before indexing
a byte table at `4a4688`, then a target table at `4a4490`:

| Actual opcode | Intermediate index | Handler |
|---|---:|---:|
|82|88|4a3880 native dispatch|
|83|89|4a3986 script return|

Decompiler switch labels caused the earlier off-by-one description. Native CALL
is `4a3907`, continuation `4a3909`; all native commands share this continuation.
The exact executable-byte and table proof is independently runnable with
`python3 /tmp/x3-selection-vm/verify_opcode_dispatch.py`. The diagnostic-only
provenance check in chase_transition_core.h used 83; root independently corrected
and reviewed it in `29da5a7`. This finding does not change camera restoration logic.

## Approved bounded diagnostic extension

Reuse the existing opt-in phase diagnostic and owner thread. Add ten targeted
markers, with no global native-command or per-opcode tracing. Their proved
native calls are sufficient to separate the common publisher, playback and its
creation/seek work while retaining an honest residual:

| Marker | Bytes/length | Replay and purpose |
|---|---|---|
|403b3a|39 ae d8 04 00 00 /6|CMP: pre-input work ends|
|403dc5|f6 86 a0 04 00 00 04 /7|TEST: input/control work ends|
|425a10|53 8b 5c 24 08 /5|Entry PUSH/MOV; begin only when EAX = 3|
|425c79|5f 5e 5d 5b c2 04 00 /7|Four POPs and RET 4; end matched mode 3 publication|
|499849|e8 e2 f5 ff ff /5|CALL 498e30, rel32 at 1; begin MOV 6 playback|
|49984e|83 c4 18 5f b8 01 00 00 00 /9|Cleanup 24, POP EDI, MOV EAX,1; playback end|
|498ef8|e8 43 f2 ff ff /5|CALL 498140, rel32 at 1; create stream|
|498f00|83 ef 01 66 85 ff /6|SUB/TEST; stream-create end after caller cleanup|
|498f55|e8 d6 74 03 00 /5|CALL 4d0430, rel32 at 1; seek|
|498f5a|83 c4 04 85 c0 /5|Cleanup 4/TEST; seek end, before success/failure branch|

These byte spans are read from the installed EXE and whole instructions in the
native exports. The two phase 6 boundaries additionally have the independent
interior-edge proof in `/tmp/x3-selection-input/boundaries.json`. The updated existing site probe also proves all 33 whole spans, seven complete
code regions, both dispatch jump tables, relocated calls, exact shared joins
and the unsafe cleanup-address exclusions. Its 24 site checks and 19 affected host
tests pass. Independent source/ABI review approved the retained fixture for the root-owned
X3 run. The retained fixture subsequently passed that root-owned execution.
The publisher epilogue replay includes RET 4; its actual CPU fixture must prove
stack, return PC and callee-save preservation. Do not put a five-byte end patch
at 425c76: it would cover the incoming branch target 425c79. Likewise a create-end
patch at 498efd would cover the incoming join 498f00. The proposed joins avoid
both errors. Their incoming unmatched paths are expected and must be ignored
unless an exact begin token is open, with separate ignored-join accounting.

At 425a10 gate EAX = 3, then save ECX = cockpit, target at ESP+4, native ESP and
the return PC at ESP. The common epilogue ESP is entry ESP minus 16 after four
native register pushes.
This covers both 42a45d and 42dd69 and labels the actual caller. The original store
and callback still execute unchanged. At 425c79 close only the same owner-thread
and native-stack token; null-target mode 2 reaches this shared end without a
begin. Null-target mode 3 has a matched short interval and performs no publication
or clear. Nonnull target with no view object is a valid short publisher interval
with stores but no notification. Record that distinction using a
bounded entry witness; never dereference a saved target after its native call.

The MOV 6 begin sees six DWORD arguments on native ESP; capture their raw values
and the existing phase/frame/loop identity, not retained resource pointers.
An open publisher supplies its request witness; playback outside one has no
claimed target association, even if a previous publication is recent.
Its end still has the same six arguments before cleanup. Create begins with
stream ID at ESP and EAX = 0; at 498f00 caller cleanup has advanced ESP by 4.
Seek begins with start position at ESP and EAX = stream record; its end is before
cleanup, with EAX = result. Preserve argument/result registers, native flags,
x87/SSE state and LastError. Calls may trigger synchronous VM callbacks; use
bounded same-thread nesting and exact kind/ESP tokens. New call rows label the sampled register `endpoint_eax`: the playback marker
precedes its final MOV EAX,1 and the publisher has no proved boolean result.
`anchor=1` requires a captured valid prior Present; its frame/capture/reset values
are the preceding endpoint, and the exact QPC interval supplies the final frame
join. An unanchored first sample explicitly has `anchor=0`. A mismatched return or
Reset/device invalidation cannot close a later token. Reuse the existing
Reset/refusal diagnostic epoch and final Release handling.

QPC-only begin/end records suffice for these nested intervals; existing coarse
thread-CPU brackets retain the CPU/wait evidence. Keep aggregate counts, sums,
maxima and thresholded exact slow rows (10 ms, matching the existing call threshold) tied to the existing
Present frame, capture/reset generation and phase. Keep inclusion explicit:
creation and seek are inside playback, which may itself be inside publication.
Compute residuals from interval unions/direct-child exclusive accounting, not
by subtracting every inclusive row. Never add nested times to coarse totals.
Bounded storage, overflow/unmatched/ignored-join counts and no callback logging,
allocation or name lookup follow the existing core. Observe playback in phases 4,
6 and 8 even without an open publisher, so the later pending-VM frame is covered.

A long playback row identifies the native media command; creation and seek
rows locate two potentially expensive children. Remaining playback time includes
start/sample preparation, searches and synchronous callbacks through 498e30.
A long publisher with small playback time keeps script/cue/monitor work in scope;
it does not prove pure bytecode cost. The two phase 6 subdivisions separately
retain unrelated registry/input/save residuals. A later long pending-VM interval
with no playback also remains unexplained rather than being assigned to speech.
This deliberately stops short of another general VM tracer. The corrected
existing provenance witness can supply selected script CODE evidence independently. The task field at +3c is a class/object
context, not a method-row pointer: `49f330` stores the resolved method CODE entry
separately at task+1c. The tag-10 producer `4a8640` saves a reference-counted
context; tag-3 producer `4a8620` saves a return offset. The return handler restores
task+3c from that context and CODE base plus the return offset. Scanned tag pairs
remain candidate context/return records, not proven method-entry ancestry. Root
owns the separate provenance-label correction; this extension does not alter it.

Cost is tied to the patched sites: two extra callbacks per ordinary loop
for the phase 6 subdivisions, one entry callback for every call to `425a10`
(including non-mode-3 calls filtered inside the handler), and one callback for
every shared `425c79` epilogue arrival (including mode-2 null joins). A mode-3
request, including a rejected null request, therefore uses two callbacks.
Playback, create and seek each use two. `publisher_entries`, `publisher_filtered`
and `ignored_joins` expose these rates without logging each request. A typical
publication containing one playback and one seek adds six targeted callbacks beyond those two loop callbacks; a cache miss
adds two more. There is no cost proportional to script opcodes or all native
commands. The measured fixture overhead is recorded below; the full-loop result
includes all enabled phase diagnostics and CPU queries, while the targeted
results isolate the added request markers in paired native/disabled/enabled
synthetic executions. These are not game-frame or native-media cost estimates.
The paired targeted benchmark includes full mode 3, rejected-null mode 3 and
mode-2 null shared-join cases, rather than assuming the in-handler filter removes
stub cost. Use QPC-only accounting and benchmark the actual changed emitters, checked reads,
RET 4 replay, nested/ignored joins and enabled/disabled bursts before a candidate.
Reuse unchanged phase/Present evidence, run only affected host/CPU checks, and
review the logical delta. No broad suite or separate signature manifest is
proposed. Root approved the bounded batch; its implementation, focused source review and
root-owned X3 fixture run are complete. Actual gameplay attribution still needs
the consolidated candidate trace.

## Shared patch capacity

The independent review found a deterministic combined-install failure with the
old 8,192-byte patch arena. Run 27's selected resource-reader, phase, chase camera,
transition, lead/HUD/timing and aim hooks used 7,056 bytes. The ten new markers
add 1,524 bytes, bringing that same combination to 8,580; the second aim stub
would fail and roll its group back. Root approved a fixed 16,384-byte arena, an
extra 8 KiB once per process, with no dynamic growth or hot-path allocation.
The bounded combined-footprint witness extracts the six actual production
emitters into a host counting emitter, parses current SiteSpec lengths/counts,
and binds the real claim layout, reservation sizes and strict overflow rule.
It reproduces 7,056 and 8,580 bytes, proves rejection at 8 KiB and admission
with 7,804 bytes spare at 16 KiB. Existing admission, overflow and rollback
tests remain intact. The capacity correction is required
for coexistence, not a relaxation of site validation.

## Focused qualification

The independent source verdict is approved with no open finding. The affected
host set passes 29 tests: 19 site/capacity tests and 10 core, actual-handler and
installation tests, including 149 core assertions. All 24 site checks pass for
33 sites. The dedicated cross-build passes its production callback audit: no
exception-runtime symbols; GetLastError first and SetLastError last; qualified
state inventory fnsave 1, frstor 2, stmxcsr 1, fninit 1, ldmxcsr 2.

The root-owned locked X3 execution passed **7,606 checks, zero failures**, covering
33 emitters, 13 native-span replay cases and five actual-handler request variants.
Retained fixture SHA-256 is
`a07c947cc6679c8cd6bfcd9fcc2c0ff9e7719e1086ec9bbfbcd2b26acffce98c`
(334,818 bytes), built once by the existing `build_game_phase_cpu.py`. No DLL was
built for this qualification. The [compact result](../../verification/results/bottle-X3/selection-native-cpu-summary.json)
binds the exact executable, raw report and paired measurements. The 14,972-byte
raw report remains at `/tmp/x3-selection-native-cpu.txt`, SHA-256
`9fe303bd974f5b4f8649660bae465f02786fdff00e2609758187950c50ac93b2`.
There is no failure witness to retain.

Runtime provenance is **CrossOver Preview.app, bottle X3**, configured
`WineArch=arm64`, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`. Root used the retained
EXE through `wine_lock.py --holder selection-native-cpu --timeout 60`, with
`X3M_FIXTURE_BOTTLE=X3` as the only intentional environment assignment. The
emulation values are read bottle configuration, not independently sampled
process environment; the 76-byte stderr contains only successful msync startup.
No Steam counterpart or game launch was performed.

| Paired fixture workload | Markers / CPU queries | Native baseline (us) | Disabled (us) | Enabled (us) | Enabled minus native (us) |
|---|---:|---:|---:|---:|---:|
|Full main loop|18 / 17|20.061263|29.969531|184.557682|164.496419|
|Nonnull mode 3 request, playback/create/seek|8 / 0|1.098633|5.378906|8.276563|7.177930|
|Null mode 3 request|2 / 0|1.085286|2.144141|3.206901|2.121615|
|Null mode 2 shared join|2 / 0|1.059766|2.171875|2.652083|1.592318|

The full-loop measurement uses 3 trials ×128 batches ×4 measured loops, with one
unmeasured warmup loop per batch. Its aggregate enabled counts therefore include
34,560 handlers and 32,640 CPU queries; the final Core window has five loops and
four complete Present intervals. Target measurements use 3 trials ×32 batches
×16 requests per variant, with owner/phase admission outside the timed region
and an engine-memory cache epoch advance per request. First-sample capture is
included when reached in a measured batch. Disabled mode still traverses the
installed synthetic stubs; normal feature-off operation installs no phase sites.

All four runtime report windows have zero invalidations, unmatched tokens,
overflow, order/clock errors, foreign/suppressed callbacks, checked-read failures
and CPU-query failures. The last window's 16 ignored joins are the deliberately
untracked mode-2 null epilogue arrivals. Every branch's completed-call counts,
empty final stack and unchanged read-failure count are asserted inside the
fixture. Expected ignored joins are not misclassified as errors.

The measured full diagnostic cost is about **0.1645 ms per synthetic main loop**;
the nonnull targeted request adds about **7.18 us** in its separate paired
fixture. These measurements qualify the instrumentation under X3/FEX, not game
FPS, real voice initialization/seeking cost, or the selection-stall cause.
Native Windows runtime behavior and actual gameplay attribution remain open.
The same independent reviewer confirmed the compact record, raw artifacts,
provenance, counters and all four timing rows with no discrepancy. Evidence
review is approved and complete.

## Local research evidence

Raw exports and decoded asset material remain untracked:
`/tmp/x3-selection-vm/` contains loader/dispatch/native-media exports,
`decode_story.py`, `decode_code.py`, and the exact opcode proof;
`/tmp/x3-selection-input/input-findings.md` and `boundaries.json` contain the
independent input-side and two coarse-boundary study. At this qualification checkpoint, the installed gameplay DLL still has the
23-site diagnostic. The expanded 33-site source awaits the next candidate;
existing 23-site ABI/runtime evidence is retained alongside the new fixture
qualification.
