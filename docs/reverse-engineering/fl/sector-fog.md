# FL active-sector and background inputs for shared fog

Offline RE, 2026-10-29. **FL can supply the existing value-only sector sample
through its admitted ordinary camera publication; no additional native hook or
simulation-object observer is indicated.** The established contract covers a
finite copy during a healthy native pass. Implementing and qualifying that copy,
card-shader classification and shared fog GPU integration remain open. This is
not an implemented or accepted FL volumetric-fog port.

Scope: [AP runtime sampler](sector-fog.md#11-runtime-access-the-active-sectors-record-from-inside-the-process),
[AP transit ordering](../sector-transit-order.md),
[sector_background.h](../../../src/proxy/sector_background.h),
[capture.cpp](../../../src/proxy/capture.cpp) `sector_background_context`, and
[fog_sector_policy.h](../../../src/proxy/fog_sector_policy.h). Existing FL
resource-family/bake tooling needs no further inventory for this CPU contract.
No production change, commit, game/Wine/native execution, build or installation
was performed. All addresses below are preferred VAs; image base is `0x400000`.

## 1. Identity, globals and record layout

Clean-image provenance is FL SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`
and AP SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`,
read from `/mnt/c/X3FL-orig/X3FL.exe` and `/mnt/c/X3AP-orig/X3AP.exe`.
These hashes identify the inspected files; they are not a runtime capability
requirement. Reuse the recognized FL image and loaded base. Absolute instruction
operands have PE HIGHLOW relocation entries; adapter globals use
`loaded_base + (preferred_VA - 0x400000)`, never AP absolute addresses.

| Input | AP preferred VA / offset | FL preferred VA / offset | Native FL evidence |
| --- | --- | --- | --- |
| Cockpit registry slot | `0x608504` | **`0x5a82e4`**, RVA `0x1a82e4` | Active selector load `0x431fae`; lookup `0x41e420` |
| Simulation manager slot | `0x60850c` | **`0x5a82ec`**, RVA `0x1a82ec` | Sector resolver `0x443db0`; constructor `0x45c14d` |
| TBackgrounds table slot | `0x606fc0` | **`0x5a83d0`**, RVA `0x1a83d0` | Camera `0x422e4f`; loader `0x440637`, indexed element 2 of `0x5a83c8` |
| TBackgrounds count slot | `0x607040` | **`0x5a8338`**, RVA `0x1a8338` | Getter `0x47244d`; loader `0x4405b3`, indexed element 2 of `0x5a8330` |
| Configuration slot | `0x606f34` | **`0x5a83b0`**, RVA `0x1a83b0` | Fog consumer `0x4e4bc1`; startup publication `0x476c9e` |
| View-distance setting | config `+0x768` | **config `+0x758`**, signed DWORD | `0x4e4bcc`; using AP's offset is wrong |

The registry slot, manager slot, table slot and configuration slot contain
**pointers**. The count and view-distance setting are scalar values. Cockpit
registry `+0` points to a map, `+0x10` holds the active handle. Map `+0/+4` are
bucket-array pointer / capacity. Bucket index is `(capacity-1)&handle`; link
`+0/+4/+8` is next / key / cockpit pointer. The native resolver
`0x41e420..0x41e456` takes registry in ECX and handle on the stack, returns
cockpit or zero in EAX, and uses `RET 4`. It makes no call. Reuse the existing
32-link, cycle-detecting bounded policy instead of the native unbounded walk.
The simulation manager's map is **`+0x14`**, a separate map from both this
cockpit map and the observed B3D engine render map.

| Object field | FL meaning and evidence |
| --- | --- |
| Cockpit `+0x54` | Sector pointer; initialized zero at `0x4211fb`, explicit sector setter `0x421c77`, saved-sector fixup `0x41b788` |
| Cockpit `+0x58` | Sector-camera pointer; initialized zero `0x421202`, resolved assignment `0x430738/0x43073d`; distinct from HUD `+8` and galaxy `+0x5c` |
| Cockpit `+0xc` | Reference simulation-object pointer; existing [own-ship contract](own-ship.md#1-native-root-identity-and-initialization) |
| Simulation object `+8` | Scalar native object identifier; sector constructor publishes automatic-map result at `0x45c181` |
| Simulation object `+0x48/+0x4a` | Class/subtype signed words; initialized `0x44995f/0x4499f2`; sector resolver `0x443de9` requires class 1 |
| Simulation object `+0x54` | Parent pointer; initialized zero `0x4499f6`, placement publication `0x44ceb1`; distinct from B3D parent `+0x18` |
| Simulation object `+0x9c` | Marker `0xcafe` initialized `0x449a20`; destruction stamps `0xefac` at `0x44961e`, after unlink; a consistency marker, not read permission |
| Sector `+0x130/+0x134/+0x138` | Sector/galaxy/dust scene pointers; initialized zero in constructor `0x45c110`, sector-scene assignment `0x45c264` |
| Sector `+0x13c` | Signed background index; zero initialized `0x45c199`, command store `0x46eac5`, full background-change publication `0x45c395` |
| Sector `+0x140/+0x144` | Scalar body-selection indices: stars / background nebula; `0x45c320..0x45c385` uses them against row `+0x4c[]/+0x90[]` |
| Sector `+0x148/+0x14c` | Flags / size, constructor `0x45c1b1/0x45c1cd`; subtype 0 initializes flags 2 and size 10,000,000. Flags 2 is not a loading/readiness flag. |

**Diagnostic naming discrepancy:** the existing AP `Sample::neb` reads sector
`+0x140` and `Sample::stars` reads `+0x144`. Native semantics are the reverse:
`+0x140` selects star bodies and `+0x144` background nebula bodies. Preserve the
existing field/value policy in a bounded FL adapter; do not reinterpret those
members or propagate their log names as independently established semantics.
Neither selection value participates in `fog_sector_frame` family policy.

FL computes **`R = table + index*0xdb8 + 0x44`**, with no separate per-sector
runtime row copy. Its native consumers frequently use the unrebased row;
rebasing to `R` is equivalent arithmetic for the shared sampler.

| Input | Row / R offset | FL reader / writer evidence |
| --- | --- | --- |
| Body family | `+0x44 / +0` pointer | Loader `0x4409e9`; setter `0x47272c`; getter `0x4722f2`; freed `0x442c91` |
| Dust weights `[8]` | `+0x114 / +0xd0` signed DWORDs | Native CDF `0x4206f0..0x420753`; loader `0x440a59..0x440a74`; setter `0x47274b..0x472776` |
| NumDustInstances | `+0x134 / +0xf0` signed DWORD | Native population `0x420764`; getter `0x472464`; loader `0x440a85`; setter `0x472786` |
| FogNear | `+0x148 / +0x104` signed DWORD | Camera reader `0x422e5b`; getter `0x472531`; loader `0x440ac6`; setter `0x4727c0` |
| FogFar | `+0x14c / +0x108` signed DWORD | Camera reader `0x422e54`; getter `0x47256e`; loader `0x440ad1`; setter `0x4727c9` |
| Stardust percent | `+0x150 / +0x10c` signed DWORD | Native population `0x420759`; getter `0x4725ab`; loader `0x440b02`; setter `0x4727d2` |

Getter branches reject negative index and `index >= *0x5a8338` before indexing,
independently establishing count and stride. Loader `0x440ad7..0x440ae9` zeros
both fog values when far < near. Runtime setter does not repeat that clamp:
the shared dust/fog bounds remain necessary. No physical-density scalar is
established; dust counts/weights keep their AP population semantics.

## 2. Camera consistency and effective far

Cockpit updater `0x421f50` receives cockpit in ECX, saved in EBX at `0x421f5e`.
It null-checks cockpit sector at `0x4227ad`. At `0x422e3f..0x422e83`, it reads
that sector's index, loads both row fog scalars and copies them directly to
sector camera **`+0x36c/+0x370`** (`0x422e66/0x422e6c`). The pair is raw native
integer distance, matching AP's sample units; conversion/render scale remains
an existing camera/renderer responsibility.

FL **always ORs camera `+0x270` with `0x10000` at `0x422e74`**, including a zero
near distance, and writes camera `+0x368` to `0xffffff` iff near is nonzero.
This differs from AP's conditional activation branch at `0x42157a`. That bit
cannot establish loading completion, a camera role, or an initialized row.
Use it only in the shared raw-pair consistency check, and refuse a mismatch.

Fog consumer `0x4e4bc1..0x4e4bf9` reads `*0x5a83b0 + 0x758`: setting >=3 gives
`max(raw_far,500000000)`; setting 2 gives `max(raw_far,100000000)`; lower values
leave raw far. This is a register calculation, with no write back to raw camera
fog. Log/copy the effective far separately. AP uses `+0x768` at `0x4c2c22`.
FL's outer loop can temporarily set `+0x758=4` at `0x4782b3`, then restore it at
`0x4782e8` after render. Copy the setting at the admitted sampling seam; an old
cached preference could miss this native override.

## 3. Initialization, publication and actual payload lifetime

Cockpit initialization/publication/restoration/removal reuses the
[HUD owner](ui-camera-lifetime.md) and
[own-ship](own-ship.md#3-why-the-existing-material-seam-can-own-this-copy)
proofs. New cockpit initialization completes before insertion
`0x4305be/0x4305cd`; restored insertion is after its reader at `0x420e33`.
Removal precedes owner destruction `0x430605/0x43060d`. Active selector can be
zero or retain an absent key; neither permits a cached cockpit dereference.

Sector assignment is concrete: native command `0x43088e` resolves cockpit with
`0x41e420`, sector with class-checking `0x443db0`, null-checks both, then calls
**`0x4308bc → 0x421c70`**. The latter takes cockpit ECX and a sector stack
argument, stores sector at `+0x54`, then copies background colour into the
cockpit scene if sector class is 1. It has no CALL and returns `RET 4`.
The saved-sector read branch calls the same class-checking resolver at
`0x41b77d` and publishes the result directly at `0x41b788`; its zero path is
explicit. Whole-load exclusion must cover that fixup, after the B3D subsection.

**Early publication is not initialized payload.** Sector constructor `0x45c110`
links into manager's parentless list and inserts its handle at `0x45c17c`, then
writes the identifier and initializes sector `+0x130..+0x178`. The default
background index is 0. Neither map membership nor a plausible class/index proves
the constructor or later sector setup has completed.

Type-table loader `0x43e710` clears indexed table/count at
`0x44043b/0x440446`, publishes decoded count at `0x4405b3`, allocates
`count*0xdb8`, zeroes storage at `0x440629`, and publishes the pointer at
**`0x440637` before loading rows**. Family `+0x44` is written at `0x4409e9`,
then weights/dust/fog and other row fields. A non-null table/count or a zeroed,
in-range row is therefore not a row-loaded barrier.

Runtime `SA_SetBgTypeData` inside dispatcher **`0x46a640`**, block `0x472693`, can
reallocate storage (`0x4726be → 0x4d8ef0`), increment count **before** publishing
the returned table (`0x4726c3/0x4726c9`), replace the family string through
`0x50b670`, and rewrite fog/dust. Full type cleanup `0x442b80` frees each family
at `0x442c91`, frees table storage at `0x442d11`, then clears its slot at
`0x442d25`; count may remain nonzero during cleanup. No retained table, record
or string pointer is valid across these operations.

The producing verifier measures this reverse ownership cone:

| Native owner | Direct edges and lifetime implication |
| --- | --- |
| Type loader `0x43e710` | Exactly two: `0x4778ae` inside startup `0x476bc0`; `0x448bf9` inside simulation restore `0x448bb0` |
| Simulation restore `0x448bb0` | Sole caller `0x47987e` inside the existing whole-load entry `0x479430`; cleanup `0x448bf2` precedes reload `0x448bf9` |
| Type cleanup `0x442b80` | Exactly two: restore `0x448bf2`, simulation-manager destructor `0x476af9` |
| Simulation-manager destructor `0x476ac0` | Sole caller `0x4763ce` inside shutdown/error helper `0x4762d0` |
| Startup `0x476bc0` | Sole caller `0x476b68`; startup is outside the healthy complete-render borrow |

No absolute DWORD function-pointer literal occurs for these five entry points.
This is a measured direct/literal census, with Ghidra owner confirmation; it
does not pretend to exclude arbitrary computed private calls from foreign code.
Runtime row/name edits are synchronous native commands. Registration is now
explicitly tied to the admitted interpreter transport: `0x43dcb5` loads VM
`*0x5aa174`, `0x43dcc5` reads its current type index at `VM+0x2c`, and
`0x43dcc8/0x43dcdd` form `(type_index+2)*3`. The publication at **`0x43dce0`**
stores **`0x46a640`** into `VM+(type_index+2)*24`, equivalently
`VM+0x30+type_index*24`. **`0x46a610` is the adjacent type/index validator**
(`ECX<=0x1f`, nonnegative EDX, `EDX<counts[ECX]`), not the dispatcher entry.
Interpreter `0x4c075c..0x4c0780` computes the same type-table cell from its
decoded type index, loads the registered target, pushes five argument words,
and **calls it synchronously at `0x4c0783`**; `0x4c0785` cleans 0x14 bytes.
Thus the concrete simulation registration joins the existing synchronous
interpreter and scheduling closure, rather than relying on a neighboring
function's identity. The existing
[command scheduling closure](material-submit.md#3-actual-borrow-readiness-writers-and-reentrancy)
and [typed callback/recovery closure](ui-render-mutation.md)
exclude executing those commands, whole-load, startup or shutdown while a
healthy admitted render copy runs. Recovery can reclaim meshes/textures; this
sample follows none of those resources. The discovered family string belongs
to the type table, not the reclaimed mesh/texture payloads.

Thus the existing scope covers the **finite row and name copy**, including the
name-pointer dereference. It does not require a permanent cold-publication
assumption, and it does not pin any table allocation after that copy. A copied
row's first DWORD is still a borrowed pointer until the name bytes are copied.
The scope protects its allocation lifetime, **not a readable 32-byte extent**.

The family helper **`0x50b670`** takes old allocation in ECX, source in EDX,
and returns the replacement in EAX. Loader **`0x4409e2`** calls it with ECX=0
and EDX=the parsed name, then stores EAX at row+0x44. Runtime setter
**`0x472727`** supplies the old `[R]` in ECX and source in EDX, then replaces
`[R]` at `0x47272c`. The helper first frees a non-null old allocation at
`0x50b68f`. At `0x50b6a7..0x50b6b7` it scans the source through its terminating
NUL: the byte-load/advance/test/backedge is `0x50b6b0..0x50b6b5`, leaving the
length in ECX after subtraction. **`0x50b6bc/0x50b6bf` set EBX and allocation
ECX to `strlen+1`**, and `0x50b6c1` calls allocator **`0x4d8fc0`** with that
size. On success, `0x50b6cc..0x50b6cf` passes count EBX, source EDI and
destination ESI to copy helper `0x52d780`, then returns the allocation.
Null source/allocation failure returns zero. There is no padding-to-32
contract. Even a healthy short name can end before a full 32-byte transfer.
Only scalar values, bounded name bytes and non-dereferenceable identity labels
may escape. Existing B3D observer serials validate the borrowed camera's identity;
they do not own cockpit, sector, simulation parent or type-table storage.

## 4. Minimal edition adapter and finite read boundary

Extend the existing ordinary camera publication **`[0x4defa4,0x4defaa)`**, already
owned by `camera_state_fl`, with an optional fixed-size fog sample. This is the
six-byte whole `PUSH [EBX+0x278]`, after view publication and before native
SetViewport at `0x4defad`. EBX=camera, ESI=context, EDI=device, EAX=view pointer;
XMM1 holds final translation. Reuse the existing register/flags, x87/XMM/MXCSR,
LastError and SEH envelope and its replayed instruction. Do not introduce a
second patch at this seam. Full ABI and continuation evidence lives in
[camera buffers](camera-buffers.md#3-snapshot-seams-abi-and-callback-obligations).

The inherited owner/load/close/callback admission supplies dereference permission.
Native update order is explicit **`0x4782ca → 0x41e550`**, which updates each
cockpit through `0x41e5b0 → 0x421f50`, then **`0x4782cf → 0x48b6d0`** renders.
An eligible ordinary activation therefore sees completed sector/camera updates.
Require active cockpit `+0x58 == supplied camera` for the fog authority and bind
the owned result to its actual camera activation/load/pass/invalidation ticket.
An arbitrary first ordinary/auxiliary/cube camera is not main-view authority.
Choose the first matching admitted ordinary activation per source frame; a
nonmatching activation must not consume that frame's eligible-sample budget.

The copy should retain the shared structural/status/family policy: bounded map
walk, aligned/non-null fields, class 1, positive bounded count, in-range index,
checked 64-bit address arithmetic, one row value copy and bounded name, dust/fog
bounds, raw-camera equality, and optional docked parent walk. Read globals anew
on every eligible frame. Configuration lookup stays optional as in AP.

**Required FL name copy:** read through `volatile const unsigned char*`, one
scalar byte at a time, stop immediately after copying the first NUL, and cap
native loads at 32. Check address arithmetic before each load. Do not use
`memcpy(...,32)`, vector/dword lookahead or AP's unconditional 32-byte read.
After the finite native copy, preserve existing AP name acceptance: a NUL must
occur within the cap, all preceding bytes must be printable `0x20..0x7e`, and
quotes/backslashes are escaped into owned storage. A name of 31 characters
plus NUL is accepted; 32 characters without a NUL in the cap is refused.
Validation/escaping must not dereference the native string again. The verifier
models exact-sized allocations to require NUL-stop behavior; the eventual
compiled adapter still needs inspection proving scalar volatile native loads.

For docking, follow **simulation `+0x54`**, starting at the reference object's
parent. Examine at most four objects / three additional hops; the first class-1
object must equal cockpit's sector. Any other sector, bound, read failure or
malformed pointer refuses the anchor. Do not substitute the B3D `+0x18` graph,
reference object's immediate parent, or a cached sector for a failed cockpit
sector. This uses the same admitted finite simulation-graph borrow as the
existing own-ship copy, without adding observers for each structure.

Implement the complete native read interval with inline scalar/byte loads and
no engine helper, resolver call, allocation, logging, page query, D3D/COM call
or observer lock. In particular, mechanically calling AP's page-validating
`engine_memory::read` loop at this seam is not the existing FL call-free copy
contract. Validate the owned observer binding and invalidation ticket around
the interval; perform string escaping/policy after copying. Reject unsupported
nesting, loss of admission, foreign invalidation, load/close quarantine and
abnormal unwind. Nothing saved before an unwind can authorize later reads.
The existing wrapper's SEH cleanup must retire the optional sample along with
its camera record; fixture fault qualification remains implementation work.

**Cost (inferred bound, not measured FPS):** once per eligible source frame,
at most 32 hash links, four parent objects, one `0x120`-byte row and **up to
32 scalar native name-byte loads, ending at NUL**. This replaces the earlier
single 32-byte name-span cost assumption. Inline copies need no heap allocation,
engine lock or per-draw work. Keep the optional
record outside the camera assembly scope if its current `sizeof(Scope)<=512`
constraint would be exceeded; this is a storage choice, not a new framework.

`sector_id` is the native map handle copied at sector `+8`, an identity label
within the current load/pass. Automatic-map allocation can wrap and saved
handles are restored; do not turn it into a permanent incarnation token.
Heap labels may detect shared fog/TAA key changes, but placement remains keyed
by background index/profile/recipe under existing common policy.

## 5. Proven limits and remaining gates

No separate scalar loading flag is established or needed here. Zero active
handle, absent cockpit/sector, invalid table/index/name and mismatches have
named refusals; only the admitted owner/load protocol proves the read interval.
Do not infer readiness from camera `0x10000`, sector flags 2, alive marker,
index 0, committed pages, or observer membership.

The ordinary frame provider is implementable from the evidence above. AP's R3
prefill is a **separate** scope: this pass does not establish FL compiled-script
gate/jump/new-game ordering, the first destination sector's initialized scene
before resource loads, its bounded parentless-tail occupancy or prefill lead
time. The manager/list offsets alone do not port `fog_prefill_poll`; retain
its FL refusal until the specific transition and interception borrow are proved.

Required implementation acceptance includes rebased profile operands, actual
call-free compiled copy, bounds/cycle/overflow failures, sector-camera mismatch,
docked nested parents, same-address reuse/load invalidation, duplicate and
nonmatching ordinary activations, nested render, Reset, failure/unwind retirement
and AP policy parity. Native Windows compatibility is inferred from existing
engine/Windows boundaries, not measured by this offline study. FL live
sector/view/load correspondence and fog visuals still need user-run evidence.
Card-shader classification, march inputs and GPU state/Reset integration remain
future gates; the resource-family tooling and this sample map do not close them.

## Reproduce and measured evidence

```sh
sh verification/results/fl-port/sector-fog/reproduce.sh
python3 -m py_compile verification/results/fl-port/sector-fog/verify.py
```

[Verifier](../../../verification/results/fl-port/sector-fog/verify.py) and
[compact record](../../../verification/results/fl-port/sector-fog/record.json):
40 complete-instruction regions / **922 instructions**, **187 FL operand
witnesses**, **18 AP correspondence witnesses**, **26 relocated witness
operands**, three synthetic loaded bases (`0x10000000`, `0x60000000`,
`0x70000000`), **35 changed-operand/instruction rejections**, and AP-as-FL
rejection. Seven guarded-allocation name-copy model cases cover empty/short,
escaped, 31-character, capped non-NUL and nonprintable names; an unconditional
32-byte transfer is rejected on a two-byte allocation. These are offline
contract checks, not compiled adapter qualification. The script checks operand
contracts and PE shape, not an expected
full-image hash; hashes are output provenance. It is bounded task evidence,
not a replacement production executable recognizer or a runtime fixture.

Ghidra 12.1.4 used the existing analyzed local
`verification/results/fl-port/registry-writers/ghidra/FLRegistry` project with
`-readOnly -noanalysis`, project `X3CameraState.java`, targeted `dec`, `data`
and `range` queries at the named functions/globals. Raw outputs remain local
under `/tmp/fl-sector-fog/*.local.txt`, outside tracked evidence. To reproduce
owner references, use `data:0043e710 data:00442b80 data:00448bb0 data:00476ac0
 data:00476bc0`; field studies used `dec:00420660 dec:00421c70 dec:00421f50
 dec:0045c110 dec:0045c260 dec:0045c2a0 dec:00443db0` and globals
`data:005a83d0 data:005a8338 data:005a83c8 data:005a8330`.

Review correction (2026-10-29): targeted Ghidra instruction queries at
`0x43dcb5`, `0x46a610`, `0x4c075c`, `0x50b670`, `0x4409de` and `0x47271f`
are retained only in `/tmp/fl-sector-fog/review-correction.local.txt`.
The verifier checks the registration cell arithmetic and registered dispatcher,
interpreter transport, exact string allocation/copy count, and both string
callers under the same three synthetic rebases. Focused negatives include
registering validator `0x46a610` instead of dispatcher `0x46a640`, changing
VM/type-cell arithmetic, allocator/copy targets or size, and changing callers.

## 6. Verification pass for the step 6 reader (2026-11-01)

Targeted re-derivation for `src/engine/fl/sector_background_fl.cpp` (Farnham's Legacy port step 6),
from the installed `/mnt/c/X3FL/X3FL.exe` (same SHA-256 `e7f3aad8…01dfbb8` as section 1) side by side
with `/mnt/c/X3AP-orig/X3AP.exe`. Method: capstone linear sweep and whole-instruction decoding of
named spans, HIGHLOW relocation census, direct-caller census; no game, Wine or build execution. Every
value below is an **RVA** (preferred VA − `0x400000`) or a field offset; the generated fragment adds
`fl::module_base()`. Sections 1–5 keep their preferred-VA notation; RVA = VA − `0x400000`.

**Generator.** [`tools/analysis/fl_sector_background_constants.py`](../../../tools/analysis/fl_sector_background_constants.py)
writes [`sector_background_fl_inc.h`](../../../src/engine/fl/sector_background_fl_inc.h): 8 slots,
53 named values and **22 anchors** (1,112 bytes, 27 relocated operand words). Each anchor must decode
as whole instructions; every base relocation in its span must be an absolute operand naming a listed
`.data` slot; each value is read from a named instruction operand (disp, immediate or branch target)
and must equal the documented value; row offsets read from the getters must equal the rebased
offsets read from the setter plus `0x44`. The runtime check mirrors the camera provider's: compare
the span at `module_base() + rva` with each operand word equal to `module_base() + operand_rva`.
[`test_fl_sector_background_constants.py`](../../../verification/analysis/test_fl_sector_background_constants.py)
(6 tests) accepts every span relocated to `0x400000`, `0x4d0000` and `0x10000000`, refuses every
single-bit flip, a foreign base and an unreadable span, refuses nine mutated operands (wrong slot,
`+0x368`, stride `0xdb4`, rebase `0x40`, row `+0x138`, AP's `+0x768`, class 2, split instruction) and
the AP image. Census: [`census.py`](../../../verification/results/fl-port/sector-background/census.py),
[`tbackgrounds_census.py`](../../../verification/results/fl-port/sector-background/tbackgrounds_census.py).

### 6.1 AP input → FL (question 1)

All rows **verified**: the FL operand is read by the generator, and the AP instruction at the cited
address performs the same use (function match by identical instruction shape and operand set).

| Reader input (AP) | FL RVA / offset | FL anchor (RVA) | AP match |
| --- | --- | --- | --- |
| Cockpit registry slot `0x608504` | **`0x1a82e4`** | `registry_lookup` `0x0cf46f`; `active_control_set` `0x031fab`; `ref_object_set` `0x0308d6` | inline lookup `0x40f85b`; `INS_SetActiveControlCockpit` `0x42ebed` |
| Active handle `registry+0x10`; map `registry+0` | `+0x10`, `+0` | `0x0cf474`, `0x0cf477`; store `0x031fd0` | `0x40f860`, `0x40f863`; store `0x42ec03` |
| Map `{buckets +0, capacity +4}`, index `(capacity−1)&handle`, link `{next +0, key +4, cockpit +8}` | same | `0x0cf479`…`0x0cf4a3`; resolver `0x01e420` (thiscall, `RET 4`) | `0x40f865`…`0x40f88f`; resolver `0x41cd20` (EAX/EDX) |
| Cockpit `+0xc` ref object | `+0xc` | store `0x030911` | own-ship contract |
| Cockpit `+0x54` sector | `+0x54` | store `0x021c77`; load `0x022e3f` | store `0x42d670`; load `0x421533` |
| Cockpit `+0x58` sector camera | `+0x58` | `0x022e42` | `0x42155a` |
| Class word `+0x48` == 1 | `+0x48`, 1 | `0x021c86`, `0x043de9`, `0x025057` | `0x420369`, `0x43a596`, `0x423839` |
| Parent `+0x54` | `+0x54` | `parent_walk` `0x025031`/`0x02504c` (class 5/6 → parent → class 1) | `0x423813`/`0x42382e` |
| Sector id `+8` | `+8` | `sector_constructor` store `0x05c181` (map insert `0x10cfe0`) | `0x43f972` (insert `0x4efcc0`) |
| Background index `sector+0x13c` | `+0x13c` | `0x022e45`, `0x021c8d`, `0x05c199` | `0x421536`, `0x420370` |
| `neb`/`stars` = `sector+0x140/+0x144` | same | `sector_bodies` `0x05c320`/`0x05c351` | `0x452630`/`0x452662` |
| TBackgrounds table `0x606fc0` | **`0x1a83d0`** = type array `0x1a83c8` + 4·2 | `fog_copy`, getters, `setter_rebase`, `loader_table` `0x040637` | `0x42153c`, `0x4363ea` |
| Record count `0x607040` | **`0x1a8338`** = count array `0x1a8330` + 4·2 | getters (`cmp`), `loader_count` `0x0405b3` | getters `0x467765`…, `0x4363c8` |
| Stride `0xdb8`; `R = row + 0x44` | same | `0x022e45`; `setter_rebase` `0x072703`/`0x07270f` | `0x421542`; `0x467b47` |
| Name `R+0` (row `+0x44`) | same | getter `0x0722f2`; setter `0x07272c` | `0x46778d`; `0x467b5f` |
| Rates `R+0xd0[8]` (row `+0x114`) | same | getter `0x072427` (`cmp 8`, dword stride `0x36e`); setter `0x07274b` | `0x467892`; `0x467b76` |
| Dust `R+0xf0` (row `+0x134`) | same | `0x072464`; `0x072786` | `0x4678c5`; `0x467ba6` |
| Fog near/far `R+0x104/+0x108` (row `+0x148/+0x14c`) | same | `0x072531`/`0x07256e`; `0x0727c0`/`0x0727c9`; copy `0x022e5b`/`0x022e54` | `0x467980`/`0x4679b9`; `0x421548`/`0x42154f` |
| Stardust `R+0x10c` (row `+0x150`) | same | `0x0725ab`; `0x0727d2` | `0x4679f2`; `0x467bf2` |
| Camera `+0x36c/+0x370`, `+0x270` OR `0x10000` | same | `0x022e66`/`0x022e6c`/`0x022e74` | `0x42156e`/`0x421574`/`0x42157c` |
| Config slot `0x606f34` | **`0x1a83b0`** | `far_floor` `0x0e4bc1` | `0x4c2c1a` |
| View distance `config+0x768` | **`config+0x758`** | `0x0e4bcc`; floors 500,000,000 / 100,000,000 at `0x0e4bda`/`0x0e4beb` | `0x4c2c22`, `0x4c2c3e`/`0x4c2c57` |

The cockpit registry is **not** the object-trace registry: `object_trace_fl_inc.h`'s slot `0x1aa100`
with `*(*slot+0xc)` is the B3D render-object map. The sector reader walks the cockpit map at
`0x1a82e4`; the simulation-object map is object manager `0x1a82ec` `+0x14` (`sector_by_id`
`0x043db0`, AP `0x43a560`), which the ordinary reader does not traverse.

### 6.2 Record layout and table shape (question 2)

The layout holds unchanged. FL has the same 14 count-bounded `SA_GetBgType*` getters in the same order
and offsets as AP (`+0x44`, `+0x48`, `+0x8c`, `+0xd0`, `+0x114[i<8]`, `+0x134`, `+0x138` packed RGB,
`+0x144`, `+0x148`, `+0x14c`, `+0x150`, `+0x154` packed RGB, `+0x90[i]`, `+0x4c[i]`; FL `0x0722cc`…`0x072664`,
AP `0x467765`…`0x467a98`), the same `0xdb8` stride and the same indexed loader publication
(type 2; clear/count/table/cleanup writes `0x04043b`, `0x040446`, `0x0405b3`, `0x040637`, `0x042d25`;
AP `0x436277`/`0x43627e`/`0x4363c8`/`0x4363ea`/`0x4396e4`). The `+0x44` rebase is the compiler's
addressing choice, not a layout: FL's fog copy and background change index the unrebased row
(`[row+0x148]`), FL's setter rebases (`0x07270f`), AP rebases in both. The reader's single `0x120`-byte
copy from `R` ends at row `+0x164`, inside the row. **Count differs by data only (measured):** FL
resolves types under `addon2\types\` and `addon2/04.cat` `TBackgrounds` has **110** records (version 25,
38 columns; AP `addon/03.cat` 83), dust max 50, 23 rows with dust, far max 500,000,000, no row with
far < near, family names ≤ 21 characters (100 families). All inside the shared bounds (count ≤ 4096,
dust ≤ 64, far ≤ 2e9, name < 32).

### 6.3 Pointer chain (question 3)

Same hops and offsets as AP §11.2: `[0x1a82e4]` → registry; `+0x10` handle, `+0` map → bucket walk →
cockpit; `cockpit+0x54` sector (class word `+0x48 == 1`), `+0x13c` index, `[0x1a83d0] + index·0xdb8 + 0x44`
= `R`; `cockpit+0x58` camera; `cockpit+0xc` → ref object `+0x54` parent for the anchor check; the
docked parent walk follows simulation `+0x54` with class word `+0x48` (FL `0x025031`, native shape: one
extra hop through class 5/6, then class 1, identical to AP `0x423813`). The only edition differences
the reader sees are the four slot RVAs and `config+0x758`.

### 6.4 Mutability and the FL read window (question 4)

Writers, by direct-operand census (linear sweep; aliasing through computed pointers not excluded):

| State | FL writers (RVA) | When |
| --- | --- | --- |
| Table / count slots | loader `0x03e710` (clear `0x04043b`/`0x040446`, count `0x0405b3`, table `0x040637` before rows are filled); cleanup `0x042b80` (frees each row name through `0x042c6e`…`0x042c91`, the table at `0x042d11`, clears the slot at `0x042d25`); `SA_SetBgTypeData` block `0x072693` (realloc `0x0d8ef0`, count `inc` `0x0726c3` **before** table `0x0726c9`) | loader: startup `0x0778ae`, simulation restore `0x048bf9` (sole caller `0x07987e` in whole-load); cleanup: restore `0x048bf2`, manager destructor `0x076af9`; setter: synchronous script command |
| Row fields and name | loader (`0x0409e9` name …`0x040b02`); setter (`0x07272c`…`0x0727d2`); name helper `0x10b670` frees then allocates **`strlen+1`** bytes | same as above |
| `sector+0x13c` | constructor `0x05c199` (0); background change `0x05c2a0` writes the new index **last** at `0x05c395` (sole caller `0x06eaf4`); raw script store `0x06eac5` (no bound check; neither path bounds the index) | script commands; universe/save load |
| `cockpit+0x54` | init `0x0211fb`; `0x021c77` (sole caller `0x0308bc`, `INS_CockpitSetSectorSpace`); save restore `0x01b788` | script command; whole-load |
| Registry slot | `0x075ed3`, `0x0763af`, `0x0785f9` (zero), `0x077e37`, `0x0798b2` (publish) | create/teardown; reread every sample |

Frame order is verified: `0x0782ca` → cockpit update loop `0x01e550` (sole caller) → per cockpit
`0x01e5b0` → updater `0x021f50` (sole caller), whose fog copy `0x022e3f` refreshes the sector camera,
then `0x0782cf` → complete render pass `0x08b6d0` (sole caller). The FL safe window therefore restates
§11.4 with the FL addresses: **sample at most once per frame inside the complete render pass, at or
after the ordinary main camera activation** (section 4's seam), where the row, the sector index and
the sector camera `+0x36c/+0x370` are consistent with this frame; re-read all four slots every
sample; copy the row in one span. A table rebuild happens only in the loader/cleanup (startup and
whole-load) or a synchronous `SA_SetBgTypeData`; that the script interpreter cannot run inside the
render pass is section 3's closure (reused, not re-derived here).

### 6.5 Corrections and settled points (question 5)

- **The `0x10000` fog flag is unconditional on AP too.** AP's `JL 0x4215a0` at `0x42157a` takes its
  flags from `AND EAX,0xffffff` at `0x421563` (the following MOVs do not touch flags): SF = OF = 0, so
  the branch to the clearing `AND [ECX+0x270],0xfffeffff` is never taken and has no other predecessor
  (census). Section 2's "differs from AP's conditional activation branch" is wrong in effect; both
  editions set the bit on every fog copy, and `+0x368 = 0xffffff` iff near ≠ 0 on both.
- **The view-distance override is not FL-specific.** FL `0x0782b3`/`0x0782e8` (`+0x758 = 4` around
  the cockpit update and render, gated by `[0x1a8fe4]`) matches AP `0x403f20`/`0x403f54` (`+0x768`).
- **The `neb`/`stars` naming inversion is an AP reader naming issue.** AP `0x4525b0` also pairs
  `sector+0x140` with row `+0x4c[]` (stars, via `R+8`) and `+0x144` with row `+0x90[]` (background
  nebula, via `R+0x4c`). The values are right; only the log names are swapped, on both editions.
- **The index is unbounded natively.** The raw store `0x06eac5` and the change function `0x05c2a0`
  both accept any index; the reader's `0 ≤ index < count` check is mandatory, not defensive.
- Re-verified as stated in sections 1–3: every slot, the resolver contracts `0x01e420`/`0x043db0`,
  `0x021c70`, `0x01b788`, `0x0211fb`/`0x021202`, `0x0227ad`, `0x022e3f`…`0x022e83`, `0x0e4bc1`…`0x0e4bf9`,
  `0x05c181`/`0x05c199`, the loader/cleanup/setter writes, the loader clamp `0x040ad7`…`0x040ae9`, the
  `strlen+1` name allocation (`0x10b6bc`) and all caller counts in the section 3 table.
- AP §11.1's "16 `SA_GetBgType*` handlers" are 14 count-bounded getters plus `SA_BgTypeFromBody` and
  the setter on both editions.

### 6.6 For the implementation, and what remains open

- `sector_background::sample()` hard-codes AP absolutes (`0x608504`, `0x607040`, `0x606fc0`,
  `0x606f34`, `+0x768`). Step 6 must pass the four FL slots (module base + RVA) and `+0x758` through an
  edition parameter; every other offset in the shared header is identical on FL.
- `read_record()` reads 32 name bytes unconditionally; FL names are `strlen+1` allocations (≤ 22
  bytes in the shipped data), so the FL `Read` must fail safely across an uncommitted page or the
  name copy must stop at NUL (section 4). This is an implementation choice, not a layout change.
- Not established here: the seam's own ABI (section 4 owns it), the interpreter closure (reused),
  aliasing writes through computed pointers, and the FL R3 prefill (section 5).
- Reproduce: `python3 tools/analysis/fl_sector_background_constants.py --check`;
  `PYTHONPATH=verification/probe python3 -m unittest discover -s verification/analysis -p 'test_fl_sector_background_constants.py'`;
  `python3 verification/results/fl-port/sector-background/census.py`;
  `python3 verification/results/fl-port/sector-background/tbackgrounds_census.py`.
