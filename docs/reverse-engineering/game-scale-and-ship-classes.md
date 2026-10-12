# Game scale, ship classes and obstacle geometry (installed Mayhem 4 vs stock AP)

2026-10-12. Data extraction from the installed type tables, universe maps, KC script and `ShipStats.xlsx`,
plus a bounded `X3AP.exe` check of the unit scale (SHA-256 `fdbf3418…f8ab`, preferred VAs). Inputs for the
predictive collision-avoidance layer and the steering law; the movement integrator is
[ship-movement.md](ship-movement.md), the AI goal side [ship-navigation-ai.md](ship-navigation-ai.md).
Nothing was launched; no Wine. Scripts and their outputs (derived numbers only, no game bytes):
[`verification/results/game-scale/`](../../verification/results/game-scale/). Marks: **[m]** read from data or
code, **[s]** static reading in the cited note, **[i]** inferred, **[p]** proposal for the design agent.

**Answer.** **500 engine units = 1 m** and **500 units/s = 1 m/s** **[m]**: the engine converts the
metre-valued Globals with `imul 500` at load and compares the results with position distances and with the
object speed. Rotation columns are **turns per second** (deg/s = 360 × raw) **[m]**. Mayhem 4 makes small ships
about 4–5× faster than stock (M5 median 541 vs 120 m/s) and capital ships 3–6× quicker to turn (M2 11.2 vs
3.2 deg/s). Capital ships are larger than their own turning circle: a Boreas (M2) has a radius of 705 m against a
297 m turn radius. Their avoidance distance is therefore set by geometry, not by dynamics.
Proposed lookahead `L = clamp(0.5 s·v + 1.25·min(√(C(C+2v/ω)), v²/2a), C + r, 30 km)`.
Against a ship of the same class it gives 442 m (M5), 288 m (M3), 531 m (M6), 382 m (TS) and 2.47 km (M2);
against a station of 1.5 km radius it gives 1.5–3.3 km (§10).

## 1. Unit scale from the EXE

Script `exe_units.py` re-decodes every site below (17/17 PASS, `exe_units.txt`).

| fact | site | evidence |
| --- | --- | --- |
| Globals ids: name table of `(char*, id)` pairs at `0x00552b50`; `SG_MAX_DISTANCE_COMM` = 0x60, `SG_TRACTOR_BREAK_DIST` = 0x7a, `SPRING_DIST` 0x7b, `SPEED_LIMIT` 0x7d, `SG_MAX_VERTEXDIST_DOCKCOMPUTER` 0x62 | `0x00552b50..` | **[m]** |
| `types/Globals` loader fills `(id, value)` pairs at `0x00606fa4` (count `0x00606fa8`) | `0x00434c30` | **[m]** |
| metre-valued globals × 500 into statics: COMM → `0x0057a6d0`, tractor break/spring/speed → `0x0057a764/68/6c` (`imul eax,eax,0x1f4`). The Globals file comments these as "in m" (COMM: "largest Terran station ca 25km diameter") | `0x004333f8`, `0x00433daa`, `0x00433dd2`, `0x00433df9` | **[m]** |
| tractor update: speed limit (80 m/s × 500) compared with and written to the object speed `[obj+0x10]`; break distance (1332 m × 500) compared with the distance computed from `[obj+0x70]+0x30/34/38` positions | `0x0044a520` (`0x0044a54a`, `0x0044a72d`) | **[m]** |
| odometer carries every 500,000 position units into the km counter | [ship-movement.md](ship-movement.md) §7 | **[s]** there |
| scanner ranges: stock `SG_SCANNER_RANGE_SHIP` 5,000,000 / `UPGRADE2` 10,000,000 / `UPGRADE3` 15,000,000 = 10/20/30 km at 500/m (the 10/20/30 km scanners the community documents for AP); `FREIGHTSCANNER` 2,000,000 = 4 km | Globals | **[i]** consistency |
| the HUD-derived 5.07 render units per m ([station-material-distance.md](station-material-distance.md)) is engine/≈100 | — | **[i]** consistency |

The "M/K" formatter at `0x00424e00` (`"%d M"`, `"%3.1f M"`, ×1e-6) is a value abbreviator, not the distance
readout; the player's metre/km strings are formatted in KC. Not traced further: the time base is settled
(rates × `dt_ms`/1000, `dt` clamped to 1000 ms at `0x004597e5`).

**Angles:** `0x004f0270` masks each angle to 16 bits and scales by `1/65536 × 2π` (`0x005654e0`,
`0x005654f8`) **[m]**. The integrator applies `rate × dt_ms / 1000` per axis (`0x00459f0f..`) and feeds the
three deltas to `0x004f0270` (`0x0045a39b..0x0045a3ad`) **[m]**. TShips columns 2/3/4 are stored as 16.16
(`0x004eafc0` → `0x004ec490`) at record `+8/+0xc/+0x10`. The getters `0x00450d60`/`0x00450cf0`/`0x00450dd0`
return `value × (1 + rudder-tuning bonus [ext+0x270]/65536)`. So a column value is turns/s **[m]**. Axis names:
yaw/pitch/roll per ship-movement.md **[i]**. Speed (`+0x44`) and acceleration (`+0x48`, getter `0x00450b80`, ship branch `0x00450bbf`) are
both multiplied by `(1 + engine-tuning bonus [ext+0x274]/65536)` **[m]**. All tables below are **base
(untuned)** values.

## 2. Collision radius `[obj+0xa4]`, and how it is estimated statically

At object creation (`0x0043ffa0`, `0x00441b4c..0x00441c45`) planets take `[node+0x70]`, suns a TSuns value or
110,000,000, bullets the node scale × TBullets `+0x68/+0x6c`, and TSpecial subtype 0x5c a fixed 10,515,000
(21 km). **Every other class** gets `0x00488170(root)` **[m]**. That function is recursive and caches its
result in `+0xa4`: `radius(node) = max(+0xa0, max over children (radius(child) + max_axis |child local
position|))` **[m]**. Here `+0xa0 = +0x70` is the body's LOD-0 value (`0x00487e30`), multiplied by the largest
node scale in `0x00488270`. LOD-0 points are normalised to `max |coord| = 65536`, so the body value is the
body's L∞ half-extent in engine units ([body-format-bob1.md](body-format-bob1.md) §3) **[i]**. The ship root
model is TShips column 0, which is body 0 (value 47) on 524/525 rows.

`scene_radius.py` applies this rule to the text scenes. Parts are `P n; B body; F parent`, each at its first key
position; numeric bodies map to `objects/v/%05d` and dummies count 0. It also reports a **hull** value: the
largest non-effect, non-dummy part. **Validation [m]:** the only runtime radius on record is
`radius_a = 9,205,962` for TDocks index 26 (`collide_narrow_pair`, [sector-collide.md](sector-collide.md)). The
static estimate for stock TDocks[26] "Orbital Defence Station" (`usc_dock_e_scene`) is **exactly 9,205,962**
(`radius_crosscheck.py`). Not modelled: part rotation, dummy cut scenes (dock ports), animated keys.

The radius is a loose sphere: L∞ offsets plus child radii, and engine-jet bodies are included (a jet's max
scale is 1). It is what the engine's broad phase uses, not a physical clearance. Stations come out at 0.5–52 km.

## 3. TShips layout and its validation

X3 Editor 2 layout, 0-based: 0 root body; 2/3/4 rotation (yaw/pitch/roll, turns/s); 5 subtype `SG_SH_*`;
6 name id (page 17); 7 speed (+0x44); 8 acceleration (+0x48); 16 scene (+0x68); 26/27 max engine/rudder tunings;
28/29 cargo; 45 race; 46 hull; 50 variation; 52 `OBJ_SHIP_*`. The engine loader `0x004373e9` writes columns
7..21 to `+0x44..+0x7c` in order, and the common header `0x004364cb..0x00436507` writes columns 1..6 to
`+4..+0x18` **[m]**. Validations **[m]**:

* `ShipStats.xlsx` (the Mayhem 4 author's sheet, speed in m/s): **232/232** name-matched rows have a TShips row
  with `col7/500` within 1 m/s (`ships.py`).
* The installed Discoverer (M5) runs at 532 m/s, Boreas (M2) at 70 m/s and Mammoth (TL) at 113 m/s. Stock:
  Discoverer 98.9 m/s, 121.8 deg/s; Boreas 42 m/s, 3.0 deg/s; Mammoth 58.4 m/s, 2.1 deg/s yaw.

Sources: installed `addon/12.cat` TShips (decoded SHA-256 `132706caad0f…`); stock `addon/03.cat` (`84df8364a122…`).
Catalogue `.pck` members are plain gzip after XOR 0x33. Only a loose `.pck` needs its first byte XOR 0xC8, and
several Mayhem `.pck` members are plain text **[m]** (resolver `sector_fog_census.Assets`, `unpack`).

## 4. Q1 — per-class dynamics and size (base values; min/median/max)

Raw columns: speed ÷ 500 = m/s, accel ÷ 500 = m/s², rotation raw = turns/s (deg/s = 360 × raw). The radius is
the §2 estimate; the hull value is the largest hull part.

**Installed (Mayhem 4)** **[m]**

| class | n | speed m/s | accel m/s² | yaw raw (turns/s) | yaw deg/s | roll deg/s | radius m | hull m |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| M5 | 49 | 123/541/766 | 10.0/55.4/110.7 | 0.050/0.410/2.000 | 18.0/147.7/720.0 | 12.0/118.1/648.0 | 1/10/17 | 1/9/17 |
| M4 | 58 | 121/362/504 | 11.8/40.0/65.0 | 0.142/0.344/1.092 | 51.0/123.8/393.2 | 46.2/99.0/372.7 | 3/11/20 | 3/10/20 |
| M3 | 88 | 127/267/487 | 7.0/40.0/65.0 | 0.092/0.258/0.394 | 33.0/92.8/141.8 | 30.0/74.2/113.4 | 10/15/40 | 10/15/40 |
| M6 | 62 | 120/186/292 | 8.4/20.0/40.0 | 0.060/0.092/0.305 | 21.6/33.1/109.8 | 16.2/26.5/87.6 | 37/90/140 | 33/88/140 |
| M7 | 46 | 81/143/219 | 10.0/15.0/25.0 | 0.031/0.050/0.075 | 11.2/18.0/27.0 | 9.0/14.4/21.6 | 216/560/1431 | 216/560/1431 |
| M8 | 13 | 159/199/275 | 15.0/20.0/20.0 | 0.131/0.150/0.248 | 47.2/54.0/89.1 | 37.8/43.2/71.3 | 28/37/83 | 25/36/83 |
| M1 | 24 | 2/98/155 | 0.5/7.5/15.0 | 0.0017/0.025/0.030 | 0.6/9.0/10.8 | 0.6/7.2/8.6 | 227/1170/4583 | 227/1135/4583 |
| M2 | 45 | 70/104/145 | 5.0/10.0/15.0 | 0.015/0.031/0.0375 | 5.4/11.2/13.5 | 4.5/9.0/10.8 | 307/1337/2998 | 307/1337/2998 |
| TS | 69 | 58/124/255 | 1.2/20.0/40.0 | 0.022/0.050/0.063 | 7.7/18.0/22.8 | 7.0/15.5/19.6 | 31/55/97 | 28/48/97 |
| TP | 22 | 98/160/265 | 2.8/15.0/35.0 | 0.033/0.063/0.102 | 12.0/22.5/36.6 | 10.8/18.0/29.2 | 45/74/95 | 42/69/95 |
| TL | 13 | 14/104/139 | 5.0/10.0/15.0 | 0.019/0.0375/0.045 | 6.8/13.5/16.2 | 5.4/10.8/13.0 | 745/1135/1837 | 745/1135/1823 |
| TM | 18 | 200/350/448 | 10.0/10.0/15.0 | 0.025/0.050/0.063 | 9.0/18.0/22.5 | 8.1/14.4/18.0 | 77/91/128 | 67/87/120 |
| GO (UFO) | 1 | 123 | 21.6 | 3.0 | 1080 | 972 | — | — |

**Stock AP 3.x** **[m]**

| class | n | speed m/s | accel m/s² | yaw raw | yaw deg/s | roll deg/s | radius m | hull m |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| M5 | 57 | 0/120/652 | 0/47.7/110 | 0/0.335/2.2 | 0/120.5/792 | 0/99.0/720 | 1/12/21 | 1/8/17 |
| M4 | 54 | 43/75/600 | 11.8/24.1/70 | 0.225/0.351/0.470 | 80.9/126.4/169.2 | 65.4/102.0/144.0 | 3/14/26 | 3/10/26 |
| M3 | 70 | 39/66/128 | 6.7/12.8/27.9 | 0.100/0.354/0.583 | 36.0/127.4/210.0 | 36.0/103.5/168.0 | 12/19/32 | 10/15/26 |
| M6 | 28 | 42/64/100 | 3.1/5.2/15.0 | 0.018/0.036/0.140 | 6.6/13.0/50.4 | 4.8/7.9/28.8 | 22/87/141 | 22/86/140 |
| M7 | 31 | 33/64/81 | 3.0/6.2/10.0 | 0.0077/0.027/0.040 | 2.8/9.6/14.4 | 1.9/8.6/12.0 | 216/618/1707 | 216/618/1707 |
| M8 | 7 | 50/69/88 | 5.0/9.1/15.8 | 0.036/0.230/0.258 | 13.0/82.8/93.0 | 5.0/65.4/73.4 | 33/36/46 | 25/35/37 |
| M1 | 18 | 9/49/70 | 0.3/4.6/7.8 | 0.0010/0.0075/0.010 | 0.4/2.7/3.6 | 0.4/1.8/2.5 | 776/1064/2421 | 776/1064/2421 |
| M2 | 17 | 15/35/50 | 2.7/4.3/8.0 | 0.0050/0.0088/0.015 | 1.8/3.2/5.4 | 1.2/2.4/3.3 | 705/1288/2554 | 671/1225/2554 |
| TS | 46 | 27/46/69 | 1.2/2.4/5.3 | 0.022/0.046/0.070 | 7.7/16.6/25.3 | 5.0/9.7/16.8 | 32/50/140 | 28/46/140 |
| TP | 18 | 37/66/103 | 2.5/4.2/6.8 | 0.032/0.040/0.061 | 11.4/14.4/22.0 | 10.2/12.5/19.8 | 46/74/86 | 42/69/81 |
| TL | 21 | 27/58/88 | 0.9/2.7/8.0 | 0.0033/0.0058/0.010 | 1.2/2.1/3.6 | 0.6/1.9/16.4 | 745/1403/1806 | 745/1403/1806 |
| TM | 10 | 49/65/86 | 4.8/8.1/12.0 | 0.023/0.030/0.040 | 8.4/10.8/14.4 | 5.5/7.0/9.4 | 77/92/140 | 67/87/140 |

Mayhem adds no new `SG_SH_*` class: it has 521 rows (508 with speed > 0) against 404 stock (386). It drops the
stock `TL_P`/`TS_P` rows and keeps 1 of the 7 GO rows; the `OBJ_SHIP_*` sub-classes are in `ships_by_class.json`.
Mayhem sets pitch = yaw on 507/508 moving rows and roll = 0.8 × yaw on 373/508 (median roll/yaw 0.8) **[m]**.
Stock: median pitch/yaw 0.8 and roll/yaw 0.8, with outliers (the stock Mammoth rolls at 16.4 deg/s against
2.1 deg/s yaw).

**Capital dispersion (slowest steering axis)** **[m]**:
* **Installed M1** 6.0–10.8 deg/s. Excluded are the two OWP "starport/starhold" rows typed M1 (0.6 deg/s,
  2 m/s), which are effectively stations.
* **Installed M2** 5.4 deg/s (Machine Titan A) to 13.5 deg/s, with Excalibur and Forseti at 5.6 deg/s.
* **Installed TL** 6.75 deg/s (Atmospheric Lifter) to 16.2 deg/s, with Ryu at 10.8 deg/s.
* **Stock** M1 0.36–3.6 deg/s, M2 1.6–5.4 deg/s, TL 1.2–3.6 deg/s.

## 5. Q2 — derived dynamics (per ship, then min/median/max per class)

Definitions: ω is the slower of yaw and pitch; turn radius R = v/ω at maximum speed; t90 and t180 are taken at
the full rate with no angular ramp; stop is v²/2a, with the **table acceleration used as deceleration [i]**
(TShips has no separate deceleration column; the integrator's braking law is in
[ship-movement.md](ship-movement.md)); arc180 = v·π/ω is the path length of a 180° turn, whose lateral offset is
2R. The angular ramp is not included: the steering law accelerates angular rate (`0x0040eb60`, about 0.475 × the
sum of the three rates per second for player steering **[i]**), so real turns take longer.

| class | R m (inst) | t90 s | t180 s | stop m | arc180 m | R m (stock) | t180 s (stock) | stop m (stock) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| M5 | 10/206/415 | 0.1/0.6/5.0 | 0.2/1.2/10 | 76/2475/6519 | 31/649/1303 | 6/73/557 | 0.2/1.9/10 | 0/150/6125 |
| M4 | 18/179/438 | 0.2/0.7/1.8 | 0.5/1.5/3.5 | 450/1570/4635 | 55/564/1375 | 25/41/337 | 1.2/1.8/2.8 | 63/105/2571 |
| M3 | 104/173/413 | 0.6/1.0/2.7 | 1.3/1.9/5.5 | 422/828/6513 | 326/543/1299 | 19/39/91 | 1.1/1.7/5.0 | 55/184/577 |
| M6 | 147/318/584 | 0.8/2.7/4.2 | 1.6/5.4/8.3 | 206/856/2457 | 460/999/1833 | 67/337/622 | 3.6/17.0/27.3 | 205/377/829 |
| M7 | 172/456/1024 | 3.3/5.0/8.0 | 6.7/10.0/16.0 | 131/738/1599 | 539/1432/3216 | 256/382/1487 | 13.6/18.7/96.7 | 129/316/825 |
| M8 | 115/193/292 | 1.0/1.7/1.9 | 2.0/3.3/3.8 | 629/990/2060 | 360/607/917 | 48/60/289 | 2.4/2.7/17.3 | 188/235/276 |
| M1 | 191/619/1232 | 8.3/10.0/150 | 16.7/20.0/300 | 4/475/2399 | 600/1946/3872 | 410/1362/2228 | 62.5/82.8/500 | 33/268/333 |
| M2 | 297/530/1416 | 6.7/8.0/16.7 | 13.3/16.0/33.3 | 163/541/2092 | 933/1664/4448 | 477/781/1528 | 37.5/75.0/115 | 27/148/191 |
| TS | 218/399/962 | 4.0/5.0/11.6 | 7.9/10.0/23.2 | 108/640/4805 | 686/1252/3022 | 121/206/506 | 8.6/13.5/30.0 | 169/410/804 |
| TP | 182/519/702 | 2.5/4.0/7.5 | 4.9/8.0/15.0 | 208/1219/4169 | 571/1629/2206 | 196/307/437 | 9.4/15.1/21.0 | 226/518/856 |
| TL | 59/468/1180 | 5.6/6.7/13.3 | 11.1/13.3/26.7 | 10/541/1932 | 187/1470/3707 | 939/1996/3824 | 50/107/150 | 198/639/1054 |
| TM | 764/1114/1740 | 4.0/5.0/10.0 | 8.0/10.0/20.0 | 1674/5543/10035 | 2400/3500/5467 | 286/440/621 | 15.6/20.8/26.8 | 212/263/325 |

Reading: in Mayhem, fighters stop over a longer distance than they need to turn (M5 median 2.5 km to stop
against R = 206 m), so turning is the cheaper evasive action. For capitals, R (300–600 m) is smaller than the
ship's own radius (0.5–1.3 km), so the clearance geometry, not the dynamics, sets the avoidance distance.

## 6. Q3 — obstacle sizes and station spacing

Script `map_stats.py`, output `map_stats.txt`/`.json`. All radii are §2 estimates.

**Stations, radius by family (installed TDocks `addon/12.cat` + TFactories `addon/10.cat`; stock in brackets), m** **[m]**:
FAC_TECH 528/1800/2404 (528/991/6954); FAC_BIO 677/1268/2556; FAC_FOOD 615/1198/1814; FAC_POWER 697/2513/3135;
FAC_MINE 783/4137/6756; **FAC_SHIP (shipyards)** 1800/4559/52,487; DOCK_TRADE 10/1480/52,487 (778/2030/52,487);
DOCK_EQUIP 1351/4594/52,487 (1863/5565/52,487); DOCK_EXCHANGE 1351/1574/1686; DOCK_HQ 3510 (stock up to 10,402).
The largest radii are the Lost-Colony/Aldrin bases at 52.5 km (`lostcolony_shipyard_scene`), the Federal Argon
Shipyard at 8.1 km, the stock Earth Torus at 36.7 km and the Terran Orbital Defence Station at 18.4 km. These
are bounding spheres, so the physical clearance of elongated stations is much smaller (§10).

**Asteroids** (TAsteroids, 10 types; the root body is the radius; the `*_dummypositions` break-up scene is given
in brackets in case it is attached, which was not traced), m: A 3512 (8686), B 2033 (4328), C 1075 (2093),
D 578 (1790), E 331 (822), F 190 (232), G 112 (185), H 60 (130), I 29 (47), J 15 **[m]**.

| | installed (Mayhem 4 map "My", ZMap) | stock AP map |
| --- | --- | --- |
| placed asteroids (t=17) | 54,990; 14/**194**/732 per sector | 2,882; 0/6/98 per sector |
| placed asteroid radius m (min/p10/med/p90/max) | 15/15/**60**/1075/3512 | 331/2033/2033/2033/3512 |
| per-sector median asteroid nearest-neighbour km | 0.48/0.94/**1.61**/3.64/36.5 | 0.0/6.4/10.5/23.1/68.3 |
| gate radius m | 1105 (types 0–3, 5–14), 2900 (type 4), 3208 (21–24), 0 (15–20, empty body stub) | 1362 (0–3), 2498 (4), 7181 (5–8), 1359 (9–20), 1428 (21–24); corrected 2026-10-12, see below |
| stations in the map | **0** (Mayhem places stations at game start) | 1794; 0/9/23 per sector |
| station nearest-neighbour, per-sector minimum km | — | 3.02/5.69/**10.16**/24.05/161 (min/p10/med/p90/max over 203 sectors) |
| station nearest-neighbour, per-sector median km | — | 3.58/8.54/**14.3**/30.6/161 |

**Stock gate radii corrected (2026-10-12) [m].** `map_stats.py` ran the installed view first, and `scene_radius._BV`
caches body values by name across views, so its stock gate radii (2999, 2995, 3208 m) used the Mayhem bodies. A fresh
cache gives the values above ([ship-navigation-ai.md](ship-navigation-ai.md) §10.9, `gate_hole.py`). Other stock
radii in `map_stats.json`/`ships_by_class.json` that share a body name with an installed override are suspect for the
same reason; they were not recomputed.

**Gate trigger distance: not tabulated.** TGates carries only the shared trailing fields
(`128;10000;25;1;0;10000;-100000`) and the scene **[m]**. The jump is triggered by the collision path
(`"CanWarp"` in [sector-collide.md](sector-collide.md) §12) against the gate radius above **[i]**; the approach
ends at `|v|/10 + r` from the gate point ([ship-movement.md](ship-movement.md) §9).

Mayhem station spacing cannot be read statically: the installed map has no t=5/6 objects, the director
`start.xml` creates none, and the saves are the binary `XSA3` format. The stock spacing is the best available
reference **[i]**.

## 7. Q4 — mission distances

| | installed (Mayhem 4) | stock AP |
| --- | --- | --- |
| sectors | 247 | 239 |
| sector `size` attribute km | 37.1/38.7/**46.1**/52.3/55.0 (min/p10/med/p90/max) | 20/25/**40**/60/300 |
| object extent (L∞ from centre, gates, stations, asteroids) km | 23.2/48.3/**64.1**/106.7/1938 | 11.3/28.2/**44.3**/87.5/434 |
| gates per sector | 1/2/4 | 0/2/6 |
| gate-to-gate km (529 / 469 pairs) | 27.2/47.2/**65.3**/92.0/131.7 | 12.6/38.5/**58.5**/100.9/418.5 |
| gate → nearest station km | — (no static stations) | 5.4/13.4/**21.7**/44.1/128.6 |
| gate → nearest asteroid centre km | 1.4/11.8/**35.9**/87.2/159 | 4.7/10.9/22.3/46.5/194 |

The Mayhem galaxy is moderately larger than stock: median `size` 46 vs 40 km, median gate-to-gate 65 vs 58 km.
It is much denser in asteroids (median 194 per sector, 1.6 km median spacing) **[m]**. A median gate-to-gate leg
takes **120 s** for an M5 at the 541 m/s class median, 4.1 min for an M3 at 267 m/s and 10.5 min for an M2 at
104 m/s **[i]**.

## 8. Q5 — weapon, sensor and docking ranges

**Lasers** (`weapons.py`): range = TBullets `+0x50` (lifetime, ms) × `+0x54` (speed, units/s) / 1000. This is the
engine getter `0x00433e90`; the speed getter is `0x00433ee0`, and the laser→bullet link is TLaser col 9
(`+0x4c`) **[m]**.
* **Installed:** 38 lasers; range min/p25/median/p75/max **3.0/4.0/5.3/8.4/21.7 km**; median bullet speed 2000 m/s.
  IRE 3.9 km at 1500 m/s; Ion Cannon 20.1 km, Gauss 21 km at 3500 m/s, Point Singularity Projector 21.7 km.
* **Stock:** 39 lasers; range 0.79/1.97/**2.32**/4.86/100 km (the 100 km entry is `SS_LASER_DUMMY1`, a spare non-weapon row); median speed
  739 m/s. IRE 1.39 km, PAC 1.93 km, PPC 6.6 km at 333 m/s.

**Scanners** (Globals × 1/500, installed / stock) **[m]**: ship 20 / 10 km, duplex 30 / 20 km, triplex 70 / 30 km,
satellite 40 / 22 km, advanced satellite 200 / 35 km, station 50 / 10 km, freight scanner 92 / 4 km, orbital
laser 2.5 km both, nebula 1.5 km both.

**Docking computer:** `SG_MAX_VERTEXDIST_DOCKCOMPUTER` is **5000 m installed, 4000 m stock** ("distance to nearest
vertex of station"). It is read only through the generic KC global native (`0x00460630` → `0x00439ff0`
at `0x00464bb1`); the EXE does not convert it **[m]**, and the KC conversion was not traced. Other metre
globals: COMM 50 / 25 km, BEAMING 9 / 5 km, BUILDCOMPLEX 99.999 / 20 km, tractor break 1332 m, spring 777 m,
speed limit 80 m/s. Mayhem guide (`mayhem4-guide/index.html`): OWPs engage within 5 km, the data scan needs
9 km, defend-position covers a 10 km radius, missiles reach 30–110 km **[m: guide text]**.

## 9. Q6 — formation, escort, autopilot and avoidance constants

**Globals**: no formation, escort or avoidance `SG_` value exists in the 155 installed / 140 stock entries
**[m]**. The steering-related ones (installed / stock):
* `SG_MAXSTRAFEFACTOR_SMALLSHIP` 24576 / 49152 and `_BIGSHIP` 13107 / 26214 (16.16 → 0.375 / 0.75 and
  0.2 / 0.4 of max speed **[i]**);
* `SG_MAXGLIDESPEED` 50 / 100;
* `SG_AVRSPEED` 42500 (85 m/s at 500 units per m/s **[i]**) in both;
* `SG_CURSORSTEERING_DAMPING` 30 and `_MAXFIREANGLE` 30° in both.

**KC `L/x3story.obj`** (`kc_nav_names.py`; stock `addon/04.cat`, installed override `addon/07.cat`; 264 nav-related
methods installed):
* **Formation distance = f(GetSize)**, identical in stock and installed **[m]**. There are four overrides of
  `GetFormationDist`:
  * the base ship class: `4 × GetSize()`, where opcode `0x50` is integer multiply (`0x004a3387` → `0x004a9100`);
  * one class with boarding/capture methods: `SE_MulFix(GetSize, 1.125)`;
  * one class: `SE_MulFix(self.GetSize, 1.375)`;
  * one class: `4 × self.GetSize`.
* `GetSize` = `SA_GetTotalSize(instance)`. With no instance it falls back to a per-class default of 200000,
  1000, 10000, 50000, 30000 or 20000 units **[m]**. That `SA_GetTotalSize` returns the §2 radius in engine units
  is **[i]**. So the base rule spaces followers about 4 collision radii apart: M5 37 m, M3 47–60 m, M6 258 m,
  TS 154 m **[i]**.
* Methods present: `GetFormationDist`, `GetFormationDistToLeader` (`SE_Distance3` to the leader), `SetFormationPos`,
  `BuildFormation`, `ReorgFormation`, `GetMaxFormationSpeed`, `__EscortObject`, `StartFollowLeader`,
  `CollisionWarn`/`NotifyCollisionWarn`, `IsAutopilotActive`, `SetAutopilot`, `GetWantedDistance`/`SetWantedDistance`.
* Natives, with call sites installed: `SA_AvoidObjectCollision` 14, `SA_CollisionObject` 14,
  `SA_GetCollisionObjects` 4, `SA_SetMaxAvoidShield` 1, `SA_FollowObject` 6, `SA_EscortObject` 3,
  `SA_SetDesiredDistance` 2, `SA_SetSpeedLimit` 26, `SA_SetSectorSize` 2.

**Engine-side constants already read by [ship-movement.md](ship-movement.md)** (§8–§10 there), converted here:
* Escort slots are offset vectors rotated by the leader's yaw and pitch; the ship steers at the slot at full
  speed beyond `10 × f+0x260`, and the speed floor is 5000 units/s (10 m/s).
* The docking final zone is at `2·[obj+0xa4] + 25000` units, i.e. twice the radius plus **50 m**. In it the
  ship glides kinematically at 1–40 m/s and lands at 1500 units (3 m).
* The docking approach speed cap is 75,000 / 50,000 units/s (150 / 100 m/s) by class.
* Native avoidance (command 0x12) re-plans every 200 ms with obstacle radii inflated by 1.2.

Behaviour is traced in [ship-navigation-ai.md](ship-navigation-ai.md). Mayhem's fleet templates (Line, Delta,
X, Pyramid, Dragon Claw, The Shield, Big Ship Escort; guide §16) are script-side and not decoded here.

## 10. Q7 — proposals for the design agent [p]

These are proposals, not measurements; `recommend.py` evaluates them on named installed ships (`recommend.txt`).
* **Turn radius:** `R = v/ω`, with v the current speed (or `vmax`) and ω the slower of the yaw and pitch rates,
  taken from the engine getters at run time (§1) so that tunings are included.
* **Clearance:** `C = r_own + r_obs + m`, where `m = max(25 m, 0.5·r_own)`. Use the §2 sphere only to select
  candidates. For stations, refine with the part AABBs (`render-node-bounds.md`) before steering, because a
  bounding sphere of 1.5–52 km would make station avoidance absurd.
* **Escape distances:** a constant-rate turn of radius R clears a sphere of radius C centred at distance D ahead
  iff `√(R² + D²) ≥ R + C`, so `D_turn = √(C(C + 2R))`. The braking distance is `D_stop = v²/2a`.
* **Lookahead:** `L = clamp(v·t_r + k·min(D_turn, D_stop), C + r_own, 30 km)`, with `t_r` = 0.5 s (reaction plus
  angular ramp) and `k` = 1.25 (ramp margin). As a time horizon this is `L/v`.
* **Arrival radius:** `max(2·r_own, R/4)`, where R/4 is the turn radius at a quarter-speed final approach.
* **Formation spacing:** `r_lead + r_follow + max(50 m, 0.5·(r_lead + r_follow))`. The engine's own base rule is
  `4·GetSize`, which is tighter for fighters and much looser for capitals.

| ship (installed) | v m/s | ω deg/s | r m | R m | stop m | L vs same class (time) | L vs 1.5 km station | L vs 60 m asteroid | arrival m | spacing m (engine 4r) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| M5 Discoverer | 532 | 147.7 | 9.2 | 206 | 2573 | **442** (0.8 s) | 2426 | 539 | 52 | 68 (37) |
| M3 Nova | 247.5 | 92.8 | 11.9 | 153 | 766 | **288** (1.2 s) | 1549 | 371 | 38 | 74 (47) |
| M6 Centaur | 180 | 33.8 | 64.4 | 306 | 810 | **531** (2.9 s) | 1661 | 523 | 129 | 193 (258) |
| TS Mercury | 124 | 21.1 | 38.5 | 337 | 256 | **382** (3.1 s) | 1602 | 382 | 84 | 127 (154) |
| M2 Boreas | 70 | 13.5 | 705 | 297 | 163 | **2469** (35 s) | 3264 | 1824 | 1411 | 2116 (2822) |

Reasoning:
* For the M5, M3 and M6, `D_turn` (130–350 m) is far shorter than `D_stop` (0.77–2.6 km), so avoidance should
  steer around obstacles rather than brake. For the TS the two are equal (281 vs 256 m), so either works. A lookahead of about 1–3 s of flight suffices against ships and asteroids, and
  about 5–13 s against stations.
* For the M2 and other capitals the floor `C + r_own` dominates: the ship is wider than its turning circle and
  slow, so the lookahead is a size-driven distance (about 2.5–3.3 km) rather than a time.
* Arrival radii below 2·r_own cause orbiting. The Mayhem M5's R of 206 m at full speed means it should slow to
  about 130 m/s inside about 1 km of a point target to stay within a 50 m arrival radius.

## 11. Q8 — unknown or inferred, and what a runtime log would settle

* **[i] Body value = L∞ half-extent, and the scene-rule radius for ships.** It is exact for one station (§2);
  ships and the scene rule beyond that one case are unverified. A one-shot log of `(class, subtype, [obj+0xa4],
  [obj+0x70]+0x30..)` per object in the player's sector would settle every class at once.
* **[i] Whether `SA_GetTotalSize` returns `[obj+0xa4]`.** This decides the formation-spacing reading of §9; a
  breakpoint log on the native, or one `GetFormationDist` return value, would settle it.
* **[i] Deceleration equals the table acceleration, and the angular ramp.** Owned by
  [ship-movement.md](ship-movement.md). Logging `[obj+0x10]` and `[obj+0x24..0x2c]` against time through a full
  stop and a 180° turn of one fighter and one capital would give the true stop distance and turn time.
* **[i] Tuning bonuses.** The per-tuning factor in `ext+0x270/0x274` was not decoded; log the getter outputs
  (`0x00450b80`, `0x00450d60`) for a tuned player ship.
* **[i] Mayhem station spacing.** Needs a runtime census of class 5/6 positions per sector (or a save parser).
  The stock map is the stand-in.
* **[i] Asteroid break-up scene attachment** (radius 60 m vs 130 m for type H): settled by the same `[obj+0xa4]`
  log.
* **Gate trigger distance:** not in the tables. The collision path (`0x0044d4c0` "CanWarp") is the place to read it.
* **Laser ranges** use base speed and lifetime; the KC `SA_GetLaserTypeRange` native was not tied to
  `0x00433e90` by address **[i]**.

## 12. Reproduce

```sh
cd verification/results/game-scale
python3 exe_units.py        # EXE unit sites, 17 checks (capstone)
python3 ships.py            # ships_by_class.{txt,json}; ShipStats validation
python3 radius_crosscheck.py
python3 map_stats.py        # map_stats.{txt,json}
python3 weapons.py          # weapons.{txt,json}
python3 kc_nav_names.py > kc_nav_names.txt
python3 recommend.py        # recommend.{txt,json}
```

The scripts read `/mnt/c/X3` through `tools/analysis/sector_fog_census.Assets` (stock view
`STOCK_AP_CATALOGUES` = root 01–13 + `addon/01–04`) and `bob1.resolve_body`. Run time is about 40 s each for
`ships.py` and `map_stats.py`. Ghidra was used only read-only (project `~/x3-ghidra-research`, serialised by
`flock /tmp/x3-ghidra.lock`); decompiler text stays in `~/x3-nav-study/scale/`.
