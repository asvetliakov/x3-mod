# FL SETA restore and manual-flight contracts

2026-10-29, bounded offline reconstruction against clean AP and FL. **The shared
SETA policy is reusable; FL needs native bindings and SSE replay stubs. Gate
restore additionally needs an FL destination-Ready provider, which this study
does not establish.** No production edit, PE build, install, Wine, native game
execution or flight acceptance is included. The only executable built/run here
is the existing host story-detector harness against a synthetic VM image.

Scope is the currently supported `seta_restore.cpp` and
`seta_manual_flight.cpp`, including the key-stop latch and debug input readers.
Reuse the shared injection consumer and the [transition contract](chase-transition.md)
(the shared injection/SETA contract this study cited, `farnhams-legacy-chase-binding.md`, is not in the tree), and existing common
owner/load/close admission. No second lifetime table, render token, scheduler
pump or task-completion service is proposed.

All addresses are preferred x86 VAs, base `0x400000`. FL image operands and
sites require admitted loaded base plus RVA and HIGHLOW-aware expected bytes.
Digests are local evidence provenance, not a production hash prerequisite.

| Measured input | Bytes | SHA-256 |
| --- | ---: | --- |
| `/mnt/c/X3AP-orig/X3AP.exe` | 2,153,984 | `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` |
| `/mnt/c/X3FL-orig/X3FL.exe` | 2,804,736 | `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8` |
| Clean FL `L/x3story.obj`, build 1636047691 | local decoded asset | `dbb732716e1acab6b8cc7f87f5d31324fb4764dfc22d1cec808749a4f45ec7b9` |

The [producer](../../../verification/results/fl-port/seta-contract/produce.py) and
[record](../../../verification/results/fl-port/seta-contract/contract.json) pin
**26 AP source windows**, **31 FL instruction regions / 1,951 instructions**,
**69 explicit semantic anchors**, six hook spans, caller sets, stop geometry,
**163 loader-width entries**, two rebases with **141 relocated operands each**,
**36 rejected memory-only image mutations/edition controls**, and six synthetic
story-detector cases. Counts are measured; proposed adapters and costs below
are inferred. No raw native/KC bytes or disassembly are retained in the record.

```sh
python3 verification/results/fl-port/seta-contract/produce.py \
  --story /home/asvetl/x3-mod/verification/results/fl-port/ui-graphs/x3story.obj
```

The story asset is already local and ignored; `--story` permits another local
copy with the same digest. The harness and temporary memory-form CODE live in a
temporary directory and are removed. The producer reuses project PE, relocation,
KC and story-image tools. The synthetic image tests recognition policy, not a
native VM snapshot or a complete loader emulator. The native width/registration
witnesses below establish the particular layout/opcodes this detector needs;
no arbitrary modified story is thereby qualified.

## 1. Installed hooks and native transport

| Current AP surface | FL counterpart and extraction |
| --- | --- |
| Factor hook `0x4b148c..0x4b1494`, EBX=args, EBP=task | **`0x4ce09e..0x4ce0a6`**, **EAX=args, ESI=task**; case entry is `0x4ce097`. |
| Shared injection `0x410100` | **`0x4cfd00..0x4cfd06`**, incoming **ECX=controlled simulation object**, stack+4=processing flag. |
| Shared VM clear `0x49ea80` | **`0x4ba6b0..0x4ba6b5`**, ECX=VM. |
| Shared VM load `0x4a0880` | **`0x4bcb10..0x4bcb15`**, existing load transaction owns admission. |
| Shared interpreter EH `0x52f298` | **`0x54e80a..0x54e80f`**, relocated metadata and native tail remain owned by shared transition transport. |
| Eight input-stop branch rewrites | **`0x4d2186`, `0x4d218f`, `0x4d2198`, `0x4d21a1`, `0x4d21aa`, `0x4d21b3`, `0x4d21bc`, `0x4d21ce`**; section 3. |
| Three angular and two strafe hooks | Five SSE spans in section 4. AP x87 stubs are incompatible. |

The four shared sites are not new SETA claims. Their exact bytes, exits,
canonical extraction, abandoned-call cancellation, CPU envelope and exception
limits remain with the reviewed transition owner. Keep chase-first,
consumer-second dispatch; separate prefilter bits; `vm(kind=1/2)` and lock-free
`unwind()` epoch notification; no module lock across `poll/result/vm/unwind`
or the engine call. Consumer attachment must be edition-admitted before its
prefilter becomes visible. The current AP-only attachment cannot authorize FL.

### Factor observer

FL TI registration publishes thunk **`0x4cdf30`** and names **`0x5a6510`** at
`0x4cde1c/0x4cde47`; name slot 9 is `TI_SetTimeWarpFactor`. The thunk obtains
its time owner from `*0x5aa17c`, forwards five stack words to **`0x4cdf50`**,
and returns after the implementation's `RET 0x14`. That implementation reads
command at original stack+0xc, task at +8 and argument cells at +0x14. It saves
EBX/EBP/ESI/EDI; ESI=task, EBX=[task+8], EBP=1 and EDI=time owner. Its case table
**`0x4ce1e4[9]`**, slot **`0x4ce208`**, points to **`0x4ce097`**.

At the proposed seam two outgoing native arguments have already been pushed,
1 then ESI. **EAX holds the first argument-cell pointer.** The displaced span
is two whole MOVs, lengths **3+5**, bounded patch specification
**`8b4801a1b0835a00`**: load ECX from args+1 and EAX from cfg root. Continue at
**`0x4ce0a6`**, which stores ECX into cfg+0xbc. There is one HIGHLOW operand,
**`0x4ce0a2`**. A trampoline must rebase that operand; raw AP or raw preferred
FL absolute replay is invalid. Handler arguments are `handler(EAX,ESI)` before
replay. Preserve the existing native stack, every GPR/EFLAGS/DF and XMM state,
four-byte incoming stack ABI, MXCSR and LastError envelope. EAX/ECX are outputs
of replay; ESI/EBX/EBP/EDI and the two pending argument words remain live.
Preserving flags avoids relying on a larger dead-flag claim; this case has no
conditional flag read before its call and epilogue.

The setter calls **`0x4c1940`** at `0x4ce0b2`, ECX=VM, stack `(task,1)`.
That native helper sets task status WORD +0x20=1, publishes integer tag/value
at **+0x28/+0x29**, inserts the task into the ready list through **`0x4c1880`**,
and `RET 8`. The dispatcher restores its four saved registers, returns EAX=1,
and `RET 0x14`. This native result is distinct from named-wrapper AL and from
completed KC execution. The observer still reads the old factor before store
and performs no VM call.

Immediate every-byte call census finds `0x4cdf4a -> 0x4cdf50`; the thunk is
published through the native registration, with no direct caller. No decoded
branch or relocated pointer targets the interior of the factor or five axis
spans. The raw every-byte edge scan finds two false encodings inside existing
SSE instruction operands (`0x4d1f1d -> 0x4d1f12`, `0x4d1f80 -> 0x4d1f91`);
full input decoding proves neither source is an instruction start. The
producer pins these exceptions and rejects added interior edges/callers.
Computed third-party entry is outside the accepted native closure.

### Named calls and results

SETA's AP wrapper **`0x49f4c0`** maps to **`0x4bb500`**, **not** chase's
`0x4bb5b0`. The native input stop demonstrates the zero-argument cdecl call at
**`0x4d21e1..0x4d21fb`**: six words, caller pops 0x18. Both FL wrappers share
preparation **`0x4bb310`**. For injected SETA use:

`0x4bb500(32, 0, 150, "StartFastForward"/"StopFastForward", 1, 0)`.

The FL callback-free selector is **32** for this sibling too. `0x4bb500` loads
its first two arguments into ECX/EDX for preparation, which stores them into
task callback selector/token +0x30/+0x34. Success completion rejects selector
>=32 at `0x4be9ee/0x4be9f1`; missing-method failure likewise rejects it at
`0x4bb248/0x4bb24b` before callback indexing. Native input's selector0 is a
real engine callback route and is not a safe callback-free extension choice.

Preparation returns AL0 for unresolved receiver/method. On successful task
setup the wrapper tail-jumps to **`0x4bb470`**, which appends bottom return
cells (tag3 then tag10, return0), enters interpreter **`0x4bed90`**, and sets
AL1 after a normal interpreter return. Thus AL1 is accepted execution dispatch,
including a suspend/yield; it is not task success or a completed warp ramp.
Current `seta_restore::State::on_result` reports `restored` from this wrapper
result without an actual-factor observer. Preserve that bounded AP policy and
its names; do not upgrade the claim or silently add chase's observed-mode
rule. The key latch treats AL0 as unresolved/off and observes its own factor
writes separately. Exceptions that escape dispatch do not call `result`;
existing epoch/outstanding-call handling applies. Retain no native task or KC
context pointer, no lock or C++ guard across the injected call. Engine calls
run under the incoming native CPU/LastError image; bookkeeping reestablishes
the helper FP state afterward, as shared transport already does.

## 2. Roots, layout and story recognition

| Reader required by these modules | FL binding / evidence |
| --- | --- |
| cfg root / requested warp / governor / step | **`*0x5a83b0`, +0xbc, +0xc0, +0x704**. Native TI cases `0x4ce097/0x4ce0c0/0x4ce0e0`; angular loads `0x4ce85e/0x4ceb6e/0x4ced7e`. AP uses +0xcc/+0xd0/+0x714. |
| Effective rate | Preserve signed Q16 `(factor*governor+0x8000)>>16`. FL tick `0x4fff26/0x4fff2c` and clock `+0x708`; [clock contract](music-alert.md#3-clock-lifecycle-thread-and-reentrancy). |
| Simulation manager / player | **`*0x5a82ec`, +0x38**; e.g. alert `0x42f913/0x42f91b`. Not the cockpit/control/render identity. |
| Game / pause | **`*0x6243cc`, +0x4a0, bit 0 (mask 1, `test cl,1`)**; input `0x4786ad/0x4786bd`. |
| VM / object key | **`*0x5aa174`**; CODE+8, size+0x10, STRG+0x14, size+0x18, classes count+0x1c/base+0x20, build+0x143c. Loader `0x4b90af/0x4b9122/0x4b913b`; string resolver `0x4bb16b/0x4bb197`. |
| Ship rebind / recorded speeds | Keep native ID+8, speeds+0x10/+0x14, Flags1+0x40 and autopilot `0x80000`, class WORD+0x48 (ship7/sector1), parent+0x54, script ID+0x94, marker WORD+0x9c=`0xcafe`. Native input reads autopilot `0x4ce7d1/0x4d21fb`, clears it `0x4d227b`; ship script read `0x46a836`. Other common fields reuse [sector layout](sector-fog.md#1-identity-globals-and-record-layout). |
| Return scanner | Task capacity+0x10, cells+0x14, signed index+0x18, next IP+0x1c; five-byte cells. FL return `0x4c0836..0x4c085c` tests tag10 above tag3, reads context+1/return+6, stops at return0 and skips null/classless context. Existing 128-cell/640-byte bounded scanner applies. |
| Interpreter native IP | `0x4c0751/0x4c0759` stores task index/next CODE offset before `0x4c0783` dispatch. The factor observer receives ESI from TI's frame, not interpreter EBP. |

Class lookup **`0x4cd180`** uses signed IDs and stride0x38; class descriptor+8,
parent+0x14, method count+0x28/base+0x2c, row stride16 and entry/name at +0/+4.
Name lookup **`0x4cd1d0`** compares unsigned bytes and follows the same layout
as the shared read-only detector. FL native loader **`0x4b9fd3..0x4ba079`**
scans groups1..31, names at VM+0x40+group*0x18 and rewrites the native operand
to little-endian WORD group/command. Its in-memory width pass uses table
**`0x4ba604`** / dispatch **`0x4ba5d0`**; all 163 entries for opcodes5..0xa7
match the existing image model, including variable78/79. Interpreter
`0x4beddf` bounds opcode1..0xb1. The inhibit member6
corresponds to AP's killed-state guard by method role; that semantic label is
inferred, while the field numbers/branches are measured. Native82 and call86 have width5 and are not
peephole heads; their PC+5 effects survive rewriting. No file PCs should be
hardcoded as runtime arming rules; retain unique name/instruction-start
recognition per VM pointer/CODE/size/build and bounded retry/failure policy.
The listed arities below are clean-file observations: the existing detector
reads method entry/name without checking arity, and its synthetic VM image
models method locals/arity as zero. Modified-arity refusal is not established.
Native preparation clamps the supplied argument count and zero-fills missing
arguments rather than refusing an arity mismatch.

| Clean FL KC method | File CODE / relevant effect |
| --- | --- |
| `150::StartFastForward(0)` | **`0x18cb5`**; guards inhibit member6 and active member18, sets active18, resets ramp19, captures time20. Does not immediately raise the factor. |
| `150::StopFastForward(0)` | **`0x18d03`**; only factor native at **`0x18d34`**, next IP **`0x18d39`**; clears active18/ramp19, factor1.0. AP documentation's member21 is not FL's member18. |
| `150::ToggleFastForward(0)` | **`0x18d45..0x18dd6`**; stop call `0x18d54`, return **`0x18d59`**; otherwise tests eligibility and calls StartFastForward. |
| `150::StartGateWarp(2)` | **`0x199b0..0x19a59`**; sole stop call **`0x199d1`**, return **`0x199d6`**. |
| `150::StartSectorWarp(5)` | `0x19af2..0x19b9e`; diagnostic named range. |
| `150::QuickWarp(0)` | `0x1a73e..0x1a817`; diagnostic named range. |
| `2004::JumpToSector(5)` | `0x1485fa..0x148efe`; diagnostic named range, contains multiple stops; do not require a unique stop here. |
| `150::Vbi(0)` | **`0x169c9`**; ten ramp steps, strictly >250 ms between steps, factor write `0x16a43`. The existing real-time latch entry/tail policy does not need a new completion observer. |

The host detector accepts the clean FL model with **2,923 checked reads**,
stop IP `0x18d39`, gate return `0x199d6` and the five ranges above. Missing or
duplicate stop native/gate call are refused (`no_site`/`ambiguous_site`), as
is an exhausted read budget. The build is absent from AP's expectation table;
existing dynamic recognition accepts an unknown build only after successful
unique detection. A future FL table entry can record these measured values,
without making a build/hash substitute for loaded-object recognition.

Only this clean story is qualified here. Optional `StartSectorWarp`,
`QuickWarp`, `JumpToSector` diagnostic ranges may be absent under the existing
detector's policy. Missing/ambiguous mandatory StopFastForward/StartGateWarp
refuses the gate ticket; absent ToggleFastForward refuses the key latch.
Mods, intros/config objects and other FL story revisions require the existing
fail-closed detection outcome; no blanket compatibility with them is claimed.
The StartFastForward method must still resolve at dispatch, which can refuse.

## 3. Speed-only SETA stop and key latch

The nine FL stop rows are **9 bytes** each from **`0x4d217f`**:
`cmp dword [flag],0` (7), then short Jcc (2). AP used 8-byte rows comparing
EBX. FL flags are record+0x10 at
`0x61c6d0/6f8/720/748/77c/7b0/7dc/808/840`, respectively yaw, pitch, roll,
turret yaw, turret pitch, strafe X, strafe Y, speed, look. Keep the speed row
(index7, branch **`0x4d21c5`**) unchanged. Replace the other seven JNEs by
`66 90`; replace look JE **`0x4d21ce`** by JMP with the same rel8. Non-speed
flags then fall through; only speed reaches **`0x4d21d0`** and native factor
check **`0x4d21d5`**, before the native StopFastForward call. Join remains
**`0x4d21fb`**, which overwrites flags via TEST and begins separate autopilot
cancellation. Preserve that block. The controller/strafe/speed changes have
already applied when stop runs.

The eight rewrites occupy **`[0x4d2186,0x4d21d0)`**, one page. **`0x4d218f`**
is offset7 within its aligned eight-byte block: this two-byte write straddles
that block. AP's `stop_patches_well_formed()` atomic-qword assertion therefore
cannot simply be rebound. Use the existing admitted initial install/rollback
protocol and explicitly qualify `engine_patch::write_code`'s **plain fallback**
for this row. Do not claim all eight are atomic; no new patch protocol is
inferred. Every intermediate row mixture has a valid instruction path, but
that alone is not concurrent-execution permission.

The key latch uses action **675**. Clean FL `501::HandleKeyInput` switch at
CODE **`0x24188`** maps action675 to **`0x23b38`**, whose call at `0x23b3c`
reaches ToggleFastForward. Action counter base is **`0x62d0d8`**, SETA byte
**`0x62d37b`**, steering524..529 starts **`0x62d2e4`**. Input zeroes the 1,024
counter bytes at `0x4cfd20..0x4cfd34`; held-key native increments the translated
action at **`0x4d0600`**. At shared injection entry counters still describe
the preceding iteration, as in AP. Preserve eight zero-counter tail entries,
2,048-entry cap, ramp guard, detected toggle-return qualification and existing
VM/EH cancellation; no new ring patch is needed.

All current diagnostic readers have FL counterparts:

| Input debug scalar | FL |
| --- | --- |
| Last ring-read key WORD | **`0x62ddf0`**, stored after reader call at `0x4786b6`. |
| Ring read/write indices WORD | **`0x5a65e8` / `0x5a65a4`**, read `0x4ecd1f/0x4ecd26`. |
| Four ring DIK slots, stride12 | **`0x5ab14c`**; same 37-byte snapshot span. Reader old-index3 test at `0x4ecd3b`; DI writer new-index3 test at `0x4ef100`. This retains the AP ring mismatch rather than proving it repaired in FL. |
| Input context / buttons | **`*0x5a8478`**, joystick+0x450, mouse+0x454; shared [input/pause contract](ui-input.md). |

Input driver **`0x478690`** reads the ring first (`0x4786a8`) and optionally
dispatches KC Input (`0x478768`). When invoked, that dispatch returns before
either native input call at **`0x4787c8/0x4787dc`**. The injection runs after
any key re-toggle performed by that optional dispatch.
Its native call can reenter the factor observer synchronously. The factor
observer never calls VM, and neither lock spans that dispatch. An unwind is
seen through the existing epoch, with no invented full-task observer.

## 4. Five SSE manual-flight spans

| Axis | AP hook | FL hook / length / continuation | Record / ship at seam |
| --- | --- | --- | --- |
| Yaw | `0x40ebde` | **`0x4ce878` / 16 / `0x4ce888`** | **EDI=`0x61c6c0`, ESI=ship** |
| Pitch | `0x40ee3e` | **`0x4ceb88` / 16 / `0x4ceb98`** | **EDI=`0x61c6e8`, ESI=ship** |
| Roll | `0x40f04d` | **`0x4ced98` / 16 / `0x4ceda8`** | **EDI=`0x61c710`, ESI=ship**; AP roll swapped these registers. |
| Strafe Y | `0x411e0c` | **`0x4d1f04` / 15 / `0x4d1f13`** | ESI=cfg, EDI=ship; record command/max `0x61c7f0/+4`. |
| Strafe X | `0x411eb2` | **`0x4d1f8b` / 15 / `0x4d1f9a`** | ESI=cfg, EDI=ship; record command/max `0x61c7c4/+4`. |

Angular native entries **`0x4ce7c0/0x4cead0/0x4cece0`** take ECX=record and
stack+4=ship, `RET 4`. Their only surveyed direct callers are respectively
**`0x4d1e90/0x4d1e9b/0x4d1ea6`**. All three keep record in EDI and ship in
ESI. They skip autopilot, clamp record+0x24 through native max-rate readers
`0x45a830/0x45a7d0/0x45a890`, and calculate acceleration from those rates.
At the proposed seam XMM0 holds the already computed **double**
`accel * game_ms * 0.001`; XMM2 is previous current rate; XMM4 is 2^-16.
Replay is **`cvtpd2ps xmm3,xmm0; movd xmm0,[edi+0x24]; cvtdq2ps xmm0,xmm0;
mulss xmm0,xmm4`**, lengths **4+5+3+4**. Bounded bytes for all three:
**`660f5ad8660f6e47240f5bc0f30f59c4`**. No relocation occurs in these spans.

Minimal apply path: scale XMM0's existing step double by **1/k²**, replay its
conversion to XMM3, substitute the shared policy's rounded/clamped target
**round(record+0x24 / k)** in the MOVD, then replay conversion/angle scaling.
Keep native operation order and conversion rounding. This avoids temporarily
writing the record or changing native clamping. Do not reuse AP's
`step_factor=game_ms/k²` as a direct multiplier of an already-computed FL
step; doing so multiplies time twice. The next COMISS consumes XMM0/XMM2,
and its result drives the native slew. It overwrites EFLAGS before any
subsequent flag read. Preserve all other GPRs/XMM lanes, ESP, DF, MXCSR,
LastError and native x87 image; do not infer upper lanes dead from scalar
intent. FL replay itself needs no x87. Yaw/pitch write ship+0x24/+0x28;
roll writes +0x2c, retaining its extra native steering policy after the seam.

For strafe XMM0 at each seam holds float game_ms, XMM6 the limit constant,
XMM7 double0.001. Replay is `mulss xmm0,xmm6; cvtps2pd xmm0,xmm0;
mulsd xmm0,xmm7; cvtpd2ps xmm4/xmm1,xmm0`, lengths **4+3+4+4**;
Y bytes **`f30f59c60f5ac0f20f59c7660f5ae0`**, X final byte `c8`.
Apply **1/k** to the double before its final conversion, preserving the
remaining clamp and multiplication by the axis maximum. Y keeps command in
EAX and maximum in ECX/XMM5; X keeps command in ECX and maximum in EDX/XMM4.
Those integer values, the other SIMD state and ESI/EDI are live. The next
COMISS at `0x4d1f25/0x4d1fac` overwrites flags before their branch use.
Final displacement reaches ship+0x60/+0x64/+0x68 in native code. No native
pointer or helper output may outlive this synchronous site.

The delta-mode exclusion also maps: mouse dispatcher **`0x4cfa10`** chooses
**`0x5a6c60/+4`** or view-flag pair **`0x5a6c68/+4`**, types1/2/3 feed the
three records through **`0x4ce6f0/0x4ce9c0/0x4cec00`**. These normalize by
cfg+0x704, retaining the rationale for leaving delta axes unscaled. Continue
checking all four words conservatively. Keep factor>1 and effective-rate>1
admission, range bounds and scale mathematics; do not globally enable
fast-math. Runtime equality of the emitted SSE adapter's register lanes,
rounding, rates and control response still needs its focused fixture/flight.

These handlers make no engine call and should retain allocation-free,
lock-free math. Shared output scratch is safe only within the existing healthy
synchronous input closure: no new helper callback is introduced. Native input
has callback-capable script calls outside these five spans; arbitrary computed
reentry/third-party hooks are not proven safe. Preserve current documented AP
exception limits and avoid extending scratch ownership across a native call.

## 5. Option boundaries and remaining Ready dependency

Actual source dependencies, rather than a whole-feature blanket gate:

| Configuration / path | Required FL closure |
| --- | --- |
| `SETA_MANUAL_FLIGHT=1` part A | Eight stop rewrites/native factor guard; no destination-Ready walk and no angular/strafe helper. |
| `SETA_MANUAL_FLIGHT=2` | Part A plus five SSE hooks, cfg/input scalar bindings and shared scaling policy. No destination-Ready walk. |
| Key-stop latch under either manual-flight mode | Factor observer, story detector/toggle range, action675 counter, callback-free StopFastForward and shared VM/EH/injection consumer. No destination-Ready walk. **Current source additionally requires `seta_restore` armed**: `key_latch_enable` returns `seta_restore_disabled/refused` otherwise; manual flight itself remains installed. |
| `SETA_RESTORE=1`, gate ticket | Factor observer, shared transport, mandatory story recognition, player/parent/identity/autopilot/pause probes, **destination Ready**, two presented frames, callback-free StartFastForward and current cancel/result policy. |
| `debug=1` snapshots | Bound ring/context readers above; prefilter remains set and raises per-entry cost. Not a prerequisite for base controls. |

`seta_restore.cpp::poll` first calls `machine_.on_inject` for the gate ticket;
only if it hands out no call does it poll the latch. The ticket's probe
`EngineProbe::ready` runs **`fog_prefill::walk`**, which hardcodes AP manager,
sector-background roots/recipe and AP evidence. It searches backward from
manager tail, at most8 nodes/25 reads, requiring a live newest sector, scene,
changed ID and Ready background sample; the player parent must equal the
result, followed by two Presents. This identifies the required boundary, independently of shaders and resource
creation.

The subsequent [destination-Ready contract](prefill-ready.md)
closes the bounded input-entry ordering/read-admission question with static and
synthetic evidence. It reuses the common owner/load identity, copies only the
values needed by the shared Ready predicate, and keeps the eight-node bound
as conservative defer policy. Family-name copying and resource-creation prefill
are separate, unnecessary dependencies for this predicate. The broader
[sector-fog limits](sector-fog.md#5-proven-limits-and-remaining-gates)
remain applicable to resource-creation work.

Implementation and runtime qualification of the Ready adapter remain open;
no render-owned token alone grants its native reads. Retain the current
restore/latch arming relationship and two-Present readiness policy. Full
selected-option acceptance requires that adapter and the existing latch
ordering, not partial manual-flight evidence.

## 6. Minimal adapter and acceptance

1. Bind immutable FL roots, six spans/continuations and eight stop rewrites;
   keep shared restore/latch/scale policy. Supply EAX/ESI factor extraction,
   five SSE replay envelopes, current complete install/owned rollback and
   HIGHLOW replay. The relocated factor span and straddling stop row need
   emitted installation checks, not inferred atomicity.
2. Select FL SETA wrapper `0x4bb500`, selector32/token0, names/arity/result
   policy; admit the consumer's edition before attachment. Keep shared four-site
   ownership, chase priority, consumer cancellation and lock boundaries.
3. Bind the native story reader and four mouse words/action/ring diagnostic
   scalars. Retain bounded per-key recognition and transient retries. Preserve
   option independence above and refuse unsupported story outcomes.
4. Close the Ready-provider dependency under the existing owner/load scope,
   then qualify the complete selected restore/manual-flight combination.

Required later acceptance: native/rebased exact-byte refusal and rollback,
plain-write refusal/recovery, AP parity, stack/DF/EFLAGS/GPR/XMM upper lanes/
x87/MXCSR/LastError preservation at four-byte incoming alignment, original and
apply SSE paths, real-time target/acceleration/strafe math, both input callers,
selector32 success and missing-method refusal, accepted-without-factor result
semantics, nested factor callback and escaped dispatch cancellation, VM clear/
load/EH, press/release/tail/ramp guard, player rebind and Ready/defer/failure.
No install or runtime qualification follows from this offline record.

Inferred hot-path changes are fixed binding access/canonical extraction at
existing sites, three rotations and up to two nonzero strafe axes per eligible
input iteration. No allocation, registry walk, lock or native callback belongs
in these five helpers. Reuse checked record identities and current disabled/
warp-off early exits; do not add per-draw work. Factor recognition is rare and
cached per VM key; the measured host read count is not native latency. Idle
injection retains the existing prefilter skip; debug intentionally keeps it
active. Gate Ready sampling remains at most once per presented frame while a
ticket is active. FL loading cost and timing are unmeasured. All proposed APIs
are portable documented Windows/x86 contracts; Windows runtime and CrossOver
flight behavior remain unverified.

Independent consequential RE review reproduced the scoped producer checks and
accepted the contract after correcting the relocation count and narrowing the
arity-recognition claim above. This remains static/synthetic evidence, not an
implemented adapter or native gameplay qualification.

## Port (2026-11-03)

Both options now have FL bodies: `src/engine/fl/seta_restore_fl.cpp` + `seta_restore_sites_fl.h` and
`src/engine/fl/seta_manual_flight_fl.cpp` + `seta_manual_flight_sites_fl.h` (shared window form
`seta_windows_fl.h`). The state machine, story recognition, return scan, key latch and per-axis decision are the AP
headers, included unchanged; the FL files bind them to the loaded base and carry the glue with `edition=fl` rows.
`loader.cpp` calls `seta_restore::initialize()` then `seta_manual_flight::initialize()` in the FL install window right
after the chase family (the latch needs the restore armed); `capture.cpp` makes both `present()` calls. The order
matters: the chase transition plans its identity windows, the shared injection entry's included, before any claim,
so a SETA attach that claimed the entry first made the whole chase transition refuse (`window_mismatch`, window 12; the
review's witness is `verification/results/fl-seta/chase_order_probe.cpp`). After it, the SETA consumer attaches to the
sites the chase restore claimed, or claims them itself. An out-of-order call while the chase camera is wanted but not yet
initialized is refused before any write (`chase_pending`). The shared consumer of `chase_transition.cpp` is admitted
on FL: `inject_attach` binds the four shared sites through `fl_admit()` when the chase restore has not, and
`consumer_inject` passes ECX as the controlled object and selector 32 (`detail::restore_callback_none`). How the
section 6 contract is met:

| Contract item | Implementation (verified on the host, 2026-11-03) |
| --- | --- |
| Factor hook, EAX/ESI | `0x0ce09e`, 8 bytes, expected bytes = the live relocated span (`+4` = base + `0x1a83b0`); the asm stub passes EAX (cells) and ESI (task). It saves EFLAGS, GPRs, XMM0-7 (128 bits), FNSAVE image and MXCSR; the C side keeps LastError. The jump straddles a qword (offset 6): plain write in the install window, read back, and refused unless the write path was the plain one (`write_path_mismatch`). |
| Wrapper `0x0bb500`, selector 32 | `call_rva=0x0bb500 selector=32 key=0x96`, six cdecl words. AL 1 = accepted dispatch → `restored` (AP policy, no factor observer); AL 0 → `restore_failed` / latch `call_unresolved`. |
| Eight stop rewrites | Seven JNE → `66 90`, look JE → JMP; seven through `lock cmpxchg8b`, the pitch row `0x0d218f` plain. The install refuses (rolled back) unless each row took the path its qword geometry predicts. Speed row kept. |
| Five SSE spans | Claimed through `engine_patch` (strafe Y `0x0d1f04` plain, read back; the other four atomic). Apply path: rotation `mulsd xmm0,[1/k^2]` then the replay with `round(record+0x24/k)` in place of the MOVD; strafe `mulsd xmm0,[1/k]` before the final CVTPD2PS. The replay runs on the restored engine registers before POPFD, so every GPR, EFLAGS and DF leave unchanged on both paths. Rotation EDI must be the axis record, strafe ESI the cfg pointer; any other value takes the original path (`contract`). |
| Destination Ready | `fog_prefill_fl::walk` (the FL provider); `install()` refuses with `ready_provider_refused` unless `fog_prefill_fl::admitted()`. |
| Story reader, latch scalars | VM `*0x1aa174`, cfg `*0x1a83b0` +0xbc, manager `*0x1a82ec` +0x38, game `*0x2243cc` +0x4a0 bit 0, counters `0x22d0d8` (SETA `0x22d37b`, steering `0x22d2e4`), ring `0x22ddf0`/`0x1a65e8`/`0x1a65a4`/`0x1ab14c`, input context `*0x1a8478`. Each is a fixup target of a compared window. |

Windows checked at base + RVA before any write: 22 for the restore (TI case, table entry, thunk/dispatcher, ready-list
callee, wrapper and its run, both selector bounds, native stop call and its string, return scan, interpreter IP,
manager, pause, injection head, counter increment, ring reader/slots, ship script, autopilot, VM build/CODE) and 20
for manual flight (nine stop rows, stop block, three controllers from entry through the COMISS, axis calls, strafe
block, mouse dispatcher and table entries 1-3, and the constants 2^-16, 0.001 and 20000.0). Mode 1 compares only part
A's ten.

Verified (host, measured 2026-11-03):

- `verify_seta_restore_sites_fl.py` and `verify_seta_manual_flight_sites_fl.py` PASS on `e7f3aad8…`: 64 and 28
  Capstone anchors; the header rows equal the emitted windows; the AP addresses do not match their AP bytes in
  X3FL.exe. Three raw branch encodings that land inside a claim are pinned and proven not to be instruction starts
  of the input routine: `0x0d1f1d → 0x0d1f12`, `0x0d1f80 → 0x0d1f91` (LOOPE/LOOP inside SSE operands) and
  `0x0d215b → 0x0d21a5` (JS inside a MOV).
- `fl_seta_manual_flight.cpp` 1,414 checks, 0 failures; `fl_seta_restore.cpp` 213 checks, 0 failures. Both run
  natively through WSL interop on X3FL.exe mapped and relocated into a private image.
- The real FL story (build 1636047691, read from the local `addon2/03.cat` into a temporary VM image) is recognized by
  the production detector: stop IP `0x18d39`, gate return `0x199d6`, toggle return inside ToggleFastForward.
- Part A executed on the real rows: before the install every set flag reaches the stop; after it only the speed flag
  does.
- Part B on the real spans at four ESP skews with flags/DF set and clear. Original path equals a reference copy of the
  span (GPRs, EFLAGS, ESP, XMM0-7, x87 CW/TOP/st0, MXCSR, LastError). Apply path equals an independent reference: k =
  4, 10, 1.5 and 1000; step within one float ulp, target exact, strafe limit 320 per frame at 16 ms real; XMM3 lane 1
  as CVTPD2PS leaves it.

Init order (fixture modes, measured): the chase family's 14 install rows (addresses masked) are identical alone, with
the SETA consumer attaching after it, and after an out-of-order SETA call that was refused (`chase_pending`) and then
attached; the chase plan admits (no failed window) before the chase install in all three.

Open (Q4): the ticket expiry (60 s, `GetTickCount64`) and the latch bounds (8 tail and 2,048 cap input-loop entries,
4-frame ramp guard) are real-time and input-entry clocks, unchanged from AP, not scaled by the game-time frame step.
The expiry starts at the arm, when the factor is already 1.0; the latch follows the real-time key path.

Not verified: in game (not flown), native Windows game behaviour, the 60 s wall-clock ticket expiry (not driven by the
fixture), modified or other FL story revisions, and the ring-slot byte as a DIK code (diagnostic rows only). The
restore and latch semantics are exercised against engine stand-ins (fake wrapper, Ready stub), not the engine's own
interpreter.
