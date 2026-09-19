# Sector fog and nebula in X3: Albion Prelude

Answer to "does the game have genuine in-sector fog, in which sectors, and how is it
rendered". Static analysis only: archive data from the read-only install plus bounded
disassembly of `X3AP.exe` (preferred base `0x00400000`, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`). No game, Wine or
build was run. Extracted archive members stayed under the session scratch directory;
only derived tables are recorded here. Companion notes:
[asteroid distance fog](asteroid-fog-temporal.md) (the `+0x148/+0x14c` camera path) and
[the volumetric fog design](../architecture/volumetric-fog.md), whose "universe-map
attribute: not traced" row this note closes.

## 1. Answer

**Yes.** Two independent mechanisms, both driven by the background type record, not by
any attribute on the sector element:

1. **Distance fade** (the engine's "fog"): per-sector `FogNear`/`FogFar` distances, an
   alpha fade of distant render nodes with no colour. Active in 51 of 239 sectors in the
   sense that `FogNear` is smaller than the sector's own size; in the other 188 the near
   distance is at or beyond the sector bounds and nothing fades inside it.
2. **In-sector fog cards**: `NumDustInstances` large textured quads spawned around the
   camera and drawn with `nebulafog.fx` — pixel shader `f7e0b6647a3bfa62`, the program
   never seen in the first 115 captured sessions and first captured in run174 (Argon
   Prime, section 10). **35 of 239 sectors** have
   `NumDustInstances > 0`; the remaining 204 have exactly zero and can never draw it.

Neither mechanism is a map-placed object. The universe map contains **no** fog or nebula
bodies: all 913 `t="20"` special objects across all sectors are signs, wrecks, destroyed
stations, gate debris and the Hub parts; there is no fog body among them.

## 2. Where the data comes from

| Item | Winning archive member |
| --- | --- |
| Universe map | `addon/maps/x3_universe.pck` in `addon/02.cat` (1,402,139 bytes decoded, 239 `<o t="1">` sectors) |
| Background types | `addon/types/tbackgrounds.pck` in `addon/03.cat` (header `25;83;`, 83 records x 38 columns) |
| Sector names | page 350007 (fallback page 7) of `t/0001-L044.pck`, merged over all base and addon layers |

CAT index is XOR `(0xdb + i) & 0xff`; DAT payload is XOR `0x33`; `.pck` members are gzip
under a single-byte XOR. Later catalogue numbers win, and `addon/` wins over the base.

Sector name id is `1020000 + 100*(y+1) + (x+1)`, confirmed against `1020101 = Kingdom End`
at grid `(0,0)` and `1020212 = Saturn` at `(11,1)`. 224 of 239 grid cells resolve; the
rest display as "Unknown Sector" in game, which several named-in-the-file pirate sectors
also do (the file keeps the developer name in parentheses, which the engine strips as a
comment).

### The sector element carries no fog attribute

`<o t="1" f=".." x=".." y=".." r=".." size=".." m=".." p=".." qtrade=".." ...>` — `f` is
the generic object flag word that also appears on docks and ships (`f="1"` on `t="5"`
and `t="7"` elements); its values over sectors are `None` 167, `4` 61, `1` 10, `5` 1 and
it does not correlate with fog (of the 61 sectors with `f="4"`, 12 have fog cards and 49
do not). The only fog-relevant child is the background element
`<o t="2" s="N" neb="0" stars="0"/>`: **`s` is a zero-based index into TBackgrounds**,
and `neb`/`stars` are per-sector overrides of which nebula/star body of that background
to use (0 in every one of the 239 sectors).

Zero-based was settled on the data: the nine Sol sectors (Earth, Mars, Venus, Mercury,
Jupiter, Saturn, Neptune, Pluto, The Moon) all carry `s="67"`, and record 67 is
`solarsystem`; `Uranus` carries `s="76"`, record 76 is `uranus`.

## 3. The TBackgrounds record, from the loader

`*0x00606fc0` is element 2 of the type-table pointer array `0x00606fb8`, with its record
count at `0x00607040` (element 2 of `0x00607038`). Stride `0xdb8` is the common type-record
stride for every T-file, so the column-to-offset map below is specific to TBackgrounds.

The record parser is inside `FUN_00434e40` at `0x004367e0-0x0043695d`. `CALL 0x004ea610`
reads the next integer token, `CALL 0x004ee250` the next string token, and `[ESP+0x40]`
holds the file version (`25;83;` -> 25). Fields in parse order:

| File column (0-based) | Record offset | Meaning | Evidence |
| --- | --- | --- | --- |
| 7 (name) | `+0x44` | body family name, e.g. `fogblue` | `0x004367f6`; used as the path stem at `0x0043695d` |
| 8 | `+0x48` | count for `+0x4c[16]`, clamped to `[1,16]` | `0x00436806`; array filled at `0x00436aee` with `environments\nebulae\<fam>\nebula_<fam>_stars_%02d` |
| 9 | `+0x8c` | count for `+0x90[16]`, clamped to `[1,16]` | `0x0043681c`; `..._background_%02d` at `0x00436c15` |
| 10 | `+0xd0` | count for `+0xd4[8]`, clamped to `[1,8]`, defaults to 1 below file version 25 | `0x0043683f`; `..._outside_part%02d` at `0x00436d47` |
| 11-18 | `+0x114[8]` | **DustBodyRate[8]**, weights over the 8 dust body slots | 8-iteration loop at `0x00436862`; body ids at `+0xf4[8]` from `..._dust_part%02d` at `0x00436e6a` |
| 19 | `+0x134` | **NumDustInstances** | `0x00436888`; consumed at `0x0041f0ba` |
| 20,21,22 | `+0x138/+0x13c/+0x140` | colour triple A (0,0,0 in every shipped record) | `0x00436895`-`0x004368af`; read as one packed RGB by the script accessor at `0x004678ec` |
| 23 | `+0x144` | scalar (10 only for `midnightblue`, else 0) | `0x004368bc` |
| 24 | `+0x148` | **FogNear** -> sector camera `+0x36c` | `0x004368c9`; camera copy at `0x00421548` |
| 25 | `+0x14c` | **FogFar** -> sector camera `+0x370`; if `col25 < col24` the loader zeroes **both** | `0x004368d6-0x004368ec` |
| 26 | `+0x150` | stardust population percent, defaults to 50 below file version 24 | `0x00436900`; used as `(v << 9) / 100` at `0x0041f0de` |
| 27,28,29 | `+0x154/+0x158/+0x15c` | colour triple B (120,120,120 in 82 of 83 records); defaults to triple A below version 24 | `0x0043690d`-`0x0043692f`; copied into the dust scene object at `0x0041efe3`-`0x0041eff5` |

Columns 0-6 and 30-37 are parsed before/after this window and are zero or constant in the
shipped file except column 37, the `SS_BG_<n>` text key.

The script command names for these fields exist in the executable at `0x0055c3fc` and
following: `SA_BgTypeFromBody`, `SA_SetBgTypeData`, `SA_GetBgTypeStarBodyID`,
`...NebulaBodyID`, `...StardustColor`, `...StardustDensity`, `...FogFar`, `...FogNear`,
`...HueModifier`, `...AmbientColor`, `...NumDustInstances`, `...DustBodyRate`,
`...NumNebulaOuterBodies`, `...NumBgNebulaBodies`, `...NumBgStarBodies`,
`...BodyFamilyName`, registered through the pointer table at `0x0057ab7c-0x0057abb8`
(descending address order). The accessor handlers are the block `0x004677b4-0x00467b38`
inside `FUN_00460630`; they read exactly the offsets above, including the index-bounded
`+0x114[i<8]` array (`0x00467884`), so `DustBodyRate` is `+0x114` and `NumDustInstances`
is the neighbouring scalar. **Not resolved:** which of colour A / colour B is
`StardustColor` and which is `AmbientColor`, and whether the `+0x144` scalar is
`HueModifier`; the handler emission order does not follow the name table order (the table
lists `FogFar` before `FogNear`, but `+0x148` is provably the near distance), so the
remaining names cannot be assigned positionally. It does not affect the fog answer:
colour A is `(0,0,0)` in all 83 records and colour B is `(120,120,120)` in 82 of them.

## 4. How in-sector fog is rendered

`FUN_0041efc0` is the per-sector dust/fog scene updater. It takes the sector
(`+0x13c` = background index), computes `record + 0x44`, and:

1. copies colour B into the scene object at `*(param2+0x1c) + 0x30/0x34/0x38`;
2. builds a 16-bit cumulative distribution over `DustBodyRate[8]` (`rate * 0x10000 / sum`);
3. walks the scene's object list, counting objects **without** flag `0x4000000` against
   `NumDustInstances` (read at `0x0041f0ba`) and objects **with** it against
   `(col26 << 9) / 100` — two populations in one list, the second being the stardust
   streaks;
4. for each missing instance it allocates one (`FUN_00486d10`), places it at one of 16
   fixed offsets from the table at `0x0057adf0` (`0x0041f353`) scaled by 8, draws a `rand()` from the
   CDF and instantiates the chosen body id from `+0xf4[8]` (`0x0041f1ca`, `0x0041f3c8`),
   with a random roll.

The dust bodies are `objects/environments/nebulae/<family>/nebula_<family>_dust_part01..06.pbd`.
Each is a **single quad 199,998 x 198,978 units** (automatic body size 55,000) carrying
one material (text quoted from `foggreeneye` dust part 01; `bluedistance` part 01 is
identical apart from the texture path):

```
MATERIAL6: 0;0x2000000;1;nebulafog.fx;...;g_AlphaBlendEnable 1;g_SrcBlend 2;g_DestBlend 4;
           g_ZEnable 1;g_ZWriteEnable 0;g_CullMode 2;
           t_DiffuseTexture environments\nebulae\<family>\nebula_<family>_background_diff.tga
```

That is the `nebulafog` program family already inventoried in
[effect shader users](effect-shader-users.md) (277 materials, all under
`objects/environments/nebulae`, all screen-blend, never created in a preserved session).
All 11 families used by a fog sector ship exactly 6 dust parts, so the *bodies* exist wherever
they are needed (5 of the 51 families -- `bluenova`, `closeplanet`, `orangeblackhole`,
`orangenexus`, `yellowrift` -- ship none, and all 5 have zero instances anyway). What
differs is `NumDustInstances`, which is **0 in 60 of the 83 background records**. That is
the gate the volumetric-fog note was looking for, and it is sufficient to explain
`nebulafog` being absent from all 115 logs: 204 of 239 sectors can never draw it. Which
sectors those sessions were actually flown in is not recorded, so this is the explanation,
not a measurement of it.

## 5. Sectors with genuine in-sector fog (35 of 239)

Ranked by `NumDustInstances`, then by `FogNear / sector size` (smaller = the distance fade
bites earlier inside the sector). "Stations" counts `t="5"` docks plus `t="6"` factories
placed in the map; it excludes anything a running economy adds. Distances are native
units; the engine scale seen in `object_fade` rows is 0.01, so 3,000,000 native is
30,000 game units against a 25,000,000-native (250,000-unit) sector.

| Sector | grid x,y | Race | Background family | NumDustInstances | FogNear (native) | FogFar (native) | Sector size | Stations |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: |
| Unknown Sector (Veil of Delusion) | 16,16 | Pirate | fogdeepred | 50 | 100,000,000 | 125,000,000 | 20,000,000 | 2 |
| Unknown Sector | 19,9 | Pirate | foggreeneye | 16 | 3,500,000 | 5,000,000 | 40,000,000 | 2 |
| The Hole | 2,2 | Argon | foggreenoutlands | 16 | 3,000,000 | 3,500,000 | 27,500,000 | 10 |
| Atreus' Clouds | 3,2 | Boron | foggreenoutlands | 16 | 3,000,000 | 3,500,000 | 25,000,000 | 13 |
| Queen's Space | 2,0 | Boron | foggreenoutlands | 16 | 3,000,000 | 3,500,000 | 22,500,000 | 10 |
| Unknown Sector | 18,9 | Pirate | foggreeneye | 16 | 3,500,000 | 5,000,000 | 22,500,000 | 3 |
| Rolk's Fate | 3,1 | Boron | foggreenoutlands | 16 | 3,000,000 | 3,500,000 | 18,500,000 | 12 |
| Unknown Sector | 2,15 | Boron | fogred | 16 | 8,700,000 | 10,200,000 | 50,000,000 | 7 |
| Dark Waters | 1,13 | Boron | fogred | 16 | 8,700,000 | 10,200,000 | 27,500,000 | 11 |
| Priest Refuge | 8,13 | Paranid | fogdeepred | 16 | 20,000,000 | 30,000,000 | 37,500,000 | 8 |
| Unknown Sector (Acquisition Repository) | 11,9 | Pirate | fogparanid | 16 | 11,500,000 | 12,500,000 | 20,000,000 | 10 |
| Unknown Sector (Spaceweed Grove) | 11,10 | Pirate | fogparanid | 16 | 11,500,000 | 12,500,000 | 20,000,000 | 8 |
| Great Reef | 2,14 | Boron | fogred | 16 | 8,700,000 | 10,200,000 | 15,000,000 | 15 |
| Reservoir of Tranquillity | 2,13 | Boron | fogred | 16 | 8,700,000 | 10,200,000 | 12,500,000 | 9 |
| Spring of Belief | 8,14 | Paranid | fogdeepred | 16 | 20,000,000 | 30,000,000 | 20,000,000 | 12 |
| Priest Rings | 2,6 | Paranid | fogparanid | 16 | 11,500,000 | 12,500,000 | 11,000,000 | 10 |
| Cardinal's Domain | 9,13 | Paranid | fogdeepred | 16 | 20,000,000 | 30,000,000 | 18,000,000 | 11 |
| Ore Belt | 1,5 | Argon | whitenexus | 16 | 17,000,000 | 17,500,000 | 15,000,000 | 10 |
| Paranid Prime | 1,6 | Paranid | fogparanid | 16 | 11,500,000 | 12,500,000 | 10,000,000 | 12 |
| Empire's Edge | 1,7 | Paranid | fogparanid | 16 | 11,500,000 | 12,500,000 | 10,000,000 | 11 |
| Duke's Domain | 2,7 | Paranid | fogparanid | 16 | 11,500,000 | 12,500,000 | 10,000,000 | 11 |
| Cloudbase South East | 2,5 | Argon | whitenexus | 16 | 17,000,000 | 17,500,000 | 10,000,000 | 11 |
| Unknown Sector | 13,8 | Terran | fogdeepred | 16 | 500,000,000 | 500,000,000 | 10,000,000 | 1 |
| Uranus | 10,2 | Unknown | uranus3 | 15 | 500,000,000 | 500,000,000 | 30,000,000 | 6 |
| Uranus | 10,1 | Unknown | uranus | 15 | 500,000,000 | 500,000,000 | 25,000,000 | 6 |
| Light Water | 5,8 | Boron | fogcyancorner | 10 | 15,500,000 | 19,500,000 | 20,000,000 | 9 |
| Lucky Planets | 6,9 | Boron | fogcyancorner | 10 | 15,500,000 | 19,500,000 | 15,000,000 | 13 |
| Shore of Infinity | 5,9 | Boron | fogcyancorner | 10 | 15,500,000 | 19,500,000 | 14,500,000 | 10 |
| Unknown Sector | 16,0 | Terran | fogcyancorner | 10 | 15,500,000 | 19,500,000 | 10,000,000 | 0 |
| Unknown Sector (Vestibule of Creation) | 2,16 | Pirate | fogbluedistance | 8 | 2,350,000 | 3,850,000 | 10,000,000 | 2 |
| Getsu Fune | 17,1 | Argon | fogbluedistance | 8 | 18,500,000 | 23,500,000 | 31,500,000 | 11 |
| Herron's Nebula | 1,2 | Argon | bluewell | 8 | 18,000,000 | 18,500,000 | 25,000,000 | 11 |
| Argon Prime | 1,3 | Argon | bluewell | 8 | 18,000,000 | 18,500,000 | 22,500,000 | 10 |
| Menelaus' Paradise | 18,1 | Boron | fogbluedistance | 8 | 18,500,000 | 23,500,000 | 22,500,000 | 10 |
| Cloudbase North West | 0,2 | Argon | bluewell | 8 | 18,000,000 | 18,500,000 | 16,500,000 | 10 |


Backgrounds `fogkhaak` (21 and 50 instances) and `khaakhive` (50) carry the densest fog in
the type file but are used by **no** sector of the shipped universe map; they only appear
in Kha'ak hive content placed by scripts.

## 6. Sectors with distance fade but no fog cards (top 12 of 32)

These have `NumDustInstances = 0` yet `FogNear` inside the sector bounds, so distant
objects fade without any fog geometry.

| Sector | grid x,y | Race | Background family | NumDustInstances | FogNear (native) | FogFar (native) | Sector size | Stations |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: |
| Xenon Sector 597 | 19,1 | Xenon | brennanstriumph | 0 | 7,500,000 | 10,000,000 | 50,000,000 | 1 |
| Unknown Sector | 7,16 | Paranid | bluenexus | 0 | 11,000,000 | 17,000,000 | 50,000,000 | 6 |
| Unknown Sector (Bright Profit) | 18,12 | Teladi | brennanstriumph | 0 | 7,500,000 | 10,000,000 | 20,000,000 | 8 |
| Duke's Vision | 1,9 | Paranid | bluenexus | 0 | 11,000,000 | 17,000,000 | 25,000,000 | 10 |
| Unknown Sector | 0,9 | Yaki | bluenexus | 0 | 11,000,000 | 17,000,000 | 22,500,000 | 2 |
| Xenon Sector 596 | 19,0 | Xenon | reddawn | 0 | 22,500,000 | 25,000,000 | 45,000,000 | 1 |
| Akeela's Beacon | 8,6 | Argon | rednexus | 0 | 22,500,000 | 25,000,000 | 42,500,000 | 12 |
| Unknown Sector (Clarity's End ARGON/PARANID SECTOR 6-11) | 6,11 | Paranid | bluenexus | 0 | 11,000,000 | 17,000,000 | 20,000,000 | 7 |
| Unknown Sector (Hollow Infinity BORON SECTOR 18-14) | 18,14 | Boron | bluenexus | 0 | 11,000,000 | 17,000,000 | 20,000,000 | 7 |
| Brennan's Triumph | 4,5 | Pirate | brennanstriumph | 0 | 7,500,000 | 10,000,000 | 12,500,000 | 5 |
| Unknown Sector | 8,8 | Teladi | darkhorizon | 0 | 22,500,000 | 25,000,000 | 37,500,000 | 5 |
| Aldrin | 13,10 | Unknown | orangenexus | 0 | 100,000,000 | 100,000,000 | 150,000,000 | 23 |


## 7. What the captured values correspond to

| Captured `object_fade` (N, F) | Runs | Map interpretation |
| --- | --- | --- |
| 25,000,000 / 30,000,000 | 37 | 61 sectors; the green ones are the `greenoutlands` (8 sectors: Kingdom End, Rolk's Drift, Menelaus' Frontier, and 5 Unknown Sectors) and `greenvoid` (5: Ceo's Buckzoid, Teladi Gain, Family Whi, Argon Sector M148, one Unknown) families. **All have `NumDustInstances = 0`**, and `FogNear` 25,000,000 exceeds every one of their sizes (18,500,000-27,500,000), so neither mechanism is active: the green is the painted sky alone. |
| 50,000,000 / 55,000,000 | 1 (run48) | 66 sectors: `standardblack` 47, `greeneye` 8, `burninghorizon` 5, `purpleoutlands` 4, `greenspot` 2. |
| 500,000,000 / 500,000,000 | 21 | 25 sectors: the 21 Sol/Terran sectors on `solarsystem` plus `standardblack`/`oos_v*` cases. The far value also gets floored to 500,000,000 by the configuration integer at `*0x00606f34 + 0x768 >= 3`, so these runs are consistent either way. |

| 18,000,000 / 18,500,000 | run174 | `bluewell`: Argon Prime, Herron's Nebula, Cloudbase North West. The user flew Argon Prime; the row matches the table in section 5 and the fog cards are in every captured frame (section 10). |

So the user's "green nebula sector" is a painted `greenoutlands`/`greenvoid` background
with no in-sector fog at all. The first frames containing the fog cards are run174.

## 8. Recommended test capture

Sectors that combine the maximum `NumDustInstances` with a short `FogNear` relative to
their size and enough stations to give the shot large occluders:

1. **Atreus' Clouds** (Boron, grid 3,2) — `foggreenoutlands`, 16 fog cards, FogNear
   3,000,000 against a 25,000,000 sector (the shortest fade of any well-populated
   sector), 13 stations, 3 gates. First choice.
2. **Great Reef** (Boron, grid 2,14) — `fogred`, 16 fog cards, FogNear 8,700,000 against
   15,000,000, **15 stations**, 3 gates. A different colour family, densest station set of
   the fog sectors.
3. **Paranid Prime** (Paranid, grid 1,6) — `fogparanid`, 16 fog cards, FogNear 11,500,000
   against 10,000,000 (fade covers the whole sector), 12 stations, 3 gates.

`The Hole` (Argon, 2,2) and `Rolk's Fate` (Boron, 3,1) are equivalent substitutes for (1).
For a no-travel baseline, **Argon Prime** (1,3) already has 8 fog cards on `bluewell`, so
pair it with any zero-card sector, for example **Kingdom End** (0,0), for an A/B.

## 9. What remains unknown

- Names for colour A / colour B and the `+0x144` scalar (`StardustColor`, `AmbientColor`,
  `HueModifier` in some order); the shipped values make the distinction inert.
- Resolved by run174 (section 10): the capture hooks do see the dust camera's draws, and
  the cards' visual weight is measured. Still open: a capture in a 16-card sector
  (`foggreenoutlands`, `fogred`, `fogparanid`), where the card texture and weight may differ.
- Columns 0-6 and 30-36 of TBackgrounds (constant in the shipped file) and the `p` and `m`
  attributes of the sector element.

## 10. Measured: Argon Prime, run174 (2026-09-19)

Four 32-frame `--taa-debug` captures (frames 23674-, 25317-, 26413-, 28628-) under
`/tmp/x3-bottleX3-run174`, 1280x768, 128 frames, 34,720 draws. The survey needed no
correction: Argon Prime is already in section 5 (`bluewell`, 8 instances) and the sector
camera's `object_fade` row reads `near36c` 18,000,000 / `far370` 18,500,000, scale 0.01,
`flags270 = 0x0085492d`, `config768 = 3`, in all 128 frames.

| Question | Measured |
| --- | --- |
| Present | 422 `nebulafog` draws (VS `7b6393fe2d3e1d85`, PS `f7e0b6647a3bfa62`, 2 primitives, 4 vertices, 24-byte stride: FLOAT16_4 position, texcoord, normal; **no vertex colour**). Per frame 1-4 of the 8 instances: 4 in captures 1, 2 (3 in its last 5 frames) and 4, 1-3 in capture 3. |
| Camera | All on a separate dust camera (`flags270 = 0x00009201`, N = F = 0), not the sector camera. Cards are camera-facing billboards (world basis row 2 = the view forward axis) with a random roll, world scale 1800 (two instances 7200), centred 340-2,500 units in front of the camera; the lattice position is `0x40000`/`0x80000` native multiples as section 4 derived. At that range one card covers most of the screen. |
| State | `ZENABLE 0`, `ZWRITEENABLE 0`, `ALPHABLENDENABLE 1`, `SRCBLEND ONE`, `DESTBLEND INVSRCCOLOR`, `BLENDOP ADD`, `ALPHATESTENABLE 0`, `CULLMODE NONE`, colour write 7, `FOGENABLE 0`; identical in all 422. The material text says `g_ZEnable 1` / `g_CullMode 2`; the dust pass overrides both. **The cards are never depth tested.** `D3DRS_FOGENABLE` is 0 on all 34,720 draws of the session. |
| Texture / alpha | One 512x512 DXT1 texture (10 levels) for all six dust models `0x501a-0x501f`. PS c0.x = node `alpha13c` / 255 scales RGB; it ranged 1-255 and ramps by about 6 per frame (251 -> 64 over 32 frames in capture 2), so instances fade in and out over roughly 40 frames; there is no pop. |
| Order | Last scene draws: opaque hulls, glow quads (`d5e1c753...`/`8360f422...`, additive), `36f98d15...`, stardust (`5e484a06...`/`0a523f33...`), **fog cards**, then the game's scene end (`set_depth`, 3 `color_fill`, `stretch_rect`), bloom quads, HUD. So the proxy's scene-end hook (TAA resolve, bloom, AgX) sees them as ordinary scene colour. |
| Motion route | `gate=3 routed=0 unmatched=unregistered`; bound surfaces are rt0 and depth only. They write neither the motion nor the depth target; pixels behind them keep their own depth, sentinel and motion. |
| Weight | Screen blend in engine space lifts the black floor identically on the own ship (49-300 units), the station at 10,000-18,000 units and the sky: p2-p10 engine-space floor (0.0055, 0.017, 0.026) in capture 4 (card alphas 1.00, 0.96, 0.47, 0.23), (0, 0.004, 0.006) in capture 3 (alpha 0.33). The fade-out in capture 2 (one card 0.98 -> 0.25) changed the star-free low-passed sky by mean (0.0004, 0.0023, 0.0042), p95 (0.009, 0.016, 0.024), against a painted sky of mean (0.012, 0.033, 0.052). The cards are a faint blue cloud-textured veil, 7-39 % of the sky median; most of what reads as "fog" in Argon Prime is the painted `bluewell` background. |

Side finding, not fog: the sun-shadow lane refused 18,585 of 19,311 frames of this session
(`untracked_writers >= 1`, `unregistered = 1`): an opaque depth-writing pair
VS `ac2319bc3953efc6` / PS `03a16e5c63daa6e8` (924-1000 primitives per draw, 1-3 draws per frame) is not registered, so sun shadows were off in all four captures.

## Reproduce

Archive side (scratch script, reads the install read-only; no game bytes enter the repo):
decode `addon/02.cat` -> `addon/maps/x3_universe.pck`, `addon/03.cat` ->
`addon/types/tbackgrounds.pck`, and merge page 350007/7 of every
`t/0001-l044.pck` layer.

Disassembly, Ghidra 12.1.3 against the existing read-only project:

```sh
JAVA_HOME='/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home' \
  '/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless' \
  /tmp/x3-ghidra-research X3Render -process X3AP.exe -readOnly -noanalysis \
  -scriptPath tools/analysis -postScript X3CameraState.java /tmp/out.txt \
  data:00606fc0 data:0055f95c data:0055faa8 data:0055fa60 data:0055fa54 data:0055fa80 \
  ptr:0057ab70:32 range:004367e0:420 range:00467787:400 dec:0041efc0 dec:004343e0 txt:0xdb8
```
