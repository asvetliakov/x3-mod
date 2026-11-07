# Weapon, impact and explosion effects: selection, spawn sites and hook candidates

2026-11-05. Read-only study for the weapon/impact/explosion effects replacement (step 1: where the engine selects
and spawns each effect, and what a proxy hook at each site can read). Installed `X3AP.exe` SHA-256
`fdbf3418…f8ab` (`/mnt/c/X3/X3AP.exe`, preferred VAs, base `0x00400000`); Ghidra 12.1.4 headless on the existing
project `~/x3-ghidra-research` / `X3Render` (`-noanalysis -readOnly`) with
[`X3DecContaining.java`](../../verification/results/engine-effects/X3DecContaining.java), checked against an
`i686-w64-mingw32-objdump -d -M intel` listing. Data: the Mayhem 3 install (`installed`) and its stock layers
(`STOCK_AP_CATALOGUES`, `stock`) through `tools/analysis/sector_fog_census.Assets`. No game launch, no Wine, no
build. Raw decompiler output and the listing stay in the session scratchpad (untracked). Tags: **[m]** measured
(instruction bytes or data read), **[i]** inferred (decompiler reading or reasoning not checked instruction by
instruction), **[u]** not established.

Builds on [collision-damage.md](collision-damage.md) §1.4 (the damage routine `0x00449a60`),
[engine-effects.md](engine-effects.md) §7 (the effect-instance spawner `0x004148a0`, the Effects table
`0x00580068`, stride `0x14`) and [game-scale-and-ship-classes.md](game-scale-and-ship-classes.md) §8 (TLaser col 9
→ bullet, TBullets `+0x50`/`+0x54`).

**Answer.** Every weapon-related effect is one `types/Effects` row turned into an instance by the generic
find-or-create spawner `0x004148a0(kind, k, eff, obj, …)`. The role is fixed by the call site and is visible as
`kind`: **4 muzzle** (`0x00413c00`, row = TBullets col 21), **3 impact** (`0x00413c40`, row = TBullets col 20 or
TMissiles col 20), **1 death explosion** (`0x00413610`, row = a per-type column chosen by `0x00413040`), **5
break-up** (`0x004132d0`), **0 attached** (`0x00414590`: ship jets, and the projectile's own flight effect, TBullets
col 19). The engine does **not** pick a different row for a shield hit and a hull hit; the damage routine knows which
one happened (a local flag) and a hook at its call site can read it. A beam is the same class-0 bullet object with
flag bits 2/4/0x2000. A single hook in `0x004148a0` can tell the role from its return address, but it cannot reach
the victim/source pair, the shield/hull flag or the dying object, so the four specific call sites are the hook
points (§6).

## 1. TBullets record layout (question 1)

Type tables: `*(0x00606fb8 + class·4)`, count `*(0x00607038 + class·4)`, **stride `0xdb8` for every class**
(`0x004363d7`). TBullets is class 0 (`*0x00606fb8`), TLaser 8 (`*0x00606fd8`), TShields 9, TMissiles 10
(`*0x00606fe0`), TShips 7 (`*0x00606fd4`), TDocks 5, TFactories 6, TAsteroids 17 (`*0x00606ffc`) **[m]**
(`0x00436277`, jump tables `0x0043944c`, `0x00439510`/`0x004394cc`). The loader `0x00434e40` reads columns 0–6 for
every class (`+0x0` body, `+0x4`, `+0x8/+0xc/+0x10` 16.16 rotation, `+0x14` text id, `+0x18`), then the class case;
TBullets is the case `0x00436518`. Both views' `TBullets.txt` are file version 55 (`0x37`), 50 columns per row
**[m]**, so the version-gated columns 27–38 are read.

Column → offset map ([`type_loader_columns.py`](../../verification/results/weapon-effect-spawn/type_loader_columns.py) →
[`_out.txt`](../../verification/results/weapon-effect-spawn/type_loader_columns_out.txt), trace of
`0x00436518..0x00436770` with the file-version branches evaluated) and every reader the field scan found
([`tbullets_field_readers.py`](../../verification/results/weapon-effect-spawn/tbullets_field_readers.py) →
[`_out.txt`](../../verification/results/weapon-effect-spawn/tbullets_field_readers_out.txt); it follows the table
pointer through `add`, `mov` and `lea` displacements for 40 instructions, but not a record base spilled to the stack
and reloaded later, such as the fire path's `rec+0x44` slot, so every "no reader" below is **[i]**):

| col | offset | type | EXE use | tag |
| --- | --- | --- | --- | --- |
| 0 | `+0x0` | body name → id | bullet node body (`0x00441107` → `0x00487e30`); INSTANCE_BULLETS geometry | m |
| 7 | `+0x44` | int | damage argument 3 (shield stage) of `0x00449a60` at `0x0045e324`/`0x0045e380` | m |
| 8 | `+0x48` | int | energy per shot: ship `ext+0x2b4 −= value·1000` (fire path) | m |
| 9 | `+0x4c` | int | impact sound id → `0x0049b1f0` (`0x004172a8`) | m |
| 10 | `+0x50` | int | lifetime ms: `ext+0 = now + value` | m |
| 11 | `+0x54` | int | speed; quad length `0x00469a30(speed, 0x1999, node scale)` (`0x004411f7`, 0.1 · speed **[i]**) | m |
| **12** | **`+0x58`** | int | **flag word**, bits below | m |
| 13–15 | `+0x5c/+0x60/+0x64` | int | **light colour R, G, B**: `0x00444a70` creates a light node on the bullet (`ext+0xc`, RGB words at node `+0x150/0x152/0x154`, range 1,000,000) when any is non-zero | m |
| 16, 17 | `+0x68`, `+0x6c` | 16.16 | **size**: width and height of the bullet quad (`0x0047f0a0(w, h, len)`, `0x0047eef0`); radius `+0xa4 = max(ext+0x78, w, h) · node scale` (`0x00441bd9..`) | m (call args), i (names) |
| 18 | `+0x70` | 16.16 | beam length for flags 4 and 8 (`ext+0x78`, `0x00441193`, `0x004411d0`) | m |
| **19** | **`+0x74`** | int | **flight effect**: Effects row attached to the projectile, `0x004148a0(kind 0, k 0, eff, bullet)` at `0x004146c0` | m |
| **20** | **`+0x78`** | int | **impact effect** (`0x00413d5d`, `0x00413e2e`) and, for flag `0x100`, the **death explosion** row (`0x00413109`) | m |
| **21** | **`+0x7c`** | int | **muzzle effect** (`0x00413c0c`) | m |
| 22 | `+0x80` | int | damage argument 4 (hull stage) of `0x00449a60` | m |
| 23 | `+0x84` | int | **trail**: Particles3 id → `0x00412d70` at `0x004146fa` (also needs `VideoD3DFlags` bit 30 and `VideoD3DFlags2 & 2`) | m |
| 24 | `+0x88` | int | **not an Effects id**: ESI to `0x00488070`, which writes body `+0x18` (word) and node `+0x144` (`0x0044129e`); a texture/animation override **[i]** | m (writes) |
| 25 | `+0x8c` | int | no reader found | i (scan) |
| 26 | `+0x90` | int | node `+0x124` each frame (`0x0045b345`); flag 2: `·alpha/255` (`0x0045b380`) **[i]** glow/alpha | m |
| 27 | `+0x94` | int | flag `0x2000`: beam end `ext+0x90 = now + value` (`0x004469fa`); added to the lifetime at creation (`0x00440154`) | m |
| 28, 29 | `+0x98`, `+0x9c` | int | flag `0x200` (slow): victim `ext+0x914 = value·65536/100`, `ext+0x918 = now + value` (`0x00449db0`) | m |
| 30 | `+0xa0` | int | flag `0x400` (energy drain): victim `ext+0x2b4 −= value·1000` (`0x00449e20`) | m |
| 31, 32 | `+0xa4`, `+0xa8` | int | flag `0x800`: victim `ext+0x920`, `ext+0x91c` (`0x00449e70`) | m |
| 33, 34 | `+0xac`, `+0xb0` | int | flag `0x1000` at expiry: spawn col-34 count of fragment bullets of type col 33 (`0x00449ee0`, `0x0043f900`) | m (reads), i (count) |
| 35–38 | `+0xb4..+0xc0` | int | `+0xb8` `0x0045a98f`; `+0xbc`/`+0xc0` `0x00466b05`/`0x00466b97` (defaults `+0x44`/`+0x80` for version < 0x37: out-of-sector damage **[i]**); no `+0xb4` reader found **[i]** | m (readers) |
| 39–49 | — | — | common tail (col 48 is the `SS_BULLET_*` name) | — |

**Flag word (col 12, `+0x58`) as tested by the EXE** ([`tbullets_flag_tests.py`](../../verification/results/weapon-effect-spawn/tbullets_flag_tests.py) →
[`_out.txt`](../../verification/results/weapon-effect-spawn/tbullets_flag_tests_out.txt) for the table-pointer
sites; the fire path `0x00445170` tests the same word as `[rec+0x44]+0x14`, read from its decompile):

| bit | meaning in the EXE | sites | tag |
| --- | --- | --- | --- |
| `0x2` | beam kind A: tied to the gun, refreshed each fire tick (`0x00447ff0`), length `ext+0x78 = 0x00444bd0(obj)/2`, node `+0x138 = 10000` (`0x00441c7e`); with `& 6` the contact damage is scaled by `0x0042fb20` (frame step **[i]**) | `0x0044112f`, `0x00441c74`, `0x0045b365`, `0x0045e321` (`&6`) | m |
| `0x4` | beam kind B (`0x004482a0`), length col 18 | `0x0044117e`, `0x004432e1`, `0x004482c2`, `0x0045aa66` | m |
| `0x8` | fixed length col 18 (`0x0047eb90`); skips the radius from node `+0x70` | `0x004411bb`, `0x00443308`, `0x0045aa76`, fire path | m |
| `0xc` | swept-pass branch for beam kinds B and fixed-length | `0x0045de5f` | m |
| `0xe` | "beam-like": not removed on contact (`0x0045ec78`) | `0x0045ec78`, `0x0045ed93` | m |
| `0x20` | hull damage even when the shields absorbed | `0x00449bbe` | m |
| `0x40` | uses ammunition (`UseAmmunition` / `GetAmmunitionCountReload` script calls) | `0x00433f0e`, fire path | m |
| `0x80` | repair: hull damage negated (`0xa0` scales by 96–100 %) | `0x00449bf1`, `0x00449c3b` | m |
| `0x100` | **area/flak: explodes on death with row col 20**; speed zeroed | `0x00413100`, `0x004136cd`, `0x0044ac24` | m |
| `0x200`, `0x400`, `0x800` | on-hit victim effects (slow, energy drain, col 31/32) | `0x0045e397`, `0x0045e3b8`, `0x0045e3db` (+ second path) | m |
| `0x1000` | fragments at lifetime expiry (`0x00449ee0`) | `0x00453b17` | m |
| `0x2000` | continuous beam with its own end time (col 27); `& 0x2006` keeps the muzzle offset in `ext+0x10..` and sets node `+0x12c \|= 0x10000`; shield damage argument += `ext+0x94` (`0x0045e372`) | `0x00447f3d`, `0x0045e369`, `0x0045e54f`, fire path | m |

Bits `0x1` and `0x10` are set in stock rows but no test of them was found **[i]** (the scan misses spilled record
bases and the `test BYTE PTR [..+0x58],0xa0` at `0x00449bf1`, which sits behind a `jmp` and was read by hand).

## 2. Projectile spawn (question 2)

**Fire routine `0x00445170(ship, target, gun group, flags, gun mask)`** (four callers in `0x00461b04..0x00461b66`)
**[i: decompile, m: offsets quoted]**. Only class 7 fires through it (the group count is TShips `+0x180`). For each
installed laser object (class 8, `0x0044e590(8)`, kept at `[ebp-0x18]`): TLaser `+0x4c` (col 9) = bullet index,
TLaser `+0x44` (col 7) = refire interval, TLaser `+0x48` (col 8) = fire sound (`0x0049b1f0` at `0x00446b5c`); laser
`ext+4` (word) = gun slot `k` (`0x004452ea` → `[ebp-0x58]`). The muzzle position comes from the gun part of the
group's scene (TShips `+0x184 + group·0x138`, part key position, through the parent chain) **[i]**.

Order per shot:

1. **Muzzle effect** at `0x00446268` → `0x00413c00` (only when no live beam is refreshed, `local_4c == 0`):
   `EAX` = bullet index, `EDX` = `&orientation` (12 dwords, `[ebp-0x200]`, from `0x004f0270`), stack (cdecl,
   caller pops `0xc`) `[esp+4]` ship, `[esp+8]` gun slot k, `[esp+0xc]` `&pos` (`[ebp-0x70]`, 16 bytes). The callee
   reads **TBullets `+0x7c` (col 21)** and, when it is non-zero, calls
   `0x004148a0(4, k, eff, ship, 0, 0, 0, &pos, 0, &orientation)` **[m]**. The instance is keyed (ship, 4, gun slot),
   so every shot re-arms the same instance. `pos` is relative to the ship (the instance is parented to the ship,
   like the engine-effect `&pos`) **[i]**.
2. **Bullet object**: `0x0043f900(mem 0x130, bullet index)` creates class 0, subtype = TBullets index. The
   creation case (`0x00440139..0x004401b6`) sets `obj+0x40 = 0x40004801`, node `+0x130 |= 0x20800000` (the
   `0x20000000` group bit selects `INSTANCE_BULLETS`, [effects-engine-remaining-emission.md](effects-engine-remaining-emission.md)), `ext+8` = now **[m]**.
3. Fire path fills the bullet (**[i]** decompile; offsets as written): `ext = obj+0x50`; `ext+0` = expiry
   (now + col 10); **`ext+4` = owner ship**; **`ext+0x70` = the laser object** (class 8; its `+0x4a` is the TLaser
   index) and the laser's `ext+0xc` = this bullet; `obj+0x3c` = owner's `+0x3c`; node `+0x30` = muzzle world
   position, node `+0x100` = velocity vector, `obj+0x10/+0x14` = speed; `obj+0x18/+0x1c` = heading angles;
   `obj+0x44 |= 0x100` for a player-flag shot, `|= 0x400` inherited. Beams (`& 0x2006`) also keep the muzzle
   offset `ext+0x10..0x1c` and `ext+0x50..`/`ext+0x60..`.
4. Light flash `0x00444a70` (ESI = bullet; cols 13–15) when `[*0x0060850c+0x38]` (the player ship **[i]**) is set,
   the shot is within 100,000 and a rate counter set by `[*0x00606f34+0x748]` allows it **[i]**; fire sound.

**Flight effect and trail.** The projectile's own attached effect is **col 19** (`+0x74`) and its trail is
**col 23** (`+0x84`), both driven per frame by `0x00414590`'s class-0 branch (`0x00414669..0x004146fa`) **[m]**.

**Beams.** Same class-0 object and spawn; flag 2 → `0x00447ff0`, flag 4 → `0x004482a0`, flag `0x2000` → end time
col 27. A live beam is refreshed through the laser's `ext+0xc` (the `else` branch of the fire routine), which does
not respawn the muzzle effect **[i]**.

**Lifetime end.** `0x00452ad0` (`0x00453b17`): when `now ≥ ext+0`, flag `0x1000` spawns fragments, then
`0x0044abd0(bullet, reason 4, 0, 0)` **[i]**.

## 3. Impact (question 3)

**`0x00413c40(victim, source)` is the impact effect [m].** Single caller `0x00449d85`, inside `0x00449a60` after
the shield and hull stages. `cdecl`, 2 dwords, plain `ret`, returns 1 (0 when either node is missing). Logic:

* contact point: the **smaller** body (`+0xa4` radius compare, `0x00413c84`) gives
  `p = node+0x30 + R(node+0x40) · node+0x180` (`0x004f0da0`, `0x0040e750`) **[m]**; the meaning of node `+0x180`
  (local centre or last-contact offset) is **[u]**;
* whichever of the two is class 0 or 10 (victim tested first) is the projectile; the other is the hit object.
  `pos = Rᵀ(hit node) · (p − hit node+0x30)` (`0x00412490`, `0x004f0c00`): the point in the hit object's frame **[m]**;
* row: **TBullets `+0x78` (col 20)** for class 0 (`0x00413d5d`/`0x00413e2e`), **TMissiles `+0x68` (col 20)** for
  class 10 (`0x00413d78`/`0x00413e49`) **[m]**; the missile rows are 158–161 stock and 400–403 installed **[m data]**;
* `0x00414410(hit, 3, eff, &pos)` spawns the `EEDF_PROPAGATE` (`0x4000`) children (`0x00413eb0`), then
  `0x00414a70(3, eff, hit, &pos, 0)` with **ESI = projectile id** (`[proj+8]`) as k. `0x00414a70` handles
  `EEDF_PUSH` (`0x10000000`) by resolving the id in the object hash (`*0x0060850c + 0x14`) and passing it as the
  source; it calls `0x004148a0(3, id, eff, hit, src|0, 0, 0, &pos, 0, arg5)` **[m]**. The returned instance is
  **tested** (`0x00413db0`) and filled: `+0x70..+0x9c` ← projectile node `+0x40` (12 dwords), `+0x14` = projectile **[m]**.
* No projectile on either side (ship–ship contact): no effect, returns 1 **[m]**.

**Shield or hull.** `0x00449a60` passes no damage information and `0x00413c40` uses one row for both **[m]**: no
TShields column and no constant. Within a row, an `EEDF_HULLDAMAGE` element runs only while the hit object's
current shield is below 100 points (§9); no referenced impact row has one. The routine's local **absorbed** flag (`[esp+0x18]` of its frame) is still intact
at the call **[m: the only writers are `0x00449a83` (0), `0x00449b95` (1) and the transient capacity at `0x00449b6c`,
which `0x00449b95` overwrites on the same path]**. Its exact meaning **[m listing]**:

* it is set to 1 when, walking the installed shields (`[[victim+0x50]+0x6c]`), one shield has a remaining
  shield-stage damage `d` below its capacity (`0x00449aed`) and an energy **strictly greater** than
  `d·0x3e80000/capacity` (`0x00449b1a jg 0x00449b58`, then `0x00449b95` with no branch out). This includes `d = 0`
  against any shield with energy > 0, so a zero-damage hit on a charged shield also sets it;
* it stays 0 for unshielded classes (only ship, class 1, class `0x12` or flag bit 20 run the stage, `0x00449a76..`),
  for a victim without a charged shield, and when the shields were drained (the hull then takes the remainder);
* it does not mean "no hull damage": a class-0 source with TBullets flag `0x20` sets ECX = 1 and jumps past the test
  (`0x00449bbe..0x00449bca` → `0x00449bd8`), so the hull stage runs with absorbed = 1. Installed, that is the repair
  laser (`0xa2`, hull damage negated); stock, the two `0x20` rows;
* the hull stage also needs a non-zero hull argument (`0x00449bd8`).

So a shield-hit look is `absorbed == 1 && !(source class 0 && flag 0x20)`; anything else reached the hull or had no
shield.

**`0x0049b1f0` is the positional sound player [i] (decompiler reading; the role is not checked instruction by
instruction):** `(channel, handle, sound id, node)`; id/100 = bank (≤ 12),
id%100 = entry in the bank table `0x00591910` (0x20-byte entries with volume and random-pitch fields), positioned
against the listener nodes of the node's scene, played through `0x0049a700`. It is not an effect. Its weapon
callers: `0x00417250` (impact sound: TBullets col 9; `0x3e0` for TShips subtype `0x113`; `0x387`/`0x388` by
radius) and `0x00446b5c` (TLaser col 8 fire sound) **[m]**.

## 4. Explosions (question 4)

**Kill `0x0044abd0`** (`thiscall`, ECX = obj; `[ebp+8]` reason word, `[ebp+0xc]` killer, `[ebp+0x10]`; `ret 0xc`;
11 callers) **[m]**. It sets `obj+0x40 |= 0x8000000` once. Reasons `0xe` and `0x10..0x15` skip the explosion.
With `obj+0x40 & 0x40000000` (every bullet: `0x40004801`; also the invincible flag of other objects,
collision-damage.md §1.4) the explosion runs only when **TBullets[`obj+0x4a`]** has flag `0x100`: `0x0044ac0d..0x0044ac24`
tests the flag and indexes TBullets by the object's own subtype **without checking the class** **[m]**. A stub on
`0x0044ac64` must therefore not assume class 0; `0x00413040` still picks the row by the real class. It also
runs only when the object's parent is a sector (`[obj+0x54]+0x48 == 1`). The two calls are `0x0044ac64` (area
bullet, or any object with that flag) and `0x0044acb2` (everything else): `0x00413610(obj, killer, arg4)`, then the explosion sound `0x00417170`
when the object is in the player's sector **[m]**. Whether the killer argument is a pointer or an object id is **[u]**
(it is forwarded to `0x0045eeb0` and `0x00439ff0`). Inside `0x00413610` the killer is read only by the missile skip
(`0x0041369b`); the instance `+0x18` receives the **third** argument (`[ebp+0x10]`, §10.4), not the killer **[m]**.

**Row choice `0x00413040`** (ECX = subtype, EDX = class, `[esp+4]` = `obj+0x40`; default row **48**) **[m]**:

| class | row | column | stock rows | Mayhem rows |
| --- | --- | --- | --- | --- |
| 7 ship | TShips `+0xbc` | **col 47** | 35 (M3–M5, TS, TM, most TP, drones, 2 of 29 M6), 37 (M1, M2, M7, TL), 40 (27 M6, M8, Khaak, orbital laser, 4 TP); one row 0 → 48 | 600 / 602 / 601 by the same tiers (one M6 row has break-up 38, one M7 row 34); the one M0 row uses **40, which is missing from the installed Effects table** |
| 5 dock, 6 factory | `+0x60` | col 14 | 37, 201 (docks); 35, 37, 201 (factories) | 602 |
| 17 asteroid | TAsteroids `+0x48` | col 8 | 59–67 (one per subtype) | 59–68 |
| 10 missile | TMissiles `+0x7c` (only if `obj+0x40 ≠ 0`) | col 21 | 39, 158–161 | all 0 (default 48) |
| 0 bullet | TBullets `+0x78` when flag `0x100` | col 20 | 9 rows (flak, PSP, MAM, fragments) | none |
| other | 48 | — | | |

(per-class tiers from [`weapon_effect_census_out.txt`](../../verification/results/weapon-effect-spawn/weapon_effect_census_out.txt), **[m]**.)

The TShips column numbering accounts for the 12-column loop at cols 31–42 (`0x00437640..0x00437662`); with it col 49
is `+0xc4` (the trail, engine-effects.md) and col 52 is `+0xc8` (`OBJ_SHIP_*` via the text-id reader `0x004ea440`)
**[m]**. The break-up row chooser `0x00413140` reads TShips `+0xc0` (col 48), TDocks/TFactories `+0x64` (col 15),
TAsteroids `+0x4c` (col 9), TMissiles `+0x80` (flag `0x1000000`), default **36** **[m]**.

**`0x00413610(obj, killer, arg4)`** (`cdecl`, 3 dwords, plain `ret`, return unused) **[m listing; body i]**: returns
0 at once for a missing node or class `0x1c`. It also **returns 1 with no explosion for a class-10 object with
`obj+0x40 & 0x1000000` and a non-zero killer argument** (`0x00413685..0x004136b4`) **[m]**; the kill routine then runs
the missile's area damage `0x0045eeb0` (`0x0044ad18`). A death replacement must not add an explosion there. Area
bullets have their speed zeroed. Then it allocates a
**carrier object of class `0x1b`** whose subtype is the dying root body (`node+0x140`, `0x0043f900(mem, body|0x1b0000)`),
copies position, rotation and velocity, and spawns `0x004148a0(1, carrier id, row, carrier, second carrier or 0, arg3, a7, &zero, t0, 0)` (§10.4). Row flags (`0x0058006c + row·0x14`,
names at `0x00554d78`: `BREAKUP 1, PROPAGATE 2, REINIT 4, NOROTATE 8, NODRIFT 0x10, HIDEOBJECT 0x20`):
`HIDEOBJECT` hides the carrier node; without `NOROTATE` it gets a random spin; `PROPAGATE` makes a second carrier
with a copy of the dying scene (`0x004871e0`, `+0x4c/+0x4e` = the dying class/subtype) for the propagated
children **[i]**.

**Size.** The explosion is not scaled by the dying object at the spawn: the row is a per-type tier, and elements are
sized relative to the carrier's radius only with `EEDF_RELATIVESIZE`. The carrier's radius is its body radius
(generic `0x00488170`), the dying body **[i]**. Every death row reachable in either view, including the bullet
area-death rows (stock 15, 155, 156, 178, 186, 189, 196) and the default 48, has **no RELATIVESIZE and no BREAKUP
element**; the stock break-up rows 36/38/41 and the installed 650/651 have three RELATIVESIZE elements each
**[m data, census "death / break-up rows reachable"]**. Missing from the installed Effects table: death row 40 (the M0
row) and the break-up rows 34, 36 (the default), 38, 41, and the installed TDocks col 15 holds −1, which
`0x00413140` returns as a row (`jne` on non-zero, `0x004131b4`) **[m]**.

**Break-up `0x004132d0(obj)`** is triggered by an element with `EEDF_BREAKUP` (`0x2000`, `0x0041574d`) on an
instance whose `+0x14` holds the object; it spawns one class-`0x1b` carrier per body part with kind 5 and the row
from `0x00413140` (or the part's own effect), then removes the object (`0x0044aab0`) **[i]**. No reachable death row
has a BREAKUP element **[m data]**, so the break-up path and its missing installed rows are not reached from a kill.

**At the death site** (stub on `0x0044ac64`/`0x0044acb2`): `[esp+4]` = EDI = dying object; class `+0x48`, subtype
`+0x4a` → type record (TShips `+0xc8` = ship class, `+0xbc` = row), radius `+0xa4`, position node `+0x30`;
`[esp+8]` killer; the caller's EBP is `0x0044abd0`'s frame, so the reason is `word [ebp+8]` **[m]**.

## 5. The generic spawner (question 5)

`0x004148a0(kind, k, eff, obj, a5, a6, a7, &pos, t0, &orient)` **[m: engine-effects.md §7, listing]**: `cdecl`,
10 dwords, returns the instance (key obj/kind/k; eff is not part of the key, so a re-armed instance keeps its row).
Layout: `+8` row pointer (`0x00580068 + eff·0x14`), `+0x10` obj, `+0x14` a5 (source/attached object), `+0x18`
a6, `+0x1c` a7, `+0x20` kind, `+0x24` k, `+0x30..+0x3c` pos, `+0x40..+0x6c` orientation (3×4 16.16), `+0x70` (impact:
projectile rotation). It has no scale argument; sizing is per element (EEDF flags, obj `+0xa4`).

| return address | caller | kind | obj | role |
| --- | --- | --- | --- | --- |
| `0x004147f0` | `0x00414590` | 0 | ship / missile | engine emitter (redirected by `engine_effects_sites.h`) |
| `0x004146c5` | `0x00414590` | 0 | bullet | projectile flight effect (col 19) |
| `0x00413c34` | `0x00413c00` | 4 | ship | muzzle |
| `0x00414afd`, `0x00414b2c` | `0x00414a70` ← `0x00413c40` / script `0x00414b40` | 3 | hit object | impact (or script) |
| `0x00413a29`, `0x00413a6b` | `0x00413610` | 1 | carrier | death |
| `0x004135b0` | `0x004132d0` | 5 | part carrier | break-up |
| `0x004140a0`, `0x004143e3` | `0x00413eb0` (propagation) | inherited (3 in practice) | parent's obj | PROPAGATE children of impact, death and break-up rows |

One hook there can tell the role from the return address **[m: these are all 10 E8 callers]**, `kind` alone
cannot (kind 3 is also every propagated child). But it is called every frame for every engine jet, and it does not
carry the role context: for an impact the projectile is only an id (the caller writes `+0x14` after the return), the
shield/hull flag is in the damage routine's frame, and for a death `obj` is the new carrier, not the dying object.
**The specific sites are needed**; `0x004148a0` is only useful as a common "instance created" tap.

## 6. Hook sites (question 6)

All call bytes, callees and branch/pointer scans from
[`weapon_effect_sites.py`](../../verification/results/weapon-effect-spawn/weapon_effect_sites.py) →
[`_out.txt`](../../verification/results/weapon-effect-spawn/weapon_effect_sites_out.txt) (69,945 direct branches,
every image dword): **no branch or dword lands inside any of the call instructions below, and each is a whole
5-byte `E8 rel32`** **[m]**. The pattern is the engine-effects call redirect (`engine_patch::claim_call`, a stub
that reads its stack and jumps to or calls the original callee).

| site | callee, ABI | arguments at stub entry | register inputs to callee | return tested? | dead after return | live, preserve | atomic claim |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **impact `0x00449d85`** | `0x00413c40`, cdecl 2, plain ret | `[esp+4]` victim, `[esp+8]` source (bullet/missile/other); `[esp+0x24]` = absorbed flag (§3); `[esp+0x1c]` = the routine's running total, written at the end of every shield walk (`0x00449b9d`) and after the hull stage (`0x00449cd4`), clamped to 50,000 for a piloted victim (`0x00449cf9`) **[m]** | none (`0x00413c46` loads ECX from the stack) | **no** (`0x00449d8d mov eax,esi`) | EAX, ECX, EDX, EFLAGS (`add esp` then `test`; `0x00450310`/`0x00417250` take EAX/ECX set after) | ESI = victim, EDI = source (used at `0x00449d8d`/`0x00449d98`), EBX, EBP | no: five bytes `0x00449d85..89` cross `0x00449d88` (plain write in the install window) |
| **muzzle `0x00446268`** | `0x00413c00`, cdecl 3, plain ret | `[esp+4]` ship, `[esp+8]` gun slot k, `[esp+0xc]` &pos; caller EBP frame: `[ebp-0x18]` laser object, `[ebp-0x7c]` bullet index | **EAX = TBullets index, EDX = &orientation** (forward unchanged) | no (EAX reloaded at `0x0044626d`) | EAX, ECX, EDX, ESI (`xor esi,esi` at `0x00446273`), EFLAGS | EBX (realigned frame base), EBP, EDI | yes (`0x00446268..6c` in one qword) |
| **death `0x0044ac64`** (area bullet) / **`0x0044acb2`** (others) | `0x00413610`, cdecl 3, plain ret | `[esp+4]` dying obj, `[esp+8]` killer, `[esp+0xc]` arg4; caller EBP: `word [ebp+8]` reason | none | no (`mov eax,edi` after `add esp`) | EAX, ECX, EDX, EFLAGS | EDI = obj, EBX (0, used at `0x0044acf6`), ESI, EBP | `0x0044ac64`: no (crosses `0x0044ac68`); `0x0044acb2`: yes |
| flight `0x004146c0` / trail `0x004146fa` | `0x004148a0` cdecl 10 / `0x00412d70` stdcall 4 | as engine-effects site A/B with obj = bullet (ESI) | none | no | EAX, ECX, EDX, EFLAGS | ESI, EDI, EBX, EBP | yes / yes |
| break-up `0x0041575c` | `0x004132d0`, cdecl 1 | `[esp+4]` obj | none | **yes** (`0x00415764` stores EAX) | — | — | no |
| impact instance `0x00413da8` / `0x00413e79` | `0x00414a70`, cdecl 5 | `[esp+4]` 3, `+8` eff, `+0xc` hit object, `+0x10` &pos (hit frame), `+0x14` 0 | **ESI = projectile id** | **yes** (instance, `0x00413db0`/`0x00413e81`) | — | EBX/EDI = projectile | yes / yes |

Reentrancy and thread **[i]**: all four sites run on the game-update thread. Callers of the damage routine
`0x00449a60` (and so of the impact site) **[m: Ghidra references and containing functions]**: the collision response
`0x0045e130` (six calls), the planet branch of `0x0045d250` (`0x0045dc43`), `0x0044f7b0` (`0x0044fab1`), the KC native
`0x00460630` (`0x00463518`) and the **area damage `0x0045eeb0`** (`0x0045f054`). `0x0045eeb0` is called from the kill
routine itself (`0x0044ad18` for an area missile, `0x0044ad6c` for TBullets subtype `0x1c`) and calls the kill back
(`0x0045f110`). So after a death stub has returned, the same outer `0x0044abd0` can run impact stubs for every object
in the blast and nested kills, each with its own death stub. A stub is never re-entered through its own site while it
is running, but a death replacement must tolerate impact and death events for other objects arriving inside the same
kill, and state shared between the stubs must not assume one event per kill. An impact instance's first update
(`0x00414c10` → `0x00414cf0`) can, for a row with an `EEDF_BREAKUP` element, call `0x004132d0` and remove the victim
inside the impact call; no reachable row has one.

Both `0x00445170` (`0x0044518d..0x00445194`) and `0x00413610` (`0x00413616..0x00413624`) install an SEH frame
through `fs:0`, so the muzzle stub runs inside the fire routine's EH region and the death callee sets up its own; a
stub must not unwind across them. The fire routine's try state at `0x00446268` is not checked **[u]**.

Recommended taps for the replacement: **impact `0x00449d85`** (victim, source, shield/hull flag). For a class-0
source: the bullet record via `source+0x4a`, the weapon via `[source+0x50]+0x70` and the owner via `[source+0x50]+4`
(the bullet extension layout of §2). For a class-10 source: the TMissiles record via `source+0x4a` (impact row
`+0x68`); the missile extension layout is not traced **[u]**, so no owner or launcher read is established.
Any other class has no impact effect. **muzzle `0x00446268`**
(ship, gun slot, pos, orientation, bullet index, laser), **death `0x0044ac64` + `0x0044acb2`** (dying object, class
record, killer, reason), and optionally the projectile flight/trail at `0x004146c0`/`0x004146fa`.

## 7. Data census (question 7)

[`weapon_effect_census.py`](../../verification/results/weapon-effect-spawn/weapon_effect_census.py) →
[`weapon_effect_census_out.txt`](../../verification/results/weapon-effect-spawn/weapon_effect_census_out.txt)
(about 1 s) **[m]**:

| | stock (`addon/02.cat`, 45 rows) | installed (`addon/12.cat`, 38 rows) |
| --- | --- | --- |
| Effects rows (`types/Effects`) | 127 (`addon/01.cat`) | 158 (`addon/10.cat`) |
| impact col 20, distinct non-zero | **26**, all present: 15–19, 23, 50, 53, 57, 58, 69, 70, 151, 155, 156, 178, 180, 182, 184, 186, 189, 192, 194, 196, 198, 199 | **15**: 18, 200–203, 206, 207, 210, 215, 224, 230, 233, 235–237; all present except 18 |
| muzzle col 21, distinct non-zero | **21**, all present; 5 rows 0 | **22**, 21 present; 129 missing; 1 row 0 (range 100–137) |
| flight col 19 | 2 (170, 172), 42 rows 0 | 3 (308–310), 33 rows 0 |
| trail col 23 | 1 (id 1), 44 rows 0 | none |
| col 24 (not an effect) | 111–113, 1156 | 122 |
| flag values (col 12) | `0x0`×8, `0x1`×2, `0x2`×8, `0x8`×2, `0x40`×2, `0x60`, `0xa2`, `0x100`×4, `0x140`×2, `0x214`, `0x300`, `0x400`, `0x800`, `0x802`, `0x808`, `0x1000`, `0x1200`, `0x2000`, `0x2100`×2, `0x2600`, `0x2800`, `0x3000`×2 | `0x0`×36, `0x2` (PBC), `0xa2` (repair laser) |
| bits present (row count) | `0x1`:2 `0x2`:10 `0x4`:1 `0x8`:3 `0x10`:1 `0x20`:2 `0x40`:5 `0x80`:1 `0x100`:9 `0x200`:4 `0x400`:2 `0x800`:4 `0x1000`:4 `0x2000`:7 | `0x2`:2 `0x20`:1 `0x80`:1 |
| light colour (cols 13–15 non-zero) | 45 / 45 | 38 / 38 |

Mayhem keeps no area/flak, fragment, slow, drain or `0x2000` beams: every installed projectile is a plain bolt
except PBC (flag-2 beam) and the repair laser. An impact or muzzle replacement keyed on (site, record) therefore sees
36 plain bolt records, one beam and one repair beam in the installed view; its impact rows are the Mayhem rows
200–237 plus 18 (missing from the installed Effects table) for bullets, and 400–403 for missiles.

## 8. Follow-up for the design note (docs/architecture/weapon-effects.md §6, 2026-11-05)

Evidence: [`class_id_tables.py`](../../verification/results/weapon-effect-spawn/class_id_tables.py) →
[`class_id_tables_out.txt`](../../verification/results/weapon-effect-spawn/class_id_tables_out.txt) (EXE tables
read from the bytes, AP and FL; class columns per view), the `object` row of
[`weapon_effect_sites_out.txt`](../../verification/results/weapon-effect-spawn/weapon_effect_sites_out.txt), and
listings of `0x004ea440`, `0x0043cb60`, `0x004376fb..0x0043770c`, `0x00414590..0x0041489a`, `0x0045ac6d..0x0045ac9c`.

### 8.1 Class ids for a death-site tier (item 1)

**The text-id reader `0x004ea440`** (ECX = name table, one stack argument = the line parser, `ret 4`) **[m]**: text
that starts with a digit or `%` is parsed as a number (`0x004ea4dc..0x004ea4ea` → `0x004eb930`), so `0`, `2061` and
`2153` are taken literally. Otherwise it walks `{char *name, int id}` pairs (`[esi+ebp*8]`, `[esi+ebp*8+4]`) until
a name matches and returns its id. An unknown name runs into the table's empty-string end (`0x00554ef0`), returns 0
and sets the parser's byte `+0x24` (`0x004ea5d0`), after which this reader returns 0 at entry (`0x004ea45e`) **[m]**;
whether the other readers honour that byte too is **[i]**.

**TShips col 52 → `+0xc8`** uses the table `0x00552a28` (`mov ecx,0x552a28` at `0x004376fc`, store `0x0043770c`)
**[m]**. The AP table (35 names; the FL EXE `e7f3aad8…` has the same ids plus four):

| name | id | name | id | name | id |
| --- | --- | --- | --- | --- | --- |
| OBJ_SHIP_M0 | 2020 | OBJ_SHIP_TP | 2030 | OBJ_SATELLITE / 2 | 2065 / 2080 |
| OBJ_SHIP_M1 | 2021 | OBJ_SHIP_TS | 2031 | OBJ_BEACON | 2060 |
| OBJ_SHIP_M2 | 2022 | OBJ_SHIP_TL | 2032 | OBJ_JUMPBEACON | 2062 |
| OBJ_SHIP_M3 | 2023 | OBJ_SHIP_TM | 2141 | OBJ_MINE / 2 / 3 / TR | 2066 / 2143 / 2144 / 2145 |
| OBJ_SHIP_M4 | 2024 | OBJ_SHIP_TS_PIRATE | 2035 | OBJ_ORBITALLASER / 2 | 2011 / 2149 |
| OBJ_SHIP_M5 | 2025 | OBJ_SHIP_TL_PIRATE | 2036 | OBJ_FIGHTDRONE / 2 / USC | 2067 / 2146 / 2147 |
| OBJ_SHIP_M6 | 2026 | OBJ_SHIP_GONER | 2039 | OBJ_FREIGHTDRONE | 2092 |
| OBJ_SHIP_M7 | 2027 | OBJ_SHIP_UFO | 2068 | OBJ_RECONDRONE | 2150 |
| OBJ_SHIP_M8 | 2142 | OBJ_SHIP_ASTRONAUT | 2064 | OBJ_SPACEFLY | 2070 |
| OBJ_SHIP_KHAAKCLUSTER | 2131 | (FL only) OBJ_RELAYBEACON | 2063 | (FL only) OBJ_SHIP_SPECIALDRONE / DRONECARRIER / MISSILE_FRIGATE | 2151 / 2152 / 2153 |

There is no Khaak id other than the cluster (2131): the Khaak fighters' rows use the M-class names **[m table; the
per-row race check i]**. Numeric col 52 values in the data **[m]**: stock `0` ×6 (col 5 `SG_SH_M3` ×3, `SG_SH_M5`
×3; death row 35 ×5 and 0 → 48 ×1); installed none; FL `0` ×5, `2061` ×7 (all `SG_SH_M5`, death row 35), `2153` ×1
(`SG_SH_M7`, row 37; FL's own table names it `OBJ_SHIP_MISSILE_FRIGATE`). **2061 is in neither table**; it lies
between OBJ_BEACON 2060 and OBJ_JUMPBEACON 2062 and its name is **[u]**.

**The common column 5 (`+0x14`)** is a per-class group read through the same reader with the table from `0x0043cb60`
(ECX = class): 5 TDocks `0x00552840` (`SG_DOCK_TRADE 0, GONER 1, PIRATES 2, EQUIP 3, HQ 4, KHAAK 5, EXCHANGE 6`),
6 TFactories `0x005527e8` (`SG_FAC_SHIP 0, TECH 1, BIO 2, FOOD 3, POWER 4, MINE 5, STORAGE 6, COMPLEX 7, CCKIT 8`),
7 TShips `0x00552758` (`SG_SH_TL 0, TS 1, M0 2, M1 3, M2 4, M3 5, M4 6, M5 7, TL_P 8, TS_P 9, GO 10, M6 11, TP 12,
M7 13, TM 14, M8 15`), 8 TLaser `0x00552888`, 10 TMissiles `0x00552998` (`SG_MISSILE_LIGHT 0 … COUNTER 15`); any
other class gets ECX = 0, which the reader treats as numeric (`0x004ea4cf je`) **[m]**. TAsteroids col 5 is 0 in every
view **[m data]**. The other text-id columns (TDocks col 17, TFactories col 18, TMissiles col 26, TLaser col 12) use
`0x00550bc0`, the `ICON_*` picture table, not a class **[m]**.

**The explosion row does not use either id**: `0x00413040` reads the per-type death column directly (§4) **[m]**.
A tier at the death stubs (`0x0044ac64`, `0x0044acb2`; `[esp+4]` = EDI = the dying object) therefore comes from plain
reads that the callee itself performs (`0x0041366f..0x00413676` read `+0x48`, `+0x40`, `+0x4a`) **[m]**:

```
class   = *(uint16_t *)(obj + 0x48)              // 0 bullet, 5 dock, 6 factory, 7 ship, 10 missile, 17 asteroid
subtype = *(int16_t  *)(obj + 0x4a)
rec     = *(uint8_t **)(0x00606fb8 + 4 * class) + subtype * 0xdb8   // check class < 32, rec base != 0,
                                                                     // 0 <= subtype < *(int *)(0x00607038 + 4 * class)
ship (7):      OBJ id *(int *)(rec + 0xc8)   SG_SH group *(int *)(rec + 0x14)   death row *(int *)(rec + 0xbc)
dock (5) / factory (6): SG_DOCK / SG_FAC group *(int *)(rec + 0x14)             death row *(int *)(rec + 0x60)
asteroid (17): subtype itself (TAsteroids col 5 is 0)                            death row *(int *)(rec + 0x48)
missile (10):  SG_MISSILE group *(int *)(rec + 0x14)                             death row *(int *)(rec + 0x7c)
```

The death stub on `0x0044ac64` must still test the class first (§4: that site is taken for any object with
`obj+0x40 & 0x40000000`). The engine does not bound-check `subtype` here; the check above is the stub's own **[i]**.

### 8.2 The projectile branch of `0x00414590` (item 2)

**Who calls it [m call site; i loop semantics].** `0x00414590` has one caller, `0x0045ac7d` in the sector update
`0x004596e0`. That routine walks all 32 per-class object lists of the sector (`[sector+0x50] + class·0xc`,
`local_148` 0..31) and reaches the call for every object that is not dead (`+0x40 & 0x8000000`) and not skipped by
`+0x44 & 0x2000000` (`goto LAB_0045b3e9`), behind the gate `*(*0x0060850c) != 0` (`0x0045ac6d..0x0045ac76`). Class 0
objects are in those lists, so **every live bullet** enters `0x00414590` once per update pass **[i]**. The pass is
the per-frame object update ([engine-effects.md](engine-effects.md) §7) **[i]**.

**What the class-0 branch does [m, listing `0x00414669..0x004146ff`].** It loads `eff = TBullets[obj+0x4a]+0x74`
(col 19) and `trail = +0x84` (col 23). When both are ≤ 0 it jumps to `0x0041488f` and returns 1 without any call
(`0x00414689..0x00414693`). Otherwise it zeroes the 16-byte local `[esp+0x20..0x2c]` and:

* when `eff > 0`: `0x004148a0(0, 0, eff, bullet, 0, 0, 0, &zero, 0, NULL)` at **`0x004146c0`**. At stub entry
  `[esp+4]` = 0 (kind), `+8` = 0 (k), `+0xc` = eff, `+0x10` = the bullet (also ESI), `+0x20` = `&zero`,
  `+0x28` = NULL;
* when `trail > 0`, `VideoD3DFlags` bit 30 and `VideoD3DFlags2 & 2`: `0x00412d70(0, trail, bullet, &zero)` at `0x004146fa`.

So **a bullet whose col 19 is 0 never reaches `0x004146c0`**. Installed, 5 of 38 records have it (rows 308–310);
stock, 3 of 45 (170, 172) **[m data, §7]**. For those, the call repeats on every pass for the bullet's whole life:
the first call creates the instance (key bullet/0/0), later calls find it and re-arm it (`0x00414c60`, §5) **[m
structure; i per frame]**. It passes no position or orientation of its own: `pos` is the zero vector (the instance
sits at the bullet node's origin) and the orientation is NULL (identity at creation, unchanged on re-arm). A
recorder reads the bullet's pose from its node: `[obj+0x70]+0x30` position and `+0x40` the 3×4 rotation **[i]**.

**Bolt tap for step 4.** `0x004146c0` sees only bullets with a flight effect, so it is not a per-bolt tap. The tap
that sees **every live bullet each pass** is the call site **`0x0045ac7d`** (`E8 0E 99 FB FF` → `0x00414590`,
`cdecl`, one argument) **[m bytes]**:

| item | value |
| --- | --- |
| arguments | `[esp+4]` = obj (pushed from EBX at `0x0045ac7c`); the stub filters `word [obj+0x48] == 0` and forwards every object |
| register inputs | none (`0x00414590` reads only its stack argument) **[m]** |
| return tested | no: EAX is overwritten at `0x0045ac86` (`movsx eax,si`) **[m]** |
| dead after | EAX, ESI (reloaded at `0x0045ac82`), EFLAGS (`add esp` then `cmp`); ECX and EDX are caller-saved across the call **[m]** |
| live, preserve | EBX = obj (`0x0045ac82`), EBP, EDI |
| atomic claim | no: the five bytes `0x0045ac7d..0x0045ac81` cross the qword boundary `0x0045ac80` (plain write in the install window) |
| other references | sole E8 caller of `0x00414590`; no branch or image dword lands in bytes 1..4 **[m]** |
| composition | the engine-effects redirects at `0x004147eb`/`0x0041482c` sit inside the callee, so both can be installed together |

The drawn-bullet alternative is the `INSTANCE_BULLETS` collection (`0x0046cef0` per visible object,
[effects-engine-remaining-emission.md](effects-engine-remaining-emission.md)), which sees only bullets that are drawn
this frame **[i]**.

## 9. Victim shield energy and maximum at the impact stub (2026-11-05)

Evidence: [`shield_layout_sites.py`](../../verification/results/weapon-effect-spawn/shield_layout_sites.py) →
[`shield_layout_sites_out.txt`](../../verification/results/weapon-effect-spawn/shield_layout_sites_out.txt)
(27/27 instruction checks) and the decompiles of `0x0044ecd0`, `0x0044ee10`, `0x0044ef60`, `0x0043fe50`,
`0x00427d50`, `0x00414cf0`.

**Which objects have shields [m].** The predicate `0x0043fe50` (and the damage routine's own gate
`0x00449a76..0x00449a9e`) accepts class 7, class 1, class `0x12` and any object with `obj+0x40 & 0x100000` (bit 20).
Docks and factories get that bit at creation: class 5 `0x004409bc` and class 6 `0x004407f9` write
`obj+0x40 = 0x100000`, ships `or` it in at `0x00440621` (cases of the creation switch `0x004400d8`). So **stations keep
their shields exactly like ships**, in the same list; there is no separate station layout. Whether a script can later
clear bit 20 is **[u]**.

**Where the energy lives [m].** The installed shields are the class-9 objects in the owner's per-class list at
`ext+0x6c` (`ext = obj+0x50`; `0x6c = 9·0xc`, the same layout as the class-8 laser list the fire routine walks). The
list is a chain of object pointers through `node+0`; the node whose `+0` is 0 is the end sentinel, not a shield
(`0x00449ac7`, `0x0044ecd0`, `0x0044ee10` all stop there). There is no count field: the count is the number of
nodes before the sentinel. Per shield node:

| field | read | meaning |
| --- | --- | --- |
| `node+0x4a` | int16 | TShields index (`0x00449ad9`) |
| `node+0x50` | pointer | the shield's extension (`0x00449acf`) |
| `[node+0x50]+4` | int32 | **energy**, a 16.16 fraction of the capacity in thousandths: full is `0x3e80000` (1000.0). A new shield starts full (`0x004402a2`); recharge clamps at `0x3e80000` (`0x0044fa55..0x0044fa5d`); the damage stage subtracts `d·0x3e80000/capacity` (`0x00449b01`, `0x00449b8a`) or writes 0 (`0x00449b4a`) |
| `node+0x54` | pointer | owner; the damage walk restarts from the head when it differs from the victim (`0x00449ab0`) |
| TShields `+0x48` | int32 | **capacity** in hull points (col 8; table `*0x00606fdc`, count `*0x0060705c`) (`0x00449ae9`) |

**Current and maximum [m: formulas in the decompiles].** The engine's two getters are
`0x0044ecd0` (EDX = obj) = **Σ energyᵢ·capacityᵢ / 0x3e80000** (current shield points; collision-damage.md called it
the maximum, which it is not) and `0x0044ee10` (EAX = obj) = **Σ capacityᵢ** (maximum). There is no cached maximum
on the object. The shield setter `0x0044ef60` uses the same pair: it clamps to `0x0044ee10` and writes every shield's
energy as `value·0x3e80000/max`. **The HUD status bars** in `0x00427d50` draw bar 0 from exactly these two calls
(`0x00427cb0(0x0044ecd0(), 0x0044ee10(), …)`; bar 1 is the hull `obj+0xec` against `0x0044e910`) **[i: role of the
routine]**. So the shield percentage the player sees is the capacity-weighted sum, not the mean of the per-shield
fractions.

**Read recipe at the stub `0x00449d85`** (victim = `[esp+4]`, also ESI) **[m offsets; the bounds are the stub's own]**:

```
cls = *(uint16_t *)(victim + 0x48);
if (!(cls == 7 || cls == 1 || cls == 0x12 || (*(uint32_t *)(victim + 0x40) & 0x100000))) -> no shields
ext = *(uint8_t **)(victim + 0x50);            if (!ext) -> no shields
tab = *(uint8_t **)0x00606fdc;  n = *(int *)0x0060705c;   // TShields
cur = 0; max = 0;
for (node = *(uint8_t **)(ext + 0x6c), k = 0; node && *(void **)node && k < 64; node = *(uint8_t **)node, ++k) {
    int16_t i = *(int16_t *)(node + 0x4a);     if (i < 0 || i >= n) break;
    int32_t cap = *(int32_t *)(tab + i * 0xdb8 + 0x48);
    uint8_t *sx = *(uint8_t **)(node + 0x50);   if (!sx) break;
    cur += (int64_t)*(int32_t *)(sx + 4) * cap / 0x3e80000;   // per shield, truncated like 0x0044ecd0
    max += cap;
}
fraction = max > 0 ? (double)cur / max : 0;   // what the HUD bar shows
```

The walk is on the game thread with no allocation between the damage stage and the stub, so the list is the one the
damage stage just walked **[i]**. A node without `+0` next is the sentinel; a NULL head never occurs in the code
paths read (every reader dereferences it unchecked), but the stub should test it anyway.

**Pre- or post-hit.** The stub runs after the shield stage has stored the new energies (`0x00449b4a`, `0x00449b8a`)
**[m]**. No instruction of the routine between the stage and `0x00449d85` writes them **[m]**; the two callees in
between, `0x0044ec10` (hull) and `0x0042c7d0` (cockpit indicator), were read as not touching shields **[i]**. So the
energy at the stub is **post-hit**. This is the value the HUD bar would show this frame, if it draws after this update
**[i]**. The recharge `0x0044f7b0` runs from the per-object update, and its order against the collision pass within a
frame is **[u]**.

**Pre-hit from a register [m].** EBX at the stub holds the shield points this hit removed:

* it is zeroed at `0x00449a74`;
* each drained shield adds its points (`0x00449b51`) and the absorbing shield adds the remainder it took
  (`0x00449b8d`);
* the hull stage adds the hull damage (`0x00449cce`); no instruction between `0x00449cd8` and the call writes EBX.

When absorbed = 1 and the source is not a flag-`0x20` bullet, the hull stage is skipped (`0x00449bce`) and
**pre-hit shield points = cur + EBX** exactly; the truncation of each term is as in `0x0044ecd0`. When the hull stage
ran, EBX also contains the hull damage, and pre-hit = cur + EBX overstates by that amount. The shield-damage argument
is still in its slot (`[esp+0x34]` at the stub) except on the flag-`0xa0` path, which rewrites it at `0x00449bfd`.

**The engine's own shield/hull switch in Effects data [m].** An effect element with `EEDF_HULLDAMAGE`
(`0x80000`) runs only while the instance's object, which for an impact is the hit object, has `0x0044ecd0() < 100`.
The gate is `0x00414d98..0x00414daa` in the element update; the element then also plays a random hull-hit sound.
So the data can split an impact row into shield and hull elements. **No referenced impact row uses it**: in either
view the only row with such an element is stock 45, which is not an impact row
([`weapon_effect_census_out.txt`](../../verification/results/weapon-effect-spawn/weapon_effect_census_out.txt),
"impact rows, EEDF_HULLDAMAGE") **[m data]**. The threshold is 100 shield points, not a percentage.

## 10. Death-row element layout and the mask seam (2026-11-07)

Evidence: [`death_row_layout_sites.py`](../../verification/results/weapon-effect-spawn/death_row_layout_sites.py) →
[`_out.txt`](../../verification/results/weapon-effect-spawn/death_row_layout_sites_out.txt) (137/137 instruction
checks, and a scan of the row offsets `0x00414cf0` reads through the instance's row pointer); the block "death-row
element timelines" of [`weapon_effect_census_out.txt`](../../verification/results/weapon-effect-spawn/weapon_effect_census_out.txt);
the four "section 10" rows of [`weapon_effect_sites_out.txt`](../../verification/results/weapon-effect-spawn/weapon_effect_sites_out.txt);
decompiles of `0x004126d0`, `0x00414ba0`, `0x00414c10`, `0x00414c60`, `0x00414cf0`, `0x004148a0`, `0x00413610`,
`0x00414410`, `0x00413eb0`, `0x00416750`, `0x004871e0`, `0x0043acf0`, `0x00486cd0`, `0x00414b60` (untracked).

**Answer.** An element has **no kind id**. What it does is decided at its start by its EEDF flags and its body
field. **No element of a reachable death row is a body-only operation**: `WRECK`, `FADEOUTBODY`, `PUSH` and
`CUTKEEPBODY` are flags on sprite or scene elements. No ship, station or missile death row has a `LIGHT` or `LENSFLARE` element. Lights appear only in the stock bullet area rows 178, 189 and 196 and in child row 184. Elements play no sound except the `HULLDAMAGE` hull hit (`0x00414dfb`). The
instance **keeps a pointer to the static row** (`inst+8`) and re-reads the shared 0x70-byte records on every update.
It owns only 0x14 bytes of state per element. So a per-call mask that keeps the body operations needs a **clone row**
(option b): sprite elements made `DISTANT`, the `WRECK` element and a life timer turned into empty timed nodes,
`FADEOUTBODY` kept. Select the clone for one call by swapping the type record's death column around the forwarded
`0x00413610`. Two findings correct [explosion-look.md](../architecture/explosion-look.md) §1:

* `CUT` makes the element a multi-part scene. It does not cut the hull.
* The capital hull copy fades over 1 s once the propagation pre-roll ends: about 1.8 s stock, 6.0-6.4 s on Mayhem
  row 602. The 22 s is the debris scene.

`WRECK` acts only when the kill passes a non-zero third argument (section 4 had the killer there). Every direct
native kill caller (collision, area damage, five more native sites and the KC native with three arguments) passes 0, and whether the combat-death script path passes
one is **[u]**.

### 10.1 Row record `0x00580068 + id·0x14` (question 1) [m: loader `0x004126d0`]

| offset | type | content | readers |
| --- | --- | --- | --- |
| `+0x00` | int | element count (`0x00412894`) | `0x00414cf0`, `0x00416750`, `0x00414410`, `0x00414c60`; 0 → no state array (`0x0041494f`), instance dropped at its first liveness check (`0x00416ac5`) |
| `+0x04` | uint | `EDF_*` flags (`0x0041289f`, table `0x00554d78`), plus **`|1` for any `BREAKUP` element (`0x00412926`) and `|2` for any `PROPAGATE` element (`0x0041293a`)**: bullet area rows 155/156/186 are `PROPAGATE` at runtime with text flags 0 | death `0x00413766` (`HIDEOBJECT`), `0x00413858` (`NOROTATE`), `0x004138fa` (`PROPAGATE` → second carrier); re-arm `0x00414c64` (`REINIT`) |
| `+0x08` | int | 30000 (death rows), 15000 (row 149) (`0x004128ac`) | break-up `0x0041355a` only; not read on the death path **[m: no reference in `0x00413610`; `0x00414cf0` reads only `+0` and `+0x10` through `inst+8`]**; meaning **[u]** |
| `+0x0c` | number (`0x004eafc0`) | 1.0 (`0x004128c3`) | break-up `0x00413484` (with `NODRIFT`) only; meaning **[u]** |
| `+0x10` | pointer | element array, count·0x70 bytes, allocated with `0x004b89f0` and zero-filled (`0x004b8a97`); `0x004128d7` | everything |

Ids 0..999 (`0x00412831`). The loader zeroes count and pointer of all 1000 slots before a load (`0x004126ec` loop).
The reset `0x00412c80` frees every slot's `+0x10` with the EXE's `free` `0x0050e1b0` when the count is non-zero
(`0x00412ca8`). Free ids **[m census]**: installed 842, top block 901-999; stock 873, block 203-999.

### 10.2 Element record, stride 0x70 (question 2) [m: loader stores and the cited test sites; i: size and GROW semantics from the decompile]

| offset | content (loader store) | runtime use |
| --- | --- | --- |
| `+0x00` | body id (`0x00412a5c`): scene id through `0x0046e690` when `CUT`, body id through `0x0046e400` otherwise; light id (`sscanf %d`, `0x00412a28`) when `LIGHT`; −1 for `PROPAGATE` | node body (`0x00487e30`), scene (`0x00492970`), light record `*0x00608518+0xcc+id·0x18` |
| `+0x04` | `I3DF_*` (`0x00412911`; `I3DF_2D 0x4000`, `I3DF_HASLENSFLARE 0x20000000`) | OR-ed into the new node's `+0x12c`; `0x4000` selects the 2D roll (`0x00414f1f`) **[i: screen-aligned]** |
| `+0x08`, `+0x0c` | size a, b (`0x00412a70`, `0x00412a81`; 16.16 numbers **[i]**) | start size `+0x08` (+ random), `GROW` → `+0x0c` over the timeout, `RANDOMSIZE` random in `[0, +0x0c − +0x08)` into state `+0x10` (`0x00414e13`) |
| `+0x10` | timeout, int ms (`0x00412a92`) | `TIMEOUT`: end = start + timeout (`0x00416439`/`0x0041643d`, liveness `0x00416b32`) |
| `+0x14` | number (`0x00412aa3`) | used only when 0 < f < 1.0 (`0x00415f3b..0x00415f4c`); meaning **[u]** |
| `+0x20..+0x28` | offset x, y, z; numbers when `RELATIVESIZE`, ints otherwise (`0x00412aad`) | added to the node position (`0x00415a6a..`) |
| `+0x30` | int (`0x00412b27`) | lens-flare id (`LENSFLARE`, node `+0x1a0`, `0x004152e2..`); particle id for `PARTICLEGENERATOR` parts (`0x00412d70`) |
| `+0x34` | int (`0x00412b38`) | lens-flare `+0x1b0` |
| `+0x38` | number (`0x00412b49`) | **[u]** |
| `+0x3c` | `EEDF_*` flags (`0x00412902`, table `0x005530b8`) | below |
| `+0x40` | start delay, int ms, both branches (`0x00412b96`) | starts when `now − inst+0x28 ≥ +0x40` (`0x00414d79`) |
| `+0x44..+0x60` | `PROPAGATE` only (`0x00412954..0x004129cb`): `+0x44` child row, `+0x48`/`+0x4c` child count min/max, `+0x50`/`+0x54` gap a/b (ms), `+0x58` chain length, `+0x5c` random delay (ms), `+0x60` number (`WAVE` spacing) | `0x00414410`, `0x00413eb0` |

Bits and the instruction that acts on each **[m: test sites; i: effect where marked]**:

| bit | name | acts |
| --- | --- | --- |
| `0x1` | SPEEDDEPENDENT | size from speed (engine-effects.md §2) |
| `0x2` | RELATIVESIZE | size × obj `+0xa4` (`0x004161a5`); offsets read as numbers |
| `0x4` | LENSFLARE | flare on the node (`0x004152e2`) |
| `0x10` | TIMEOUT | end at `+0x40 + +0x10` (`0x0041642d`) |
| `0x20` | CUT | **scene element**: root node at the carrier (`0x00414e76`), one node per scene part (`0x00415085`); every frame it removes finished parts (part `+0x1f4` bit 0 clear or bit `0x200` set), and when none is left it ends (`0x004162d9..0x00416332`) |
| `0x80` | LIGHT | light node `0x004885a0` (`0x004155eb`) |
| `0x400` | FADEOUT | element-node alpha ramp, children too; not for `PROPAGATE` (`0x004161ea`/`0x004161f6`) |
| `0x800` | FADEOUTBODY | **static**: after the element loop, the last `FADEOUTBODY` element's `+0x40` sets a 1000 ms alpha ramp 255 → 1 on the **root node of `inst+0x10`**, only for a class-`0x1b` carrier (`0x00416638..0x00416718`); it is read from the static record in every phase, including a pending, masked or finished element; only a `DISTANT` element's start frame and a `HULLDAMAGE` element while its gate holds skip the read (`0x00414d93`, `0x00414daa` → `0x0041664c`) |
| `0x2000` | BREAKUP | `0x004132d0(inst+0x14)` (`0x0041574d`) |
| `0x4000` | PROPAGATE | nothing at its own start (empty node, body −1); its work is done at spawn (§10.6) |
| `0x8000` | RANDOMSIZE | `0x00414e13` |
| `0x10000` | PARTICLEGENERATOR | trail per scene part (`0x0041510a`); links removed at the end (`0x00416466`) |
| `0x20000` | DISTANT | **phase 2 at its start, never a node** (`0x00414d85..0x00414d93`) |
| `0x40000`, `0x8000000` | DEBRIS, DEBRISMAP | at the scene's end: `"CreateDebris"` event with `inst+0x18` (`0x00416342..0x00416368`); `DEBRISMAP` takes the scene from `inst+0x1c` (`0x00415011`) |
| `0x80000` | HULLDAMAGE | start waits while the obj's shield sum is ≥ 100 (§9) |
| `0x200000` | NOREINIT | kept on re-arm (`0x00414c86`) |
| `0x400000` | CUTACTUALSIZE | scene part scale base 0x10000 instead of 1000 (`0x00414eb1`) |
| `0x800000` | CUTKEEPBODY | finished scene parts are kept, not removed (`0x00416316`) |
| `0x1000000` | NOZTEST | node `+0x130 |= 0x18` (`0x00415f0b`) |
| `0x4000000` | WRECK | at the element's end (scene end `0x0041637a` or timeout `0x004165b0`), **only when `inst+0x18 ≠ 0` and `inst+0x10 ≠ 0`**: `0x00486cd0(node, 0x8100000, 0)` on the carrier's node tree (`0x100000` is the hide bit `HIDEOBJECT` sets, `0x00413776`), carrier `+0x40 |= 0x400`, the same on `inst+0x14`, then the script event `"CreateWreck"` (`0x0043acf0`, string `0x0055fb58`) with `inst+0x18` |
| `0x10000000` | PUSH | at its start, only when `inst+0x10` is a class-7 ship (`0x0041536d`): **a no-op on a death carrier** |

Other bits (GROWANDSHRINK, CAMERARELATIVE, GROW, RANDOMROT, BULLETROT, WAVE, NOSKIP) shape the node and were not
traced further.

### 10.3 Instance state and the first update (question 3) [m]

The state array (`0x00414ba0`: count·0x14 bytes, zero-filled, `inst+0xc`) holds, per element:

* `+0x0` the instance;
* `+0x4` a flag word (bit `0x10000` cleared at the end, `0x0041659e`);
* `+0x8` the node;
* `+0xc` the phase: 0 pending, 1 running, 2 done;
* `+0x10` the random size offset.

`0x00414ba0` creates no node. `0x00414c10` zeroes node and phase, sets the clock base `inst+0x28 = now + t0`
(`0x00414c47`) and runs `0x00414cf0`. The first update gives a node to **every element whose `+0x40` has passed**.
For a row with t0 = 0 that is every start-0 element. For a `PROPAGATE` row (t0 > 0) it is none.

Dispatch at the start, in test order:

1. `DISTANT`: nothing.
2. `HULLDAMAGE`: wait.
3. `CUT`: a scene root node plus one node per part.
4. `LIGHT`: a light node.
5. `BREAKUP`: the break-up of `inst+0x14`.
6. Body id > 0: a body node (2D with `I3DF_2D`).
7. Body id ≤ 0: an **empty node** (`0x0041594b`; no body, `+0x140 = −1`), drawn as nothing **[i]**.

Liveness (`0x00416ac0..0x00416b97`):

* An element with a node is alive until its node is flagged `0x2000` or its `TIMEOUT` end passes.
* An element without a node is alive only while pending.
* When nothing is alive, the instance is freed. A class-`0x1b` carrier with no other instance of the same obj, kind
  and row pointer is then removed (`0x00416c2d`/`0x00416c36`).

**The carrier, and with it the hull copy, lives exactly as long as its instance.**

Rows (installed 600/601 = stock 35/40; 602 = stock 37 with children at 200 ms + random 400 ms instead of 60 + 10; 201 = 37
at ten times the size with 40 children at 40 ms; census **[m data]**):

| row | element (start..end ms after t0) | what it is | body op |
| --- | --- | --- | --- |
| 35, 600 | 0: 11997, 0..3110, NOZTEST, CAMERARELATIVE | 2D body (the fireball) | — |
| | 1: 4264, 0..1900, GROW, RANDOMSIZE | CUT scene | — |
| 40, 601 | 0: 10670, 10..8010, and 1: 10670, 30..9030 (FADEOUT) | 2D body | — |
| | 2: 11998, 0..3110 | 2D body | **WRECK**, PUSH (no-op) |
| | 3: 4264, 0..1900 | CUT scene | — |
| 37, 201, 602 | 0: PROPAGATE → row 149 ×30 at 60 ms (201: ×40 at 40 ms; 602: ×30 at 200 ms) | empty node | spawn-time propagation |
| | 1: 4262, 0..3840 | CUT scene, 2D | — |
| | 2: 4262, 25..3865 | CUT scene, 2D | **WRECK** |
| | 3: 11999, 700..2780 (shock) | body (3D) | PUSH (no-op) |
| | 4: 11994, 780..4380 (flash) | 2D body | — |
| | 5: 10664, 0..7000 (dim red glow) | 2D body | **FADEOUTBODY** (start 0) |
| | 6: 10670, 1400..11300 | 2D body | — |
| | 7: body −1, 0..0 | empty node | — |
| | 8: 4264, 1800..3700, and 9: 4264, 1858..3758 | CUT scene | — |
| | 10: debrislarge_scene, 120..22120, CUTACTUALSIZE | CUT scene (debris) | **CUTKEEPBODY** |
| 149 (child) | 0: 4262, 0..3840, RANDOMROT, RANDOMSIZE | CUT scene, 2D | — |

Installed row 40 (the M0 row) is missing (§4). The default row 48 has a `FADEOUTBODY` CUT scene at start 0.

### 10.4 The carrier timeline (input to question 5) [m paths; i visibility]

* **t0.** `0x00413610` passes `t0 = (first BREAKUP element's +0x40, 0x00414b60 with ESI = 0x2000) + 0x00414410(…)`
  (`0x004139e0..0x00413a0f`) on the `PROPAGATE` path. The second term is the latest child start: count × gap +
  random. The other path passes 0 (`0x00413a66`). So the main explosion of row 37 starts at 1800-1810 ms, 201 at
  1600-1610 ms and Mayhem 602 at **6000-6400 ms** (30 × 200 + random 400) after the kill **[m data; i: assumes every
  child finds a surface point]**. The 30 surface explosions on the hull before it are the engine's own pre-roll.
* **Fade.** `FADEOUTBODY` fades the main carrier's root node over t0 .. t0+1000 ms (37/201/602, 48). 35/600 and
  40/601 have no fade: the hull copy stays until the carrier is removed at 3110 ms and 9030 ms. That end is exact,
  because the last end belongs to a body element.
* **Life.** For 37/201/602 the census life of t0 + 22120 is an **upper bound**. A CUT element ends at the earlier of
  "all parts finished" and its timeout, and whether the debris scene's parts count as running for 22 s is **[u]**.
* **WRECK** fires at t0 + 3110 (40/601) or t0 + 3865 (37/201/602) **only if `inst+0x18 ≠ 0`**.
  `inst+0x18` = a6 = **the third argument of `0x00413610`** (`[ebp+0x10]`, `0x00413a0c`/`0x00413a54`). The kill
  routine passes its own `[ebp+0x10]` (`0x0044acaf`). Every native kill caller pushes 0 there:
  * collision `0x0045dc70`;
  * area damage `0x0045f10a`;
  * `0x0045ed6f`, `0x0045ee91`, `0x004494c3`, `0x0045332f`, `0x00453b2d`;
  * `0x004634b9`/`0x004634d5`.

  Only the KC native passes a value, a fourth script argument (`0x0046345e`, `0x0046346c` → `0x00463478`/`0x0046349d`).
  Whether the script path a combat death takes (`"KilledBy"`, `0x004494d5`, `0x0045dc82`) supplies it is **[u]**.
* **Two carriers.** The main carrier (`inst+0x10`) is built from the root body only (`0x0043f900(mem, body|0x1b0000)`).
  The second (`inst+0x14`, `PROPAGATE` rows) is a full copy of the dying scene: body plus the whole part hierarchy
  (`0x004871e0` recursion). `FADEOUTBODY` touches only the main root. Whether both are drawn, and which one the
  player sees as "the hull" during the pre-roll, is **[u]**.

### 10.5 The seam (question 4)

`inst+8` is `&row` in the static table (`0x0041493f..0x0041494c`). `0x00414cf0` reads only `row+0` and `row+0x10`
through it, 2 and 43 times (`0x00414cf0..0x0041672d` scan), and the element records are never copied **[m]**. So:

* **(a) Per-instance phase flip** (state `+0xc = 2`, node removed with `0x00487be0`, as the re-arm `0x00414c9c` does)
  works after `0x00413610` returns. The main instance is then the list tail `*0x0057b100`: it is the last append
  (`0x004149fc`), with the children appended before it. For `PROPAGATE` rows no node exists yet. For t0 = 0 rows the
  start-0 nodes already exist and must be freed. It costs nothing to allocate, but it **cannot keep `WRECK`**,
  because that runs only at the end of a running element. It cannot keep the carrier alive either: a done element
  does not count, and every element of these rows is visual. `FADEOUTBODY` survives (§10.2).
* **(b) Clone row: recommended.** Build a clone of row R in a free slot R′ (count, flags, `+8`, `+0xc` copied).
  Allocate its element array with the EXE's `malloc` `0x005112c4`, the allocator `0x00414ba0` uses, so the engine's
  reset frees it with the matching `free`. Per element:
  * a visual element without a body op: `+0x3c |= DISTANT`;
  * the `WRECK` element: body `+0 = 0` (empty node), `+0x3c = TIMEOUT|WRECK`, `+4 = 0`, start and timeout kept;
  * a life timer: the CUTKEEPBODY debris element (or the last-ending element) made an empty `TIMEOUT` node with the
    row's end;
  * `FADEOUTBODY` kept on any element; its `+0x40` is then the hull fade start, and the pre-roll can retime it;
  * the `PROPAGATE` element: point `+0x44` at a second clone row R″ (one empty `TIMEOUT` element), or set
    `+0x48 = +0x4c = 0`.

  Select R′ per call. The **data swap** is the type record's death column: TShips `rec+0xbc` (read at `0x0041306e`),
  TFactories / TDocks `rec+0x60` (`0x0041308f` / `0x004130a9`; tables `0x00606fd0` / `0x00606fcc`). Write R′ in the
  death stub before forwarding and restore it after `0x00413610` returns. This needs no new patch site.
  `0x00413040` reads the column once, at `0x0041367d`. Nested kills run only after `0x00413610` returns (§6), so the
  window needs no stack **[i: no kill in its call tree for rows without BREAKUP]**. The code alternative is a
  redirect of `0x0041367d` (`E8 BE F9 FF FF` → `0x00413040`; ECX = subtype, EDX = class, `[esp+4]` = obj `+0x40`,
  returns EAX, caller pops at `0x00413682`). After the call EAX → EDX (`0x00413689`) → `[esp+0x10]`; EBX = class,
  ESI = obj, EDI = obj `+0x40` and EBP are live; ECX and EFLAGS are dead. No branch or image dword lands in its bytes
  1..4. It is not atomic, because it crosses `0x00413680` **[m]**. Returning 0 there skips the carrier and the spawn
  (`0x004136f4`/`0x00413706`).

  **Rollback:** stop swapping (or restore the five bytes). Instances already made keep `&slot[R′]` until they end, so
  the clone memory must outlive them: never free it, and let the reset free it. A reload zeroes the slot
  (`0x004126ec`), so rebuild when slot R′ reads count 0 or R's `+0x10` has changed.
* **(c) Node-creation hook** keyed by the carrier needs four `0x00486d10` sites (`0x00414e76`, `0x00415085`,
  `0x00415788`, `0x0041594b`) plus `0x004885a0`. It still lets WRECK ride on a node that has to be hidden every frame.

### 10.6 Per-element timeout (question 5)

Not per instance: start and timeout live only in the shared record, and the only per-instance time datum is the base
`inst+0x28`, which shifts all elements together **[m]**. In the clone (b) it can be done:

* sprites marked `DISTANT` are gone at their start (t = 0 for start-0 elements, never drawn for later ones);
* one empty `TIMEOUT` element with the row's end keeps the instance and the carrier alive to that end (liveness
  `0x00416b2c..0x00416b72`);
* the hull copy stays visible until the clone's `FADEOUTBODY` start + 1 s, the `WRECK` timer end (if `inst+0x18 ≠ 0`),
  or the carrier's removal, whichever comes first.

### 10.7 The PROPAGATE children (question 6)

**Sprites, separate, made before the parent [m].** `0x00414410(second carrier, 3, R, 0)` at `0x004139ff` runs
before the main spawn at `0x00413a24`. Per `PROPAGATE` element it calls `0x00413eb0` count times. Each call picks a
random point on the second carrier's mesh (`0x0047f560`/`0x0047fc20`, **[i]**) and spawns `0x004148a0(3, k, +0x44 = 149, second
carrier, 0, 0, 0, &point, delay, 0)` (`0x0041409b`; chain `0x004143de`), with delays `k·gap + random`. Row 149 is one
CUT 4262 scene element, `I3DF_2D`, 3840 ms: a 2D sprite scene. **Masking the parent does not mask them**: they are
their own kind-3 instances with obj = the second carrier. Under (b) the clone's `PROPAGATE` element decides them (child
row R″ or count 0). Under (a) the stub flips the single element of each kind-3 instance whose obj is the main
instance's `+0x14`, before any child's delay has passed.

The second carrier is removed with its last child instance (§10.3). With count 0 no instance references it, and its
removal is then **[u]**: clearing the clone's `EDF_PROPAGATE` bit avoids creating it (path `0x00413a43`, a5 = 0).

## Unknown

- The meaning of node `+0x180` in the contact point, and whether `0x00413c40` sees the true contact or the smaller
  body's centre.
- Whether the bullet's `ext+0x70` laser pointer can dangle after the laser is removed from the ship.
- Bits `0x1` and `0x10` of the TBullets flag word, and columns 25 and 35–36.
- That the muzzle `pos` is in the ship frame (follows from the parenting, not traced through the gun-part chain).
- Stations' and turret-platform weapons (class 5/6 do not fire through `0x00445170`); their fire routine was not
  traced.
- A reference to a row missing from Effects (installed impact 18, muzzle 129, death 40, break-up 34/36/38/41, and the
  −1 break-up row). The table is zeroed before the load (`0x00412700`, engine-effects.md §7), so a missing row has
  element count 0: `0x004148a0` then skips the element array (`0x00414953` is reached only for a non-zero count) and
  `0x00414410` returns 0 **[m]**. That such an empty instance draws nothing and is dropped on the next update is
  **[i]**. A −1 row would point 0x14 bytes before the table **[i]**; it is reachable only through a BREAKUP element.
- The missile extension layout (owner, launcher) for the impact tap.
- The fire routine's EH try state at the muzzle site.
- The name of class id 2061 (FL data, 7 M5 rows), and whether the parser error byte `+0x24` set by an unknown
  text id also stops the other column readers.
- That `0x004596e0` runs every frame for every sector (only the call chain is known).
- The order of the shield recharge `0x0044f7b0` against the collision pass in a frame, and whether bit 20 of a
  station's `obj+0x40` can be cleared after creation.
- §10: the meaning of row `+0x08` / `+0x0c` (read only by the break-up) and element `+0x14` / `+0x38`; whether
  both death carriers are drawn and whether the main carrier (root body only) shows the hull's parts; whether the
  combat-death script path (`"KilledBy"`) passes a non-zero kill third argument, without which `WRECK` and
  `"CreateWreck"` never run; when a scene part counts as finished (part `+0x1f4` bit 0), and so the real life of the
  22 s debris element; how a `PROPAGATE` carrier with no child instance is removed; how `0x004ea9d0` treats an
  unknown flag token (stock row 39 `EEDF_FADOUT`).

## Reproduce

```sh
L=<scratch>/x3.lst; i686-w64-mingw32-objdump -d -M intel /mnt/c/X3/X3AP.exe > $L      # 0.4 s, untracked
R=verification/results/weapon-effect-spawn
python3 $R/tbullets_field_readers.py $L > $R/tbullets_field_readers_out.txt
python3 $R/tbullets_flag_tests.py $L > $R/tbullets_flag_tests_out.txt
for c in "TBullets 436518 436770 55 esi" "TShips 4373e9 437a2c 50 edi" "TDocks 43717f 4372af 47 esi" \
         "TFactories 4372af 4373e9 47 esi" "TMissiles 437ab1 437c41 50 esi" "TAsteroids 437d52 437e72 47 esi" \
         "TLaser 437a2c 437a84 50 esi" "TShields 437a84 437ab1 47 esi"; do
  set -- $c; echo "== $1 ..."; python3 $R/type_loader_columns.py $L $2 $3 --version $4 --vreg $5
done > $R/type_loader_columns_out.txt      # loops expanded, version branches evaluated (decimal file versions)
python3 $R/weapon_effect_sites.py /mnt/c/X3/X3AP.exe $L > $R/weapon_effect_sites_out.txt
python3 $R/weapon_effect_census.py > $R/weapon_effect_census_out.txt
python3 $R/shield_layout_sites.py $L > $R/shield_layout_sites_out.txt   # §9, 27 checks
python3 $R/death_row_layout_sites.py $L > $R/death_row_layout_sites_out.txt   # §10, 137 checks + row-pointer scan
python3 $R/class_id_tables.py /mnt/c/X3/X3AP.exe /mnt/c/X3/X3AP.exe /mnt/c/X3FL/X3FL.exe \
  > $R/class_id_tables_out.txt                          # §8.1: EXE tables, FL anchor table, class columns per view
# Ghidra (raw output untracked): analyzeHeadless ~/x3-ghidra-research X3Render -process X3AP.exe -noanalysis
#   -readOnly -postScript X3DecContaining.java <out> 413c40 49b1f0 417250 449a60 414410 414a70 4148a0 413040
#   413610 4132d0 413c00 44abd0 445170 43ffa0 449db0 449e20 449e70 444a70 452ad0 449ee0 460630
#   §10: 4127c0 414ba0 414c10 414c60 414cf0 4148a0 413610 413eb0 416750 416e10 414410 412c80 43acf0 49f570
#        486cd0 487be0 486d10 414b60 4885a0 4871e0 43f900 469a30
```
