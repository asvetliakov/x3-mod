# FL field of view: constructor, script setter and save restoration

Bounded offline study, 2026-10-29. This establishes the FL native equivalents
of the three sites currently implemented in [AP FOV](field-of-view.md), plus
their immediate readers and ownership constraints. It does not install an FL
feature or qualify new runtime wrappers. Clean inputs are `/mnt/c/X3FL-orig/X3FL.exe`
and `/mnt/c/X3AP-orig/X3AP.exe`; `/mnt/c/X3` was not used. Preferred base is
`0x400000`. All runtime addresses below mean **admitted loaded base + RVA**;
preferred VAs are labels for the offline evidence.

The shared remap and save policy carry over; **the generated stub registers,
expected windows, relocation handling and constructor write mechanism differ**.
In particular, FL's four-byte constructor immediate crosses an aligned qword.
AP's atomic literal writer cannot implement that FL write or its late restoration.

## 1. Exact sites and independent proof

| Purpose | AP preferred VA | FL preferred VA / RVA | FL instruction/operand contract |
| --- | --- | --- | --- |
| Registry constructor | `0x41c960` | `0x41df90` / `0x1df90` | ECX=registry, saved to ESI at `0x41dfac`; returns registry in EAX. |
| Constructor default instruction | `0x41c9d9` | `0x41e08a` / `0x1e08a` | Seven-byte `MOV dword [ESI+0x24],0x4000`; immediate at **`0x41e08d` / `0x1e08d`**, four bytes. Validate full window `[0x41e076,0x41e098)`. |
| INS_SetFocus case | `0x42dbed` | `0x430e9f` / `0x30e9f` | Opcode `0x21`; whole case `[0x430e9f,0x430ec2)`. |
| SetFocus detour | `[0x42dbf8,0x42dbfe)` | **`[0x430eaa,0x430eaf)`** / `[0x30eaa,0x30eaf)` | One five-byte `MOV EAX,[registry_slot]`; replay then resume at `0x430eaf`. |
| Registry serializer | `0x41c6e0` | `0x41d9a0` / `0x1d9a0` | ECX=registry; stack+4 byte=save flag, stack+8=stream; `RET 8`. |
| Load detour | `[0x41c8c1,0x41c8c7)` | **`[0x41df09,0x41df0e)`** / `[0x1df09,0x1df0e)` | Two instructions, lengths 2+3: `MOV AL,1; MOV [EDI+0x24],EDX`; replay then resume at `0x41df0e`. |
| Focus slot | `0x608504` | **`0x5a82e4` / `0x1a82e4`** | Pointer to cockpit registry, focus DWORD at `+0x24`. |
| Update reader | `0x421148` | **`0x422a1c` / `0x22a1c`**, field read `0x422a25` | Slot → EAX, focus → ESI. The four-byte stack store between those reads means AP's nine-byte reader pattern is not an FL pattern. |

The registration at `0x41df2f` publishes dispatcher `0x430540`; the paired
name-table registration at `0x41df66` publishes `0x5a3d98`. Entry `0x21` of
that name table (`0x5a3e1c`) names `INS_SetFocus`, and jump table entry
`0x43250c` points to `0x430e9f`. These semantic anchors establish the match
independently of an address delta. The dispatcher receives its opcode from
`[EBX+0x10]` at `0x430564`, bounds it at `0x430579` and dispatches at
`0x430582`; EBX is the saved incoming stack frame, not the registry.

The verifier pins immutable reviewed region digests and exact semantic
instructions. It separately validates slice endpoints against decoding from the
known `.text` origin, including the two detour spans; a complete standalone
decode from the ModRM byte inside the constructor MOV is deliberately rejected.
A direct-edge and relocated-pointer census finds no interior entry into either
detour or the constructor window. This is a fixed-image direct-operand census,
not proof against arbitrary computed or foreign calls to private addresses.

## 2. ABI, liveness, relocation and reentrancy

**SetFocus.** At `0x430e9f`, native code reads the marshalled argument pointer
from `[EBX+0x18]`, pushes zero and `[EBX+0xc]`, then reads the incoming binary
angle into ECX at `0x430ea7`. These two outgoing arguments are already on the
stack at the hook. The displaced instruction replaces EAX, so **EAX is scratch
and ECX is remap input/output**. Preserve EDX, EBX, ESI, EDI, EBP and ESP.
After replay, `0x430eaf` stores ECX into registry+0x24; `0x430eb2` replaces
ECX with VM global `0x5aa174`, and `0x430eb8` calls `0x4c1940`. That callee
uses ECX=VM and stack+4=VM context, stack+8=zero, and returns with `RET 8`.
It replaces EAX at `0x4c1947` and establishes its own flags at `0x4c1952`
before any conditional use. Thus the remap can clobber arithmetic flags;
it must not touch the outgoing stack or FP/SIMD/LastError state.

Emit the existing nearest-integer remap with **EAX in place of AP's EDX
scratch register**: bound ECX, form `round(ECX*360/65536)` in EAX and load the
uint16 table entry into ECX. Then tail-jump through the continuation slot,
replay `MOV EAX,[loaded_base+0x1a82e4]` exactly once, and continue. The native
case does not clamp the incoming angle before storing it. Do not call a C++
helper or add a registry walk here.

**Load.** The final stream read is `0x41dedc → 0x506700`; native code reverses
its four bytes and leaves the decoded focus in **EDX**, owner in **EDI**.
Use EDX as remap input/output and **ECX as scratch** for the exact-match load
rule. Preserve EAX and the other registers until replay; `MOV AL,1` then
establishes the native success result. Epilogue is POP EDI/ESI/EBX; RET 8.
The only surveyed serializer callers are `0x420c62` (save: push 1) and
`0x420db6` (load: push 0), both testing **AL only** immediately after return.
On the load path EDX is overwritten at `0x420dc6`, ECX at `0x420dcb`; the
failure exit reloads ECX from the saved SEH slot at `0x420da1`. Neither needs
the pre-hook ECX. Arithmetic flags are dead: the native epilogue does not use
them and the caller executes TEST AL,AL. This register choice avoids any
reliance on unspecified high EAX return bits. No stack, FP/SIMD or LastError
change is needed in either stub.

Both stubs can be entirely call-free with immutable tables and no mutable
per-invocation globals, making their algorithm reentrant. Install all tables,
continuations and ownership records before publishing code. Do not retain an
engine registry pointer. Native callbacks and serializer exceptions can occur
outside the short stubs; no new exception handler or engine read belongs in
cleanup. The tables/tails must outlive in-flight native execution and any
retained continuation during dynamic DLL unload, as with AP's never-freed
arena. Runtime preservation, foreign unwind and unload behavior still need
execution fixtures; static liveness is not that fixture.

**HIGHLOW and boundaries.** SetFocus's A1 operand is a HIGHLOW field at
**`0x430eab` / RVA `0x30eab`**; the VM operand is another at `0x430eb4`
(RVA `0x30eb4`). The dispatcher and name table entries above are HIGHLOW
fields, as is the reader's slot operand at `0x422a1d`. Normalize expected
absolute operands by the admitted load delta, or construct expected loaded
operands from base+RVA. Relative calls remain relative at a new module base;
new trampoline calls, if any, require newly encoded displacements. The load
detour and constructor MOV contain no HIGHLOW field. The verifier records
all HIGHLOW offsets for every reviewed window, not just the patch bytes,
and checks two concrete synthetic load bases (`0x12000000`, `0x100000`).

The SetFocus five-byte jump starts at qword offset 2; the load jump starts
at offset 1; both fit. **Constructor immediate `0x41e08d..0x41e090` starts at
qword offset 5 and crosses into the next qword (`5+4=9`)**. Its original
instruction starts at offset 2, but that does not make the immediate atomic.
Do not reuse AP's four-byte `CodeOps`/`cmpxchg8b` literal contract for FL.

## 3. Representation, readers and save semantics

The registry layout needed here is unchanged: `+0x10` active cockpit selector,
`+0x18..+0x1a` flags, `+0x1c` node, `+0x20` scalar and **`+0x24` focus**.
This is the cockpit registry, distinct from the engine's generic object maps.
The native allocation is `0x458` bytes; the FOV feature needs only the one
DWORD, not permission to interpret the rest of that allocation.

The FL perspective builder `0x4dec70` takes ECX=output, EDX=camera. It reads
camera+0x298 at `0x4dec97`; the constants at `0x591994`, `0x591ba0` and
`0x591af0` are 2^-16, 2 and pi/2. Their product gives half-angle
`F*pi/65536`, confirming **65536 = 360 degrees**, with `0x4000=90 degrees`.
The existing [camera study](camera-buffers.md) establishes the
cotangent projection. Width/height come from camera+0x300/+0x304 when positive,
otherwise display+0x28/+0x2c through global `0x5a82f8`. Camera allocation
initializes those two overrides to zero at `0x4a46e2/0x4a46ec`.

Display initialization `[0x4f3f10,0x4f3fd9)` computes 3/4 in fixed point,
compares height/width with it and fixes H=0xc000 for aspects at least 4:3,
with W=H*width/height. Narrower displays use W=0x10000 and H=height/width.
Thus the shared AP equation **tan(F'/2)=0.75*tan(F/2)** gives the same
16:9-horizontal interpretation for the ordinary default plane in FL.
Explicit script aspect overrides keep their intentional native semantics.

The cockpit update seeds EDI=0x4000, ESI=registry focus. Internal zoom divides
both by cockpit+0x248; the focus division is at `0x422c14..0x422c27`.
It floors the effective angle at 0x106, writes EDI to cockpit-scene camera+0x298
at `0x422cb0`, and ESI to sector/galaxy/dust cameras from cockpit+0x58/+0x5c/+0x60
at `0x422cc5/0x422cda/0x422cef`. The effective focus cache is
**cockpit+0x240**, stored at `0x422cfd` (AP uses +0x230). Connection mode 6
can use that cache instead of the registry at `0x422c92`; do not remap the
cockpit-scene constant or assume its focus should equal the sector camera.
This study does not claim an exhaustive new census of every downstream camera
consumer; changing the native base preserves the existing native propagation.

The serializer version is 1. Its native save branch reads the registry focus
at `0x41dbe7`, reverses its bytes and writes one four-byte value through
`0x506820` at `0x41dc1d`. Load reads it through `0x506700` at `0x41dedc`,
reverses bytes, then performs the proposed store. There is no native unit
conversion or provenance marker. Retain AP's policy: remap only exact vanilla
`(N<<16)/360`, N=50..130; pass remapped/other values through. Share the current
81-entry math, rounding, parser, constructor collision avoidance and
zero-collision tests; do not replace the load rule with nearest-degree lookup.
Save-side inverse conversion is outside the current AP enhancement and this port.

The separate cockpit cache is also serialized unchanged: save read
`0x41b575`, stream write `0x41b5ae`; load read `0x41c6f6`, cache store
`0x41c736`. This is the FL counterpart of AP's explicitly unimplemented
optional cache-load remap. It can preserve a vanilla cached focus for mode 6
or before the first normal update. The three-site port has the same bounded
limitation; qualifying/patching that optional store would be an additional task.
The resolved FL KC default, menu range, conversion and concrete save/load
paths are established in section 6 below. Actual game execution and the
optional saved-cache behavior remain unmeasured.

## 4. Ownership, startup admission and implementation brief

The slot is in PE zero-fill. Its five surveyed direct native writers are
initial clear `0x475ed3`, teardown clear `0x4763af`, new-registry publication
`0x477e37`, close clear `0x4785f9`, and restored-registry publication
`0x4798b2`. Constructor callers are exactly `0x477e21` and `0x4798a5`.
Both publish only after the constructor returns; restore then calls cockpit
loading at `0x4798bf → 0x420d30`. Close destructs at `0x4785e6 → 0x41e330`,
frees at `0x4785f1`, then clears the slot. The old pointer may therefore be
nonzero during destruction/free; a readable page, plausible angle, compare-exchange
or repeated equal address is not a lifetime lease.

Use the already established [FL initial-static-load entry transaction](../../architecture/farnhams-legacy-install-boundary-decision.md):
after cookie initialization, before the game CRT, callbacks and workers.
At this boundary the native slot has no constructed registry and no initial
heap focus write is needed. Retain the established startup admission; do not
invent a first-Present, late backend-load or dynamic-attach fallback. A
constructor default applies to both later new and restored registries; the
load store then wins with the saved setting under the exact-match policy.

The implementation should separate shared math/table generation and config
from edition-specific descriptors and two small generated register variants.
Add the three FL sites to one startup ownership/rollback group, validate all
windows and HIGHLOW fields first, publish immutable stubs, then change the
literal and both jumps, checking ownership/read-back throughout. Constructor
write and rollback require the existing early exclusion; any failed rollback
retains explicit debt and denies activation. Dynamic removal of the crossing
literal needs separately established quiescence or retained ownership; the AP
atomic shutdown assumption is insufficient. Native Windows and CrossOver use
the same documented memory/protection/cache APIs and x86 contract; no private
Wine state is required or qualified here.

Keep native reads minimal. The feature itself needs no per-frame hook, registry
lookup, lock or allocation: one startup table build/window validation,
a handful of integer instructions per script change or save load, and the
existing constructor literal. Diagnostic confirmation must use the current
registry under the existing qualified owner/load/readiness admission, or an
owned scalar captured at a qualified native seam. Do not port AP's unrestricted
slot/page-readable polling into FL or cache a registry pointer across load/close.
No runtime costs or game FPS were measured by this offline task.

## 5. Reproduction and next finite checks

[verify_fl_fov_sites.py](../../../verification/probe/verify_fl_fov_sites.py) emits
compact derived evidence under `verification/results/fl-port/fov-sites/`.
The producing command is retained there in `reproduce.sh`; raw bounded listings
are optional `--raw`, ignored and untracked. SHA-256 inputs: FL
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`, AP
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`.

```sh
sh verification/results/fl-port/fov-sites/reproduce.sh
python3 verification/probe/verify_fl_fov_sites.py --raw
```

Measured counts are recorded in the generated summary: AP seven regions,
59 instructions, 17 semantic anchors; FL 25 regions,
503 instructions, 107 semantic anchors and 54 independently validated boundary
edges;
all region/seam boundary edges match the independent reference decode.
The fixed-image census covers 396,567 AP and 428,814 FL linear instructions;
no surveyed direct edge or relocated pointer enters the two detours or
constructor-window interiors. Eight negative witnesses reject six semantic
mutations with digests bypassed, a fully decodable operand-byte seam and the
wrong edition. Two synthetic loaded-base cases validate the FL A1 and relative
CALL operands; they do not execute Windows loader relocation.

Next acceptance is a focused FL generated-stub fixture: nearest-degree
SetFocus vectors and exact-match load vectors including vanilla 70/90/100,
remapped values, out-of-range and arbitrary values; live GPR preservation,
documented scratch-register/flag clobbers, FP/SIMD and LastError preservation; original outgoing stack words and RET 8 behavior; reentrant
invocations with immutable tables; preferred and rebased windows; all-three-site
failure rollback, crossing-literal protection/cache failures, foreign-owner
refusal, late install refusal and retained ownership at unload. Reuse unchanged
shared-math tests. Then a user-run load/menu/zoom/save/reload check must verify
actual first-update/cache behavior against the static script findings below. No game, Wine, build, install,
production edit or commit was performed here; native Windows runtime remains
unverified.

## 6. Resolved FL KC menu, conversion and load routes — 2026-10-29

**The existing 81-entry N=50..130 policy covers the stock FL menu.** Its default
is 90, its tuning limits are 70 and 100, and its integer degree-to-binary-angle
conversion is the same as AP. The FL focus member moved to **class 150 member
`0x18`**, so an AP member-offset assumption would read the wrong setting.
No KC hook, asset rewrite or extra load-time SetFocus dispatch is justified.
This closes the static script gap in the native study, without claiming a game
run or removing the saved cockpit+0x240 limitation in section 3.

### Effective inputs and namespaces

[verify_kc_focus.py](../../../verification/results/fl-port/fov-sites/verify_kc_focus.py)
uses the existing `fl_resources.Resources` and qualified
`fonts.fl_admission.localized` resolver. The read-only clean snapshot is
`/mnt/c/X3FL-orig`, language 44, no selected mods. Its mounts are root
01..13 followed by addon2/01..05, stopping at root14 and addon2/06. Loose and
current-language candidates are considered before selecting the archive winner;
this is not a hard-coded read of a convenient CAT member. Provenance is saved
in `kc-summary.json` beside the producer.

| Resource request | Effective winner | SHA-256 after archive XOR / after packed decoding |
| --- | --- | --- |
| `addon2/L/x3story` with `obj` | slot 15, `addon2/03.cat:addon2/L/x3story.obj`, 3,110,481 bytes; build 1636047691 | Both `dbb732716e1acab6b8cc7f87f5d31324fb4764dfc22d1cec808749a4f45ec7b9` |
| `addon2/types/Globals` with `pck txt` | slot 16, `addon2/04.cat:addon2/types/Globals.pck`, 2,477 packed / 7,594 decoded bytes | `fd50eff2c2ff8fc44e747aa30a5e12bd3756f203f74e071723d5282c79bfed50` / `079dae92207c31cac318e4f0ba9c68a73a7b4b27d4464e580d7c7b86defd3a63` |

The native story caller uses descriptor+0x20, `addon2\L\%s`, at `0x4b8e55`
and the extension reader at `0x4b8eef`. The Globals caller pushes `Globals`
at `0x43e4b0` and calls the already-qualified addon2/types reader `0x487d40`
at `0x43e4de`. See the [resolver](resource-resolver.md#3-archive-members-retain-their-namespace)
and [font caller study](font-admission.md#active-caller-and-table-identity).
Root catalogs participate in those exact keys; a root `L/x3story.obj` is a
**different namespace**, not an implicit AP-script fallback when addon2 is
missing. No physical `addon` catalog series is added to FL mounts. The separate
clean AP comparison reads the previously qualified
`/mnt/c/X3AP-orig/addon/04.cat:L/x3story.obj`, SHA-256
`ed5786a0c603802d5735fb36e0332d7c732b981d2b64dcee4174890284faff7a`.
It is a comparison input, not an FL selection override. The live `/mnt/c/X3`
installation and the running AP process were not inspected or changed.

### Default, menu arms and exact conversion

FL class 150 has 231 methods and 71 member defaults. CLAS member record index 24
is **(index 24, value 90, type 1)**. `GetFocus` at CODE `0x18c70` reads that member
at `0x18c73`. `SetFocus` at `0x18c79` stores its argument there at `0x18c7f`,
then shifts by 16 (`0x18c86/0x18c88`), divides by 360
(`0x18c89/0x18c8c`) and calls `INS_SetFocus` at **`0x18c8e`**. It is the
only direct native call to that name in the resolved story. The exact setter
and getter instruction sequences match AP after symbolic relocation and the
explicit member change `0x16 → 0x18`; no other operands are masked.

The interpreter's own two-stage dispatch at `0x4bedf2/0x4bedf9` maps opcode 58
to `0x4c0079`, whose integer path performs **SHL at `0x4c008d`**. Opcode51
maps to `0x4bfeda → 0x4c61d0`; its tag1 integer path uses **CDQ/IDIV at
`0x4c620a/0x4c620b`**, storing the quotient at `0x4c620d`. Consequently the
positive stock values use `(N << 16) / 360`, truncated rather than rounded:
70→0x31c7, 90→0x4000, 100→0x471c. These named arithmetic paths establish the
conversion without executing the game VM or assuming opcode meanings from AP.

Class 2259 (`0x8d3`) retains FOV menu line `0x2406`:

| Path | FL CODE evidence | Effect |
| --- | --- | --- |
| Display | `SpecialUpdate 0x1b6dd6`, GetFocus call `0x1b6f8c` | Formats the current number; neither this method nor menu Create calls SetFocus. |
| Minimum shortcut | Input switch `0x1b7ffc → 0x1b7d5a`; setter call **`0x1b7d68`** | `SA_GetGlobalParameter(0x59,70)` then SetFocus. |
| Maximum shortcut | Input switch `0x1b84fb → 0x1b824d`; setter call **`0x1b825b`** | `SA_GetGlobalParameter(0x5a,100)` then SetFocus. |
| Increment/decrement | ChangeValue switch `0x1b8d9f → 0x1b8a1d`; getter `0x1b8a21`, setter **`0x1b8a94`** | Copies the setting into member 0x66, adds/subtracts one, and clamps against the same global limits. |

The native name/ID pairs at `0x576800/0x576808` bind `SG_MIN_FOV` to 0x59
and `SG_MAX_FOV` to 0x5a. The effective Globals payload supplies **70/100**.
The complete decoded fixed-story call inventory has exactly the three setter
call sites in the table and no by-name `SetFocus` call. Within class 150 only
the setter writes member 0x18. This is a finite file call/member-store inventory,
not a proof that arbitrary scripts cannot invoke a native or dynamic method.

### Concrete save/load routes and what is re-applied

The stock load request global **DoLoadGame CODE `0xd469`** calls
`150::BeforeLeave`, `150::SetLeave`, then `X2_SetNextLocation` at `0xd48f`.
The save request global **DoSaveGame `0xd497`** ends with `X2_Save` at
`0xd5e8`. Neither directly calls the FOV setter. `640::StartLoad` at `0x19e18a` and menu ExitMenu at `0x1b62b6`
reach DoLoadGame. `640::StartSave` at `0x19e24f` and ExitMenu at
`0x1b6337` reach DoSaveGame; the other two save callers are
`150::SetLeave` at `0x16d36` and `2016::RunPlayerTrade` at `0x11c918`.
There is no separate focus conversion in the two request globals.

The native load route remains outer call `0x477c52 → 0x479430`, story-state
restore `0x47983c → 0x4bcb10`, renderer restore `0x47984c → 0x493510`,
constructor `0x4798a5 → 0x41df90`, and cockpit load
`0x4798bf → 0x420d30 → 0x420db6 → 0x41d9a0`. The last operation restores
registry focus at `0x41df0b`, overwriting the constructor default. Native save
uses `0x420c62 → 0x41d9a0`, reading that same field at `0x41dbe7`.

Class 150's integer focus member also follows the ordinary OBJE singleton
save/restore path already established in the
[AP/FL comparison](ui-save-load-comparison.md#2-native-load-persistence-and-restart):
integer payload save at `0x4bc3d6 → 0x4bb810`, member restore at
`0x4bd622 → 0x4bc8c0`, tag1 payload/tag stores at `0x4c55a1/0x4c55a6`.
Restoring the member is a cell restore, not execution of SetFocus. There is
therefore no need to infer that CLAS default 90 replaces a saved menu number.

After successful native load, `0x477c76/0x477c8f` dispatch named Restart.
Class 150 Restart **`0x16af4`** calls global **_Restart `0xd1bb`** at
`0x16b04`. Neither that method, _Restart, Init nor InitClient directly
re-applies SetFocus. _Restart retains the native UI reload/steering sequence
and calls setup-script execution; it does not insert a focus setter in this
fixed object. Combined with the exact three menu call sites and the single
native site, the concrete stock paths provide **no automatic replay of the
saved degree member through INS_SetFocus on load**. The registry serializer
is the independently established authoritative native load write.

This conclusion is bounded to the resolved fixed object and named native
paths. Dynamic method dispatch, third-party KC objects and setup scripts
selected from other resources can add behavior. No whole-VM absence proof is
claimed. The existing native SetFocus interception remains the correct boundary
for any such future call; values outside its established remap interval keep
the existing pass-through policy. The optional cached cockpit+0x240 store
`0x41c736` and mode 6 cache read `0x422c92` are unchanged by these KC findings.

### Implementation decision and reproducibility

Retain the shared 81-entry nearest-degree SetFocus table for N=50..130 and the
separate exact-match save-load rule. Stock FL's inclusive 70..100 range lies
inside it; all 81 integer degrees recover their original N from the engine's
truncated binary angle. Constructor configuration remains a new-game seed;
a saved native focus takes precedence after load. Preserve the saved degree
member and menu behavior; changing it would alter the game's option rather
than port the native enhancement. FL production needs only the three native
sites, the register variants and early transaction already specified above.
No new per-frame work, KC patch, allocation or VM call is introduced.

```sh
python3 verification/results/fl-port/fov-sites/verify_kc_focus.py \
  --root /mnt/c/X3FL-orig --ap-root /mnt/c/X3AP-orig --language 44
python3 -m py_compile verification/results/fl-port/fov-sites/verify_kc_focus.py
```

Measured: **9,046 callable entries** (8,803 methods plus 243 SYMB globals),
**1,010,000 decoded file instructions**, four relevant calls (one native,
three menu), two matching AP/FL focus methods, **33 named native instruction
witnesses**, two arithmetic-handler maps, two tuning-ID maps and five string
witnesses. Five in-memory negatives reject a changed shift count, divisor,
member offset, menu maximum and newly introduced Init-to-SetFocus call with
image/resource hash gates bypassed. Successful producer time was about 2–3 s;
this is offline analysis time, not a game timing. Output records effective
winner receipts, commands' selectors and the bounded lifecycle call lists;
no raw script, decompiler output or game bytes are written. Runtime first-update,
mode 6/cache and menu/save/reload acceptance remain with the eventual fixture
and user-run checks.
