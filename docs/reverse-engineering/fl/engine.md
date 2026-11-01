# Farnham's Legacy engine feasibility

Offline audit, 2026-10-27, against clean `/mnt/c/X3AP-orig/X3AP.exe` and
`/mnt/c/X3FL-orig/X3FL.exe`. The port baseline is mod release 1.7.3; executable
version numbers below belong to the games. No game, Wine, installation or
production modification was used. Addresses below are **preferred VAs**, not
runtime addresses. Measurements are static unless explicitly marked inferred.
This is an initial mapping and ABI audit, **not a qualified FL hook profile**.

Owning integration design: [FL port](../../architecture/farnhams-legacy-port.md).
AP contracts: [UI scale](../../architecture/ui-scale.md),
[text density](../../architecture/text-density.md), [GUI RE](../gui-scale.md),
[font RE](../font-rendering.md).

## Executable identity and relocation

| Measured property | AP | FL |
| --- | --- | --- |
| File version / product version | 3.3.0.0 | 1.3.0.0 |
| Bytes | 2,153,984 | 2,804,736 |
| Machine / optional-header kind | i386 / PE32 | i386 / PE32 |
| Preferred ImageBase | `0x00400000` | `0x00400000` |
| SizeOfImage | `0x2f5000` | `0x391000` |
| Entry VA | `0x00512ead` | `0x0051ea9d` |
| Link timestamp | `0x5a1d70ad` | `0x618416c8` |
| COFF characteristics | `0x123` | `0x122` |
| LAA / relocations stripped | yes / yes | yes / no |
| DLL characteristics | `0` | `0x8140` (DYNAMIC_BASE, NX_COMPAT, TERMINAL_SERVER_AWARE) |
| Sections / import DLLs / import entries | 4 / 19 / 238 | 8 / 19 / 241 |
| D3D / D3DX import | `d3d9.dll` / `d3dx9_37.dll` | `d3d9.dll` / `d3dx9_43.dll` |

SHA-256:

- AP: `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`.
- FL: `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.

FL adds `.gfids`, `.tls`, `_RDATA`, `.reloc` to the four AP section names.
Its relocation directory has **38,327 non-padding entries, all HIGHLOW (type 3)**.
Ten selected absolute operands/table entries have matching relocation entries:
`0x48289a` (dispatcher registration), `0x4828d2` (name-table registration),
`0x4afc44` (jump-table operand), `0x50a36a` (config pointer), `0x4b23c7`
(display pointer), `0x4b23d7` (VM pointer), `0x4b229b` (cached image pointer),
`0x4de767` (projection pointer), `0x4b2b3c` (OpenFont case address), and
`0x5a6038` (OpenFont name address). Thus even on-disk expected bytes containing
these absolute values must account for the loaded-image delta. Actual runtime
base and OS mitigation behavior have **not** been measured.

`src/engine/ap/executable_identity.h` reads headers at fixed `0x400000`, requires the
AP four-section layout, size and timestamp, and compares fixed-address anchors.
Feature site headers and inline assembly also embed AP globals and addresses.
`engine_patch.cpp` itself accepts supplied absolute pointers and compares raw
bytes; it does not discover/rebase an executable or normalize relocations.
Its aligned-qword write choice depends on the **actual** site/span. A future
profile therefore needs loaded-module base + per-edition RVAs, relocation-aware
expected windows, separate layouts/ABI adapters, and per-site admission. Simply
relaxing identity or adding one delta is unsafe. Preserve AP's existing gate.

## Native table anchors and measured correspondences

The FL registration at `0x482897` installs dispatcher `0x4afbf0`; its paired
name-table store is `0x4828ce` -> `0x5a5e78`. The dispatcher checks opcode <=
`0xa9` at `0x4afc35`, then indexes jump table `0x4b297c` at `0x4afc41`.
AP uses `0x493b40`, names `0x57a420`, jump table `0x49679c`, maximum `0xa7`.
The tables contain 168/170 names (144/146 beginning `B3D_`). All AP names remain;
FL inserts `B3D_InstSetQualityModifierRecursive` and `B3D_InstGetQualityModifier`
at opcodes 38/39. The texture/font/screen opcodes consequently increase by two.

Each FL call below is checked as a decoded direct call to the listed target;
native names and case addresses supply the semantic anchor. Projection has the
separate branch/layout/caller evidence below. These are function correspondences,
not approved patch addresses.

| Role | AP function | FL function | FL witnessed call | Delta |
| --- | --- | --- | --- | --- |
| Instance projection | `0x4bdee0` | `0x4de570` | `0x496fb9` | `+0x20690` |
| Font open | `0x48cdc0` | `0x4a8990` | `0x4b2394` | `+0x1bbd0` |
| Rect fill | `0x48b0b0` | `0x4a6b60` | `0x4b1ea6` | `+0x1bab0` |
| Line | `0x48bbc0` | `0x4a7660` | `0x4b20ae` | `+0x1baa0` |
| Circle | `0x48bd30` | `0x4a7810` | `0x4b24f5` | `+0x1bae0` |
| Filled circle | `0x48bee0` | `0x4a79a0` | `0x4b2525` | `+0x1bac0` |
| Block blit | `0x48c090` | `0x4a7b30` | `0x4b20f5` | `+0x1baa0` |
| Alpha block blit | `0x48c460` | `0x4a7f80` | `0x4b213c` | `+0x1bb20` |
| Texture lookup | `0x4f5110` | `0x5123a0` | `0x4b22a6` | `+0x1d290` |
| Plain copy wrapper | `0x4dbbe0` | `0x4f4590` | `0x4b234e` | `+0x189b0` |

The differing deltas already disprove a single shift. A supplementary conservative
comparison of 53 existing AP qualification windows and 29 function-range records
(including duplicates) found no matching complete function or informative window
after masking only branch targets and image addresses. It preserves registers,
structure offsets, instruction lengths and constants; windows with fewer than 12
fixed bytes are not candidates, and partial-instruction windows are skipped.
This negative signature result is not proof that a semantic equivalent is absent.

## UI projection, screen sizes and map callback

FL `0x4de570` has ECX = object (`+0x3c` checked at `0x4de577`), EDX = instance,
and its first stack argument = camera. It creates an EBP frame and aligns ESP to
8. At `0x4de581`, EDI takes the instance; EBX holds the camera; node flags remain
at `instance+0x130`, with `0x200` tested at `0x4de5af`. Camera viewport fields
`+0x288/+0x28c/+0x290/+0x294`, instance position `+0x30/+0x34`, and anchor bits
`0x1000/0x2000/0x4000/0x8000` retain the AP meaning. The FL body computes the
same pixel orthographic terms with SSE conversions/divisions and anchor adds.
At `0x4de765`, ESI loads P from `*0x5aae4c`; P[0], P[5], P[12], P[13] writes
are at `0x4de7fd`, `0x4de83f`, `0x4de880`, `0x4de8ec` (anchor updates follow).

The five-byte `mov eax,1` at `0x4de91b` is on an instruction boundary, but FL
still zeroes/stores other P elements afterwards and exits at `0x4de96c`.
At this point ESI=P, EDI=instance, EBX=camera, EBP=frame: **none of AP's
EAX=P / EBX=flags / EBP=camera assumptions can be carried over**. Both observed
callers (`0x496fb9`, `0x4976a0`) reload ECX/EDX and execute `add esp,8` before
testing the next callee's result. That establishes flag deadness along these two
returns, not complete hook qualification. FL uses XMM0..5 in this routine;
AP's no-XMM census cannot justify FL scratch choices. Inbound-edge/indirect
entry analysis, full caller SIMD liveness and other-thread/lifetime evidence
remain required before selecting a patch span. No exception registration appears
in this function prefix, but its callee/unwind behavior has not been audited.

FL ScreenGetWidth/Height opcodes 115/116 enter `0x4b23c6`/`0x4b23eb`.
Display global is `0x5a82f8`, VM global `0x5aa174`; width/height remain signed
words at `display-record+4/+6`. Unlike AP, the VM is already in ECX **before**
the dimension loads at `0x4b23db`/`0x4b2400`; the following return helper
`0x4c1940` consumes ECX. AP's width/height scratch-register rules do not apply.

`WriteGalaxyMapSectorName` uniquely anchors FL call `0x420179` -> `0x4bb500`
(string `0x585150`, VM/context cell `0x5a82e8`). At `0x420158` and `0x42015d`,
SSE `cvttsd2si` converts projected coordinates and each value is pushed directly.
AP's `ftol`/ECX-load seam at `0x41eab2` is absent at this corresponding callback.
The coordinate meaning and complete map pick/label pairing require follow-up;
this pass does not qualify the FL map input sites or cockpit exclusion walk.

## Text density, graph primitives and lifetime

`-fontscale` at `0x590f0c` anchors parse `0x50a34b..0x50a377`: the integer
result is stored at `[config+0x774]` at `0x50a371`, using global `0x5a83b0`.
Default 1 is stored at `0x509ed5`; config allocation is 0x7a0 bytes at
`0x509e3c..0x509e4d`. AP's field is `+0x784`; the image loader's config flag
field also moves from `+0xe4` to `+0xd4` (`0x4b222f`). Initialization order
relative to D3D creation, Materials and first font open is not established for FL.

FL font open `0x4a8990` receives **ECX=name, EDX=size**, with three caller-cleaned
stack arguments (`0x4b237d..0x4b239f`). AP used EAX=name, ECX=size. At FL entry,
the first seven bytes are whole instructions (three pushes and a stack load),
but no inbound scan has approved overwriting them. Returns at `0x4a89ff` and
`0x4a8a07` use plain `ret`; a failed open still returns -1. The downstream font
opener is `0x515190`; its style multiplier is `1 + (N>1)`, produced by
`cmp`/`setg bl`/`lea edx,[ebx+1]` at `0x51519b..0x5151b6`, with flag 1/4
records forced to factor 1. AP's patchable `mov edi,2` immediate is absent.

There are **no new drawing primitive names in the B3D name table**, and no
graph/chart-specific B3D name was found. FL's graph-facing existing primitives
are rect, line, circle and filled circle (opcodes 95, 106, 122, 123), plus text
and block/image blits. This does not exclude unnamed native paths or new script
graph composition; script callers and visible chart behavior remain to be checked.
Line `0x4a7660` receives ECX=destination id, EDX=x1, stack y1,x2,y2,colour
from dispatcher case `0x4b2099`. Its negative composite destination path
recursively calls itself at `0x4a76ff`. Circle/fill and blit recursion likewise
exists (`0x4a78b8`, `0x4a7a48`, `0x4a7cca`, `0x4a811a`). A global single-call
scratch frame would therefore be invalid even without multiple threads.

At `0x4a776f..0x4a77af`, line uses Materials count `0x61b410`, rows pointer
`0x61b418`, row stride `0x3c`, flags `+0x10`, `MPF_FONTSCALE=0x10000`, then
multiplies all four coordinates by config `+0x774`. These particular row fields
match AP. Texture lookup `0x5123a0` takes id in CX, reads the same row count/rows,
then lazily creates a missing texture via `0x5113b0`; table pointer `0x5cb3f4`
has stride `0x10`, object field `+8`. The full surface object layout, D3D wrapper
offsets, ownership/destruction and Reset behavior are **not verified**.

`TexBltImage` is now opcode 111, case `0x4b21ed`, with cache object/path globals
`0x5aa070` / `0x5aa0a4`. The replacement path frees the old object via
`0x510c10` at `0x4b2225` before loading a new one via `0x510890` at `0x4b227a`;
pointer reuse must not be treated as stable image identity. At
`0x4b22c0..0x4b22f4` EDX becomes 1 or N according to destination row flags.
The coordinate block `0x4b22f4..0x4b234e` multiplies **all six source/destination
rectangle words** by this factor and narrows products to 16 bits before calling
plain copy `0x4f4590`. The source-scaling hazard that motivated AP's guard thus
persists structurally; an actual FL crash/overread has not been demonstrated.
Here ESI=argument record, EDX=factor, image/destination live in EBP locals
`-0x50/-0x20`, unlike AP's EBX/ESP contract.

The dispatcher establishes an FS:[0] SEH chain and dynamically aligns its frame
at entry; normal epilogue restores FS:[0], EBP and the original ESP through EBX
at `0x4b2962..0x4b2979`. Any skipped argument block must preserve that frame and
the caller's cleanup. Its exception handler itself is not qualified. No FL
claim yet establishes LastError preservation, SEH unwinding through injected
code, install timing, thread confinement, rollback, Reset or recovery. These
remain port requirements, not assumptions inherited from AP.

## Resource-root evidence and limits

The FL path descriptor at `0x5a5ca0` is placed in config `+0xc8` at `0x509e95`.
Its entries `+0x34`, `+0x38`, `+0x40` point to `%02d.cat` (`0x58b0e8`),
`addon2\%02d.cat` (`0x58b0f4`), and `addon2\mods\%s.cat` (`0x58b10c`).
The same executable contains `addon2\types\...` and `addon2\T\...` paths.
This establishes root and addon2 path candidates; it does **not** prove mount
order, loose-file precedence, highest-catalog wins, or any dependency on AP's
`addon` tree. The enumeration/open/lookup callers need a focused follow-up.

## Reproduce and acceptance for this audit

```sh
python3 verification/probe/compare_fl_engine.py
python3 -m py_compile verification/probe/compare_fl_engine.py
i686-w64-mingw32-objdump -d -Mintel /mnt/c/X3FL-orig/X3FL.exe > verification/results/fl-port/engine/fl-disassembly.txt
```

The comparison imports the project PE reader and AP site-verifier tables, and
uses Capstone 5.0.7. `comparison.json` records metadata and signature results;
`anchors.json` records both native tables, ten checked call targets, relocation
witnesses and path-table strings. `targeted-disassembly.txt` and the full
objdump output are local, untracked raw evidence. The producing command wrapper
is `verification/results/fl-port/engine/reproduce.sh`. JSON was parsed and
asserted for 168/170 table rows, ten mappings and ten type-3 relocation witnesses;
the comparison and Python compilation passed. No runtime safety or rendering
acceptance follows from these checks. No full suite is needed for this offline
probe/documentation checkpoint.
