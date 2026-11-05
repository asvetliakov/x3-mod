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
TShields column and no constant. The routine's local **absorbed** flag (`[esp+0x18]` of its frame) is still intact
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
(it is forwarded to `0x0045eeb0`, `0x00439ff0` and the instance `+0x18`).

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
copies position, rotation and velocity, and spawns `0x004148a0(1, carrier id, row, carrier, …, killer, …, &zero, …)`. Row flags (`0x0058006c + row·0x14`,
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
python3 $R/class_id_tables.py /mnt/c/X3/X3AP.exe /mnt/c/X3/X3AP.exe /mnt/c/X3FL/X3FL.exe \
  > $R/class_id_tables_out.txt                          # §8.1: EXE tables, FL anchor table, class columns per view
# Ghidra (raw output untracked): analyzeHeadless ~/x3-ghidra-research X3Render -process X3AP.exe -noanalysis
#   -readOnly -postScript X3DecContaining.java <out> 413c40 49b1f0 417250 449a60 414410 414a70 4148a0 413040
#   413610 4132d0 413c00 44abd0 445170 43ffa0 449db0 449e20 449e70 444a70 452ad0 449ee0 460630
```
