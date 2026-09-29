# UI scale: enlarging the in-game 2D UI

`--ui-scale S|auto` / `X3M_UI_SCALE` / `ui_scale` in `x3m.ini` (default `1` = off).
Implements strategy (b) of [gui-scale.md](../reverse-engineering/gui-scale.md)
section 5: the in-game 2D UI (menus, sidebars, HUD panels, message ticker,
gravidar: every scene instance carrying node flag `0x200`) is drawn `S` times
larger, the script sees a `W/S x H/S` screen, and its cursor keeps landing where
it looks. The main menu is not touched: it goes through the perspective path
that already grows with the screen height. Source: `src/proxy/ui_scale.cpp`,
`src/proxy/ui_scale_sites.h` (portable core), `src/proxy/ui_scale.h`; verifier
`verification/probe/verify_ui_scale_sites.py`; host test
`verification/analysis/test_ui_scale.py`; ledger
[verification/ui-scale.md](../verification/ui-scale.md).

## The scale

- `S` in [1, 3] with up to four decimals, or `auto` = back-buffer height / 1080
  snapped down to quarter steps and clamped (1080 -> 1, 1440 -> 1.25,
  2160 -> 2, 4320 -> 3). `auto` is resolved from the presentation parameters at
  `CreateDevice`, never earlier; a `Reset` with another height re-derives it
  into the data cells (no code is rewritten). `1`, unset or empty patches
  nothing.
- Three fixed-point forms live in pinned DLL data cells the stubs read:
  `s` as float (projection), `inverse16 = round(65536/s)` (screen size and
  mouse), `fixed256 = round(256 s)` (cursor). At the identity every stub
  reproduces the displaced instruction exactly, so a failed rollback still
  leaves vanilla behaviour.

## Sites and contract

Ten claims through `engine_patch`, all or none, in this order (the
projection last, so nothing visible changes until every input-side site is
in); a failure restores the earlier ones and puts the identity cells back.
Every window is byte-compared at `initialize()` (fail closed) and the claims
happen at `CreateDevice`, inside the install window (a device created after
the first Present is refused with `late_claim`). Every displaced span is whole
instructions; `verify_ui_scale_sites.py` proves the boundaries, that no direct
branch, jump-table entry, raw rel8/rel32 encoding or image dword lands inside a
span, and the register facts below (75 checks, 10,331 instructions decoded).

| Site | Claimed bytes | Stub | Registers and flags |
| --- | --- | --- | --- |
| A projection, the 2D exit of `0x004bdee0` | `0x004be246 mov eax,1` (5) | `P[0] *= s; P[5] *= s; P[12] = (P[12]-ax) s + ax; P[13] = (P[13]-ay) s + ay`, `ax/ay` from the anchor bits in EBX through two 4-entry tables (`0x1000` left -1, `0x2000` right +1, `0x4000` top +1, `0x8000` bottom -1, the engine's precedence); skipped when EBP (the camera) equals the excluded camera; one counter `inc` | EAX = P (dead: the tail loads 1), EBX = flag word, EBP = camera; ECX scratch (dead: caller-saved, both callers reload it); EFLAGS dead (`add esp,8` follows); XMM0..2 scratch (volatile across the call boundary, no XMM use in the function or its callers); x87 untouched; no push, call or memory write outside P and the counters. Per instance on the scaled path: 31 instructions (11 movss, 8 mulss/subss/addss, 3 mov/shr/and index pairs, the compare, the inc and the jmp). With the identity cells (a Reset to 1080 lines under `auto`) P[12]/P[13] are within one ulp of the engine's, invisible at pixel scale |
| B, C script screen size, `0x00493b40` cases `0x71`/`0x72` | `0x00496194 mov edx,[VM]` / `0x004961bb mov ecx,[VM]` (6 each) after the `movsx eax,word [..+4/+6]` | `eax = (eax * inverse16 + 0x8000) >> 16` = `round(W/s)`, `round(H/s)` | EAX in/out; EDX / ECX dead (the tail loads them); EFLAGS dead (push, push, mov, then the callee `0x004a47f0`'s `cmp` before its `jb`) |
| D1..D4 script mouse deltas | `0x00403d36`, `0x00403d7a` (`movzx eax,word [eax+0x414/0x416]`, main loop) and `0x00411b40`, `0x00411b57` (`movsx edi,word [edx+0x414/0x416]`, the `0x00410080` loop), 7 bytes each | the stub performs the load itself: `acc += delta * inverse16; out = acc >> 16; acc &= 0xffff` (one 16.16 remainder cell per site starting at 0x8000, so slow motion is not lost and the running sum rounds to nearest symmetrically: ten +1 or ten -1 at 1.5 give +7 / -7), then `jmp [slot]` through its own continuation word, which `install_site` fills with the instruction after the load and reads back before the stub goes live (the claim's tail is emitted but never entered) | D1/D2: EAX = context in, `movzx eax,ax` of the result out; EFLAGS dead (`add esp,0x24` / `cmp ax,bx` follow). D3/D4: EDX = context in, EDI = result out; EFLAGS dead (`xor eax,eax`, or `0x0040fec0`'s first `cmp`). Integer only |
| E script cursor store, `X2_UpdateCursorSteering` (case `0x1f` of `0x00406de0`) | `0x004074ec mov [0x00607cf0],eax` (5) | `eax = (eax * fixed256 + 0x80) >> 8` (y), `edx` the same (x): the script's virtual cursor back to real pixels before both stores | EAX, EDX in place; ECX (the VM, pushed after) and EBX untouched; EFLAGS dead (push, mov, mov, the callee's `cmp`). The window compared at install skips the six bytes of the chase-fire claim `chase_cursor_write` at `0x004074de` |
| G click icon, INS dispatcher `0x0042d340` case `0x64` (`INS_CockpitGetObjectByTargetOverlayIconPos`) | `0x0042ece0 mov esi,[esi+6]; push ecx; push esi` (5, one aligned qword) | the stub performs the load, multiplies ECX (y, loaded at `0x0042ecdd`) and ESI (x) by `fixed256` with the E rounding, re-issues both pushes and continues at `0x0042ece5` through its slot word: the icon hit test `0x004299a0` receives the script's click in real pixels ([gui-scale.md](../reverse-engineering/gui-scale.md) section 6) | EAX (the cockpit) and EDX untouched (the callee writes EDX at `0x004299bf` before reading it); EFLAGS dead (`add eax,0x3ac` follows); stack: the stub's two pushes replace the displaced ones, the callee pops 12 |
| H cursor aim, case `0x28` (`INS_CockpitGetCursorAim`) | `0x0042ddf1 mov esi,[ebx+6]; mov edi,[ebx+0xb]` (6; the jump in the qword `0x0042ddf0`) | both loads, ESI (x) and EDI (y) multiplied the same way, then `cmp ecx,4` and the slot word `0x0042ddf7`: `0x00425410` and its `0x00489780` ray fallback receive real pixels | EFLAGS are live across the span (the `jl` at `0x0042ddf7` reads `cmp ecx,4` from `0x0042ddee`): the stub re-executes the compare with ECX (the argument count) untouched, so the engine's flags are reproduced exactly; EAX, EBX, ECX, EDX untouched. The `-1,-1` sentinel of the short-argument path skips the span. The natives themselves are not patched: the fire path reaches them with real pixels through E |
| F diagnostic (`--debug` only, and only when the option asks for a scale: `auto` or a value above 1) | `0x004bdee0 sub esp,8; cmp [eax+0x3c],0` (7) | per camera (8-row table, linear probe, the last row takes overflow) counts the `0x200` instances and the others, summed over the 300-frame window the other per-frame diagnostics use; printed and cleared at every 300th Present as `ui_scale_frame … frames=300` | EAX kept; ECX/EDX dead (the function writes both before reading, the callers reload); EFLAGS dead (the displaced `cmp`). Shares its bytes with the fixture-only `submit_phase_world_enter` claim: one of them refuses |

Threading: every site runs on the engine's main/render thread (the frame loop,
the script VM's dispatcher and the per-instance visit are one thread); the
Present hook that writes the cells and reads the counters runs on the same
thread, so no synchronisation is needed and the per-camera table is read and
cleared between two frames. The cells written at `Reset` are aligned dwords.
Re-entrancy: no stub calls anything; the arena blocks are never freed and the
DLL is pinned (`GET_MODULE_HANDLE_EX_FLAG_PIN`). Rollback: `restore_all()` in
reverse order, only over our jumps; `shutdown()` (dynamic unload) sets the
identity first, restores, and writes one `ui_scale_restore` row to the log
handle without the capture lock. LastError is saved and restored around every
entry point.

## What follows automatically, what is excluded

- Follows: every script menu, window, ticker, sidebar and overlay that lays out
  against `B3D_ScreenGetWidth/Height` and positions instances in pixels; script
  hit-testing (its own rectangles against its own cursor in the same virtual
  space); screen-edge clamping and the menu size limits; the cursor sprite
  (a script instance, scaled with the rest, so it sits where the real cursor is
  after the `x s` at E).
- Excluded: the active cockpit's HUD scene (`cockpit+4`, rendered with the
  camera at `cockpit+8`): the central crosshair group, the three text panels,
  the target/aim icons the native overlay `0x0042a2d0` places at projected pixel
  positions and the mod's chase lead marker (`chase_lead.cpp`,
  `finalize_hud_anchor` moves the same group to the projected forward point in
  real pixels). Excluding the whole scene by camera identity keeps every one of
  those in real pixels at vanilla size; `present()` refreshes the camera pointer
  once per frame through the registry walk of the native resolver
  (`*0x00608504` -> handle -> cockpit), and `device_created` resolves it once
  right after the install. Every change of the pointer (0 = none: no cockpit
  yet, or a walk that failed, so the HUD scene is scaled until it resolves)
  writes one `ui_scale_camera frame= excluded_camera= previous=` row. With no
  cockpit (main menu, loading) nothing is excluded; the first frames after a
  cockpit appears run unexcluded until the next Present publishes it. The world-object brackets (`0x00427d50`) are not `0x200`
  instances and are untouched either way.
- Cursor fire and steering (`0x00489780` unprojection, `0x00425410` aim,
  `0x004257f0`'s 35-pixel box, the fire gate) read `0x00607cec/0x00607cf0`,
  which E keeps in real pixels; the engine's own absolute cursor
  (`*0x00606f3c + 0x410`) was never in the script's space.
- Click selection and the hover cursor icon over a bracket: the script hands
  its (virtual) cursor to the two INS natives of G and H, which scale the
  point before the real-pixel icon test; run390 (5120x1440, 1.25) showed the
  layout right and bracket clicks missing by `X(1 - 1/s)` before G/H.

## Known limitations

- Bitmap text: fonts are fixed-size atlases blitted into textures, so text is
  magnified by `s` with the texture filter and goes soft at non-integer scales
  (blocky on the `MPF_NOFILTERING` textures). Sharp text is strategy (a) of the
  note (`-fontscale s`, `s x` atlases, `MPF_FONTSCALE` on every text texture),
  deferred; integer scales look best until then.
- The script's virtual size is `round(W/s)`; the projection's viewport is
  `W/s` exactly: the edge mismatch is under one pixel.
- Script code that hands a pixel size to a native call that works in real
  pixels (a screen-sized texture, a viewport in pixels) would get the virtual
  size; none is known, the first flight must look.
- Open item 1 of the note (which scenes carry the main menu, the gravidar and
  the ticker) is what the `ui_scale_frame` row (one per 300 Presents under
  `--debug` with the option on) settles: the excluded camera must appear in
  its `cameras=` list with `0x200` instances in flight, and the main menu's
  rows should show no scaled instances.
