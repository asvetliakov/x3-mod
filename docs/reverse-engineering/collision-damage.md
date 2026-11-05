# Collision damage: where it is computed and applied, and the patch that removes it (AP and FL)

2026-11-05. Static study of `X3AP.exe` (SHA-256 `fdbf3418…f8ab`, `C:\X3`) and `X3FL.exe` (SHA-256
`e7f3aad8…dfbb8`, `C:\X3FL`, preferred base `0x400000`, relocatable), and of the KC story objects the two installs
load (AP: Mayhem `addon/07.cat`, build 1462141839, with stock `addon/04.cat` build 1510658513 compared; FL:
`addon2/03.cat`, build 1636047691). Nothing was launched; no Wine. Instruction text from
`i686-w64-mingw32-objdump -d -M intel` over the files in place; raw listings stayed in the session scratchpad.
Builds on [sector-collide.md](sector-collide.md) §1–3 and §11 (the pass `0x0045d250`, the response `0x0045e130`),
[music-script-driver.md](music-script-driver.md) §2 and §9.1 (the by-name call `0x0049f4c0`, the four `AttackedBy`
sites), and for FL on [fl/collision-box.md](fl/collision-box.md) (FL pass `[0x466de0,0x467e9c)`). **[m]** = read
from the code this session, **[i]** = inferred.

| Script (tracked) | Output | What it establishes |
| --- | --- | --- |
| [`verify_collision_damage.py`](../../verification/results/collision-damage/verify_collision_damage.py) | `verify_collision_damage_output.json` | `PASS` on both images: EXE hashes, the response routine hashes, 13 byte windows, the caller census of the damage routine (10 each), the response routine (AP 5, FL 3), the hull subtractor (2 each) and the `"MakeDamage"` string (3 pushes each), no raw branch or abs32 into the patched spans, no FL relocation in them, the contact block entered only through its class gate, no write of `[reg+0xec]` in the collision code, and the KC `2004::MakeDamage` zero-damage early return in all three story objects |

## Answer

* **`"MakeDamage"` is not the damage path.** The engine computes the collision damage natively and applies it
  with the native damage routine (AP `0x00449a60`, FL `0x00453070`), which subtracts shields, then hull, and plays
  the impact sound. `"MakeDamage"` is a script notification sent beforehand, only to a ship of class 7 with a
  script object, and only when one of the two bodies is under manual control. In stock AP and FL its KC handler
  (`2004::MakeDamage`) damages and destroys **equipment** (wares, lasers, shields); in the Mayhem object this AP
  install loads it is a stub that returns 0. **[m]**
* **The law.** Strength `s(X) = 2·maxHull(X) + maxShield(X)`. Let `c = min(s_A, s_B)`, or the other one when the
  minimum is 0. With `v` = the Chebyshev norm of the two pre-contact velocities' difference, `d = c·v / 100000`.
  Each body takes `max(d, c/50)` if it is a ship (class 7), else `max(d, 100)`. If either body is a planet, both
  take `max(s_A, s_B)`; a planet's strength includes 10,000,000, so this kills. **Mass is not in the law**: it
  weights only the impulse and the cockpit hit indicator. **[m]**
* **Planet atmosphere.** Inside a band 750,000 units deep outside the planet's contact radius, a body takes, at a
  probability of `step/200` per frame, `maxHull·(depth/150)²·max(step,200)/1000`, where depth runs 0..150 in
  5,000-unit steps. **[m]**
* **Player and AI share one code path.** The player gets extra feedback only: `AttackedBy`/`MakeDamage`
  (manual control), `NotifyPlanetCollision` and the cockpit hit indicator (piloted flag `[obj+0x40] & 2`). **[m]**
* **Patch, per edition, two sites.**
  1. **Contact:** a detour at the join where both damage values are final. AP: `0x0045eaf4`, 7 B. FL:
     `0x00468a58`, 8 B. The stub zeroes both values, then replays the displaced instructions. `MakeDamage` and
     the native damage routine still run, with 0: the impact sound and the hit effect stay, and hull, shields and
     equipment are unchanged.
  2. **Planet atmosphere:** a one-byte change of `test` to `xor` at the damage gate. AP: `0x0045dbaf`
     `85→31`. FL: `0x004678bb` `85→31`. This forces vanilla's own "no damage this frame" branch.

  Weapons, missiles, blasts and scripts call the same damage routine from other sites and are not touched. **[m]**
  sites, **[i]** that nothing else depends on the damage amount (§6).

## 1. AP: the sites (Q1)

### 1.1 The response routine and its callers **[m]**

`0x0045e130`–`0x0045eea5` (3445 B, SHA-256 `3384552f…abab5`). It is `stdcall (A, B)` with `ret 8` and an EBP
frame aligned to 16. EBX = A (`[ebp+8]`) and ESI = B (`[ebp+0xc]`) throughout the contact block. Five callers,
all in the sector pass:

* `0x0045d233` (helper `0x0045d1b0`, class 20 subtype `0x5c`);
* `0x0045d2ff` (the swept pass L1);
* `0x0045d691` (the narrow-phase accept of the pair loop);
* `0x0045da3c` (planet, body inside the contact radius);
* `0x0045df5c` (class-0 projectile pairs).

The routine runs in this order:

1. "CreateRock", "CatchWare" (ware pick-up) and the self-hit guard.
2. The projectile branch: A or B is a bullet (class 0) or an armed missile (class 10 with `0x1000000`):
   `AttackedBy`, Flags2 `0x80000`.
3. The **contact branch**.
4. The kill check (`KilledBy` or the native kill `0x0044abd0`).

**Class gate of the contact branch.** It is `0x0045e6b0`–`0x0045e6d8`: neither A nor B may be of class 0 or 10.
Otherwise the routine jumps to `0x0045ec5f`. The block `[0x0045e6de, 0x0045ec5f)` is entered only by fall-through
from the gate. No decoded branch in the routine targets it from outside, and the raw scan of `.text` finds none.

### 1.2 The contact block, in order **[m]**

| VA | What |
| --- | --- |
| `0x0045e6de`–`0x0045e96c` | velocities `v = [obj+0x60..0x68] + [phys+0x100..0x108]` into `[esp+0x30]` (A) and `[esp+0x20]` (B); impulse weights `muldiv(0x10000, 2·m_other, m_A+m_B)` with mass `[obj+0xa0]` (`0x00469a30`); impulse into the velocities unless `[obj+0x44] & 0x80`; force feedback `0x004d5340` when either body has the piloted flag **[i]** role |
| `0x0045e96f`–`0x0045e9f4` | contact `AttackedBy(other id, 0)` to each body with a script object, when either is under manual control (`0x0043ff80`) and neither is TShips subtype `0x10b` (`0x004191c0`) |
| `0x0045e9f7`–`0x0045ea29` | strengths: `edi = 2·0x0044eb90(A) + 0x0044ecd0(A)`, `eax = 2·0x0044eb90(B) + 0x0044ecd0(B)` |
| `0x0045ea2d`–`0x0045ea3c` | either class 4 (planet): go to `0x0045eae7` |
| `0x0045ea42`–`0x0045ea5a` | `c = min(s_A, s_B)`, or `max` when the minimum is 0; kept in `[esp+0xc]` |
| `0x0045ea5e`–`0x0045ea80` | `v = 0x00419190([esp+0x20], [esp+0x30])`, the Chebyshev distance of the two velocity vectors, kept in `[esp+0x1c]`; `d = c·v / 0x186a0` (ECX) |
| `0x0045ea82`–`0x0045eab6` | `dmgA` into `[esp+0x10]`: ship `max(d, c/50)` (`0x51eb851f`, `sar 4`), else `max(d, 100)` |
| `0x0045eaba`–`0x0045eae5` | `dmgB` into EDI, by the same rule |
| `0x0045eae7`–`0x0045eaf0` | planet: `EDI = [esp+0x10] = max(s_A, s_B)` |
| **`0x0045eaf4`** | **join**, the patch site (§4.1) |
| `0x0045eaf4`–`0x0045eb8c` | manual-control and `0x10b` filter as above; `MakeDamage(dmgA, 1)` at `0x0045eb55` to A if A is class 7 with a script object; `MakeDamage(dmgB, 1)` at `0x0045eb87` to B |
| `0x0045eb8f`–`0x0045eba2` | asteroid (class `0x11`) with `[obj+0x44] & 0x80000000` on either side: no damage (`jne 0x0045ebc7`) |
| `0x0045ebaf`–`0x0045ebc2` | `0x00449a60(A, B, dmgA, −1)`; `0x00449a60(B, A, dmgB, −1)` |
| `0x0045ebc7`–`0x0045ec5c` | position restore: `[obj+0xc0..0xcc] → [phys+0x30..0x3c]`, `[obj+0xd0..0xd8] → [obj+0x18..0x20]` for both, so the bodies return to their previous frame's position |

The `"MakeDamage"` string `0x0055fd58` is pushed at exactly three sites: `0x0045dbf4` (planet), `0x0045eb49` and
`0x0045eb7e`. All three use the encoding `0x0049f4c0([M+0x60], 0, script object, "MakeDamage", 1, argc 2, dmg,
flag)` with `M = *0x0060850c`. The flag is 1 for a contact and 0 for the planet atmosphere.

### 1.3 Helpers and units **[m]** unless marked

* **`0x0044eb90` (EAX = obj): max hull.** It returns 0 when `[obj+0x40] & 0x40000000`, `*0x0057a6d4` for a
  missile without `0x1000000`, and otherwise the table `0x0044e910(subtype)`. That table is the one
  [`0x00450980`](../../tools/analysis/x3ap_function_labels.json) compares with the current hull `[obj+0xec]`.
* **`0x0044ecd0` (EDX = obj): current shield strength** (corrected 2026-11-05: for a shielded object it is
  Σ energy·capacity/0x3e80000, the current value; the maximum is `0x0044ee10`, Σ capacity,
  [weapon-effect-spawn.md](weapon-effect-spawn.md) §9). By class:
  * bullet: record `+0x44`;
  * missile: TMissiles `+0x58`;
  * shield ware: its energy;
  * planet or sun: `0x989680` (10,000,000);
  * asteroid and classes 28–31: 0;
  * class 20: 100 or 0 by subtype;
  * ship and station: the sum over the installed shields (loop at `0x0044edb0`).
* **Damage values** are in hull points, the unit of `[obj+0xec]` and of max hull. The shield stage of
  `0x00449a60` converts them to shield energy as `dmg·0x3e80000 / record+0x48` (TShields `*0x00606fdc`). So a
  shield's energy is a 16.16 fraction of its capacity in thousandths, and `+0x48` is its capacity in the same
  units as hull. **[i]** the reading of `+0x48`.
* **The relative speed `v`** is in the engine's integer velocity units (position units per second **[i]**). It
  is the larger axis of `|v_A − v_B|` before this contact's impulse, not the Euclidean speed.

### 1.4 The damage routine `0x00449a60` **[m]**

`stdcall (victim, source, shieldDmg, hullDmg)`, `ret 0x10`. Ten callers (§3).

1. **Shields**, only when the victim is a ship (class 7), class 1, class `0x12` or carries flag bit 20. For each
   installed shield with energy > 0 (`[[victim+0x50]+0x6c]` list), the routine converts and subtracts the
   remaining shield damage. If that shield absorbs all of it, it sets `absorbed = 1`.
2. **Hull.** The hull stage is skipped when `absorbed` is set, unless the source is a bullet with record flag
   `0x20`. A bullet with record flags `0xa0` subtracts `hullDmg` scaled to 96–100 % (negated for flag `0x80`,
   the repair laser). Otherwise:
   * `hullDmg < 0`, which is the `−1` that collisions pass, means the residual after the shields;
   * a positive `hullDmg` is scaled by the share that got through the shields;
   * when the result is > 0, the routine scales it to 96–100 % and subtracts it with `0x0044ec10`
     (ESI = obj, ECX = amount).
   `0x0044ec10` acts only on classes 5–16 and on class 20 subtypes 3, 4 and `0x1c`. It clamps the hull at 0 and,
   with Flags2 `0x800000`, at 93 % of the maximum. **Asteroids (class 17), planets and gates never lose hull
   here.**
3. **Cockpit hit indicator.** When the victim has the piloted flag `[victim+0x40] & 2`, the routine calls
   `0x0042c7d0(level, victim)`, where `level = clamp(total·100 / mass, 5, 100)` and `total` is capped at 50,000.
   For the cockpits viewing the victim, `0x0042c7d0` writes `cockpit+0x200 = now + 1000 ms` and
   `cockpit+0x204 = level`. **[i]** that this drives the shake or flash.
4. **Effects**, unless the victim has no scene node, carries `[node+0x12c] & 0x100000`, or carries the invincible
   flag `0x40000000`:
   * `0x00413c40(victim, source)`, the impact effect (role **[m]**, [weapon-effect-spawn.md](weapon-effect-spawn.md)
     §3; whether its point is the true contact is **[u]**), with no damage argument. It is
     skipped only for a bullet hitting the piloted ship while `0x0043fd00` is false and the victim's cockpit has
     `+0x150 & 1` (`0x00449d58`–`0x00449d81`);
   * when `0x00450310(victim)` holds (the victim is in the player's sector), `0x00417250(ECX = victim,
     EAX = source)`. That routine is the **impact sound**: it calls `0x0049b1f0` with the bullet's record
     `+0x4c`, with id `0x3e0` for TShips subtype `0x113`, or with ids `0x388`/`0x387` by radius (large or small)
     when the victim is not smaller than the source **[i]** that `0x0049b1f0` plays a positional sound.

**With both damage arguments 0** the routine does the following:

* Every shield with energy > 0 and capacity > 0 subtracts 0 and sets `absorbed`, so the hull stage is skipped.
* A non-shielded victim reaches the hull stage with 0, and `0x0044ec10` is not called (`jle` at `0x00449c90`).
* The indicator fires at level 5, and the effect and the sound run as before.

So the call is a no-op on hull and shields; the only exception is a shield record with capacity 0, whose energy it
would set to 0 (§6). A 0-damage call is also reachable in vanilla: a ship pair with `c = 0` gives `dmg = 0`.

### 1.5 The planet path in the sector pass **[m]**

These instructions are in `0x0045d250`, not in the response routine.

* **Contact radius.** `0x0045d913`–`0x0045d98d` picks the planet P (`[esp+0x24]`) and the other body O (EDI). The
  radius is `R = P.radius`, or the model-table override at `*0x00608518 + 0xbc`, plus `O.radius/10`.
* **Distance.** `0x0045d9c1`–`0x0045da17` measures it with x87 and float32.
* `dist ≤ R`: both bodies get flag 2, and unless `[O+0x44] & 0x40000`, the response `0x0045e130(P, O)` runs at
  `0x0045da3c`. That is the contact block with the planet rule, which kills.
* `R < dist < R + 750000`, O not a bullet:
  * if O has the piloted flag, `NotifyPlanetCollision` (`0x0045daa7`, static class 150, no arguments);
  * then the random gate: `rand·200/0x8000 < step` (`cfg+0x714`);
  * `ESI = maxHull(O)·((R+750000−dist)/5000)² / 22500 · max(step,200) / 1000` (`0x0045db10`–`0x0045dbad`);
  * **`0x0045dbaf test esi,esi / jle 0x0045df90`**;
  * for a manual-controlled class-7 O with a script object (not `0x10b`), `MakeDamage(ESI, 0)` (`0x0045dc05`);
  * `0x00449a60(O, P, ESI, −1)` (`0x0045dc43`);
  * if `[O+0xec] == 0` and O is not already dying, `KilledBy(P id, 3, …)` or the native kill
    `0x0044abd0(3, P)`.

  Since P is larger than O, `0x00417250` plays no sound on this path.

### 1.6 Player versus AI **[m]**

There is one code path. The engine distinguishes only in these places:

* `0x0043ff80` (manual control `[+0x40] & 0x40000` without `0x80000`) gates the contact `AttackedBy`, the contact
  `MakeDamage` and the planet `MakeDamage`;
* the piloted flag `[+0x40] & 2` gates `NotifyPlanetCollision`, the cockpit hit indicator and the force feedback.

The damage law and its application are identical for the player ship and AI ships.

## 2. FL: the same sites, and what differs (Q2) **[m]**

| Role | AP | FL |
| --- | --- | --- |
| response routine | `0x0045e130`, 3445 B, `stdcall (A,B)`, `ret 8` | `0x00467f10`, 3741 B (SHA-256 `2fa0c2a6…46df`), `stdcall (A,B)`, `ret 8` |
| response callers | 5 (above) | 3: `0x00466eac` (swept), `0x00467377` (narrow accept), `0x00467d31` (planet inside, from `0x0046774d`) |
| contact class gate / block | `0x0045e6b0` / `[0x0045e6de,0x0045ec5f)` | `0x0046859e` / `[0x004685cc,0x00468b79)` |
| registers in the block | A = EBX, B = ESI | A = ESI (`[ebp+8]`, `0x00468978`), B = EDI (`[ebp+0xc]`, `0x004688b9`) |
| strengths | `0x0044eb90` (EAX), `0x0044ecd0` (EDX) | `0x00458410` (ECX), `0x004585a0` (ECX) |
| Chebyshev velocity | `0x00419190` (ESI, EDI) | `0x00419f40` (ECX, EDX) |
| law constants | `0x186a0`, `/50`, 100 | identical (`0x004689d3`, `0x004689ea`, `0x004689e3`), with `cmov` instead of branches |
| `c`, `v` | `[esp+0xc]`, `[esp+0x1c]` | `[esp+0x38]` = `[esp+0x50]`, `[esp+0x5c]` |
| dmgA / dmgB at the join | `[esp+0x10]` / EDI | EDX = `[esp+0x10]` / ECX (stored to `[esp+0x20]` at `0x00468a60`) |
| **join (patch site)** | **`0x0045eaf4`**, `8b c3` `e8 85 14 fe ff` | **`0x00468a58`**, `8b 46 40` `25 00 00 0c 00` |
| manual-control test | call `0x0043ff80` | inlined `([obj+0x40] & 0xc0000) == 0x40000` |
| subtype `0x10b` test | `0x004191c0` (EAX) | `0x0041a0b0` (ECX; leaves EDX alone) |
| `MakeDamage` contact | `0x0045eb55` (A, ECX = dmgA), `0x0045eb87` (B, EDI) | `0x00468ab8` (A, `push edx`), `0x00468aeb` (B, `push [esp+0x20]`) |
| asteroid skip | `[obj+0x44] & 0x80000000` | `[obj+0x44] < 0` (the same bit) |
| contact damage calls | `0x0045ebb8`, `0x0045ebc2` | `0x00468b16`, `0x00468b24` |
| damage routine | `0x00449a60(victim, source, shield, hull)`, `ret 0x10` | `0x00453070`: ECX = victim, stack `(source, shield, hull)`, `ret 0xc`; the same stages, constants `0x3e80000` and `0xc350`, indicator `0x0042f7f0`, sound `0x00417a40` |
| hull subtractor | `0x0044ec10` (2 callers) | `0x004584b0` (2 callers: `0x0045325f`, `0x004532a4`) |
| by-name call, globals | `0x0049f4c0`, `M = *0x0060850c`, `cfg = *0x00606f34`, step `+0x714` | `0x004bb500`, `M = *0x005a82ec`, `cfg = *0x005a83b0`, step `+0x704` |
| planet gate | `0x0045dbaf` `85 f6 0f 8e d9 03 00 00`, dmg in ESI | `0x004678bb` `85 c9 0f 8e bb 05 00 00`, dmg in ECX, also stored to `[esp+0x24]` at `0x004678b7` |
| planet `MakeDamage` / damage / kill | `0x0045dc05` / `0x0045dc43` / `0x0045dc93` | `0x0046791d` / `0x0046797d` / `0x004679c5` |
| `"MakeDamage"` pushes | `0x0045dbf4`, `0x0045eb49`, `0x0045eb7e` | `0x0046790d`, `0x00468aa8`, `0x00468adb` |

FL differs from AP in these points:

* **Damage routine ABI.** `0x00453070` takes the victim in ECX and three stack arguments, where AP's routine takes
  four stack arguments.
* **Helper ABIs.** Several helpers changed their register arguments. The manual-control test is inlined.
* **Dispatch.** The response has three callers instead of five; FL dispatches projectile pairs differently.
* **Planet path.** In FL, the planet path keeps the damage in a stack slot as well as in ECX. On the path through
  the post-callback re-check (`0x00467970`), the slot is reloaded into ECX before the damage call. A planet patch
  must therefore leave the stack slot dead, which the one-byte variant does (§4.2).
* **KC handler.** FL's story object has the full equipment-damage `MakeDamage` (524 instructions; it adds
  `Signal_EquipmentDamaged` and a predefined-ware test).

## 3. Weapons share the damage routine; the discriminating sites (Q3) **[m]** unless marked

The rel32 census of each damage routine finds ten callers per edition:

| Caller AP | Caller FL | Role |
| --- | --- | --- |
| `0x0045e383`, `0x0045e569` | `0x00468185`, `0x004683eb` | bullet hit (record `+0x44` shield, `+0x80` hull; continuous weapons scaled by the frame step) |
| `0x0045e433`, `0x0045e621` | `0x004682b1`, `0x00468517` | armed-missile hit (95–100 % of the missile's damage `0x0044ecd0`, hull `−1`) |
| `0x0045ebb8`, `0x0045ebc2` | `0x00468b16`, `0x00468b24` | **body contact** |
| `0x0045dc43` | `0x0046797d` | **planet atmosphere** |
| `0x0045f054` | `0x00468f41` | area damage in `0x0045eeb0`, called from the kill path `0x0044ad18`/`0x0044ad6c`; explosion blast **[i]** |
| `0x0044fab1` | `0x004593ed` (unclassified) | self-damage (victim = source), shield only (`hullDmg = 0`), from `flight+0x91c` **[i]** a drain |
| `0x00463518` | `0x0046d82b` (unclassified) | in the `SA_` native range, arguments from the KC operand stream **[i]** a script damage command |

So the callee cannot be patched. The discriminating sites are the two collision callers:

* the contact block, which only bodies that are neither bullets nor armed missiles reach;
* the planet atmosphere gate in the sector pass.

**The KC `MakeDamage` is shared too.** All three story objects call `2004::MakeDamage(rand(maxLaser)+10, 0)` from
`2004::AttackedBy` when the projectile argument is nonzero, i.e. on weapon hits. A contact `AttackedBy` passes
projectile 0 and does not reach it. Patching the KC method would also remove weapon equipment damage. The native
`"MakeDamage"` pushes, by contrast, are collision-only (three each, all listed in §2).

No instruction in the AP collision code `[0x0045c780, 0x0045eea5)` or the FL collision code
`[0x00466660, 0x00468dad)` writes `[reg+0xec]` (decoded scan). The hull changes only through the damage routine.

## 4. The patch (Q4)

### 4.1 Contact: zero both damage values at the join

**AP `0x0045eaf4`, 7 bytes `8b c3 e8 85 14 fe ff`.** The two whole instructions are `mov eax,ebx` and
`call 0x0043ff80`; the next one is `0x0045eafb test al,al`.

* **Entries.** `jmp 0x0045eadd`, `jl 0x0045eae1`, `jmp 0x0045eae5` and the fall-through from `0x0045eaf0`, all
  onto the first byte. There are 0 raw rel8/rel32/abs32 hits into `0x0045eaf5..0x0045eafa`.
* **State at the site.**
  * EBX = A, ESI = B;
  * `[esp+0x10]` = dmgA, EDI = dmgB;
  * `[esp+0xc]` = c, `[esp+0x1c]` = v.
* **Liveness.**
  * EAX is defined by the displaced `mov`.
  * ECX and EDX are written before they are read on every path to `0x0045ec5f`: `0x0045eb38`/`0x0045eb3c`,
    `0x0045eb6e`/`0x0045eb74`, `0x0045eb98`, `0x0045ebce`/`0x0045ebd9`.
  * The flags are dead: the call and then `test al,al` follow.
  * EDI is next written at `0x0045ed31` and restored by the routine's `pop edi`.
  * `0x0043ff80` is a leaf that reads only EAX.

The patch writes `jmp rel32` to the stub and `nop nop` over the last two bytes. The stub is:

```
    ; optional diagnostics: count; record [esp+0x10], edi, [ebx+0x48], [esi+0x48], [esp+0xc], [esp+0x1c]
    mov  dword [esp+0x10], 0       ; dmgA
    xor  edi, edi                  ; dmgB
    mov  eax, ebx                  ; displaced
    call 0x0043ff80                ; displaced (leaf)
    jmp  0x0045eafb
```

Only EAX, ECX and EDX may be used as scratch. ESP is unchanged, because the stub is entered by `jmp`, so
`[esp+0x10]` is the same slot.

**FL `0x00468a58`, 8 bytes `8b 46 40 25 00 00 0c 00`.** The two whole instructions are `mov eax,[esi+0x40]` and
`and eax,0xc0000`; the next one is `0x00468a60 mov [esp+0x20],ecx`.

* **Entries.** `jmp 0x00468a3b`, `jmp 0x00468a47` and the fall-through from `0x00468a54`. There are 0 raw hits
  into the interior and no `.reloc` entry in the span (the immediate is not an address).
* **State at the site.**
  * ESI = A, EDI = B;
  * EDX = `[esp+0x10]` = dmgA, ECX = dmgB;
  * `[esp+0x38]` = c, `[esp+0x5c]` = v.
* **Liveness.**
  * EAX is defined by the displaced `mov`.
  * ECX is stored at `0x00468a60` and EDX is pushed at `0x00468aa3`. `0x0041a0b0` in between writes only EAX.
  * The flags written by the displaced `and` are dead: `cmp eax,0x40000` at `0x00468a64` redefines them.

The patch writes `jmp rel32` plus 3 `nop`. The stub is:

```
    ; optional diagnostics as above, with esi/edi as A/B and edx/ecx as the values
    xor  ecx, ecx                  ; dmgB
    xor  edx, edx                  ; dmgA
    mov  dword [esp+0x10], 0       ; dmgA slot
    mov  eax, [esi+0x40]           ; displaced
    and  eax, 0xc0000              ; displaced, defines the flags as vanilla
    jmp  module_base + 0x68a60
```

FL is relocatable: the site and the return address are module base + RVA (`0x68a58`, `0x68a60`), and the stub
contains no preferred-base address. The jump is direct, so CFG does not apply.

**What survives the contact patch.** **[m]** unless marked

* **Physics unchanged:** the impulse, the position restore, the force feedback and the contact `AttackedBy`.
* **KC call, no effect:** `MakeDamage(0, 1)` still reaches the script. In all three objects the handler returns
  before any write for damage ≤ 0: the decoded guard `ldloc 9; push0; le; jt` leads to `push1; jf; dropn; push0;
  ret`, after only the getters `IsInvincible`, `GetShield`, `GetMaxShield`, `GetHull` and `GetMaxHull`.
* **Damage routine called with 0:** the impact sound and the hit effect stay, and hull and shields are unchanged.
  The cockpit indicator fires at level 5 instead of the vanilla level.
* **Kill check unchanged:** it still runs. With the hull unchanged it fires only for a body whose `[+0xec]` was
  already 0, or which carries `0x20000`, which is the vanilla condition for such a body.
* **Planet inside the contact radius:** the ship takes 0 instead of ≥ 10,000,000 and is put back to its previous
  position every frame. **[i]** that it cannot tunnel.

**Rollback:** write the 7 or 8 original bytes back after comparing the patched bytes.

**Cost:** one stub per contact pair per frame. Accepted pairs are 0–200 per frame ([sector-collide.md](sector-collide.md) §11.5).

**Reentrancy:** the stub runs on the main thread inside the collision pass. It calls nothing but the replayed
leaf, keeps state only in counters, and leaves the x87 state as found.

### 4.2 Planet atmosphere: take vanilla's zero-damage branch

| Edition | VA | Before | After |
| --- | --- | --- | --- |
| AP | `0x0045dbaf` | `85 f6` `test esi,esi` | `31 f6` `xor esi,esi` |
| FL | `0x004678bb` | `85 c9` `test ecx,ecx` | `31 c9` `xor ecx,ecx` |

`xor` sets ZF = 1, SF = 0 and OF = 0, so the following `jle` (unchanged) is taken to the loop-continue label: AP
`0x0045df90`, FL `0x00467e7e`. This is exactly vanilla's path when the computed damage is 0.

* **AP:** ESI = 0 lies in the value set vanilla brings to that edge (≤ 0).
* **FL:** the slot `[esp+0x24]` keeps the computed value on this edge, which is the role vanilla's ≤ 0 value plays.
  It is dead: the next use, on every path, is the write `mov [esp+0x24],eax` at `0x00466fdb` in the
  inner-loop head `0x00466fd1`. That instruction is also the slot's first reference in the routine's text.
* **Effects:** `NotifyPlanetCollision` has already run. The skipped calls are `MakeDamage`, the damage routine (no
  sound on this path, §1.5) and the kill check.
* **Instruction boundaries:** a one-byte opcode change of the same length, with the operand byte unchanged, so
  they hold.
* **Rollback:** write `85` back.

A variant that keeps the calls with 0, by also turning the `jle` into a `nop`, is **not** equivalent. Bodies with
`maxHull = 0` (the invincible flag) would then reach the kill check, which vanilla never sends there.

### 4.3 Validation the installer needs

`verify_collision_damage.py` holds the windows (§2 table and the `contact_gate`, `contact_law`,
`contact_damage_calls`, `planet_damage_call` windows). An installer compares:

* the patch span itself, plus 2 bytes after it (AP `8b c3 e8 85 14 fe ff 84 c0`, FL
  `8b 46 40 25 00 00 0c 00 89 4c 24 20`);
* the two damage-call windows (AP `0x0045ebaf`, 24 B; FL `0x00468b0d`, 28 B), which pin the slot and register
  assignments;
* the class-gate window, which pins that the site is contact-only.

For FL, compare at module base + RVA. None of the six FL windows contains a base-relocation entry (measured, the
validator checks it), so the preferred-base bytes compare unchanged at any load address.

## 5. One flight row

One consolidated diagnostic build, with the contact stub's counters on:

* **Every Present with events:** `collision_damage_suppressed events=<n> max_a=<dmgA> max_b=<dmgB>
  last=<classA>/<classB> c=<c> v=<v> player=<0|1>`.
* **Planet.** The planet site needs the same shape: a 6-byte detour over the `jle` that records ESI/ECX when > 0
  and jumps to the continue label. Its row is `planet_damage_suppressed events=<n> max=<dmg>`.

To confirm, the pilot rams a station and an asteroid at speed, bumps an AI ship and skims a planet. The checks:

* the HUD hull and shield bars do not move;
* the impact sound is heard;
* `CollisionWarn` still sounds;
* `events > 0` with nonzero damage values;
* the class pairs 7/5, 7/6, 7/17 and 7/7 appear.

## 6. Not established statically

* **Value scale.** Typical damage values, and the unit of `v` (needs the flight row).
* **Callee roles.**
  * whether `0x0042c7d0`'s level drives a shake whose strength the player would notice at 5 instead of the vanilla
    value;
  * the exact role of `0x0049b1f0` (2026-11-05: positional sound player, still **[i]** from the decompiler;
    `0x00413c40` is the impact effect **[m]**, [weapon-effect-spawn.md](weapon-effect-spawn.md) §3);
  * the roles of the FL callers `0x004593ed` and `0x0046d82b`.
* **Shields with capacity 0.** A shield record with capacity 0 and energy > 0 would be set to 0 by a 0-damage call
  (AP `0x00449b1e`, FL `0x0045311f`). It is implausible but not excluded.
* **Other scripts.** Whether any MD or other script class applies damage on `CollisionWarn` or
  `NotifyPlanetCollision`. The decoded handlers `2004::CollisionWarn` (70 instructions) and
  `150::NotifyPlanetCollision` (42 AP, 47 FL) call nothing damage-like; other classes' handlers were not walked.
* **Asteroid and gate `[+0xec]` values.** They decide the kill check on contact, which is unchanged by the patch.
* **Tunnelling.** That a ship held at a planet's contact radius every frame does not tunnel or jitter in a visible
  way.

## Reproduce

```sh
python3 -I verification/results/collision-damage/verify_collision_damage.py     # PASS, writes the JSON
i686-w64-mingw32-objdump -d -M intel --start-address=0x45e130 --stop-address=0x45eea5 /mnt/c/X3/X3AP.exe     # local only
i686-w64-mingw32-objdump -d -M intel --start-address=0x467f10 --stop-address=0x468dad /mnt/c/X3FL/X3FL.exe   # local only
```

## Implementation (2026-11-05)

Setting `collision_damage` under `[engine]` (`X3M_COLLISION_DAMAGE`, bool, builtin `1`, no launcher flag): `1` or unset
patches nothing; `0` applies both sites of §4 on either edition; any other value logs `reason=invalid_value` and patches
nothing.

| File | Role |
| --- | --- |
| `src/proxy/collision_damage_core.h` | both editions' sites, the 13 windows of the verifier (AP 7, FL 6), the EXE and response-routine hashes as provenance constants, the window comparator, the contact patch bytes, the two-site install and restore over an Ops policy (`src/engine/fl/inline_patch_fl.h` per span), the row cadence |
| `src/proxy/collision_damage_stubs.h` | the AP and the FL contact stub (both assembled into both DLLs), their record and the bound return/leaf words |
| `src/proxy/collision_damage.cpp` | one module for both editions: AP binds `core::ap` at `0x400000` behind `object_trace::executable_verified()`, FL `core::fl` at `fl::module_base()` |
| `verification/probe/collision_damage_fixture.cpp` | host and native fixture; `verification/analysis/test_collision_damage.py` runs it and compares the tables with `verify_collision_damage.py` |

**Install** (backend-load path, `capture.cpp` initialize, shared by both editions): identity, the engine_patch install
window (else `late_claim`), every window of the edition compared whole at base + RVA (else `window_mismatch` with the
window's name and its first 8 expected/got bytes), the DLL pinned, the stub's return bound (AP `0x0045eafb` and the leaf
`0x0043ff80`; FL base + `0x68a60`), then the contact span (`jmp rel32` + NOPs) and the planet byte (`85→31`), each
protect / write / flush / read-back / re-protect. A planet failure takes the contact span back; a rollback that cannot be
confirmed by a read-back keeps the site registered (`*_rollback_failed`) for `shutdown()`. Write paths: AP contact
plain (the 7 bytes cross the qword at `0x0045eaf8`), FL contact one `lock cmpxchg8b` (`0x68a58` is 8-aligned), both
planet bytes atomic. `shutdown()` (AP `FreeLibrary` only, unreachable while pinned) restores planet then contact, each
only over this feature's bytes.

**Stubs.** As §4.1, with three refinements: (1) the record for the flight row runs first, between `pushfd`/`push eax,
ecx, edx` and their pops, so every register and EFLAGS leave it unchanged; (2) the damage values are zeroed with `mov`
(AP `mov [esp+0x10],0; mov edi,0`; FL `mov ecx,0; mov edx,0; mov [esp+0x10],0`), not `xor`, so the incoming flags reach
the displaced instructions unchanged; (3) AP's displaced `call 0x0043ff80` is replayed as `call [bound leaf]` and the
return as `jmp [bound return]`; only the dead return-address word below ESP differs from vanilla. No call into C, no
CRT, no x87, no SSE; both stubs are `check_no_x87.py` roots.

**Attach row** (one per process): `collision_damage edition=<ap|fl> status=<applied|skipped> reason=<ok|vanilla|
invalid_value|executable_mismatch|late_claim|window_mismatch|pin_failed|...> setting=<0|1|-|?> base= contact_site=
contact_len= contact_write=<plain|atomic|none> planet_site= planet_byte=85->31 planet_write= stub= resume= windows=
window= expected= got=`.

**Flight row** (§5, contacts only): `collision_damage_suppressed edition= frame= events=<since last row> total=
last_a= last_b= max_a= max_b= last=<classA>/<classB> c= v= player=`, at most one per second (wall clock) and only after
new contacts; `max_*` reset per row. `c` and `v` are the law's stack slots and are stale on a planet contact (the
planet rule does not write them). `player` is `([A+0x40] | [B+0x40]) & 2`; on FL that offset and bit are **[i]** (the
FL contact block reads its manual-control flags at `+0x40`). The planet-atmosphere site has no row: the one-byte patch
runs no code of ours, and §5's 6-byte counting detour was not built.

**Verified 2026-11-05** (no game, no Wine): both DLLs build with 0 warnings; `check_no_x87.py` PASS for both
(the two stubs among the roots); the fixture natively (WSL interop, i686 MinGW with the DLL's flags) runs 225 checks,
0 failures, on images built from the verifier's bytes: per edition the comparator accepts its windows (AP 7, FL 6) and refuses a
one-bit change of each of their 133 (AP) / 141 (FL) bytes naming that window; a fault at each of the 8 Ops calls rolls
back to the original bytes, and a persistent fault keeps the site registered (7 of 8 cases) until a clean restore puts
the original bytes back; on real `VirtualProtect` pages the engine path executed from the site with five fabricated
register/EFLAGS/stack/object images reads both damage values 0 after the stub (FL also the `[esp+0x20]` store), every
other register, EFLAGS (DF included, and through a flag-preserving leaf) and stack slot equal to the vanilla run, the
leaf sees EAX = EBX, the record holds the values, classes, `c`, `v` and the piloted bit; the planet gate with damage
> 0 takes vanilla's zero-damage branch with the state vanilla leaves at damage 0 (AF excluded, undefined after
`test`/`xor`); after restore the bytes, protection and behaviour are vanilla again. A stub mutated to zero with `xor`
fails 4 checks (AP, flag-preserving leaf). The native run is the user's flight (§5).
