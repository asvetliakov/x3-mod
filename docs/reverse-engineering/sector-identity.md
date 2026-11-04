# Sector identity: the player's sector as universe map x/y, read without hooks

2026-11-05. Static study of `X3AP.exe` and `X3FL.exe` plus the compiled KC story objects, for the per-sector star
seed of [sector-background-modern.md](../architecture/sector-background-modern.md). Nothing was run, hooked,
patched or installed. **[m]** = measured from code or data in this pass, **[i]** = inferred.

**Outcome.** The universe map coordinates are **not** fields of the native sector object. They live in the sector's
KC object (class 2001, members `m0` = x and `m1` = y, written only by `2001::Create`). The native sector object
links to that KC object through its event-object field **`+0x94`** (`SA_SetEventObject`, called by
`2001::Activate`). The KC object is found by id in the script VM's object hash **`[VM+0x12d0]`**. The chain is
the same on both editions, with the same offsets. Only the two image globals and the KC member that holds the
native id differ. The read needs no hook: 7 bounded reads once per sector change, then one header span per
frame. x/y is unique per map (0 duplicate coordinates in all three maps checked) and every engine name of a
sector is derived from it. The engine's music id, the background index, the native id `+8` and the KC id all
fail as a stable key (section 4).

| Input | Bytes / build | SHA-256 |
| --- | --- | --- |
| `/mnt/c/X3/X3AP.exe` | 2,153,984 | `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` |
| `/mnt/c/X3FL/X3FL.exe` | 2,804,736 | `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8` |
| AP+Mayhem `addon/07.cat:L/x3story.obj` (loaded on this install) | build 1462141839 | via `kc_obj` |
| stock AP `addon/04.cat:L/x3story.obj` | build 1510658513 | via `kc_obj` |
| FL `addon2/03.cat:addon2/L/x3story.obj` | build 1636047691 | via `kc_obj` |

Reproduce (PASS, exit 0; 13 AP + 11 FL byte windows, 3 KC builds, 3 maps):

```sh
python3 -I verification/results/sector-identity/verify_sector_identity.py > verification/results/sector-identity/summary.json
```

[`verify_sector_identity.py`](../../verification/results/sector-identity/verify_sector_identity.py) /
[`summary.json`](../../verification/results/sector-identity/summary.json). Disassembly came from MinGW `objdump` over
both EXEs and Ghidra 12.1.4 headless (`X3DecompileFunctions.java`, read-only `~/x3-ghidra-research/X3Render`) for
`0x004421a0`/`0x0042fe10` (AP restore). The listings stay local. FL addresses are preferred VAs (base
`0x00400000`, relocatable): subtract `0x400000` for the RVA.

## 1. AP: where x and y are (Q1)

| Step | AP evidence **[m]** |
| --- | --- |
| x, y are KC members | `2001::GetX` = `frame 1; ldmem 0; ret`, `2001::GetY` = `ldmem 1`. `2001::Create(x, y)` opens with `ldloc 4; stmem 0; drop; ldloc 5; stmem 1; drop`. Class 2001 has no parent class, so the member numbers are absolute. A census of every `stmem` in every class-2001 method finds exactly two writes of `m0`/`m1`, both in `Create`. KC `stmem` only writes members of `this`, so nothing else can change them. Same in Mayhem and stock. |
| `200::EnvironmentToMusicTrack` uses them | `GetX`, `GetY` → `(x·103 + y·37 + 5) mod 22 + 19` (the music note's hash). |
| Native sector ↔ KC object | `2001::Activate`: `push0 push1 push2 native SA_AllocObject; stmem 34; this; ldmem 34; push2; native SA_SetEventObject`. SA command 3 (`SA_SetEventObject`, name table `0x0057a7f0` + 3·4, dispatcher table `0x004690f0`) at `0x0046078b`: looks up the native object by id (`0x0043a4f0`), and for an int argument (tag 1) does `mov ecx,[esi+6]; mov [eax+0x94],ecx` at `0x004607a7`. `SA_GetEventObject` (command 4, `0x004607dc`) returns `[obj+0x94]`. `SA_AllocObject` returns `[obj+8]` (`0x00460825`). So `m34` = `[S+8]` and `[S+0x94]` = the KC object id. |
| `+0x94` lifetime | Object init `0x0043ffa0` zeroes it (`0x00440055`). Free/restore init `0x004421a0` zeroes it (`0x00442201`). Save writes it (`0x0042fe10`, `FUN_004e94a0([obj+0x94])`). Load restores it from the stream at **`0x0043013b`** (inside `0x0042fe10`, called by the per-object restore `0x004421a0`). `2001::Deactivate` reads `m34` into `SA_FreeObject`. |
| `+0x94` is used as a KC object | e.g. `0x004231d4 mov eax,[ecx+0x94]` → `0x0049f570(…, obj = eax, "GetScannerRange")`: the engine calls KC methods on it through the id lookup below. |
| VM global | `VM = *0x006085e4`. Written only by VM create `0x004ab010` at startup (`0x004031ef`, `0x00403352`) and cleared at shutdown (`0x00401f6f`, `0x00402ef1`). The VM init `0x0049c9a0` is the single writer of `[VM+0x12d0]` (`0x0049cb59`). |
| KC id → object | `0x0049f1e0` (EAX = id, ESI = VM): for a negative id, `key = ~id`, `map = [VM+0x12d0]`, `buckets = [map]`, `n = [map+4]`, `node = buckets[key & (n-1)]`, follow `{+0 next, +4 key, +8 object}` until `key` matches. Non-negative ids are classes, found by binary search over `[VM+0x20]` (count `[VM+0x1c]`, stride `0x38`, `+0` = class number; `0x004b06f0`). |
| KC object layout | New object (op `2b`, `0x004a2d50`): 0x10 bytes, `+8` = class record, `+0xc` = member cells. The id comes from `0x004efcc0` on `[VM+0x12d0]` and `+0` = `~key` (`0x004a2e17..0x004a2e2b`). A cell is 5 bytes: tag byte, then a dword (`ldmem` `0x004a283c`: `cell = [obj+0xc] + 5·n`). Tag 1 = int (`this` `0x004a3052` pushes tag 1 with `[obj+0]`). Saves keep the keys: the load path re-inserts with the saved key (`0x004efbf0` at `0x004a0cbe`). |

**Offsets: x = dword `[[obj+0xc] + 1]`, y = dword `[[obj+0xc] + 6]`, tags at `+0`/`+5` = 1.** The chain from the sector is
`S → [S+0x94] (KC id, negative) → [VM+0x12d0] hash → obj → [obj+0xc]`. **Range [m]:** AP+Mayhem map (loose
`addon/maps/x3_universe.xml`, 247 sectors) x 0..19, y 0..18. Stock AP (`addon/02.cat`, 239 sectors) x 0..23,
y 0..19. No duplicate coordinates. The type is a KC int (signed 32-bit). Accept 0..255 per axis: an
out-of-range or non-int cell means a wrong object.

## 2. FL equivalents (Q2)

| Item | AP | FL | Same? |
| --- | --- | --- | --- |
| Player root M (image global) | `0x0060850c` | **`0x005a82ec`** (RVA `0x1a82ec`; alert `0x0042f913`) | differs |
| Player ship | `[M+0x38]` | `[M+0x38]` (`0x0042f91b`) | same |
| Parent | `[P+0x54]` | `[P+0x54]` (`0x0042f92e`) | same |
| Class word, sector = 1 | `i16 [S+0x48]` | `i16 [S+0x48]` (`0x0042f939`) | same |
| Native id | `[S+8]` | `[S+8]` (`SA_AllocObject` returns `[eax+8]`, `0x0046a709`/`0x0046a7ac`) | same |
| Alive marker | `u16 [S+0x9c]`: `0xcafe` live (`0x00440061`, `0x0044220d`), `0xefac` freed (`0x0043fc23`) | same (`0x00449a20`, `0x0044b757`; `0xefac` `0x0044961e`) | same |
| Event object (KC id) | `[S+0x94]` | `[S+0x94]` (`SA_SetEventObject` `0x0046a7c1`, store `0x0046a7e4`; restore `0x00434665` in `0x00433410`, called from restore `0x0044b6b0`) | same |
| Sector allocation | 0x180 bytes, ctor `0x004524d0` | 0x180 bytes, ctor `0x0045c110` (`0x0046a71e`) | same size |
| VM global | `0x006085e4` | **`0x005aa174`** (RVA `0x1aa174`; written `0x00477561`/`0x00477681`, cleared `0x0047648a`/`0x004772c6`) | differs |
| KC object hash | `[VM+0x12d0]` | `[VM+0x12d0]` (lookup `0x004bb1b0`, thiscall ECX = VM; single writer `0x004b8bce`) | same |
| KC object / cell / class record | `+0` id, `+8` class, `+0xc` cells; 5-byte cells; class stride `0x38`, `+0` number | same (op `2b` `0x004bf706`, id store `0x004bf7cc`; `ldmem` `0x004bef32`; `this` `0x004bf9eb`; class search `0x004cd180`) | same |
| x / y members | `m0` / `m1` | `m0` / `m1` (`GetX` `0x105b81`, `GetY` `0x105b8a`, `Create` `0x1036b8`) | same |
| Native-id member (cross-check) | `m34` (Mayhem and stock) | **`m36`** (`Activate` `0x1075d9`) | differs |
| Music member | `m32` | `m33` | differs |

No code window is byte-identical: the FL compiler's register allocation and calling conventions differ (for
example the FL lookup is thiscall `ret 4`). The validator therefore carries per-edition windows. Three FL
windows contain HIGHLOW relocations, which a runtime comparator must rebase from the module base, as
[fl/music-alert.md](fl/music-alert.md) does: `player_root` +1, `sa_set_event_object` +5/+43/+75 and
`sa_get_event_object` +5/+33. **FL map [m]** (`addon2/04.cat:addon2/maps/x3_universe.pck`, 298 sectors): x 0..23,
y 0..19, no duplicates. The FL rows are static facts. Like every FL note
([fl/README.md](fl/README.md)), nothing here has been confirmed in the running game.

### Byte windows a validator can compare

Hex at the preferred VA. The verifier pins exactly these windows.

| Window | AP VA: bytes | FL VA: bytes |
| --- | --- | --- |
| `player_root` | `0x42c890`: `a10c85600083ec08535556578b7838` | `0x42f913`: `a1ec825a005355578b5838` |
| `parent_is_sector` | `0x42c8a9`: `8b475485c00f84b10000006683784801` | `0x42f92e`: `8b435485c00f849601000066837848010f858b010000` |
| `sa_set_event_object` (store) | `0x4607a7`: `8b4e06898894000000` (in the 67-byte pin from `0x46078b`) | `0x46a7df`: `8b4e066a00898a94000000` (in the 92-byte pin from `0x46a7c1`) |
| `sa_get_event_object` | `0x4607dc`: `8b45188b5001a10c856000e8049dfdff85c00f8411ffffff8b809400000050e9f2250000` | `0x46a81d`: 49 bytes, see verifier |
| `kc_object_lookup` | `0x49f1e0`: 70 bytes `85c07d3283c9ff2bc88b86d0120000…c3` | `0x4bb1b0`: 75 bytes `8b54240485d279318b81d0120000f7d2…c20400` |
| `kc_ldmem` | `0x4a283c`: `0fbf078b4c24208d048003410c83eb058bf3895c2414e8995c000083c702` | `0x4bef32`: `0fbf0683ef058b4de0897de8897de48d048003410c8bcf50e821650000` |
| `kc_this` | `0x4a3052`: `8b4c24208b0183eb05c60301894301` | `0x4bf9eb`: `8b45e083ef058b00c60701894701e9d3f3ffff` |
| `kc_new_object_id` | `0x4a2e0f`: `8b5424188b74242c8b82d012000056e89dce040083c9ff2bc883c404890e` | `0x4bf7b2`: `8b45ec8bf18b7de88bd68b88d0120000e819d8040083c9ff2bc8890e` |
| `restore_event_object` | `0x430130`: `898780000000e8e5920b00898794000000` | `0x434658`: `8bcb6a010bd08d4424146a0450899794000000` |

The class binary search (`0x4b06f0` / `0x4cd180`, 65 bytes each), the AP `+0x94` init (`0x44004f`) and AP VM
create (`0x4031e8`) are pinned in the verifier as well.

## 3. Read sequence from Present (Q3)

The music core's model applies: image globals are plain loads (both in `.data`, mapped for the process). Every
engine dereference is one validated `span()` (`engine_memory::read`). Any refusal ends the sample with a named
status and no write. All writers of these fields run on the main-loop thread, which is also the thread the proxy's
Present runs on ([sector-fog.md](sector-fog.md) §11.3, [script-task-scheduler.md](script-task-scheduler.md) §4).
So nothing is torn within a frame. Between frames anything can change.

1. `M = image32(root)`. If it is 0: `no_game`.
2. `P = [M+0x38]`. If it is 0: `no_player`.
3. `S = [P+0x54]`. When `i16 [S+0x48] != 1`, follow `+0x54` at most 3 more hops, the
   `sector_background::walk_anchor` rule (docked: ship → station → sector). No sector within the hops:
   `not_in_sector`. Alternative anchor: the cockpit sector `[cockpit+0x54]` that `sector_background.h` already
   reads every frame on both editions. That is the sector whose background is being drawn, so it is the better
   seed source when the two disagree (for example a remote view) **[i]**.
4. One header span `[S+0x08, S+0x9e)`: `id = [S+8]` (nonzero), `i16 [S+0x48] == 1`, `kc = (i32)[S+0x94] < 0`,
   `u16 [S+0x9c] == 0xcafe`. Otherwise `sector_header`.
5. Cache key `(S, id, kc)`. If it equals the cached key and the load epoch is unchanged, return the cached
   (x, y). This is the per-frame cost: one span.
6. `VM = image32(vm)`. If it is 0: `no_vm`. One span `[VM+0x12d0]` → `map`. One span `map[0..8)` →
   `buckets`, `n`. Require `buckets != 0`, `n` a power of two, `n <= 1<<22`.
7. `key = ~kc`. `node = [buckets + 4·(key & (n-1))]`. Walk at most 64 nodes, one 12-byte span each
   `{next, key, obj}`, until the key matches. Otherwise `kc_missing`.
8. One span `obj[0..0x10)`: `[obj+0] == kc`, `cls = [obj+8]`, `cells = [obj+0xc]`. Then `[cls+0] == 2001`
   (one 4-byte span).
9. One span `cells[0..10)`: tags 1 and 1, `x = [cells+1]`, `y = [cells+6]`, both 0..255. Cross-check span
   `cells[5·N .. 5·N+5)` with N = 34 (AP) or 36 (FL): tag 1, value == `id`. This proves that the KC object owns
   this native sector. Any failure: `kc_mismatch`.
10. Store the cache and return `known (x, y)`.

**What can be stale, and what to return.** Return a tri-state `{known(x, y), unknown(reason)}`. The consumer
keeps the last known seed until a different known value arrives, so a transient `unknown` never reseeds the sky.

| Event | What moves | Effect on the read |
| --- | --- | --- |
| Gate, jump or scripted sector change | `[P+0x54]` and the cockpit `+0x54` are re-pointed. `M+0x38` itself changed once per transit in Run 190 ([sector-transit-order.md](sector-transit-order.md) §1). `2001::Deactivate` frees the old native sector (`SA_FreeObject m34`), and a later `Activate` allocates a new one with a new `+8` (2317 → 3221 measured in flight, [volumetric-fog ledger](../verification/volumetric-fog.md)) | key changes → re-walk; a freed `S` fails the marker (`0xefac`, then 0x130 bytes zeroed) or the class check |
| Player death | `M+0x38` is cleared in the free path | `no_player` |
| Save load | Objects are freed and restored. `+0x94` and `+8` come back from the stream (`0x0043013b` / `0x00434665`). KC ids are re-inserted with their saved keys | refuse while the load bracket is active (`current_load()` on AP). Bump the load epoch so the cache is rebuilt even if `(S, id, kc)` repeat |
| New game / main menu | VM persists for the process. Whether the menu scene has a player ship and sector is not established | whatever the chain yields passes all checks or is `unknown`; never garbage |
| KC hash growth | bucket array reallocated (`0x004efeb0`) | walk runs only on a key change; a failed read is `unknown` for that frame |
| Universe edits | `308::DoAlterSector` rewrites music and background, not `m0`/`m1` | x/y unchanged |

**Hook-site suitability.** No hook and no patch. The sequence is pure validated reads: no instruction-boundary,
register or flag liveness question. It makes no engine call, allocation or lock, so it is reentrancy-safe. It
belongs inside the capture lock the Present path already holds. Cost: one header span per frame. On a sector
change it costs at most about 72 spans, and in the expected case about 8 (one image global, map header, a
short bucket chain, object, class, two cell spans).

## 4. Simpler keys, and the music id (Q4)

| Candidate | Stable across visits and sessions? | Evidence |
| --- | --- | --- |
| **x/y (`m0`/`m1`)** | **Yes.** Fixed per map, unique (0 duplicates in 247 / 239 / 298 sectors) | §1, §2 **[m]** |
| Native id `[S+8]` | No. A new id on every `Activate` (monotonic per session) | 2317 → 3221 in flight **[m]** |
| KC id `[S+0x94]` | Within one savegame lineage only. Keys survive save/load, but the id is chosen by the free-key allocator `0x004efcc0` when the universe is built, so it is not guaranteed across new games or mod changes | §1 **[m]**; cross-game equality **[i]**, not checked |
| Background index `[S+0x13c]`, `neb`/`stars` `+0x140/+0x144` | No. Shared by many sectors (83 rows for 239+ sectors; `neb`/`stars` 0 in every stock sector) | [sector-fog.md](sector-fog.md) §11, [sector-background-modern.md](../architecture/sector-background-modern.md) |
| Sector name | It is a function of x/y: text page 7, id `1020000 + 100·(y+1) + x + 1` (`sector_fog_census.py`) | no simpler index exists |
| Music id (`m32` AP, `m33` FL) | No. See below | **[m]** |

**The music id is a per-sector map attribute, not a per-type value, and not unique.** Each `<o t="1">` carries its
own `m`. `2001::Create` and `2001::SetMusicTrack` are its only writers (callers `300::BuildObjects`,
`BuildFromEditor`, `202::Patch6`/`702::__runScript`, `308::DoAlterSector`). It does not encode x/y: `80412` sits
at (12, 18), `80202` at (11, 15), `80300` at (11, 14), `81616` at (14, 8) in the loaded AP map. `8404` and `81307`
are not sector `m` values on this map. Sharing:

| Map | Sectors | Distinct `m` | `m` values shared | Sectors on a shared `m` | `m = 0` (falls back to the x/y hash) |
| --- | ---: | ---: | ---: | ---: | ---: |
| AP+Mayhem (loaded) | 247 | 198 | 44 | 93 | 0 |
| stock AP | 239 | 40 | 5 | 204 | 188 |
| FL | 298 | 48 | 18 | 268 | 196 |

The music note's flown ids (8404, 80412, …) are therefore track numbers, not coordinates. They are unusable as a
seed.

## 5. Not established

- No runtime confirmation on either edition. The first diagnostic should log `(S, id, kc, x, y, status)` on
  change and compare x/y with the sector the user names (Argon Prime is (1, 3) in [sector-fog-census.csv](sector-fog-census.csv)).
- Whether the main menu's scene has a player ship and a class-1 sector.
- Whether other AP mods' story objects keep `m34` as the native-id member. `m0`/`m1` are the same in all three
  builds checked. If `m34` moves, the cross-check fails closed (`kc_mismatch`); the coordinates do not move.
- The KC hash's load factor, which sets the real chain length. The 64-link bound is a guess, not a measured value.
