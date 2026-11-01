# FL graphs, texture density and UI coordinates

Offline study, 2026-10-27. Owning integration decision:
[FL port](../../architecture/farnhams-legacy-port.md). This extends the
[engine anchors](engine.md), rather than qualifying an FL patch
profile. AP references: [UI scale](../../architecture/ui-scale.md),
[text density](../../architecture/text-density.md), [font rendering](../font-rendering.md).
No production edits, game/Wine execution, installation or DLL build occurred.
`s` below is projection/UI scale; `N` is integer texture/font density. They are
different transformations. All executable addresses are **preferred VAs**.
KC addresses are **file CODE offsets**, not executable VAs or post-peephole VM PCs.

## Result and evidence boundary

**Established statically:** FL charts are KC class **2327**, derived from the
ordinary menu class 2200. The renderer uses generated texture **1236** for its
plot and **20** for its menu/legend; the plot body uses `gui2d.fx`. Class 2327
explicitly marks both instances with `0x200`, making them eligible for the
native pixel-orthographic branch. The chart's size queries and menu interaction
also use the ordinary virtual-coordinate paths. This establishes a concrete
chart path; absence of new B3D names is no longer the evidence for coverage.

**A separate density gap:** `TexLine` scales endpoints by `N`, then draws a
one-texel-wide software line. It has no stroke-width argument. Applying the
current generated/writeable row policy to texture 1236 would therefore change
line footprint relative to its text/rectangles. This is a static prediction,
**not an observed visual defect**. Projection-only scaling at `N=1` does not
introduce this density-specific issue. Graph stroke quality, clipping at the
plot boundary, sampler behavior and interaction still require targeted acceptance.

Reproducible probe: [verify_fl_ui_graphs.py](../../../verification/probe/verify_fl_ui_graphs.py).
It uses the project's CAT/PCK and PE readers and Capstone; its compact KC reader
walks complete method bodies, including switch arms. It does **not** trust the
old symbolic stack printer for FL: that printer omits string-push/OR and switch
semantics, so its apparent `GetBodyID(1)`/`GetBodyID(3)` output is wrong here.
Actual operands are the body-path strings below; 1/3 are the separate `AddInst`
arguments. Small sequences and direct calls were checked against file CODE.

Measured input identities:

| Input | Source | SHA-256 |
| --- | --- | --- |
| FL EXE | `/mnt/c/X3FL-orig/X3FL.exe` | `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8` |
| FL story, build `1636047691` | `addon2/03.cat`, `addon2/L/x3story.obj` | `dbb732716e1acab6b8cc7f87f5d31324fb4764dfc22d1cec808749a4f45ec7b9` |
| Materials, 1239 rows | `addon2/04.cat`, `addon2/types/Materials.pck` | `a6b01213328edd06e9548688d37231e9a7ba84e6653a49ce0c871d8ea46afb40` |
| plot body | `addon2/01.cat`, `objects/effects/x3ap/menugfx/graph.pbd` | `84898f909d85328f1d5ceaeef713255717113adf06a2b3f3f5881437bf18e7c3` |
| legend body | same CAT, `objects/effects/x3ap/menugfx/menu_sector.pbd` | `94269d50fda57f9c554180294742b030e0dba7272893271ea4f9309b0ead8b0b` |

The probe audits these fixed clean-media members and loose scripts, not arbitrary
mod resolution. Their provenance is separate from the qualified general resolver.

## 1. Script to chart to pixels

The loose scripts under `addon2/scripts` provide both readable source and compiled
`codearray` records. `graph.data.collector.station` registers callbacks including
`graph.draw.networth.station`, `graph.draw.station.profit` and
`graph.draw.station.wares`. `graph.draw.networth` and `graph.draw.station.wares`
call `!lib.graph.build.series`, which calls `!lib.graph.build.axis` and
`!lib.graph.build.data`. Compiled string references independently witness the
named dependencies. The scripts return arrays; they do not call B3D drawing
primitives themselves. The six-element series result carries data, labels,
X-axis description, Y-axis description, series names and colours (indices 0..5);
the draw scripts fill the label bundle. Additional option/event responses can
use longer arrays. The probe records script hashes without exporting their text.

The executable-independent KC path is concrete:

| KC owner/method | CODE witness | Established operation |
| --- | --- | --- |
| 503 `OpenGraphsMenu` | `0x285ae`; call `0x285e4` | opens generic menu class 2330 unless the custom-menu-script branch is used |
| 702 `__runScript` | calls `0x750a6`, `0x754e2`, `0x7599b` | registration paths reach 2330 `AddGraph` (`0x23a68c`) |
| 2330 `MenuAction` | `0x23aff4`; `0x23b1ae` | starts the selected script through 700 `StartScript`, carrying menu callback context |
| 2330 `MenuCallback` | `0x23aeed`; `0x23af50`, `0x23af62` | converts returned script array, instantiates class 2327, invokes `CreateNextWithScript`; class operand at `0x23af5e`, new opcode at `0x23af61` |
| 2327 `CreateNextWithScript` | `0x23b273` | stores callback script in member 159, calls `CreateNext` at `0x23b28b` |
| 2327 `CreateNext` | `0x23b232` | clones graph data into member 101 at `0x23b25f`; calls `BuildGraphData` at `0x23b264` |
| 2327 `BuildGraphData` | `0x23b3ae` | builds axis/series pixel positions from data, text measurements and available plot dimensions; fixed-point `SE_MulDivFix` calls include `0x23cf43`, `0x23d06b` |
| 2327 `SpecialUpdate` | `0x23d86d` | draws the plot and normal menu legend/options |
| 2327 `MenuAction`/`SwitchOption` | `0x23e332`/`0x23e4a7` | `RunNamedScript` at `0x23e3a2`/`0x23e582`, conversion of response, graph-data rebuild and menu update |

This is static control/data reachability, not proof every registration is visible
in every save or that every callback executes successfully at runtime.

`SpecialUpdate` contains **17 `B3D_TexLine`**, **7 `B3D_TexRectFill`** and
**4 `DrawRawTextMat`** call sites (counts are static, not calls per frame).
Axes/ticks/grid lines start at `0x23db33`; connected series segments reach
`0x23e2a0`; crossed point markers use `0x23e212`/`0x23e24e`. Bar modes use
rectangle fills. Plot draws have destination 1236. No direct block/image blit or
circle call exists in this method; this negative check does **not** exclude
blits in base-menu helpers, other chart classes or unrelated UI.

`2200::DrawRawTextMat` at `0x18e8a0` calls `B3D_TexTextBlock` at `0x18e8bb`
with destination, text, global font slot `g0`, x/y, width 0, colour and flags
129. Plot label calls pass destination 1236. Thus labels and line endpoints are
written into the **same generated texture**, not independent screen overlays.

## 2. Texture and projection contracts

`SetupTexture` (`0x23b1de`) sets menu members 68/69 to 384/1024 and member 70
to texture 20. `SpecialMenu` (`0x23d48f`) resolves
`effects/x3ap/menugfx/menu_sector` at `0x23d499` and
`effects/x3ap/menugfx/graph` at `0x23d500`, adding them through base-menu
`AddInst` (`0x18d13e`). That helper sets the body and adds each instance to the
menu scene (native call sites `0x18d203`/`0x18d214`, or its alternate allocation
branch). Members 99/100 retain the instances.

Both instance flag sequences (`0x23d4cd..0x23d4da`,
`0x23d53c..0x23d549`) OR **`0x200 | 0x10000 | 0x4000`** into `Flags2`;
setters are at `0x23d4df`/`0x23d54e`. `0x200` selects pixel orthographic
projection and `0x4000` is the established top-anchor bit. This is direct KC
evidence, rather than an inference from a class name. The bodies have two
triangles and `gui2d.fx`; the plot samples `1236.jpg` across UV 0..1, while the
legend samples `20.jpg` over U 0..0.375 and V 0..1.

Both Materials rows are generated, writeable, alpha-blended, texture-alpha,
NOFILTERING **1024×1024** surfaces; neither ships with FONTSCALE. At `N=1`,
layout pixels are texels. The plot is composed directly in that surface; it has
no external chart image that needs a source rectangle scaled separately.

`GetMenuMaxWidth`/`GetMenuMaxHeight` (`0x23b2d6`/`0x23b342`) each compute
`clamp(min(ScreenGetWidth()-512, ScreenGetHeight()-128), 512, 1024)`.
They directly call both screen-size natives. `SpecialResize` (`0x23d821`)
repositions both instances and rebuilds the data. Substituting virtual dimensions
`round(W/s), round(H/s)` therefore changes the chart's own layout. A viewport
smaller than its 512 minimum cannot be assumed to fit; acceptance must include
the intended scale/resolution combinations.

The existing FL projection contract remains ECX=object, EDX=instance,
`[entry ESP+4]=camera`, plain `ret`. At candidate exit seam `0x4de91b`,
ESI=P, EDI=instance, EBX=camera, EBP=aligned frame. The continuation writes
only other matrix elements before popping registers; it does not rewrite
P[0], P[5], P[12], P[13]. An FL adapter can therefore apply the documented
anchor transform at this seam, loading flags from `[EDI+0x130]`. AP's
EAX=P/EBX=flags/EBP=camera register recipe is invalid. Preserve all live
registers and SIMD state unless their scratch use is separately qualified.

## 3. Labels, density, stroke width and clipping

**Current source policy, conditional on a future qualified FL adapter:**
`text_density_sites.h::plan_row` flags *every* GENERATED+WRITEABLE row;
`text_density.cpp::apply_rows` iterates all rows, not an AP ID whitelist.
Consequently FL rows 20 and 1236 satisfy the rule. A focused host invocation of
the real `plan_row` confirmed FONTSCALE is added for both minification states;
NOFILTERING is cleared only when `s != N`. `apply_rows` refuses newly flagged
rows whose texture objects already exist. Actual current production activation
on FL is not established by this policy test; the AP gate/address assumptions
remain. Font availability, density caps, initialization timing and table adapters
can also prevent activation. Plain `ui_scale` does not itself edit these rows.

FL generated allocation scales width/height by `N` at `0x511517..0x511532`
only when `N>1` and flags include `0x10000`. The following semantics are proven
from the selected native paths:

| Operation | FL evidence | Coordinate/source behavior |
| --- | --- | --- |
| plot line | dispatcher `0x4b20ae` → `0x4a7660` | ECX=dst, EDX=x1; caller-cleaned stack y1,x2,y2,colour. All four coordinates multiply by config `+0x774` only for flagged dst at `0x4a7786..0x4a77ab` |
| plot rect | `0x4a6b60`; `0x4a6d37..0x4a6d5c` | ECX=dst, EDX=x; stack y,width,height,colour. x/y/width/height multiply by `N` for flagged dst |
| text block | case `0x4b1ec1`, call `0x4b1ef2` → `0x4a6f80` | ECX=dst, EDX=string; stack font,x,y,width,colour,flags; caller pops 24. Width 0 derives available destination width; flagged surface width is divided by `N` at `0x4a7041` |
| wrapping/line pitch | `0x4a70c7`, `0x4a713b`; alternate path `0x4a7284`, `0x4a72ed` | wrap width multiplies by `N`; font/style line pitch divides by `N`, independent of destination flag |
| text line destination | call `0x4a7197` → `0x4a6d90`; `0x4a6f25..0x4a6f40` | x/y/extent multiply by `N` only for flagged dst; call `0x4a6f56` enters glyph renderer `0x515840` |
| text measurement | `0x4a7440`/`0x4a7470` | width/height divide by config `N` at `0x4a745e`/`0x4a74ab`, returning layout units to KC |
| glyph source | `0x515a6c..0x515aab`, `0x515b66..0x515b6e` | source x/y and widths come from opened font arrays at +0x44/+0x48/+0x2c; pitch uses requested font size +0xe minus 1 for glyph height. No destination-row-dependent multiplication of atlas source coordinates here |

Thus font-open adaptation must supply the matching larger atlas/metrics, size
and y-offset, as already required by AP. A correct `N` atlas and flagged graph
destination keep label positions and measured layout consistent. Merely setting
global `N` while leaving graph texture at 1x is **not** a simple fix for thin
lines: text measurement/wrapping/line pitch still use `N`, and the selected
larger glyphs would be copied into an unscaled destination. No full proof of
every FL font/language/style or atlas lifetime is claimed here.

Line clipping is separate from line thickness. `0x4a77d6` calls `0x50bfa0`
with pointers to the **already scaled** endpoints and the destination surface.
That clipper tests destination signed-word width/height (`0x50c05f`,
`0x50c065` etc.), rejects endpoint differences beyond `0x7fff`, and computes
boundary intersections. It is surface clipping, not a graph-specific plot
scissor. The subsequent `0x50bea0` dispatches to software format leaves;
the 32-bit branch calls `0x4d3b40` at `0x50bf55`. Its major-axis loops write
one pixel per step (e.g. `0x4d3ce6`, `0x4d3d83`, `0x4d3db9`), with no `N`
read or width parameter. Endpoint scaling therefore does not scale thickness.
The 8/16-bit leaves likewise store one pixel per step.

Inferred geometrically, a one-texel stroke in a density-N surface occupies
`1/N` layout pixel, hence nominal `s/N` display-pixel width after the body's
projection. Actual coverage, visibility and aliasing depend on sampling. The
body declares point filter values, while the mod has its own density/filter
policy; their final FL interaction has not been exercised. Do not claim the
visible result or a required particular fix from this footprint alone.

Rect wrapper `0x50be40` checks signed coordinates and width/height against the
surface and **rejects** rectangles outside it; it does not partially clip them.
Text glyph code has its own bounds handling at `0x515b7b..0x515c00` (including
source adjustment for clipped left/top). KC also explicitly clears unused plot
surface regions at `0x23e301`/`0x23e327`. These are distinct behaviors; a future
stroke replacement must preserve applicable endpoint clipping and avoid writing
past a surface or inventing a plot-area scissor. Acceptance must distinguish
axis/series footprint, label placement and partially visible primitives.

### Ordinary blits still needed by the surrounding menu

`0x4a7b30`/`0x4a7f80` take **ECX=source id, EDX=destination id** and eight
caller-cleaned stack words: sx,sy,dx,dy,width,height,colour,clip. This differs
from AP's ten stack-word ABI. A negative composite destination recurses through
the same entry (`0x4a7cca`/`0x4a811a`); use per-invocation state.

Plain block clipping (`0x4a7d24..0x4a7e26`) divides flagged destination
dimensions by `N` before clipping layout-space destination bounds, adjusting
source origin for negative destination coordinates. At `0x4a7e87..0x4a7f11`,
a flagged destination scales dx/dy; sx/sy/width/height scale only when the source
row is also flagged. An unflagged static atlas therefore still needs the
qualified shadow-source strategy to fill an N-density destination. Generated
1236-to-generated flagged targets already use matching density; no static
chart-image shadow is involved in the direct plot renderer. The separate image
blit hazard and cache replacement contract remain as documented in the engine
note. This study does not qualify its destruction/Reset hooks.

## 4. Native input and candidate spans

The KC cursor is class 502. Its `SetClipping` at `0x17db53` obtains screen
dimensions into members 10/11; `UpdateCursorSteering` passes member 0/1 as x/y
at native call `0x17d5b3`. Class 150 `InputMouseDelta` calls
`502::MoveCursorRelative` at `0x17b7e`. That method (`0x17db00`) adds its two
arguments to members 0/1 and invokes `UpdateCursor` at `0x17db1a`.
`UpdateCursor` (`0x17dec9`) clamps against members 8..11, sets cursor instance
position, updates the native steering cursor, and sends listeners the same
member 0/1 through `NotifyMouseMove` at `0x17dfaf` (subsequent suppressed
listeners receive -1,-1 at `0x17e00f`). Base-menu `NotifyMouseMove` at
`0x191092` combines instance position/anchor flags and ScreenGetWidth/Height
before testing menu-local coordinates. The native main-loop event callback is
named `MouseInput` (`0x58ba4c`); its complete dispatch to `InputMouseDelta`
is not reconstructed here. Graph `Input` (`0x23e5d0`) delegates ordinary inputs
to `2200::DefaultSelectInput` at `0x23e678`. The base menu's `NotifyClick` and
`NotifyMouseMove` query screen width/height and operate on cursor/menu positions.
Legend rows and option selection are menu interaction. No native graph-specific
screen-coordinate conversion was found on this traced path. This does not
prove all graph events, mouse mappings or every UI family are covered.

The following are **whole-instruction candidate seams**, not install-ready
claims. Sizes and boundaries are measured by the probe. No copied game bytes
are necessary in this note; local JSON records span hashes and instruction sizes.

| Purpose | Candidate `[start,end)` | Live contract and continuation |
| --- | --- | --- |
| projection | `0x4de91b..0x4de920` (5) | displaced `mov eax,1`; ESI=P, EDI=instance, EBX=camera, EBP=frame. No later write to four scale elements; preserve other state |
| virtual width | `0x4b23db..0x4b23e1` (6) | signed word `[EAX+4]` → EAX, push value, push EDI. ECX already holds VM and must survive; scale after load before pushing. Continue at helper call |
| virtual height | `0x4b2400..0x4b2406` (6) | same with `[EAX+6]`. Helper `0x4c1940` receives ECX=VM and stack return-record,value; `ret 8` |
| main delta X | `0x4780d1..0x4780d8` (7) | context EAX, zero-extended word `+0x414` → **ECX**. Next `test cx,cx` kills flags. Preserve EAX: zero-X path uses it to read Y |
| main delta Y | `0x478113..0x47811a` (7) | context EAX, zero-extended `+0x416` → EAX. Next `test ax,ax` kills flags; nonzero path `cwde` sign-extends |
| axis reader X | `0x4d1af1..0x4d1af8` (7) | context ESI, signed `+0x414` → EAX. Preserve ESI/ECX and existing stack; pushes feed `0x4cfa10` |
| axis reader Y | `0x4d1b10..0x4d1b17` (7) | context EAX, signed `+0x416` → EAX. Earlier outgoing arguments already on stack. `0x4cfa10` writes flags before reading them and returns `ret 0x14` |
| script cursor stores | `0x47bc4d..0x47bc5d` (16) | ECX=record, load x at +0xb into EAX/store `0x5aa180`, load y at +0x10/store `0x5aa188`. Scale both; AP's simultaneous EAX=y/EDX=x seam does not exist. Preserve record and dispatcher frame |
| overlay click | `0x4320c3..0x4320cf` (12) | ESI=record, EAX=cockpit; push y, ECX=cockpit+**0x3bc**, push x. Continue at call `0x42bae0` (`ret 8`). Preserve other registers; adjust outgoing values, not shared record |
| cursor aim | `0x4310c0..0x4310c6` (6) | ESI=record, loads x→EDX,y→EDI; ECX=argument count, EAX=cockpit. Next `cmp ecx,4` regenerates flags. Short-argument -1 sentinel path bypasses seam |
| map label | `0x420158..0x420162` (10) | `cvttsd2si EAX,XMM1; push EAX; cvttsd2si EAX,XMM4; push EAX`. Divide these resulting integer offsets, preserve live SIMD, stack and ESI/EDI/EBP. Continue with other callback arguments; call `0x4bb500` at `0x420179` |
| map pick | `0x431d01..0x431d08` (7) | ESI=record, EAX=scene; push y(+0xb),x(+6),scene; two output-pointer arguments already pushed. Scale x/y; continue at `0x41f6b0` (`ret 0x14`) |

For width/height, arithmetic flags are dead at the helper's first compare before
any read. ECX is **not** scratch as it was in one AP site. The main delta readers
send events `0x11d`/`0x11e` via `0x4bb5b0` at `0x478106`/`0x478146`.
Axis dispatcher `0x4cfa10` reads axis mappings and may accumulate into
`0x61c880`/`0x61c884`; the separate unscaled-reader block at
`0x4d2289`/`0x4d22b2` also accumulates those cells. It must not be silently
ignored or indiscriminately scaled: selection/control-mode reachability and
double-scaling exclusion for these reader branches remain open. The mapping is
more precise than labelling every read a menu delta.

`X2_UpdateCursorSteering` is opcode **30**, with name table `0x5a5d60`;
registration `0x4778d8`, adapter `0x47b540` → dispatcher `0x47b560`.
Its two cursor stores are separate from preceding mode stores at `0x47bc40`
and `0x47bc48`. The 16-byte combined candidate crosses the aligned qword for
the five-byte entry jump. A two-site design or validated non-atomic install
protocol is required; neither is approved here.

INS registration `0x41df2f` binds dispatcher `0x430540`; names `0x5a3d98`,
jump table `0x432488`. Opcodes 40/62/100 still identify aim/map-pick/overlay.
The overlay private subobject moves from AP +0x3ac to FL **+0x3bc**. Its hit
test reads real display dimensions at `0x42bb03..0x42bb37`, establishing the
virtual-to-real crossing. Map pick reads real dimensions at
`0x41f6f8` and its corresponding height path; its existence check moved into
the callee (`0x41f6bc`, global `0x5a82e0`). AP's flag-live compare replay is
therefore not part of this FL caller seam. Label conversion and pick conversion
must be paired, just as in AP; the different SSE callback seam needs a new adapter.

## 5. Hook safety, lifetime and remaining evidence

The probe decodes **15** candidate spans (the table plus font/block/alpha
entries), **14** named direct-call edges, and a conservative linear .text scan
of **428,814** decoded instructions. It finds **zero direct branches/calls into
span interiors**. This is a measured negative census, **not** proof against
indirect entries, tables embedded in code, alternate instruction decodes or
unobserved thread execution. The two absolute cursor-store operands have HIGHLOW
relocations at `0x47bc51`/`0x47bc59`. Loaded-image relocation handling remains
mandatory. The five-byte entry jump fits a single aligned qword at the other
listed seams, but that alone does not prove safe tail/padding writes for spans
longer than five bytes; engine_patch's actual write protocol must be reviewed.

Projection's two known callers (`0x496fb9`, `0x4976a0`) reload ECX/EDX and
adjust ESP before testing the next callee's result. The span and suffix show no
further SIMD reads inside the function. These facts do not justify freely
clobbering XMM registers across every possible caller; an adapter preserving
SIMD avoids inheriting AP's unsupported scratch-register assumption. Do not
overwrite the stack-alignment prologue or FS:[0] chain of the SEH dispatchers.
Any C/C++ callout needs the four-byte incoming-stack contract, LastError/DF,
CPU/SIMD/flags preservation, correct callee cleanup and a qualified exception
path. Inline arithmetic avoids a new callout but still needs register and
overflow/rounding validation. The AP accumulated delta and coordinate-rounding
policies can be reused mathematically, not as byte stubs.

Pointers at the seams are borrowed for the synchronous invocation. Do not retain
the script argument record, projection pointer, cursor context, image object or
surface pointer in a process-global scratch frame. Composite primitive recursion
is established. The callback path can invoke KC/script work and change menu
state; reentrancy cannot be ruled out from a normal frame trace. Thread
confinement, per-reader remainder reset, camera-exclusion registry lifetime,
Materials/font-open order, Reset/recreation, exception unwinding and rollback
are not newly qualified by this static study. The existing image-cache free/load
path also forbids pointer-only identity for a shadow cache.

Remaining bounded work before activation:

1. Resolve axis-reader mode reachability and the `0x4d2289`/`0x4d22b2` path;
   qualify cockpit camera exclusion and each install/rollback boundary.
2. Qualify FL font/style and texture-lifetime adapters. Keep label metrics,
   font-source density and destination scaling consistent. A 1x exception for
   graph rows alone is insufficient under global font density.
3. The later [density implementation](../../architecture/farnhams-legacy-text-density.md)
   supplies the ratified positive-square stroke footprint. Existing native raster
   checks cover 16 segments at densities 2 and 3, including clipping and reversed
   endpoints. Final sampling and perceived graph quality still need user acceptance.
   Chart rendering is script update work, not necessarily per-frame work; the
   static 17-call count is not a runtime performance estimate.
4. User-run chart acceptance at integer/fractional scale: axes/points/bars,
   text alignment, plot edges, options and legend toggles, resizing, native
   overlay/map picking, alt-tab cursor and Reset. Native Windows and CrossOver
   remain separate runtime targets. No game has been launched for this study.

## Reproduction

```sh
python3 verification/probe/verify_fl_ui_graphs.py --raw
python3 -m py_compile verification/probe/verify_fl_ui_graphs.py
sh verification/results/fl-port/ui-graphs/reproduce.sh
```

The last wrapper additionally compiles/runs the tiny local `row-policy.cpp`
against the actual production header. Probe, Python compilation and both row
policy cases passed. `summary.json` is parsed and asserted by the producer.
Raw decoded assets, KC/engine listings, the host executable and reproducer stay
under ignored `verification/results/fl-port/ui-graphs/`. Only this derived note
and the probe are intended source changes. No full suite or Wine test is needed
for this offline RE/documentation checkpoint.

Independent deep review reproduced the probe and policy checks and found no
blocking issue within this static scope. It confirmed the graph/label/stroke
distinction and the stated input, indirect-entry, lifecycle and runtime limits.
No UI capability is enabled by this evidence.

## Follow-up input qualification

The [reviewed input study](ui-input.md) corrects the candidate
input routing above. The sites labelled main delta feed legacy `MouseInput`;
the established cursor uses earlier direct `InputMouseDelta` and a separate
accumulated callback. Scale outgoing callback copies at `0x477fda` and
`0x4d22f8`, rather than the four raw-reader candidates, to cover the null-object
fallback without changing flight-axis controls. The study also establishes
map/overlay/aim conversions and concrete camera teardown hazards. These are
implementation contracts, not activated UI support; camera lifecycle and the
graph stroke policy remain separate requirements.
