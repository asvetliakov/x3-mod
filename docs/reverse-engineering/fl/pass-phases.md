# FL effect-pass stamps: exact boundaries and shared-installer binding

Offline study, 2026-10-30. FL `1.3` input: `/mnt/c/X3FL-orig/X3FL.exe`,
SHA-256 `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
AP reference: `/mnt/c/X3AP-orig/X3AP.exe`, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`.
Addresses below are preferred VAs, image base `0x400000`; production binding
must use the admitted image's loaded base plus RVA. No production edit,
build, native/Wine execution, game launch, install or runtime timing is claimed.
Raw native listings stayed under `/tmp` and untracked.

The four AP pass roles have exact FL equivalents in material submission
`0x4e0f30..0x4e60ca`. Their meaning and order are unchanged: `BeginPass`, one
indexed draw, `EndPass`. FL-specific instruction spans are required; neither
AP addresses nor AP stack/local offsets apply. These are optional high-rate
diagnostics; the existing AP-only executable gate remains unchanged by this
study. The owning AP contract is [effect-pass-loop.md](../effect-pass-loop.md).

## 1. Four sites and semantic equivalence

| ID / role | AP VA | FL VA / RVA | Length / instruction sizes | Displaced operations, in order |
| --- | --- | --- | --- | --- |
| 0 `pass_begin` | `0x4c3ff0` | `0x4e6010` / `0xe6010` | 10 / 2,1,1,6 | Load effect vtable through EDI; push ECX pass index; push EDI effect; load slot `+0x100`. |
| 1 `pass_applied` | `0x4c4000` | `0x4e601c` / `0xe601c` | 9 / 3,6 | Replace ESI with `[ESI+0x14]` geometry; load EDI device from `[EBP-0xa8]`. |
| 2 `pass_drawn` | `0x4c403e` | `0x4e604f` / `0xe604f` | 6 / 3,1,2 | Reload EDI effect from `[EBP-0x18]`; push EDI; load its vtable into EAX. |
| 3 `pass_end` | `0x4c4049` | `0x4e605d` / `0xe605d` | 6 / 3,3 | Reload ECX index from `[EBP-0x2c]`; restore ESI material from `[EBP-0x34]`. |

Each is the smallest whole-instruction span of at least five bytes at its
semantic boundary. All four are plain copies: `rel32_offset=0`, no relative
control transfer, no absolute HIGHLOW field. Site 0 resumes at `0x4e601a`, site
1 at `0x4e6025`, site 2 at `0x4e6055`, site 3 at `0x4e6063`.

`Begin` at `0x4e3da3` uses effect slot 63 (`+0xfc`), passes flag 1
(`D3DXFX_DONOTSAVESTATE`), and writes its count to `[EBP-0xbc]` through the
LEA at `0x4e3d93`. The geometry slot-4 call at `0x4e5ffa` guards the pass loop;
zero skips to effect `End`. `0x4e6002` initializes index `[EBP-0x2c]`; the
unsigned comparison at `0x4e6005` skips zero-pass techniques.

The measured intervals would contain the following native work:

| Interval | FL native dispatches | Meaning |
| --- | --- | --- |
| apply: 0 → 1 | `0x4e601a`, slot `+0x100` | `ID3DXEffect::BeginPass(effect,index)` only. |
| draw: 1 → 2 | `0x4e602d`, geometry `+0x10`; `0x4e6038`, geometry `+0x14`; `0x4e604d`, device `+0x148` | Geometry count queries, then direct `DrawIndexedPrimitive(device,4,0,0,NumVertices,0,PrimitiveCount)`. |
| end: 2 → 3 | `0x4e605b`, slot `+0x108` | `ID3DXEffect::EndPass(effect)`. |

`0x4e6063` increments ECX, `0x4e6064` stores it, `0x4e6067` compares the same
count slot, and `jb 0x4e606d → 0x4e6010` repeats. `End` follows at
`0x4e6078`, slot `+0x10c`. No `+0x104` memory operand occurs anywhere in this
material body; there is no native `CommitChanges` dispatch in the surveyed
routine. One normal loop iteration issues exactly one draw, so the diagnostic
would dispatch four times per material-path draw. This is a static rate
relationship, not a measured FL draw count or a flight-cost estimate.

The device local is assigned once at `0x4e0fb6` from
`[[0x5aaf94]+0x18]`'s first word. The same object receives
`SetSoftwareVertexProcessing` at `0x4e0ff8`, slot `+0x134`, and the draw's
`+0x148` slot. EDI changes between effect, device and device-vtable roles;
the table deliberately describes each site's actual role.

## 2. Instruction, incoming-edge and relocation proof

[Producer](../../../verification/results/fl-pass-phases/produce.py) reuses the
project PE32/Capstone tools and AP verifier. [Compact derived result](../../../verification/results/fl-pass-phases/mapping.json)
contains span digests, operand witnesses, relocation offsets and refusal reasons.
Run from the repository root:

```sh
python3 verification/results/fl-pass-phases/produce.py
python3 -m py_compile verification/results/fl-pass-phases/produce.py
```

Measured static results: FL body **20,890 bytes / 6,421 instructions**, gap-free
from known entry through the last RET; **86 semantic pins**; **four spans**;
**24 relocation/replay cases** (four sites × two loaded bases × three arena
addresses); **11 rejected in-memory mutations**. AP's existing verifier also
passes **15 checks** over its **4,695-instruction** body. The script checks
JSON before writing it; no raw native bytes are emitted into this evidence.

The only direct instruction edge landing anywhere in the four FL spans is
`0x4e606d → 0x4e6010`, at the span start. No direct edge enters an interior.
The other three sites follow their native calls. The conservative raw scan
covers every `.text` byte offset for near/short direct branch/call forms. It
finds one additional candidate, `0x4e5fbf → 0x4e6018`, disproved as an
instruction edge by the complete body decode: `0x4e5fbf` lies inside the
immediate of the instruction beginning `0x4e5fb5`. No candidate has an unresolved
source boundary. A whole-image dword census finds no literal reference to any
byte of any span, and no relocation field overlaps a span.

Unlike AP, FL has one indirect jump in the body: `0x4e4f91`, indexed by EAX
after `dec eax; cmp eax,4; ja 0x4e513a`. Its five-entry table at
`0x4e60cc..0x4e60e0` targets `0x4e50ec`, `0x4e5097`, `0x4e5042`,
`0x4e4fed`, `0x4e4f98`. Every target is a decoded instruction start, disjoint
from the spans; the table operand and all five entries have type-3 HIGHLOW
relocations. The body has **295 HIGHLOW fields**, all outside the four spans.
The preferred-byte body digest is
`b00a18ecf570a6c18abd337bb2e65612d153633ec7be4163016922e7dc66bcb7`;
this matches the earlier material-submit study. The result also records a
zeroed-HIGHLOW digest; zeroing is an analysis fingerprint, not a proposed
loaded-code validator's normalization rule.

Replay copies each span byte-for-byte at `0x10000000`, `0x71000000`,
`0xf1000000`, at preferred load and load `+0x01000000`, then appends the shared
installer's continuation jump. All instruction sizes and continuation targets
remain correct. This tests instruction relocation, not CPU execution.

Every mutation bypasses exact EXE/body digests. Refusals cover all four site's
register/local operands, `BeginPass`/draw/`EndPass` slot changes, the count
slot, the back edge redirected into an operand, a switch target redirected
into an operand, and the real wrapper CALL at `0x4e7142` redirected to a span
interior. The latter's source is independently decoded in the bounded wrapper
`0x4e6ef0..0x4e7162`; it fails the whole-text edge check. Unknown computed
private-address entry or third-party patches remain outside this static proof.

## 3. Native ESP, CPU liveness, callbacks and exceptions

The FL entry ABI and copied material scope remain owned by
[material-submit](material-submit.md). At `0x4e0f31`, EBX retains
the original stack after saved EBX; EBP is assigned after native 16-byte
alignment at `0x4e0f44`. Three SEH words, saved EBX, `sub esp,0x5a8`, then saved
ESI/EDI put the ordinary body frame base **E = EBP−0x5c0**. EDI is saved at E,
ESI at E+4. Both terminal paths pop those slots before `mov esp,ebp`
(`0x4e60a9/aa/ab`, `0x4e60ba/c2/c3`), independently anchoring the ordinary
body depth. The straight normal path from the pass tail to the terminal pops
has no stack adjustment except effect `End`'s balanced push/CALL.

All four sites have ESP=E on the normal native path, with no pending argument.
The concrete loop accounting below uses ESP immediately **before CALL**, hence
excludes the return address pushed by CALL itself:

| Call | ESP before CALL | Callee-cleaned argument bytes | ESP after normal return |
| --- | --- | --- | --- |
| `BeginPass`, `0x4e601a` | E−8 | 8, documented COM stdcall | E |
| geometry slot 4, `0x4e602d` | E−4 | 4, inferred this-only | E |
| geometry slot 5, `0x4e6038` | E−12 | 4, inferred this-only; two early draw args retained | E−8 |
| draw, `0x4e604d` | E−28 | 28, documented D3D9 stdcall | E |
| `EndPass`, `0x4e605b` | E−4 | 4, documented COM stdcall | E |
| `End`, `0x4e6078` | E−4 | 4, documented COM stdcall | E |

The private geometry methods' `this`-only cleanup is an ABI inference from
these exact pushes and the documented seven draw arguments, with no caller
cleanup or stack adjustment between them. Their possible implementation
vtables have not been independently enumerated. EBP-relative locals alone
would not establish ESP; the saved-pop anchors and call accounting supply the
additional evidence. Native COM failure HRESULTs are ignored by this loop;
normal returns still cross the next stamp regardless of HRESULT value.

At site 0 ECX is the live pass index, EDI the effect, and ESI the current
material; at site 1 ESI is still that material and must survive observation
before its geometry load. At site 2 EDI is the device vtable, then the replay
reloads the effect; site 3 restores the next-iteration ECX/ESI state. EBX and
EBP retain original/aligned frame identity throughout. Incoming EAX HRESULTs
are dead before the next use; preserving all registers remains required by the
shared stub contract. Incoming arithmetic flags are dead at every site: there
is no branch/flag consumer before the next native arithmetic in the tail,
and `cmp 0x4e6067` establishes the loop's branch flags. Preserve EFLAGS,
including DF, regardless; do not treat native EAX/flags as callback scratch.

The existing lean stub saves flags, EAX/ECX/EDX and XMM0–7, clears DF for its
cdecl helper, and restores them before tail replay. The helper preserves
EBX/ESI/EDI/EBP by the x86 ABI, realigns its four-byte incoming stack, and uses
`LightCallBoundary` for MXCSR/LastError. The no-x87 callgraph audit remains a
required implementation check; the lean stub intentionally does not save x87.
The native material body's SIMD use grants no extra scratch permission.

**The pushed instructions are safe only after restoring native ESP.** Site 0's
copied pushes leave E−8 for the original `BeginPass` CALL; site 2's copied push
leaves E−4 for the original `EndPass` CALL. They must never run against the
stub's temporary save area. The existing `jmp [next]` after register/flag/ESP
restoration and the claim tail's final continuation jump provide exactly that
shape. Sites 1 and 3 contain only loads. This study verifies the copied
instructions and destinations; it adds no synthetic claim of executed CPU
preservation.

Reuse the established owner-thread frame diagnostic gate and the existing FL
[callback exclusion contract](ui-render-exclusion.md), through
the material/complete-render cone already documented in material-submit §3.
Recursive scene traversal does not imply nested material-call execution on
its normal path. D3DX/device/geometry methods remain callback frontiers;
arbitrary callback reentry into rendering is not disproved here. The shared
accumulator has one open interval, not a nesting stack, and the thread gate
does not protect same-thread nested rendering. No new general reentrancy
support or whole-engine exclusion is established by these four stamps.

Native SEH remains linked throughout the loop (handler `0x54ef51`, linked at
`0x4e0f54`; unlinked at the two native epilogues). A native call that unwinds
past a closing stamp leaves an incomplete interval; reuse existing orphan/
frame-discard behavior and do not synthesize a successful closing timestamp.
The diagnostic retains no engine pointer or lease, installs no scoped native
borrow, and has no cleanup that dereferences the effect/geometry. An exception
originating inside the injected helper or generated stub is not qualified by
this mapping; keep that helper nonthrowing and limited to the existing clock,
owner and accumulator operations. The material observer's foreign-SEH scope
cleanup remains its own responsibility.

## 4. Smallest shared-installer binding and collisions

An FL binding needs only one edition-specific, cold-resolved four-`SiteSpec`
pack in the existing ID order. Use recognized FL `1.3` identity, checked loaded
base/RVA arithmetic, the qualified normalized code/operand profile, and an
explicit quiescent install window. Resolve/preflight once; then use the same
`stamp::install_group`, lean emitter, transactional rollback, final activation,
frame tracker and recorder. Keep the AP binding and its executable gate
separate; do not widen `object_trace::executable_verified()` for other groups.
Per draw, retain only the existing active/owner check, clock and accumulation;
no code validation, allocation, native pointer read, lock or new framework.

The existing FL material observer owns `[0x4e7142,0x4e7147)`, outside this
body; point-light admission owns `[0x4e4915,0x4e491b)`, inside the body but
strictly before these four spans. All four spans and both existing hooks are
pairwise disjoint. The new sites neither replace the material invocation
scope nor modify light admission. They observe the ordinary native effect
calls regardless of whether the separate material snapshot feature is enabled.

**Actual initialization order.** `fl_startup::body` calls FL
`engine_profile::bootstrap` at `src/engine/fl/fl_startup.cpp:368` and loads
configuration at line 372. Its exclusive Preparing phase later installs the
material observer at line 521 and point-light admission at line 860, before
Complete publication at line 1055. Only after that can backend resolution
reach `load_backend` (`src/proxy/loader.cpp:63`) and `initialize_log`. The
bootstrap there (`src/proxy/capture.cpp:5417`) reuses published identity; game,
frame and pass initialization follow at lines 6825, 6826 and 6828. These are
source-order facts, not timings from a game run.

**Practical binding point.** Qualify the optional FL pass pack in the existing
Preparing/profile stage immediately after recognized-image/configuration setup
and before material or point-light writes, retaining its checked loaded base,
four specs and diagnostic-only readiness for later initialization. Pass
initialization in `initialize_log` should consume that immutable qualification
and let `stamp::install_group` check the four live spans. It must not rehash the
original whole material body there or require either material observation or
point-light admission to be enabled. A pass-profile refusal disables only this
optional diagnostic. No new startup framework or per-draw validation is needed.

**Cold profile ordering matters.** The existing point-light profile's
material regions are `[0x4e0f30,0x4e0f7c)` and `[0x4e481a,0x4e4a68)`
(`fl_dust_light_regions_inc.h`), both disjoint from the four pass spans.
Installing the pass stamps therefore does not disturb that existing profile.
Conversely, the new pass evidence's original whole-body digest cannot be
reapplied after point-light admission has patched `0x4e4915`. Qualify the
immutable FL pass pack before that write, then let the shared installer
preflight its four untouched spans at claim time. A later implementation must
preserve that ordering or use the existing checked owner/patch contract;
silently skipping changed bytes or performing a per-draw revalidation is not
qualified. Material CALL redirection is outside this body and does not alter
its pass profile.

The original-file complete-body/edge proof in this study and the future live
span preflight are different evidence: the file proof establishes the native
layout and all surveyed incoming edges; the earlier loaded-image qualification
binds that layout to this process before owned writes; the late four-span
preflight rejects changed pass sites at the actual claim boundary. None alone
permits unknown third-party patches or late nonquiescent installation.

The profile is now concrete enough for a bounded shared-installer change.
Remaining acceptance belongs to that implementation: source review, loaded
base/rollback and foreign-thread/disabled fixtures, generated-stub CPU/flags/
LastError/no-x87 checks, normal/exception incomplete-chain behavior and scoped
native evidence under the queue owner. No FL benchmark, Windows verification,
CrossOver verification or diagnostic activation follows from this static map.

The bounded adapter and its subsequent host/x86/native synthetic acceptance
are recorded separately in the
[FL pass verification ledger](../../verification/farnhams-legacy-pass-phases.md).
Those implementation results do not extend the static callback/unwind or live
game-delivery claims above.
