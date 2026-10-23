# SETA and manual flight

`X3M_SETA_MANUAL_FLIGHT` (schema `seta_manual_flight`, section `[engine]`, enum `0|1|2`, default `2`; launcher
`--seta-manual-flight {0,1,2}`, refused and never forwarded under `--vanilla`). Source:
`src/proxy/seta_manual_flight.cpp`, `src/proxy/seta_manual_flight_core.h` (sites, windows, decision),
`src/proxy/seta_manual_flight_stubs.h` (the five stubs). Ledger:
[seta-manual-flight.md](../verification/seta-manual-flight.md). Engine facts:
[seta-manual-flight.md](../reverse-engineering/seta-manual-flight.md) (RE note, cited as "§n" below).

| value | effect |
| --- | --- |
| `0`, empty, anything else | nothing patched (anything but `0`/`1`/`2` logs `reason=invalid_value`) |
| `1` | part A: steering, strafe, turret and look input no longer stop SETA; a manual speed change still does |
| `2` | part A and part B: under SETA the ship turns and strafes at its unwarped real-time rates |

## Part A: the input stop

The input routine `0x00410100` calls `StopFastForward` from its stop block whenever one of nine "changed this frame"
flags is set and the warp `cfg+0xcc` is not `0x10000` (§1, §2). The flags are tested by nine 8-byte rows from
`0x004120c5`, `cmp [flag],ebx` (EBX = 0) followed by a two-byte jcc at row + 6: rows 1–8 (yaw, pitch, roll, turret
yaw, turret pitch, strafe x, strafe y, speed) `jne 0x0041210d` (the stop), row 9 (look) `je 0x00412136` (the join).
Part A keeps the stop for speed only, as the user asked after flying release 1.5.0: a speed change by hand still ends
SETA, as in the stock game (the speed flag is also set by Boost, the throttle and the speed slider, so those end SETA too); steering, strafe, turret and look input keep it on. Eight 2-byte rewrites:

| row | jcc at | original | patched |
| --- | --- | --- | --- |
| yaw, pitch, roll, turret yaw, turret pitch, strafe x, strafe y | `0x004120cb` + 8k (k = 0..6) | `75 40`, `75 38`, … `75 10` (`jne 0x0041210d`) | `66 90` (two-byte nop) |
| look | `0x0041210b` | `74 29` (`je 0x00412136`) | `eb 29` (`jmp 0x00412136`) |

A set non-speed flag falls through to the next compare, and the look row never falls into the stop, so the stop runs
only through the speed row's untouched `jne`, then through the engine's own `cfg+0xcc == 0x10000` check
(`0x0041211d` stays `74 17`). The filler is the two-byte nop rather than `eb 00`: no control transfer, and the rows
read as plain compares in a disassembly. Every rewrite starts at the jcc's own instruction start, the rel8 byte is
never a branch target, nothing in `.text` branches into `0x004120bc..0x0041210c` or holds a pointer there (the study scans `.text`; `.rdata`, `.data` and `.rsrc` were scanned once in review on 2026-10-24, 0 hits), and every
instruction start of the patched block is unchanged. Each rewrite lies inside one aligned qword (offset 3 of
`0x004120c8` + 8k, `0x00412108`), so `engine_patch::write_code` writes it with one `lock cmpxchg8b`, under one
`VirtualProtect` over `0x004120cb..0x0041210c` (one page), followed by `FlushInstructionCache` and a read-back.
Every intermediate state of the eight writes is a valid mix of stock and patched rows. Every script stop (gates,
docking, the SETA key, the speed-bar click, …) still works. No other claim lies in `0x004120c5..0x0041210c`
(seta_restore's `stop_call` window starts at `0x0041211f`; the chase restore's claim is the routine entry
`0x00410100`+6).

## Part B: real-time handling under warp

All player control is a command in game-time units, ramped and integrated with game milliseconds (§3). Under warp
k the turn rate and the angular acceleration are therefore both k× per real second. Part B scales the controller
step and target, and the strafe limit, by the clock's effective rate:

* `R = (cfg+0xcc · cfg+0xd0 + 0x8000) >> 16` (warp × load governor, what the game clock advances by), `k = R / 65536`.
* The scale applies only when `cfg+0xcc > 0x10000` and `R > 0x10000` (k > 1: the game clock runs faster than real
  time). The governor is 0.3..1.0, so during the SETA ramp under load R can be below 1.0 with the warp above it
  (factor 1.5 × governor 0.4: k = 0.6). Scaling then would raise the target above the ship's clamped maximum (1/k) and
  the step by 1/k², so those frames take the original path, as does every frame at warp 1.0 and an R past int32.

### Sites and stubs

| axis | site | displaced (whole instructions) | scaled path |
| --- | --- | --- | --- |
| yaw | `0x0040ebde` | `mov eax,[0x00606f34]; fimul [eax+0x714]; mov ecx,[edi+0x24]` (14) | `fmul qword [step_factor]; mov ecx,[target]; mov eax,[0x00606f34]; jmp 0x0040ebec` |
| pitch | `0x0040ee3e` | same, `[edi+0x24]` (14) | same, `jmp 0x0040ee4c` |
| roll | `0x0040f04d` | same, `[esi+0x24]` (14) | same, `jmp 0x0040f05b` |
| strafe y | `0x00411e0c` | `fild [esi+0x714]` (6) | `fild [esi+0x714]; fmul qword [inv_k]; jmp 0x00411e12` |
| strafe x | `0x00411eb2` | `fild [esi+0x714]` (6) | same, `jmp 0x00411eb8` |

Each site is an `engine_patch::claim` (length 14 or 6, `rel32_offset` 0). The claim's tail holds the displaced bytes
and a jump back, and is the stub's continuation (the original path). Every stub:

1. saves EFLAGS, EAX, ECX, EDX and XMM0..7, clears DF, and calls `x3m_seta_manual_flight_axis(axis, record, ship)`
   (cdecl; yaw/pitch push EDI = record and ESI = ship, roll pushes them swapped, strafe pushes 0, 0);
2. restores everything it saved, so ESP, EAX..EDI and DF are as on entry;
3. reads the helper's decision byte. It then either jumps to the tail (original path) or runs the scaled
   instructions above and jumps to the span end.

The helper runs under `LightCallBoundary` (MXCSR, LastError) and computes under MXCSR `0x1f80`. It executes no x87
instruction: it is a `check_no_x87.py` root, and its debug row is formatted behind `call_preserved`. The x87 stack the
engine left is therefore untouched until the stub's own instructions:

* rotation: st0 = accel on entry; `fmul qword` replaces `fimul` in place (depth unchanged). Out: st0 =
  accel × game_ms / k² (the engine's next instruction multiplies by 0.001: step = accel × game_ms × 0.001 / k²), ECX =
  round(rec+0x24 / k), half away from zero, EAX = cfg as in the original. EDX is preserved; EFLAGS are dead
  (`sub esp,8` follows).
* strafe: one `fild` push as in the original, then `fmul` by 1/k in place. EAX, ECX and EDX are preserved; EFLAGS are
  dead (x87 instructions only up to `test ah`).

The helper also checks the record register against the axis's record (`0x00608e50`, `0x00608e78`, `0x00608ea0`). Any
other value takes the original path (`contract` count in the debug row).

**Mouse-delta modes.** The mouse dispatcher `0x0040fec0` (jump table `0x00410024`) feeds a mouse axis of type 1, 2 or
3 into the yaw, pitch or roll command as `delta · 0x8000 / game_ms` (§4). That command is already warp-invariant per
mouse count. The dispatcher reads the pair `0x00608e40`/`0x00608e44`, or `0x00608e48`/`0x00608e4c` while the caller's
view flag (`[esp+0xc]`, read at `0x00411b3c`) is set; types 1–3 in either pair feed the ship records. The skip is
conservative: while any of the four words equals the axis's delta type (yaw 1, pitch 2, roll 3), that controller takes
the original path. In a configuration where the flagged pair holds such a type but is not the one read, the axis
therefore loses the scale (keys stay warped) rather than being mis-scaled. Types 4/5 feed the turret, which no stub
scales. Strafe has no mouse-delta mode.

**Why the ramp needs 1/k².** The record's rate is re-seeded from `obj+0x24` each frame. Under warp k the real-time rate
is k × the game rate. Unwarped flight has `dr/dt_real = a` toward ω. The warped game state reproduces it with target
ω/k and a per-frame step `a · game_ms / (1000 k²)` (= `a · wall_ms / (1000 k)`). Steady state then gives
`obj+0x24 · k = target`.

**Strafe: the limit, not the increment.** The increment is `clamp(command, ±20000 · ms · 0.001)`. It is added to the
slip velocity, which moves the ship by `v · dt / 1000` and decays by `(2000 − dt) / 2000` per frame (`0x0045a74e`). In
steady state the ship moves `2 × increment` per frame, whatever dt is. Loading `ms / k` at the `fild` gives the
increment `clamp(command, ±20 · game_ms / k)`, which equals the unwarped increment whether or not the limit binds.
The real-time strafe speed is therefore the unwarped one. Scaling the whole increment by 1/k would also scale
the command, and the ship would strafe k× too slowly whenever the strafe maximum is below the limit.

### What stays warped

* **Turret** rotation (`0x0040f1e0`, its own game-ms step): not covered by the ship controller spans.
* **Auto-level roll** (integrator `0x0043ad20` with game dt, player ship with universe flag 2): levels k× faster.
* **Slip settle**: the integrator's 2 s game-time decay is shared by all objects, so the strafe velocity settles k×
  faster in real time; only its steady value is real-time.
* **Speed keys** (`0x0040f350`, boost, analog speed axis): change the desired speed k× faster per real second
  (out of scope for this build).
* **Forward speed** and every integration step: SETA's purpose. Per-frame turn angles at ×10 are still k× larger
  (the existing SETA step size). The mouse-delta axes keep their warped ramp and cap.
* The SETA ramp (ten steps 250 ms apart) changes k under a held turn. The stored game rate then gives a real-time rate
  that jumps by `R_new / R_old` per step until the controller re-converges at its scaled ramp.

## Fail closed and rollback

On the backend-load path (capture `initialize_log`, after `seta_restore::initialize()`), inside the `engine_patch`
install window, in this order:

1. the executable identity (`object_trace::executable_verified`), else `executable_mismatch`;
2. the install window, else `late_claim`;
3. every window of the mode, read whole with `engine_patch::read_code`, else `bytes_mismatch` naming the window.
   Part A: the nine rows `stop_yaw` … `stop_look` `0x004120c5` + 8k, 8 bytes each (the unpatched `stop_speed` row
   included: it is the stop's only entry after the patch), and `stop_block` `0x0041210d`+48. Part B adds
   `yaw_controller` `0x0040ebd0`+58, `pitch_controller` `0x0040ee30`+58, `roll_controller` `0x0040f03f`+58,
   `strafe_block` `0x00411dd0`+331, `mouse_dispatch` `0x0040fec0`+105 and `mouse_table` `0x00410024`+20. Each window holds its claim and the code its register and x87
   contract depends on. `verify_bytes` is not used: it caps at 16 bytes (Run 189);
4. mode 2: the DLL is pinned (`GET_MODULE_HANDLE_EX_FLAG_PIN`), because the engine jumps into its stubs;
5. the eight stop rewrites in row order, each registered before its write and read-back checked; a failure puts every
   rewrite written so far back in reverse order, judged by read-backs of the original bytes (`patch_rolled_back`);
6. mode 2: the five claims in axis order. The continuation slot is set before the stub becomes the chain head.
   Any failure restores the claims made so far and all eight stop rewrites.

The rule is all or nothing per mode: in mode 2 a mismatch in any part-B window refuses part A too (no fallback to
mode 1). A step that cannot be undone stays registered (`patched=1` in the row) and `shutdown()` retries it. `shutdown()`
(FreeLibrary only, from `loader.cpp`) disarms the helper first, so a stub still entered takes its original path. It
then restores the five claims and writes the eight original jcc back. The stop restore first checks that every
registered row still holds this feature's bytes or the original (`restore_not_owned` otherwise, nothing written).
The stub arena and the tails stay callable. Device Reset does not touch the feature (no device state).

## Log rows (`seta_manual_flight …`)

* `installed=0 mode=0 reason=disabled|invalid_value`: the variable was set to `0` or to an invalid value.
* `installed=1 mode=2 stop=speed_only stop_sites=8 stop_rows=0x004120c5 stop_status=active stop_write=atomic
  yaw=active:plain pitch=active:plain roll=active:plain strafe_y=active:plain strafe_x=active:atomic
  gate=factor>0x10000,rate>0x10000 … debug=…`: each site's status and `engine_patch` write path (`off:none` for part B
  at mode 1); `stop_write=atomic` only when all eight rewrites took the atomic path. The stop rewrites and the
  strafe-x jmp (`0x00411eb2..0x00411eb6`, inside qword `0x00411eb0`) take `write_code`'s `lock cmpxchg8b` path; the jmps at `0x0040ebde`, `0x0040ee3e`, `0x0040f04d` and `0x00411e0c` cross an 8-byte boundary and are plain
  copies, safe because the install window precedes the first Present.
* `refused reason=… mode=… window=0x… window_name=… window_length=… expected=… got=… stop_status=… yaw=… … patched=…`:
  the first failing window or step; each site's status after rollback.
* `scale frame=… wall_ms=… rate=0x… factor=0x… governor=0x… game_ms=… target=… scaled_target=… ship=0x… ship_read=…
  ship_rate=… ship_rate_k=… applied=y,p,r,sy,sx skipped_warp_off=… skipped_rate=… skipped_mouse=… skipped_contract=…
  mouse=e40,e44,e48,e4c` (`skipped_rate` counts the frames with R ≤ 1.0 or past int32) (`debug = 1` only). Written from the yaw stub while the scale applies, at most once per 500 wall ms (a
  wall bound on purpose: the row exists to read real-time rise times). `target` is the clamped command rec+0x24 in game
  units. `ship_rate` is `obj+0x24` at the span (the rate the controller stored the previous frame), and `ship_rate_k`
  is `obj+0x24 · k`. The counters cover the calls since the previous row. No other time bound exists in the feature,
  so none needs to scale with the game step.

**Flight check (§6).** One full-deflection yaw at ×1 and at ×10 with `debug = 1`. Expected: `ship_rate_k` rises to
`target` in the same wall time at both warps, and its steady value is equal at both.

## Windows and CrossOver parity

Documented Win32 only: `VirtualProtect`, `FlushInstructionCache`, `ReadProcessMemory` (through `engine_patch`),
`GetModuleHandleExW` with the pin flag, `GetTickCount64`. No backend-specific dependency. The stubs were executed
natively on Windows 11 by `verification/probe/seta_manual_flight_stub_fixture.cpp`, against a fake engine image at the
real addresses, not in the game.

## Hot-path cost

The helper is called five times per frame from the input routine (not per draw), with no allocation and no lock.
Each call makes eight loads, one `divsd` and three `mulsd` on the decision path (the built helper has two more `mulsd`
in the debug row). The native fixture measured 83.4 ns per stub run, including its driver, `finit` and landing
(2026-10-24, Windows 11 host, fixture helper without the `LightCallBoundary`). That is
under 0.5 µs per frame for all five.
