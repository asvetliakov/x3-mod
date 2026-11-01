# Farnham's Legacy voice DMO post-Init contract

Read-only reconstruction, 2026-10-29. This establishes the FL counterpart of
the AP voice-decoder fallback seam; it does not implement or qualify an FL
decoder. The existing behavior and AP runtime evidence belong to
[voice-decoder-adapter.md](../../architecture/voice-decoder-adapter.md#dmo-fallback-hook-2026-09-14)
and [voice-startup-sequence.md](../voice-startup-sequence.md), sections 12–13.
The outer media allocation/caller study is separately owned; addresses below
identify only its constructor boundary and the decoder's lifetime.

## Inputs and retained evidence

Run `python3 verification/results/fl-port/voice-dmo-contract/reproduce.py`.
The [producer](../../../verification/results/fl-port/voice-dmo-contract/reproduce.py)
and [compact result](../../../verification/results/fl-port/voice-dmo-contract/summary.json)
record input/source hashes, tool versions, checks and negative controls. No
game, Wine, native fixture, DLL build or installation was performed. Bottle,
WineArch and emulation variables are consequently not applicable.

| Input | Measured size | Measured SHA-256 |
| --- | ---: | --- |
| `/mnt/c/X3AP-orig/X3AP.exe` | 2,153,984 | `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` |
| `/mnt/c/X3FL-orig/X3FL.exe` | 2,804,736 | `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8` |

These hashes identify inspected inputs, not a proposed runtime DLL-hash
dependency. Both use preferred base `0x00400000`. The existing AP verifier
passes 9 checks. FL passes 16 check groups; Capstone 5.0.7 and GNU objdump
2.47 agree on all boundaries of the complete constructor
`0x004fda20..0x004fe5ee` (exclusive, 910 instructions) and filter cleanup
`0x004ffc60..0x004ffd6f` (94 instructions). Twelve in-memory negative controls
are rejected: start/end inside an instruction, incoming branch to span
interior, changed object register/filter field, Init/Release slot, speech
GUID, saved-result local, retry target, success condition and cleanup field.
The producer checks semantic operands and argument/control-flow sequences,
not just occurrence of the hook's bytes. It retains no raw listing or image.

## 1. Site, register and stack contract

| Property | AP | FL |
| --- | --- | --- |
| Containing constructor | `0x004cf460` | `0x004fda20` |
| Original `Init` call | `0x004cfd44`, indirect EDX | `0x004fdfb0`, `[ECX+0x0c]` |
| Post-call hook | `0x004cfd46` | **`0x004fdfb3`** |
| Eight displaced bytes | `8bf081fe0e000780` | **`8945e43d0e000780`** |
| Displaced operations | ESI = EAX; compare ESI with `E_OUTOFMEMORY` | `[EBP-0x1c]` = EAX; compare EAX with `E_OUTOFMEMORY` |
| Instruction lengths | 2 + 6 | 3 + 5 |
| Continuation | `0x004cfd4e` | **`0x004fdfbb`** |
| HRESULT / PUSHAD index | EAX / 7 | EAX / 7 |
| Media object / PUSHAD index | EBX / 4 | **EDI / 0** |
| Decoder filter | object `+0x9c` | object `+0x9c` |
| Game-owned wrapper-interface local | `[ESP+0x14]` | `[EBP-0x14]` |
| Game attempt counter | `[ESP+0x10]` | ESI |

The FL span starts immediately after the three-byte indirect call and contains
two complete, position-independent instructions. No direct call/jump in the
containing constructor targets its start or interior; entry is the call's
fall-through. Both instructions can be copied to the existing trampoline
without relative relocation. This is a containing-function edge check;
arbitrary indirect control transfers and cross-function computed targets are
not proven absent by the static producer.

FL's prologue saves the incoming stack position in EBX (`0x004fda21`), aligns
ESP to eight bytes (`0x004fda26`) and establishes EBP (`0x004fda34`). EBX is
therefore live for stack restoration, not an FL media pointer. EBP is live for
the displaced store and all subsequent locals, EDI is the allocated object
(`0x004fda7e`), and ESI is live as the Init attempt count. Restore ESP and all
GPRs except the intentionally replaced EAX; in particular do not reuse AP's
ESI-as-result convention. The original call pushes category, CLSID and `this`
at `0x004fdfa3/0x004fdfa8/0x004fdfad`; no caller stack adjustment follows, as
expected for x86 COM `STDMETHODCALLTYPE` with 12 callee-popped argument bytes.
The injected C helper must realign its own stack; eight-byte engine alignment
does not establish sixteen-byte alignment.

On a successful fallback, replace saved EAX before replaying **both** FL
instructions. This updates both the result local and compare flags. On every
skip or failed fallback, preserve the original HRESULT. Pre-hook arithmetic
flags are overwritten by the replayed compare; preserve the remaining flags
(including DF) and restore them before replay. Preserve x87 stack/control/status,
MXCSR, XMM0–7 and the post-original-call thread LastError around injected COM
work, matching the existing emitter and `PreserveCpuState` envelope. LastError
is not an HRESULT result channel. Its preservation is required for transparent
injection even though this local continuation does not read it.

## 2. Branch selection and COM lifetime

The manual speech branch is selected at `0x004fdf22..0x004fdf2e` by
`(flags & 0x110) == 0x110`, after the earlier local/flag tests at
`0x004fdeae..0x004fdec4`. The earlier `flags & 8` path and a nonzero
`[EBP-0x20]` select the preceding decoder branch instead. Consequently the
post-Init seam does not intercept arbitrary graph decoding or every audio
cue. This matches the AP branch described in
[media-cue-playback.md](../media-cue-playback.md), section 4.

| Stage | FL anchors and established contract |
| --- | --- |
| Create wrapper | `0x004fdf3b` takes `&[EDI+0x9c]`; arguments at `0x004fdf48..0x004fdf57` create CLSID_DMOWrapperFilter with null outer, context 3 and IID_IBaseFilter. ESI holds the CoCreateInstance IAT `0x005503d8` value, loaded at `0x004fda94` and reloaded after earlier counter use at `0x004fdbc9`. |
| Query wrapper interface | `0x004fdf82..0x004fdf94` QIs the `+0x9c` IBaseFilter for IID_IDMOWrapperFilter into `[EBP-0x14]`; negative HRESULT skips Init at `0x004fdf98`. |
| Init | `0x004fdfa0..0x004fdfb0` loads that view and calls vtable slot `+0x0c` with speech CLSID `0x00590cd8` and audio category `0x00590cb8`. |
| Release queried view | `0x004fdfcf..0x004fdfdc` null-checks `[EBP-0x14]`, calls Release at vtable `+8`, and clears the local. This occurs after the hook and both game attempts. |
| Add wrapper to graph | `0x004fe05f..0x004fe072` null-checks `+0x9c` and calls `[object+0x78]->AddFilter(filter,NULL)` at slot `+0x0c`, even after failed Init. |
| Remove and release owned filter | helper `0x004ffc60`: `0x004ffcf4` calls graph RemoveFilter at slot `+0x10`; `0x004ffd04` releases `object+0x9c`; `0x004ffd07` clears the field. Destruction `0x004ffd70` reaches this helper at `0x004ffdce` before releasing the graph. |

GUIDs were checked as 16-byte values as well as referenced operands:
CLSID_DMOWrapperFilter `0x00590cc8`, IID_IDMOWrapperFilter `0x0056b140`,
IID_IBaseFilter `0x00550aa4`, and speech/category addresses above. These are
edition-specific engine anchors, not Wine-private layout.

At the hook the game retains both its IBaseFilter and queried wrapper view.
The existing retry's separate QI must obtain its own reference, call Init on
that returned view, then release exactly that reference; it must neither
release the borrowed `+0x9c` pointer nor steal the game's local view. A QI
result need not equal the game's view pointer. These ownership rules follow
Microsoft's [IUnknown reference-counting contract](https://learn.microsoft.com/en-us/windows/win32/com/implementing-reference-counting).
Microsoft also documents the [create/QI/Init/release/AddFilter sequence](https://learn.microsoft.com/en-us/windows/win32/directshow/using-dmos-in-directshow).

## 3. Retry, failure and exception behavior

FL checks `E_OUTOFMEMORY` at `0x004fdfb6`; equal takes helper `0x004d9200`
at `0x004fdfbd`, then reloads the saved HRESULT at `0x004fdfc2`. Other results
skip to `0x004fdfc5`. Increment ESI, test EAX's sign, and exit on success at
`0x004fdfc8`. On failure, `ESI <= 1` returns to `0x004fdfa0`; the game issues
at most two original speech Init calls. After either success or exhaustion,
the game releases its queried view and decides whether to add the wrapper
using the `+0x9c` pointer, not the Init HRESULT. Failed wrapper creation and
failed QI converge on the same release/null-check path. A failed Init is not
itself a constructor-abort branch.

The existing exact `REGDB_E_CLASSNOTREG` (`0x80040154`) gate remains meaningful:
the original speech Init finished, its interface is live, no streaming or
AddFilter has begun on this wrapper, and the EXE already permits another Init
on the same view after failure. On successful WMA Init, substituting EAX
drives FL's original success branch and suppresses the second speech attempt.
On QI failure, missing pointer or WMA Init failure, preserving EAX retains
the original two-attempt/failure path. It does not convert other HRESULTs or
silently claim recovery. A failed fallback may be invoked again after the
game's second failed speech attempt: at most two injected retries per wrapper
on this path, consistent with AP's existing logic.

**Limit:** this proves control-flow and reference-lifetime compatibility,
not universal rollback inside every wrapper implementation. Microsoft's
[Init contract](https://learn.microsoft.com/en-us/previous-versions/ms785136(v=vs.85))
specifies CLSID/category and success/failure HRESULTs, but does not guarantee
retry after failed initialization, changing CLSID, or transactional failure.
AP's previously measured CrossOver success is reusable implementation evidence
for that backend; it is not FL or native-Windows runtime qualification.

The constructor installs x86 SEH via FS:[0] at `0x004fda38..0x004fda44`, handler
`0x0054f14b`, and carries unwind-state byte 2 at `0x004fdea4`. Normal success
restores FS at `0x004fe5df`. HRESULT failures take the explicit branches above;
they are not exceptions. An injected COM fault is not converted into a failed
retry by the current helper's `noexcept`, and its fault witness continues
exception search. Do not assume the normal Release sequence runs after a
fault or C++ termination. This note does not establish exception recovery or
new unwind guarantees through the emitted stub.

## 4. Threading, minimal edition binding and cost

The synchronous retry executes on the original Init thread with stack-local
record/view state; it adds no apartment transfer. COM calls precede the
existing telemetry publication sequence (`written.load`, ring assignment,
`written.store`), which contains no outgoing call. Independent nested
construction on the same thread therefore does not interleave that write
sequence. Neither COM's public contract nor this local routine establishes
that recursive Init of one wrapper is safe. The single-writer ring still
requires caller evidence excluding simultaneous construction/reporting on
different threads; the AP main-thread comment alone is insufficient FL proof.
The independent allocator study identifies the immediate constructor chain
`0x004b46be -> 0x004b4080 -> 0x004b40ce -> 0x004fda20`: several flags-zero
callers receive effective `0x110` for media IDs 101–999, except 800
(`8`) and 810/811 (`0x100`). Other paths forward caller/serialized flags.
This restricts the relevant media population but does not close worker or
indirect callback thread origins. If that restriction remains unproven, a
small shared diagnostic-publication change or confining only diagnostics to
their report owner can remove the ring assumption; fallback itself must still
execute on every native call thread. No global thread/COM framework is needed.

The minimal prospective port is one shared retry body with an immutable
edition binding selected during initialization: recognized executable/profile,
site/expected bytes/span/continuation, object PUSHAD index, and supporting
anchors. FL selects EDI index 0; AP retains EBX index 4. Filter offset `0x9c`,
HRESULT index 7, COM GUIDs, exact failure gate, reference handling and success
replacement can be shared. Validate the FL site within its own admitted
profile; do not globally broaden `object_trace::executable_verified()`, which
would authorize unrelated AP hook families. Keep claim/install-window,
preflight, rollback and process-lifetime trampoline rules from `engine_patch`.

The existing inactive/success path pays the saved-state stub, atomic hit and
one HRESULT comparison per speech Init, with no filter read, allocation,
formatting or COM retry. Activation adds one checked pointer read, QI, Init,
Release and fixed-ring publication. Edition selection and executable/site
validation belong at initialization, not per hit. This is constructor/load
work, not per draw/frame or per audio sample. Existing AP decoder first-load
costs in the architecture note are historical measured values; no FL timing
or FPS figure was measured here. No new fault framework, backend export or
decoder implementation is indicated by this reconstruction. Independent review
requires a small diagnostic-only atomic try-guard because the current ring and
reporting cursors have unproven single-thread assumptions. Publish after COM
work; copy at most eight records and advance reporting cursors under the guard;
release before logging. Drop diagnostic writes or defer reporting on contention.
Actual fallback remains available on every original call thread, and no guard
may span QueryInterface, Init, Release or logging. The parent accepts this
bounded synchronization approach; it is not implemented at this RE checkpoint.

Required remaining evidence: the diagnostic publication correction and its
contention witness; an FL-shaped injected replica checking EAX, EDI/ESI/EBP/EBX, stack/local/flags,
CPU/LastError, failure continuation and exact Release balance; and actual
decoder graph run/teardown on FL CrossOver and native Windows. Native Init
success leaving the fallback dormant establishes transparency only. Native
missing-speech-class fallback must also work to establish enhancement parity;
documented-API source compatibility and static inspection do not verify it.
The matching speech CLSID in the two EXEs also does not independently verify
the compressed format of every FL voice asset.

Independent review reproduced the retained ABI and lifetime evidence and
accepted this static contract with the diagnostic correction required before
FL enablement. Same-wrapper retry behavior remains an empirical backend
qualification obligation, not a documented transactional Init guarantee.
