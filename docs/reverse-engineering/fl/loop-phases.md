# FL loop-phase diagnostic mapping

2026-10-30, original-file static analysis of FL 1.3, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
All ten AP loop recorder roles have FL equivalents. This note qualifies the
native layout and an ordinary `SiteSpec` replay shape; it does not activate
FL diagnostics or establish executed CPU preservation. No production edits,
build, native/Wine execution, game launch, install or flight timing occurred.
Raw exploratory listings stayed under `/tmp`, untracked.

Owning AP contract: [main-loop input region](../main-loop-input-region.md).
Reuse the established FL [primary frame/game mapping](phase-diagnostics.md)
and [pass adapter](pass-phases.md); this work concerns only the
ten loop stamps, not residual, light or submit families.

## 1. Native roles and minimal spans

| ID / role | AP VA | FL VA / RVA | Length / instruction sizes | rel32 offset / target |
| --- | --- | --- | --- | --- |
| 0 `sector_collide` | `0x43a38e` | `0x443b40` / `0x43b40` | 7 / 2,5 | 3 / `0x466de0` |
| 1 `sector_simulate` | `0x43a394` | `0x443b47` / `0x43b47` | 7 / 2,5 | 3 / `0x45c810` |
| 2 `sector_post` | `0x43a39a` | `0x443b4e` / `0x43b4e` | 7 / 2,5 | 3 / `0x465030` |
| 3 `sector_pass_a_end` | `0x43a3a0` | `0x443b55` / `0x43b55` | 5 / 2,3 | none |
| 4 `sector_economy` | `0x43a3be` | `0x443b74` / `0x43b74` | 7 / 2,5 | 3 / `0x463000` |
| 5 `sector_pass_b_end` | `0x43a3ca` | `0x443b82` / `0x43b82` | 5 / 2,3 | none |
| 6 `region_cutevent` | `0x403b12` | `0x477ecd` / `0x77ecd` | 5 / 5 | 1 / `0x4ab2f0` |
| 7 `region_containers` | `0x403b17` | `0x477ed2` / `0x77ed2` | 5 / 5 | 1 / `0x443b10` |
| 8 `region_sweep` | `0x403b1c` | `0x477ed7` / `0x77ed7` | 5 / 5 | none; HIGHLOW at +1 |
| 9 `region_end` | `0x403b40` | `0x477f00` / `0x77f00` | 6 / 6 | 2 / `0x478150` |

Every `ret_pop` is zero. The four seven-byte spans replay `mov ecx,esi;
call rel32`; FL passes the container in ECX without a stack argument. AP's
`push esi; call` spans must not be transplanted. Sites 3 and 5 replay
`mov esi,[esi]; cmp dword ptr [esi],0`. Site 8 reloads the universe root
through `mov eax,[0x5a82ec]`. Site 9 replays the native `jne` immediately
after the existing game diagnostic's input-body marker.

FL driver `[0x443b10,0x443b8f)` makes two walks over `[universe+8]`.
Pass A runs only when `[universe+4] != 0`. Both passes require container
word `+0x48 == 1` and clear byte `+0x148 & 1`. Pass A calls collision
`0x466de0`, commands/simulation `0x45c810`, then post/media selection
`0x465030`; pass B calls integration/tick `0x463000`, then attach/transform
`0x45c3b0`. These identities agree with the existing
[ship steering](ship-steering.md),
[collision](collision-box.md), and
[media-cue](media-cue.md) studies. The historical recorder
label `economy`/`passb` continues to measure both pass-B calls; it does not
establish that all FL work there is economy simulation.

The main-loop input region `[0x477ec4,0x477ef9)` calls scene-animation
update `0x4ab2f0`, this driver, then sweep `0x464fd0` for each class-1
container. The latter walks 32 class buckets at stride `0xc`, tests object
`+0x40 & 0x08000000`, clears matching universe `+0x38`, and calls deletion
helper `0x435820`; it returns at `0x465025`. The legacy `cutevent` name
denotes the same top-level animation family as AP; no FL growth or cost is
inferred from AP run381.

The shared chains therefore remain 0→1 collision, 1→2 simulation, 2→3 post,
4→5 pass B, and 6→7 animation, 7→8 whole driver, 8→9 sweep. The sector
chain nests inside the region chain. No recorder IDs, accumulator/window
layout, field names, or frame attribution need change.

## 2. Boundaries, incoming edges, switch and relocation evidence

[Producer](../../../verification/results/fl-loop-phases/produce.py) uses the
project PE32/Capstone analysis tools and existing conservative edge/literal
scanners. [Derived result](../../../verification/results/fl-loop-phases/mapping.json)
retains span hashes, native operand witnesses and compact refusal/replay
results, not native byte dumps. Reproduce from the repository root:

```sh
python3 verification/results/fl-loop-phases/produce.py
python3 -m py_compile verification/results/fl-loop-phases/produce.py
```

Measured static decode: driver **127 bytes / 45 instructions**, complete main
loop `[0x477ba0,0x478688)` **2,792 bytes / 690 instructions**, and the known
main-loop caller **5 bytes / one instruction**. All ranges decode gap-free;
the containing routines have no computed jump or switch table. The driver
also has no indirect CALL. The original main has 89 HIGHLOW fields and the
driver one; only the root operand at `0x477ed8` intersects a new hook span.
All ten spans are the smallest whole-instruction spans of at least five bytes
at their selected semantic boundaries and are pairwise disjoint.

Exactly four direct instruction edges enter the new spans, all at a start:
`0x443b35` and `0x443b3e` → `0x443b55`; `0x443b69` and `0x443b72` →
`0x443b82`. The back edges `0x443b5a` → `0x443b30` and `0x443b87` →
`0x443b64` stay outside all spans. The conservative scan at every executable
text byte offset finds exactly those four candidates, with no unresolved
source boundary or interior edge. A whole-image dword census finds no literal
pointer to any byte of a span. These scans close surveyed static direct and
literal-address entries, not arbitrary computed entry or third-party patches.

Pause `0x477ecb` and empty-list `0x477ee2` both reach input-body CMP
`0x477ef9`; sweep back edge `0x477ef7` falls through there. The CMP is the
seven-byte existing game-phase site 23, so loop site 9 starts at its next
instruction `0x477f00`, outside that claim. All exits of the region cross
site 9, including the pause path with no region interval open. Existing
walk-marker behavior treats that as a normal empty chain.

The producer checks **60 tail cases**, ten sites × two loaded bases
(preferred and +`0x01000000`) × three arena addresses (`0x10000000`,
`0x71000000`, `0xf1000000`). The binder must add the admitted load delta to
site 8's absolute field before installation. Relative native call/branch
fields stay unchanged in loaded image memory; the claim then re-bases their
arena tails, preserving loaded absolute destinations and final continuation.
No short branch rewriting, paired end stamp, register-context decoder or
custom emitter is required. This is a decode/target proof, not CPU execution.

**15 in-memory mutation negatives** bypass the exact image digest and are
rejected by operand, boundary/edge, relocation or ABI checks. They include
the ECX receiver, collision target, class/skip gates, both join interiors,
walk CMP, root address and its missing HIGHLOW, region condition/target,
pause bypass, an introduced computed jump, changed sweep cleanup, and the
known external main-loop CALL redirected into site 8's operand. AP's existing
loop verifier independently passes **24 checks** against the named AP file.

## 3. ABI, native ESP, CPU liveness and exceptional paths

Driver entry `0x443b10` saves EBP and assigns it the incoming ESP minus four,
aligns ESP down to eight at `0x443b13`, subtracts eight, then saves ESI/EDI.
Its ordinary body depth is **D = (EBP & ~7)−16**. All six sector sites have
ESP=D: no argument push, caller cleanup or other ESP writer occurs between
the prologue and epilogue. The five ECX-container calls have no caller stack
arguments; all surveyed return sites in their existing mapped bodies are
plain RET. The producer records those return witnesses explicitly. This
establishes the ordinary caller contract; it is not a fresh exhaustive
callee callback, return-address-inspection or exceptional-exit audit.

At the four region sites, the main-loop normal frame depth is
**M = EBP−0x140**: three SEH words, `sub esp,0x128`, saved EBX/ESI, then saved
EDI at `0x477e7f`. The loop back edge retains that depth; exit `0x4785a0`
pops EDI, with ESI/EBX restored at `0x47867b/c`. The region has no push or
ESP adjustment. Animation and driver take no stack argument and have plain
RET; the sweep takes ECX=ESI at `0x477eeb`, also with plain RET. Thus no
pending stack argument exists at any of the ten stamps. Each displaced CALL
executes only after native ESP is restored, pushes its arena return address,
and returns to its tail before the continuation jump.

ESI=current container and EDI=universe are live across sector stamps; EBP/ESP
retain frame identity. ECX must be preserved by observation and then assigned
ESI by the copied instruction before its call. At region stamps EBX is game
context; EBP/ESP, native ESI/EDI and every other live register remain native
state. EAX is overwritten by site 8 itself. Incoming arithmetic flags are
dead at sector stamps and region sites 6–8. Sites 3/5 produce the flags their
native `jne` back edges consume. Site 9 has **live incoming flags** from
`cmp [ebx+0x4d8],1`; restoring them before replay is mandatory for both the
taken and fall-through cases.

Reuse the existing `lean_stub::emit`: save EFLAGS and EAX/ECX/EDX, clear DF,
save XMM0–7, call the four-byte-stack-compatible cdecl helper, then restore
all of that before tail replay. EBX/ESI/EDI/EBP survive by the helper ABI;
`LightCallBoundary` preserves MXCSR/LastError. Keep the unchanged helper
callgraph free of x87, allocation, logging and locking. The lean envelope
does not save x87; source/compiler/no-x87 evidence remains its precondition.
Frame's existing FL `mov ecx,esi; call` replay demonstrates the ordinary
installer shape, but its full CPU bridge is not the lean loop bridge, so it
does not alone claim executed FL loop preservation.

The direct CALL census finds one caller each for main (`0x477a9a`), driver
(`0x477ed2`), animation (`0x477ecd`), sweep (`0x477eed`) and each of the five
driver callees. This supports their ordinary main/Present-thread scheduling.
Retain the existing owner gate; reject/count early and foreign hits. A sole
direct caller does not prove same-thread callback exclusion. Script/media
work below the driver is a callback frontier and arbitrary reentry into this
main loop is not disproved. The accumulator has one open interval per chain,
not a nesting stack: reuse orphan/discard handling; do not grant a general
reentrancy promise or retain a native container pointer in the helper.

Main SEH handler `0x54d872` is linked through FS:[0] at `0x477bb1` and
restored at `0x47867d`. The sector driver has no local SEH registration.
A native exception that unwinds past a closing stamp leaves an incomplete
chain; use existing frame discard/orphan behavior without synthesizing a
close. No engine reference, borrowed pointer or cleanup lease is introduced.
Exceptions originating inside injected helper/stub code and return-address
sensitivity inside the native callees remain unqualified; the helper must
stay nonthrowing. No alternate native SEH/unwind ABI is proposed.

## 4. Smallest binding, collisions and actual initialization order

The adapter needs ten FL rows in the existing ID order, checked FL 1.3
recognition/base/RVA resolution, and site 8's one HIGHLOW adjustment. Use the
existing frame-style binder and `stamp::install_group` with unchanged lean
emitter, rollback and last-step activation. Keep the AP gate separate;
widening `object_trace::executable_verified()` would admit unrelated families.
Read/qualify any bounded native semantic pins once in the existing startup
owner, then preflight all ten live spans at the actual claim boundary.
The complete main decode/digest above is original-file edge evidence: it is
not a requirement to duplicate a 2,792-byte live hash or introduce a new
profile framework. If a compact cold context fingerprint is needed, driver
127 bytes plus region 66 bytes cover the reviewed local gates/joins; perform
it before native owners write those spans, normalizing the two reviewed
HIGHLOW operands by subtracting the admitted delta, not by masking operands.

The producer compares against **76 current physical spans**: primary game
30, frame ten, pass four, all 21 UI/common/display descriptors, seven
music-keep spans, plus music-alert/material/point-light/compositor. All are
disjoint from the ten new spans. Most relevant are game input marker
`[0x477ec4,0x477ecb)`, game input-body marker `[0x477ef9,0x477f00)`, and
UI direct site `[0x477fda,0x477fdf)`. The UI site is inside the original main
decode, so a late whole-main hash would reject an existing owned write.
The separate studied refresh join `0x477e6d` is not a current UI claim.
This named inventory is not a census of every future optional engine patch.

**An ordering change is required for FL activation.** `fl_startup::body`
recognizes the image and loads configuration at `src/engine/fl/fl_startup.cpp`
lines 369/373; current pass qualification immediately follows at line 377,
before native preparation and Complete publication. That existing exclusive
stage can carry any small immutable loop qualification without a new owner.
Backend initialization then runs in `initialize_log`: game at
`src/proxy/capture.cpp:6825`, frame at 6826, pass at 6828, and loop at 6836.
**Music alert initializes later, at 6941.** The startup `music_keep` preparation
is a different feature and does not qualify music alert.

`music_alert::windows_match` calls `music_alert::fl::validate` during its
installation. Its FL profile hashes the entire driver `[0x443b10,0x443b8f)`
and the region driver CALL `[0x477ed2,0x477ed7)`. New loop claims at the
current call position would make that validation fail. The smallest sequencing
option is to install the optional FL loop group after the existing music-alert
initialization, while the same install window is still open; AP need not move.
Alternatively, an already checked immutable music admission can precede loop
writes, but this study does not implement or authorize skipping those checks.
Loop diagnostics must also work when music alert is disabled. Existing
game-phase interactions with the music profile are separate pre-existing
ordering concerns; moving loop alone does not establish their compatibility.

Once selected, the hot recorder remains exactly active/owner checks, QPC and
value accumulation: no new per-stamp native reads, code validation, allocation,
lock or branch by edition. Static dispatch rate is four per active pass-A
container, two per active pass-B container, one join for each skipped container
per visited pass, plus three region starts and its end on a normal unpaused
frame. Paused frames hit only region end. Counts and FL cost remain unmeasured;
the existing 91 ns model is AP/X3-bottle provenance, not a measured FL figure.

Later implementation acceptance should target the changed binding, loaded-base
arithmetic, disabled/foreign admission, transaction rollback and the changed
FL replay composition. Reuse unchanged recorder/lean-stub evidence; add a
native fixture only for a CPU/replay contract not already covered. No broad
benchmark, native FL game support claim, or additional game run follows from
this static map.

The later adapter, ordering fix and host/x86/native synthetic evidence belong
to the [FL loop verification ledger](../../verification/farnhams-legacy-loop-phases.md).
They do not extend this original-file study's callback, unwind or live-delivery
limits.
