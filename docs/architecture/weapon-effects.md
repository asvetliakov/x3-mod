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

## Outcome

Hook the engine's four weapon-effect spawn call sites (impact `0x00449d85`, muzzle `0x00446268`, death
`0x0044ac64` / `0x0044acb2`) with the `claim_call` redirect pattern of `engine_effects_patch.cpp`; each stub hands a
64-byte **event record** (role, victim or owner, projectile record index, contact point, absorbed flag, damage, game
time) to the proxy and **skips the engine's spawn** when the stage owned the previous frame, else forwards the call
unchanged. Style comes from a **classifier over the live TBullets record** (flag bits, damage per shot, light colour)
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
| source class `+0x48`, subtype `+0x4a`: class 0 → TBullets[subtype] (flags `+0x58`, light `+0x5c..`, shield/hull damage `+0x44`/`+0x80`, speed `+0x54`, size `+0x68/+0x6c`); bullet ext `[source+0x50]`: owner `+4`, laser `+0x70` [M, §2] | `table = bullets`, `row = subtype`, the classifier inputs copied (so the record is self-contained if the bullet dies this frame); `shooter` = owner object |
| class 10 → TMissiles[subtype] (impact row `+0x68`) [M, §3]; the missile extension layout is untraced | `table = missiles`, `row = subtype`, style `missile_hit` (fixed: medium calibre, warm tint) |
| any other class on both sides | no record; **forward** (the engine spawns nothing either) |
| victim: class, subtype, root node `obj+0x70`, node handle `node+0x28`, radius `obj+0xa4`, position `node+0x30`, rotation rows `node+0x40` [M, spawn note §4, engine-effects.md §7] | `owner` = {object, node, handle, serial when `object_lifetime` is active}, `radius` |
| contact point: the smaller body (radius compare) gives `p = node+0x30 + R(node+0x40) · node+0x180`; `pos = Rᵀ(victim) · (p − victim pos)` [M, §3: the callee's own arithmetic, replicated] | `pos` in the victim's frame (3 floats), the projectile heading from the projectile's `node+0x40` z row |

Decision: skip when the stage **owns the frame** (2.6) *and* the family of the role is armed (step 1 arms
`shield_hit` only; a hull hit is forwarded until step 2). Otherwise forward. The replicated contact point is the one
unknown of this site (the meaning of `node+0x180`); it is settled by the observe flight of 2.6, not assumed.

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
  the 64-frame disarm window`. The hooks read `owned_next` during the next frame's update (same thread, before the
  scene draws). A hook never consults device state itself.
- **Skip only when owned and the family is armed; otherwise forward unchanged.** Forwarding is the vanilla effect: the
  stage never draws a record it forwarded (the record is written with `forwarded = 1` for the census only). Nothing is
  ever drawn both natively and by the stage.
- **One-shot events and a failing stage.** If the stage fails at scene end after the hooks skipped, that frame's
  events are not lost: they live in the proxy ring with their own game-time timelines and are drawn when the stage
  returns, if still within age. The stage disarms for 64 frames (the dropped design's rule), during which the hooks
  forward. The loss is bounded to the fail frame's own draw, never the event. This is the airtight path the dropped
  review B1 could not find at draw time: the decision moves to the spawn, where "skip" is an engine-native state.
- **Observe mode** (`weapon_effects = observe`): every hook records and forwards; the inner impact tap is live; the
  stage draws nothing. It is the first flight of every family (section 5.1) and the mode that prices the hooks.
- **Modes** (`weapon_effects = native | observe | <families>`, key in `tools/config/schema.py`, launcher
  `--weapon-effects`): `native` patches nothing; the family list arms steps independently (`shields`, `hull`,
  `explosions`, `bolts`, `beams`); the DLL default is the set of flown-and-accepted families (none until step 1
  lands), as `engine_effects` defaults to `plumes` after Run 123 A [S, config-file.md].

### 2.7 Nested kills and missiles

An area kill runs `0x0045eeb0`, which runs the damage routine (impact stubs) and nested kills (death stubs) **inside
the outer kill**, after the outer death stub returned [M, §6]. The ring is append-only and per-event; no stub keeps
state across another, and the retirement rule of 2.4 runs at each death record. A stub is never re-entered through its
own site while running [M]. An impact whose row has an `EEDF_BREAKUP` element could remove the victim inside the
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

### 2.10 FL

X3FL.exe is relocatable and has none of these sites traced; the FL edition compiles the families out
(`X3M_EDITION_FL`: the hooks are not installed, `weapon_effects` reports `edition_unsupported`) until an FL
reverse-engineering note names the four sites and the `absorbed` slot, as [fl/engine-effects.md](../reverse-engineering/fl/engine-effects.md)
did for the plumes. The FL weapon census has the same 45 rows and ids as stock AP; the record fields differ only in
PD's lifetime (1,800 vs 2,100 ms), speed (177.5 vs 187.5 m/s) and damage (351/80 vs 468/120), and the PAC body's scale
(8,099 vs 16,171; elongation 24.5 vs 24.7), so the classifier gives the same style for all 45 [M, `fl.json`,
`classify_preview_out.txt`]; the classifier and overrides carry over unchanged.

## 3. Record schema and the override file (question 2)

### 3.1 `weapon_effects::core::Event` (64 B, `src/proxy/weapon_effects_core.h`, Windows-free)

Shared by the DLL, the fixture (compiled in) and the lab (its JSON mirror, injected by the generator of 5.1).

| Field | Type | Source |
| --- | --- | --- |
| `role` | u8: `shield_hit`, `hull_hit`, `muzzle`, `bolt`, `beam`, `explosion` | the site (2.2–2.5) |
| `style` | u8: shape `tracer` / `slug` / `orb` / `wave` / `beam` (+ `burst` bit), or `missile_hit`; explosions carry the tier instead | the classifier (section 4) or the override |
| `tier` | u8: `light` / `medium` / `heavy` (weapons); `fighter` / `corvette` / `capital` / `station` / `asteroid` / `missile` / `burst` (explosions) | damage per shot; the victim's class |
| `calibre` | f32 0..1 | `clamp(log10(d / 300) / 2, 0, 1)` with d = max(shield, hull) damage per shot; for impacts the running total replaces d |
| `tint` | 3 × f32, linear | light colour when saturated, else the body's texture mean colour from the table, else white (4.2) |
| `t0_game_ms` | u32 | `cfg+0x718` at the hook; **every age and bound is game time**: `age = now_game − t0`, so SETA shortens ripples, sparks and fireballs exactly as it does the engine's own effects |
| `lifetime_ms` | u16 | the style's timeline (ripple 600, flash 100, sparks 250–500, fireball 1,000–2,000 by tier, beam = col 27 when flag 0x2000) |
| `owner` | object ptr, root node ptr, node handle u32, serial u64 (0 when `object_lifetime` is inactive) | the victim (impact), the ship (muzzle), the dying object (explosion, t0 only) |
| `shooter`, `slot` | object ptr, u8 | muzzle and bolts |
| `table`, `row` | u8 (`bullets` / `missiles`), u16 | the live record's identity; the override and the table lookup key |
| `pos` | 3 × f32 | local to the owner (impact, muzzle) or world (explosion) |
| `axis` | 3 × f32 | projectile heading / muzzle orientation / dying object's velocity |
| `radius` | f32 | `obj+0xa4` of the owner (shell ellipsoid, fireball size) |
| `flags` | u8 | `absorbed`, `forwarded`, `nested`, `owner_retired`, `override_applied` |

Per frame the stage derives, not stores: the owner's current object → world rows (`node+0x30`, `node+0x40` through
`engine_memory::read`, validated by `obj+0x70 == node && node+0x28 == handle` and, when active, the `object_lifetime`
serial; a failed validation retires the record), the view/projection of the resolve, and the age. The ring holds 256
events (16 KB); overflow drops the oldest `hull_hit`/`muzzle` first and counts `overflow`. The ring clears on the
resolve's cut verdict, a Reset, a sector change or load epoch (the plumes' rule [S]).

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
(M5/M4/M3; also TM, drones, satellites), **corvette** (M6/M8/TS/TP) and **capital** (M1/M2/M7/TL); class 5/6 →
**station**; class 17 → **asteroid** (size from `obj+0xa4`); class 10 → **missile**; class 0 with flag `0x100` →
**burst** (calibre from the record). The numeric values of the `OBJ_SHIP_*` text ids are **unknown** (the census reads
them as strings from the file [M]); until the text-id reader `0x004ea440`'s table is disassembled the DLL tiers by
radius bands calibrated in the observe flight (`weapon_death` rows log class id, col 47 row and radius together), and
col 47 is the cross-check: stock rows 35 / 40 / 37 and Mayhem 600 / 601 / 602 follow the same three tiers with four
exceptions (2 M6 on 35, 4 TP and M8 on 40 [M, census]); the DLL logs `tier_mismatch` when its tier and the row's
tier disagree, and the override file can pin a class.

### 4.4 Resulting style per stock weapon [M]

| weapon | flags | z/xy | m/s | dmg/shot | style | calibre | tint |
| --- | --- | --- | --- | --- | --- | --- | --- |
| IRE | 0x0 | 18.5 | 581 | 220 | tracer | light 0.00 | light |
| PAC | 0x1 | 24.7 | 351 | 1,155 | tracer | light 0.29 | light |
| MASS | 0x60 | 6.6 | 369 | 45 | tracer | light 0.00 | texture |
| PRG | 0x1 | 7.3 | 780 | 680 | tracer | light 0.18 | light |
| EBC | 0x40 | 3.8 | 302 | 1,357 | slug | light 0.33 | light |
| FBL | 0x1000 | 1.9 | 179 | 2,793 | slug+burst | light 0.48 | light |
| HEPT | 0x0 | 9.4 | 215 | 1,995 | tracer | light 0.41 | light |
| IOND | 0x214 | 2.0 | 1,013 | 1,575 | beam | light 0.36 | light |
| PBE | 0x0 | 18.7 | 3,490 | 945 | tracer | light 0.25 | light |
| PD | 0x808 | 1.5 | 187 | 468 | wave | light 0.10 | light |
| EMP | 0x800 | 1.8 | 294 | 2,000 | slug | light 0.41 | light |
| CIG | 0x0 | 10.6 | 210 | 7,845 | tracer | medium 0.71 | texture |
| IPG | 0x1200 | 7.2 | 173 | 12,563 | tracer+burst | medium 0.81 | light |
| ESR | 0x400 | 5.6 | 249 | 2,671 | tracer | light 0.47 | light |
| MAM | 0x140 | 1.3 | 222 | 8,350 | orb+burst | medium 0.72 | texture |
| FLAK_ALPHA | 0x2100 | 18.7 | 3,525 | 13,860 | tracer+burst | medium 0.83 | texture |
| FLAK_CLUSTER | 0x3000 | 18.7 | 1,100 | 11,781 | tracer+burst | medium 0.80 | texture |
| PAL | 0x2 | 21.9 | 1,280 | 3,969 | beam | medium 0.56 | texture |
| PSG | 0x8 | 0.9 | 127 | 1,996 | wave | light 0.41 | light |
| TERANTIFIGHTER | 0x0 | 2.0 | 1,000 | 20,785 | slug | heavy 0.92 | texture |
| PPC | 0x2000 | 2.9 | 166 | 63,778 | slug | heavy 1.00 | light |
| IONC | 0x2600 | 3.5 | 193 | 119,148 | slug | heavy 1.00 | light |
| GAUSS | 0x40 | 2.3 | 176 | 28,540 | slug | heavy 0.99 | light |
| IBL | 0x2800 | 1.7 | 156 | 50,860 | slug | heavy 1.00 | light |
| PSP | 0x2100 | 1.0 | 160 | 252,310 | orb+burst | heavy 1.00 | light |
| PBC | 0x2 | 2.7 | 5,322 | 14,280 | beam | medium 0.84 | texture |
| TRIBEAM | 0x2 | 21.9 | 2,815 | 7,532 | beam | medium 0.70 | texture |
| FUSIONBEAM | 0x802 | 21.9 | 1,330 | 5,400 | beam | medium 0.63 | texture |
| MINING | 0x0 | 1.0 | 42 | 6,500 | orb | medium 0.67 | light |
| TUG | 0x2 | 3.4 | 2,000 | 0 | beam | light 0.00 | texture |
| DUMMY1 | 0x3000 | 3.7 | 12,500 | 11,781 | slug+burst | medium 0.80 | texture |
| DUMMY2 / KH_ALPHA | 0x2 | 3.4 | 2,996 | 700 | beam | light 0.18 | light |
| KH_BETA | 0x2 | 1.9 | 4,882 | 2,500 | beam | light 0.46 | texture |
| KH_GAMMA | 0x2 | 1.0 | 6,071 | 7,500 | beam | medium 0.70 | light |
| REPAIR | 0xa2 | 21.4 | 2,200 | 40 | beam | light 0.00 | light |
| FBL_FRAG, FLAK_*_FRAG (4) | 0x100 | 5.4–18.5 | 250–1,050 | 220 | tracer+burst | light 0.00 | light |
| TERRANANTIFIGHTER_FRAG | 0x8 | 0.9 | 127 | 220 | wave | light 0.00 | light |
| EMP_1 / MAM_1 / TERRANANTIFIGHTER_1 / IPG_FRAG | as the base row | | | | slug / orb+burst / slug / slug+burst | | |

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

**Goal.** A shield hit shows the dropped design's Fresnel shell with up to four expanding rings and a flash (3.3 of
the dropped note, unchanged), placed by the impact hook, suppressing the native sprite, tinted by the classifier,
aged in game time. Nothing else changes: hull hits, bolts, muzzle and explosions stay native.

**Scope.**

1. `weapon_effects_sites.h` (impact site window bytes and the diagnostic inner sites), `weapon_effects_patch.cpp`
   (one stub, the handler of 2.2, the ring, arming, modes `native | observe | shields`), `weapon_effects_core.h`
   (`Event`, the classifier of section 4 with every threshold a named constant, the game-time age law).
2. `tools/effects/weapon_effects_table.py` (the generated table, install / uninstall / regenerate wiring as
   `engine_bodies.json`), `tools/effects/weapon_overrides.json` (empty `by_id`, the three known tables listed).
3. The stage: the dropped `EffectsStagePass` shell/decal programs and the icosphere, revived from `9375a1e7` as a
   `WeaponEffectsPass` fed by `Event` records (owner rows read at scene end as 3.1 says) instead of texture-keyed
   draws; riding the resolve's step-0 callback after the plumes callback (the `own_sun_with_plumes_` chaining
   pattern of `motion_output.cpp:2493–2507` [S]). Occlusion by the lane alone (unknown 11 of the dropped note was
   settled: the depth is unbound before the callback [M]).
4. The lab: **no new page.** The step fills the `renderOurs(record, t)` stub of the existing
   `generate_weapon_effects_lab.py` / `weapon_effects_lab.html` for the `shield_hit` role (a shell on an ellipsoid
   beside the vanilla sprite the page already plays) and adds its controls: `ring_speed`, `ring_width`,
   `ring_count`, `ripple_ms`, `flash_ms`, `rim_power`, `rim_gain`, `hex_scale`, `hex_gain`, `tint_mix` (light vs
   texture), `calibre_gain`, the SETA rate (the age law under warp), a hit-sequence player at 1, 4 and 12 hits per
   second; every control's default is the header's value, injected by the generator.
5. Rows: `weapon_effects_patch site= va= state= write=` at install; `weapon_event role= table= row= style= tier=
   calibre= absorbed= damage= forwarded= nested= t0=` on capture frames and the first 64 events of a run;
   `weapon_effects_frame` per 300 frames (events, hits, shells, decals, retired, overflow, owned, stage_us);
   `weapon_pos_check` in observe mode (the handler's `pos` against the inner tap's, max error).

**Acceptance (host, before the flight).**

- Hook fixture, run natively through WSL interop (the FL redirect fixture's harness): a synthetic image with copies of
  the impact window and the two inner windows; checks: window compare and refusal on one changed byte; skip path
  leaves EBX/ESI/EDI/EBP and the stack exactly, returns with the caller's `add esp`; forward path reaches the callee
  with every register and the stack as the call left them; the absorbed slot and the damage slot read from the
  synthetic frame; the shield rule (`absorbed`, flag `0x20`, class 0/10/other) on a table of cases; nested death
  retires the owner's hits; the ring's overflow order; `late_claim`; rollback on a failed read-back; the handler
  compiled with `-fno-exceptions`, no CRT import (a link check). Target: every check PASS, 0 failures.
- D3D fixture on the native host (`python3 verification/probe/wine_lock.py python3
  verification/probe/run_weapon_effects.py --host native`, through `native_host.py`; its record under
  `verification/results/native-windows/weapon-effects/` carries the host identity block). **Prerequisite: the native
  runner exists and `run_engine_plumes.py --host native` has passed once.** The checks re-pin the shell numbers
  natively; the dropped ledger's values (rim 0.50 / 0.42 at 70° vs 0.24 / 0.20 at 45°; four rings on / off their
  widths; hull cut 0.000 / 8.23; soft cut 2.80 of 8.60; decal 3.33 on hull / 0.000 over sky; core 1.000 through the
  resolve at 0 / 4 / 8 px per frame; 2 shells at 204 / 544 px 0.124 / 0.162 ms; full-screen shell 0.54 / 0.77 ms)
  were measured under Wine/wined3d on the Mac [M, the ledger] and are expected to differ on a real GPU: the fixture
  gates on the structural relations (rim rises with the angle, rings present on and absent off their widths, the
  hull cut 0, the decal 0 over sky, the core keeps ≥ 0.95 through the resolve), reports the native values as the new
  pins and does not tune to the Wine numbers. Also: the age law (a 600 ms ripple at SETA 6 completes in 100 ms of
  wall time); a retired owner draws nothing; the Reset and fault cases; timing at 1080p and 5120x1440 for 2 shells
  and the full-screen shell (advisory 0.1 ms target, reported, not gated; the winding cull stays the lever).
- `generate_weapon_effects_lab.py --check` clean; `classify_preview.py` against the header's thresholds gives the
  table of 4.4 (a host test pins it).
- Dry run: `python3 tools/manage.py launch --dry-run --weapon-effects observe` and `… shields` forward
  `X3M_WEAPON_EFFECTS=` and the table path; `--vanilla` refuses the option.

**Flights.** Flight A (`observe`): the hooks record and forward; the capture must show `weapon_pos_check` max error
below 1 unit over ≥ 50 hits (settles the `+0x180` unknown), every `weapon_event` with `role=shield_hit` matching a
visible native shield sprite, `absorbed` consistent with the HUD shield bar, the hooks' cost in `weapon_effects_frame`
(target ≤ 30 µs per frame at 50 events [I]), and `weapon_death` rows with class id, col 47 and radius for the tier
table. Flight B (`shields`): what the user must see — the native shield sprite gone on every shielded hit, a shell
whose rings start at the hit point and fade in about half a second of game time (faster under SETA, never slower),
tinted by the weapon (blue-violet IRE, green HEPT, yellow PRG on stock; Mayhem by texture), no shell on a hull hit
(still native), no shell lingering after a kill or dock, no double image, and the frame time unchanged within noise
(`stage_us` ≤ 0.2 ms at 5120x1440 in a fighter fight). The user reports with F8 captures at a hit and after a kill.

**Lab controls** (what the user tunes before Flight B, each a header constant the generator injects): ring speed and
width, ring count, ripple and flash duration, rim power and gain, hex pattern scale and gain, tint mix, calibre gain,
SETA preview. The agreed values go into the header, the fixture re-pins them, then the flight.

### 5.2 Step 2: hull hit

Arms `hull_hit` at the same site (no new patch): sparks (16–24 CPU particles, drag, 250–500 ms game time), the
depth-aware hit light on the hull (150 ms) and the flash of the dropped 3.4, replacing the native sprite and its
`EEDF_LIGHT`. The sparks' cone is the ellipsoid gradient at `pos`; the owner's velocity is the node's frame delta
read at scene end. Fixture adds: spark count and cooling ramp, the light's falloff against the lane, the resolve
survival of streaks, the SETA law for the particle integration (`pos += v · step_game`). Flight: a hit on an
unshielded hull shows sparks and a brief glow, a shielded hit shows the shell only; the repair laser (flag `0x80`,
`absorbed = 1` with the hull stage) shows a hull-hit variant without sparks (the override sets `style = repair`).

### 5.3 Step 3: explosions

Claims both death sites (2.4). The noise-sphere fireball, shockwave, spark and debris streaks and flash of the
dropped 3.5, sized by tier (fighter / corvette / capital / station / asteroid / missile / burst) and `radius`,
positioned at the dying object's world position plus its velocity × age (the record is world-space; the node is not
read again). The capital tier replaces the engine's `PROPAGATE` scene-copy debris with streaks. Fixture adds: tier
sizes, the octave LOD above 20 % coverage, the exposure meter's response to a capital fireball (unknown 8 of the
dropped note), the cost of a close capital explosion (0.22–0.45 ms expected [I]). Flight: kills of an M5, an M6 and a
freighter plus one asteroid; what the user must see is a fireball that cools into smoke over 1–2 s of game time, no
native explosion sprite, nested kills (a missile's area kill) each with their own fireball.

### 5.4 Step 4: bolts and muzzle

Claims the muzzle site and, if the decompile of 2.5 shows `0x004146c0` is reached for every bullet, the flight site
as a per-bullet per-frame tap (position, velocity, subtype: no matching at all). Otherwise bolts keep the dropped
3.1 instance derivation from the `INSTANCE_BULLETS` draw and match instances to muzzle records by predicted position
(`muzzle + axis · speed · age`, ≤ 64 × 256 compares ≈ 20 µs [I]). Either way the bullet draw is recorded and
suppressed at the draw (the one draw-time suppression left: the bullet draw's forward path is the existing additive
route, proven in phase 1 [M]). The muzzle flash replaces the native sprite, light and lens flare (13 of 21 stock
rows). Styles: tracer / slug / orb / wave with the streak law; `+burst` draws a small burst at the bullet's own death
record. Flight: chase and first-person views (the dropped open question: the user decides per view).

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
