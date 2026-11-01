# FL dust leak repair and point-light root admission

Offline targeted comparison, 2026-10-29, base `ad9ea28a`. Preferred VAs use
base `0x400000`; production bindings must resolve RVAs through the admitted
loaded image. The [feature matrix](../../architecture/farnhams-legacy-feature-matrix.md)
row names the historical `src/proxy` paths; the current AP implementations are
`src/engine/ap/{dust_leak_fix,point_light_admission}.cpp` and their adjacent cores.
No production change, PE build/execution, Wine/game launch, installation or commit
was made. These findings establish port inputs, not an active FL capability.

**Both policies can remain shared; neither AP stub can be reused unchanged.**
FL dust keeps the allocate-before-load leak, but node register, release ABI and
loop termination changed. FL light admission keeps the parent/position/scale
fields, but range and record moved, the light is in ECX, and EDX is live on both
continuations. No new registry observer or lifetime framework is justified here.

## Evidence

```sh
python3 verification/results/fl-port/dust-light-contract/reproduce.py
```

[Producer](../../../verification/results/fl-port/dust-light-contract/reproduce.py)
and [derived summary](../../../verification/results/fl-port/dust-light-contract/summary.json)
use project `inspect_x3.PE32` plus Capstone. Clean images: AP
`/mnt/c/X3AP-orig/X3AP.exe`, 2,153,984 bytes, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`;
FL `/mnt/c/X3FL-orig/X3FL.exe`, 2,804,736 bytes, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Hashes bind offline evidence; they are not proposed whole-image runtime gates.

Measured: **13 regions, 1,081 instructions, 114 operand anchors, 26 region
boundary edges**, independently checked against `.text` decode from `0x401000`;
both hook edges checked separately. **11 semantic negatives** bypass image hashes,
region hashes and all operand anchors: failure/loop/reject/bypass targets, release
register ABI, scene/range/node/parent operands, live EDX consumer, and a synthetic
incoming edge into a real instruction interior. Two additional interior-origin
starts (`0x420ac5`, `0x4e4908`) must be denied. Hook spans have no HIGHLOW operands
or HIGHLOW pointers into their interiors. Whole-text linear decoding with skip-data
is a direct-edge survey, not proof against computed private calls. Raw exploration
scripts/listings remain ignored as `*.local.py`/`*.local.txt` beside the summary;
no native code listings or extracted binaries belong in this patch.

AP baselines: [object lifetimes, Run383/Patch](object-lifetimes.md#patch-dust_leak_fix-2026-09-29),
[camera and lights](../camera-and-lights.md#point-light-admission-site), and the
[dust](../../verification/dust-leak-fix.md) / [point-light](../../verification/point-light-admission.md)
ledgers. Reuse the established [FL registry contract](object-lifetimes.md),
[material transport](material-submit.md) and
[sun/light layout](sun-light.md); this note adds only these two
local policy seams.

## 1. Dust fill, failure ownership and release

| Item | AP | Measured FL contract |
| --- | --- | --- |
| Dust updater | `0x41efc0`, `ret 8` | `0x420660..0x420c3b`, three stack words, `ret 0xc` at `0x420c38` |
| Ordinary caller | cockpit update | `0x422f67/6a/6d` pushes sector camera / sector / dust camera, then `0x422f70 → 0x420660`; VM caller also at `0x431f91` |
| Allocate | `0x41f328 → 0x486d10`, EDI=node | `0x420950 inc ebx; 0x420951 → 0x4a2200; 0x420956 mov esi,eax` |
| Null allocation | `0x41f336 → 0x41f4d1` | `0x42095a → 0x420ac4`, ESI=0 |
| Invalid body ID | `0x41f3d1` | `0x420a19 JS → 0x420ac4`, EDI=body ID, ESI=node |
| Already failed body | `0x41f436` | `0x420a28 → 0x486910`, nonzero result branches at `0x420a2f → 0x420ac4` |
| Body load returns null | `0x41f447` | ECX=ID; `0x420a37 → 0x4a1a40`; zero result branches at `0x420a3e → 0x420ac4` |
| Successful bind/attach | `0x41f4cc → 0x489da0` | Bind `0x420a47 → 0x4a35e0`; ECX=ESI, scene pushed at `0x420abc`, attach `0x420abf → 0x4a56b0`, callee pops four bytes |
| Common tail | decrement `[ESP+0x20]`, JNE | `0x420ac4 CMP EBX,[ESP+0x18]`, `0x420ac8 JL 0x420950`; exit `0x420ace` |
| Node retirement | `0x487be0`, cdecl stack argument | **`0x4a3300`, ECX=node, no stack argument, plain RET** |

Constructor zeroes a `0x270` allocation, registers its handle (`0x4a229b` /
`0x4a22a8`), initializes its empty child list (`0x4a22ab..b8`) and links it
onto manager `R+0x28` through the `R+0x2c/+0x30` sentinels (`0x4a23ac..be`).
All three non-null failure edges occur **before bind**, leaving that registered,
unattached node owned by the manager. No release is executed on those edges.
The ordinary fill repeats next frame, preserving the AP leak mechanism
(inferred runtime growth from measured control flow; no FL flight measured).
`0x486910` reads the indexed body's failure word at slot `+0x10`
(`0x486972`); the loop also avoids its lookup for IDs >=100000.

Attach clears node `+0x18` at `0x4a56cd` and writes scene `+0x1c` at `0x4a56f6`.
Thus ESI nonzero with `ESI+0x1c == 0` at the tail identifies the three failed
newborn cases. An attached node has nonzero scene; allocation failure has ESI=0.
Use the engine's existing release, not map Remove alone: it unlinks the intrusive
list (`0x4a34de/e6`), removes handle `+0x28` from `R+0xc`
(`0x4a34ef → 0x50d040`), then clears/frees the ordinary `0x270` block
(`0x4a35aa/b3`). Manager clear proves the same ownership: it gets `R+0x28`
at `0x48a875`, puts the head in ECX at `0x48a880`, releases at `0x48a882`,
and reloads the head until empty. Never call release for ESI=0.

Optional release paths inspect `+0x1a8/+0x1ac`, **`+0x174`** (AP `+0x170`),
`+0x1f4 & 0x40`, the child list, scene, `+0x130 & 0x20`, and strings
`+0x20/+0x24`. The failed newborn's zero initialization/empty children bypass
these per-node paths. They do not bypass the unconditional external-reference
scan at `0x4a3422..0x4a34d8`: it traverses `R+0x18`, each outer entry's
`+0x1c` list and each entry's `+0x2a0` chain. A `record+0xc == node` match
unlinks/frees that record and visits 13 pointers at `+0x3c`, recursively
releasing nonnull referenced nodes. No-match across preceding load/recovery is
not established here. The general release's recursion/callbacks therefore
remain relevant even for a zero-initialized newborn;
the guard may not be broadened to arbitrary unparented or partially bound nodes.
FL release preserves EBX/ESI/EDI/EBP, using its own saved frame and plain RET;
EAX/ECX/EDX are scratch. Constructor/release helper internals remain owned by the
existing registry note.

### Dust hook and liveness

Candidate **`[0x420ac4,0x420ace)`**, bytes `3b5c2418 0f8c82feffff`, is two whole
instructions (4+6 bytes), with four known failure branches to its start and no
surveyed interior edge. `SiteSpec` can retain existing `engine_patch` chaining:
length 10, `ret_pop=12`, **`rel32_offset=6`** rebases the displaced JL's operand.
The five-byte entry jump crosses an aligned eight-byte word, so both its
write and restoration need quiescence. The saved/replayed span is ten bytes;
the existing patch helper restores only the changed five-byte prefix. Retain the existing qualified FL startup window, exact
loaded-window ownership checks, rollback and retained arena/module rules.
This is a candidate, not a new live-patching argument.

At this tail ESI is node or zero, EBP background row, EBX the
incremented fill count, and `[ESP+0x18]` target count; ESP includes `sub 0x40`
and four saved registers. EDI is the body ID on the three body failures; the
null-allocation arrival may retain the camera or previous-iteration value. The
guard does not use EDI. Flags are dead before the displaced CMP. A thin FL
stub can test ESI/scene, save volatile registers, set ECX=ESI, call release,
restore volatile registers, then **set EBX to `[ESP+0x18]`** at the restored
stack depth. The displaced CMP/JL then falls through, ending fill for this
invocation. On success/null arrival, chain to the original tail unchanged.
No stack-slot decrement or `remaining=1` from AP belongs here.

The exit immediately reloads EBP (`0x420ace`) and EBX (`0x420ad2`); EDI is
redefined at `0x420af2`, ESI at `0x420b28`. None dereferences the freed node.
The target-count slot is not used after the tail. Incoming SIMD values are not
needed by the updater's continuation (it reloads XMM0 and defines XMM1/XMM2);
this is not a general promise about arbitrary instrumentation helpers. Preserve
the expected FP/control state. LastError liveness is not proved by allocator
calls: fixture the release path with a sentinel, and preserve LastError in the
failure-only adapter if its extra call changes it. No global current-node,
retained pointer, new observer scope or per-iteration allocation is required.
Native resource loading precedes the seam; the added release is restricted to
a never-bound newborn. Its external-reference scan still runs; callback absence
requires evidence of no matching records, and matches require correct recursive
cleanup. Preserve full native release and the existing ownership admission.
Concurrent or nested native ownership violations remain unproved, not a new
guard framework.

## 2. Point-light parent/range policy and exact hook

| Item | AP | Measured FL |
| --- | --- | --- |
| Material routine | `0x4c0150` | `0x4e0f30`; existing material-call adapter owns entry transport/SEH |
| Submitted node | `[EBP+0xc]` | **`[EBP-0x1c]`**, stored from entry EDX at `0x4e0f64` |
| Light | ESI | **ECX**, loaded through candidate array at `0x4e4875`, also saved `[EBP-0x4c]` |
| Parent / base scale / world position | `+0x18/+0x70/+0xb0..b8` | Same fields; parent writer `0x4a5809` links child to parent, invalidate parent extent at `0x4a580f`; scene attach clears parent at `0x4a56cd` |
| Range / attached light record | `+0x158/+0x16c` | **`+0x15c/+0x170`**, range setter `0x4a3f2b`, record load `0x4e491b` |
| Per-node distance | x87 sqrt/convert | wrapped signed DWORD deltas, SSE single square/sum; `0x4e48fb → 0x412a80` SQRTSS; `0x4e4903 CVTTSS2SI` |
| Decision site | `0x4c27af`, six-byte JG | **`0x4e4915`, `0f8f25010000`**, six-byte JG |
| Admit / reject | `0x4c27b5 / 0x4c29f5` | **`0x4e491b / 0x4e4a40`** |
| Directional bypass | enters after AP admit MOV | `0x4e48a3 JNE 0x4e491b`, bypasses the decision hook |

FL evaluates `trunc(sqrt(float(dx)^2+float(dy)^2+float(dz)^2)) -
light[+0x15c] - node[+0x70] > 0` with native 32-bit SUB semantics.
Root policy can preserve the existing bounded eight-parent-read walk and integer
squared-distance predicate with **only a bound range-offset parameter**; parent,
scale and position offsets match. Keep existing fractional-unit difference from
native truncation, negative-reach behavior, wrap/extreme/cycle/unreadable fallback
and default-off opt-in policy. Native SSE rounding is another reason not to
claim bit-identical root geometry at every large integer endpoint; per-node
admission must always retain the native result.

`[0x4e4915,0x4e491b)` can be `JMP detour; NOP`, with TEST's flags still live.
No surveyed direct edge enters it; the directional bypass enters the admit target.
Both outcomes overwrite EAX before use. **ECX is live on admit** (record load),
**EDX is live on both paths** (`[EDX-0x28]` at `0x4e492a`; `add edx,4` at
`0x4e4a4d`). ESI tracks the parameter-handle array; EDI is the effect COM pointer;
EBX the original stack frame; EBP the aligned material frame. Preserve those,
all locals and stack balance. Flags are dead after the branch choice. The AP
handler-call stub, which lets ECX/EDX clobber and pushes ESI/[EBP+0xc], is unsafe.

Thin FL reject path: save ECX/EDX, pass remainder EAX, light ECX and node
`[EBP-0x1c]` to the shared integer handler; restore native ECX/EDX, branch from
the saved handler verdict to the admitted/rejected continuation. Native-admit
path keeps its one conditional branch/counter increment/jump without helper calls.
Reuse the existing integer-only/no-XMM call-graph audit, four-byte incoming-stack
alignment, LastError preservation and applicable FP/SIMD preservation checks;
this note does not assume API calls preserve XMM registers. Reject-only SIMD
saves, if required by the final helper/liveness audit, must not tax native admits.
Six-byte install/restore crosses a qword, requiring existing startup quiescence.

### Borrow, nesting and cache boundary

The native material loop mutates light record `+0x68` (`0x4e4927`) and invokes
effect methods; it is not a once-per-frame read-only poll. Reuse the existing FL
material/complete-pass borrow and registry admission. `material_submit_fl::Record`
labels lights as **identity tokens only**: it does not supply a memory lease for
this walk. Do not turn the owned record into permission to dereference arbitrary
saved lights after native submission or add background polling. The actual light
at the hook is the native loop's current borrowed node; the root walk adds bounded
synchronous parent reads with the existing fail-closed reader policy.

AP's fixed 256-entry per-frame memo assumes one submission writer and keys raw
(node,light) pointers. Neither global publication nor setting the frame last
makes it C++ race-free across concurrent writers, nor prevents reuse within a
frame. This audit supplies **no new evidence of FL concurrent invocation**, so
it does not mandate a new cache/lifetime framework. Before sharing that cache,
reuse the existing qualified exclusion/owner interval and its invalidation,
or keep a fixed cache per owning submission context and invalidate on that
context's existing revision/load boundaries. Measure repeated-part rejects and
memo hits; do not replace all hits with repeated parent walks without a cost
check. No heap allocation, registry scan, mutex, repeated whole-image validation
or extra FP geometry belongs in the per-draw path. Nested material invocations
must not publish another invocation's verdict under the current key.

## 3. Implementation and native checks still needed

1. Extract the two AP implementations' edition-independent settings, counters,
   failure policy and integer walk without weakening AP admission; provide FL
   profile/stub inputs above and preserve the current AP fixtures. Bind FL sites
   at admitted loaded base; local windows, target prefixes and helper ABI are
   required in addition to edition recognition.
2. Build FL-shaped fixture windows with real stack/register transport and a
   recording ECX-argument release. Dust: all three non-null failures, null,
   successful mixed fills, one release/end per invocation, original tail after
   rollback, no dangling dereference, exact restore, foreign bytes, late install,
   RX arena with writable external hit counter, GPR/ESP/LastError/FP sentinels.
   Light: native admit/reject, directional bypass, root success/reject/limits,
   EDX/ECX and SIMD sentinels, nested/repeated mesh-part calls, context/cache
   invalidation and failure fallback, exact targets, rollback and teardown.
3. These fixtures must run under the owner's serialized queue and on native
   Windows when available. They close adapter ABI/state preservation, **not**
   correctness of real engine release by a stand-in. Qualify the actual release
scan with no matching references or correct matching cleanup, and measure
failure-release cost; neither follows from empty newborn fields. A user-launched FL missing-
   resource flight must show bounded dust hit counts and stable unattached-node
   population; an opt-in light flight must show root decisions with unchanged
   native-admit behavior and useful repeated-part cache rates. No agent launch.
   Native Windows behavior and FL game results remain unverified by this study.

## Independent review (2026-10-29)

Independent review reproduced all 24 checks and accepted the static hook/ABI
contract after the external-reference scan and EDI-arrival qualifications above.
The proposed fixture remains a transport/state check, not proof of real release
cost or recursive cleanup. No production implementation or activation is accepted.
