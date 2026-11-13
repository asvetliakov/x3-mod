# Weapon, impact and explosion effects: hooks at the spawn sites, one family per flight

Design note, 2026-11-05 (Fable 5.1, design agent). **Ratified 2026-11-05 by the main session** as the architecture for the restart; the native fixture runner is a prerequisite of step 1. A delta on the ratified and dropped
[effects-modernisation-opus.md](effects-modernisation-opus.md): its per-class rendering (capsule bolts, Fresnel shield
shell with ripples, sparks, noise-sphere explosions, beams; sections 3.1–3.5) and its stage placement (additive FP16
scene-end pass inside the TAA resolve's step-0 bracket, lane-depth occlusion; sections 2.2–2.5, 8.3) **stay as
written and are not redesigned here**. This note replaces its identity, suppression, data and process. Nothing here is
built. Inputs: [weapon-effect-spawn.md](../reverse-engineering/weapon-effect-spawn.md) (the sites, the TBullets map,
the death-row chooser), `src/proxy/engine_effects_sites.h` + `engine_effects_patch.cpp` (the call-redirect pattern),
[engine-effects-modern.md](engine-effects-modern.md) sections 1–2 (suppression and record extraction of the engine
plumes), the weapon census `verification/results/weapon-effects-census/{stock_ap,mayhem3,fl}.json`
(`tools/effects/weapon_effects_census.py`) and the stage ledger [effects-stage.md](../verification/effects-stage.md).

Marks: **[M]** measured (a cited row, listing or census number), **[I]** inferred, **[S]** static (code or archive
read). Costs are for 5120x1440 unless a line says otherwise.

**Look layer scratched (2026-11-08).** Steps 2 to 4 (hull hit, explosions, bolts and muzzle) were built and removed
after Run246; the code is on branch `effects-look-archive-2026-11-08`, the notes in
[the archive](../archive/effects-look-2026-11-08/index.md). Production keeps the hooks of section 2 (every site,
install and rollback), the record and classifier of sections 3 and 4 and step 1 (5.1, 5.1.1). The setting takes
`native`, `observe` and `shields`; the family modes are reached through `install_at` only (the hook fixture), and no
stage latches them, so their records forward native. The redo starts from projectiles anchored to the game's own
geometry.

## Outcome

Hook the engine's four weapon-effect spawn call sites (impact `0x00449d85`, muzzle `0x00446268`, death
`0x0044ac64` / `0x0044acb2`) with the `claim_call` redirect pattern of `engine_effects_patch.cpp`; each stub hands a
64-byte **event record** (role, victim or owner, projectile record index, contact point, absorbed flag, damage, game
time) to the proxy and **skips the engine's spawn** when the stage owned the previous frame, else forwards the call
unchanged (step 1's shield hit skips it too and the patch alone answers the hit, coordinator decision 2026-11-06,
2.2). Style comes from a **classifier over the live TBullets record** (flag bits, damage per shot, light colour)
plus the vanilla body's elongation from a generated table, with a curated override file keyed by `SS_BULLET_*` id per
edition and known mod; users configure nothing. Explosions tier by the victim's class at the death site (three ship
tiers, stations, asteroids) with TShips col 47 as the cross-check. The stage of the dropped design draws everything.
Each family is one flown step, in the order **shield hit, hull hit, explosions, bolts and muzzle, beams**, and every
step ships with a lab page and a host fixture before its flight.

What changed since the drop (the look stayed the game's; tuning took hours): phase 1 drew its capsules and shells *on
top of* the native sprites because suppression at draw time had no airtight native fallback (review B1 [M, ledger]),
and its constants lived in shaders tuned by build-and-fly. Here suppression is decided **at spawn time on the game
thread** from the previous frame's stage verdict, so the native effect never has to be forwarded late, and every
constant is a header value the lab page and the fixture read before any flight.

| | Figure | Mark |
| --- | --- | --- |
| Hot path, hook | one stub per event: ~12 plain reads and a 64 B ring write, no call out of the DLL, no allocation; a 50-hit frame ≈ 20–25 µs CPU | I |
| Hot path, stage | 60 bolts + 2 shells 0.124 ms (1080p) / 0.162 ms (5120x1440); riding an open pass 0.012 ms, opening its own 0.021 ms (price 0.008 ms, noise ±0.015); a shell past every screen edge 0.54 / 0.77 ms | M (dropped phase 1) |
| Saved | ≈ 7.5 µs Wine frontend time per suppressed native draw, 0 on native Windows | M / I |
| Native Windows | documented Win32 only (`VirtualProtect`, `FlushInstructionCache`, `GetModuleHandleExW` pin) and documented D3D9; the sites are X3AP.exe bytes, identical on both hosts; the FL redirect fixture of 2026-11-02 ran natively through WSL interop (69 checks) | S / M |
| Verification | per step: a native-run hook fixture on a synthetic image of the windows, a D3D fixture through the production `TemporalPass`, the lab page's `--check`, one flight | — |

## 1. Facts this note stands on

- The role of every weapon effect is fixed by its call site, not by the Effects row: muzzle (`0x00413c00`, TBullets
  col 21), impact (`0x00413c40`, col 20 or TMissiles col 20), death (`0x00413610`, a per-class column chosen by
  `0x00413040`) [M, spawn note §5–6]. The shield/hull distinction exists only as the damage routine's local
  `absorbed` flag, readable at the impact site as `[esp+0x24]` at stub entry [M, §3/§6].
- All four sites are whole 5-byte `E8 rel32` calls; no branch or image dword lands inside them [M, §6]. Muzzle
  and `0x0044acb2` sit inside one aligned qword (atomic `cmpxchg8b`); impact and `0x0044ac64` straddle one (plain copy,
  install window only) [M]. Every site runs on the game-update thread, which is the thread that calls Present
  [M, engine-effects.md §8 "Thread"].
- Skipping a spawn is a state the engine already runs: ship–ship contact spawns no impact (`0x00413c40` returns 1
  with no effect), 5 stock rows have muzzle col 21 = 0, kill reasons `0xe`/`0x10..0x15` skip the explosion
  [M, §2–4]. The three callees' returns are dead at the callers [M, §6].
- Mayhem 3 replaces 22 of 32 bullet bodies (the brief's figure), uses its own Effects rows (200–237 impact,
  100–137 muzzle, 600–602 death) and sets the light colour to white on 35 of 38 rows [M, census]. Texture and body
  identity therefore cannot classify; the record and the site can.
- Native impact rows carry an `EEDF_LIGHT` element on 16 of 26 stock rows and 14 of 15 Mayhem rows; 6 stock impact
  rows (16, 57, 58, 155, 156, 186) and the capital death row 37 (Mayhem 602) have `EEDF_PROPAGATE` children; 13 of 21
  stock muzzle rows (19 of 22 Mayhem) carry a lens flare [M, `classify_preview.py` session query]. Suppressing the
  outer call removes all of them together; the stage must draw its own hit light, debris and muzzle flash.
- The engine's game clock is `cfg+0x718` (cumulative game ms, SETA-scaled), the per-frame game step `cfg+0x714`,
  `cfg = *0x00606f34`, written on the main thread [M, engine-effects.md §8].

## 2. Hook contract per site (question 1)

### 2.1 The stub shape, shared

One file-scope asm stub per site in a new `src/proxy/weapon_effects_patch.cpp`, constants and expected window bytes in
`src/proxy/weapon_effects_sites.h` (the `engine_effects_sites.h` form: VA, window length, expected bytes,
`static_assert`s on the `E8`, the rel32 target, the caller's pop and the qword rule). Each stub:

1. pushes the caller-dead registers it will use plus any register the callee takes as input (muzzle: EAX, EDX);
2. calls `extern "C" int __cdecl x3m_weapon_event(const StubArgs*)` with a pointer to its own stack (the arguments
   as the call left them, the live EBP for the frame locals, the site id). The handler is integer and SSE2 only,
   compiled with `-mstackrealign` (the four-byte incoming boundary), calls nothing in the CRT or Win32, allocates
   nothing, throws nothing, leaves x87, MXCSR, DF and LastError untouched; it reads engine memory with plain loads
   (the pointers are the routine's own arguments, already dereferenced on this path, on this thread, as the engine
   plumes' stubs argue [S, `engine_effects_sites.h`]) and writes one record into a proxy-owned ring;
3. pops, then either **forwards** (`jmp dword ptr [continue_<site>]`, stack and every register as the call left
   them) or **skips** (`xor eax,eax; ret`: every callee is cdecl with a plain `ret`, the caller pops; EAX, ECX, EDX
   and EFLAGS are dead after all four calls [M, §6]).

The handler returns the decision; the stub never decides. The muzzle stub runs inside the fire routine's SEH region
and the death callee installs its own [M, §6]: a handler that never unwinds satisfies both (the try state at
`0x00446268` stays unknown and does not matter for a non-throwing handler).

### 2.2 Impact `0x00449d85` → `0x00413c40(victim, source)`

| Reads (at stub entry) | Stored |
| --- | --- |
| `[esp+4]` victim, `[esp+8]` source; `[esp+0x24]` absorbed; `[esp+0x1c]` the routine's running damage total (clamped to 50,000 for a piloted victim) [M, §6] | role = `shield_hit` when `absorbed == 1 && !(source class 0 && TBullets flag 0x20)`, else `hull_hit` [M, §3]; `damage` = the total |
| source class `+0x48`, subtype `+0x4a`: class 0 → TBullets[subtype] (flags `+0x58`, light `+0x5c..`, shield/hull damage `+0x44`/`+0x80`, speed `+0x54`, size `+0x68/+0x6c`); bullet ext `[source+0x50]`: owner `+4`, laser `+0x70` [M, §2] | `table = bullets`, `row = subtype`, the classifier inputs copied (so the record is self-contained if the bullet dies this frame); no `shooter` (the bullet extension's owner `+4` is not read, F9): its four bytes carry the hit object's `shield_q` (u16) and `patch_tier` / `tier_source` (u8 each) of the repacked record |
| class 10 → TMissiles[subtype] (impact row `+0x68`) [M, §3]; the missile extension layout is untraced | `table = missiles`, `row = subtype`, style `missile_hit` (fixed: medium calibre, warm tint) |
| any other class on both sides | no record; **forward** (the engine spawns nothing either) |
| victim: class, subtype, root node `obj+0x70`, node handle `node+0x28`, radius `obj+0xa4`, position `node+0x30`, rotation rows `node+0x40` [M, spawn note §4, engine-effects.md §7] | `owner` = {object, node, handle, serial when `object_lifetime` is active}, `radius` |
| contact point: the smaller body (radius compare) gives `p = node+0x30 + R(node+0x40) · node+0x180`; `pos = Rᵀ(victim) · (p − victim pos)` [M, §3: the callee's own arithmetic, replicated] | `pos` in the victim's frame (3 floats), the projectile heading from the projectile's `node+0x40` z row |

Step 1 as built (review 2026-11-05): `shooter` is not read (`[[source+0x50]+4]` is not dereferenced by the engine on
this path, F9; the record carries 0 until a later step binds the owner), and the handler reads the two objects' flags
`+0x40` for the nested rule of 2.7.

Decision: a record is the **stage's** (`flag_stage`, it draws the hex patch) when the stage owns the frame (2.6) *and*
the family of the role is armed (step 1 arms `shield_hit` only; a hull hit is native until step 2). The stage's shield
hits **skip** the engine's spawn (`core::suppress_native_shield_hit` = `true`, `HookState::suppress_native`; coordinator
decision 2026-11-06, superseding the user decision of the same day to keep the native sprite under the patch): the
stub returns `xor eax,eax; ret` to `0x00449d8a` as if the callee had run (the caller's `add esp,8` pops the two
arguments and `mov eax,esi` overwrites EAX; EBX/ESI/EDI/EBP, DF, MXCSR, the x87 state, LastError and the stack are as
the call left them, checked by the hook fixture; ECX/EDX come back from the stub's saves, which a cdecl callee would
have clobbered anyway). Observe, hull hits, a dying hit object and every
frame the stage did not own forward unchanged. The replicated contact point is the one unknown of this site (the meaning of `node+0x180`);
it is settled by the observe flight of 2.6, not assumed.

The handler also reads the hit object's shield fraction (spawn note §9: the class-9 list `[obj+0x50]+0x6c` to its
sentinel, at most 64 nodes, TShields index and capacity, energy `[node+0x50]+4`; capacity-weighted like the HUD bar,
post-hit; any bad node refuses the read and the patch falls back to full shields) and the patch tier (the TShips class
id `+0xc8` of 8.1 for class 7, docks and factories as capitals, else radius bands), integer only, every pointer and
index bounded, into the four bytes the unread `shooter` held (3.1).

Why the outer site and not the inner instance call (`0x00413da8`/`0x00413e79`, where `&pos` is on the stack and a
null return is tolerated [M, §6]): `0x00414410` spawns the row's `EEDF_PROPAGATE` children *before* the inner call,
so an inner skip leaks native children on the six stock rows above [M]. The inner sites are used only as a
**diagnostic tap** (record `&pos` and ESI, forward) during the observe flight, to prove the handler's `pos` equals the
engine's.

### 2.3 Muzzle `0x00446268` → `0x00413c00(ship, k, &pos)`; EAX = TBullets index, EDX = &orientation

| Reads | Stored |
| --- | --- |
| `[esp+4]` ship, `[esp+8]` gun slot k, `[esp+0xc]` &pos (16 B, ship frame [I, §2]); EAX bullet index; EDX &orientation (12 dwords); caller frame `[ebp-0x18]` laser object [M, §6] | role = `muzzle`, `owner` = ship (node, handle, serial), `pos` local, `axis` = orientation z row, `table = bullets`, `row = EAX`, the classifier inputs, `shooter` = ship, `slot = k` |

EAX and EDX are callee inputs: the stub saves and restores them around the handler so the forward path is exact
(the engine plumes' stub A saves only EAX [S]). Skip = no muzzle instance, which is the col-21 = 0 state [M]. The same
record seeds the bolt family of step 4 (a shot's origin, direction, speed and style at time t0), so the muzzle site is
claimed in step 4, not before.

### 2.4 Death `0x0044ac64` (area bullet) and `0x0044acb2` (everything else) → `0x00413610(obj, killer, arg4)`

| Reads | Stored |
| --- | --- |
| `[esp+4]` dying object (also EDI), `[esp+8]` killer, caller frame `word [ebp+8]` reason [M, §4/§6]; class `+0x48`, subtype `+0x4a`, flags `+0x40`, radius `+0xa4`, root node, position, rotation, velocity `node+0x100` [M] | role = `explosion`, `owner` = the dying object (for position and velocity at t0 only; the node dies with it), `tier` (section 4.3), `radius`, `pos` = world position, `velocity` |
| class 7: TShips[subtype] `+0xc8` class id (col 52) and `+0xbc` death row (col 47) [M, §4] | `tier` from the class id (fallback: radius bands), `native_row` = col 47 for the cross-check row |
| class 5/6: `+0x60` row (col 14); class 17: TAsteroids `+0x48` (col 8); class 10: TMissiles `+0x7c` only when `obj+0x40 ≠ 0`; class 0: TBullets `+0x78` when flag `0x100` [M, §4] | `tier` = station / asteroid / missile / burst |

Rules that mirror the callee so that a skip is exactly "no explosion": (a) `0x0044ac64` must not assume class 0 (the
flag test indexes TBullets by the object's subtype without a class check [M]); (b) a class-10 object with
`obj+0x40 & 0x1000000` and a non-zero killer gets **no record** and is forwarded (the engine spawns nothing and runs
the missile's area damage in the caller [M]); (c) a missing node or class `0x1c` → forward; (d) an unrecognised class
→ forward (native explosion). Both death sites are claimed together or not at all (the engine-plumes "both or none"
rule); at the death record the ring **retires every hit record whose owner is the dying object** (the shell must not
draw on a dead node), on the same thread before any render.

### 2.5 Flight `0x004146c0` (optional, step 4)

The per-bullet attached-effect call (`0x00414590` class-0 branch, obj = bullet, 5-byte atomic site [M, §6]) would give
each live bullet's position and velocity every frame without any draw-time matching. Whether that call is reached for
a bullet whose col 19 is 0 (42 of 45 stock rows, 33 of 38 Mayhem) is **unknown** and decides step 4's mechanism
(section 5.4); the decompile of `0x00414669..0x004146fa` settles it before step 4 is briefed.

### 2.6 Suppression, arming and the fallback

- **Ownership is latched per frame at Present**: `owned_next = stage drew this frame && device attached && not within
  the 64-frame disarm window` (step 1: `core::stage_owned`: armed, the resolve completed, and the stage callback, when
  set, ran and returned `S_OK`/`S_FALSE`; review F8). The hooks read `owned_next` during the next frame's update (same thread, before the
  scene draws). A hook never consults device state itself.
- **The stage draws only when owned and the family is armed; a skip additionally needs the family's suppression.**
  A record the stage does not own is census-only (logged, then released); the stage never draws it. Step 1's shield
  hit skips the native spawn (`core::suppress_native_shield_hit` = `true`, coordinator decision 2026-11-06): it is
  drawn by the stage alone, and every family draws either natively or by the stage, never both.
- **One-shot events and a failing stage.** If the stage fails at scene end, that frame's stage-owned
  events are not lost: they live in the proxy ring with their own game-time timelines and are drawn when the stage
  returns, if still within age. The stage disarms for 64 frames (the dropped design's rule), during which the hooks
  forward. The loss is bounded to the fail frame's own draw, never the event. This is the airtight path the dropped
  review B1 could not find at draw time: the decision moves to the spawn, where "skip" is an engine-native state.
- **Observe mode** (`weapon_effects = observe`): every hook records and forwards; the inner impact tap is live; the
  stage draws nothing. It is the first flight of every family (section 5.1) and the mode that prices the hooks.
- **Modes** (`weapon_effects = native | observe | <families>`, key in `tools/config/schema.py`, launcher
  `--weapon-effects`): `native` patches nothing; the family list arms steps independently (`shields`, `hull`,
  `explosions`, `bolts`, `beams`); the DLL default is the set of flown-and-accepted families (none until step 1
  lands), as `engine_effects` defaults to `plumes` after Run 123 A [S, config-file.md]. Since 2026-11-08 the setting
  takes `native`, `observe` and `shields`; `hull`, `bolts` and `explosions` beside `shields` (the lists built until
  then) run as `shields` with one `weapon_effects_ignored name=` row each.

### 2.7 Nested kills and missiles

An area kill runs `0x0045eeb0`, which runs the damage routine (impact stubs) and nested kills (death stubs) **inside
the outer kill**, after the outer death stub returned [M, §6]. The ring is append-only and per-event; no stub keeps
state across another, and the retirement rule of 2.4 runs at each death record. A stub is never re-entered through its
own site while running [M]. Step 1 marks an impact inside a kill `nested` when the projectile or the hit object
carries the kill's dying bit (`obj+0x40 & 0x8000000`, set by `0x0044abd0` before its area damage [i]); a dying hit
object is forwarded (the stage would retire its record unseen), a dying projectile still skips for a live victim.
An impact whose row has an `EEDF_BREAKUP` element could remove the victim inside the
impact call; no reachable row has one [M], and the record is self-contained anyway. Missiles: an impact with a
class-10 source records a fixed `missile_hit` style (owner, launcher untraced [U]); a missile's own death records
`tier = missile` unless rule (b) of 2.4 applies.

### 2.8 Native Windows parity

The sites are X3AP.exe bytes verified at install (fail closed on any window mismatch), the writes are
`VirtualProtect` + `lock cmpxchg8b` / plain copy + `FlushInstructionCache` + read-back, the DLL is pinned with
`GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN)`: the engine-plumes redirect contract, documented Win32 only
[S]. The stage is documented D3D9 (FP16 blending checked at attach, lane sampling, `COLORWRITEENABLE`) [S, dropped
note 2.6]. The game runs natively (C:\X3) and **there is no Wine bottle on this host since 2026-10-11**
([host-suite.md](../verification/host-suite.md) "WSL2 host", "Wine-record-bound tests (2026-11-03)"): every fixture
of this note runs on the Windows host through WSL interop. The hook fixture uses the FL redirect fixture's harness
[M, platform-portability.md 2026-11-02]; the D3D fixtures use the native runner `verification/probe/native_host.py`
(being built with `run_engine_plumes.py --host native` as the pilot; results under
`verification/results/native-windows/<fixture>/` with a host identity block), invoked as
`python3 verification/probe/wine_lock.py python3 verification/probe/run_weapon_effects.py --host native` (the lock
still serialises the host's one fixture queue). **The native runner is a prerequisite of step 1.** Every stage figure
quoted from the dropped ledger (shell rim, ring, cut and timing numbers; the pass price) was measured under
Wine/wined3d on the Mac and is expected to differ on a real GPU: step 1 re-pins them natively and does not tune the
shaders to match the Wine numbers. Gap to record in platform-portability.md at step 1: none of the stage's costs
has a native measurement until that re-pin.

### 2.9 Rollback

`claim_call` inside the install window (backend-load path; `late_claim` refused after the first Present [S]);
read-back after every write; a family's sites are claimed in atomic-first order and restored together on any failure
(`readback_rolled_back` / `rollback_failed` as `engine_effects_patch::confirm` does [S]); `shutdown()` restores only
sites that still hold this module's call. `weapon_effects = native` is the user's rollback; an installed DLL without
the key behaves as native until a family is accepted. Instances created before a stub arms are untouched (arm once).

### 2.9.1 AP bullet batch scope `0x0046d0a7` → `0x004bfd40(scene, batch, material, view)` (reshade mode)

The bullet re-shade's scope site on AP (2026-11-13), the twin of FL's `0x097af8`
([ap-bullet-batch-site.md](../reverse-engineering/ap-bullet-batch-site.md) section 1; table
[weapon_effects_batch_site.h](../../src/proxy/weapon_effects_batch_site.h), site 8 of `install_at`). The site is a
cdecl call with four stack words and no register input. The caller pops `0x10`. EBX, ESI, EDI and EBP are live after
it and the callee saves them. EAX, ECX, EDX and EFLAGS are dead. The callee has no SEH frame and uses x87 and
`rep movsd`.

- **Window.** 19 bytes `0x0046d0a3..0x0046d0b6` (FNV-1a 32 `0x949efb9a`, pinned by `static_assert`, and compared with
  the installed EXE by the hook fixture's build audit). It is compared whole before any write; a mismatch is
  `bytes_mismatch_batch`.
- **Claim.** The call straddles the qword `0x0046d0a8`, so the claim is the plain copy inside the install window. In
  reshade mode the batch is claimed before the impact, all or none. `x3m_bullet_reshade_armed` is set once both sites
  are live. Shields mode leaves the site `not_used`.
- **Stub** (`x3m_weapon_effects_stub_batch_scope`). It publishes the batch's body id `[[esp+8]+0x0c]` through a saved
  EAX, then raises `x3m_bullet_scope` and the call count under `pushfd`/`popfd`. It re-pushes the four words under a
  12-byte pad, so the callee's ESP is the original call's minus 32 (the same alignment modulo 16). It calls
  `x3m_weapon_effects_continue_batch`, drops the pad with `lea`, lowers the scope and returns. There is no handler, no
  XMM, no x87 and no string instruction. EFLAGS, DF, the x87 stack and LastError pass untouched.
- **Rows.** `weapon_effects_patch site=batch va=0046d0a7 …`, `bullet_reshade_site … edition=ap` and
  `weapon_effects_reshade edition=ap runs=reshade|native reason=… muzzle=reshade|vanilla`.
- **Fixture.** The AP hook fixture runs the engine's own window bytes around the stub against a fake submit (one submit,
  then the loop's two). It also flips each of the 19 bytes, faults the read-back at either site, and covers shutdown,
  shields and the late claim ([weapon-effects.md](../verification/weapon-effects.md), "Re-shade on AP").

### 2.10 FL

X3FL.exe (SHA-256 `e7f3aad8…401dfbb8`, relocatable, `C:\X3FL`) runs the same families through the same installer at its
seven call sites, module base + RVA ([fl/weapon-effect-spawn.md](../reverse-engineering/fl/weapon-effect-spawn.md),
2026-11-08; the table is [weapon_effects_sites_fl.h](../../src/engine/fl/weapon_effects_sites_fl.h), the shims
`weapon_effects_hook.cpp`, the install `weapon_effects_patch.cpp::install_base`). The windows are compared with each
relocated absolute operand checked against base + its target and masked (the object tap's gate `0x1a82ec`, the muzzle's
TBullets slot `0x1a83c8`), the rest byte-compared (`memcmp`) with the recorded bytes, fail closed (the recorded bytes'
FNV-1a is pinned by a compile-time `static_assert`); six calls fit one aligned qword (`lock cmpxchg8b`), the object tap
straddles one (plain copy inside the install window). The ABI moved: FL passes the first one or two arguments in
ECX/EDX and keeps the rest on the stack, merges the two AP death calls into one site that serves both roles (the
area/other tag is rebuilt from AP's own branch, `core::fl_death_site`), inlines the AP muzzle wrapper, and keeps the
impact running total in EBP with no stored `absorbed` flag — it is derived from `ESI == 1` with a cleared remaining
shield-stage damage and EBP equal to T clamped to 50,000 for a piloted victim (`core::fl_absorbed`). The hull path
never runs the total reload at `0x0532e3` (it ends at `0x0532e7` with EBP = T + h), so the rule is exact except where
the clamp hides h = 1 for a piloted victim with T ≥ 50,000; that path needs every shield drained to 0 (`0x053155`), so
the drained-shield rule records it as a hull hit, and absorbed hits of the four ≥ 50,000-damage weapons (PSP, IONC,
PPC, IBL) on the player's ship stay staged shield hits (proof RVAs in the FL note's section 2.1). So each site has an FL entry shim that rebuilds the AP stub contract from the FL registers and frame,
with the skip return `xor eax,eax; ret`, CPU flags, DF and LastError preserved as on AP. The data differs only by the
game clock (cfg `+0x708`), the node body id (`+0x144`, a `0x148`-byte node read) and the relocated globals (cfg
`0x1a83b0`, type tables `0x1a83c8 + 4·class`, counts `0x1a8330 + 4·class` — the `Globals` count relation is an edition
parameter, bound with `core::fl_globals`; the body cache `0x1aa100` was bound for the walk until 2026-11-12); every
other `layout::` constant is shared. The shield
colour is portable with those offset and slot changes only (section 5 of the FL note). The ship-bubble hull walk,
its body cache slot and its FL LOD-extent guard (`weapon_bubble_extent`) were removed on 2026-11-12 (5.1.1
"Placement"). The FL weapon
census has the same 45 rows and ids as stock AP; the record fields differ only in PD's lifetime (1,800 vs 2,100 ms),
speed (355 vs 375 m/s) and damage (351/80 vs 468/120), and the PAC body's scale (8,099 vs 16,171; elongation 24.5 vs
24.7), so the classifier gives the same style for all 45 [M, `fl.json`, `classify_preview_out.txt`]; the classifier and
overrides carry over unchanged. The FL hook fixture (`weapon_effects_hook_fixture_fl.cpp`, 56 checks) exercises every FL
shim's slots with the FL frames and the FL EXE's window bytes; `edition_unsupported` is gone.

## 3. Record schema and the override file (question 2)

### 3.1 `weapon_effects::core::Event` (64 B, `src/proxy/weapon_effects_core.h`, Windows-free)

Shared by the DLL, the fixture (compiled in) and the lab (its JSON mirror, injected by the generator of 5.1).

| Field | Type | Source |
| --- | --- | --- |
| `role` | u8: `shield_hit`, `hull_hit`, `muzzle`, `bolt`, `beam`, `explosion` | the site (2.2–2.5) |
| `style` | u8: shape `tracer` / `slug` / `orb` / `wave` / `beam` (+ `burst` bit), or `missile_hit`; explosions carry the tier instead | the classifier (section 4) or the override |
| `tier` | u8: `light` / `medium` / `heavy` (weapons); `fighter` / `corvette` / `capital` / `station` / `asteroid` / `missile` / `burst` (explosions) | damage per shot; the victim's class |
| `calibre` | f32 0..1 | `clamp(log10(d / 300) / 2, 0, 1)` with d = max(shield, hull) damage per shot; for impacts the running total replaces d |
| `tint` | 3 × f32, linear | light colour when saturated, else the body's texture mean colour from the table, else white (4.2). As built `tint_rgb` (u32) and the other candidate narrowed to `alt_rgb332` (u8, RGB 3:3:2, 0 none: only the `tint_mix` override reads it) |
| `t0_game_ms` | u32 | `cfg+0x718` at the hook; **every age and bound is game time**: `age = now_game − t0`, so SETA shortens ripples, sparks and fireballs exactly as it does the engine's own effects |
| `lifetime_ms` | u16 | the style's timeline (shield patch `patch_lifetime_ms` = 2941, 5.1.1; sparks 250–500, fireball 1,000–2,000 by tier, beam = col 27 when flag 0x2000) |
| `owner` | object ptr, root node ptr, node handle u32, serial u64 (0 when `object_lifetime` is inactive) | the victim (impact), the ship (muzzle), the dying object (explosion, t0 only) |
| `shooter`, `slot` | object ptr, u8 | muzzle and bolts. Step 1 does not carry `shooter` (unread, F9); its four bytes hold `shield_q` (u16, current / maximum shield points × 65535, valid with `shield_read`), `patch_tier` (u8, fighter / corvette / capital) and `tier_source` (u8: ship class id, station, radius); a later step that binds the shooter widens the record. The `slot` byte is `detail` in step 1 (an impact's `EventDetail` bits: `detail_drained`, 5.1.1); a muzzle record carries its gun slot there |
| `table`, `row` | u8 (`bullets` / `missiles`), u16 | the live record's identity; the override and the table lookup key |
| `pos` | 3 × f32 | local to the owner (impact, muzzle) or world (explosion) |
| `axis` | 3 × f32 | projectile heading / muzzle orientation / dying object's velocity. As built for impacts `dir` (3 × i8, world, × 127 rounded): the projectile root node's row 2 (`node+0x40` third row, local z in world, 16.16); 0, 0, 0 when that node is not live or the row is zero (`dir_fallback`), taking the three bytes the narrowed `alt_rgb332` freed |
| `radius` | f32 | `obj+0xa4` of the owner (shell ellipsoid, fireball size) |
| `flags` | u8 | `absorbed`, `forwarded` (the native spawn ran), `stage` (the stage draws it; took the bit of `owner_retired`, which was set only on a record about to be released), `nested`, `override_applied`, `calibre_override`, `logged`, `shield_read` |

Per frame the stage derives, not stores: the owner's current object → world rows (`node+0x30`, `node+0x40` through
`engine_memory::read`, validated by `obj+0x70 == node && node+0x28 == handle` and, when active, the `object_lifetime`
serial; a failed validation retires the record), the view/projection of the resolve, and the age. The ring holds 1024
events (64 KB, a u16 free list; 256 until 2026-11-07, raised so a 60-80 ship battle keeps its afterglow). When it is
full the handler evicts in four classes, the oldest (lowest sequence) first within each, and counts `overflow`:
census-only records whose owner the stage did not draw in its last frame, the other census-only records, stage-owned
records (shield or hull: their native spawn was skipped) of an undrawn owner, the other stage-owned records;
`evicted_visible=` beside `overflow=` on the `weapon_effects_frame` row (both per row period) counts the evictions of
records not stamped undrawn. The stamp is the per-slot byte `Ring::hidden`, written by the stage, read by the handler with integer
compares inside the one victim scan it already made (no call, no visibility test in the hook): after both builds the
glue stamps every live record (`core::ring_stamp_hidden`, one hashed probe per record) with two bits, `hidden_undrawn`
unless its owner is in the frame's drawn set and `hidden_off_screen` unless in its in-view set. The view volume is
that of the jittered projection: in front of the near plane, inside the far plane m32 / (1 − m22) kept 5 % out (the
float recovery is about 2 % off with m22 near 1) and inside the four side planes (`stage::sphere_in_view`). The shield
stage counts an owner in view when its sphere of 2 × `obj+0xa4` (past the bubble) reaches that volume
(`stage::owner_in_view`); the hull stage when it is in reach by the cull's own test below. An owner past a stage's cap
is located for this through the drawn path's checks (`stage::locate_owner`: `obj+0x70 == node`, `node+0x28 == handle`,
not dying; then its position and radius, five reads, no rows), and a failed check counts it off screen; the frame's
located table (`core::VisibleOwners::located`, keyed by owner, node and handle) holds every owner either stage read, so
an owner is read at most once a frame for both stages. It is drawn when a build also kept it within its owner cap (16
shield, 32 hull; the hull stage only for an owner with a hull hit). Both stages pick their owners visible first: owners with a
record stamped off screen by the previous frame go after the others (on screen, or new this frame), then the newest hit
(the hull stage: an owner with a hull hit first), then the address, so an on-screen owner past the cap is still stamped
in view and competes again next frame. A record acquired since the last stamp carries 0, so a fresh hit keeps its
afterglow and a new owner counts as visible until the stage has judged it.

The hull stage's reach cull (`stage::build_hull`): a kept owner with a hull hit builds no elements when the sphere
about its current node position of 2 r plus its records' largest element reach, grown by the pixel floors at its far
depth, lies outside that view volume. The three terms: the attached reach (r_hit × the largest of the flash halo, the
heavy streak, the ring's rr + 3 w, the filament, the shimmer square, the glow and the ember, plus the puff's R_b), the
spark reach (per set the launch speed × (1 + spread) × (tau + the curl drift over the longest life) plus the floored
streak's world part), and the pixel floors (`reach_floor_px()`, 318 px: the floored spark streak and flash halo), so an
element there cannot reach a pixel. `record_reach` caches both world terms per record in `HullSlot`, from the floor the
record latches. A record of such an owner still builds when its own spark sphere reaches the view: the sparks ride the
latched contact point plus the owner's velocity at the latch (`p0 + v tau (1 − e^(−t / tau))`, on the segment p0 ..
p0 + v t), not the owner's current position. Never culled: the beam, repair and tug spots and the scorch marks (their
own stores, drawn from their owners' reads). The stamp takes the same verdict, so an owner whose sparks are on screen
counts as drawn and in view. The ring clears on the
resolve's cut verdict, a Reset, a sector change or load epoch (the plumes' rule [S]; step 1: Present samples the
cockpit's sector key through `sector_key::sample`, the loading state and the `object_lifetime` load epoch, and
`core::ScopeLatch` clears the stage-owned records on a load, a different known key or a different known epoch).

### 3.2 The generated table and the curated override

- **Generated at install / `x3m-regenerate`:** `<game>/x3m/weapon_effects.json` (the `engine_bodies.json` contract
  [S, mod-compatibility.md "Engine bodies"]), by `tools/effects/weapon_effects_table.py` sharing the census reader:
  per TBullets and TMissiles row index — the `SS_BULLET_*` id, the vanilla body's `z_over_xy` and lateral half-extent,
  the body diffuse texture's alpha-weighted mean colour (the `engine_bodies.py` colour machinery [S]), the TBullets
  table's decoded hash, row count and the generator's schema. The DLL reads only this file, keyed by `(table, row)`;
  a missing or stale file (row count differs from `*0x00607038`) disables the table lookups (elongation defaults to
  `slug`, tint to the light colour or white) and logs one `weapon_effects_table` row; it never disables the hooks.
- **Shipped in the repository and merged by the generator:** `tools/effects/weapon_overrides.json`:

```json
{"schema": 1,
 "by_id":    {"SS_BULLET_REPAIR": {"style": "beam", "tint": [0.3, 1.0, 0.3]},
              "SS_BULLET_TUG":    {"style": "beam", "tint": [0.7, 0.8, 1.0], "calibre": 0.1}},
 "by_table": {"7b4ab0a6cea2cb07": {"name": "Mayhem 3 (addon/12.cat TBullets)",
                                   "SS_BULLET_IRE": {"style": "tracer"}}}}
```

  `by_id` applies to any table carrying that id; `by_table[<decoded sha256 prefix>]` applies only to that exact
  table and wins. Known tables today: stock AP `be762076cdc38b03`, Mayhem 3 `7b4ab0a6cea2cb07`, FL `f0cb79fd63c46318`
  [M, census]. The generator resolves ids to row indices, so the DLL never sees names, and a mod with unknown ids
  simply gets the classifier. Users edit nothing; a user-level override is deliberately not a feature.

## 4. The classifier (question 3)

Inputs available at the hook from the live record: flag word (`+0x58`), shield and hull damage per shot
(`+0x44`, `+0x80`), light colour (`+0x5c..+0x64`), speed (`+0x54`), lifetime (`+0x50`), quad size (`+0x68/+0x6c`).
From the generated table: the body's elongation `z_over_xy` and texture mean colour. Rules, with the stock numbers
(`verification/results/weapon-effects-census/classify_preview.py` → `classify_preview_out.txt` [M]):

### 4.1 Shape

1. `flags & 0x6` (gun-tied `0x2`, kind B `0x4`) → **beam**. Stock 11 rows, Mayhem 2 (PBC, RL). Bit `0x2000`
   ("sustained", col 27 end time) is **not** a beam cue: PPC, IONC, IBL, PSP and both flak carry it and are bolts
   [M]; it is a parameter (the beam family uses it only with `0x2`).
2. `flags & 0x8` (fixed length, col 18) → **wave**: PSG, PD, TERRANANTIFIGHTER_FRAG (3 rows): a broad soft sheet.
3. else by elongation `e = z_over_xy` of the vanilla body: `e ≥ 4` → **tracer**, `1.5 ≤ e < 4` → **slug**,
   `e < 1.5` → **orb**. Thresholds: the stock bodies cluster at 5.4–24.7 (IRE 18.5, PAC 24.7, HEPT 9.4, CIG 10.6,
   PRG 7.3, ESR 5.6), 1.7–3.8 (EBC 3.8, IONC 3.5, PPC 2.9, GAUSS 2.3, EMP 1.8, IBL 1.7) and ≤ 1.46 (PD 1.46, MAM
   1.26, MINING 1.0, PSP 1.0); the nearest values to the cuts are PD 1.46 / IBL 1.73 and EBC 3.81 / Mayhem's
   4.53 (IC, GC, TBC, FBC, PAA), so no body sits within 0.03 of either threshold in any view [M]. A row with no table
   entry is a slug (the middle shape).
4. `+burst` when `flags & 0x1100` (area death `0x100`, fragments `0x1000`): the bullet's own end is a small burst at
   the death site (stock 13 rows; Mayhem none [M]).

Misclassification cost is low by construction: tracer/slug differ only in the capsule's rest length ratio and orb
drops the tail; the override fixes the rest.

### 4.2 Tint and calibre

- **Tint:** the light colour when `max ≥ 64` and `(max − min) / max ≥ 0.25` (stock 30 of 45 rows); else the body's
  texture mean colour (stock 15: the white-light and grey-light rows; Mayhem 36 of 38, since its lights are white
  [M]); else white. The MASS driver's grey (48,48,48) and CIG's white correctly fall to the texture.
- **Calibre:** `d = max(shield, hull)` per shot; bands `light < 3,000`, `medium 3,000–19,999`, `heavy ≥ 20,000`;
  continuous `k = clamp(log10(d/300)/2, 0, 1)`. Stock: light = every fighter weapon (IRE 220 … FBL 2,793), medium =
  PAL 3,969 … PBC 14,280, heavy = TERANTIFIGHTER 20,785, GAUSS 28,540, IBL 50,860, PPC 63,778, IONC 119,148, PSP
  252,310; Mayhem spans 428 (APC) … 94,130 (GKE) with the same bands [M, session query of cols 7/22]. For an impact
  the running total at `[esp+0x1c]` replaces d, so the ripple's amplitude is the damage actually applied.
- **Speed and lifetime** are parameters, not cues: the tracer's rest length is the engine's own `0.1 · speed` [I, §1
  col 11], the streak is `v · Δt_game · k_stretch`; the quad size (col 16/17, unit **[U]**) scales the capsule width
  once the observe flight pins its unit against the drawn instance extents.

### 4.3 Explosion tiers

At the death site, by the victim: class 7 → the TShips class id (`+0xc8`, col 52) mapped to **fighter**
(M5/M4/M3; also drones, satellites), **corvette** (M6/M8/TS/TP/TM) and **capital** (M1/M2/M7/TL); class 5/6 →
**station**; class 17 → **asteroid** (size from `obj+0xa4`); class 10 → **missile**; class 0 with flag `0x100` →
**burst** (calibre from the record). The numeric values of the `OBJ_SHIP_*` text ids are **unknown** (the census reads
them as strings from the file [M]); until the text-id reader `0x004ea440`'s table is disassembled the DLL tiers by
radius bands calibrated in the observe flight (`weapon_death` rows log class id, col 47 row and radius together), and
col 47 is the cross-check: stock rows 35 / 40 / 37 and Mayhem 600 / 601 / 602 follow the same three tiers with four
exceptions (2 M6 on 35, 4 TP and M8 on 40 [M, census]); the DLL logs `tier_mismatch` when its tier and the row's
tier disagree, and the override file can pin a class.

*Corrected 2026-11-06 (weapon-look.md §7): TM is a corvette, as `patch_tier_of_ship` (id 2141) tiers it; the text counted it a fighter.*

### 4.4 Resulting style per stock weapon [M]

| weapon | flags | z/xy | m/s | dmg/shot | style | calibre | tint |
| --- | --- | --- | --- | --- | --- | --- | --- |
| IRE | 0x0 | 18.5 | 1,162 | 220 | tracer | light 0.00 | light |
| PAC | 0x1 | 24.7 | 703 | 1,155 | tracer | light 0.29 | light |
| MASS | 0x60 | 6.6 | 739 | 45 | tracer | light 0.00 | texture |
| PRG | 0x1 | 7.3 | 1,560 | 680 | tracer | light 0.18 | light |
| EBC | 0x40 | 3.8 | 604 | 1,357 | slug | light 0.33 | light |
| FBL | 0x1000 | 1.9 | 359 | 2,793 | slug+burst | light 0.48 | light |
| HEPT | 0x0 | 9.4 | 431 | 1,995 | tracer | light 0.41 | light |
| IOND | 0x214 | 2.0 | 2,027 | 1,575 | beam | light 0.36 | light |
| PBE | 0x0 | 18.7 | 6,980 | 945 | tracer | light 0.25 | light |
| PD | 0x808 | 1.5 | 375 | 468 | wave | light 0.10 | light |
| EMP | 0x800 | 1.8 | 588 | 2,000 | slug | light 0.41 | light |
| CIG | 0x0 | 10.6 | 420 | 7,845 | tracer | medium 0.71 | texture |
| IPG | 0x1200 | 7.2 | 347 | 12,563 | tracer+burst | medium 0.81 | light |
| ESR | 0x400 | 5.6 | 498 | 2,671 | tracer | light 0.47 | light |
| MAM | 0x140 | 1.3 | 445 | 8,350 | orb+burst | medium 0.72 | texture |
| FLAK_ALPHA | 0x2100 | 18.7 | 7,051 | 13,860 | tracer+burst | medium 0.83 | texture |
| FLAK_CLUSTER | 0x3000 | 18.7 | 2,200 | 11,781 | tracer+burst | medium 0.80 | texture |
| PAL | 0x2 | 21.9 | 2,561 | 3,969 | beam | medium 0.56 | texture |
| PSG | 0x8 | 0.9 | 254 | 1,996 | wave | light 0.41 | light |
| TERANTIFIGHTER | 0x0 | 2.0 | 2,000 | 20,785 | slug | heavy 0.92 | texture |
| PPC | 0x2000 | 2.9 | 333 | 63,778 | slug | heavy 1.00 | light |
| IONC | 0x2600 | 3.5 | 387 | 119,148 | slug | heavy 1.00 | light |
| GAUSS | 0x40 | 2.3 | 353 | 28,540 | slug | heavy 0.99 | light |
| IBL | 0x2800 | 1.7 | 312 | 50,860 | slug | heavy 1.00 | light |
| PSP | 0x2100 | 1.0 | 321 | 252,310 | orb+burst | heavy 1.00 | light |
| PBC | 0x2 | 2.7 | 10,644 | 14,280 | beam | medium 0.84 | texture |
| TRIBEAM | 0x2 | 21.9 | 5,630 | 7,532 | beam | medium 0.70 | texture |
| FUSIONBEAM | 0x802 | 21.9 | 2,661 | 5,400 | beam | medium 0.63 | texture |
| MINING | 0x0 | 1.0 | 84 | 6,500 | orb | medium 0.67 | light |
| TUG | 0x2 | 3.4 | 4,000 | 0 | beam | light 0.00 | texture |
| DUMMY1 | 0x3000 | 3.7 | 25,000 | 11,781 | slug+burst | medium 0.80 | texture |
| DUMMY2 / KH_ALPHA | 0x2 | 3.4 | 5,994 | 700 | beam | light 0.18 | light |
| KH_BETA | 0x2 | 1.9 | 9,766 | 2,500 | beam | light 0.46 | texture |
| KH_GAMMA | 0x2 | 1.0 | 12,143 | 7,500 | beam | medium 0.70 | light |
| REPAIR | 0xa2 | 21.4 | 4,400 | 40 | beam | light 0.00 | light |
| FBL_FRAG, FLAK_*_FRAG (4) | 0x100 | 5.4–18.5 | 500–2,100 | 220 | tracer+burst | light 0.00 | light |
| TERRANANTIFIGHTER_FRAG | 0x8 | 0.9 | 254 | 220 | wave | light 0.00 | light |
| EMP_1 / MAM_1 / TERRANANTIFIGHTER_1 / IPG_FRAG | as the base row | | | | slug / orb+burst / slug / slug+burst | | |

*Corrected 2026-11-06 (weapon-look.md §7): the m/s column and the FL PD speed in 2.10 were col 11 / 1000; they are now col 11 / 500 (`engine_units_per_metre`, the catalogue's speeds).*

Counts: stock and FL 45 rows → beam 11, wave 3, tracer 8 (+7 burst), slug 9 (+3 burst), orb 1 (+3 burst); Mayhem 38 →
tracer 20, slug 9, orb 7, beam 2 [M, `classify_preview_out.txt`]. The Mayhem IRE classifies as a slug (its replacement
body is 2.3 long) where stock's is a tracer: the `by_table` override above restores the tracer if the flight prefers it.

## 5. The steps

Every step ships three things before its flight: the lab page (`tools/effects/weapon_effects_lab.html`, generated by
`tools/effects/generate_weapon_effects_lab.py`, which another agent is creating now as the vanilla half: it plays each
weapon's muzzle, projectile, impact and tiered death from the census, with a stub `renderOurs(record, t)` for our
side; each step fills that stub and its controls, with the header constants of `src/proxy/weapon_effects_core.h`
injected and checked with `--check` on the `generate_own_sun_lab.py` contract [S]), the host fixture
(`verification/probe/weapon_effects_fixture.cpp` + `run_weapon_effects.py --host native`, the engine-plumes shape
run through `native_host.py` [S]) and one flight whose capture rows are named here.

### 5.1 Step 1: shield hit (the brief for `implement-deep`)

**Goal.** A shield hit shows the hex patch of 5.1.1 (which supersedes the dropped design's Fresnel shell of 3.3),
placed by the impact hook, coloured by the hit object's shield fraction, aged in game time, in place of the native
impact sprite (coordinator decision 2026-11-06: the native spawn is skipped for the stage's shield hits, 2.2). Nothing else changes: hull hits,
bolts, muzzle and explosions stay native.

**Scope.**

1. `weapon_effects_sites.h` (impact site window bytes and the diagnostic inner sites), `weapon_effects_patch.cpp`
   (one stub, the handler of 2.2, the ring, arming, modes `native | observe | shields`), `weapon_effects_core.h`
   (`Event`, the classifier of section 4 with every threshold a named constant, the shield read and the patch tier
   of 2.2, the game-time age law, the patch look constants of 5.1.1).
2. `tools/effects/weapon_effects_table.py` (the generated table, install / uninstall / regenerate wiring as
   `engine_bodies.json`), `tools/effects/weapon_overrides.json` (empty `by_id`, the three known tables listed).
3. The stage: a `WeaponEffectsPass` fed by `Event` records (owner rows read at scene end as 3.1 says): per owner its
   live stage-owned hits (up to 64) light cells on the faces of their side (each face a planar lattice perpendicular to
   its owner axis, 5.1.1 "Placement"), one draw per face of up to 128 cells as one box whose pixels light the hull
   the depth lane holds under it where it falls in a cell of the draw's map, shared between faces by the triplanar
   weights of the lane's normal (faces turned away from the eye culled on the CPU); a flat decal of the same law for an owner without a usable frame; riding the resolve's step-0
   callback after the plumes callback (the `own_sun_with_plumes_` chaining pattern of `motion_output.cpp:2493–2507`
   [S]). Occlusion by the lane alone (unknown 11 of the dropped note was settled: the depth is unbound before the
   callback [M]).
4. The lab: **no new page.** The step fills the `renderOurs(record, t)` stub of the existing
   `generate_weapon_effects_lab.py` / `weapon_effects_lab.html` for the `shield_hit` role (the patch on a preview owner
   per tier beside the vanilla sprite the page already plays, the same law as the HLSL and the same grouping and cap as
   the stage) and adds the controls of 5.1.1: cell size and rings per tier, ripple speed, centre gain (the marker,
   replacing the core duration on 2026-11-06) and fade duration, stagger, edge versus fill, the three colour bands (thresholds and colours), the weapon-tint override,
   the SETA rate (the age law under warp), a hit-sequence player at 1, 4 and 12 hits per game second (with a shield
   preview and drain per hit); every control's default is the header's value, injected by the generator.
5. Rows: `weapon_effects_patch site= va= state= write=` at install; `weapon_event role= table= row= style= tier=
   calibre= absorbed= damage= forwarded= stage= nested= t0= … shield= shield_read= patch_tier= tier_source=` on
   capture frames and the first 64 events of a run; `weapon_effects_frame` per 300 frames (events, hits, skipped,
   forwarded, staged, patches, decals, retired, overflow, owned, stage_us, shield_read / shield_unread);
   `weapon_pos_check` in observe mode (the handler's `pos` against the inner tap's, max error).

**Acceptance (host, before the flight).**

- Hook fixture, run natively through WSL interop (the FL redirect fixture's harness): a synthetic image with copies of
  the impact window and the two inner windows; checks: window compare and refusal on one changed byte; the shields
  default skips a stage-owned shield hit (the callee not entered, the forwarded count unchanged) while a hull hit
  forwards, the forward path (`suppress_native` cleared) reaches the callee with every register and the stack as the
  call left them, and the skip path leaves EBX/ESI/EDI/EBP and the stack exactly, returning with the
  caller's `add esp`; the absorbed slot and the damage slot read from the synthetic frame; the shield rule
  (`absorbed`, flag `0x20`, class 0/10/other) on a table of cases; the shield read on a synthetic list
  (capacity-weighted against the HUD's own formula, 2e9-point shields, clamped energy, the sentinel, an index out of
  range, null extensions, the 64-node bound, an unshielded class, bit 20); the patch tier (class id, station, radius
  fallback); nested death retires the owner's hits; the ring's overflow order; `late_claim`; rollback on a failed
  read-back; the handler compiled with `-fno-exceptions`, no CRT import, no call and no x87 (the object audit); the
  per-event cost (target ≤ 30 µs per 50 events). Target: every check PASS, 0 failures.
- D3D fixture on the native host (`python3 verification/probe/wine_lock.py python3
  verification/probe/run_weapon_effects.py --host native`, through `native_host.py`; its record under
  `verification/results/native-windows/weapon-effects/` carries the host identity block), structural checks of the
  patch: per tier the footprint's cell count 1 + 3 rings (rings + 1), the lit area bounded by hexagon edges (past
  the outermost cell centres, inside their circumradius) and zero outside, lit on both sides of the hit in both axes,
  2 rings + 2 edge bands across it; the hit's centre cell over a ring cell by the marker law; the ring moving by whole cells and a
  cell lit when the front reaches its centre; the fade monotone and zero at the record's lifetime, one cell's fade
  duration against its hash; the colour bands at shield 0.9 / 0.5 / 0.2; a repeated hit re-lights (the newest clock,
  not a sum); an adjacent second hit resets only the shared cells and no cell is drawn twice; the CPU cap culls
  nothing lit; through the production builder the grid fixed on a moved and rotated owner and 12 hits/s without a pop
  (no pixel drops by more than one frame's fade step) on a fighter and spread over a capital's face (the region's
  cells overflow one map and spill into further draws; the one-draw eviction witness is recorded), two hits across a
  region seam never brighter than one, the stage's CPU cost; the decal fallback (an owner without an ellipsoid) draws on a
  hull and 0 over sky; the game-time law at SETA 6 measured (the last lit cell, 2840 game ms, ends after 473 ms of wall
  time); the marker cell
  keeps ≥ 0.95 through the production TemporalPass at 0 / 4 / 8 px per frame; a retired owner draws nothing; Reset,
  fault and program-create refusal; the pixel program in the low hundreds of slots. Timing at 1080p and 5120x1440
  (reported, not gated): two hits on a fighter, two on a capital at close range (one and two anchor regions), the
  worst case one owner can submit at once (four regions), a fighter under 12 hits/s and a full 64-cell capital map; the flight target is `stage_us` ≤
  0.2 ms at 5120x1440 in a fighter fight.
- `generate_weapon_effects_lab.py --check` clean; the lab's law at the hit point equals the D3D record's readbacks
  (`test_weapon_effects_lab`); `classify_preview.py` against the header's thresholds gives the table of 4.4 (a host
  test pins it).
- Dry run: `python3 tools/manage.py launch --dry-run --weapon-effects observe` and `… shields` forward
  `X3M_WEAPON_EFFECTS=` and the table path; `--vanilla` refuses the option.

**Flights.** Flight A (`observe`): the hooks record and forward; the capture must show `weapon_pos_check` max error
below 1 unit over ≥ 50 hits (settles the `+0x180` unknown), every `weapon_event` with `role=shield_hit` matching a
visible native shield sprite, `absorbed` and `shield=` consistent with the HUD shield bar, `patch_tier` /
`tier_source` against the ships hit, the hooks' cost in `weapon_effects_frame` (target ≤ 30 µs per frame at 50 events
[I]), and `weapon_death` rows with class id, col 47 and radius for the tier table. Flight B (`shields`): what the user
must see — on every shielded hit the hex patch (the native impact sprite skipped since the coordinator decision
of 2026-11-06, 2.2), the patch wrapping the hull at the hit point with cells sized by the target's class, the hit's own cell
brighter as the marker and one
ring from the hit point, each cell fading over 1.76 to 2.64 s of game time (2.2 s × (1 ± 0.2) per cell, 5.1.1;
SETA shortens it in wall time exactly as the engine's own effects), its colour
walking blue → yellow → red as the target's shield bar drops, no patch on a hull hit (still native), no patch
lingering after a kill or dock, and the frame time unchanged within noise (`stage_us` ≤ 0.2 ms at 5120x1440 in a
fighter fight). The user reports with F8 captures at a hit and after a kill.

**Lab controls** (what the user tunes before Flight B, each a header constant the generator injects): the list of
5.1.1. The agreed values go into the header, the fixture re-pins them, then the flight.

### 5.1.1 Shield-hit look (user decision 2026-11-05, supersedes the full shell of 3.3)

Agreed with the user after the step-1 build (`2fc8762ce`) and before its flight; this is the look the step flies.
Coordinator decision 2026-11-06 (after Run235 Flight B, superseding the user decision of the same day to keep the
weapon's native impact visible under the patch): in `shields` mode a shield hit the stage owns no longer forwards the
native impact sprite (the engine's `0x00413c40` is not called); the patch alone answers it. Hull hits (the shield
down, or a hull-through-shield bullet) keep the weapon impact, which becomes step 2's hull look; `observe` forwards
every hit, so a diagnosis flight still sees vanilla.

- **Geometry.** Not the whole-ship Fresnel shell. A **patch of hexagon cells on the same ellipsoid**: the hit
  lights the cell containing the hit point and its neighbour rings, so the pattern wraps the hull around the hit. The
  hex cell size and the ring count scale with the victim's **tier**; cell size is fixed in world units per tier,
  never per screen. As built: cell 3 / 6 / 16 m (doubled after Run235 Flight B: the cells read too small; the
  footprint radius doubles with them), rings 1 / 2 / 2 (7 / 19 / 19 cells per hit; the patch radius control is gone);
  the tier is the TShips class id for ships (8.1 of the spawn note), capital for docks and factories, else radius
  bands below 30 / 150 m [I].
- **Placement decision 2026-11-12 (user, FL 69 A): the walk removed; profile lathe for every ship; no profile:
  native.** Every ship draws its shield hit on the lathe built from its offline profile (`tables.ship_profiles`, schema
  3; "The lathe bubble" and "Ship profiles" below) whatever its tier. A ship without a usable profile (no regeneration,
  a modded ship, a stale table, another class at its row, a refused type read, a root axis scale outside (0, 256x]) gets
  no proxy shield effect: the impact hook forwards the game's native effect, as for a drained shield
  (`core::ship_profile_verdict`, the stage's resolve checks repeated at the hit with plain loads, at most seven, no
  call). Any other object that is not a station has no profile and stays native too. A hit the hook staged whose
  owner's profile the frame then refuses (the table, the type or the axis scales changed between the hit and the frame,
  a degenerate profile, a full cache) draws nothing, its native effect already skipped, and is counted in the stage's
  `lathe_*` refusals (`not_ship` with `lathe_no_profile`); a ship whose mesh is not built yet waits while the frame's
  `stage::lathe_builds_per_frame` (2) builds are spent (`lathe_deferred=`, its records kept in the ring, drawn on a
  later frame). A station (dock or factory) keeps
  the hull decal. The own ship in the cockpit view keeps the eye sphere, sized from its profile's bounds (no profile:
  native). The runtime hull walk, its cache, the box-fitted ellipsoid of fighters and corvettes, its fallback sphere and
  the capital hull decal for ships are gone, with `core::lathe_all_tiers`, `core::shield_bubble` and the tier routing
  of ships (`core::shield_surface` now reads only the station source). The tier stays where something else reads it:
  the cell size and ring count per tier (`core::cell_m`, `core::rings`, `stage::Tuning`), the death site's boom tier
  and the station source (the decal). `weapon_effects_frame` carries `ships_native=` (the period's ship hits forwarded
  for want of a profile) and its verdicts `ships_native_no_profile=`, `ships_native_stale_table=`,
  `ships_native_mismatch=`, `ships_native_unreadable=`, `ships_native_scale=`, and `others_native=`; one
  `weapon_ship_profile_missing rows= listed= more= profiles= tships_rows= list=row:class:verdict,...` row at a frame row
  names the TShips rows forwarded native first seen since the last one, across the session: the first eight distinct
  rows each once, then the distinct rows past them as the `more=` count (a row whenever it moved). The rows `weapon_bubble_walk`, `weapon_bubble_fit`, `weapon_bubble_extent` and
  the counters `bubbles=`, `bubble_fallback=`, `bubble_walks=`, `bubble_walk_failures=`, `bubble_held=`,
  `bubble_deferred=`, `bubble_clamped=`, `bubble_partial=`, `cells_key_dropped=` are removed. The walk's text below is
  kept as the record, marked superseded; the evidence stays in the ledger ([weapon-effects.md](../verification/weapon-effects.md), "Ship bubble, station decal" and the FL run 65 to 69 entries) and `verification/results/fl-run65/`, `fl-run67/`.
- **Placement: the ship bubble, the hull decal of stations and capitals (user decision after Run238 Flight E;
  supersedes the stand-off of Run237 Flight D, 21ad87836, reverted; capitals since FL 67 A, 2026-11-12; superseded for
  ships by the decision above).** A fighter's or corvette's patch lies on a smooth bubble around
  it; a station's and a capital's stay the hull decal below. The owner's newest record decides (`core::shield_bubble`):
  `PatchTier` fighter or corvette and `TierSource` not `tier_by_station` take the bubble; `patch_capital` (M0/M1/M2/M7/TL
  by TShips class, docks and factories, an unknown class in the capital radius band) and an unknown tier take the decal
  and run no hull walk, except the own ship in the cockpit view, which keeps its eye sphere (below) whatever its tier. The cells keep their model (the six face lattices, footprints, per-cell clocks, marker, ring,
  colour, fade, eviction, spills, seam shares; below).
  - *The lathe bubble (user decision 2026-11-12 on FL run 68: capitals get the bubble, the decal was "acceptable but not
    right"; supersedes the capital decal below).* A capital-tier ship (`patch_capital`: M0/M1/M2/M7/TL by class, an
    unknown class in the capital radius band, an unknown tier) whose TShips row has an offline profile draws its shield
    hits on a lathe of that profile and walks nothing (`core::shield_surface`; else the previous rule: fighter or
    corvette → walked ellipsoid, capital or station → decal). Fighters and corvettes keep the Run239 walked ellipsoid
    (orchestrator decision 2026-11-12: the accepted look, not changed without a flight); `core::lathe_all_tiers` (false)
    is the one switch that gives every tier with a profile the lathe. The own capital ship in the cockpit keeps the
    Run240 eye sphere (its radius from the profile's bounds x 1.15, no walk). *Superseded the same day (FL 69 A, the
    decision above): every tier with a profile takes the lathe, `lathe_all_tiers` is gone, and a ship without a usable
    profile stays native.*
    - *Stale table*: the profiles carry the TShips row count the generator read (`generated_from.ship_profiles.rows`);
      the impact hook compares it with the live count at every staged hit (`ships_native_stale_table`: the hit stays
      native) and the stage once per call (the bullet table's
      `stale_table` rule) and, when they differ or none was recorded, uses no profile (`lathe_stale`, one
      `weapon_lathe event=stale_table table_rows= engine_rows=` row per process). `weapon_effects_frame` carries the
      period's `lathes=`, `lathe_no_profile=`, `lathe_mismatch=`, `lathe_unreadable=` (a refused read or the row past the
      count), `lathe_scale=`, `lathe_degenerate=`, `lathe_cache=` and `lathe_stale=`; `weapon_effects_device` the lathe
      programs' `lathe_vs_slots=` / `lathe_ps_slots=`; every mesh build one `weapon_lathe event=built slot= serial=` row
      (a slot rebuilt after an eviction logs again).
    - *Profiles* (`weapon_effects.json` schema 3, `tables.ship_profiles`, tools/effects/ship_profiles.py, "Ship
      profiles (2026-11-12)" below): per TShips row the hull along the model's z in scene units, 32 slices of (cx, cy,
      hx, hy). One scene unit is one engine unit of the owner frame; the lathe applies the root node's axis scales
      `[node+0x80 + 4 i]` (16.16) as the walk does to its children's offsets, then `world_scale`. Verified against the
      production walk over the FL 67 Titan hull part (half-extents 151,433 x 166,374 x 528,422 at z +17,329): the real
      Titan profile's bounds equal the walk's box within 0.001 m on every axis (x the widest slice 16, y the fin slices
      11 and 17, z the ends), factor 1.0000 [M, native fixture `LATHE case=units`]. The reader
      (`table::parse`, last argument) keeps them in one heap block (`core::ShipProfiles`, 1024 entries, about 0.55 MB)
      keyed by row, allocated only when the table carries a `ship_profiles` array (an older table allocates nothing),
      and logs one `weapon_ship_profiles` row at load (event loaded / absent / allocation / none, kept, dropped, unknown
      class tokens, the recorded TShips rows).
    - *Type* (`stage::resolve_lathe`, six engine reads per owner and stage call): class word `obj+0x48` = 7 (ship);
      subtype `obj+0x4a` (int16) is the TShips index, the row `read_tier` takes its class from; the type record's class
      id `[ship table + 0xdb8 sub + 0xc8]` must equal the profile's (its col 52 token through the engine's text-id
      table, weapon-effect-spawn.md 8.1), else the ship falls back (`lathe_mismatch`: a table of another tree or edition;
      one `weapon_lathe event=class_mismatch` row per process); axis scales outside (0, 256x] also fall back.
    - *Mesh* (`stage::build_lathe`, once per type and scale in `stage::LatheCache`, 32 entries, the least recently used
      not drawn this frame evicted, about 0.7 MB static; one `weapon_lathe event=built` row per mesh): 32 rings at the
      slice centres (an empty slice interpolated from its neighbours, half-widths floored at 1/16 of the widest) x
      `bubble_scale` 1.15, 24 segments each (the ellipse cx + hx sin φ, cy + hy cos φ, the seam at -y), closed at each
      end by a half ellipsoid on its end ring of depth half a slice + 0.15 x the end ring's mean half-width (at least
      0.35 of it; 0.15 x half the ship's length first made a 192 m needle on the Titan's 4 m nose): four rings at 22.5°
      steps, the last the tip, so the bow and stern are rounded. 40 rings,
      1000 vertices (`renderer::WeaponLatheVertex`, 20 B).
    - *Mapping* (`stage::map_lathe`): a hit inside the body takes its z's ring pair, their (cx, cy, hx, hy)
      interpolated, and is projected laterally onto that ellipse in its normalised space, not radially from the centre;
      past an end ring it maps radially onto the dome's ellipsoid. The FL 67 witness on the profile's own surface (side
      band |z| < 300k, 114 points): the walk ellipsoid moved the contacts a median 279 m (max 629 m), the lathe 22 m
      (max 45 m: the 15 % inflation of the slice's lateral offset); nose and tail 3.3 m (the walk ellipsoid's 153 m)
      [M, fixture `LATHE case=placement`].
    - *Pattern*: one chart per owner instead of the six face lattices. u is the arc length along the lathe's mean
      outline: per ring one u, its step from the previous ring the mean of its 24 columns' distances, so a step between
      slices keeps the cells' length on average. Each column's own arc length was tried first and rejected: neighbouring
      columns accumulated up to 188 m of u apart past the Titan's fin slices, shearing the cells; u = z stretched them
      1.5 to 1.7 x on the slice 9 to 10 ramp. In the slice-9 band the mean outline is within ±10 % of the columns' true
      lengths and exact across them; into the fin band (slice 10 to 11) ±50 %. v is the perimeter fraction x the ring's scale x the period (the median
      body ring's perimeter rounded to whole lattice periods, so the hex lattice closes around a full-width ring); a
      ring narrower than 0.6 of the median perimeter (`stage::lathe_min_width`) spans that share of the period, so the
      cells' shrink across the hull stops at 0.6 toward the nose, the tail and the dome tips (2026-11-12 look fixes,
      `verification/results/lathe-bubble/titan_side_lathe.png`, `titan_bow_{side,ahead}_lathe.png`). A ring wider
      than the median keeps the full period, so its cells stretch across the hull by its perimeter over the median (the
      Titan's slice 16 1.76 x, the fin slices 11 and 22 1.53 x and 1.37 x [M, the production mesh offline]). The
      cells, clocks,
      marker, ring, fade, eviction and spills are `fill_region`'s on that plane (anchor 0), so the cells keep their 16 m
      along a 2 km hull. A hit within a footprint and two cells of its ring's seam is placed once more one ring span
      across it, so the cells on both sides of the seam carry it (the cell hash wraps the key row by the period, which
      matches only on full-width rings). One region per owner: a lathe owner reserves one draw of the frame's 64 (a face
      owner six), resolved before the reservations.
    - *Draw* (`weapon_lathe_vs/ps.hlsl`, the cell lookup of `weapon_bubble_ps.hlsl`): each draw rasterizes the band of
      rings and columns holding its cells (`stage::lathe_band`; the indices built per draw, the mesh passed from the
      band's first ring through `DrawIndexedPrimitiveUP`), CULLMODE NONE; VFACE x the owner frame's determinant sign
      picks the near gain (1) or the far gain (0.6); the lane occludes as for the ellipsoid. The look is the fighter
      bubble's (shield-hit-accepted-look). Attach now needs 1872 primitives and 1000 vertex indices per draw.
    - *Open: boxy hulls.* A slice's ellipse on its half-widths passes inside a rectangular cross-section's corners (a
      corner lies at √2 of the half-widths in the ellipse's normalised space, against the 1.15 inflation), so on a boxy
      hull (TS, TL freighters) a corner hit's lateral projection can land inside the hull, where the lane hides it. Not
      fixed now; a profile carrying the slice's corner reach (or an inflation of √2 x 1.15 on the diagonals) would.
  - *Capitals take the decal (FL 67 A, main session decision 2026-11-12; since the lathe above only a capital whose row
    has no profile). Superseded 2026-11-12 (FL 69 A): a capital without a profile stays native; the decal is the
    stations' only.* A box-fitted ellipsoid cannot fit a long hull
    with fins at any scale. The Argon Titan (M2, radius 545,751) is one hull part whose stored extent is the per-axis
    coordinate maximum (x 151k at one z-station, the others 46 to 83k; y 166k from the fins), so its walked bubble stood
    about 174k x 194k x 608k engine units (x 1.15) far off the sides; the radial map pushes a hit outward by 1/|q|, and
    the Titan's 134 logged hits had |q| 0.22 to 0.52: median displacement 621 m, 600 m of it along z, side hits sliding
    toward nose or tail (the logged hit positions against the bubble inferred from the scene,
    `verification/results/fl-run67/titan_hit_mapping.py` on the FL run 67 log). Nose and
    tail hits fitted only because |q| is near 1 there. The hull decal lies on the hull at the contact point, as on
    stations (the accepted look: ship bubble + station decal, 2026-11-06); fighters and corvettes keep the bubble. The
    decal reads no hull extents, so a capital owner walks nothing (no slices spent). Its cells are the capital tier's
    (16 m, 2 rings), the station's values.
  - *Ship profiles (2026-11-12).* The offline input for a lathe bubble that hugs a long hull: `weapon_effects.json`
    schema 3 carries `tables.ship_profiles` (`tools/effects/ship_profiles.py`, written by
    `tools/effects/weapon_effects_table.py` and x3m-regenerate), one entry per TShips row of the edition (AP
    `addon/types/TShips`, FL `addon2/types/TShips`) keyed by `tships_index` (0-based data row), with `scene` (column 16)
    and `class` (column 52) as written, `units` `scene`, `axis` `z`, `z0`/`z1` the whole hull's z extent, 32 `slices`
    `[cx, cy, hx, hy]` (slice i spans z0 + i (z1 - z0) / 32; lateral box centre and half-widths of its points; an empty
    slice interpolates its neighbours), `radius` (largest point distance from the scene origin) and `points`. Geometry:
    LOD 0 of every non-effect part body the scene places, at its first scene key (position, angle-axis rotation in turns,
    `F` parents composed), points x LOD-0 value / 65536, faces under 0.1 % of the body's largest dropped (as
    `engine_bodies.py`). **One scene unit is one engine (world) unit**: the unit of a scene key position, and for a part
    of unit node scale the runtime's half-extent ((ext x value + 0x8000) >> 16) x axis / 0x10000 (`hull_scale16` with
    0x10000 = 1.0, the removed walk's) is the same number; 500 units per metre; the bubble's view units
    are these x `core::world_scale`. A row whose scene does not load is listed under
    `generated_from.ship_profiles_skipped` with its reason and has no entry (that ship's hits stay native). The Argon Titan (FL
    row 11) [M]: z -511,093 to 545,751 (the walk's hull part 17,329 +- 528,422), radius 545,994 (scene_radius 545,751),
    widest slice 16 (z 17,329..50,355, amidships) hx 151,433 (the stored x extent) hy 108,901, the slice half-widths 3.8k
    to 151k in x against the walk's single 174k (`verification/results/ship-profiles/titan_profile.py`; before the
    hull rules below).
    *Hull rules (FL 69 A, 2026-11-12).* The bubble hugs the hull proper; masts, antennae and spars may stay outside.
    (a) A part whose own lateral extent (max of its placed points' x/y bbox half-widths) is under 6 % of the scene's
    is dropped (none when that would drop all); (b) after one slicing, z0..z1 is trimmed to the first and last
    non-empty slice whose hx x hy is at least 1 % of the widest slice's, and the kept points are sliced again (32);
    an internal thin slice (a neck) stays. Then the half-widths are eased: an interior slice takes the running max
    over +-1 slice, then the 3-tap mean, never below its raw width (fins ease in over about two rings); an end slice
    takes max((2 raw + neighbour's raw) / 3, raw), so the domes keep about their own width (`--raw-profiles` /
    `smooth=False` turns the easing off).
    Entries add `trim_lo`/`trim_hi` (scene units cut at each end), `parts_excluded` and `smoothed` (schema 3, keys
    the reader skips); `generated_from.ship_profiles` adds `thin_parts_excluded`, `part_fraction`, `trim_fraction`,
    `smoothed`. The trim fraction is 1 %, not the 3 % first proposed: the Titan's result is the same anywhere from
    0.5 % to 3 % (its needle end slices are 0.06 % and 0.43 % of the widest, the next ones 3.0-3.8 %), while 3 %
    also cut genuine narrow noses (1-3 %) of fighters with wide wings; 5 % cuts the Titan's nose structure to z
    446k. The part fraction is insensitive between 4 % and 10 % for the Titan. Titan [M]: z -477,889 to 500,606
    (trim 33,204 stern, 45,145 bow), 14 parts dropped (8 small turret and 6 camera dummies), end slices raw 13.9k x
    44.7k / 19.5k x 25.9k, eased 33.1k x 47.8k / 19.5k x 26.3k (were 9.9k x 7.1k / 3.8k x 2.9k). FL over 403
    profiles [M]: 3,540 thin parts dropped, z changed on 243 rows (122 by more than 5 % of the length: 59 M3-M5
    fighters, 18 M1/M2/TL; the largest are Split M5 variants whose front half is a flat blade, hy 1-10); per-slice raw vs eased and the sweep in
    `verification/results/ship-profiles/hull_rules_fl.txt` (`hull_rules.py`; Titan entries `titan_profile_raw.json`,
    `titan_profile_smoothed.json`; `titan_profile.json` stays frozen as the pre-rule profile the native fixture's
    `titan_profile()` embeds).
  - *Superseded 2026-11-12 (FL 69 A: the walk removed; the evidence stays in the ledger ([weapon-effects.md](../verification/weapon-effects.md), "Ship bubble, station decal" and the FL run 65 to 69 entries) and `verification/results/fl-run65/`, `fl-run67/`). The walk, its cache, its rows and counters below
    are no longer built; the hit mapping (`bubble_point`) and the draw (`fill_bubble`, `bubble_box`,
    `weapon_bubble_ps.hlsl`) stay as the eye sphere's.*
  - *Bubble.* An ellipsoid in the owner frame from the hull bounds (render-node-bounds.md 6.1, the reads and guards of
    `ship_avoidance_walk_core.h` `hull_children` with a signed box: `stage::hull_bounds` in
    `src/proxy/weapon_effects_bubble.h`, every descendant of the root node at most 3 levels down and 1024 nodes, 1024 per
    list, 64 chain steps, 1..16 LOD records, each node read as one `node_walk_bytes` block (0x144 AP, 0x148 FL), the engine's lookup `0x004863c0`
    never called; below a rotated node a part's offset widens the box by its length on every axis and its extents are
    their length): centre the box's centre, half-extents its half-extents x `core::bubble_scale` 1.15, each floored at
    `core::bubble_floor` (1/16) of the largest and cut to `bubble_scale` x the object radius (obj+0xa4: a guard against a
    bogus part, as ship_avoidance's width takes w <= r, at the bubble's own scale so a hull that reaches the radius keeps
    its cap past the nose and tail; review F5). A failed walk (any guard, no part) takes a sphere of the object radius
    about the root node; the sphere is also the answer while a walk is unfinished. No tighter engine bound exists to
    fall back on: the object carries only the sphere radius `obj+0xa4` (sector-collide.md: "a single sphere radius"; the
    collision OBBs belong to the models' BVHs, not to the object, and FL's class-7 per-axis box is `3r + 250000` derived
    from the radius), so no per-axis object extent is read.
    The budgets (FL run 65, 2026-11-10): 128 nodes / 256 per list were sized against fighters; the Argon Titan's walk failed every
    attempt (its stock scene is 98 collision bodies in up to 152 nodes, fl/ship-enclosure.md section 3) and drew the
    radius sphere, about its half-length, so side hits were pushed about 3.4x outward. Now 1024 nodes and 1024 per list
    (the list limit never tighter than the budget). One 1024-node walk costs about 110 us through `engine_memory::read`
    (measured: best 110.0 / mean 114.6 µs, 8,325 reads), so the walk is resumable (`stage::HullWalk`: an explicit
    3-level stack, `hull_walk_start` / `hull_walk_step`) and runs at most `stage::hull_slice_nodes` (256) nodes and
    `hull_slice_reads` (2,400) engine reads per slice (no node begun past the read bound; a node reads at most 84: its
    block, the bucket, 64 chain steps, the model head, the LOD array, 16 records), one slice per owner per stage call:
    a fighter or the Titan finishes in its first slice, a 1024-node hull in four frames. A resumed slice re-reads every
    level's link (the parent's `+0x0c` or the previous sibling's `+0`) and fails the walk (`changed`) when it no longer
    names the pending child, so a list relinked between frames is never followed from a stale position; a changed walk
    restarts from the root on the owner's next stage call (no retry hold; its row says `restarted=1`). A walk parked
    (not sliced) for more than two game steps or 500 game ms, whichever is larger, restarts from the root. A fitted
    centre farther than the object radius from the root node is a misfit and fails the walk (`box`). At
    most `core::bubble_walks_per_frame` (2) slices run per stage call (review F4); an owner past them draws on the sphere
    until a later frame walks it. A failed walk logs one `weapon_bubble_walk edition=<ap|fl> state=failed guard=<cache|read|list|budget|chain|model|lod|extent|scale|bounds|empty|changed|box>`
    row with the owner, root node, radius, `nodes=`, `parts=`, `depth=` (the level it failed at), `deepest=`, `slices=`,
    `value=` (the guard's offending value: an address, a count, an extent or a scale), `restarted=` and `suppressed=`;
    `stage::WalkRowLimit` limits on wall-clock ms from the stage's QPC (SETA does not multiply rows): an owner once per
    2000 ms, 8 rows per 2000 ms in all. A finished walk logs one `weapon_bubble_fit edition=<ap|fl> owner= node= tier=
    radius= parts= nodes= slices= lo= hi= c= e= clamped= now= suppressed=` row (FL 67 A: the hull box, the bubble's centre
    and half-extents, all in engine units, so a misfit shows without a scene reconstruction), under its own
    `WalkRowLimit` (an owner once per 2000 wall ms). `weapon_effects_frame` counts `bubbles=` (owner frames on a bubble),
    `bubble_fallback=` (of them on the sphere, unfinished walks included), `bubble_walks=` (slices), `bubble_walk_failures=`, `bubble_held=` (a failed
    walk's retry held back), `bubble_deferred=` (past the frame's walks), `bubble_clamped=` (a half-extent cut to the
    radius) and `bubble_partial=` (slices that left their walk unfinished), and `cells_key_dropped=` footprint cells
    past the packed cell key's bound (+-2048 cells from the face's origin; never lit, review F6); `weapon_patch` logs
    `bubble=`, `bubble_c_m=` and `bubble_e_m=`.
  - *The walk's cache* (`stage::BubbleCache`, one per process on the game thread): 64 entries keyed by owner, root node,
    handle and the root's first child with its body id (render-node-bounds.md 6.1's key; two reads per owner a frame;
    review F7) with the object radius walked at and an unfinished walk's state (about 20 KB in all); a radius or child
    change walks again, an unfinished walk continues on the owner's next frame, a failed walk is held for
    `core::bubble_retry_ms` (2000 game ms, the fallback meanwhile) and then retried, a miss takes an empty entry or the
    least recently used, a ring scope change (`weapon_effects::scope_changes()`: a load, a sector change, a load epoch)
    clears all. CPU data naming engine objects only: a device Reset leaves it valid.
  - *Hit mapping.* The recorded hit point p (owner frame, on the hull) in the bubble's normalised space q = (p - c) / e,
    Q = q / |q|, B = c + e Q (a hit at the centre takes +z). The hit lights both faces of every axis, as the hull decal
    does (review F1: no rule on Q's sign, so a hit a hair either side of an axis plane lights the same cells); its cell
    on a face is the lattice cell of B's projection, its footprint, clocks and anchor as below (`stage::fill_bubble` runs
    `fill_region` on the B points). The opposite sheet's cells lie outside their anchor's depth window and `bubble_box`
    drops them (a nose hit never draws on the tail); for the CPU cost a hit whose window B.a -+ the extent misses the
    face's sheet within the share region is not fed to that face, and a face no hit reaches is skipped (it could keep
    no cell).
  - *Draw.* One box per face draw as the decal's (the same vertex program and back-face rule): in the plane the cells'
    hexagons clipped to the bubble's projection where the face can take a share, along the axis per cell the depths of
    the bubble's sheet on the face's side over the cell's rectangle within that region (a normal within the triplanar
    cut, 57.26 degrees with the margin, has rho^2 <= (1 - c^2) E^2 / (c^2 e_a^2 + (1 - c^2) E^2) in the plane's
    normalised coordinates, c = cos 57.26 deg, E the larger in-plane half-extent), intersected with the cell anchor's
    depth -+ the extent (the decal's footprint rule: a face nearly edge-on to a hit cannot carry its cells round the
    bubble); a draw with no such cell is dropped (`stage::bubble_box`). No face cull (the far side faces away from the
    eye); the screen-area budget and the 64 draws apply. Per pixel (`weapon_bubble_ps.hlsl`) the view ray (its view z is
    the ray parameter t) meets the bubble analytically in its normalised space, the ray linear in the raster (per-draw
    constants computed in double): the chord's middle and half chord (robust for a far eye) give the near and far
    points. A point draws where it lies in front of the near plane, nearer than the lane (the hull and anything in front
    hide it, the sky does not; an occluder texel without a valid depth hides) and inside the draw's box; its outward
    normal normalize(q / e) gives the face's triplanar share by the decal's rule (sharpness 8, floor 0.3, only where it
    points along the axis); its cell comes from the same constant map. The near side draws at full radiance, the far side
    at `core::bubble_far_gain` 0.6; where both draw they add. One lane fetch per pixel (the occlusion), no normal
    rebuilt, nothing placed from the lane. The far side shows around the hull's outline (the chase camera behind a ship
    hit on its nose), the hull hiding what lies behind it; with the eye inside the bubble (the cockpit, a close chase)
    the near point lies behind the eye and only the far side draws. Hull parts that reach past the bubble (a box corner
    lies at sqrt 3 of the half-extents) hide it there.
  - *The eye inside a box* (review F6): with the own ship in the cockpit view a face draw whose box contains the eye
    rasterizes the whole screen (eight hits around a cockpit: 258 us at 5120x1440 against 178 us for the close capital).
    Not culled: the cull would hide exactly the own ship's patch about the eye that the user decided to keep at
    `own_cockpit_gain` (Run236 Flight C, below), and the frame's screen-area budget already bounds it; the cull stays an
    option for the user (ledger).
  - *Measured* [M] (ledger "Ship bubble, station decal"): the lit near side's centroid lies 0.28 to 0.49 cells from B on
    the narrow and flat hulls from every side, nothing past the footprint, the bubble near B covered; a hit either side
    of an axis plane lights identical images; the chase camera's nose hit draws 1212 far-side pixels around the outline
    (the cap 1.8 m past the nose) and none on the near side; the eye inside draws no near and 147479 far pixels; GPU
    0.93 to 1.02 x the 6720724d3 rows; a 127-part walk 8.8 us through the fixture's reader, 12.0 us through
    `engine_memory::read` (894 reads), cached in the steady state.
- **The station's hull decal: the screen-space patch (user decision 2026-11-06 after Run236 Flight C; supersedes the
  per-face lattice at the hit point with its travel region, per-cell depth, depth cache, soft fade and seam planes;
  since the bubble above it draws the stations only, as built in 6720724d3, and since FL 67 A the capitals too; since
  2026-11-12 the stations only again).** The patch is a decal laid on the hull
  through the depth lane; nothing chooses a face from the bolt (`Event::dir` stays
  recorded for the `weapon_event` row and is unused). The cells keep their model: per owner six faces (+-z, +-x, +-y,
  each a planar hex lattice perpendicular to its owner axis, fixed in the owner frame, `stage::anchor_frame`), a hit
  lighting its footprint (the cells within `rings` of its projection's cell, each when the ring front reaches it) on both
  faces of every axis (review F1: the side of the owner's origin decides nothing, so an upward surface below the origin
  plane, a wing's underside or an off-centre model origin light; a thin wing shows the patch on both sides), every cell keyed (face, lattice cell) with its own clock (the newest hit's: a re-hit resets,
  never adds), the hit cell the marker, the newest four hits the ring, the eviction and spill draws as before. Each
  cell carries an anchor, the covering hit nearest its centre in the plane. A draw is one face's cells (up to 128;
  more spill into further draws, Morton chunks) as one box in the face's frame: the cells' hexagons in the plane and
  their anchors' depths -+ the extent (the reach plus two cell circumradii) along the axis; only the box's back faces
  rasterize (the cull mode follows the sign of the owner frame's determinant in view), so each pixel is shaded once
  per box, the eye inside it too (coordinator decision 2026-11-06: the per-cell columns of the first screen-space build
  cost 2.5 to 9 x the planar patch's GPU time). Per pixel (`weapon_shell_ps.hlsl`) the lane's view depth (sky: nothing)
  rebuilds the scene point through the jittered projection and the owner's rows (p, owner frame); a point outside the
  box draws nothing; the pixel's cell is p's projection on the face's lattice, looked up in the draw's cell map (pixel
  constants: 128 packed keys in ascending order, their clocks, and each group of sixteen keys' range, so a pixel scans
  the one group that can hold its key). The normal is the cross product of the rebuilt point's screen derivatives
  (ddx, ddy),   turned towards the eye; the face's share is the triplanar weight |n_a|^8 / sum |n_k|^8 less a floor of 0.3,
  renormalised, where the outward normal points along the face's axis (`core::triplanar_sharpness`,
  `core::triplanar_floor`): of each axis one face takes a point, the faces' shares sum to one (two lattices never add
  over a point: the seam semantics), and a face tilted past about 48 degrees takes nothing, so the anchor's depth,
  which moves along a steep face as hits arrive, never decides a pixel that still shows (the capital sphere's pops
  without the floor); a pixel with no share skips the lookup. The face cull (CPU, `stage::face_culled`): a draw whose
  axis points away from the eye at all eight box corners by more than 90 degrees plus the floor's cut (a face takes a
  share only while tan^8(tilt) < 8 (1 - f) / f, 55.26 degrees, plus 2 degrees of margin: `core::face_cull_sin` 0.841)
  is not drawn and counts as `faces_culled=` in `weapon_effects_frame` (the corners decide: the culled set is a convex
  cone about the axis; the stage tests the hits' grown box before building a face's cells, then the exact box); the
  frame takes 64 draws (`max_patches`, static off the game thread's stack, 126 KB since the bubble's fields). The screen-area budget (review F4,
  `core::screen_budget` 8): the drawn boxes' projected areas summed in screens; past 8 the draws of the owners whose
  newest hit is the oldest are dropped (`area_dropped=` in `weapon_effects_frame`), which bounds a frame's patch GPU
  cost near 8 full-screen boxes (see the ledger). The patch
  therefore lies on whatever hull is under the hit, from every approach direction (Run236's patch on edge from above
  and below is gone); a hit on the far side of its owner is hidden by the owner's own hull, as the hull hides anything
  behind it. Normal choice [M] (fixture `NORMALS`, the CPU twin of three hull lanes): the quad derivatives and a
  4-neighbourhood (per axis the neighbour with the smaller depth step) agree inside a surface (mean 0.14 to 0.44
  degrees, p99 1.3 to 4.7 against 1.5 to 5.5), the derivatives miss the dominant axis on about half the silhouette
  pixels (1134 of 2191 on the narrow hull's top) where the neighbourhood misses none, and the neighbourhood costs four
  more lane fetches per lit pixel; the derivatives were kept, silhouettes take the eye's direction. Keys stay below the 2048 bias (about 1000 cells from the owner's
  origin along a lattice axis: 15 km at a fighter's cell, 80 km at a capital's).
- **Drained shield (user report after Run237 Flight D: "shield hit effects are rendered even when the ship doesn't have
  shields left").** The engine absorbs a weak bolt on an empty shield with the recharge trickle and sets the absorbed
  flag (`C:\X3\x3m.log` of Run237 Flight D, `verification/results/weapon-effects/drained_rows.py` [M]: of 776 logged
  `shield_hit` rows with a read shield, 10 at post-hit 0.0000 to 0.0005, all TBullets row 29 at damage 428, and one at
  0.0034, row 11 at damage 12030, on two owners), so the hit is a shield hit by the engine's rule while the HUD shows
  0 %. Rule: an absorbed hit whose
  read post-hit fraction (`Event::shield_q`, `flag_shield_read`) is below `core::drained_shield_q` (0.5 % of 65535:
  327) is a hull hit: the hook forwards the engine's spawn (the native impact sprite), records `role_hull_hit` with
  `Event::detail` `detail_drained` (the byte the impact never used; a later muzzle step's gun slot shares it), stages no
  patch and counts `drained` (`weapon_event drained=1`, `weapon_effects_frame drained=`); an unread shield keeps the
  engine's rule. Only the post-hit value is known, so a hit that drains a meaningful shield to zero in one blow is
  drained too (it shows the native hull impact, not the patch): accepted. Integer compare in the handler, which keeps
  no call and no x87.
- **The own ship in the cockpit view (user decision 2026-11-06, Run236 Flight C).** In the cockpit view the own
  ship's patch sits around the eye and covers the view, so its draws take `core::own_cockpit_gain` (0.35, a header
  constant, no setting) of their radiance (`stage::dim_own_cockpit` scales the patch colour; ONE/ONE, so the colour
  is the intensity). Own ship: the patch's owner node is `resolve_own_ship`'s root node (cached per frame). Cockpit:
  the chase camera's last active-control cockpit visit read view mode 1 (`cockpit+0x150`, the value its
  `InternalView` refusal tests; `chase_camera::stats().view_mode`, read only when a patch belongs to the own ship, no
  new engine read) and that visit is this frame's or the previous one's (`stage::CockpitClock` stamps the frame the
  visit count last moved, review F2: a camera no longer visiting is stale and dims nothing). The chase view and every other ship are unchanged; without the chase camera installed
  (`camera = vanilla`) the view is unknown and nothing is dimmed. The `weapon_event` row keeps `dir=` for diagnostics
  and no longer logs `region=` or `travel_region=` (no rule uses them). The `weapon_patch` row logs `own=`, `cockpit=` and
  `gain=` (it replaces `is_view_object=`). Decals (an owner without a frame) are not dimmed.
- **Cell-map model (user decision 2026-11-06, supersedes the per-hit disc and the four-hit slots; its geometry,
  stereographic planes, caps, the pixel-constant map and the radial seam share, is superseded by "Placement" above,
  its clocks, footprints, spills and eviction stand).** Per owner a
  **stable hex grid fixed in the owner's object frame**: six anchor regions centred on the owner's local axes (nose,
  tail, flanks, top, belly mid-region, the seams on the cube's edges), each a stereographic tangent plane (conformal,
  the cell scale grows at most 27 % at a cube corner), so the cells stay put as the ship moves and every hit in a
  region lands on the same cells. Each cell has its **own fade clock**: a hit sets the clocks of its footprint cells
  to "lit now" as its ring front reaches each centre (a cell already lit is reset to full, never drawn twice); cells
  the new hit does not cover keep their clocks and fade out. The stage rebuilds each region's cell map from the
  owner's live hits every frame (a cell's clock is the newest hit that lit it; up to 64 hits per owner, the rest
  counted as `hits_dropped`; the stage keeps no state, "Placement"); one draw per owner and region over the cap bounding its
  lit cells, 128 cells per map; a region with more lit cells spills into further draws (`region_draws` = 8 per region,
  from the frame's 32 after every region's first, each map a compact Morton chunk with its own cap; review F1/F2
  2026-11-06), and past them the cells with the oldest clocks, the dimmest, are dropped and counted
  (`cells_dropped`). The pixel program finds the pixel's cell on the exact ray-ellipsoid point (not the
  facet-interpolated one, which made border pixels change cells when the cap changed) and reads its clock from the map
  in pixel constants c28..c59 (32 registers of four packed keys) and c60..c91 (their clocks), unrolled in eight groups
  of four registers behind a uniform branch: this d3dx compiles a rolled loop's `keys[k]` into a compare per register
  (64 per iteration, no `aL` addressing), so the old rolled loop cost grew with the map's size twice over. The patch
  boundary is therefore made of hexagon edges (cells lit as wholes), not a circle. The ripple ring stays per hit (the
  region's newest four hits still in their ring). Hits either side of a region seam draw on two lattices; where two of
  an owner's region draws have overlapping caps the pixel program shares the light across the seam over one cell
  (each keeps saturate((s_own - s_other) S / (sqrt 2 cell) + 1/2), the shares summing to one; up to 3/2 within a cell
  of a cube corner where three regions meet), so the two lattices never add up (review F3; 9 slots).
- **Pattern.** Hexagons, cells lit individually: a small random stagger in brightness and fade rate per cell, edges
  brighter than the fill. One pattern for every race and class in step 1. As built: a sine-free hash of the cell key
  and the region (stagger 0.4: × 0.8 to 1.2), edge band 0.1 cell, fill 0.3 of the edge, edges anti-aliased by
  `fwidth`; the ring band uses the same edge-versus-fill pattern.
- **Timing (game time, SETA-scaled).** No hard flash. The hit's own cell is the marker (user decision 2026-11-06,
  replacing the 0.1 s per-pixel core flash): it lights at `centre_gain` 2 × a ring cell's edge and fill with its fill
  at `centre_fill` 0.6 of the edge (ring cells 0.3), in the shield colour, and fades on the same per-cell law; a cell
  hit again returns to the full level the newest hit gives it, centre or ring. One soft ring
  expanding from the hit cell over ~0.3 s that lights the cells as it reaches their centres (the ring itself moves
  by whole cells), then each cell fades on its own clock. Repeated hits re-light cells already lit (sustained fire
  keeps a fed area lit; cells outside it fade). As built: ring 3.333 footprint radii ((rings + 0.5)
  cells) per second, fade 2200 ms × (1 ± 0.2) per cell (1.76 to 2.64 s, user decision 2026-11-06: the 1.2 s fade
  looked too fast), the record's lifetime 2941 ms (the last fighter cell goes dark at 2840 ms).
- **Colour.** Weapon tint **off by default** (kept as an override, `weapon_tint`). The patch colour follows the
  victim's **shield percentage**, interpolated smoothly: light blue at >= 75 %, through light yellow near 50 %, to light
  red below 35 %. The hook reads the current and maximum shield (spawn note §9, 2.2: capacity-weighted like the HUD
  bar, post-hit); a refused read colours light blue.
- **Cost.** A patch is a fraction of the full shell (which measured 201.8 us for two shells at 5120x1440 with the
  winding cull, the ledger). The hull decal's program is 457 ps_3_0 slots, the bubble's 873 (two points' cell work,
  unrolled); at 5120x1440 the bubble's fighter, close-capital, worst-owner and capital-spread rows measure 0.93 to
  1.02 x (the spread 0.43 x: fewer, smaller boxes) the decal's 6720724d3 figures, the own ship's cockpit with eight hits
  258 us; the walk (removed 2026-11-12) was cached and bounded to two a frame (12.3 us for 127 parts through
  engine_memory::read when it ran); the per-frame figures and the stage's CPU cost are in the ledger [M].
- **Lab controls** (replacing 5.1 item 4's shell list): cell size per tier, rings per tier, the surface (ship bubble or
  station hull decal), the bubble scale and the far side's gain, the hull presets with the chase camera's nose hit, ripple speed,
  centre gain and fade duration, stagger, edge-versus-fill, the three colour bands, the SETA rate (default 1), the
  hit-sequence player in hits per game second (so SETA 6 is as dense as the game).
- **Later options, not now:** patterns per race or class; weapon tint.
- **Decisions on the surface (dated).**
  - 2026-11-06 (user, Run239): ships carry the ellipsoid bubble fitted to the hull bounds; stations keep the on-hull
    screen-space decal.
  - 2026-11-12 (main session, FL 67 A): capital-tier ships (M0/M1/M2/M7/TL, and any capital-tier owner) take the hull
    decal at the contact point; fighters and corvettes keep the bubble.
  - 2026-11-12 (main session, FL 67 A review F2): the own ship in the cockpit view keeps the Run240 eye sphere whatever
    its tier; only an external capital-tier owner takes the decal.

### 5.2 Step 2: hull hit

Built and removed with the look layer (2026-11-08): [archive](../archive/effects-look-2026-11-08/index.md).

### 5.3 Step 3: explosions

Built and removed with the look layer (2026-11-08), the clone row R' with it (its EXE anchors stay in
[weapon-effect-spawn.md](../reverse-engineering/weapon-effect-spawn.md) section 10): [archive](../archive/effects-look-2026-11-08/index.md).

### 5.4 Step 4: bolts and muzzle

Built and removed with the look layer (2026-11-08); the bolt tap, muzzle and flight hooks stay:
[archive](../archive/effects-look-2026-11-08/index.md).

**The projectile stage (FL, 2026-11-08 to 2026-11-13).** The redo first drew FL's bolts and muzzle flashes itself (a
stage beside the shield stage fed by the object tap's pool, the batch submit `0x097af8` skipped per body, the lone draws
dropped in the draw hook, the muzzle stub skipping the game's flash). It was superseded by the re-shade of the game's
own draws (`weapon_effects = reshade`, user decision 2026-11-09: [projectile-look.md](projectile-look.md),
[muzzle-flash-look.md](muzzle-flash-look.md), [bullet-draw-batch.md](../reverse-engineering/fl/bullet-draw-batch.md)
sections 6 and 7) and removed on 2026-11-13
([archive](../archive/effects-look-2026-11-13-custom-projectile/index.md), last commit `04d518bf3`). Since then the
batch site is claimed only in reshade mode, with the scope stub that forwards every submit; the object tap and the
muzzle site only in the bolts modes, where the FL muzzle forwards every shot; AP installs nothing new.

### 5.5 Step 5: beams

No new site: a beam is a class-0 bullet with flags `0x2`/`0x4` whose muzzle record (step 4) carries the shooter, slot
and axis; its length is col 18 (kind B, fixed) or `0x00444bd0(obj)/2` refreshed per fire tick (gun-tied) [M, §1], read
from the bullet's `ext+0x78` through the flight tap when available. The ribbon, sheath, scrolling noise and end flare
of the dropped 3.2; the beam's draw (its body pair is untraced [U]) is recognised at draw time only to suppress it.
Flight: PAL / PBC on a capital, the Khaak beams, the repair laser and tractor beam (overrides).

## 6. Unknowns and what settles each (question 5)

| Unknown | Settles it |
| --- | --- |
| The meaning of `node+0x180` in the contact point; whether the handler's replicated `pos` equals the engine's | Flight A's `weapon_pos_check` against the inner diagnostic tap (2.2); a max error below 1 unit over ≥ 50 hits |
| Unit of the quad size cols 16/17 and the drawn bolt width | Flight A: `bolt_size` row per weapon (col 16 against the drawn instance extents the footprint path derives) |
| Numeric `OBJ_SHIP_*` text ids for the explosion tiers | Disassembly of the text-id reader `0x004ea440` and its table (one `disassemble` task); until then radius bands from Flight A's `weapon_death` rows |
| Whether `0x004146c0` is reached for bullets with col 19 = 0 | Decompile of `0x00414669..0x004146fa` before step 4 is briefed |
| The missile extension layout (owner, launcher) | Disassembly of the missile creation path; step 2 draws `missile_hit` without it |
| The bullet `ext+0x70` laser pointer dangling after the laser is removed | The handler never dereferences it; the shooter is `ext+4` (the owner object), validated by handle/serial |
| The fire routine's SEH try state at `0x00446268` | Irrelevant to a non-throwing handler; the hook fixture asserts no unwind path is linked |
| The hooks' real cost and the stage's cost in a fight on the native host | Flight A `weapon_effects_frame` (hooks), Flight B `stage_us` |
| The meter's response to large fireballs; starfield trails behind streaks | Step 3's fixture and flight (unknowns 8 and 12 of the dropped note) |
| FL sites | An FL reverse-engineering note of the four sites and the `absorbed` slot (2.10) |
| The census JSON lacks cols 7/22 (damage per shot), so `classify_preview.py` reads the type table itself | `tools/effects/weapon_effects_census.py` will gain TBullets cols 7 and 22 (`shield_damage`, `hull_damage`); `classify_preview.py` then reads the census JSON only |
| The native fixture runner (`native_host.py`) and native pins for every stage figure of this note | The runner lands with `run_engine_plumes.py --host native` as its pilot; step 1's fixture re-pins the shell numbers (2.8, 5.1) |

## 7. Options considered and why they lose

- **One hook in the generic spawner `0x004148a0`, role from the return address.** It cannot reach the victim/source
  pair, the absorbed flag or the dying object (for a death `obj` is the new carrier), and it runs every frame for
  every engine jet [M, §5]. Loses to the four specific sites.
- **Texture-key identity at the draw (the dropped design's 2.1/8.2).** Mayhem replaces 22 of 32 bullet bodies and
  all its effect rows; the key table is per mod and knows no role; the user ruled it out. Loses.
- **Suppressing at the draw, forwarding natively later when the stage cannot run (dropped review B1).** No airtight
  forward path for a sprite whose resources the engine may have released; phase 1 therefore drew on top of the
  native sprite and the look stayed the game's. Spawn-time suppression with the previous frame's verdict and
  proxy-owned timelines bounds the loss to one frame's draw (2.6). Loses.
- **Suppressing at the inner impact instance call `0x00413da8`/`0x00413e79`** (gives `&pos` for free, null return
  tolerated). Leaks the row's `PROPAGATE` children spawned before it on six stock rows [M]. Kept as the observe-mode
  diagnostic only.
- **Explosion tiers from the Effects row id (35/37/40).** Per mod (Mayhem 600–602) and already inconsistent in stock
  (M6 split 27/2, TP 14/4) [M]. The class id with the row as cross-check wins; radius bands bridge the unknown ids.
- **A user-facing per-weapon config.** The user configures nothing; a shipped override file keyed by id and table hash
  carries curation.
- **Keeping the native sprite under the proxy effect (phase 1's option b).** Two draws per effect and the old look
  remains; this is what was dropped.
- **Tiering explosions by the dying object's radius only.** Robust but blind to a small-radius capital hull and to
  stations; it stays the fallback.
- **One flight for several families.** The user's rule is one family per flown step; each family's flight is its own
  acceptance and the hooks are armed per family so a later step cannot regress an accepted one.

## Reproduce

```sh
python3 verification/results/weapon-effects-census/classify_preview.py > verification/results/weapon-effects-census/classify_preview_out.txt
python3 verification/results/weapon-effects-census/classify_preview.py --counts   # per-style counts only
python3 -I -c "import json; d=json.load(open('verification/results/weapon-effects-census/stock_ap.json')); print(d['tables']['TBullets'])"
# Per step, once built (no Wine on this host; the lock serialises the native fixture queue):
python3 verification/probe/wine_lock.py python3 verification/probe/run_weapon_effects.py --host native
python3 tools/effects/generate_weapon_effects_lab.py --check
```
