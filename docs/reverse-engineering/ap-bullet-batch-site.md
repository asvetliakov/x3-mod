# AP bullet batch site: the twin of the FL batch scope stub, the AP bullet programs and the body census

Bounded offline contract, 2026-11-13, for porting the FL bullet re-shade and muzzle re-shade
([fl/bullet-draw-batch.md](fl/bullet-draw-batch.md) sections 5 to 7, [fl/muzzle-flash-draw.md](fl/muzzle-flash-draw.md),
[muzzle-flash-look.md](../architecture/muzzle-flash-look.md)) to AP. Inputs: `X3AP.exe` SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` (`/mnt/c/X3/X3AP.exe`, the Mayhem install; the
same bytes as `/mnt/c/X3AP-orig/X3AP.exe`), `X3FL.exe` SHA-256 `e7f3aad8…dfbb8` for the FL side, and three catalogue
views: **AP stock** (`/mnt/c/X3`, `01..13.cat` + `addon/01..04.cat`, `STOCK_AP_CATALOGUES`), **AP Mayhem** (every
catalogue `/mnt/c/X3` mounts: 13 root + 15 `addon`) and **FL** (`/mnt/c/X3FL`, root + `addon2`). **AP addresses are
VAs** (the EXE is not relocatable); FL addresses are RVAs. **[s]** static instruction or data evidence from the
producer, **[i]** inference, **[u]** not established. No production edit, no build, no Wine, no game.

**Answer.**

1. The AP twin of the FL batch call `0x097af8 → 0x0e0b40` is **`0x0046d0a7 → 0x004bfd40`**, in the separate flush
   `0x0046d080`. It is a cdecl call with four stack words: scene, batch, material, view. The caller pops `0x10`.
   No register carries an argument. The site is a valid claim. The FL stub's *idea* transfers: a
   forwarding call that publishes the batch's body id `[batch+0x0c]` and raises `x3m_bullet_scope` around the submit.
   Its **shape does not transfer one to one**. The body id comes from stack word 2 (`[esp+8]` at stub entry) instead of
   EDX. The stub must re-push four words under a 12-byte pad instead of two under a 4-byte pad. The call straddles a
   qword, so it is claimed by the install-window plain copy, not by `lock cmpxchg8b`. The window, its FNV-1a 32 and
   every VA are different.
2. All three INSTANCE_BULLETS pixel programs, their three vertex programs, both lone vertex programs and the
   standard_lighting DEFAULT **vertex** program `494fe349…` are byte-identical in AP stock, AP Mayhem and FL. Mayhem
   overrides no `shader/` entry. The standard_lighting DEFAULT **pixel** programs differ: FL `ced7edef…` / `0d5848c8…`
   are absent from both AP views. AP uses `7c83ed50c9894e44` (1298 dwords) and `e70adc744a38ca59` (1324 dwords, 2s).
3. The AP node body id is **node `+0x140`**. The class-0 factory loads TBullets word `+0`, and the body bind
   `0x00487e30` stores it at `0x00487e8d`. The batch keys on it, and the material-submit scope node `+0x140` is
   read at `0x004c5008`. Stock AP has FL's TBullets and FL's muzzle chain: 45 rows, which differ from FL only in
   row 9's speed and lifetime, and the same scenes and bodies 445/444/446/447/450/314/10463/12009 (+582). **Mayhem
   differs.** Its TBullets has 38 rows on 32 bodies, 37 of them effects-material and one classic (row 25 PBC, body
   525). Its muzzle rows use 445/444/446/314 plus seven Mayhem bodies, 12011..12018 from `addon/06.cat`, that the
   current muzzle list does not name.

## Reproduce

```sh
python3 -I verification/results/ap-bullet-batch-site/ap_bullet_batch_site.py \
  > verification/results/ap-bullet-batch-site/ap_bullet_batch_site_out.txt     # about 26 s
```

[Producer](../../verification/results/ap-bullet-batch-site/ap_bullet_batch_site.py) →
[`_out.txt`](../../verification/results/ap-bullet-batch-site/ap_bullet_batch_site_out.txt). It uses the
`fl_weapon_effect_sites.py` Image reader (PE, Capstone, `.reloc`, both hashes checked). It asserts **105 AP and 5 FL
whole-instruction anchors with 0 failures**, then runs **16 checks, all ok**. It prints both call-site windows
(bytes, FNV-1a 32, relocations, qword geometry, branch targets, image dwords, callers). It scans the `shader/*.fb`
entries of the three views for the program hashes, using the proxy's FNV-1a 64 from
`shader_coverage_offline._program`, last mount wins. It then runs the TBullets census
(`tools/effects/weapon_effects_census.census`) and the muzzle census (the `fl_muzzle_flash_census.py` readers) on
both AP views. Two runs are byte-identical. The figures below come from that output (**[s]**) unless tagged
otherwise. `tools/effects/weapon_effects_census.py` was re-run as well, and its three untracked
`verification/results/weapon-effects-census/*.json` came out byte-identical to the 2026-11-05 files.

## 1. The batch call site and the stub contract (question 1)

**Flush `0x0046d080`** (AP's form of FL's inlined `0x097ad9..0x097b0e`): stdcall (scene, view), `ret 8`. It has one
E8 caller, `0x0047eb7a`, at the end of the view submit `0x0047e920`. The view submit has 5 E8 callers (`0x00472295`,
`0x004722a8`, `0x00472484`, `0x0047e8e0`, `0x0047e8e9`), which matches FL's five.

| VA | Instruction | Role |
| --- | --- | --- |
| `0x0046d080` | `push ecx` | 4-byte local, popped back into ECX at `0x0046d0c1` |
| `0x0046d082` / `0x0046d087` | `mov ebx,[esp+0x10]` / `mov ebp,[esp+0x10]` | **EBX = view**, **EBP = scene** |
| `0x0046d08d` | `mov edi,[ebp+0x40]` | **EDI = batch** (the scene's batch list) |
| `0x0046d095` | `test byte [edi+0x10],2` | batch collected in this call |
| `0x0046d09b` | `mov esi,[edi+0x30]` | **ESI = material** (the batch's texture list) |
| `0x0046d0a3..0x0046d0a6` | `push ebx; push esi; push edi; push ebp` | view, material, batch, scene |
| **`0x0046d0a7`** | **`call 0x004bfd40`** (`e8 94 2c 05 00`) | one submit per (batch, texture) |
| `0x0046d0ac` / `0x0046d0ae` | `mov esi,[esi]` / `add esp,0x10` | next material; the caller pops four words |
| `0x0046d0b4` / `0x0046d0b6` | `jne 0x0046d0a3` / `mov edi,[edi]` | loop; next batch |

**Submit `0x004bfd40`**: cdecl. At entry `[esp+4]` = scene, `[esp+8]` = batch, `[esp+0xc]` = material, `[esp+0x10]` =
view. The prologue `sub esp,0x11c` is followed by four pushes, so these are `[esp+0x130..0x13c]` inside. It returns
with a plain `ret`, EAX = 1 (`0x004c00a4`) or 0 (`0x004c00b7`). It has 1 E8 caller, `0x0046d0a7`, and **no SEH
frame** (no `fs:` access in `0x004bfd40..0x004c00c1`; FL's `0x0e0b40` installs one). Its steps are FL's:

* it rejects any null argument (`0x004bfd4b..0x004bfd7c`);
* VB record = material `+0x18`; Lock DISCARD (`0x004bfdd9`), memcpy, Unlock;
* `"effects"` unless batch `+0x1c & 0x10` (`0x004bfe1d`);
* INSTANCE_BULLETS when batch `+0x18 & 0x20000000` (`0x004bfed9`, string `0x005633dc`);
* `DrawPrimitive` through device vtable `+0x144` (`0x004c007c`, `call eax` at `0x004c008a`, TRIANGLELIST, count / 3).

No instruction before its first register write (`0x004bfdb0`) names EAX, ECX or EDX, so **no register is an input**
[s]. The routine uses x87 (`fld`/`fstp` at `0x004bfe8f..0x004bfe9e`, `0x004bff99`/`0x004bffa4`) and a string
instruction (`rep movsd` at `0x004bffcb`, `0x004bfffa`). A stub must therefore leave the x87 stack empty and DF clear
[s, i for the requirement].

**Batch record and body id.** The lookup `0x0046ca80` takes EAX = scene and `[esp+4]` = node and ends with `ret 4`.
It walks scene `+0x40` comparing **batch `+0x0c` with node `+0x140`** (`0x0046ca96`, `0x0046caa0`). Otherwise it
allocates `0x3c` bytes and writes:

* batch `+0x0c` = node `+0x140` (`0x0046cb35`/`0x0046cb3b`);
* `+0x14` = node `+0x12c`, `+0x18` = node `+0x130`, `+0x1c` = body `+0x50` (`0x0046cb3e..0x0046cb89`).

These are FL's records with node `+0x144` → `+0x140`. Capacity (`0x200`/`0x400`, `0x0046cf5f`/`0x0046cf76`), the
`0x78f0` budget (`0x0046ced0`) and the body `0xe1` exclusion (`0x0046ce58`) are FL's too [s]. The `0xe1` test is in
the AP admission `0x0046ce20`, which corrects the "instancing exclusion: none" row of the FL note's Q3 table.

**Hook-site suitability of `0x0046d0a7`.**

| Check | Finding |
| --- | --- |
| instruction boundary | whole 5-byte call. Window `0x0046d0a3..0x0046d0b6` (19 bytes, call offset 4), bytes `53 56 57 55 e8 94 2c 05 00 8b 36 83 c4 10 83 3e 00 75 ed`, FNV-1a 32 **`0x949efb9a`**, no relocated dword |
| claim | `0x0046d0a7 & 7 = 7`: **the call straddles the qword `0x0046d0a8`**, so `engine_patch::write_code` takes the plain copy. That is safe only inside the install window, which closes at the first Present, before any device or flush exists. This is the same argument as the AP tap `0x0045ac7d` (`weapon_effects_bolt_sites.h`) [i] |
| branches and pointers | the only direct branch into the window targets its first byte `0x0046d0a3` (from `0x0046d0b4`); none lands in the call bytes 1..4; no image dword names them |
| callers | the target has 1 E8 caller; no other AP hook names `0x0046d0a7`, `0x004bfd40` or `0x0046d080` (source grep) |
| live across the call | EBX = view, ESI = material, EDI = batch, EBP = scene. All four are used after the return (`0x0046d0ac`, `0x0046d0b6`, the next iteration's pushes), and `0x004bfd40` saves and restores them |
| dead after return | EAX: no reader in the flush. The flush returns it through `0x0047eb7f..0x0047eb83`, and no caller of `0x0047e920` reads EAX in its first instruction. ECX, EDX: no reader, and ECX is reloaded by `pop ecx`. EFLAGS: `add esp,0x10` rewrites them [s]. A forwarding stub returns the callee's EAX anyway |
| stack | caller-cleaned (`add esp,0x10`) |
| preserve | EBX/ESI/EDI/EBP, LastError, DF, the x87 stack; no XMM is live across the call [i] |
| reentrancy | render thread only. `0x004bfd40` calls the VB, the effect, the device and the engine helpers `0x00515810` (memcpy), `0x00469700`, `0x004bb0f0`, `0x004f52e0`, `0x004b9ed0`, `0x004c63b0`, `0x004faf00`. None of them reaches `0x0047e920` [i, not traced past one level] |
| SEH | the callee has none. An exception unwinding through the stub leaves the scope counter raised, which the FL design already handles: Present zeroes it while armed |
| rate | per (selected batch, texture) per `0x0047e920` call [i: same loop as FL] |

**FL constant → AP twin** (`weapon_effects_sites_fl.h` site 7 `batch`, `batch_body`, the stub in
`weapon_effects_hook.cpp`; the FL batch site has no frame witness, only its window).

| FL constant | FL value | AP twin |
| --- | --- | --- |
| window start / length | RVA `0x097af0` / 23 | VA **`0x0046d0a3`** / **19** |
| call offset / call | 8 / RVA `0x097af8` | **4** / **`0x0046d0a7`** |
| return / window end | `0x097afd` / `0x097b07` | **`0x0046d0ac`** / **`0x0046d0b6`** |
| target (`x3m_weapon_effects_continue_batch`) | base + `0x0e0b40` | **`0x004bfd40`** (absolute) |
| window FNV-1a 32 | `0xf86f890b` (reproduced) | **`0x949efb9a`** |
| relocation fixup | `no_fixup` | none (fixed-base EXE) |
| `in_one_qword` | true (`lock cmpxchg8b`) | **false** (plain copy, install window) |
| ABI static_assert bytes | `[4..7]` `8b d3 57 56` (mov edx,ebx; push edi; push esi), `[15..17]` `83 c4 08` | `[0..3]` `53 56 57 55` (push ebx/esi/edi/ebp), `[9..10]` `8b 36`, `[11..13]` `83 c4 10` |
| callee inputs | ECX = scene, EDX = batch, `[esp+4]` material, `[esp+8]` view | `[esp+4]` scene, `[esp+8]` batch, `[esp+0xc]` material, `[esp+0x10]` view; no register |
| `batch_body` | `0x0c` from EDX | **`0x0c` from `[esp+8]`** (the batch record is the same layout) |
| stub body-id read | `mov eax,[edx+0x0c]` after `pushfd; push eax` | `mov eax,[esp+0x10]` (the batch, after `pushfd; push eax`); `mov eax,[eax+0x0c]` |
| stub re-push | `lea esp,[esp-4]`, 2 × `push dword ptr [esp+12]`, `lea esp,[esp+12]` | `lea esp,[esp-12]`, 4 × `push dword ptr [esp+0x1c]`, `lea esp,[esp+0x1c]`. The callee's ESP is entry − 32, the same modulo 16 as the original call [i: arithmetic] |
| caller's pop | 8 | `0x10` |
| bullet node body id | node `+0x144` | node `+0x140` (section 3) |
| TBullets table slot / stride | base + `0x1a83c8` / `0xdb8` | `0x00606fb8` (`core::Globals::bullet_table_slot`, read at `0x00440195`) / `0xdb8` |
| view submit / its callers | `0x0978e0` / 5 | `0x0047e920` / 5 |
| flush | inlined | `0x0046d080`, caller `0x0047eb7a` |

The FL stub's flag discipline (`pushfd`/`popfd` around the counters, `lea` for ESP), its EAX save and its
no-call/no-XMM/no-string-instruction rule all carry over unchanged [i]. ECX and EDX need no preserving on AP, but
keeping them untouched costs nothing.

## 2. The programs in the AP catalogues (question 2)

Only `01.cat` and `addon/01.cat` carry `shader/` entries (384 and 196 per profile directory 1_1..3_0). Neither the
Mayhem `addon/05..15.cat` nor any loose `shader/` directory adds or replaces one. So AP stock and AP Mayhem resolve the
same 2352 `.fb` entries, and every row below is identical in the two AP views [s].

| Hash (FNV-1a 64) | Role | AP stock / Mayhem | FL |
| --- | --- | --- | --- |
| `ec1f5c4a2f4e1445` | INSTANCE_BULLETS PS (`effects`, `effects2s`, `engine`, `engine2s`), 49 dwords | 24 rows, every dir 1_1..3_0, `addon/01.cat` | 24 rows, `addon2/01.cat` |
| `d4a26efb7c603931` | INSTANCE_BULLETS PS (`effects_0000/1`) | 12 rows, `01.cat` | 12 rows |
| `84d3de8887c963c5` | INSTANCE_BULLETS PS (`engine_0000/1`) | 12 rows, `01.cat` | 12 rows |
| `5e484a06672e28fb` / `21a2c13be7f989c3` / `1b6863a088a177af` | the three INSTANCE_BULLETS VS, 101 dwords | 30 / 12 / 24 rows | same |
| `d5e1c75351ed3f04` | lone VS (effects DEFAULT), 253 dwords | 16 rows, 2_0..3_0 | same |
| `89193868c61c3846` | lone VS (effects INSTANCE), 221 dwords | 16 rows | same |
| `8360f422de08b5bd` | effects DEFAULT / INSTANCE PS, 1108 dwords | 28 rows | same |
| `494fe349b8bc12ec` | standard_lighting DEFAULT VS (`muzzle_reshade_core.h` `vertex_shader`), 526 dwords | 8 effects, 3_0 | same |
| `ced7edef48059e84` / `0d5848c8489b6630` | FL standard_lighting / 2s DEFAULT PS, 1418 / 1450 dwords | **absent** | 1 / 1 |
| `7c83ed50c9894e44` / `e70adc744a38ca59` | AP standard_lighting / 2s DEFAULT PS, 1298 / 1324 dwords | 1 / 1 (3_0) | absent |

So `bullet_reshade_core.h` `bullet_programs` and `lone_vertex_shaders`, and the muzzle route's VS gate, match AP
bytes without change. Whatever profile directory the AP device selects, the INSTANCE_BULLETS hashes are the same in
every one. The muzzle route checks the VS only, never the PS, so the different AP standard_lighting PS does not affect
recognition. Our program replaces that PS and reads only the VS's interpolators, which are the same VS bytes [i]. Any
code or test that names the FL PS hashes `ced7edef…` / `0d5848c8…` does not hold on AP. That the live AP device
receives these bytes at CreatePixelShader is not measured [u]; a flight's `bullet_reshade_program` row would show it.

## 3. The node body id and the AP bullet / muzzle census (question 3)

**Node `+0x140` is the body id** on AP, in the same evidence shape as FL's `0x049b70` → `0x0a35e0` → `0x0a365d`:

* **creation** (object factory `0x0043ffa0`, class-0 case):
  * `0x0044018e` sets the object flags `0x40004801`, as FL's `0x049b64` does.
  * `0x00440195` loads the TBullets table `[0x00606fb8]`, and `0x0044014e` computes `imul 0xdb8`.
  * `0x0044019b` `movzx edx,word [eax+ecx]` reads TBullets `+0`, and `0x004401a6` stores it to `[esp+0x10]`.
  * After the class switch (`0x004410aa`, `0x004410f0 cmp ax,bx`), `0x004410f9` allocates the node.
  * `0x004410fe` `movsx ecx,word [esp+0x10]`, then `0x00441107` `call 0x00487e30`.
* **body bind `0x00487e30`** (EAX = node, `[esp+4]` = body id, `ret 4`): `0x00487e8d` `mov [esi+0x140],ebx`; node
  scale = LOD 0 `+0x14` at `0x00487e98`/`0x00487e9b`. This settles the beam note's [i] on the AP `+0x14` store.
* **batch**: batch `+0x0c` = node `+0x140`, and the lookup keys on it (section 1).
* **material-submit scope**: `0x004c4fc0` reads its node argument `[esp+0x40]` `+0x140` as a body id (`0x004c5004`,
  `0x004c5008` → body lookup `0x004863c0`; again at `0x004c51c5`/`0x004c51c9`). It passes the same slot as argument
  2 of `0x004c0150` (`0x004c5206`/`0x004c5213` or `0x004c5217`/`0x004c5226`, call `0x004c5228`). That call is the one
  `object_trace` claims; it publishes `args[1]` as the node and reads `node[0x140/4]` as `model`
  (`src/proxy/object_trace.cpp`). So on AP the lone and muzzle routes' body test is the scope's `model`, or a read of
  node `+0x140`, which is `weapon_effects_core.h` `layout::node_body` for AP [s code; i that every TBullets draw
  reaches `0x004c0150` through `0x004c5228`, as on FL].

**TBullets.**

| View | Table | Rows / distinct bodies | Body material kinds | Non-effect rows |
| --- | --- | --- | --- | --- |
| FL | `addon2/04.cat` | 45 / 30 | 23 effect, 13 classic, 9 material3 | 22, 14 of them beam-flagged (col 12 & `0x200e`) |
| AP stock | `addon/02.cat` `be762076…` | 45 / 30 | identical to FL | identical to FL |
| AP Mayhem | `addon/12.cat` `7b4ab0a6…` | **38 / 32** | **37 effect, 1 material3** | **1**: row 25 PBC, body 525, beam |

Stock AP and FL TBullets differ only in row 9 (PD): speed 187,500 against 177,500 and lifetime 2,100 against
1,800 ms. Body, flags and the flight/impact/muzzle columns are equal on all 45 rows. The 22 non-effect rows on both
are 2 MASS 10436, 7 IonD 300, 8 PBE 10458, 10 EMP 517, 15/16 flak 10458, 17 PAL 10385, 18 PSG 225, 19 TerAF 300,
26/27 Tri/Fusion 10385, 28 Mining 10678, 29 Tug 525, 30 Dummy1 10459, 31/32 Dummy2 / Kyon α 525, 33 Kyon β 526,
34 Kyon γ 527, 40 TerAF frag 225, 41 EMP_1 517, 43 TerAF_1 300 and 44 IPG frag 10459. The FL classic body list of
`classic_bullet_bodies.py` (10436, 300, 10458, 10385, 225, 517, 525, 526, 527, 10459, 10678) is therefore the stock
AP list unchanged; 518, the PPC, is an effects body. The brief's "14 classic rows" equals the 14 beam-flagged
non-effect rows [i: how that figure was defined].

28 of Mayhem's 38 rows name one of 22 bodies absent from the stock TBullets. Of the 38 rows' body files, 24
resolve from `addon/06.cat`, 2 from `addon/10.cat`, 3 from `addon/12.cat` and 9 from `01.cat`
(`bullet_ebc`, `bullet_pbe`, `bullet_apc`, `bullet_maml`, `bullet_mds`, `bullet_ire`, …). Their textures are mostly
new: `fx_bullet_PBE`, `fx_bullet_apc`, `fx_bullet_mds`, `fx_bullet_paa*`, `fbc`, `fx_bullets_diffx` and others.
Rows 11 CIG / 19 SSC (`bullet_empc`), 14 MAML (`bullet_push`) and 17 PALC (`bullet_psg`) are ONE / ONE; every other
effects body is ONE / INVSRCCOLOR (`weapon_effects_census.py` output, `mayhem3.json`).

**Muzzle chain** (TBullets col 21 → Effects row → CUT scene → part body).

| Scene | Body (material) | FL | AP stock | AP Mayhem |
| --- | --- | --- | --- | --- |
| 04251 | 445 (MAT3 tex 384, `DESTINATIONBLEND\|2SIDED`) | rows 22, 52, 157, 190, 195, 202 | identical | rows 105, 114, 127 |
| 04252 | 444 (MAT3 tex 376) | 24, 152, 191, 193 | identical | 106, 131 |
| 04253 | 446 (MAT3 tex 383) | 25, 52, 179, … (9 rows) | identical | 110, 114, 126, 134, 135, 137 |
| 04258 / 04260 | 447 / 450 | 185 / 33 | identical | not used |
| 04261 | 314 (MAT3 tex 384) | 34 | identical | 107 |
| 04274 | 582 (MAT3 tex 380) | 180 | identical | not used |
| 04437 | 10463 (MAT5 tex 380) | 49, 152 | identical | **overridden**: body **12014** (MAT5 tex 926), row 113 |
| 08001 | 12009 (MATERIAL6 `effects.fx`) | 183 | identical | **overridden**: body **12011** (MATERIAL6 `effects.fx`), row 103 |
| 08002 / 08004 / 08005 / 08006 / 08311 | — | — | — | **12013 / 12016 / 12017 / 12018 / 12012** (MAT3 tex 925 / 931 / 932 / 933 / 924) |
| 04265 | missing from the catalogues | 157 | identical | 126, 127, 135 |

The Mayhem scene bodies 12012..12018 are flagged `DESTINATIONBLEND|BESTQUALITY|AUTOFREE`, **without `2SIDED`**. They
therefore draw with the one-sided standard_lighting DEFAULT pair, VS `494fe349…` + PS `7c83ed50…` [i: the same
material-to-effect rule as FL's]. That VS passes the muzzle route's gate, and the ONE / ONE blend follows from
`DESTINATIONBLEND` [i]. Mayhem's Effects row 129 (muzzle of row 28 RG) is missing. On stock AP the
`muzzle_reshade_core.h` `bodies[]` list (445, 446, 444, 450, 447, 314, 10463) covers what it covers on FL. On Mayhem
it matches only 445, 444, 446 and 314. The flashes of IRE, PAC, MD, HEPT, PBE (12013), PRG/CFA/RL (12018),
EBC/FAA/AKE/BKE/GC (12016), PBG/ISR (12017), PALC/PAA (12012), PPC/IBL (12014 + 12013) and ID (12011, effects.fx,
like 12009) are absent from it. MAML, SSC and GRC (row 118) have no CUT element at all, only lens flares and a light.

## Consequences for an AP port (inferred, nothing implemented)

- The scope stub on AP needs: a VA site record with the window above and no fixup; the plain-copy claim inside the
  install window; a stub that reads the batch from `[esp+8]` and re-pushes four words; and the AP node body id
  `+0x140` (already `layout::node_body`) for the lone and muzzle tests.
- `x3m_bullet_body`'s map to UV layouts depends on the generated `tables.bullet_bodies` /
  `tables.bullet_textures`. On Mayhem most bodies and textures are not the FL ones. An unmapped body keeps the
  accepted look without the geometric size, and an unlisted texture keeps the 2.75 normaliser
  (`bullet_reshade_core.h`, `weapon_effects_patch.cpp` `bullet_body_layout`). The body map's row-count gate
  compares the live TBullets count (38 on Mayhem) with the table's.
- The muzzle list would need the Mayhem bodies above to cover Mayhem's flashes. Their texel statistics, which the
  per-body lit radius and hue need ([muzzle-flash-look.md](../architecture/muzzle-flash-look.md)), are not measured.

## Unknown

- Whether the live AP device receives the scanned program bytes at CreatePixelShader / CreateVertexShader: confirmed
  only by a flight's program rows [u].
- The callees of `0x004bfd40` beyond one level, for the reentrancy claim [i].
- The Mayhem scene bodies' draw path. They are assumed to take FL's classic route (`0x004c5228` → `0x004c0150`,
  standard_lighting DEFAULT). Their MAT3/MAT5 parse is from the census readers; 12011 and 12014 have no decoded
  vertices, like 12009 and 10463 [i].
- How often AP bullets take the batch path versus the lone path in a fight: the same rule as FL (count ≥ 2, budget,
  capacity) [i]. A flight's scope-call and lone counts would settle it.
