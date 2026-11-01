# Farnham's Legacy text-density port contract

Offline, 2026-10-28. This qualifies the bounded engine contracts needed to port
**the existing AP text-density feature**; it does not add UI refresh or activate
FL support. Inputs are clean `/mnt/c/X3AP-orig/X3AP.exe` and
`/mnt/c/X3FL-orig/X3FL.exe`, with SHA-256 respectively
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` and
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
All addresses are **preferred VAs**, requiring the qualified FL base/RVA and
HIGHLOW normalization at runtime. Measurements below are static; rendering,
Windows/CrossOver execution and hook publication were not tested.

Owners reused: [AP fonts](../font-rendering.md),
[FL graphs](ui-graphs.md),
[initial UI owner](ui-layout-generation.md),
[recovery](ui-render-mutation.md), and
[circle borrow/locking](circle-fill.md). The circle arithmetic
fix remains a separate implementation. This note corrects the preliminary
font-register interpretation in [FL engine feasibility](engine.md).

## 1. Initial commit and resource policy

The earliest **already-qualified owner boundary with both actual device and CPU
screen dimensions** is normal return from `0x47765a -> 0x516890`, before native
`add esp,8` at `0x47765f`. Use the existing initial-publication adapter and its
actual device/output-slot/screen checks. Return EAX is the 0x13c-byte display
container: load its pointer slot0 to obtain the screen surface, then read signed
surface words +4/+6. Container +4 holds a separate surface pointer. The later
caller store `0x477662` has not run, but the callee already publishes the container
at `0x5168ab`; the caller-store ordering does not imply a null/stale global.
Use the checked returned-owner chain, not a container-as-surface read, configured
dimensions, the last observed device or premature UI capability. The corrected
[owner study](ui-layout-generation.md#returned-owner-correction-2026-10-28)
binds this distinction to allocation, both surface slots and native queries.

The actual subsequent ordering is:

| Boundary | Consequence |
| --- | --- |
| Config default `0x509ed5`, option store `0x50a371` | density is `[config+0x774]`, config pointer `*0x5a83b0`; **not AP +0x784** |
| CreateDevice `0x4f2489` | device exists; CPU screen creation/GetDesc remains later, per owner note |
| display return `0x47765f` | stage/commit density from the accepted initial scale and actual device caps; font assets and every mandatory claim must already be admitted |
| `0x4777db -> 0x4a69f0`; `0x4a69f6 -> 0x5116c0` | load Materials, then apply density row edits **on normal return**, before `0x4a69fb -> 0x5125c0` |
| `0x477d15` story load; `0x477e21 -> 0x41df90` | later startup cockpit constructor opens the first witnessed native font; KC font operations also occur after native registration/UI initialization |

The Materials loader allocates its table at `0x51175a -> 0x511cc0`, registers
names with `0x511bf9 -> 0x511f60`, and copies flags at
`0x511c3a -> 0x512280`. Registration explicitly writes entry+8/+0xc = 0 at
`0x511fd3/0x511fda`; it does not lazily create a texture. The post-load seam
therefore has row/table metadata before those objects exist on this normal
startup path. Validate count, pointers and null generated-row objects before
editing; do not silently modify already-created objects after an unexpected
re-entry. Direct-call census finds one Materials-loader caller. This is a
bounded normal-startup contract, not proof against all indirect/custom natives.

The old AP `device_created(scale)` resolves only once and removes claims for
scale1. Calling it at FL CreateDevice with the still-uncommitted UI scale1 would
permanently disable automatic density. Stage caps there; resolve at successful
initial owner publication. Keep actual scale, density, row flags, font requests,
style offsets and sampler policy coherent. On Materials return, fit density to
the **actual loaded rows** and device limits before any font open; if lowered,
update config/style/native literals together. Do not reselect density on Reset
without a separate complete font/texture rebuild contract.

### Effective clean-media comparison

The probe uses mounted root then edition catalogues and exact resource keys,
plus same-key loose candidates. It does not invent addon-prefix fallback.
Font loader `0x514a91..0x514aa7` uses config path descriptor+0x3c = `F\\%s`
(`0x5a5cdc -> 0x58b104`), `tga bmp` at `0x5918c0`, then `abc siz` at
`0x5918c8`, through extension reader `0x505e00`. The qualified resolver's
loose-first extension behavior therefore applies to generated `F` pairs.

Measured: all **12 shared font assets** have identical decoded AP/FL hashes.
The default/L007/L034 Fonts tables are identical, including size/y-offset:
Zekton26/8, Harrier24/5, ZektonES26/0 and Tahoma13/1. Existing generated AP
pairs for densities2/3 are reusable for those requests. FL parser
`0x514da8/0x514dc7` accepts ABC versions4/5; version5 copies the code map at
`0x514df4`, reads count at `0x514e0d`, and steps records by24 at `0x514f0e`.
Glyph source coordinates still derive from UV×atlas dimensions plus y-offset,
now through SSE conversions (`0x514e80..0x514ec4`). No new asset format is needed.

**Japanese gap:** FL `addon2/04.cat` adds `types/Fonts-L081`, requesting
`LARGE;UmePlusCLP;23;0` and `HUD;Tahoma;13;2`. No UmePlus font asset is present
in the inspected clean root/edition catalogues or loose tree. The AP generator
has no UmePlus family, and its Tahoma bands target y-offset1. Checking just AP's
four installed families cannot admit this language safely. Fail density admission
for unsupported effective font requests, or add separately verified assets and
a language-aware Tahoma band/name policy. In particular native cockpit Tahoma
still requests y-offset1, whereas L081 HUD asks for2; the name-keyed font cache
makes a blanket y-offset change insufficient. Asset acquisition is outside this
study. Custom Fonts overrides similarly need a defined admission/fallback policy.

| Measured Materials property | Clean AP | Clean FL |
| --- | --- | --- |
| effective source | addon/01.cat | addon2/04.cat |
| rows | 1237 | 1239 |
| generated + writeable | 34 | 36 |
| already FONTSCALE in that set | 10 | 10 |
| NOFILTERING in that set | 27 | 29 |
| file-backed FONTSCALE | 8 | 8 |

FL adds rows1237 (512×64),1238 (512×1024); both need the existing generic row
policy. The clean AP count differs from the older installed/modded 35-row study;
do not hard-code either. File-backed rows46/47/56/158/219/327/568/705 lose
FONTSCALE as before. Largest generated width/height are1024/2048; row8, not the
1024² graph row, sets the height cap (4096 at d2,6144 at d3). Apply the generic
row predicate, setting FONTSCALE and clearing NOFILTERING when s != d, to both
row+0x10 and table-entry+4. Keep per-draw linear-sampler override/restoration for
flagged generated textures at fractional scale; clearing the row flag alone
was insufficient in AP. The FL D3D texture pointer is object+0x34, written at
`0x4f59c7`, cleared/released at `0x4f5a6b..0x4f5a7e`.

## 2. Hook and ABI mapping

All entry detours below need saved per-invocation frames, four-byte incoming
stack alignment, preservation of untouched GPRs/flags, FP/SIMD controls and
LastError around added bookkeeping. Clear DF for C and restore it for native
continuation. A plain `ret` means caller cleanup; these register-plus-stack ABIs
are **not compiler fastcall**. Forward native returned state unless deliberately
replacing it. Do not copy AP pushad argument indices blindly.

| Role (AP address) | FL boundary / whole span | Actual FL contract |
| --- | --- | --- |
| font entry (0x48cdc0) | `0x4a8990`,7 bytes; continue `0x4a8997` | ECX=name; **EDX=cell width**; stack size,flags,yoff. Replay push ECX/EBX/ESI; mov ESI,[ESP+0x10]. Scale EDX, stack size and yoff. EAX result slot or−1; plain ret. |
| Materials call (0x48af71) | `0x4a69f6`,5-byte call→`0x5116c0`; return `0x4a69fb` | no arguments; preserve returned state around post-load edits; next native instruction is another call. New wrapper must use its own return stack, not AP's singleton return-address cell. |
| style factor (0x4f813b immediate) | `0x5151a2`,5 bytes; continue `0x5151a7` | push ESI; push EDI; **setg BL consumes CMP flags** from `0x51519b`. Native EBX=0, later factor=EBX+1. For d3 set BL=2 after reproducing pushes; preserve ECX=name, EBP=cell width and native stack. Fallback must replay SETG with original flags. |
| Block (0x48c090) | `0x4a7b30`,6 bytes; continue `0x4a7b36` | ECX=src id, EDX=dst id; stack sx,sy,dx,dy,w,h,colour,clip (8 words). Replay sub ESP,0x34; push EBX/EBP/ESI. |
| Alpha (0x48c460) | `0x4a7f80`,6 bytes; continue `0x4a7f86` | same arguments/prologue; colour path is alpha blending. |
| Image block (0x4960c0) | `0x4b22f4`,7 bytes; continue `0x4b22fb` | ESI=KC record; EDX=factor; EBP frame−0x50=source,−0x20=destination; replay MOVSX ECX,[ESI+0x24]; MOVSX EAX,DX. |
| text-line diagnostic (0x48b2d0) | `0x4a6d90`,6 bytes; continue `0x4a6d96` | ECX=dst, EDX=text; stack font,x,y,right,colour,flags. Replay sub ESP,0x14; push EBX/EBP/ESI. |
| rect diagnostic (0x48b0b0) | `0x4a6b60`,6 bytes; continue `0x4a6b66` | ECX=dst, EDX=x; stack y,width,height,colour. Replay sub ESP,0x2c; push EBX/EBP/ESI. |
| graph line candidate | `0x4a7660`,6 bytes; continue `0x4a7666` | ECX=dst, EDX=x1; stack y1,x2,y2,colour; same four instructions as text-line entry. |

Block/Alpha preserve caller-cleaned eight-word arguments and return the destination
lookup result in EAX (tail lookups at `0x4a7f50/0x4a7f74/0x4a8397`). Their
entry SUB overwrites arithmetic flags, but ECX/EDX are live input registers.
An unhandled adapter must restore both before replay. Diagnostic adapters retain
the native text return value; no replacement operation belongs in them.

Font naming is proved by the ordinary formatter pushing entry-stack size at
`0x4a8998`, and native caller `0x420dc4/0x420dc6` pushing13 and loading EDX=16.
The earlier EDX=size interpretation was wrong, although both dimensions require
multiplication. The font cache at `0x5a8c98` has10 slots keyed by **formatted
name only** (`0x515445..0x515486`), so first-open arguments determine later reuse.

**Additional native inline opener:** cockpit constructor `0x41df90` formats
Tahoma13 itself (`0x41dffb`) and directly calls `0x515190` at `0x41e00f`.
It bypasses the ordinary font-entry hook. The other direct style caller is
`0x4a89b7`; ordinary font entry has two direct callers, `0x420dd0/0x4b2394`.
An implementable bounded startup solution is four validated immediate edits:

| Instruction / immediate | Stock → density d |
| --- | --- |
| push13 at `0x41dfd9`, imm8 `0x41dfda` | formatted suffix13→13d |
| push1 at `0x41e000`, imm8 `0x41e001` | y-offset1→d |
| mov EDX,16 at `0x41e004`, imm32 `0x41e005` | cell width16→16d |
| push13 at `0x41e00b`, imm8 `0x41e00c` | requested size13→13d |

All values fit at d2/3. Apply/verify/rollback them together with config/style
before that constructor can run; the imm32 crosses a qword. No runtime hook or
new callback is required for these fixed native literals. Never change just the
name or just the raster size. The constructor has an FS:[0] SEH frame; these
literal edits leave it intact.

The style loop covers11 records of stride0x44 (`0x515434..0x51543d`);
flags1/4 force factor1 at `0x5151cd..0x5151d7`. Preserve this exception: replacing
the entire style table with d-scaled offsets would be wrong. No AP MOV-imm32
style patch exists in FL. A small inline adapter at the listed span can set
BL=d−1 while retaining the native loop and its flag1/4 exception.

Image handling can reuse the AP bounded coordinate planner with a new frame
adapter. Record dst is dword+1, six source/destination words start+0xb,stride5.
Native sign-extends each word, multiplies by signed low16 factor, then zero-extends
the product's low16; modulo16 result matches AP's word multiplication. Source
rectangle scaling remains hazardous. Run the clipped copy once with a compatible
shadow or safe unscaled-source fallback. Handled continuation can jump directly
to **`0x4b235a`** with original ESP and ECX low16 restored from record+1; this
skips both native copy and its `add esp,0x20`. Alternatively reserve exactly32
bytes and resume `0x4b2353`; do not mix those conventions. EAX/flags are dead
there (lookup defines result); ESI,EDI,EBP,EBX and the dispatcher SEH/aligned frame
remain live. Failure after copy/lock effects must not replay entry.

Probe decodes9 candidate spans and finds **zero direct interior edges** among
428814 decoded .text instructions; zero relocation overlaps those spans.
Materials call and Image jump cross qwords; all other listed five-byte jumps fit.
This is not an indirect-entry/thread proof. Use quiet startup install/rollback,
qualified relocated expected windows, process-lifetime tails and ownership/readback
checks. Pin helper functions and native literals too. Instruction-immediate edits
are not entry detours and must never be entered at their operand bytes.

## 3. Surface helpers, borrows and recovery

| Helper | FL ABI / established layout |
| --- | --- |
| lookup `0x5123a0` | CX signed id; EAX=borrowed object or0; lazy creator `0x5113b0`; no retain |
| alloc `0x510c90` | CX=width,DX=height; stack bpp,flags; plain ret, caller pops8; EAX=owned object or0 |
| free `0x510c10` | ECX=object**; plain ret; decrements object+0x60, releases when <=0, clears caller slot |
| plain copy `0x4f4590` | 8 cdecl stack arguments src,dst,sx,sy,dx,dy,w,h; plain ret |
| copy leaf wrapper `0x50bc10` | **ECX=src, EDX=dst**; stack sx,sy,dx,dy,w,h; caller pops24 |
| colour wrapper `0x50bd20`, alpha `0x50cde0` | **ECX=src, EDX=dst**; seven stack words sx,sy,dx,dy,w,h,colour; caller pops28 |
| class `0x50ba70` | ECX=src or0, EDX=dst; signed AX result; reads flags, no retain |

Row count `0x61b410` is i16; rows `*0x61b418`,stride0x3c,+0xc validity,
+0x10 flags,+0x14/+0x16 dimensions. Texture table `*0x5cb3f4`,stride0x10:
+0 name,+4 flags,+8 object,+0xc usage. Fixed count `0x61b40c`, named count
`0x5fb404` (both i16); do not substitute another nearby count.
Object allocation is0x64 bytes: +0 bits,+4/+6 i16 dimensions,+8 bit depth,
+0xc i16 bytes/pixel,+0x10 flags,+0x14/+0x16 stride-related words,+0x30 surface,
+0x34 texture,+0x58 row table,+0x60 reference count. Allocating32bpp with0x402c
adds0x2000 (`0x510ced..0x510d21`), giving0x602c. Reuse native pitch/addressing.

Copy/colour/alpha call pair lock `0x4f5b70` first, then class; pair lock first
locks destination with flags0, then source with flags0x10, both through
`0x4f5a90`. On source-lock failure it unlocks the destination backing and clears
destination bits/flag2 (`0x4f5bd9..0x4f5be7`), then reports failure. Do not reorder
or duplicate these effects. Plain copy's25-class table copies only same-depth
classes0/5/10/24; others return without conversion. Alpha accepts only class24.
32-bit leaves are copy `0x4d2fb0`, colour `0x4d3500`, alpha `0x4d3670`.
Thus AP's image-shadow predicate (pixels bit4 and depth mask exactly0x2000)
remains valid; a 16/24-bit source into a32-bit target is a native no-copy case.

The image cache is object `*0x5aa070`, path `*0x5aa0a4`; replacement frees at
`0x4b2225` before loading `0x4b227a`. Texture-system teardown `0x4a6af7..0x4a6b53`
releases the table/cache. Pointer equality alone is not generation identity.
Invalidate owned shadows and filter pointer snapshots on pre-Reset, owner loss,
texture-table teardown and image replacement/generation change. A copied path,
id, size and pointer tuple is useful but cannot prove content identity across
same-address reuse. Borrowed pointers must not escape an invocation.

**Concrete additional shadow exposure:** AP `make_shadow` allocates after it
has borrowed source/destination objects. FL allocator calls heap allocator
`0x4d8fc0` and backing allocator `0x4f5710`; both can enter recovery before
returning (heap `0x4d906a`, CreateTexture OOM `0x4f5853`, other backing path
`0x4f5950`). Reused circle evidence identifies the actual recovery free route
`0x47b500 -> 0x512410 -> 0x510c10`. Merely copying AP's cache lock and shims is
not a lifetime proof. Stage allocation before final table lookup, then re-resolve
ids and validate current dimensions/flags before copying. Do not hold an
SRWLOCK across those native callbacks: it adds a reentrant deadlock opportunity.
Owned unpublished shadows need explicit exception cleanup/retirement; a foreign
unwind must not leave the cache permanently locked or publish a partial object.

Native copy itself borrows both surfaces through pair-lock/recovery, just as
the circle borrows its surface through lock. This existing native exposure is
not evidence of safe retention, nor proof it actually frees the active source.
A replacement may reuse that exact native operation/order, but added allocations,
logging or cache callbacks inside the borrow need their own contract. Source
and destination lookup must account for lazy creation of the second object:
refresh the first after any added allocator boundary. All composite primitive
recursion uses per-call frames; the recovery guard only suppresses nested
recovery, not all nested drawing. Cache publication needs generation/in-flight
rules; no private refcount increment is authorized by this note.

Performance bound (inferred from the existing algorithm): first shadow build makes
O(dW+dH) native copy calls and one surface allocation; each copy repeats native
pair-lock/class selection. Cached blits need bounded cache lookup and the native
copy. Keep diagnostics and allocation out of the hot cached path, avoid a full
Materials scan per draw, and retain the existing once-per-Present filter snapshot.
No runtime cost or loading-time improvement is measured by this study.

Preserve native exception propagation through helper calls, never catch and
retry the stock operation after a partial copy. Preserve LastError around added
bookkeeping rather than erasing native helper effects. The Materials loader and
font loader have native SEH frames; post-return wrappers leave them intact and
must not publish on exceptional exits. Shared Windows/D3D documented interfaces
are sufficient; no Wine-private dependency is required, but no runtime parity
claim follows from this static result.

## 4. Graph stroke exception and bounded acceptance

Rows20/1236 carry text and graph primitives together. The native line wrapper
multiplies all endpoints byN at `0x4a779b..0x4a77ab`, clips through `0x50bfa0`,
then calls `0x50bea0`; its software raster still emits one-texel strokes. Rect
wrapper scales x,y,width,height at `0x4a6d4e..0x4a6d59`, so bars grow in both
axes. There is no row-specific engine exemption. **Leaving row1236 at1x breaks
globally scaled text on that same row**; it is not a complete workaround.
The later [ratified adapter](../../architecture/farnhams-legacy-text-density.md)
retains this row policy and expands supported lines through N² translated native
clip/raster calls. Existing native tests compare 32 complete raster buffers
(16 segments at N=2/3), with separate forwarding and retirement controls.
The original engine behavior above explains the need for that implemented
footprint; final sampling and perceived graph quality remain live acceptance. The reviewed circle fix is separately required for scaled
scanner circles; it does not fix graph lines.

Implementation can now bind the listed hooks, four native literals, helper ABIs
and startup order. Remaining concrete edges are language-aware asset admission
(L081/custom Fonts), a cache/allocator reentrancy and retirement implementation,
and the graph line-footprint policy. Test new adapters with fake native helpers
for four-byte stack alignment, exact cleanup, register/flag/DF/LastError state,
normal/declined/exception paths and callback reentry. Test copy/lock failure
without replay and cache invalidation before Reset/teardown. Native-image/frame
and font-entry mutation guards must reject stale AP layouts. Then request one
consolidated user run covering d2/d3 and fractional UI scale, graph lines/bars/
labels, image menu backgrounds, map circles, alt-tab cursor and Reset. No game
or Wine run belongs to this offline RE checkpoint.

## Reproduction

```sh
python3 verification/probe/verify_fl_text_density.py --raw
python3 -m py_compile verification/probe/verify_fl_text_density.py
```

Measured result:38 finite regions,49 exact ABI/layout operands,33 direct call
witnesses;38 independent byte mutations rejected;9 whole-instruction spans;
428814 decoded instructions in the conservative direct-edge census;12 identical
shared font assets. The checker validates its written JSON. Raw listings,
resource hashes and producing wrappers stay in ignored
`verification/results/fl-port/text-density/`. Region fingerprints pin this
binary's evidence, not runtime hash-only admission. No production code, build,
installation, commit or shared evidence tree was changed by this study.

Independent deep review accepted this bounded mapping without actionable
findings. Read-only verification reproduced 38 regions, 49 operands, 33 call
witnesses, 38 rejected mutations, nine instruction spans and 12 matching font
assets. The font entry uses ECX/EDX with three caller-cleaned stack arguments;
it must not be declared compiler fastcall. Resource policy, allocation/recovery
and graph-stroke implementation remain open as described above.
