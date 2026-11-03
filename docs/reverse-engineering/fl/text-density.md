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

**Byte-verified 2026-11-03** against `/mnt/c/X3FL/X3FL.exe` (same SHA-256): every site and data item of the AP
module, as RVAs, is in [Verified 2026-11-03](#verified-2026-11-03-against-x3flexe) at the end of this note, with the
corrections to the sections below. Addresses in sections 1-4 are preferred VAs (RVA + `0x400000`).

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

## Verified 2026-11-03 against X3FL.exe

Static, read-only, against `/mnt/c/X3FL/X3FL.exe` (SHA-256 `e7f3aad8…dfbb8`, link stamp `0x618416c8`, image size
`0x391000`, 38,327 HIGHLOW fixups, 72,230 direct edges in the linear sweep). Every address below is an **RVA**
(preferred base `0x400000`, ASLR: the module rebases them). Verifier
[`verify_text_density_sites_fl.py`](../../../verification/probe/verify_text_density_sites_fl.py): **PASS** (13 sites,
168 Capstone anchors, 22 identity windows, data, exact caller sets, config-writer census, EBX/EAX liveness, init
order); 23 single-byte in-memory mutations at the pinned sites all turn it to FAIL. Output, census and mutation
scripts: `verification/results/fl-text-density/`. Nothing ran in the game; no runtime behaviour is claimed.

"Verified" = bytes, instruction boundaries and the stated register/layout facts match; "differs" = verified, but the
FL shape is not the AP one and the AP code cannot be reused unchanged; "not mapped" = no one-to-one counterpart needed.

### Sites (`text_density_sites.h`)

| Item | AP VA | FL RVA | Bytes / anchor | How confirmed | Differences |
| --- | --- | --- | --- | --- | --- |
| font entry (claim) | `0x48cdc0` (6 B) | `0x0a8990` (7 B, continue `0x0a8997`) | `51 53 56 8b 74 24 10` push ecx/ebx/esi; mov esi,[esp+0x10] | bytes, lengths 1/1/1/4, no edge or pointer inside, qword-contained; callers exactly `0x020dd0` (native Tahoma13) and `0x0b2394` (KC `B3D_OpenFont`) | **differs**: ECX = name, **EDX = cell width**, stack `[esp+4]` size, `+8` flags, `+0xc` y offset (AP: EAX name, ECX size, stack cell/flags/yoff). Scale EDX, `[esp+4]`, `[esp+0xc]`. Two direct callers (AP four) |
| "%s%d" request name | `0x48cdc8` | `0x0a899a` push `0x58cc38` (fixup `0x0a899b` -> RVA `0x18cc38` = `%s%d`) | formatter `0x10b890(0, "%s%d", name, size)` at `0x0a89a3` | anchors + relocated-dword check | verified; the name carries the **stack** size, so the size argument must be scaled before the replayed span |
| font open / cache | `0x4f8120`, cache `0x606f0c` | `0x115190`; cache `0x1a8c98` (10 slots, name-only compare `0x115445..`) | callers exactly `0x01e00f`, `0x0a89b7` | anchors, caller set | verified (inherited name-only cache claim holds) |
| native cockpit inline opener | (AP opens through the entry) | `0x01df90`: four literal bytes `0x01dfda` (push 13), `0x01e001` (push 1 = y offset), `0x01e005` (low byte of mov edx,16), `0x01e00c` (push 13) | `6a 0d` / `6a 01` / `ba 10 00 00 00` / `6a 0d`, flags `0x21` at `0x01e009` | bytes, lengths, no edge/pointer to the edited instructions; callers of `0x01df90` exactly `0x077e21`, `0x0798a5` | **FL-only**: formats `Tahoma13` and calls `0x115190` itself, bypassing the entry. Four single-byte edits (d2: `1a/02/20/1a`, d3: `27/03/30/27`); write only the imm32's low byte (the imm32 `0x01e005..8` crosses the qword at `0x01e008`) |
| style factor | imm32 at `0x4f813b` | disp8 of `lea edx,[ebx+1]` at `0x1151b6`: byte `0x1151b8` | `cmp [eax+0x774],1` `0x11519b`, `setg bl` `0x1151a4`, `lea edx,[ebx+1]` `0x1151b6`; type 1/4 keep 1 (`0x1151cd..0x1151d7`) | bytes; EBX written at `0x115197`/`0x1151a4`, read only at `0x1151b6` until `pop ebx` `0x1154aa` (Capstone regs census); no edge into `0x1151b6..8` | **differs**: factor = `(N>1) + disp8`. One byte `01 -> 02` gives 3 at N=3, but unlike AP's `mov edi,2` it is **not inert at N=1** (factor 2): apply after `cfg+0x774 = 3`, restore before the field goes back to 1. The type 1/4 exception is the same in AP (`0x4f8160..0x4f8167`) |
| Materials call (claim) | `0x48af71` (5 B, qword-contained) | `0x0a69f6` `call 0x1116c0`, return `0x0a69fb` | `e8 c5 ac 06 00` | bytes, call target; sole caller of `0x1116c0`; wrapper `0x0a69f0` sole caller `0x0777db` | **differs**: crosses the qword at `0x0a69f8` (rel32 at `0x0a69f7..a`). Equivalent post-load seam with no crossing: the next instruction `0x0a69fb` `call 0x1125c0` (`e8 c0 bb 06 00`, Animations load, AP `0x4f5460`; sole caller). Wrapper frame is `push ebp; mov ebp,esp; and esp,-8`; returns 1/0 |
| block blit (claim) | `0x48c090` (7 B) | `0x0a7b30` (6 B, continue `0x0a7b36`) | `83 ec 34 53 55 56` | bytes, lengths 3/1/1/1, 12 direct callers, none inside, no pointer | **differs**: ECX = source id, EDX = destination id, 8 caller-cleaned stack words sx,sy,dx,dy,w,h,colour,clip (AP: 10 cdecl words). EAX dead at entry (first access a write on both paths: `0x0a7b53` / lookup); result = tail `jmp 0x1123a0` with ECX = dst |
| alpha blit (claim) | `0x48c460` | `0x0a7f80` (6 B, continue `0x0a7f86`) | `83 ec 34 53 55 56` | as above, 11 callers | as block; leaf `0x10cde0` at `0x0a8386`, tail `0x0a8397` |
| blit row count read | `0x48c265` | `0x0a7d24` `mov ax,[0x61b410]` + `cwde` | fixup -> RVA `0x21b410` | window `blt_block_reads` | differs in form (mov+cwde vs movsx) |
| blit row flag test | `0x48c28b` | `0x0a7d53..0x0a7d67` `mov eax,[rows]; ...; test [eax+ecx*4+0x10],0x10000; je` | fixup -> `0x21b418` | anchors | verified |
| blit config read | `0x48c29a` | `0x0a7d69` `mov eax,[0x5a83b0]`; `0x0a7d6e` `mov ecx,[eax+0x774]` | fixup -> `0x1a83b0` | anchors | verified; destination W/H divided by N (`idiv` `0x0a7d77`) as AP |
| image block (claim) | `0x4960c0` (8 B) | `0x0b22f4` (7 B, continue `0x0b22fb`) | `0f bf 4e 24 0f bf c2` movsx ecx,[esi+0x24]; movsx eax,dx | bytes, lengths 4/3; inbound edges exactly `0x0b22d1`, `0x0b22e7` (to the start); displaced loads write ECX/EAX, `imul` `0x0b22fb` writes EFLAGS | **differs**: factor in **EDX** (AP CX), source object `[ebp-0x50]`, destination object `[ebp-0x20]` (AP EBX, `[esp+0x10]`), ESI = record (dst `+1`, six words from `+0xb` stride 5, unchanged); words sign-extended. Crosses the qword at `0x0b22f8` |
| image scale block | `0x49608b` | `0x0b22c0..0x0b22f4` | edx=1; row flagged -> `edx=[cfg+0x774]` `0x0b22ee` | window `image_scale` | verified |
| image call / return | `0x4960fc` / `0x496101` | `call 0x0f4590` `0x0b234e`; return `0x0b2353` (`mov cx,[esi+1]; add esp,0x20; call lookup`) or `0x0b235a` (after the `add`) | anchors | lookup `0x1123a0` reads no EAX before writing it | verified; handled path: reserve 0x20 and resume `0x0b2353`, or keep ESP, set CX and resume `0x0b235a` |
| image case, table | `0x495fc1`; table `0x49679c`, native **109** | case `0x0b21ed`; dispatcher `0x0afbf0` (published by pointer `0x08289a`), `cmp esi,0xa9` `0x0afc35`, table `0x0b297c`, **index 111** (fixup `0x0b2b38`) | anchors, relocated entry | **differs**: case index 111 of 0xaa (AP 109 of 0xa8) |
| image object / path slots | `0x6085cc` / `0x6085c8` | `0x1aa070` / `0x1aa0a4` | stores `0x0b228b` / `0x0b2295`; free `0x0b2225 -> 0x110c10` | anchors | verified |
| text line diag | `0x48b2d0` (6 B) | `0x0a6d90` (6 B, continue `0x0a6d96`) | `83 ec 14 53 55 56` | bytes, 5 callers, none inside | **differs**: ECX = dst, EDX = text, `[esp+4]` font slot (`0x0a6dd7` indexes `0x1a8c98`), x, y, right, colour, flags |
| rect fill diag | `0x48b0b0` (8 B) | `0x0a6b60` (6 B, continue `0x0a6b66`) | `83 ec 2c 53 55 56` | bytes, 14 callers, none inside | **differs**: ECX = dst, EDX = x; stack y,w,h,colour (from the prologue; rest inherited) |
| lookup | `0x4f5110` | `0x1123a0` (whole, `0x6a` B window) | CX = id; rows `0x21b410`/`0x21b418` (+0xc valid), named `0x1fb404` + fixed `0x21b40c`, table `0x1cb3f4` +8, lazy `0x1113b0` | window, 48 direct callers | differs in form (`mov dx,cx`; `+0xc` row test); same contract |
| alloc | `0x4f3950` (EAX bpp; cdecl w,h,flags) | `0x110c90` | CX = width, DX = height, stack bpp, flags; caller pops 8 (`0x07612e`); 0x64-byte object; `+0x10` flags, `+4/+6` dims, `+0xc` bytes/pixel; bpp 32 adds `0x2000` (cmov chain `0x110cc0..0x110d1b`) | anchors, windows | **differs** (register ABI) |
| free | `0x4f38d0` (cdecl object**) | `0x110c10` | ECX = object**; `dec [eax+0x60]` refcount | anchors | **differs** (ECX) |
| plain copy | `0x4dbbe0` | `0x0f4590` | cdecl src,dst,sx,sy,dx,dy,w,h -> leaf wrapper `0x10bc10(ECX=src, EDX=dst, 6 words)` | anchors, window | verified |
| colour / alpha | leaves `0x4ee990` / `0x4efa70` (EAX dst, EDI src) | wrappers `0x10bd20` / `0x10cde0` | ECX = src, EDX = dst, 7 caller-cleaned words; pair lock `0x0f5b70` first | anchors | **differs**: call the wrappers as the native blits do |
| copy class / slots | `0x4ee6e0`, `0x4ee934`, `0x4ee91c`, `0x4ee916` | classifier `0x10ba70`; class table `0x10bcfc` (25 bytes, identical to AP), slot table `0x10bce4` (6 relocated entries), nothing `0x10bcde` | byte compare, fixups | verified; same-depth classes 0/5/10/24, so the image-shadow predicate is unchanged |
| alloc depth block | `0x4f39b6` | `0x110cb7..0x110d21` | cmov form | window `alloc_fields` | differs in form only |
| object texture / surface | `+0x34` / `+0x30` | same | store `0x0f59c7`; clear `0x0f5a64`; Release `call [eax+8]` `0x0f5a75` | anchors | verified |
| config flag test anchor | `0x472315` | not mapped | `a1 b0 83 5a 00 f7 40 04 00 01 00 00` occurs 4x (`0x08bae6`, `0x08bc01`, `0x08bcce`, `0x095693`) | byte search | diagnostic slot anchor only; any occurrence or the constructor store serves |

### Data items and order proof

| Item | AP | FL RVA | How confirmed | Differences |
| --- | --- | --- | --- | --- |
| config slot | `0x606f34` | `0x1a83b0` | constructor store `0x109e4d`, init store `0x076c9e`, 943 HIGHLOW-fixed references (measured) | verified |
| density field | `cfg+0x784` | **`cfg+0x774`** | default `0x109ed5` `mov dword [edi+0x774],1` (10 B imm store), atol `0x10a371` `mov [ecx+0x774],eax` after `-fontscale` (`0x10a34b`, string `0x190f0c`) | writer census over `.text`: exactly these two writers |
| config object | ctor `0x4ec9e0` | ctor `0x109e00` (0x7a0 bytes), sole caller `0x076c96` | anchors | verified |
| Materials rows | `0x608dac`/`0x608db0`, stride 0x3c | count `0x21b410` (i16, `0x111754`), rows `0x21b418` (`0x111780`), stride 0x3c (`*15*4`); `+0xc` id, `+0x10` flags, `+0x14/+0x16` w/h (`0x1118fb..0x1119dd`) | anchors | verified |
| texture table | `0x6069ac`, `+4`/`+8` | `0x1cb3f4` (`0x111cf5`, capacity rows+1000), entry `+4` flags (`0x1122b5`: row flags `|` old `& 0x20000000`), `+8` object | anchors | verified |
| named / fixed count | `0x6069b4` / `0x6069b0` | `0x1fb404` (zeroed `0x111cca`, `++` `0x1121ae`) / `0x21b40c` (= rows, `0x111cd1`) | anchors | verified |
| `F\%s` path | descriptor | config `+0xc8` = `0x1a5ca0` (`0x109e95`); `+0x3c` dword `0x1a5cdc` -> `0x18b104` `F\%s`; loader `0x114a91..0x114aa7`, `tga bmp` `0x1918c0`, `abc siz` `0x1918c8` | anchors, fixups | verified; loose-first resolver exit `0x1053e6` byte-matches the inherited claim (semantics from [font-admission](font-admission.md), not re-traced) |
| order | ctor `0x40283a` < D3DCreate9 `0x402edc` < Materials `0x403497` < first font `0x403a26` | init routine `0x076bc0..0x078687` (sole caller `0x076b68`): ctor `0x076c96` < `Direct3DCreate9` helper `0x0772bc` (`0x0f13cd` IAT `0x150320`) < display `0x07765a` (-> `0x1168c5` -> `0x0f3e0a` -> CreateDevice `0x0f2489` `call [ecx+0x40]`), return `0x07765f` < Materials `0x0777db` < game start `0x077c52` (native opens `0x0798a5 -> 0x01df90`, `0x0798bf -> 0x020d30 -> 0x020dd0`) < cockpit `0x077e21` | linear decode order + exact caller sets (static; not a control-flow proof) | **differs**: CreateDevice always precedes the Materials load, so the FL port takes AP's "rows applied in the Materials thunk" branch; every native font open is below `0x077c52`/`0x077e21` |
| display / Reset return | proxy CreateDevice / Reset hooks | display return `0x07765f` (container stored `0x077662` -> `0x1a82f8`) | anchors | no engine site needed for Reset (proxy) |

Hook-site summary: every detour span is whole instructions with no relative control flow, no HIGHLOW fixup, no direct
edge and no relocated pointer into its interior. Qword-contained five-byte writes: font entry, both blits, text line,
rect fill, `0x0a69fb`. Crossing: `0x0a69f6`, `0x0b22f4` (plain write only while no thread can execute them: the
backend-load window inside `Direct3DCreate9` at `0x0772bc`, before the KC VM and the Materials load run; inferred from
the init order). The port installs `0x0b22f4` under that rule (run-time witness: the VM slot `0x1aa174` and the row count
`0x21b410` still zero) and never writes it back once the witness lapses: a rollback or shutdown after the VM exists
(already at `CreateDevice`, `0x077561` < `0x07765a`) leaves the claim in place, inert (the thunk goes straight to the tail
while the module is inactive; `image_claim=retained_inert`). Edits (`0x1151b8`, the four cockpit bytes) are single bytes, atomic, and no edge or pointer reaches the
edited instructions. Reentrancy: all sites run on the init/main thread (as AP); not re-proved here.

### Font assets on FL

FL requests (measured, `font_material_census.py` over the FL mounts, `addon2/04.cat`): `types/Fonts` LARGE
`Zekton 26/8`, `-L007 Harrier 24/5`, `-L034 ZektonES 26/0`, **`-L081 UmePlusCLP 23/0`**, HUD `Tahoma 13/1`
(**`13/2` under `-L081`**); the executable's only font literal is `Tahoma` (`0x184fdc`, used by `0x01dfdb` and
`0x020dcb`, both 13/16/1/`0x21`). The 12 stock `f/` members are byte-identical to clean AP (decoded SHA-256), so
the AP generated set `assets/fonts/generated/F` (Tahoma26/39, Zekton52/78, ZektonES52/78, Harrier48/72, `.abc` +
`.tga`, 16 files) covers every FL request except UmePlusCLP (46/69), and FL's catalogues hold no UmePlusCLP font at
any size (the stock `-L081` LARGE font is missing at 1x too). `C:\X3FL\f` does not exist (measured), and
`tools/manage.py install` is AP-only (records `X3AP.exe`), so an FL install must copy the 16 files into
`C:\X3FL\f` with their manifest. Materials (FL, `addon2/04.cat`): 1239 rows, 36 GENERATED|WRITEABLE (AP clean 34; new
rows 1237 512x64, 1238 512x1024), 10 of them already FONTSCALE, 29 NOFILTERING; the same 8 file-backed FONTSCALE
rows (46, 47, 56, 158, 219, 327, 568, 705); largest 1024 wide, 2048 high (row 8): the generic `plan_row` applies
unchanged.

### FL-only items for the implementer

1. Register ABIs of the font entry, blits, image block, diagnostics and helpers differ (tables above); AP frame
   indices (`font_arg_*`, `BlitArg`, `image_frame_dst`, `frame_ebx`) do not carry over.
2. The cockpit inline opener needs the four literal bytes, applied/restored with the config field (the name cache
   keys on the formatted name, so the first opener decides).
3. The style edit is not inert at N=1: order it after the field and restore it first.
4. `-L081`: the four-family font gate passes while UmePlusCLP{23d} is missing, and HUD Tahoma asks y offset 2:
   refuse density under `-L081` (or add a verified UmePlusCLP set and a language-aware Tahoma policy).
5. `auto` resolves from `ui_scale::scale()`; `ui_scale` is on FL since `69be37f27` (`src/engine/fl/ui_scale_fl.cpp`), so
   `auto` gives `d = ceil(s)` as on AP.
6. Graph strokes: `TexLine` multiplies endpoints by N into a flagged row (`0x0a7795`, `0x0a779d..0x0a77ab`) and draws
   one-texel lines; AP's engine does the same and the AP module neither touches nor depends on it. FL's chart class
   2327 (rows 20/1236, [ui-graphs](ui-graphs.md)) makes it visible: a visual acceptance item, not a port blocker.

### Learned while porting (2026-11-03, static, read-only)

Facts the port (`src/engine/fl/text_density_fl.cpp`, `text_density_sites_fl.h`) rests on beyond the tables above. Each
was read from `X3FL.exe` with Capstone and is pinned by one of the module's eight extra identity windows (30 in all;
`verification/results/fl-text-density/emit_header_windows.py --check`: header = executable).

| Item | FL RVA | How confirmed | Consequence |
| --- | --- | --- | --- |
| blit recorder | `0x0a7e3a` / `0x0a828a` `mov eax,[0x5aa100]; test byte [eax+0x24],1` -> `call 0x08ed70` / `0x08ee30` (writes `RTBB` / `RTBA` chunks through `[*0x1aa100+0xa0]`) | disassembly; windows `blt_block_clip`, `blt_alpha_clip` | the FL form of AP's blit trace (`[0x00608518]+0x24 & 1`, which AP's shadowed blits skip): on FL a blit the module would draw itself is left to the engine while bit 0 is set (1x source at the scaled position), so a recording stays complete |
| destination clip | `0x0a7d88..0x0a7e2e` (block), `0x0a81d8..0x0a827e` (alpha): clip word `[esp+0x64]`, W/N and H/N from the destination object | disassembly, same two windows | identical to AP's `clip_destination` (incl. the left/top overhang quirk); reused unchanged |
| alloc stack order | `0x110c90`: `[esp+4]` = bpp (`movzx [esp+0x20]` after the prologue), `[esp+8]` = flags (`[esp+0x24]`); caller `0x076128` pushes flags then bpp | disassembly | the shim pushes flags, bpp; CX/DX = width/height |
| plain copy | `0x0f4590` keeps ECX (`push ecx` .. `pop ecx`) and calls `0x10bc10(ECX=src, EDX=dst, 6 words)` | window `plain_copy` | called as cdecl |
| generated surface flags | `0x1114a1` `mov edi,0x402c` for a row with `0x60100`; `| 0x4000000` when `0x0db710` returns non-zero, `| 0x20000000` for entry bit `0x200000` | window `creator_flags` | the shadow uses AP's `(32 bpp, 0x402c)` |
| image case | `0x0b21f8..0x0b22c0`: path `*0x1aa0a4` (a heap copy), object `*0x1aa070`, `[ebp-0x50]` = image, `[ebp-0x20]` = `lookup(dst)`, both null-checked at `0x0b22ab..0x0b22ba`; the block pushes each product as `movzx eax,cx` (16-bit) | window `image_case` | AP's record words (uint16) and plan carry over |
| KC VM slot | `0x1aa174` written only at `0x077561` and `0x077681` (init routine, after `Direct3DCreate9` at `0x0772bc`) | store census over `.text` (`a3 74 a1 5a 00` / `mov [..],r`) | with the row count it is the run-time order witness for the qword-crossing image write |
| row count | `0x21b410` written only at `0x111755` (Materials loader) | same census | zero before the load (`.bss`: `.data` raw size `0x5600` from `0x1a2000`) |
| language | `cfg+0x75c` default 44 at `0x109ec4`; final before display `0x07765a` ([font-selector-readiness](font-selector-readiness.md)) | window `config_language` | read at `CreateDevice` for the `-L081` rule |
| Materials seam | `0x0a69fb` `call 0x1125c0` (the call after the load), qword-contained | window `materials` | claimed in place of `0x0a69f6`: the row edit runs right after the load, the callee then runs with the game's stack |

Reproduce: `python3 verification/probe/verify_text_density_sites_fl.py [--json|--emit-windows]`;
`python3 verification/results/fl-text-density/mutation_check.py`;
`python3 verification/results/fl-text-density/font_material_census.py`.
