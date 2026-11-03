# FL UI scale: sites, ABI and the port of the AP contract

The FL counterpart of every AP site of [ui-scale.md](../../architecture/ui-scale.md) ("Sites and contract"), read from
the bytes of `X3FL.exe` (SHA-256 `e7f3aad8…`, timestamp `0x618416c8`, ASLR: every address is module base + RVA).
Implementation: `src/engine/fl/ui_scale_fl.cpp`, `src/engine/fl/ui_scale_sites_fl.h`; verifier
[verify_ui_scale_sites_fl.py](../../../verification/probe/verify_ui_scale_sites_fl.py); fixture
`verification/probe/fl_ui_scale.cpp` with [test_fl_ui_scale.py](../../../verification/analysis/test_fl_ui_scale.py);
evidence [farnhams-legacy.md](../../verification/farnhams-legacy.md), "UI scale on FL, host evidence (2026-11-03)".
The inherited offline notes ([ui-input.md](ui-input.md), [ui-graphs.md](ui-graphs.md) §4,
[ui-camera-lifetime.md](ui-camera-lifetime.md)) were the leads; every address below was confirmed from the bytes.
VAs in the text are at the preferred base `0x400000` (RVA = VA − `0x400000`).

## Verified 2026-11-03 against X3FL.exe

Eleven claims, all or none, in this order (A last). Every first five bytes lie inside one aligned qword (one
`lock cmpxchg8b`). "Bypass": the stub performs the displaced instructions itself and continues at RVA + length.

| Site | AP VA | FL RVA | Bytes (length) | Anchor (how the case is identified) | Register facts |
| --- | --- | --- | --- | --- | --- |
| B width | `0x00496194` | `0x0b23db` | `0f bf 40 04 50` movsx eax,word [eax+4]; push eax (5, bypass) | B3D dispatcher `0x0afbf0` (registration `0x082897`), jump table `0x0b297c` entry `0x73` → `0x0b23c6`; names table `0x1a5e78`[`0x73`] = `B3D_ScreenGetWidth` | EAX = display record in, `round(W/s)` pushed; ECX = VM (loaded before the site, **not** scratch as on AP), EDI kept; EFLAGS dead (callee `0x0c1940`'s `cmp byte [edi+0x28],8`) |
| C height | `0x004961bb` | `0x0b2400` | `0f bf 40 06 50` (5, bypass) | entry `0x74` → `0x0b23eb`; `B3D_ScreenGetHeight` | as B with `[eax+6]` |
| D1 direct delta | `0x00403d36`, `0x00403d7a` (main loop) | `0x077fda` | `68 <base+0x18ba3c>` push "InputMouseDelta" (5, relocated imm, tail replays it) | loop `0x077ba0`: `movsx` of input context `+0x416`/`+0x414` pushed at `0x077fc6`/`0x077fce`, callback wrapper `0x0bb5b0` at `0x077fed` | at the site `[esp+8]` = dx, `[esp+12]` = dy (signed dwords); EAX, EDX, EFLAGS dead (pushes, then the wrapper writes all three first) |
| D2 mapped delta | `0x00411b40`, `0x00411b57` (flight input) | `0x0d22f8` | `68 <base+0x18ba3c>` (5, relocated imm) | input worker `0x0cfd00`: mapped sums `0x21c880`/`0x21c884` pushed at `0x0d22ee`/`0x0d22e8`, wrapper `0x0bb500` at `0x0d2306`, sums cleared after | as D1; the native sums are not touched |
| E1 cursor x | `0x004074ec` (one site, EAX = y, EDX = x) | `0x07bc50` | `a3 <base+0x1aa180>` mov [x],eax (5, relocated) | X2 dispatcher `0x07b560`: index byte `0x07bedc`[`0x1e`−3] = `0x18`, table `0x07be4c`[`0x18`] → `0x07bc3a`; names `0x1a5d60`[`0x1e`] = `X2_UpdateCursorSteering` (AP case `0x1f`) | ECX = records; x from `+0xb`; EAX × s before the store; EAX reloaded after; EFLAGS dead |
| E2 cursor y | (same) | `0x07bc58` | `a3 <base+0x1aa188>` (5, relocated) | y from `+0x10` | EAX dead after (`0x0c1940` sets it); EFLAGS dead |
| G click icon | `0x0042ece0` | `0x0320c3` | `ff 76 0b 8d 88 bc 03 00 00 ff 76 06` push [esi+0xb]; lea ecx,[eax+0x3bc]; push [esi+6] (12, bypass) | INS dispatcher `0x030540` (registration `0x01df2f`), table `0x032488`[`0x64`] → `0x0320aa`; `INS_CockpitGetObjectByTargetOverlayIconPos` | ESI = records, EAX = cockpit (resolver `0x01e420` at `0x0320b6`); stub pushes y·s, x·s and sets ECX = cockpit + **`0x3bc`** (AP `+0x3ac`); EDX untouched; EFLAGS dead (hit test `0x02bae0` starts `sub esp,0x14`) |
| H cursor aim | `0x0042ddf1` | `0x0310c0` | `8b 56 06 8b 7e 0b` mov edx,[esi+6]; mov edi,[esi+0xb] (6, bypass) | table[`0x28`] → `0x031093`; `INS_CockpitGetCursorAim` | EDX = x·s, EDI = y·s (AP: ESI/EDI); ECX = argument count untouched; EFLAGS **not** live (FL's `cmp ecx,4` follows the span at `0x0310c6`, AP's preceded it); the −1/−1 path `jl 0x0310d9` at `0x0310be` skips the span |
| I map label | `0x0041eab2` | `0x020158` | `f2 0f 2c c1 50 f2 0f 2c c4 50` cvttsd2si eax,xmm1; push eax; cvttsd2si eax,xmm4; push eax (10, bypass) | galaxy-map worker `0x01fa30` (AP `0x0041e350`); `push "WriteGalaxyMapSectorName"` at `0x02016a`, wrapper `0x0bb500` at `0x020179` (AP: x87 `ftol` and a native call) | y from XMM1, x from XMM4, each `round(v/s)` a tie away from zero (AP `map_offset`); XMM untouched; EAX, EDX, EFLAGS dead (wrapper) |
| J map pick | `0x0042e952` | `0x031d01` | `ff 76 0b ff 76 06 50` push [esi+0xb]; push [esi+6]; push eax (7, bypass) | table[`0x3e`] → `0x031cd8`; `INS_GalaxyMapFindSector`; pick `0x01f6b0` (`ret 0x14`) reads the real display width at `0x01f6f8` | EAX = scene; x·s, y·s through ECX, which the stub restores; EFLAGS dead (pick's `and esp,-8`); FL tests the map state inside the pick (`0x01f6bc`), so AP's flag-replaying compare is not needed |
| A projection | `0x004be246` | `0x0de91b` | `b8 01 00 00 00` mov eax,1 (5) | routine `0x0de570` (callers `0x096fb9`, `0x0976a0` only), 2D test `test [edi+0x130],0x200` at `0x0de5af` | ESI = P (AP EAX), EDI = instance with flags at `+0x130` (AP: flags in EBX), EBX = camera (AP EBP); stub keeps EFLAGS, ECX, XMM1, MXCSR; EAX dead (tail loads 1) |
| F diagnostic | `0x004bdee0` | not ported | — | — | under `--debug` the projection stub's two counters are printed instead (`ui_scale_frame … cameras=-`) |

Projection details (bytes `0x0de5bf..0x0de91b`): `P = *(base+0x1aae4c)`; `P[0] = 2·1.0/Wvp`, `P[5] = 2·1.0/Hvp` with
`Wvp = (camera[0x294]−camera[0x290])·W` in 16.16 and `W` from the display record `*(base+0x1a82f8)`;
`P[12] = 2(x − 0.25)/Wvp ∓ 1` with `0x1000` (left, −1) tested before `0x2000` (right, +1), `P[13]` with `0x4000` (top,
+1) before `0x8000` (bottom, −1) — the AP model (`sites::engine_projection`) and AP's anchor tables unchanged (constants
`0x591aa0` = 1.0f, `0x5919e0` = 0.25f checked). After the site only immediate stores to P[15], P[1..4], P[6..9], P[11]
and the epilogue; both callers reload ECX/EDX and `add esp,8` after the call.

Identity windows (28, FNV-1a with the relocated dwords zeroed, every fixup bound to the live base): the projection
entry, P loads, 2D tail, both callers; both size cases and their callee; both delta sends and both wrappers; the cursor
case (the same 40 bytes and FNV `0xe849e23a` as `chase_fire`'s `cursor_writer` window) and its return; the G, H, J case
bodies and the hit test / pick prefixes; the label span with its callback arguments; the registry walk, cockpit init and
camera attach; the five jump-table entries and the X2 index byte. Checked at `initialize()` and again before the claims.

## What differs from AP and why

- **Mouse deltas.** FL's KC cursor (class 502, `MoveCursorRelative`) moves by the two arguments of the
  `InputMouseDelta` callback, sent from two places: the direct loop (`[owner+0x4d8] == 1`) and the input worker's
  mapped-sum send, which covers both the controlled-object and the null-object branches ([ui-input.md](ui-input.md) §1).
  The four AP readers have no FL equivalent for the cursor: the raw-axis readers `0x0d1af1`/`0x0d1b10` feed native
  flight controls and stay unscaled; the legacy `MouseInput` events (`0x078106`/`0x078146`, classes 103–105) are absent
  from the FL story and stay out (consequence if a mod adds such a class: its cursor moves `s` times faster). The stubs
  scale only the outgoing copies, with AP's 16.16 remainder per axis and site, through a 64-bit product (a mapped sum is
  any dword).
- **Cursor store.** Two five-byte stores through EAX instead of one EAX/EDX site: two claims.
- **Excluded camera.** The same registry walk (`*(base+0x1a82e4)`, handle `+0x10`, buckets/rows as resolver `0x01e420`),
  cockpit `+8` = HUD camera, `+4` = HUD scene (`0x021b6c`, `0x021b96`). FL addition: the camera is published only while
  its attached scene (`camera+0x1c`, written by attach `0x0a5897`) equals the cockpit's `+4`; the cockpit teardown clears
  only `+4` ([ui-camera-lifetime.md](ui-camera-lifetime.md) §2), so a stale `+8` publishes 0 instead of a dead pointer.
  The stub only compares EBX with the cell; it never dereferences it.
- **Graphs.** FL charts (KC class 2327) are two `0x200 | 0x4000` instances with generated textures 1236/20
  ([ui-graphs.md](ui-graphs.md) §2): A scales them like every menu instance and B/C give their layout the virtual size;
  their lines and labels are magnified (the rows are `NOFILTERING`, so blocky at fractional `s`), not redrawn denser.
  Line footprint and density belong to the text-density port.
- **Text density** is not ported: one `text_density installed=0 status=ap_only edition=fl reason=not_ported` row.
- **Other screen queries.** B3D cases `0x75`/`0x76` (`B3D_ScreenGetAspectX/Y`) and `0x77`/`0x78`
  (`B3D_ScreenGetBorderX/Y`; names table entries confirmed) are not claimed and return unscaled values, as on AP
  (parity inferred from AP's site list, not traced into the scripts that read them).
- **Who receives D1.** The direct-path send passes the class in `[owner+0x4dc]` (`push [ebx+0x4dc]` at `0x077fdf`), so
  any KC class running in direct mode (`[owner+0x4d8] == 1`) receives its `InputMouseDelta` divided by `s`: a script
  that drag-rotates with these deltas turns `1/s` as fast. The legacy `MouseInput` events (classes 103–105) stay
  unscaled.

## Limits

Static and fixture evidence; not flown. The direct-branch census is a linear sweep (72,230 edges), the raw-encoding
scan covers the containing functions, the relocated-dword scan all 38,327 HIGHLOW entries; none proves the absence of a
computed jump into a span. The install happens at `CreateDevice` inside the engine_patch window, as on AP; that no game
thread executes a span at that moment is the AP assumption, not re-proven for FL. The excluded camera is refreshed per
Present; the first frames after a cockpit appears run unexcluded (as on AP).

Tooltip edge clamp (known gap, AP and FL): `INS_ShowTooltip` (INS case `0x6a`, `0x03228d` → `0x02fe00` on the object at
cockpit `+0x894`) clamps the tooltip against the **real** display width at `0x02ff32` (`movsx esi,word [edx+4]`) and
height at `0x02ff58` (`movsx ecx,word [edx+6]`), while its x/y arrive in the script's virtual pixels and the tooltip
node (Flags2 `0x5600`, a `0x200` instance) is scaled by A. At `s > 1` the clamp fires only past the real edge, which
virtual coordinates never reach, so a tooltip near the right or bottom edge runs off-screen. AP has the same clamp
(`INS_ShowTooltip` case `0x6a` at `0x0042ee95` → `0x0042cc10`, real width `0x0042cd31`, height `0x0042cd58`) and claims
no site there; the port keeps parity and leaves it a documented limit for both editions (a twelfth claim would need the
same span, liveness and fixture qualification as the eleven).
