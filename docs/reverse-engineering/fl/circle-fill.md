# Farnham's Legacy filled-circle arithmetic and port boundary

2026-10-28, offline inspection of `/mnt/c/X3FL-orig/X3FL.exe`, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Owning AP study: [sector-map-scanner-range.md](../sector-map-scanner-range.md).
This is derived static evidence and an implementable adapter contract, including the
independent deep ABI/lifetime review below; **it is not an implemented FL fix or
runtime qualification**. No game, Wine or native fixture ran.

Reproduce with `python3 verification/probe/verify_fl_circle_fill.py` (Capstone and the
project PE reader). Derived JSON and its reproducer are local under
`verification/results/fl-port/circle-fill/`; raw objdump listings stayed in
`/tmp/fl-circle/`. The verifier pins thirteen inspected regions, checks the entry,
direct edges, interior encodings, pointer literals, relocation overlap and class map,
and models the ordered span calls. Region hashes are qualification evidence for this
binary, not a substitute for structural executable identity or ASLR-aware profiles.

## Arithmetic correspondence

FL rasterizer `0x50c940` still uses 32-bit midpoint products and signed decision tests.
`A = s16(r)^2` at `0x50c958..63`. `B` conversion is at `0x50ca24..58`:

1. Signed 32-bit aspect squared into EDX:EAX, add `0x8000`, shift right 16;
   only the low signed 32-bit quotient `q` survives.
2. Convert `q` to **float32** (`cvtdq2ps`, `0x50ca46`), multiply by float32
   `2^-16` from `0x591994` (`mulss`, `0x50ca49`), widen to double.
3. Convert A to double, divide by that denominator, truncate to signed int32
   (`cvttsd2si`, `0x50ca58`, again at `0x50caa6`). Invalid/out-of-range conversion
   produces `0x80000000` with masked exceptions. MXCSR rounding affects conversion
   to float32; the offline model uses nearest-even and does not qualify other modes.

Thus AP's exact integer `semi_axis` is **not generally interchangeable**. Measured
model witness: r=814, aspect=1051682, q=16876755 gives FL B=2572 versus exact integer
B=2573. At aspect=65536, B=A exactly for every signed-positive 16-bit radius.

The recurrence corresponds algebraically to the AP portable `fill`:

- `0x50ca87..aab`: d=2A(1−B)+2B(r−1)r+B; initial region test rB>0 at `0x50cab1..b6`.
- `0x50caf0`: if d≥0 emit, decrement y and subtract 4By (`0x50cbb5..c1`).
  Then add 2A(2x+3), increment x (`0x50cbd2..ee`). Continue while yB>xA
  (`0x50cbf6..cc06`). All products and the comparison are signed 32-bit.
- `0x50cc16..4a`: d=2B((y−2)y+1)+2Ax(x+1)+A(1−2B).
  Emit, if d≤0 increment x and add 4Ax (`0x50cd2f..4c`); decrement y,
  add 2B(3−2y) (`0x50cd59..72`); stop at signed-low-word y=−1.

Measured derived model: all 825 radii 1..825 have identical ordered span calls
between wrapping and wide arithmetic at aspect65536; radius826 differs.
Maximum exact |d| at r813 is 2109152079, at r814 2151449212. This reproduces AP's
conservative 813 boundary and explains why transient modular overflow precedes a
changed branch. At aspects32768/65536/98304/131072, the first changed span radii
are respectively **519/826/938/1030** (searched each integer radius from1).
The AP radius-only gate therefore does not cover all FL ellipse inputs.

Clip determination is signed16 centre ± signed16 radius versus signed16 surface
width/height (`0x50c975..a9`), independent of B. Both regions clamp low16 signed x
endpoints, skip empty spans and compare low16 row unsigned against height
(`0x50cb12..76`, `0x50cca3..ccfc`). Row order is cy+y then cy−y, including the
centre twice. AP's `fill` additionally clips when B>A; that is an intentional
safety change, not exact stock behavior for aspect<65536. The measured wide
r1640, centre(80,120), surface832×700 case emits701 calls, all inside bounds.
Keep this distinction in tests; do not claim exact arbitrary-aspect compatibility.

## Call path, site and candidate stub

Measured direct rasterizer caller: **only `0x4a7b19`**, cleanup `add esp,0x10`
at `0x4a7b1e`. Upstream `0x4a79a0` direct callers: playback `0x490f90`, composite
recursion `0x4a7a48`, KC native case `0x4b2525` (opcode123, B3D_TexCircleFill).
These are a whole-text decoded direct-edge census, not proof against every
computed call. No image literal points at the rasterizer or the claimed interior.
The normal path scales cx/cy/r by texture-density at `0x4a7b03/06/09` then calls.

| Contract | Static evidence / proposed mapping |
| --- | --- |
| Entry | `0x50c940`: push ebp; mov ebp,esp; and esp,-8; 6 bytes `55 8b ec 83 e4 f8` |
| Continuation | `0x50c946 sub esp,0x64`; replay all three displaced instructions |
| Claim | 5-byte jump lies in one aligned8-byte word; six-byte whole-instruction span; no relative operand in displaced instructions |
| Interior | no decoded edges, no raw rel8/rel32 targets, no image dword targets in `0x50c941..45`; no base relocation overlaps span |
| Inputs | ECX=surface, EDX=cx; entry stack +4 cy,+8 r,+12 colour,+16 aspect |
| Entry liveness | ECX/EDX live; EAX dead (first write radius at `0x50c949`); flags overwritten by AND; EBP/EBX/ESI/EDI saved by stock |
| Return | both exits `0x50cd88`, `0x50cd9c` are plain ret; caller removes16 bytes; no ret16 |
| Saved frame | pushfd,pushad: EDX index5, ECX6, EAX7, flags8, return9, stack arguments10..13 |

Candidate thunk: read signed radius from `[esp+8]` using EAX, preserve ECX/EDX;
small/supported-stock path jumps to the published displaced-instruction tail.
Wide path saves flags/all GPRs, clears DF for C, calls an aligned C adapter,
restores registers and returns plainly when handled; on decline replay stock from
its first instruction. Preserve entry registers before fallback, especially ECX/EDX.
The stack alignment instruction in the trampoline is valid only after replaying
push EBP/mov EBP,ESP; never jump directly to the post-AND continuation.
Register/MXCSR/LastError preservation and exception policy are specified in the
deep review below. In particular the adapter must retain the caller's MXCSR
rounding and exception controls when reproducing B.

Suggested arithmetic policy: reuse AP's 64-bit `fill` only after supplying the FL
B conversion and explicitly selecting general-aspect routing/clipping behavior.
An aspect65536-only initial qualification proves circle compatibility but does
not establish a complete general ellipse fix. Do not advertise the full port on
that evidence alone. O(r) integer work with no per-span allocation is sufficient.

## Surface, helpers, lifetime and reentrancy

Surface +0 bits; +4/+6 signed16 dimensions; +0x10 flags; +0x30 backing interface.
Rasterizer returns without drawing for null backing or flag0x200000. When bits
is null it calls `0x4f5a90`: **ECX=backing, EDX=out, one stack argument0**, caller
pops4. On success stores returned EAX at surface+0 and ORs flag2; failure returns.
This is not AP's three-stack-argument lock ABI. Allocate at least0x1c bytes for
out; helper writes signed pitch at out+0x18 (`0x4f5b60`). The stock rasterizer
ignores this out pitch; do not copy it into a guessed surface field.

The lock uses interface vtable+0x30 GetDesc and +0x34 LockRect. On memory failures
it calls the global callback `[0x5aae24]` at `0x4f5b10`, protected only by global
`0x62de10` against nested recovery. It attempts LockRect at most twice; special flags0x800 failure
can recursively invoke itself at `0x4f5b45` (not selected by this caller's flags0).
The helper performs at most **two LockRect attempts total**, not two retries after
the initial attempt. Each OOM result can call recovery; the guard is reset between
attempts. Surface/backing lifetime is a native borrowed-reference assumption,
shared by a replacement using the same calls; the deep review below identifies
the concrete release route and additional obligations. Keep per-call state on
stack, with no singleton temporary surface/geometry.

Class function `0x50ba70..0x50bc09`: ECX=0, EDX=surface, signed AX result
(`cwde` at `0x50c9eb`); saves ESI, no calls or writes to object storage observed.
Dispatch jump table `0x50cda0`, 25-byte index table `0x50cdb8` yields:

| Class | Leaf | Behavior / end exclusive |
| --- | --- | --- |
| 0 | 0x4d42a0 | byte fill through0x521920; 0x4d42eb |
| 5 | 0x4d45e0 | converted16-bit colour, rep stos; 0x4d4650 |
| 10 | 0x4d44f0 | converted24-bit colour, byte stores; 0x4d457f |
| 15 | 0x4d42f0 | bitplanes, mask tables0x57b080/0x57c000; 0x4d44f0 |
| 24 | 0x4d4580 | converted32-bit colour, rep stosd; 0x4d45e0 |

Leaves take **five cdecl stack arguments (surface,x0,y,x1,colour)**; they load
surface from their stack, with no AP-style live EDI input requirement. Coordinates
are low16 words; width is inclusive. All return plainly. Row base uses table+0x58
when flag0x10 is clear and table exists, else signed16 stride surface+0x16 times
y. Class15 additionally uses plane spacing+0x1c and count+8. Reuse leaves instead
of inventing pitch/addressing or color conversion. Converted leaves call
`0x4f4e90` with ECX=surface, EDX=colour; the deep review below reconstructs its
descriptor calls and CRT dependency. Unsupported class prints at `0x50cd89`;
the replacement mirrors that diagnostic and returns without replaying the entry.

## Independent deep review: implementation contract

2026-10-28. Evidence is the named instruction sites above plus converter
`0x4f4e90..0x4f4fb3`, format mapper `0x4ea770..0x4eadb7` and its tables through
`0x4eae55`, CRT fill `0x521920..0x521a79`, and lookup `0x5123a0..0x512409`.
These four additional regions are now pinned by the verifier. Shared recovery
evidence is owned by [the UI render mutation study](ui-render-mutation.md);
the conclusions here concern this circle's borrow, not UI publication.

**Entry and shims.** The six-byte entry claim and saved-frame offsets in the
table are correct. ESI is live after return: `0x4a7b22` copies the texture id
from ESI into ECX before the tail lookup. Preserve EBX/EBP/ESI/EDI, ESP and the
caller's four stack arguments. EAX is unused on the return edge, and ECX/EDX,
arithmetic flags and XMM values are volatile here; `add esp,16` kills flags and
the tail lookup defines EAX before use. Restoring all GPRs is a simple sufficient
wrapper. Before falling back, restore ECX=surface and EDX=cx; EAX and arithmetic
flags need not equal entry values because the native prologue overwrites them.
DF is not killed by AND: clear it before calling compiled C, and preserve its
entry value in the wrapper. Stock string leaves assume the normal C ABI DF=0.
Use the project's four-byte incoming stack realignment flags for C; saving
pushad does not establish 16-byte alignment. Do not keep a frame pointer in a
caller-clobbered register across the C call.

The lock shim must pass ECX=backing, EDX=private out[0x1c], push flags=0, call,
then remove four bytes. It is **not** a compiler `__fastcall` function (that
convention would make this stack argument callee-cleaned). The class shim passes
ECX=0, EDX=surface and sign-extends AX. A leaf can be invoked as a five-argument
cdecl function; no incoming EDI or other secret register is required. Its return
value is ignored. All callee saves survive the inspected helpers. If class
selection yields an unsupported class, call the same native diagnostic directly:
`0x475110(0x590fc0)`, one cdecl stack argument, caller removes4 bytes
(`0x50cd89..0x50cd95`); then return handled. Do not jump into `0x50cd89` without
its native frame. This retains the native unknown-class effect without repeating
lock/class or reaching B conversion. All stock fallback decisions precede native
helpers; **there is no post-lock or post-conversion entry replay**.

**Floating-point and error state.** Reproduce the exact SSE2 conversion sequence
for signed q, including float32 rounding, before using signed int64 recurrence
terms. Do not substitute AP's integer quotient, set round-to-nearest globally,
or mask unmasked exceptions. `cvtdq2ps` and `divsd` observe caller MXCSR; native
SSE status bits may become sticky. Preserve control bits, retain native status
effects, and introduce no x87 work. Merely restoring the entire entry MXCSR
would erase native conversion effects. Lock/class happen before B conversion in
stock, so a pre-lock floating-point preflight changes observable exception and
side-effect order. A pure integer routing test is fine. There is no LastError
preservation guarantee from native descriptor/lock calls; preserve it around
**added** diagnostics, using the value after the native helper work. Neither
native raster nor lock installs a local SEH cleanup frame. An exception must
continue outward; never catch it and retry stock, unlock, or release the borrowed
surface. New RAII ownership or a global recursion guard would add cleanup
obligations absent from this design.

**Borrow boundary and reentrancy.** `0x4a7a6c` calls lookup `0x5123a0`, saves the
returned surface at caller ESP+0x18, optionally calls recorder `0x48ecc0`, then
passes the saved pointer to the raster. The cached lookup returns table+8 at
`0x512400`, with no object+0x60 retain. The raster retains surface in ESI through
lock, class and every leaf; the lock retains backing in EBX through both attempts.
No unlock/release occurs on raster return. Recovery's concrete texture route is
`0x47b500 -> 0x512410 -> 0x510c10`: entries with object!=0, slot+0xc==0 and no
slot flag0x20000000 are eligible; destruction happens if decrementing object+0x60
makes it nonpositive. `0x4f59e0` releases backing interfaces before the object is
freed. The circle caller itself does not establish that its slot is ineligible.
The guard at `0x62de10` only suppresses nested recovery, not nested raster calls
or general callbacks. This is an existing native borrow exposure, not evidence
that the replacement creates a new ownership problem or that deletion actually
occurs for the active circle. Do not invent a retain by changing private counts.

The converted leaves make **one descriptor call per emitted span**. Converter
`0x4f4e90` uses texture+0x34 / vtable+0x44 GetLevelDesc when surface flag0x4000
is set, otherwise surface+0x30 / vtable+0x30 GetDesc. It then calls the local
format mapper; that mapper's default path calls CRT fill. CRT fill has no calls,
uses only stores and optional SSE2 selected by CPU flags, and preserves EDI.
There is no game recovery callback inside this conversion path; the COM calls
remain native external-call boundaries. A replacement reusing the same leaf
inherits that exposure and must not hoist/cache the converted colour. Wide
geometry intentionally changes span count, so it also changes descriptor-call
count; no equality of total callbacks is claimed. No new allocations, logging,
locks or external callbacks belong inside the surface borrow. Defer a first-wide
diagnostic until all surface access finishes and log copied scalar values only.

**Remove the extra dimension-stability assumption.** Native computes the clip
flag from signed dimensions **before** lock (`0x50c975..0x50c9a9`). With clipping
enabled it reads width once per emitted pair (`0x50cb1d`, `0x50ccb2`), then reads
height independently before each row (`0x50cb4a/0x50cb72`, `0x50ccd6/0x50ccf8`),
including after the first leaf's descriptor callback. Therefore an FL adapter
must retain the pre-lock clip decision and reproduce these live reads in its
emitter. Direct AP `fill(...width,height...)` reuse snapshots dimensions and
adds an unproven stability requirement across recovery and descriptor callbacks.
The recurrence can be shared; the FL row emitter needs its own read policy.
Use a frame-local borrowed surface and fetch bits/row tables through the native
leaf, never a cached pixel pointer across callbacks. This gives the replacement
the same kind of borrow as stock without requiring a new global lifetime proof.

**Routing and clipping policy (replay-free decision).** Perform these integer-only
checks before lock, class, floating conversion, diagnostics or spans. Signed r
and centre/dimensions use the native low-word interpretation; aspect is signed32.

1. Route r<=0 or `r<=813 && aspect==65536` to stock. Otherwise set A=r² and
   `q=s32((s64(aspect)*aspect+0x8000)>>16)`. Route q<=0 to stock. No floating
   instruction is needed to obtain q, so no MXCSR status or exception is moved.
2. If q>=65536, admit wide: under every MXCSR rounding mode its float32 value
   remains >=65536, so native B lies in [0,A], within32767². No B prediction is
   required for this branch, and the native radius-box covers its horizontal
   extent. The actual SSE2 conversion still runs in native order after helpers.
3. If 0<q<65536, obtain the **exact eventual B** with unsigned64 integer division
   `B=(A<<16)/q`. Route B>32767² to stock. Preserve the native radius-box clip
   flag, computed from pre-lock dimensions. If that flag is false and B>A,
   run the wide recurrence with a no-write, no-allocation bounds-check emitter;
   route to stock on the first escaping span. Otherwise admit wide. This
   preflight runs only for those nonunit, unclipped ellipses and costs O(r),
   with no native callbacks. Do not add AP's unconditional B>A clip extension.
4. After admission, perform native no-draw/lock/class work and SSE2 B conversion
   in order, then emit wide spans. Lock failure/no-draw returns handled;
   unsupported class performs the native diagnostic above and returns handled.
   There is no numeric decline after conversion: the bounds were already proved.
   Exceptions propagate outward without entry replay or synthetic cleanup.

Proof for step3 [inferred mathematically]: q<65536 converts to float32 exactly,
and multiplying by2^-16 is exact. Let N=A·65536<2^46. The exact division is N/q.
An integral quotient is <2^46 and exactly representable as binary64. For a
nonintegral quotient, its distance to either neighboring integer is at least1/q.
One binary64 ulp at N/q is at most `(N/q)·2^-52 < 1/(64q)`, so even directed
rounding cannot cross either integer boundary. Truncation therefore gives N//q
in every rounding mode. Out-of-int32 results take stock before any conversion;
admitted results are <=32767². Denormals, FTZ and DAZ do not affect these normal
positive operands. The proof classifies the result only: inexact/unmasked
exceptions still occur at the actual native-ordered SSE2 conversion, not in the
integer preflight. Callback changes to MXCSR rounding do not invalidate the bound.

Native itself can emit out-of-bounds spans when the radius-box flag is false:
measured r300/aspect32768, centre400/400 in800×800 gives602 identical stock/wide
calls with x endpoints−199..999. Step3 forwards this input without any prior
native helper side effect. It also retains safe unclipped wide ellipses:
r900/aspect32768, centre2000/2000 in4000×4000 is admitted. Invalid/out-of-domain
inputs continuing through stock are not claimed corrected. The verifier checks
458745 binary64 adjacent-value bounds (seven radii ×65535 positive q values)
and seven routing witnesses; these are **modeled directed-rounding bounds, not
executed engine instructions or real MXCSR-mode qualification**.

**Claim publication and retirement.** Reuse `engine_patch` with length6,
ret_pop0 and rel32_offset0; displaced push/mov/AND need no relocation. Its tail
executes all six bytes before jumping to+6. Claim initially publishes the tail,
then publish the adapter continuation before pushing the thunk to the front.
The five-byte write fits one aligned qword; bytes5..7 are preserved. Pin the DLL
before claiming, install only during the open install window, retain tail/thunk
storage on restoration, and retain ownership state after failed rollback. The
existing arena is process-lifetime. Resolve addresses from the qualified image
base and normalize absolute helper/table operands for relocated-image validation;
preferred-base hashes alone are not runtime profiles. Readback and restore must
check ownership and actual bytes. No FL claim or rollback behavior was executed
in this documentation/probe review.

## Verification limits and implementation acceptance

Verifier passes thirteen region comparisons and15 independently mutated byte cases
(entry/helper/leaf/caller/table/float constant), the direct-edge/interior checks,
class mapping,825 equal-circle-radius cases, four aspect onset searches and the
clipping witness, plus20 named ABI/read-order operands,12 plain-return sites and
two call-free bodies. It is a derived Python model, not executed engine instructions.
No production stub exists in this change. Before claiming port acceptance:

- Implement the deep-review contract and replay-free arbitrary-aspect routing
  above, and verify load-base normalization
  for absolute operands/tables. Region hashes are preferred-base file hashes.
- Execute an offline machine-code oracle or stub fixture covering ECX/EDX inputs,
  four-byte incoming stack alignment, callee saves, plain return cleanup, lock
  failure/recovery, supported/unsupported classes, and all fallback decisions
  occurring before native helpers/conversion. Assert each effect occurs once.
- Compare ordered spans and canary-backed leaf writes at all four clipping edges,
  radii813/814/825/826/1290/1291/32767, varied centres and nonunit/invalid aspects;
  test MXCSR modes or constrain/reproduce the engine conversion explicitly.
- Mutation guards must reject wrong register conventions, ret16, truncated AND
  prologue, stale AP addresses and modified class/lock/leaf bodies. Existing
  binary-region negatives cover byte changes, not the unimplemented stub behavior.

Windows/CrossOver portability is inferred from engine-local calls and a portable
integer core; neither native platform behavior nor rendering has been verified.

## Shared midpoint traversal — 2026-10-28

AP and FL now use the edition-neutral `src/engine/common/circle_fill_core.h` for the
same signed-64-bit midpoint recurrence identified above at
`0x50ca87..0x50cd72`. AP supplies its unsigned-low-word initial Y and raw pair
coordinates; FL supplies signed-low-word initial Y and normalized pair
coordinates. These choices preserve the old helper interfaces, including their
behavior beyond admitted production calls. They are compile-time/inlined choices,
not per-draw edition dispatch. Both production paths bound B to 0..32767².

The adapters still own genuinely different behavior. AP retains its radius-only
fast route, integer axis calculation, dimension snapshot and extra B>A clipping.
FL retains its aspect-sensitive preflight, early-stop extent check, native SSE2
axis conversion/MXCSR effects and pre-lock clip decision. Its emitter reads width
once per clipped pair and height independently before each span, including after
a native leaf callback. Sharing AP's entire emitter would lose that behavior.
No hook, ABI, lifecycle, installation, CPU/LastError or recovery policy changes.

The [differential producer](../../../verification/results/fl-port/complexity-audit/circle-sharing-differential.py)
compares both new paths with independently compiled, pinned pre-change headers:
2,240 broad cases and 336,058 ordered output records match. This includes
32 AP, 96 FL pair and 32 FL emitter ordered cases with B>A. Existing independent
Python references and FL mutable-dimension/MXCSR checks remain unchanged; all
19 affected host tests pass using the [recorded command](../../../verification/results/fl-port/complexity-audit/circle-sharing-host.json),
which includes the adapter module and both verification Python paths. Fresh
i686 compilation of the differential harness emits no out-of-line recurrence
symbol; this is not production-adapter disassembly. The nine-run host timing comparison is noisy and establishes
no speedup; this is a maintenance simplification with no allocations, locks or
indirect dispatch added. Native game/CrossOver execution is not newly qualified.
The [review](../../../verification/results/fl-port/complexity-audit/circle-sharing-review.md)
covers source and producing evidence together.

## Port (2026-11-03)

Implemented: `src/engine/fl/circle_fill_fl.cpp` provides the `x3m::circle_fill` functions for the fl target
(`circle_fill_fix = on|off`, `X3M_CIRCLE_FILL_FIX`, unset = on); the sites, windows, routing, 64-bit recurrence and row
emitter are in `src/engine/fl/circle_fill_sites_fl.h`. AP's `circle_fill.cpp`/`circle_fill_sites.h` are unchanged; the
shared core and `engine_profile` layer of the 2026-10-28 attempt (section above, `fl-codex`) are not used. The
verifier is now `verification/probe/verify_circle_fill_site_fl.py` (it keeps the study's thirteen region hashes and
named operands); `verify_fl_circle_fill.py` never reached `main`.

| Item | As built |
| --- | --- |
| Claim | base+`0x10c940`, six bytes (`push ebp; mov ebp,esp; and esp,-8`), `engine_patch::claim` (one `lock cmpxchg8b`), tail replays all three, resumes at base+`0x10c946` |
| Identity | X3FL.exe identity (`fl::module_base()`), eleven windows (FNV-1a 32, relocated dwords zeroed; 24 relocations each checked equal to base + target): rasterizer with both tables, class function, lock, five leaves, diagnostic entry, caller block `0x0a7b0c..2e`, the float 2^-16 |
| Stub | `movsx eax, word [esp+8]`; r <= 0, or r <= 813 with aspect exactly 65536, jumps to the tail with ECX/EDX untouched; else `pushfd; pushad; cld`, C side, `popad; popfd`, then `ret` (handled) or the tail (stock from the first instruction) |
| Routing | `route()` before any native helper: `stock_fast`, `stock_quotient` (q <= 0), `stock_axis` (exact B > 32767²), `stock_extent` (unclipped, B > A, a span of the exact recurrence leaves the surface), else `wide` |
| Wide | no-draw checks, lock (asm shim: ECX = backing, EDX = out[0x1c], push 0, caller pops), surface +0/+0x10 as native, class (ECX = 0, EDX = surface), unsupported class: `0x075110(base+0x190fc0)` with the class call's ECX, then the engine's B sequence (asm: `cvtdq2pd`, `cvtdq2ps`, `mulss`, `cvtps2pd`, `divsd`, `cvttsd2si` twice) under the caller's MXCSR, then the spans through the native leaf with 64-bit d/A/B and the native emitter (pre-lock clip flag, width once per pair, height before each span) |
| Diagnostic | one `circle_fill_wide edition=fl` row after the last span, LastError (after native work) and an FXSAVE image restored around it |
| Row | `circle_fill site=… rva=10c940 edition=fl status=… reason=… mode=… setting=… write=… threshold=813 continue=…` |
| Shutdown | restores the six bytes only over our jump (`restore_not_owned` otherwise); fixture-only like every FL module (the FL loader has no unload calls; the module is pinned at install) |

Build: `circle_fill_fl.cpp` has AP's per-file options (`-fno-exceptions;-mno-sse;-mno-mmx;-mfpmath=387`), so the
compiled C side is integer only; the B conversion and the register shims are file-scope asm.

Facts established while porting (measured on X3FL.exe and by executing its relocated code in the fixture):

- With ECX = 0 and EDX = surface the class function returns only 0, 5, 10, 15 or 24 (flags bit 4 clear: 15; bit 4 with
  0x200: 10, with 0x100: 5, with 0x2000: 24, alone: 0). The unsupported-class diagnostic is unreachable from this
  caller; the port keeps it anyway (fixture: a fake class function).
- The diagnostic `0x075110` reads ECX (`push ecx`) besides its stack argument; native passes the class function's
  leftover ECX, so the port passes the same value.
- Coordinates stay the engine's 16-bit words. Near y = 0 a span of a disc with r close to 32767 has one end beyond
  ±32767 and wraps; the stock routine and the port both drop it (x0 > x1). The exact recurrence removes the 32-bit
  decision overflow (the scanner-disc collapse from r 826), not the 16-bit coordinate range.
- The lock's two LockRect attempts, the recovery callback per out-of-memory result and the guard word reset between
  them run unchanged inside the port (fixture: real lock code, fake COM backing).

Verified (measured 2026-11-03, `verification/results/fl-circle-fill/summary.json`, producer `reproduce.py` beside it):

- `verify_circle_fill_site_fl.py /mnt/c/X3FL/X3FL.exe`: PASS, 29 checks (windows and relocation census, the study's
  13 SHA-256 regions, entry span and qword, only caller `0x0a7b19`, upstream callers, no entry into the span, 66 named
  operands incl. ECX/EDX convention and `add esp,0x10`, 12 plain returns, class table, B witness 2572/2573, 458,745
  directed-rounding bounds, eight mutation guards: ret 16, caller EDX from ECX, caller ECX load, truncated AND, class,
  lock and leaf bodies, entry at the AP address).
- Native fixture `verification/probe/fl_circle_fill.cpp` (MinGW i686, project flags, run through WSL interop on the
  Windows host; X3FL.exe mapped relocated at a 64 KB base ≠ 0x400000; the real rasterizer, lock, class function,
  class-0 leaf and CRT fill execute): 962 checks, 0 failures. 22,448 window bits and 24 rebound relocations refused,
  six named mutations, stale and misaligned bases, an unreadable window; claim/read-back/chain/rollback-failure/
  not-owned/shutdown with 5 faults; six initialize rows plus `late_claim`. 993 calls kept EBX/ESI/EDI/EBP, ESP (plain
  `ret`, caller pops 16), the stack words, DF, x87 CW, XMM7, MXCSR controls and LastError, at four incoming alignments.
  871 plain runs (1,225,682 ordered leaf records) equal `test_fl_circle_fill.py`'s independent Python model: native
  runs the wrapping routine, patched runs by route (stock: wrapping, wide: exact), routes equal the note's routing.
  Radii 813/814/825/826/1290/1291/32767 at all four edges, unclipped and cap placements; aspects 32768, 98304, 131072,
  1051682, -65536, 65535, 65537, 46341, 0, 0x7fffffff, 256. MXCSR RC 0..3 and FTZ|DAZ: 30/30 patched = native (spans,
  effects, MXCSR out). Lock ok / OOM then ok / OOM twice / vidmem twice / E_FAIL twice / no backing / no-draw flag at
  r 500/820/1291, dimension changes by a leaf or the recovery callback, unsupported classes 3/25/-1/0x7fff: patched =
  native. Canary-backed real class-0 leaf writes: 12/12 inputs, 8,157,959 pixels, no byte outside the spans.
- Static walk of the RelWithDebInfo FL DLL from `_x3m_circle_fill_fl_wide` (`walk_wide.py`): no x87; XMM only in
  the asm B conversion. AP objects (`capture.cpp`, `loader.cpp`, `circle_fill.cpp`) disassemble identically to `main`
  (`ap_objects_identical.py`).

Not verified: not flown (no FL scanner disc seen in game); native Windows rendering unverified (the fixture executes
the engine's code natively but not the game); unmasked floating-point exceptions and exception propagation through the
stub not exercised; general-aspect ellipses are routed as documented but the scripts' aspect in game is unobserved.
