# Installed AP sector fog census — 2026-09-20

Read-only installed-file analysis of bottle **X3**. No game, Wine, build, install or rendering change. The [analysis script](../../tools/analysis/sector_fog_census.py) produces a local metadata JSON and the complete [239-sector CSV](sector-fog-census.csv). Engine interpretation belongs in [sector-fog.md](sector-fog.md), especially §12. This is the shipped universe map, not the current savegame or script-modified runtime table.

## Coverage and provenance

**239 sectors, 83 background records, 43 used background records, 73 distinct record family names (39 used), 35 sectors with positive cloud-card counts.** All 239 English sector names resolve when page 7 is overlaid by the 300007, 350007 and 380007 versions, resolving text references and removing parenthesized developer comments for the display name. Earlier counts of 224 resolved names / 51 families were incomplete. The CSV retains file names/comments separately.

Numbered catalogue precedence: base `01.cat` through `13.cat`, then `addon/01.cat` through `addon/04.cat`; paths and decoded member hashes are recorded in JSON. CAT member extents are validated against DAT length before reading. Archive resource keys use forward slashes on every host. AP `addon/` resources retain priority over base-game counterparts, including loose TC files. Explicit AP-selected `--mod` archives follow the stock archives in argument order; their unprefixed known resources (`maps`, `types`, `t`, `objects`, `dds`) are rebased into the AP namespace, so an unprefixed selected-mod `types/TBackgrounds.pck` overrides the stock addon table. Loose addon files override AP archives/mods; loose base files affect only the base fallback namespace. Loose resources are checked after archives; there are **no relevant loose map, TBackgrounds, cloud-body, texture, or English 0001 localization overrides**. The 22 loose `0002` language files are outside the name lookup. No installed mod catalogues were found. Explicit selected mods can be supplied with `--mod`; the script does not infer launcher/registry mod selection.

| Input | Winner | Decoded SHA-256 |
| --- | --- | --- |
| Map | `addon/02.cat` → `addon/maps/x3_universe.pck` | `567c125e42c73608cb4519fee749092faeb7ab6d021770624b5250e762ba3118` |
| TBackgrounds | `addon/03.cat` → `addon/types/TBackgrounds.pck` | `2c4c3a94550b364b0c3bc70e350688025654cb706473e4f30a6e6c394f4cd733` |

Localization entries are merged from 14 base/addon English archive members; latest is `addon/04.cat` → `addon/t/0001-L044.pck`. Cloud bodies and resolved textures in this census all win from `01.cat`; per-member decoded hashes are in the local JSON.

## Units and independent mechanisms

`500 native units = 1 metre`; divide native distances by **500,000** for kilometres. This converts the map sector `size` scalar and FogNear/FogFar only. The sector `size` scalar is not a measured volume or established diameter. Do not confuse the captured camera scaling factor 0.01 with a native-to-metre conversion. Automatic body size and exported mesh coordinates remain raw asset quantities, not physical cloud widths.

Cloud-card population: `0: 204`, `8: 6`, `10: 4`, `15: 2`, `16: 22`, `50: 1` sectors. All 239 sectors independently configure stardust at **100%**, yielding target `(100 << 9) / 100 = 512` streak objects. Nebula and star body overrides are zero everywhere. All 83 records have valid ordered near/far pairs; no loader clamp is triggered. FogNear is below sector size in 51 sectors; this comparison is a data flag, not proof of a volumetric medium or exact visible-space coverage.

## Density evidence and anomalies

- **Argon Prime** `(1,3)`: background 2 `bluewell`, 8 cards, body size 45,000, sector size 45 km, fade 36–37 km. **Atreus' Clouds** `(3,2)`: background 14 `foggreenoutlands`, 16 cards, body size 55,000, sector size 50 km, fade 6–7 km. Both use equal weights over six bodies and the same exported quad extent 199,998 × 198,978. Their textures differ.
- A geometric count × size² comparison gives `2 × (55/45)² = 2.987654` for Atreus versus Argon, rather than count-only 2. This is **not optical density**: texture RGB, view overlap, position-dependent alpha, screen blending, and runtime size reuse affect the result. The user's 0.01 / 0.05 settings remain artistic targets; the census does not derive a factor of five.
- D8/D16 use interleaved fixed lattices; D>16 repeats slot indices modulo 16 (§12). **Veil of Delusion** `(16,16)` is the sole mapped D50 case (record 52 `fogdeepred`, body size 45,000). Its sector size is 40 km, but fade starts at 200 km and ends at 250 km. Neither long fade distance nor large count is interchangeable with physical haze density. The file also contains unused D21/D50 `fogkhaak` and D50 `khaakhive` records.
- **Uranus** and **Uranus 3** use D15, much smaller body size 20,508, and unequal weights. Slots 7/8 carry positive weights but no same-family part07/08 files exist; the record/asset distinction is explicit below. Configured counts are not measured draw counts.
- Unused `xtmgreenring` record 45 has D16 but no family dust bodies. Most zero-count records still contain nonzero rates; this does not create clouds. Nine positive-count records are unused by this map; the table lists all of them.
- This evidence supports a finite set of **data profiles**, not a calibrated clear/light/dense physical classification. Zero cards only establishes absence of this card population, not absence of a painted nebula or distance fade.

## Cloud body and material facts

269 dust body files were found across 45 of the 73 referenced families: 233 have automatic size 45,000, 18 size 20,508, 12 size 55,000, and 6 size 75,000. All have four vertices and one `nebulafog.fx` material. 251 have exported extent 199,998 × 198,978 × 0; 18 have 199,999 × 198,978 × 0. All materials specify `g_AlphaValue=1`, `g_AlphaBlendEnable=1`, `g_SrcBlend=2`, `g_DestBlend=4`, while **251 materials specify no alpha texture (`0`) and 18 reference a nonzero alpha texture**. The 18 exceptions are six parts each from `uranus`, `uranus2`, and `uranus3`; all reference `environments\\nebulae\\redfire\\dust\\nebula_redfire_dust_part_01_alpha8.tga`. No matching `dds/nebula_redfire_dust_part_01_alpha8` packed/DDS/TGA member or loose file resolves in this install, so its availability is **missing**, with no bytes to hash. The JSON separately records diffuse and alpha reference status and, when resolved, source/member, decoded SHA-256 and DDS metadata. Missing reference does not establish what the runtime binds or which sampler the shader uses. These are material defaults, not runtime opacity: the dust pass overrides state and drives node alpha (§10/§12). Screen blending uses RGB; DXT1 or material alpha 1 does not establish constant transmittance. No texture-average-to-density conversion is made.

All 11 mapped positive-card families have six body files. Texture names below are DDS member stems under `dds/`, stored packed as `.pck`, all DXT1. The script retains original material paths and DDS dimensions/mipmap header fields.

| Active family | Sectors | D | Automatic size | Texture stem | Pixels |
| --- | ---: | --- | ---: | --- | --- |
| bluewell | 3 | 8 | 45000 | `nebula_bluewell_dust_diff` | 512×512 |
| fogbluedistance | 3 | 8 | 45000 | `nebula_fogbluedistance_background_diff` | 2048×2048 |
| fogcyancorner | 4 | 10 | 45000 | `nebula_fogcyancorner_background_diff` | 2048×2048 |
| fogdeepred | 5 | 16,50 | 45000 | `nebula_fogdeepred_background_diff` | 2048×2048 |
| foggreeneye | 2 | 16 | 55000 | `nebula_foggreeneye_background_diff` | 2048×2048 |
| foggreenoutlands | 4 | 16 | 55000 | `nebula_foggreenoutlands_background_diff` | 2048×2048 |
| fogparanid | 6 | 16 | 45000 | `nebula_fogparanid_background_dust_diff` | 1024×1024 |
| fogred | 4 | 16 | 45000 | `nebula_fogred_background_diff` | 2048×2048 |
| uranus | 1 | 15 | 20508 | `uranus_dust_noise_diff` | 1024×1024 |
| uranus3 | 1 | 15 | 20508 | `uranus_dust_noise_diff` | 1024×1024 |
| whitenexus | 2 | 16 | 45000 | `nebula_whitenexus_dust_diff` | 1024×1024 |

Inactive-family body sets (`standardblack`, `void`, `oos_v1`–`oos_v5`: seven families / 48 bodies) refer to absent diffuse texture members; those references are retained as missing, not replaced by invented opacity. `supercluster` has five dust body files rather than six. These do not affect a mapped positive-card family.

## Rate profiles

Rates are selection weights, not percentages or density coefficients. The engine builds a CDF using their sum. Letters below are report keys only.

| Key | Rates for body slots 1…8 | Sum | Sector uses |
| --- | --- | ---: | ---: |
| A | 15,10,5,4,8,1,7,6 | 56 | 3 |
| B | 25,21,6,30,14,23,23,25 | 167 | 0 |
| C | 25,25,20,21,20,15,0,0 | 126 | 4 |
| D | 25,25,25,25,25,25,0,0 | 150 | 33 |
| E | 25,25,25,25,25,25,25,25 | 200 | 0 |
| F | 25,50,10,0,0,0,0,0 | 85 | 0 |
| G | 25,50,10,5,5,0,0,0 | 95 | 0 |
| H | 25,50,10,5,5,0,5,0 | 100 | 189 |
| I | 25,50,25,0,0,0,0,0 | 100 | 10 |

## All 83 background definitions

`N/F` and sector-size values below are km; raw integers are retained in CSV/JSON. `Missing weighted slots` means no matching family dust part file; engine fallback/zero-body handling is not inferred. `—` means none. Cloud automatic size is an asset scalar.

| Index | Family | Sector uses | D | Rates | N/F km | Body size | Missing weighted slots |
| ---: | --- | ---: | ---: | --- | --- | --- | --- |
| 0 | bluedistance | 8 | 0 | H | 50/60 | 75000 | 7 |
| 1 | bluenexus | 7 | 0 | H | 22/34 | 45000 | 7 |
| 2 | bluewell | 3 | 8 | D | 36/37 | 45000 | — |
| 3 | brennanstriumph | 3 | 0 | H | 15/20 | 45000 | 7 |
| 4 | burninghorizon | 5 | 0 | H | 100/110 | 45000 | 7 |
| 5 | cyancorner | 10 | 0 | H | 50/60 | 45000 | 7 |
| 6 | darkhorizon | 5 | 0 | H | 45/50 | 45000 | 7 |
| 7 | deepblue | 5 | 0 | H | 50/60 | 45000 | 7 |
| 8 | deepred | 11 | 0 | H | 45/50 | 45000 | 7 |
| 9 | fogblue | 0 | 14 | G | 6/8 | 45000 | — |
| 10 | fogbluedistance | 2 | 8 | D | 37/47 | 45000 | — |
| 11 | fogcyancorner | 4 | 10 | D | 31/39 | 45000 | — |
| 12 | fogdeepred | 3 | 16 | D | 40/60 | 45000 | — |
| 13 | foggreeneye | 2 | 16 | D | 7/10 | 55000 | — |
| 14 | foggreenoutlands | 4 | 16 | D | 6/7 | 55000 | — |
| 15 | fogkhaak | 0 | 21 | B | 45/100 | 45000 | 7,8 |
| 16 | fogparanid | 6 | 16 | D | 23/25 | 45000 | — |
| 17 | fogred | 4 | 16 | D | 17.4/20.4 | 45000 | — |
| 18 | greeneye | 8 | 0 | H | 100/110 | 45000 | 7 |
| 19 | greenoutlands | 8 | 0 | H | 50/60 | 45000 | 7 |
| 20 | greenspot | 2 | 0 | H | 100/110 | 45000 | 7 |
| 21 | greenvoid | 5 | 0 | H | 50/60 | 45000 | 7 |
| 22 | midnightblue | 10 | 0 | I | 90/110 | 45000 | — |
| 23 | oppositered | 6 | 0 | H | 50/60 | 45000 | 7 |
| 24 | purpleoutlands | 4 | 0 | H | 100/110 | 45000 | 7 |
| 25 | redborderland | 4 | 0 | H | 50/60 | 45000 | 7 |
| 26 | reddawn | 5 | 0 | H | 45/50 | 45000 | 7 |
| 27 | redfire | 4 | 0 | C | 45/50 | 45000 | — |
| 28 | rednexus | 7 | 0 | H | 45/50 | 45000 | 7 |
| 29 | redreef | 4 | 0 | H | 50/60 | 45000 | 7 |
| 30 | redsplit | 6 | 0 | H | 50/60 | 45000 | 7 |
| 31 | standardblack | 47 | 0 | H | 100/110 | 45000 | 7 |
| 32 | void | 1 | 0 | H | 200/250 | 45000 | 7 |
| 33 | whitenexus | 2 | 16 | D | 34/35 | 45000 | — |
| 34 | void | 0 | 0 | F | 400/420 | 45000 | — |
| 35 | xtmgreenmyst | 0 | 0 | H | 38/45 | — | 1,2,3,4,5,7 |
| 36 | xtmbrownyellow | 0 | 0 | H | 38/45 | — | 1,2,3,4,5,7 |
| 37 | xtmredyellowplasma | 0 | 0 | H | 38/45 | — | 1,2,3,4,5,7 |
| 38 | xtmredrift | 0 | 0 | H | 38/45 | — | 1,2,3,4,5,7 |
| 39 | xtmiceplasma | 0 | 0 | H | 38/45 | — | 1,2,3,4,5,7 |
| 40 | xtmblueplasma | 0 | 0 | H | 38/45 | — | 1,2,3,4,5,7 |
| 41 | xtmsaturn | 0 | 0 | H | 38/45 | — | 1,2,3,4,5,7 |
| 42 | xtmbluenexusring1 | 0 | 0 | H | 38/45 | — | 1,2,3,4,5,7 |
| 43 | xtmbrownnexusring1 | 0 | 0 | H | 38/45 | — | 1,2,3,4,5,7 |
| 44 | xtmtwinrings1 | 0 | 0 | H | 22/34 | — | 1,2,3,4,5,7 |
| 45 | xtmgreenring | 0 | 16 | D | 35/45 | — | 1,2,3,4,5,6 |
| 46 | xtmpurplesupernova | 0 | 0 | H | 100/110 | — | 1,2,3,4,5,7 |
| 47 | xtmgreenbluewisp | 0 | 0 | H | 50/60 | — | 1,2,3,4,5,7 |
| 48 | fogred | 0 | 6 | D | 17/34.4 | 45000 | — |
| 49 | fogbluedistance | 1 | 8 | D | 4.7/7.7 | 45000 | — |
| 50 | foggreenoutlands | 0 | 6 | D | 9.1/41 | 55000 | — |
| 51 | foggreeneye | 0 | 12 | D | 8.8/42 | 55000 | — |
| 52 | fogdeepred | 1 | 50 | D | 200/250 | 45000 | — |
| 53 | xtmpluto | 0 | 0 | H | 100/110 | — | 1,2,3,4,5,7 |
| 54 | xtmneptune | 0 | 0 | H | 100/110 | — | 1,2,3,4,5,7 |
| 55 | xtmuranus | 0 | 0 | H | 100/110 | — | 1,2,3,4,5,7 |
| 56 | xtmjupiter | 0 | 0 | H | 100/110 | — | 1,2,3,4,5,7 |
| 57 | xtmmars | 0 | 0 | H | 100/110 | — | 1,2,3,4,5,7 |
| 58 | xtmvenus | 0 | 0 | H | 100/110 | — | 1,2,3,4,5,7 |
| 59 | xtmmercury | 0 | 0 | H | 100/110 | — | 1,2,3,4,5,7 |
| 60 | xtmearth | 0 | 0 | H | 100/110 | — | 1,2,3,4,5,7 |
| 61 | xtmmoon | 0 | 0 | H | 100/110 | — | 1,2,3,4,5,7 |
| 62 | orangenexus | 3 | 0 | H | 50/60 | — | 1,2,3,4,5,7 |
| 63 | bluenova | 1 | 0 | H | 50/60 | — | 1,2,3,4,5,7 |
| 64 | supercluster | 1 | 0 | H | 50/60 | 45000 | 4,7 |
| 65 | orangenexus | 2 | 0 | H | 200/200 | — | 1,2,3,4,5,7 |
| 66 | standardblack | 0 | 0 | H | 1000/1000 | 45000 | 7 |
| 67 | solarsystem | 21 | 0 | H | 1000/1000 | — | 1,2,3,4,5,7 |
| 68 | fogkhaak | 0 | 50 | B | 45/100 | 45000 | 7,8 |
| 69 | oos_v1 | 0 | 0 | H | 1000/1000 | 45000 | 7 |
| 70 | oos_v2 | 0 | 0 | H | 1000/1000 | 45000 | 7 |
| 71 | oos_v3 | 0 | 0 | H | 1000/1000 | 45000 | 7 |
| 72 | oos_v4 | 0 | 0 | H | 1000/1000 | 45000 | 7 |
| 73 | oos_v5 | 0 | 0 | H | 1000/1000 | 45000 | 7 |
| 74 | fogdeepred | 1 | 16 | D | 1000/1000 | 45000 | — |
| 75 | khaakhive | 0 | 50 | E | 40/60 | 45000 | 7,8 |
| 76 | uranus | 1 | 15 | A | 1000/1000 | 20508 | 7,8 |
| 77 | earth | 0 | 15 | A | 1000/1000 | 45000 | 7,8 |
| 78 | uranus2 | 1 | 0 | A | 1000/1000 | 20508 | 7,8 |
| 79 | uranus3 | 1 | 15 | A | 1000/1000 | 20508 | 7,8 |
| 80 | orangeblackhole | 0 | 0 | H | 200/200 | — | 1,2,3,4,5,7 |
| 81 | yellowrift | 0 | 0 | H | 200/200 | — | 1,2,3,4,5,7 |
| 82 | closeplanet | 0 | 0 | H | 200/200 | — | 1,2,3,4,5,7 |

## Every mapped sector

Each row joins to its complete background definition above; the CSV additionally carries native values, all eight rates, separate stardust fields, name provenance text, body sizes, missing weighted slots, and alpha-texture availability/missing references/resolved hashes. Names that remain “Unknown Sector” are explicit localized names, not lookup failures.

| Grid x,y | Sector | Background index/family | Sector size km | D | N/F km |
| --- | --- | --- | ---: | ---: | --- |
| 0,0 | Kingdom End | 19 / greenoutlands | 28 | 0 | 50/60 |
| 1,0 | Rolk's Drift | 19 / greenoutlands | 25 | 0 | 50/60 |
| 2,0 | Queen's Space | 14 / foggreenoutlands | 45 | 16 | 6/7 |
| 3,0 | Menelaus' Frontier | 19 / greenoutlands | 50 | 0 | 50/60 |
| 4,0 | Ceo's Buckzoid | 21 / greenvoid | 35 | 0 | 50/60 |
| 5,0 | Teladi Gain | 21 / greenvoid | 45 | 0 | 50/60 |
| 6,0 | Family Whi | 21 / greenvoid | 40 | 0 | 50/60 |
| 8,0 | Kuiper Belt | 67 / solarsystem | 40 | 0 | 1000/1000 |
| 10,0 | Uranus 2 | 78 / uranus2 | 60 | 0 | 1000/1000 |
| 11,0 | Titan | 67 / solarsystem | 40 | 0 | 1000/1000 |
| 13,0 | The Vault | 22 / midnightblue | 35 | 0 | 90/110 |
| 16,0 | Unknown Sector | 11 / fogcyancorner | 20 | 10 | 31/39 |
| 17,0 | Xenon Sector 534 | 0 / bluedistance | 54 | 0 | 50/60 |
| 19,0 | Xenon Sector 596 | 26 / reddawn | 90 | 0 | 45/50 |
| 0,1 | Three Worlds | 31 / standardblack | 35 | 0 | 100/110 |
| 1,1 | Power Circle | 31 / standardblack | 34 | 0 | 100/110 |
| 2,1 | Antigone Memorial | 31 / standardblack | 45 | 0 | 100/110 |
| 3,1 | Rolk's Fate | 14 / foggreenoutlands | 37 | 16 | 6/7 |
| 4,1 | Profit Share | 31 / standardblack | 40 | 0 | 100/110 |
| 5,1 | Seizewell | 7 / deepblue | 35 | 0 | 50/60 |
| 6,1 | Family Zein | 31 / standardblack | 45 | 0 | 100/110 |
| 7,1 | Oort Cloud | 67 / solarsystem | 40 | 0 | 1000/1000 |
| 8,1 | Pluto | 67 / solarsystem | 60 | 0 | 1000/1000 |
| 9,1 | Neptune | 67 / solarsystem | 50 | 0 | 1000/1000 |
| 10,1 | Uranus | 76 / uranus | 50 | 15 | 1000/1000 |
| 11,1 | Saturn | 67 / solarsystem | 75 | 0 | 1000/1000 |
| 12,1 | Mercury | 67 / solarsystem | 60 | 0 | 1000/1000 |
| 13,1 | Shareholder's Fortune | 31 / standardblack | 55 | 0 | 100/110 |
| 14,1 | Mines of Fortune | 26 / reddawn | 40 | 0 | 45/50 |
| 15,1 | Saturn 2 | 67 / solarsystem | 40 | 0 | 1000/1000 |
| 16,1 | Saturn 3 | 67 / solarsystem | 40 | 0 | 1000/1000 |
| 17,1 | Getsu Fune | 10 / fogbluedistance | 63 | 8 | 37/47 |
| 18,1 | Menelaus' Paradise | 10 / fogbluedistance | 45 | 8 | 37/47 |
| 19,1 | Xenon Sector 597 | 3 / brennanstriumph | 100 | 0 | 15/20 |
| 20,1 | Guiding Star | 6 / darkhorizon | 55 | 0 | 45/50 |
| 0,2 | Cloudbase North West | 2 / bluewell | 33 | 8 | 36/37 |
| 1,2 | Herron's Nebula | 2 / bluewell | 50 | 8 | 36/37 |
| 2,2 | The Hole | 14 / foggreenoutlands | 55 | 16 | 6/7 |
| 3,2 | Atreus' Clouds | 14 / foggreenoutlands | 50 | 16 | 6/7 |
| 4,2 | Spaceweed Drift | 7 / deepblue | 70 | 0 | 50/60 |
| 5,2 | Greater Profit | 7 / deepblue | 28 | 0 | 50/60 |
| 6,2 | Thuruk's Pride | 31 / standardblack | 45 | 0 | 100/110 |
| 7,2 | Family Pride | 31 / standardblack | 25 | 0 | 100/110 |
| 8,2 | Rhonkar's Might | 5 / cyancorner | 60 | 0 | 50/60 |
| 9,2 | Patriarch's Retreat | 31 / standardblack | 90 | 0 | 100/110 |
| 10,2 | Uranus 3 | 79 / uranus3 | 60 | 15 | 1000/1000 |
| 11,2 | Jupiter | 67 / solarsystem | 60 | 0 | 1000/1000 |
| 12,2 | Venus | 67 / solarsystem | 60 | 0 | 1000/1000 |
| 13,2 | The Moon | 67 / solarsystem | 60 | 0 | 1000/1000 |
| 14,2 | Home of Opportunity | 26 / reddawn | 30 | 0 | 45/50 |
| 15,2 | Jupiter 2 | 67 / solarsystem | 40 | 0 | 1000/1000 |
| 16,2 | Jupiter 3 | 67 / solarsystem | 40 | 0 | 1000/1000 |
| 18,2 | Bluish Snout | 0 / bluedistance | 32 | 0 | 50/60 |
| 19,2 | Spires of Elusion | 0 / bluedistance | 55 | 0 | 50/60 |
| 20,2 | CEO's Wellspring | 29 / redreef | 55 | 0 | 50/60 |
| 21,2 | Cathedral of Xaar | 30 / redsplit | 60 | 0 | 50/60 |
| 0,3 | Ringo Moon | 31 / standardblack | 40 | 0 | 100/110 |
| 1,3 | Argon Prime | 2 / bluewell | 45 | 8 | 36/37 |
| 2,3 | The Wall | 31 / standardblack | 40 | 0 | 100/110 |
| 3,3 | Farnham's Legend | 31 / standardblack | 30 | 0 | 100/110 |
| 4,3 | Bala Gi's Joy | 30 / redsplit | 45 | 0 | 50/60 |
| 5,3 | Blue Profit | 31 / standardblack | 50 | 0 | 100/110 |
| 6,3 | Rhonkar's Fire | 27 / redfire | 35 | 0 | 45/50 |
| 7,3 | Rhonkar's Clouds | 27 / redfire | 40 | 0 | 45/50 |
| 8,3 | Tharka's Sun | 31 / standardblack | 33 | 0 | 100/110 |
| 9,3 | Cho's Defeat | 31 / standardblack | 30 | 0 | 100/110 |
| 11,3 | Asteroid Belt | 67 / solarsystem | 60 | 0 | 1000/1000 |
| 12,3 | Mars | 67 / solarsystem | 50 | 0 | 1000/1000 |
| 13,3 | Earth | 67 / solarsystem | 90 | 0 | 1000/1000 |
| 14,3 | Family Tkr | 26 / reddawn | 40 | 0 | 45/50 |
| 15,3 | Tkr's Deprivation | 26 / reddawn | 50 | 0 | 45/50 |
| 16,3 | Ghinn's Escape | 31 / standardblack | 23 | 0 | 100/110 |
| 17,3 | Hila's Joy | 4 / burninghorizon | 40 | 0 | 100/110 |
| 18,3 | Ocean of Fantasy | 31 / standardblack | 35 | 0 | 100/110 |
| 20,3 | Twisted Skies | 27 / redfire | 60 | 0 | 45/50 |
| 0,4 | Red Light | 23 / oppositered | 32 | 0 | 50/60 |
| 1,4 | Home of Light | 23 / oppositered | 25 | 0 | 50/60 |
| 2,4 | President's End | 8 / deepred | 30 | 0 | 45/50 |
| 3,4 | Elena's Fortune | 31 / standardblack | 40 | 0 | 100/110 |
| 4,4 | Olmancketslat's Treaty | 30 / redsplit | 33 | 0 | 50/60 |
| 5,4 | Ceo's Sprite | 24 / purpleoutlands | 60 | 0 | 100/110 |
| 6,4 | Family Rhonkar | 27 / redfire | 60 | 0 | 45/50 |
| 7,4 | Unknown Sector | 67 / solarsystem | 30 | 0 | 1000/1000 |
| 8,4 | Unknown Sector | 67 / solarsystem | 30 | 0 | 1000/1000 |
| 9,4 | Patriarch's Keep | 22 / midnightblue | 25 | 0 | 90/110 |
| 10,4 | Two Grand | 22 / midnightblue | 70 | 0 | 90/110 |
| 13,4 | Heretic's End | 8 / deepred | 30 | 0 | 45/50 |
| 14,4 | Harmony of Perpetuity | 8 / deepred | 55 | 0 | 45/50 |
| 16,4 | Family Njy | 25 / redborderland | 35 | 0 | 50/60 |
| 0,5 | Cloudbase South West | 23 / oppositered | 20 | 0 | 50/60 |
| 1,5 | Ore Belt | 33 / whitenexus | 30 | 16 | 34/35 |
| 2,5 | Cloudbase South East | 33 / whitenexus | 20 | 16 | 34/35 |
| 3,5 | Split Fire | 4 / burninghorizon | 29 | 0 | 100/110 |
| 4,5 | Brennan's Triumph | 3 / brennanstriumph | 25 | 0 | 15/20 |
| 5,5 | Company Pride | 24 / purpleoutlands | 28 | 0 | 100/110 |
| 6,5 | Thuruk's Beard | 5 / cyancorner | 70 | 0 | 50/60 |
| 7,5 | Unknown Sector | 67 / solarsystem | 30 | 0 | 1000/1000 |
| 8,5 | Unknown Sector | 67 / solarsystem | 30 | 0 | 1000/1000 |
| 10,5 | Profit Center Alpha | 22 / midnightblue | 55 | 0 | 90/110 |
| 11,5 | PTNI Headquarters | 22 / midnightblue | 27 | 0 | 90/110 |
| 12,5 | Void of Opportunity | 22 / midnightblue | 40 | 0 | 90/110 |
| 13,5 | Circle of Labour | 0 / bluedistance | 40 | 0 | 50/60 |
| 14,5 | Elysium of Light | 25 / redborderland | 20 | 0 | 50/60 |
| 15,5 | Xenon Sector 472 | 22 / midnightblue | 30 | 0 | 90/110 |
| 16,5 | Thyn's Abyss | 25 / redborderland | 36 | 0 | 50/60 |
| 17,5 | Albion Delta | 19 / greenoutlands | 40 | 0 | 50/60 |
| 19,5 | Zyarth's Dominion | 31 / standardblack | 100 | 0 | 100/110 |
| 20,5 | Zyarth's Stand | 31 / standardblack | 45 | 0 | 100/110 |
| 21,5 | Xenon Sector 695 | 31 / standardblack | 20 | 0 | 100/110 |
| 0,6 | Emperor Mines | 31 / standardblack | 33 | 0 | 100/110 |
| 1,6 | Paranid Prime | 16 / fogparanid | 20 | 16 | 23/25 |
| 2,6 | Priest Rings | 16 / fogparanid | 22 | 16 | 23/25 |
| 3,6 | Priest's Pity | 31 / standardblack | 45 | 0 | 100/110 |
| 4,6 | Danna's Chance | 24 / purpleoutlands | 20 | 0 | 100/110 |
| 5,6 | Nopileos' Memorial | 62 / orangenexus | 33 | 0 | 50/60 |
| 6,6 | Hatikvah's Faith | 24 / purpleoutlands | 35 | 0 | 100/110 |
| 7,6 | Aladna Hill | 31 / standardblack | 25 | 0 | 100/110 |
| 8,6 | Akeela's Beacon | 28 / rednexus | 85 | 0 | 45/50 |
| 11,6 | Scale Plate Green | 31 / standardblack | 29 | 0 | 100/110 |
| 12,6 | Nyana's Hideout | 31 / standardblack | 50 | 0 | 100/110 |
| 13,6 | Omicron Lyrae | 18 / greeneye | 28 | 0 | 100/110 |
| 14,6 | Treasure Chest | 29 / redreef | 46 | 0 | 50/60 |
| 15,6 | Black Hole Sun | 28 / rednexus | 32 | 0 | 45/50 |
| 16,6 | Albion Alpha | 19 / greenoutlands | 50 | 0 | 50/60 |
| 17,6 | Albion Beta | 19 / greenoutlands | 50 | 0 | 50/60 |
| 18,6 | Albion Gamma | 19 / greenoutlands | 50 | 0 | 50/60 |
| 19,6 | Xenon Sector 598 | 31 / standardblack | 40 | 0 | 100/110 |
| 20,6 | Xenon Sector 627 | 30 / redsplit | 40 | 0 | 50/60 |
| 21,6 | Xenon Core 023 | 30 / redsplit | 40 | 0 | 50/60 |
| 0,7 | Savage Spur | 31 / standardblack | 45 | 0 | 100/110 |
| 1,7 | Empire's Edge | 16 / fogparanid | 20 | 16 | 23/25 |
| 2,7 | Duke's Domain | 16 / fogparanid | 20 | 16 | 23/25 |
| 3,7 | Emperor's Ridge | 31 / standardblack | 30 | 0 | 100/110 |
| 4,7 | Freedom's Reach | 31 / standardblack | 30 | 0 | 100/110 |
| 5,7 | Xenon Sector 101 | 30 / redsplit | 35 | 0 | 50/60 |
| 7,7 | Light of Heart | 31 / standardblack | 60 | 0 | 100/110 |
| 8,7 | Legend's Home | 64 / supercluster | 35 | 0 | 50/60 |
| 9,7 | Unknown Sector | 28 / rednexus | 55 | 0 | 45/50 |
| 10,7 | Eighteen Billion | 20 / greenspot | 35 | 0 | 100/110 |
| 11,7 | Xenon Sector 347 | 4 / burninghorizon | 40 | 0 | 100/110 |
| 14,7 | Enduring Light | 18 / greeneye | 60 | 0 | 100/110 |
| 15,7 | Nathan's Voyage | 28 / rednexus | 34 | 0 | 45/50 |
| 16,7 | Wastelands | 31 / standardblack | 35 | 0 | 100/110 |
| 17,7 | Midnight Star | 31 / standardblack | 60 | 0 | 100/110 |
| 18,7 | Belt of Aguilar | 31 / standardblack | 36 | 0 | 100/110 |
| 19,7 | Grand Exchange | 18 / greeneye | 45 | 0 | 100/110 |
| 20,7 | Tears of Greed | 0 / bluedistance | 40 | 0 | 50/60 |
| 0,8 | Ocracoke's Storm | 1 / bluenexus | 20 | 0 | 22/34 |
| 1,8 | Preacher's Void | 31 / standardblack | 30 | 0 | 100/110 |
| 3,8 | Pontifex' Realm | 31 / standardblack | 40 | 0 | 100/110 |
| 5,8 | Light Water | 11 / fogcyancorner | 40 | 10 | 31/39 |
| 7,8 | Montalaar | 6 / darkhorizon | 33 | 0 | 45/50 |
| 8,8 | Avarice | 6 / darkhorizon | 75 | 0 | 45/50 |
| 9,8 | New Income | 6 / darkhorizon | 49 | 0 | 45/50 |
| 10,8 | Ianamus Zura | 31 / standardblack | 35 | 0 | 100/110 |
| 11,8 | Homily of Perpetuity | 0 / bluedistance | 40 | 0 | 50/60 |
| 13,8 | Unknown Sector | 74 / fogdeepred | 20 | 16 | 1000/1000 |
| 14,8 | Argon Sector M148 | 21 / greenvoid | 40 | 0 | 50/60 |
| 16,8 | Interworlds | 31 / standardblack | 30 | 0 | 100/110 |
| 19,8 | Merchant Haven | 18 / greeneye | 29 | 0 | 100/110 |
| 0,9 | Senator's Badlands | 1 / bluenexus | 45 | 0 | 22/34 |
| 1,9 | Duke's Vision | 1 / bluenexus | 50 | 0 | 22/34 |
| 2,9 | Emperor's Wisdom | 31 / standardblack | 80 | 0 | 100/110 |
| 3,9 | Trinity Sanctum | 5 / cyancorner | 30 | 0 | 50/60 |
| 4,9 | Preacher's Refuge | 5 / cyancorner | 33 | 0 | 50/60 |
| 5,9 | Shore of Infinity | 11 / fogcyancorner | 29 | 10 | 31/39 |
| 6,9 | Lucky Planets | 11 / fogcyancorner | 30 | 10 | 31/39 |
| 7,9 | Rolk's Legacy | 5 / cyancorner | 45 | 0 | 50/60 |
| 8,9 | Great Trench | 5 / cyancorner | 42 | 0 | 50/60 |
| 9,9 | Ceo's Doubt | 5 / cyancorner | 30 | 0 | 50/60 |
| 11,9 | Acquisition Repository | 16 / fogparanid | 40 | 16 | 23/25 |
| 15,9 | Althes | 22 / midnightblue | 35 | 0 | 90/110 |
| 17,9 | Gaian Star | 28 / rednexus | 40 | 0 | 45/50 |
| 18,9 | Mercenaries' Rift | 13 / foggreeneye | 45 | 16 | 7/10 |
| 19,9 | Maelstrom | 13 / foggreeneye | 80 | 16 | 7/10 |
| 0,10 | Weaver's Tempest | 6 / darkhorizon | 50 | 0 | 45/50 |
| 3,10 | Bad Debt | 31 / standardblack | 28 | 0 | 100/110 |
| 7,10 | Gunne's Crusade | 62 / orangenexus | 40 | 0 | 50/60 |
| 9,10 | LooManckStrat's Legacy | 32 / void | 20 | 0 | 200/250 |
| 11,10 | Spaceweed Grove | 16 / fogparanid | 40 | 16 | 23/25 |
| 13,10 | Aldrin | 65 / orangenexus | 300 | 0 | 200/200 |
| 14,10 | Aldrin 2 | 65 / orangenexus | 30 | 0 | 200/200 |
| 15,10 | Megnir | 8 / deepred | 55 | 0 | 45/50 |
| 16,10 | Segaris | 18 / greeneye | 65 | 0 | 100/110 |
| 17,10 | Xenon Sector | 18 / greeneye | 45 | 0 | 100/110 |
| 18,10 | Lost Order | 7 / deepblue | 45 | 0 | 50/60 |
| 1,11 | Rhy's Crusade | 31 / standardblack | 31 | 0 | 100/110 |
| 2,11 | Rhy's Desire | 29 / redreef | 29 | 0 | 50/60 |
| 3,11 | Ministry of Finance | 29 / redreef | 75 | 0 | 50/60 |
| 4,11 | Rhonkar's Trial | 23 / oppositered | 40 | 0 | 50/60 |
| 5,11 | Unknown Sector | 20 / greenspot | 60 | 0 | 100/110 |
| 6,11 | Clarity's End | 1 / bluenexus | 40 | 0 | 22/34 |
| 7,11 | Third Redemption | 18 / greeneye | 40 | 0 | 100/110 |
| 8,11 | Perdition's End | 23 / oppositered | 40 | 0 | 50/60 |
| 9,11 | Mi Ton's Refuge | 31 / standardblack | 90 | 0 | 100/110 |
| 17,11 | Unknown Sector | 25 / redborderland | 60 | 0 | 50/60 |
| 2,12 | Family Rhy | 31 / standardblack | 34 | 0 | 100/110 |
| 3,12 | Wretched Skies | 22 / midnightblue | 40 | 0 | 90/110 |
| 4,12 | The Shallows | 23 / oppositered | 40 | 0 | 50/60 |
| 8,12 | Desecrated Skies | 8 / deepred | 40 | 0 | 45/50 |
| 9,12 | Moo-Kye's Revenge | 8 / deepred | 34 | 0 | 45/50 |
| 12,12 | Perpetual Sin | 31 / standardblack | 40 | 0 | 100/110 |
| 18,12 | Bright Profit | 3 / brennanstriumph | 40 | 0 | 15/20 |
| 0,13 | Depths of Silence | 22 / midnightblue | 40 | 0 | 90/110 |
| 1,13 | Dark Waters | 17 / fogred | 55 | 16 | 17.4/20.4 |
| 2,13 | Reservoir of Tranquillity | 17 / fogred | 25 | 16 | 17.4/20.4 |
| 3,13 | Barren Shores | 31 / standardblack | 50 | 0 | 100/110 |
| 8,13 | Priest Refuge | 12 / fogdeepred | 75 | 16 | 40/60 |
| 9,13 | Cardinal's Domain | 12 / fogdeepred | 36 | 16 | 40/60 |
| 10,13 | Sacred Relic | 8 / deepred | 65 | 0 | 45/50 |
| 17,13 | Sanctity of Corruption | 5 / cyancorner | 40 | 0 | 50/60 |
| 18,13 | Company Strength | 5 / cyancorner | 40 | 0 | 50/60 |
| 1,14 | Shining Currents | 0 / bluedistance | 68 | 0 | 50/60 |
| 2,14 | Great Reef | 17 / fogred | 30 | 16 | 17.4/20.4 |
| 8,14 | Spring of Belief | 12 / fogdeepred | 40 | 16 | 40/60 |
| 9,14 | Friar's Retreat | 8 / deepred | 29 | 0 | 45/50 |
| 10,14 | Pontifex' Seclusion | 8 / deepred | 30 | 0 | 45/50 |
| 11,14 | Duke's Citadel | 4 / burninghorizon | 20 | 0 | 100/110 |
| 17,14 | Queen's Retribution | 18 / greeneye | 40 | 0 | 100/110 |
| 18,14 | Hollow Infinity | 1 / bluenexus | 40 | 0 | 22/34 |
| 2,15 | Mists of Elysium | 17 / fogred | 100 | 16 | 17.4/20.4 |
| 3,15 | Quiet Tides | 5 / cyancorner | 40 | 0 | 50/60 |
| 7,15 | Emperor's Pride | 1 / bluenexus | 20 | 0 | 22/34 |
| 8,15 | Unholy Descent | 8 / deepred | 40 | 0 | 45/50 |
| 9,15 | Consecrated Fire | 31 / standardblack | 57 | 0 | 100/110 |
| 10,15 | Heaven's Assertion | 63 / bluenova | 40 | 0 | 50/60 |
| 14,15 | Patriarch's Conclusion | 62 / orangenexus | 40 | 0 | 50/60 |
| 15,15 | Contorted Dominion | 28 / rednexus | 40 | 0 | 45/50 |
| 16,15 | Faded Dreams | 8 / deepred | 40 | 0 | 45/50 |
| 17,15 | Queen's Harbour | 19 / greenoutlands | 40 | 0 | 50/60 |
| 18,15 | Menelaus' Oasis | 21 / greenvoid | 40 | 0 | 50/60 |
| 2,16 | Vestibule of Creation | 49 / fogbluedistance | 20 | 8 | 4.7/7.7 |
| 7,16 | Unseen Domain | 1 / bluenexus | 100 | 0 | 22/34 |
| 8,16 | Unknown Sector | 31 / standardblack | 70 | 0 | 100/110 |
| 10,16 | Lasting Vengeance | 7 / deepblue | 40 | 0 | 50/60 |
| 15,16 | Thyn's Excavation | 4 / burninghorizon | 40 | 0 | 100/110 |
| 16,16 | Veil of Delusion | 52 / fogdeepred | 40 | 50 | 200/250 |
| 18,16 | Distant Clouds | 28 / rednexus | 40 | 0 | 45/50 |
| 23,19 | Unknown Sector | 0 / bluedistance | 20 | 0 | 50/60 |

## Reproduce and validation

```sh
python3 tools/analysis/sector_fog_census.py --self-test
python3 tools/analysis/sector_fog_census.py \
  "$HOME/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3" \
  --json /tmp/x3-sector-fog-census.json --verify-stock \
  --csv docs/reverse-engineering/sector-fog-census.csv
```

Focused self-test passes 29 checks under both ordinary Python and `python3 -O`: gzip/XOR packing, body extent and TBackgrounds parsing, CAT/DAT rejection, host-independent POSIX resource keys, numbered/addon/selected-mod/loose precedence, localization references, resolved alpha texture metadata/hash, missing versus absent alpha references, output path collisions and invalid stock rejection. Validation uses explicit exceptions, so Python optimization cannot disable it. JSON and CSV destinations must be distinct files and cannot overwrite any explicitly supplied mod CAT or paired DAT input. The tool's explicit installed-data checks verify 239 unique grid rows, 83 complete records, 35 positive-card sectors, all resolved names, representative Argon/Atreus/Kingdom End values, D50, ordered fog ranges, and zero overrides. A separate focused comparison verified every serialized CSV field against the generated JSON for all 239 rows; that comparison is evidence from this run, not part of `--verify-stock`. No full suite or Wine run is relevant to this offline script.

Limitations: this census does not measure runtime mutation, savegame sectors, rendered card coverage, sampled texture opacity, or native Windows rendering. Loose packed/unpacked format conflicts with distinct suffixes are rejected as ambiguous rather than guessed. No extracted game bytes or raw disassembly are tracked.
