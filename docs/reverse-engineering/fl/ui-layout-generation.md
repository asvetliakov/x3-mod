# FL tooltip bounds and UI layout generation

Scope update, 2026-10-28: the [AP/FL save-load comparison](ui-save-load-comparison.md)
finds no need for additional layout repair or KC completion hooks at unchanged
configured scale/resolution. The user excludes in-game resolution changes.
Earlier repair/changed-generation requirements below are conditional engineering
proposals, not active port prerequisites. Native borrow/ABI findings still apply.


Offline study, 2026-10-28. Implements the bounded evidence request in the
[ratified UI design](../../architecture/farnhams-legacy-ui-implementation.md).
Reuses the [input](ui-input.md),
[graph](ui-graphs.md),
[load](ui-load-readiness.md) and
[callback](ui-render-exclusion.md) contracts. Addresses are
preferred FL VAs; KC addresses are file CODE offsets. No production modification,
build, game/Wine execution, installation or commit. This is not activation approval.

## Result

Both tooltip bounds have complete, relocation-free detour spans. Successful
return from startup display creation supplies a concrete **initial** generation
publication boundary before the game story initializes clipping, cursor or menus.
The native device is identifiable independently of arbitrary intercepted devices.

The native Reset route is device-loss recovery, not a KC resize transaction.
Its failure return is ignored by its Present caller; the caller's ready flag is
therefore not a success witness. Reset passes the persistent presentation block
by address to an API which is allowed to change it. On Windows, documented Reset
output zeroes width and height; a later windowed Reset can use client dimensions.
Thus absence of later CPU dimension stores alone does **not** prove invariant
back-buffer dimensions.

No ordinary native user-resize route was found: game windows are fixed-size or
borderless, SC_SIZE/SC_MOVE/SC_MAXIMIZE are consumed, WM_SIZE does not rebuild the
display, and the mod's monitor-rectangle correction preserves configured extents.
There is no demonstrated dimension-changing user event in this evidence. Initial
auto selection and recovery with **verified equal actual dimensions** have a
concrete contract. A successful changed-size Reset still has no proved KC rebuild
boundary. Whether the supported native/OS window contract excludes that case is
the remaining bounded admission decision; it must not be assumed from Reset
success or from the two CPU initialization stores.

## 1. Tooltip spans and liveness

The function `0x42fe00` has ECX=tooltip owner and five stack arguments, returns
`ret 0x14`, and saves EBX/EBP/ESI/EDI without a separate local frame. At the bounds
sites ESP is entry ESP minus 16. Its x/y arguments are current `[ESP+0x1c]` and
`[ESP+0x20]`; `[ESP+0x14]` has already been overwritten with constant 2.
`*0x5a82f8` is the display container; `EDX=[container+0]` is the real
screen surface record, EDI the tooltip owner. These tooltip
scenes are ordinary scalable UI, as established by the scene-lifetime study.

| Span | Displaced effects and required virtual replacement |
| --- | --- |
| `0x42ff32..0x42ff39`, 7 bytes (4+3) | Signed word `[EDX+4]` to ESI; `lea EBX,[EAX+6]`. Substitute committed virtual width in ESI, retain native EBX calculation. EAX is the just-computed text-height quotient; EBP holds tooltip width. |
| `0x42ff58..0x42ff60`, 8 bytes (4+4) | Signed word `[EDX+6]` to ECX; load EDX from `[ESP+0x20]`. Substitute committed virtual height in ECX **before discarding the screen pointer**, then reproduce the EDX y-argument load using the original stack location. ESI is the selected x coordinate, EBX tooltip height, EBP=2. |

Width continuation loads x into EAX, compares x+tooltip-width to ESI at
`0x42ff40`, then picks the right-clamped or left-minimum x. Its right-clamp branch
jumps **to the height span's start** at `0x42ff4a`; that legal entry must be hooked
with the same semantics as fallthrough. Height continuation compares y+height to
ECX at `0x42ff63`, then uses its original above-cursor/lower-minimum placement.
Do not replace that placement algorithm with a guessed bottom alignment.

No incoming arithmetic flags are consumed before the next native CMP on either
path; the displaced loads/LEA themselves preserve flags. Keep exact native replay
when the coordinate group is uncommitted. Preserve all registers other than the
displaced destinations, stack arguments and FP/SIMD/LastError if bookkeeping is
called. In particular, do not use EDX as a scratch register before the height
read or lose EAX before the width LEA. Neither span calls native code; any new
helper callback must remain non-reentrant bookkeeping and use the qualified
four-byte incoming stack/preservation envelope.

Both spans have zero HIGHLOW operands and zero surveyed direct interior entries.
The width span begins at qword offset 2 and crosses the aligned qword; the height
span is aligned and occupies eight bytes. Quiet startup installation/rollback is
still required. Exact bytes are bound by SHA and instruction/operand checks in
the probe; raw listings stay untracked. These bounds must use the **same committed
generation** as screen queries, projection and cursor conversions.

## 2. Initial device owner and publication

The ownership chain is `cache=*0x5aaf94`, `record=[cache+0x18]`,
`IDirect3DDevice9*=[record]`. `0x4f1e10` allocates/zeroes the 0x1a8-byte record and
publishes it at `0x4f1ec1`. At the CreateDevice call `0x4f2489`, EDI is that
record: the output pointer is **EDI**, presentation parameters are **EDI+0x164**,
factory is `[cache]`, and focus window comes from `*0x5aaf08`. Seven COM words
include the factory `this`. The caller branches to failure for nonzero HRESULT.
This is an actual output-slot identity, not an inference from the latest device
created by the process.

Bind a staged successful device observation to that native factory/output slot
and record, and verify the resulting device equals `[record]`. A separately
created device must not replace this binding. The sole direct display-device
creator is `0x4f3e0a → 0x4f1e10`; the sole outer display-setup call is
`0x47765a → 0x516890 → 0x4f3a30`. No native multi-device UI ownership policy is
inferred from a generic capture hook that supports many devices.

The concrete outer initial publication seam is the call
**`0x47765a..0x47765f`**, a five-byte relative call to `0x516890`. Entry ECX/DX
carry configured width/height (signed word reads by the callee); two caller-cleaned
stack words carry depth and flags. `0x47765f` performs `add esp,8`, then stores
EAX as the display pointer at `0x477662`. Preserve native EAX, all other returned
state and the original two-argument cleanup. A call wrapper must copy both stack
arguments and retain the incoming ECX/EDX; a no-argument C++ cast is invalid.
The span fits its aligned qword, has no HIGHLOW operand or surveyed direct
interior entry; reconstruct its relative target when relocating it.

On normal non-null display return, match the previously observed successful
device and actual back-buffer dimensions against the created screen dimensions.
**The returned EAX is a display container, not a surface record.** The finite
chain is `container=returned_EAX`, `surface=[container+0]`, then signed words
`[surface+4]` / `[surface+6]`. The container's `+4` is a second surface pointer,
not width/height. Use checked reads rooted in the normal returned owner; the
factory/record/device binding remains independently checked.

The wrapper allocates its 0x13c-byte container at `5168a1/5168a6` and publishes
it to `5a82f8` at **`5168ab`**, before calling device setup. Thus the later caller
store at `477662` is not the first global publication, and its ordering alone
does not prove the global is null/stale at return. Primary surface allocation is
0x64 bytes at `4f3ed7/4f3eda`, stored at `[container+0]` at `4f3ee5`; `4f3f05`
loads that slot into ECX before the initializer call. A separate 0x64-byte surface
is stored at `[container+4]` at `4f4020`, loaded at `4f403e` and initialized at
`4f4041`. Screen queries load slot0 at `4b23d3/4b23f8` before their signed
word reads, and tooltip bounds use the same slot at `42ff30`. The ordinary
wrapper return reloads the container at `5169c8` and returns it at `5169ce`.
These distinguish both stores, both surface slots and the actual query owner.

The surface initializer `0x4f5c70` calls GetDesc at `0x4f5cad` and stores width and
height in screen+4/+6 at `0x4f5de6/0x4f5dee`; it is called at `0x4f3f07`, after
device creation. Screen queries subsequently read this record. Thus the outer
boundary has both device and CPU screen state; the earlier CreateDevice callback
alone does not yet have the latter. Missing/mismatched/nonpositive/unsupported
signed-word dimensions refuse initial coordinate commit, not a guess from zero
presentation outputs. A null setup result reaches fatal startup; do not publish
on failure or foreign unwind. Initial publication performs no relayout because
the game UI has not yet initialized.

There can already be a VM at this point: startup may load **x3config**, invoke
`_Init` at `0x4775d7`, and show its native configuration dialog. Do not claim the
VM is necessarily null. The fixed addon2 x3config object is 660 bytes, SHA-256
`a77611e0fad6a0cb89059c561886f313d438b28a72e58c042882e1b05bbffa31`; its 104-byte
CODE has no class methods and uses only table allocation, formatting, file-read
and XML-import natives. It has no game cursor/menu layout path.

After display setup, `0x4776b1` constructs input. Later outer-loop story loading
is `0x477d15 → 0x4b8e20`; `0x477e5b` dispatches the current class's `Run`.
Class 150 Run calls `502::Init` at KC `0x16970`. Init calls SetClipping at
`0x17d613`, then CenterCursor at `0x17d61a`, before constructing cursor assets.
SetClipping obtains virtual width/height and writes members 10/11; CenterCursor
writes members 0/1 as their halves. CenterCursor does **not** call UpdateCursor;
later UpdateCursor performs the reviewed native steering-store/listener path.
Initial generation publication precedes all of these game-story operations.

## 3. Reset, API aliases and native window behavior

`0x4f39d0` calls Present, tests DEVICELOST and TestCooperativeLevel, and only for
DEVICENOTRESET calls `0x4f3740` at `0x4f3a18`. The sole direct caller of this
Present/recovery wrapper is `0x500c77`. The reset helper releases old surfaces,
loads the bound device into ESI and passes **record+0x164 directly** at
`0x4f37cd` (COM slot +0x40). There is no temporary copy of the parameter block.

On failed HRESULT, `js 0x4f38b8` returns zero without the success-only surface
reacquisition/state/resource reset. Success returns one at `0x4f38b1` after that
work. The caller nevertheless writes `*0x5aaf38=1` immediately at `0x4f3a1d`,
without testing the helper result. Neither this flag nor Present's return
establishes successful Reset, actual dimensions or layout completion. Use the
actual HRESULT/device observation; failed Reset retains the committed generation
and cannot justify GetBackBuffer/GetDesc on the lost device.

The typed CPU width/height stores in device construction are `0x4f22d2` and
`0x4f22db`, from the selected mode record; creation zero-initialization is another
writer. No width/height refill occurs anywhere in the complete reset helper.
The full-text offset census has 111 references to +0x164/+0x168, mostly unrelated
ship/cockpit objects and stack data; it is not a typed alias proof by itself.
The important escaped aliases are positive: CreateDevice gets record+0x164,
and Reset gets the same address. Both APIs take input/output structures.

Microsoft documents that Reset sets width/height/count to zero on return, while
windowed zero dimensions select the current device-window client dimensions.
Consequently a later reset can acquire changed client dimensions even with no
new native CPU store. This is a documented conditional possibility, not an
observed resize. [Reset](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-reset),
[presentation parameters](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dpresent-parameters).
CreateDevice instead resolves initially zero dimensions in its output structure;
actual back-buffer observation remains preferable for publication.
[CreateDevice](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3d9-createdevice).

The inspected native game window path supplies additional negative evidence:

- Normal window style construction uses caption/system/minimize controls without
  a sizing frame, or a borderless popup. The `0x2000` special display mode that
  uses the resizable `0x10cf0000` style returns at `0x4f3be8` before creating a
  game D3D device; it is not the qualified game-device path.
- SC_SIZE=0xf000, SC_MOVE=0xf010 and SC_MAXIMIZE=0xf030 go to the handled return
  at `0x4ec8df` (LRESULT=1). Do not describe SC_SIZE as initiating a reset.
- WM_SIZE=5 goes through `0x4ec7f3 → 0x4ec90e`; the input filters eventually
  forward it to DefWindowProcA. It does not call display setup or Reset and does
  not notify KC of dimensions. WM_MOVE follows the same route.
- The current `window_mode::apply` receives width/height **by value**, changes no
  presentation field, and moves only a monitor-sized borderless window already
  having those extents at the work-area origin. Its predicate rejects zero or
  mismatched back-buffer dimensions. It supplies no dimension-changing UI path.

These facts exclude the obvious native drag/maximize/menu reconfiguration
explanation. They do not prove that the current client extent is invariant under
all ordinary OS/display circumstances after an API-zeroed reset. No alternative
native mode-change setter or supported mod parameter mutation was found in this
bounded owner trace. Do not equate that with an observed resize, nor add support
for arbitrary foreign private-address calls as a requirement.

## 4. KC rebuild coverage and the precise remaining seam

The corrected story call census includes all 243 SYMB global functions as
well as CLAS methods. SetClipping occurs in `502::Init` and global `_Restart`
(`0xd353`); `503::Reload` occurs in `_Restart` (`0xd385`). `_Restart` spans
`0xd1bb..0xd3bb`. The earlier reader used only CLAS entries as boundaries and
omitted opcode `0x84` direct-global calls: it incorrectly attributed these calls
to class 202 Patch7. That attribution and the earlier claim that `150::Restart`
calls only X2_SetModified are withdrawn. `150::Restart` also directly calls
`_Restart` at `0x16b04`.

Reload conditionally calls `Resize` on its current menu at `0x28a89`;
base-menu Resize queries maximum dimensions then invokes virtual SpecialResize
at `0x18dc42`. Graph class 2327's implementation is `0x23d821`. Other explicit
base-menu Resize calls belong to local maximize/custom-setting methods.
These calls establish a real restart refresh path, not complete coverage of
saved cursor/menu state or a device/reset notification. The native restart
invocation and remaining saved-owner coverage are being qualified separately.

The complete native reset body contains no direct VM callback-dispatch call.
Its successful surface rebind copies surface pointers into CPU screen records
at `0x4f37fa/0x4f3805`; it does **not** rerun `0x4f5c70` to publish screen width
and height. Calling SetClipping or 503::Reload alone would therefore not be a
proved transaction: screen-query dimensions can remain old, cursor members need
resynchronization, and Reload's current-menu coverage is not all UI ownership.
The existing restart path is not by itself permission to invoke the entire
restart operation from Reset. No unqualified KC invocation is proposed here.

For initial startup, commit at the outer display-return seam above. For verified
same-device/same-actual-size recovery, retain the committed generation and CPU
roles; no dimension-driven relayout is necessary. For genuinely different actual
dimensions, stage the observation but **do not commit a new factor**. The exact
missing native seam is a complete owner-thread transaction that updates CPU
screen dimensions and rebuilds all live KC clipping/layout/cursor state before
coordinate use resumes. Alternatively, a narrowed supported window contract must
positively establish that such changed dimensions cannot occur on its admitted
native/OS route. Neither alternative is closed here. Quarantining a contract
violation is a failure policy, not implementation of dynamic resize.

## 5. Input context and carry lifetime

`*0x5a8478` is allocated/published only at `0x4ebf0a`, in `0x4ebef0` whose sole
direct caller is startup `0x4776b1`. Allocation is 0x4fc bytes. The complete
absolute-store census finds that publication plus zero stores at `0x4ec004`
(initialization failure through `0x4eec8b → 0x4ebfd0`) and `0x476a07` (fatal
teardown). Thus a surviving normal session has one input-context incarnation;
no native success-path replacement or Reset recreation was found. A startup
failure frees it and returns null, then reaches fatal startup. This rules out
input-context address reuse within the qualified surviving session, not reuse
under arbitrary foreign calls.

At direct callback preparation, use the observed input-context pointer plus
owner/callback context (`owner+0x418`), class (`owner+0x4dc`) and route selected
by `owner+0x4d8==1`. The mapped callback is the other route and targets class 150.
Each X/Y carry is private to its route; do not share direct and mapped residue.
Reset on initial generation publication, accepted later layout generation, and
observed context/route change before adding the next delta; commit the new carry
before the synchronous native callback. The existing load scopes can explicitly
invalidate carries for a restored UI session without changing scale or roles.
Pointer comparison is not sufficient for restored KC cursor state: the native
input allocation survives load. Whether repeated same-context scripted Init
needs a further cursor-lifecycle epoch belongs to any future explicit rebuild
transaction; no such Reset callback is established here. No per-draw pointer
walk or allocation is required by these tokens.

## Verification and limits

[verify_fl_ui_layout_generation.py](../../../verification/probe/verify_fl_ui_layout_generation.py)
passes **65 instruction/operand anchors, six complete bounded regions (465
decoded instructions), four mutation negatives and six policy examples**. It
binds EXE/story/config hashes and checks tooltip instruction boundaries, direct
interior entries/relocations, owner calls, Reset success/failure CFG, the input
publication census and complete KC call references. Four operand/branch mutations
bypass the image hash and must fail: wrong tooltip field, wrong replay destination,
reversed Reset success branch and wrong device-record member. Six policy examples
exercise foreign devices, failed/unknown/same/changed dimensions and the documented
API-zeroed alias. They are sequential requirements, not native wrapper tests.

```sh
python3 verification/probe/verify_fl_ui_layout_generation.py --raw
python3 -m py_compile verification/probe/verify_fl_ui_layout_generation.py
```

Raw listings, extracted config and producer/result JSON remain ignored under
`verification/results/fl-port/ui-layout-generation/`. Initial publication wrapper,
SEH/nonlocal exits, actual back-buffer observation and native Windows/CrossOver
behavior require implementation acceptance. No enabled capability, live resize,
visual result or native-window runtime measurement is claimed. Shared capture's
post-Reset observation correction is independently owned by the parent and does
not by itself supply the missing KC transaction.

### KC reader correction checks, 2026-10-28

`verify_fl_ui_graphs.KC` now parses the SYMB count and four-word global records,
uses global and class entries as instruction-region boundaries, and reports
opcode `0x84` calls instead of silently omitting them. The corrected layout
verifier asserts `_Restart` ownership and its edge from `150::Restart`.
Three synthetic regressions in `test_fl_kc_reader.py` distinguish a class call
from the following global function's native call, reject an unknown global
target, and reject a truncated symbol table. No copyrighted script bytes are
embedded in these tests.

Affected offline checks passed: layout 65 anchors / 4 mutations / 6 policy cases;
graphs 17 line / 7 rectangle / 4 text calls; input 74 semantic checks; render
exclusion 84 semantic checks / 5 mutations / 9 policy cases. Reproduction:
`PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_fl_kc_reader`
and the four `verification/probe/verify_fl_ui_{layout_generation,graphs,input,render_exclusion}.py`
scripts. This corrects call attribution; it does not qualify every saved UI
owner, actual KC completion, or a Reset generation transaction.


### Returned-owner correction, 2026-10-28

The initial implementation incorrectly treated the global's container value as
if it were the surface record. Its fixture likewise used one flat object for
both meanings and returned a non-pointer sentinel, so passing publication tests
did not establish the real owner chain. Production now follows returned EAX's
slot0 before reading dimensions. The targeted `--initial-owner-only` mode of
`verify_fl_ui_layout_generation.py` verifies **30 native anchors** and rejects
**five semantic mutations** covering early global publication, primary/secondary
slot stores, the query pointer load and dimension offset. Its compact result is
`verification/results/fl-port/ui-layout-generation/initial-owner.json`.
Null/poisoned/stale-global cases in the corrected synthetic fixture are deliberate
negative perturbations; they do not claim the qualified native callee leaves its
global unpublished. Native fixture evidence is in the
[FL ledger](../../verification/farnhams-legacy.md#returned-display-owner-correction-2026-10-28).
