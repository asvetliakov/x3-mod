# FL residual diagnostic boundaries

Offline qualification, 2026-10-30, recognized FL 1.3 original executable,
SHA-256 `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
The two AP residual roles have FL equivalents using the existing shared
installer, lean stub and value-only accumulator. This is an implementation
plan and static native contract, not installed or executed FL support.
No production edits, build, native/Wine execution, game launch or install.

| ID / role | FL preferred VA / RVA | Span / instruction sizes | Replay metadata |
| --- | --- | --- | --- |
| 0 `material_setup` | `0x4e3d91` / `0xe3d91` | 8 / 2,6 | `ret_pop=0`, `rel32_offset=0`, no HIGHLOW |
| 1 `view_particles` | `0x48bae6` / `0x8bae6` | 5 / 5 | `ret_pop=0`, `rel32_offset=0`, HIGHLOW at +1 |

Material replay loads EAX from `[EDI]` then forms ECX=`EBP-0xbc`.
Particle replay loads EAX through native global `0x5a83b0`. Generate expected
loaded bytes by adding the admitted module delta to that one absolute operand;
`engine_patch::claim` copies those loaded bytes and appends its ordinary jump
to the loaded continuation (`site+8` or `site+5`). Neither span contains a CALL,
relative branch, push, cleanup or return-address-dependent operation. No custom
emitter, branch rewriting, register-context helper or native pointer argument
is needed.

## Boundaries, guards and native interval semantics

The material boundary has four direct incoming edges in the complete routine:
`0x4e1b34`, `0x4e1d73`, `0x4e1d7c`, `0x4e3d21`; all land at its start.
`0x4e1b34` skips initialization when material `+0x34` is already populated on
the native `object+0x130 & 0x20000` path. `0x4e1d73` exits that initialization;
`0x4e1d7c` skips the other initialization when material `+0x2c` is populated.
`0x4e3d21` skips the optional parameter loop when its handle is null. The
fall-through parameter loop ends with CMP/JL at `0x4e3d89/8c` and restores ESI
at `0x4e3d8e`. Earlier argument/null/visibility guards can leave without
crossing this stamp; a stamp is a surviving material setup boundary, not a
successful draw count.

EDI is the selected effect, loaded from `[EBP-0x18]` at `0x4e1abc`.
Immediately after the span, `push 1; push ecx; push edi` supplies
`Begin(effect, &pass_count, D3DXFX_DONOTSAVESTATE)`. Dispatch loads vtable
slot 63 (`+0xfc`) at `0x4e3d9d` and calls it at `0x4e3da3`.
Its output `[EBP-0xbc]` is read by the initial pass-count CMP at `0x4e6005`
and the pass back-edge CMP at `0x4e6067`. The HRESULT is overwritten at
`0x4e3da5`. Begin can therefore produce no later pass stamp; retain existing
`setup_skipped` behavior rather than pretending every setup draws. These
semantics and material lifetime reuse the [pass study](pass-phases.md)
and [material observer](material-submit.md).

Frame `view_submit_end` is `0x48ba9b`. Its reset loop stays inside the interval;
`0x48bad2` tests camera `+0x270 & 0x4000`, and `0x48badc` skips particles
**onto the residual span start**. The called path assigns ECX=`[ESI+0x1c]`
at `0x48bade`, calls particles `0x4dff50` at `0x48bae1`, then falls into the
same stamp. The native identity reuses the
[depth-writer study](depth-writer-selection.md). There is no
push or ESP adjustment between submit-end and this boundary. All direct
branches in that interval stay in it or join its closing boundary.

This differs from AP, whose particles skip bypasses its residual stamp.
For FL, `particle_views` counts **closing view boundaries, including skips**;
it must never be presented as the number of executed particle calls.
The shared interval remains meaningful: fresh `submit_end` → closing boundary
covers reset bookkeeping, the native gate and any executed particle pass.
On a skip it measures that shorter bookkeeping/gate interval. No native pointer
read or extra branch is needed in the diagnostic helper.

A source audit of `residual_phases_core.h` confirms that `view()` pairs only
with a fresh nonzero submit-end clock, adds `now-submit_end`, and counts
`view_skipped` for a missing/repeated clock. It has no open particle scope to
leave unmatched on a skip. `unmatched` is reserved for an invalid site ID.
`other` subtracts the measured particle interval, not `particle_views`, from
views minus setup/submit; its existing underflow clamp/counter remains intact.
The count is used for reporting/window reduction and the dispatch self-cost
model, whose dispatch count should include skipped views. No timing division
assumes an executed-call count. Prepare/setup normalization uses pass count.
The existing 109 ns dispatch constant is AP/X3-bottle provenance, not measured
FL overhead or a new FL performance claim.

## Static entry, replay and overlap evidence

[Producer](../../../verification/results/fl-residual-phases/map_sites.py) and
[derived result](../../../verification/results/fl-residual-phases/mapping.json)
use the project PE32/Capstone tools and reuse the existing whole-text
control/literal scanners. Run from the repository root:

```sh
python3 verification/results/fl-residual-phases/map_sites.py
python3 -m py_compile verification/results/fl-residual-phases/map_sites.py
```

Complete material/frame routines, the known material wrapper and particle
routine decode gap-free; bounded caller and SEH entries supply ABI witnesses.
The material routine has one previously qualified switch at `0x4e4f91`;
all five entries and its relocation fields are checked, none entering a stamp.
The frame has no computed jump. Whole-text direct-control scanning finds only
the named five incoming edges plus one short-branch pattern inside instruction
`0x48ba96`; apparent source `0x48ba97` is not an instruction boundary. No
whole-image literal targets any byte of either span. This closes the surveyed
static direct/literal entries, not arbitrary computed entry or third-party code.

Measured checks: **12 relocated-copy cases**, two loaded bases × three arena
addresses per site, including the HIGHLOW rebase and final jump-back target;
**21 mutation/dependency negatives** bypass exact EXE digest and reject changed
Begin/count/effect arguments, material guards, particle gate/receiver/target,
skip/interior edges, global operand, native cleanup/ESP, submit bypass,
computed entry, switch/literal targets, external CALL entry, missing HIGHLOW
and overlapping claim. AP's existing residual verifier independently passes
**17 checks** on `/mnt/c/X3AP-orig/X3AP.exe`.

The producer compares **95 current physical spans plus one conservative
reserved FL lens window**. It reuses the established 76-span primary
(game/frame/pass/UI/music/material/point-light/compositor) inventory, adds the
ten installed loop spans and six camera spans, sun lens CALL and both LOD
byte writes. The reserved 15-byte FL cull lens window remains AP-only in
`cull_site::admit()` and is not counted as an installed FL claim. All are
disjoint. Additional owners in the containing routines are explicitly covered:
UI frame entry, ordinary/cube/auxiliary/special camera activation, compositor,
frame/pass stamps, sun lens and material LOD byte. Material observer CALL
`0x4e7142` and point-light `[0x4e4915,0x4e491b)` are also disjoint.
This named inventory is scoped ownership evidence, not a guarantee about a
future optional patch or unknown third-party modification.

## CPU, stack, lifetime, reentrancy and exceptions

Material ordinary body ESP is **E=EBP-0x5c0**, derived from its aligned EBP,
three SEH words, saved original-stack EBX, local allocation and saved ESI/EDI;
both epilogues anchor that depth. This reuses the established material/pass
normal-call accounting. At setup there is no pending argument: its replay
only loads/forms addresses. Begin's next three pushes leave E−12, and its
COM stdcall normal return restores E. Private calls earlier in material
initialization retain the existing ABI evidence/limits from the pass study;
this is not a new exhaustive vtable implementation audit.

Frame ordinary ESP is **F=EBP-0x5c**: three SEH words, `sub esp,0x44`, then
saved EBX/ESI/EDI. Its terminal pops at `0x48be23..25` anchor the body depth.
The particle callee consumes its receiver from ECX (`0x4dff76` saves it in
EBX); its two returns `0x4e0044`, `0x4e0604` are plain RET. Both called and
skipped paths reach the stamp at F. AP's argument cleanup must not be copied.

EDI effect and EBP frame identity are live at material setup; ESI retains
native material state, and EBX retains its original stack anchor. EAX/ECX
are overwritten by replay but must survive observation until replay. At the
particle join ESI remains the camera; the replay replaces EAX before TEST
`0x48baeb`. Incoming arithmetic flags are dead at both sites: material reaches
a standard call before a native flags consumer, and particle replay reaches
that TEST. Preserve all flags, including DF, regardless. Preserve other GPRs,
ESP and XMM0–7 through the existing lean envelope; the helper preserves
EBX/ESI/EDI/EBP by the x86 ABI and realigns a four-byte incoming stack.
`LightCallBoundary` preserves MXCSR/LastError. Keep its callgraph nonthrowing,
without x87, allocation, locking, logging or engine reads. No x87 preservation
or new SIMD permission follows from these sites.

Whole-text direct CALL census has one decoded caller each: material
`0x4e7142`, frame `0x4782cf`, particles `0x48bae1`. This supports ordinary
frame-thread delivery, not callback/reentry exclusion. Retain the existing
Present owner gate, early/foreign counters and shared frame discard rules.
D3DX/device calls remain callback frontiers. The residual accumulator is not
a nesting stack; arbitrary same-thread recursive rendering is unqualified.
No diagnostic state borrows or retains an effect/camera/native pointer.

Native FS:[0] registration remains live: material handler `0x54ef51`, frame
`0x54e1e8`, particles `0x54ef20`. Their bounded entries load metadata
`0x59f074`, `0x59dcac`, `0x59f030`, respectively, and jump to common handler
`0x521368`. Native exceptions that bypass a closing stamp leave an incomplete
chain; reuse skipped/discard behavior without inventing a close. A fault in
the copied memory load reports an arena EIP. These handler entry witnesses do
not prove exception dispatch/unwind through injected helper code or continued
faults in relocated loads; that remains an explicit implementation-fixture
limit. Ordinary replay adds no displaced CALL or altered native return address.

The material five-byte write fits one aligned qword (offset 1). The particle
write starts at offset 6 and straddles one; it uses the shared plain-copy path.
Claim and rollback must remain in the actual exclusive pre-Present install
window. Atomicity of the first site does not make late/nonquiescent installation
safe. Reuse the shared all-span preflight, reverse rollback, arena/protection
recovery and last-step activation; no alternate patch writer is needed.

## Smallest cold binding and remaining acceptance

Resolve exactly the two rows above against the already recognized FL 1.3 image,
with checked loaded-base/RVA bounds. Reuse `engine_profile::recognized_image()`;
do not widen `object_trace::executable_verified()`. Keep active frame/pass
groups as prerequisites, as AP already does. Their immutable pass qualification
already binds the entire material body, count readers and guards before native
point-light/LOD writes. A second whole-body live hash is unnecessary.

The producer provides just **109 bytes** of exact residual context inputs:
Begin `[0x4e3d91,0x4e3da5)` (20 bytes, no HIGHLOW), and submit-end/particle join
`[0x48ba9b,0x48baf4)` (89 bytes, HIGHLOW offsets 2 and 76).
Their derived FNV-1a values/fields are in the result. Normalize by subtracting
the admitted load delta, preserving exact preferred operands rather than masking.
Check once in existing `fl_startup::body` Preparing stage after recognition and
configuration, beside the current pass/loop qualification, before any native
prepare. This does not introduce an identity service, broad global gate, or
per-stamp validation. Failure disables only this optional residual diagnostic.

The frame context includes frame stamp 9; a later context hash would reject
that owned write. Retain the earlier immutable qualification, then let
`stamp::install_group` preflight only the two actual untouched live spans at
the current residual initialization after frame and pass. Existing point-light
and dust-light validation windows are disjoint; no additional music-order
change is required by these two residual writes. The shared handler and
accumulator need no edition branch or extra hot-path operation.

Implementation acceptance still needs its independent source/evidence review,
loaded-base/read-failure admission checks, both live-span preflight/rollback
cases, disabled/early/foreign behavior and the relevant generated-stub
CPU/flags/ESP/MXCSR/LastError/no-x87 checks. Reuse unchanged shared-stub evidence;
add the FL absolute-load tail and skip-boundary pairing witness. Native exception
continuation through replay remains unverified until a meaningful fixture covers
it. No native Windows game behavior, CrossOver FL delivery, game timings or FL
cost is established by this offline map.

## Adapter implementation evidence

The shared adapter and bounded host/x86 checks are recorded in the
[feature ledger](../../verification/farnhams-legacy-residual-phases.md).
The offline counts above remain original mapping evidence; runtime acceptance
and its limitations are recorded separately there.
