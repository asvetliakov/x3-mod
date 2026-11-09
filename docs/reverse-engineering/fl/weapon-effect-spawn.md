# FL weapon-effect call sites: impact, inner taps, bolts, muzzle, death

Bounded offline contract, 2026-11-08. Inputs: `X3FL.exe` SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8` (read at `/mnt/c/X3FL/X3FL.exe`; the
`/mnt/c/X3FL-orig` copy named in older notes no longer exists, the hash is the same) and `/mnt/c/X3AP-orig/X3AP.exe`
SHA-256 `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` for the twins. **Every FL address is an
RVA** (VA at the preferred base `0x400000` minus `0x400000`); a hook adds the running module base. AP addresses are VAs.
**[s]** measured static instruction or data evidence, **[i]** inference, **[u]** not established. No production edit,
no build, no Wine, no game. The FL weapon families stay compiled out until this contract is implemented and flown.

The AP sites and their frames are in [weapon-effect-spawn.md](../weapon-effect-spawn.md) sections 3, 4, 6, 8 and 10 and
in `src/proxy/weapon_effects_sites.h`, `weapon_effects_bolt_sites.h` and `weapon_effects_death_sites.h`. The FL spawner
ABI comes from [engine-effects.md](engine-effects.md) ("Verified 2026-11-02"), whose site A is a call of the same
spawner.

**Answer.** Every AP site has an FL counterpart that is a whole five-byte `E8 rel32` call with no direct branch and no
image dword landing in its bytes 1..4 [s]. The ABI changed at every site: FL passes the first one or two arguments in
ECX and EDX and keeps the rest on the stack, as engine-effects site A does. FL also merges the two AP death calls into
one site and inlines the AP muzzle wrapper `0x00413c00` into the fire routine, so the FL muzzle site is a call of the
generic spawner. The damage routine keeps its running total in EBP and leaves no absorbed flag in memory. The
atomic-write geometry reverses: six of the seven FL calls lie inside one aligned qword, and only the object tap
straddles one. The type records, the shield list, the bullet extension, the Effects row and element layouts and the
FADEOUTBODY fade are unchanged. The game clock moved to cfg `+0x708`, the node body id to `+0x144`, and every global
moved; the type-count array now sits below the table array. So the claim and window machinery carries over with RVA,
module base and relocation-masked witnesses, but every stub needs an FL entry shim (section 6, Q1).

## Reproduce

```sh
python3 -I verification/results/fl-weapon-effect-spawn/fl_weapon_effect_sites.py \
  > verification/results/fl-weapon-effect-spawn/fl_weapon_effect_sites_out.txt     # about 2.3 s
```

[Producer](../../../verification/results/fl-weapon-effect-spawn/fl_weapon_effect_sites.py) →
[`_out.txt`](../../../verification/results/fl-weapon-effect-spawn/fl_weapon_effect_sites_out.txt). It uses Capstone
over `tools/analysis/inspect_x3.py`'s PE reader and reads `.reloc`. It checks both input hashes and runs a linear
direct-edge census of `.text`: 72,230 edges on FL. It also scans every image dword. For each of the 7 windows it
prints the masked bytes, the relocations, the qword geometry, branch targets and callers, and it prints 9 frame
witnesses. It asserts **472 whole-instruction anchors on FL and 22 AP twins, 0 failures**, the type loader's class-0 dispatch to the TBullets case, and the E8 caller counts of the routines named below. Finally it runs a TBullets / TShips / Effects
census of the FL installation (`addon2/04.cat`). Output is deterministic: two runs are byte-identical. All figures below
are **[s]** from that output unless tagged otherwise. Raw listings and decompiler output stayed in the session
scratchpad.

## 1. Sites

FL is DYNAMIC_BASE (`DllCharacteristics 0x8140`, 38,327 HIGHLOW fixups). "One qword" means the five call bytes lie
inside one aligned qword, which allows the `lock cmpxchg8b` claim. The base is 64 KiB-aligned, so the RVA's alignment
is the live address's. A rel32 is never relocated, so relocated dwords appear only as absolute operands elsewhere in
a window.

| Site | AP call → callee | FL call RVA → callee RVA | FL window RVA, length | Relocated dwords in window | One qword (claim) | Branch targets inside the window (none in the call bytes) | E8 callers of the callee FL / AP |
| --- | --- | --- | --- | --- | --- | --- | --- |
| impact | `0x00449d85` → `0x00413c40` | `0x053390` → `0x014440` | `0x05338b`, 43 | none | **yes** (AP: no) | `0x05338b` (4 sources), `0x053398` (1), `0x0533ac` (4) | 1 / 1 |
| tap 1 | `0x00413da8` → `0x00414a70` | `0x014583` → `0x015270` | `0x014572`, 27 | none | yes | none | **4** / 3 |
| tap 2 | `0x00413e79` → `0x00414a70` | `0x014650` → `0x015270` | `0x01463f`, 27 | none | yes | none | 4 / 3 |
| flight | `0x004146c0` → `0x004148a0` | `0x014eaa` → `0x0150e0` | `0x014e95`, 29 | none | yes | none | 10 / 10 |
| object (per-bullet tap) | `0x0045ac7d` → `0x00414590` | `0x0645bd` → `0x014d90` | `0x0645ad`, 31 | `+1` → `0x1a82ec` | **no**: crosses `0x0645c0` | `0x0645ad` (12 sources: the loop join) | 1 / 1 |
| muzzle | `0x00446268` → `0x00413c00` | `0x04f639` → `0x0150e0` (**the spawner**) | `0x04f603`, 62 | `+1` → `0x1a83c8` | yes | none | **10** / 1 |
| death | `0x0044ac64` and `0x0044acb2` → `0x00413610` | `0x0543b3` → `0x013ed0` (**one site**) | `0x0543ac`, 22 | none | yes | none | **1** / 2 |

Each rel32 shares its call's verdict (in one qword everywhere except the object tap). None of the five callees has a non-call direct edge; the spawner
`0x0150e0` has 10 callers and the tap callee `0x015270` has 4 (two more at `0x06e5e5`/`0x06e6d5`, the script path
[i]), so all claims are call-site claims and never target-entry hooks. Image dwords pointing into any call's bytes 1..4:
0 at every site. The window starts that are branch targets are instruction boundaries outside the patched bytes, so
they do not affect the claim.

Window bytes with relocated dwords zeroed (the `code_window_fl.h` convention: at install each zeroed dword must equal
base + the listed target RVA, and the rest must hash to the FNV-1a 32 value):

| Site | Bytes | FNV-1a 32 | Call offset |
| --- | --- | --- | --- |
| impact | `51 8b d3 8b cf e8 ab 10 fc ff 83 c4 04 8b cf e8 e1 69 00 00 85 c0 74 09 8b d3 8b cf e8 94 46 fc ff 5f 5e 5d 5b 83 c4 08 c2 0c 00` | `de6f8702` | 5 |
| tap 1 | `8b 57 08 8d 44 24 18 6a 00 50 53 56 b9 03 00 00 00 e8 e8 0c 00 00 8b c8 83 c4 18` | `c5cdd97d` | 17 |
| tap 2 | `8b 53 08 8d 44 24 18 6a 00 50 57 56 b9 03 00 00 00 e8 1b 0c 00 00 8b c8 83 c4 18` | `73ecdfa4` | 17 |
| flight | `6a 00 6a 00 8d 44 24 48 33 c9 50 6a 00 6a 00 6a 00 57 52 33 d2 e8 31 02 00 00 83 c4 20` | `38c0ec0e` | 21 |
| object | `a1 [00 00 00 00] 83 38 00 0f 84 7f 07 00 00 8b cb e8 ce 07 fb ff 0f b7 73 48 0f bf c6 83 f8 0a` | `d392394d` | 16 |
| muzzle | `a1 [00 00 00 00] 83 c4 08 8b 75 dc 8b 44 06 7c 85 c0 74 2b 8b 95 78 ff ff ff 8d 8d 20 fd ff ff 51 6a 00 8d 8d e0 fe ff ff 51 6a 00 6a 00 6a 00 57 50 b9 04 00 00 00 e8 a2 5a fc ff 83 c4 20` | `35569ad2` | 54 |
| death | `ff 75 10 8b d3 8b ce e8 18 fb fb ff 83 c4 04 8b ce e8 be 59 00 00` | `6452f5c6` | 7 |

The frame witnesses (read only, compared like the AP prologue and writer witnesses) are in the output: the damage
routine's prologue `0x053070` (25 bytes, `bc88bb15`), its shield-walk total store `0x05318d`, the absorbed path
`0x053194` (9), the source reload and flag-`0x20` test `0x0531a1` (53, one relocated dword → `0x1a83c8`), the hull add
`0x0532a9`, the total reload and piloted clamp `0x0532e3` (29), the fire routine's gun-slot and bullet-index stores
`0x04e4a4` / `0x04e4f9`, and the death function's row-chooser call `0x013f3d` (14).

## 2. Caller frames

### 2.1 Impact `0x053390` (damage routine `0x053070`)

The damage routine is ECX = victim, then three stack dwords (`[esp+4]` source, `+8` shield damage, `+0xc` hull damage)
and `ret 0xc` (`0x0533b3`). The AP routine took four stack dwords and returned with `ret 0x10`. Its ten callers include the collision call
`0x068185`, which reads TBullets `+0x44` / `+0x80` / `+0x58`, and the area damage `0x068db0` (`0x068f41`). The area
damage is called from the kill routine (`0x054445`) and calls it back (`0x069003`), the same nesting as AP section 6.
The guards before the call match AP's (victim node `+0x12c & 0x100000`, victim `+0x40 & 0x40000000`, and the class-0
piloted-small-ship test through `0x049690` / `0x01e460`), so the stub sees the same hits [s FL anchors; AP twin `0x00449d3b..0x00449d81` read in the listing].

The impact function `0x014440` is ECX = victim, EDX = source. It reads no stack argument (it realigns its frame and
has no positive `ebp` reference), and it has three plain `ret` (`0x0145c4`, `0x01468d`, `0x014696`). The caller pushes
one dummy dword (`push ecx` at `0x05338b`) and pops it.

| At stub entry | FL | AP (stub slot) |
| --- | --- | --- |
| victim | **ECX**, also EDI (live) | `[esp+4]`, ESI |
| source | **EDX**, also EBX (live, read at `0x0533a3`), and `[esp+0x24]` (the routine's own argument) | `[esp+8]`, EDI |
| dummy | `[esp+4]` (whatever ECX held; never read) | — |
| saved EDI, ESI, EBP, EBX | `[esp+8]`, `+0xc`, `+0x10`, `+0x14` | `+0xc..+0x18` |
| shield-walk total T | `[esp+0x18]` (routine `[esp+0x10]`; final writes `0x053151` drain, `0x05318d` absorb) | — |
| remaining shield-stage damage | `[esp+0x1c]` (routine `[esp+0x14]`; 0 after an absorb, `0x053196`) | — |
| **running damage total** | **EBP**: T, plus a positive hull term (`add ebp,esi` `0x0532a9`), clamped to 50,000 for a victim with `+0x40 & 2` (`cmovg` `0x0532fd`); no memory copy except `[esp+0x2c]` on the piloted path (`0x053307`) | `[esp+0x1c]` (slot 7) |
| **absorbed** | **not stored.** ESI = 1 only on the absorbed, hull-skipped path (`lea esi,[ebp+1]` `0x05319a` → `test esi,esi; jne` `0x0531ce`); otherwise ESI is 0 (no shield absorbed and a zero hull argument) or the last hull term applied (`0x05323d`, `0x05329f`, `0x0532b1`, `0x0532db`). No ESI write after `0x0532e3` | `[esp+0x24]` (slot 9) |
| shield / hull arguments | `[esp+0x28]` / `[esp+0x2c]`, both rewritten as temporaries on the flag-`0xa0` and piloted paths | `[esp+0x34]` / `[esp+0x38]` |

**Absorbed at the FL stub.** AP's look rule is `absorbed == 1 && !(class-0 source && flag 0x20)`. On FL, the first half
of that rule (absorbed and no flag-`0x20` bypass) is exactly the path where ESI = 1 survives to the call. Read it as
`ESI == 1 && [esp+0x1c] == 0 && EBP == (victim+0x40 & 2 ? min([esp+0x18], 50000) : [esp+0x18])`. Proof over
`0x053070..0x0533b3` (re-traced 2026-11-08 for the port review) [s]: the absorbed path `0x053161..0x05319d` stores
T (`0x05318d`), clears the remaining (`0x053196`), sets ESI = 1 (`0x05319a`) and, without the flag-`0x20` bypass
(`0x0531c5` → `0x0531d6`, where ESI becomes the hull argument), jumps to the reload `mov ebp,[esp+0x10]` at `0x0532e3`;
the piloted `cmovg` at `0x0532fd` then clamps, and no ESI write follows. The hull path never runs that reload: it
reaches `0x0532e7` with EBP = T + h (`0x053264` reload, `add ebp,esi` `0x0532a9`) or EBP = T with ESI ≤ 0
(`jle` `0x05326a`), and ESI = h from `0x05323d` / `0x05329f` / `0x0532b1` / `0x0532db`. So ESI == 1 alone is not
sufficient (h can be 1), but with EBP == T it is unique for an unclamped victim. The one case the clamp hides is a
piloted victim with T ≥ 50,000 whose hull stage ran with remaining 0 and a final term of 1. Remaining 0 on the hull path
means the walk drained every shield to 0 (`0x053155`): a shield still holding energy sends the next walk step to the
absorb branch (`0x05311d` `jg 0x053161`). The port therefore compares EBP with the clamped T and reads that residual
case as absorbed; the impact handler's drained-shield rule (post-hit shields below 0.5 %, here 0) records it as a hull
hit, while a genuinely absorbed hit leaves its absorbing shield above 0. Absorbed hits with T ≥ 50,000 on a piloted
victim (the PSP, IONC, PPC and IBL rows) stay staged shield hits. A
flag-`0x20` hit through charged shields reads absorbed = 0 on FL but 1 on AP. `role_from` already makes it a hull hit,
so only the event's `flag_absorbed` bit differs [i]. The shield energies at the stub are post-hit as on AP. The
recipe of AP section 9 applies unchanged with the FL TShields slot `0x1a83ec`, count `0x1a8354` and the walk
`ext+0x6c`, node `+0`, `+0x4a`, `+0x50`, `+4`, capacity `+0x48`. EBX no longer holds the removed shield points: it is
the source.

Liveness after the return: EAX, ECX, EDX and EFLAGS are dead (`add esp,4; mov ecx,edi; call 0x059d80`; `0x059d80`
reads only ECX). EDI and EBX are live. ESI and EBP are restored by the routine's epilogue pops, so the stub may read
them but must not change them. Skip return: `xor eax,eax; ret` (plain, the caller pops the dummy).

### 2.2 Inner taps `0x014583` / `0x014650` (callee `0x015270`, AP `0x00414a70`)

The callee is ECX = kind (3), EDX = projectile id (`[proj+8]`), then `[esp+4]` eff, `+8` hit object, `+0xc` &pos
(hit frame), `+0x10` 0. It has plain `ret` (`0x015301`, `0x015330`), and the caller pops `0x18`, which includes
`0x014c10`'s two arguments. Tap 1 has hit = EBX = victim and projectile = EDI = source. Tap 2 has hit = EDI = source
and projectile = EBX = victim. These are the AP tap 1 / tap 2 roles (AP: ESI = id, `[esp+0xc]` hit, `[esp+0x10]` &pos). The
return is tested (`mov ecx,eax; test ecx,ecx`), so the taps stay observe-only and always forward ECX and EDX unchanged.
The eff column is TBullets `+0x78` (`0x014550`) or TMissiles `+0x68` (`0x014561`). The callee reads the Effects row
through `0x21c8a0 + id·0x14` and tests `EEDF_PUSH` (`0x10000000`) on element `+0x3c` (`0x0152a6`).

### 2.3 Flight `0x014eaa` (inside the per-object routine `0x014d90`, class-0 branch)

Spawner ABI (engine-effects.md): ECX = kind, EDX = k, `[esp+4]` eff, `+8` obj, `+0xc..+0x14` 0, `+0x18` &pos,
`+0x1c` t0, `+0x20` &orient, caller pops `0x20`, single plain `ret` `0x01526a`. At this site ECX = 0 and EDX = 0
(`0x014e9d`, `0x014ea8`), `[esp+4]` = TBullets `+0x74` (col 19, `0x014e65`), `[esp+8]` = **the bullet, EDI**
(AP: ESI, `[esp+0x10]`), and `[esp+0x18]` = `&zero` (`[esp+0x40]` zeroed by `movaps` at `0x014e8c`). The trail id
TBullets `+0x84` is in EBX (live, `test ebx,ebx` `0x014eb2`). EAX and EFLAGS are dead after the return. The skip path
is `xor eax,eax; ret`, as on AP.

### 2.4 Object tap `0x0645bd` (sector update, every object, AP `0x0045ac7d`)

Here `mov ecx,ebx; call 0x014d90`: **the object is in ECX, with no stack argument and no caller pop** (AP:
`push ebx`, `[esp+4]`, `add esp,4`). The gate `*(*0x1a82ec) != 0` comes first (`0x0645ad`; AP `0x0060850c`). After the
return, `movzx esi,[ebx+0x48]; movsx eax,si; cmp eax,0xa` leave ESI, EAX and EFLAGS dead and EBX live. The callee
clobbers ECX and EDX. The window start `0x0645ad` is the loop join (12 sources). The five bytes cross
`0x0645c0`: **plain copy in the install window**, the only such site on FL.

### 2.5 Muzzle `0x04f639` (fire routine `0x04e310`, AP `0x00445170`)

The fire routine is ECX = ship (EDI, `[ebp-0x3c]`), four stack dwords and `ret 0x10`, with SEH at `0x04e334`. It has
4 callers, as on AP. The AP wrapper `0x00413c00` is inlined. The site loads TBullets `+0x7c` (col 21) through
`[ebp-0x24]` (bullet index · `0xdb8`) and **skips the call when the muzzle row is 0** (`je 0x04f641`). It then calls
the spawner with:

| Item | FL | AP |
| --- | --- | --- |
| kind 4 / gun slot k | ECX = 4 / **EDX** = `[ebp-0x88]` (laser `ext+4` word, single writer `0x04e4a8`) | inside the wrapper / `[esp+8]` |
| eff (col 21) | `[esp+4]` (EAX) | read by the wrapper |
| ship | `[esp+8]` (EDI) | `[esp+4]` |
| &pos / &orientation | `[esp+0x18]` = `&[ebp-0x120]` / `[esp+0x20]` = `&[ebp-0x2e0]` | `[esp+0xc]` / EDX |
| bullet record | ESI = index · `0xdb8` (single writer `0x04e548`); index word `[ebp-0x8c]` | EAX = index, `[ebp-0x7c]` |
| laser object | `[ebp-0x18]` (single writer `0x04e46d`) | `[ebp-0x18]` |

After the return, EAX, ECX, EDX and EFLAGS are dead (`mov eax,[ebp-0x28]`; `cmp`). ESI and EDI are live, because the
path at `0x04f648` → `0x04f73e` still uses them. The skip path is `xor eax,eax; ret`. The fire routine fills the bullet
extension exactly as AP does: `ext+0` expiry (`0x04f985`), `ext+4` owner = `[ebp-0x3c]` (`0x04f98d`), `ext+0x70` laser
(`0x04f99b`), and the laser's `ext+0xc` = the bullet (`0x04f9a1`).

**Reach.** The AP site ran for every shot, because the wrapper tested col 21. The FL site runs only for rows with a
muzzle row. In the FL installation 5 of 45 TBullets rows have col 21 = 0 (`SS_BULLET_TUG`, `KH_ALPHA`, `KH_BETA`,
`KH_GAMMA`, `REPAIR`), and their shots never reach the FL muzzle site [s data]. A per-shot muzzle tap on FL for
those rows would need a different site [u].

**What the muzzle row draws** (2026-11-09, [muzzle-flash-draw.md](muzzle-flash-draw.md)). Col 21 is an Effects
row, for the IRE row 22. Its element 0 is a `CUT` scene (04251) whose one part is body 445, a 3D mesh. The mesh is
drawn through the traversal and the material submit `0x0e7142` → `0x0e0f30` as a classic `MATERIAL3` body:
`standard_lighting` `DEFAULT`, texture 384, additive ONE/ONE, z-write off. It is identified at the draw by node
`+0x144` = 445. The engine animates only the root's scale (grow-and-shrink, 150 ms) and its placement from the ship
node.

### 2.6 Death `0x0543b3` (kill routine `0x0542f0`) and the hull-copy fade

The kill routine is ECX = obj (ESI), `[ebp+8]` reason word (`0x05430b`), `[ebp+0xc]` killer (EBX, `0x054330`),
`[ebp+0x10]` arg4, and `ret 0xc`. It has the same dying bit and reason gates as AP. The AP area-bullet path (a
`0x40000000` object whose TBullets row has `0x100`) only zeroes `+0x10/+0x14` (`0x054354`) and **joins the common
tail**, so FL has a single call. That call is reached for every exploding object whose parent is a sector
(`cmp word [eax+0x48],1` `0x0543a5`). As on AP, the bullet-flag test indexes TBullets without a class check, so the
stub must test the class.

| At stub entry | FL | AP |
| --- | --- | --- |
| dying object | **ECX**, also ESI (live, `mov ecx,esi` `0x0543bb`) | `[esp+4]`, EDI |
| killer | **EDX**, also EBX (live, read at `0x05440c`, `0x05442f`) | `[esp+8]` |
| arg4 (→ instance `+0x18`, the WRECK gate) | `[esp+4]` (`push [ebp+0x10]`), forwarded as the spawner's a6 (`0x0142bb`, `0x014308`) | `[esp+0xc]` |
| reason | `word [ebp+8]` of the caller's frame, as on AP | same |

The death function `0x013ed0` is ECX = obj, EDX = killer, one stack dword, with plain `ret` and SEH at `0x013ef4`.
It refuses class `0x1c` (`0x013f23`). It returns 1 with no explosion for class 10 with `+0x40 & 0x1000000` and a
non-zero killer (`0x013f53..0x013f68`). It reads the death row **once**, through the chooser `0x0138e0` at `0x013f46`. The
chooser is ECX = class, EDX = subtype (**swapped against AP**), `[esp+4]` = obj `+0x40`, default 48, with columns
TShips `+0xbc`, TDocks / TFactories `+0x60`, TAsteroids `+0x48`, TMissiles `+0x7c` and TBullets `+0x78` if `0x100`
(identical). The carrier is class `0x1b` with body id from node **`+0x144`** (`0x01400a`; AP `+0x140`) via `0x049110`.
The death function tests HIDEOBJECT `0x20` (`0x014045`), NOROTATE 8 (`0x014111`) and EDF_PROPAGATE 2 (`0x0141a6`) on
`0x21c8a4 + row·0x14`. It spawns kind 1 with ECX = 1, EDX = the carrier id (`0x0142c3`, `0x014310`). After the
return, EAX, ECX, EDX and EFLAGS are dead. The skip return is `xor eax,eax; ret`, plain, with the caller popping one
dword.

**The hull-copy fade is unchanged except the clock.** The element update tests FADEOUTBODY `0x800` on element `+0x3c`
and keeps its `+0x40` (`0x016ead`, `0x016ebb`). After the loop, for a class-`0x1b` instance object (`0x015579`), it
computes age = clock − `inst+0x28` − start with **clock = cfg `+0x708`** (`0x015591`; AP `+0x718`). The ramp is 255
before start, 1 from 1000 ms (`0x016ec8`) and linear between. It writes to the carrier node's `+0x13c` (`0x016ef9`),
which is the AP rule. DISTANT `0x20000` (`0x01554c`), HULLDAMAGE `0x80000` with `0x0585a0` (the shield sum [i]) < 100
(`0x0155ae..0x0155bd`) and WRECK `0x4000000` (`0x016b3f`, `0x016dc8`) are tested on the same word. So the AP
clone-row seam (column swap around the forwarded call) carries over, with these FL inputs:

* Effects rows at `0x21c8a0 + id·0x14`, ids bounded by `cmp eax,0x3e7` (`0x012eb4`), table end `0x2216c0`. Row `+0`
  count, `+4` EDF flags (`|1` BREAKUP, `|2` PROPAGATE at load, `0x012fc3` / `0x012fdb`), `+8`, `+0xc`, `+0x10`
  elements.
* Element stride `0x70`, with body `+0`, I3DF `+4`, sizes `+8` / `+0xc`, timeout `+0x10`, `+0x14`, offsets
  `+0x20..+0x28`, `+0x30` / `+0x34` / `+0x38`, EEDF `+0x3c`, start `+0x40`, PROPAGATE `+0x44..+0x60` (loader stores).
  All are identical.
* **Allocation.** The loader allocates element arrays with `0x0d8fc0` (ECX = size, EDX = flags; `xor edx,edx` at
  `0x012f50`). With flags 0 that is `malloc 0x131395` plus memory accounting (`[0x1aae20]`, `[0x1aaa14]`,
  `[0x1aaa18]` += size, `[0x1aae1c]`++) and a zero fill. The reset `0x0135c0` frees `+0x10` with `0x12f4e5` and
  subtracts size from `[0x1aae20]` and `[0x1aaa18]`. A clone array built with `0x0d8fc0(size, 0)` therefore stays
  balanced with the reset. A plain `0x131395` would leave those two counters short by the clone's size [i: the
  counters' use is not traced]. FL's malloc is a `mov edi,edi; push ebp; mov ebp,esp; pop ebp; jmp` thunk, so the AP
  witness `55 8b 6c 24 08` does not match.

## 3. Record and object offsets the AP code reads (weapon_effects_core.h `layout::`, `bolt_layout::`, `Globals`)

| Constant | AP | FL | FL anchor (RVA) | Verdict |
| --- | --- | --- | --- | --- |
| obj flags / class / subtype / ext / node / radius | `+0x40/+0x48/+0x4a/+0x50/+0x70/+0xa4` | same | `0x0542fc`, `0x014e07`, `0x014e56`, `0x0530bd`, `0x014458`, `0x01448a` | identical |
| obj dying bit, shielded bit 20 | `0x8000000`, `0x100000` | same | `0x0542ff`, `0x053096` | identical |
| classes 1/5/6/7/10/17/`0x12`, no-effect `0x1c`, missile area `0x1000000` | | same | `0x05308d..0x0530a3`, chooser jump table, `0x013f23`, `0x013f59` | identical |
| node live / handle / pos / rot / contact | `+0x1c/+0x28/+0x30/+0x40/+0x180` | same | `0x014463`, `0x0a22a8`, `0x0144ab`, `0x01449d`, `0x0144a0` | identical |
| node velocity / rot row 2 (`bolt_layout`) | `+0x100` / `+0x60` | same | `0x014db4`, `0x013fb0..0x013fce`; `0x0145ab` | identical |
| node flags | `+0x12c` | same | `0x053347` | identical |
| **node body id** (`node_body`, the bubble walk) | `+0x140` | **`+0x144`** | `0x01400a`, `0x013f2e`; engine-effects.md node walk | **changed** |
| node walk read size (`node_walk_bytes`) | `0x144` | at least `0x148` (the node block is `0x270`) | follows the body offset | **changed** (section 5) |
| node children / sentinel / scales | `+0xc` / `+0x10` / `+0x70`, `+0x80..+0x88` | same | engine-effects.md "node walk" | identical (cited) |
| bullet ext expiry / owner / created / laser / length | `+0` / `+4` / `+8` / `+0x70` / `+0x78` | same | `0x049b3d`, `0x04f98d`, `0x049b4d`, `0x04f99b`, `0x04a766` | identical |
| shield list and node | `ext+0x6c`; `+0`, `+0x4a`, `+0x50`, energy `+4`, full `0x3e80000`, capacity TShields `+0x48` | same | `0x0530c0`, `0x0530b9`, `0x0530de`, `0x0530d4`, `0x0530d7`, `0x053102`, `0x0530ee` | identical |
| type stride | `0xdb8` | same | `0x014e5a` | identical |
| TBullets shield / hull damage / flags | `+0x44` / `+0x80` / `+0x58` | same | `0x068112`, `0x068115`, `0x06810b`; loader `0x040754`, `0x0407ea`, `0x040786` | identical |
| TBullets light colour | `+0x5c/+0x60/+0x64` | same | flash `0x04dbd0`: `0x04dbef..0x04dbfd`; loader `0x040790..0x0407a4` | identical (the light **node's** R moved to `+0x154`, unused by the hook) |
| TBullets flight / impact / muzzle / trail | `+0x74/+0x78/+0x7c/+0x84` | same | `0x014e65`, `0x014550`, `0x04f60e`, `0x014e69`; loader `0x0407cc..0x0407f7` | identical |
| TBullets loader map | col n → `0x44 + 4(n−7)`, version gates `0x33`/`0x35`/`0x37` | same | case `0x04074b` (class-0 entry of the dispatch `0x04073d`/`0x040744`), gate `0x04081c` | identical |
| TShips class id (col 52) | `+0xc8` | same | `0x0413d2` (table `0x1763d8` pushed, reader `0x107960`) | identical |
| death rows | TShips `+0xbc`, TDocks/TFactories `+0x60`, TAsteroids `+0x48`, TMissiles `+0x7c` | same | `0x01390e`, `0x01392f`, `0x013949`, `0x013963`, `0x013984` | identical |
| caller reason | `word [ebp+8]` | same | `0x05430b` | identical |
| Effects row / element layout, EEDF bits | section 10 of the AP note | same | section 2.6 | identical |
| **cfg game clock** (`cfg_game_ms`) | cfg `+0x718` | **`+0x708`** | `0x049b47`, `0x015591` | **changed** |
| **cfg slot** | `0x00606f34` | RVA **`0x1a83b0`** | `0x014eba`, `0x01558c` | changed (relocated) |
| **type tables** | `0x00606fb8 + 4c` | RVA **`0x1a83c8 + 4c`** | `0x014e60` (0), `0x0530e2` (9), `0x013909` (7) | changed (relocated) |
| **type counts** | `0x00607038 + 4c` (table + `0x80`) | RVA **`0x1a8330 + 4c`** (table **− `0x98`**) | `0x04e510` (0), `0x03d8a5` (7), `0x03d8c6` (6), `0x03d8da` (5), `0x06f732` (9), `0x043e85` (10) | **changed layout**: the `Globals` static_assert relation `count = table + 0x80` does not hold |
| Effects rows | `0x00580068` | RVA **`0x21c8a0`** | `0x01534a` | changed (relocated) |
| body cache root | `0x00608518` | RVA **`0x1aa100`** | `0x0867c1` | changed (relocated) |
| body cache hash `+0x14`, model LODs `+0xc` / count `+0x10`, LOD extent `+0x20` | | same | section 5 | identical |
| malloc / free | `0x005112c4` / `0x0050e1b0` | RVA `0x131395` / `0x12f4e5`; loader allocator `0x0d8fc0` | section 2.6 | changed |
| fixed-point rotate | `0x004f0da0`: `imul`, `+0x8000`, `adc`, `shrd 16` | `0x10e0c0`, same rounding | `0x10e0dc..0x10e0ee` | identical arithmetic [s]; the summation order differs, and the exact 32-bit sum makes the result the same [i] |

## 4. What differs from AP in ABI and geometry

FL keeps every record and object field the weapon hooks read, but it moves the arguments at every site. The damage
routine, the impact function, the impact-instance function, the spawner, the death function, the per-object routine
and the row chooser all take their first one or two arguments in ECX and EDX. The rest stay on the stack in AP order,
with plain `ret` at the five spawner-side callees. The FL site windows are therefore new byte sequences with new stack
slots, not relocated copies. Two AP sites disappear as distinct sites. The two death calls merge into one, which sees
area bullets and everything else. The muzzle wrapper is inlined, so the FL muzzle site is one of the spawner's ten
callers, reached only for muzzle rows other than 0. The impact frame loses its memory copies: the running total lives
in EBP and the absorbed decision lives only in ESI, so the FL stub must read both registers. The atomic geometry
reverses for the impact and the AP area-death site. Every FL site except the object tap fits one aligned qword. The object
tap is the only plain-copy site, whereas the AP impact, object and area-death calls all straddled. Two windows carry a
relocated absolute operand (object: the gate slot `0x1a82ec`; muzzle: the TBullets slot `0x1a83c8`), and no call's
rel32 is relocated. Data-side changes are the clock offset, the node body offset, the relocated globals and the count
array's position.

## 5. Shield-hit runtime reads

Everything the shield-hit path reads from game memory at runtime, from `src/proxy/weapon_effects_core.h` (the impact
handler `impact()` and the shield read), `weapon_effects_stage.h` (the owner rows read at scene end, before Present)
and `weapon_effects_bubble.h` (the hull walk behind the ship bubble, budgeted per frame and cached per owner). The
bubble anchors are paired with their AP twins in the producer (group `bubble`: 31 FL, 22 AP, 0 failures) **[s]**
unless tagged.

| Read (when) | AP | FL | FL evidence (RVA) | Verdict |
| --- | --- | --- | --- | --- |
| impact inputs: victim, source, running total, absorbed (impact) | `[esp+4]`, `[esp+8]`, `[esp+0x1c]`, `[esp+0x24]` | ECX, EDX, **EBP**, derived from **ESI** + `[esp+0x18]`/`[esp+0x1c]` | section 2.1 | **changed (site ABI)** |
| classes, flags, subtype, radius of victim/source (impact) | obj `+0x48/+0x40/+0x4a/+0xa4` | same | section 3 | identical |
| contact point: smaller body's node rot `+0x40`, contact `+0x180`, pos `+0x30`; hit node pos/rot (impact) | node offsets | same | `0x01449d`, `0x0144a0`, `0x0144ab` | identical |
| node live `+0x1c`, handle `+0x28` (impact, stage) | | same | `0x014463`, `0x0a22a8` | identical |
| projectile node rot row 2 `+0x60` (impact) | | same | `0x0145ab` | identical |
| TBullets flags `+0x58`, light `+0x5c..`, shield/hull damage `+0x44`/`+0x80` (impact) | table `0x00606fb8`, count `0x00607038` | table RVA `0x1a83c8`, count RVA **`0x1a8330`** | `0x014e60`, `0x04e510`; section 3 | offsets identical; slots changed |
| TShips class id `+0xc8` for the tier (impact) | table `0x00606fd4`, count `0x00607054` | RVA `0x1a83e4`, `0x1a834c` | `0x0413d2`, `0x03d8a5`/`0x03d8ad` | offsets identical; slots changed |
| shield gate: class 7/1/`0x12` or `+0x40 & 0x100000` (impact) | | same | `0x05308d..0x0530a3`, `0x053096` | identical |
| shield list `ext+0x6c`, node next `+0`, index `+0x4a`, ext `+0x50`, energy `+4` (full `0x3e80000`), TShields capacity `+0x48` (impact) | TShields `0x00606fdc`, count `0x0060705c` | RVA `0x1a83ec`, count `0x1a8354` | `0x0530c0..0x0530ee`, `0x053102`, `0x06f732` | offsets identical; slots changed |
| game clock (impact `t0`, stage `now`, bubble retry) | `[0x00606f34] + 0x718` | `[RVA 0x1a83b0] + **0x708**` | `0x049b47`, `0x015591` | **changed** |
| owner rows (stage, every frame): `obj+0x70` node, `+0x40` flags (dying `0x8000000`), node `+0x28` handle, `+0x30` pos, `+0x40` 48-byte rows, `obj+0xa4` radius | | same | section 3 | identical |
| bubble key: root `+0xc` first child (≠ sentinel `+0x10`), its body id (stage, per owner) | body `+0x140` | body **`+0x144`** | `0x013f2e`, `0x01400a`; engine-effects.md node walk (setter `0x0a365d`) | **changed** |
| child list `c = [n+0xc]`, `while [c]`, next `[c]`, sentinel `n+0x10` (bubble) | | same | `0x094382`/`0x094385`; engine-effects.md node walk | identical |
| node block read per child (bubble) | `0x144` bytes (body at `+0x140`) | **`0x148`** to reach body `+0x144`; the node is `0x270` bytes | engine-effects.md (`push 0x270`) | **changed** |
| part filter: body > 0, not `+0x12c & 0x8000000` (bubble) | | same | `0x097b5c`, `0x097c5e`, `0x0a6836` ↔ AP `0x0047ebd8`, `0x0047ecf5`, `0x0048ae1f` | identical |
| placement: offset `+0x30` in the parent frame, × parent `+0x80/+0x84/+0x88` unless parent `+0x12c & 0x10000`, × parent `+0x70` when `+0x12c & 0x40`; basis `+0x40` (bubble) | `0x0047b564..0x0047b648` | same rule | `0x09438e..0x09441c` (`0x0943a8`, `0x0943b6/d2/ee`, `0x09440f`, `0x09441c`) | identical |
| part scale `+0x70`, axis scales `+0x80..+0x88` (bubble) | | same | `0x0943b6..0x0943ee` (as parent), engine-effects.md | identical |
| body cache root (bubble) | `0x00608518` | RVA **`0x1aa100`** | `0x099870`, `0x0867c1` | changed (relocated) |
| body cache hash: `H = [root+0x14]`, buckets `[H]`, size `[H+4]` (power of two, `& (size−1)`), key = id + 1, entries {next `+0`, key `+4`, model `+8`} (bubble) | `0x004863e2..0x00486419`, hit `0x00486464` | same | the read-only lookup **`0x099870`** (ECX = id; `inc ecx`, `[eax+0x14]`, `dec; and`, `cmp [eax+4]`, `mov eax,[eax+8]`, plain `ret`) | **identical** |
| model LODs: count int16 `+0x10`, array `+0xc`, `L_i = [arr + 4i]` (bubble) | `0x004c6c59`, `0x004861c9` | same | `0x0a17eb`, `0x0a17f1`, `0x0a17f6`, `0x0a1843` | identical |
| LOD extents `L+0x20/+0x24/+0x28` (bubble) | stored `0x004861b2` from `0x0047f350` | same, stored as one 16-byte `movups` (`+0x2c` alongside) | `0x0a182a` (`0x098560`), `0x0a183f` | identical |
| own ship root node (stage, cockpit dimming / eye sphere) | cockpit registry `0x00608504` | registry RVA `0x1a82e4` through `sector_background_fl` | `motion_output_shadow_adaptive_inc.h` (FL branch), [own-ship.md](own-ship.md) | already ported (FL adapter) |
| cockpit view mode (stage) | cockpit `+0x150` via `chase_camera::stats()` | `+0x150` | `chase_camera_sites_fl.h`, [chase-camera.md](chase-camera.md) | identical; needs the FL chase camera installed (else no dimming, as on AP) |

**The EXE's allocator.** In this tree (base `10880db5e`) the only weapon-effects code that calls into game memory
management is the clone-row builder R' (`weapon_effects_patch.cpp`: `Globals::malloc_va`, the `malloc_prologue`
check, the array copy, and the death-column swap). The impact handler, the stage and the bubble only read
(`engine_memory::read`; the bubble header: "no allocation") **[s: grep of `weapon_effects_*` and
`motion_output_weapon_effects_inc.h` for allocator calls and game-memory writes]**. With R' stripped, nothing left
allocates with the EXE's allocator or writes game memory except the call-site claims themselves. The FL `malloc` thunk
and the accounting question of section 2.6 then do not matter.

**Q4.** Yes: the shield-hit path is portable with offset and global changes only. No walk differs structurally. The
body-cache hash, the model → LOD array, the LOD extents, the child list with its sentinel, the placement rules and the
shield list have the same layout and arithmetic on FL **[s]**. What changes is data:
* the node body id `+0x144` and a `0x148`-byte node read;
* the clock at cfg `+0x708`;
* the relocated slots (cfg `0x1a83b0`, tables `0x1a83c8 + 4c`, counts `0x1a8330 + 4c`, body cache `0x1aa100`), all
  base-relative.

The one non-data difference is the impact site's input ABI (section 2.1): total in EBP, absorbed derived from ESI, and
victim/source in ECX/EDX. That belongs to the stub, not to any walk. The `Globals` `static_assert` that puts counts at
table + `0x80` must become an edition parameter. The hull-extent bound `[0, 0x20000]` assumes FL bodies keep AP's
normalised extents **[i]**: the producer of the extents (`0x098560`) is not compared instruction by instruction.

## 6. Questions

**Q1 — reuse of the AP site machinery.** The installer carries over with RVA + module base: `claim_call`, whole-window
compare before any write, both-or-none rollback, read-back, and the atomic/plain choice by qword geometry. The windows
need the relocation-aware compare of `code_window_fl.h` (zeroed dwords = base + target RVA, FNV-1a over the rest)
instead of `memcmp` against AP bytes, and the impact frame witnesses of section 1. **Every site needs an FL stub entry
shim**, because the AP stubs hand the handler `esp` (and EAX, EDX or EBP) and the handler reads AP stack slots:

* impact must also publish ECX, EDX, EBP and ESI, and take the frame slots `[esp+0x18]` / `[esp+0x1c]`;
* the taps read EDX (id) and `[esp+8]` / `[esp+0xc]` and must forward ECX and EDX;
* flight reads `[esp+4]` / `[esp+8]` and must forward ECX and EDX;
* the object tap reads ECX;
* muzzle reads ECX, EDX, `[esp+4]`, `+8`, `+0x18`, `+0x20`, and ESI or `[ebp-0x8c]`;
* death reads ECX and EDX, `[esp+4]` and `word [ebp+8]`, and its swap path must re-issue the forwarded call with ECX,
  EDX and one stack dword.

The pushfd / save / handler / forward-or-skip skeleton and the `xor eax,eax; ret` skip return are valid at all seven
sites, because every callee returns with plain `ret` and every return value outside the taps is dead [s]. The muzzle
stub's AP `malloc` prologue check and the `Globals` count relation need FL values (section 3).

**Q2 — claim route.** One aligned qword, so **`lock cmpxchg8b`**: impact `0x053390`, tap 1 `0x014583`, tap 2
`0x014650`, flight `0x014eaa`, muzzle `0x04f639`, death `0x0543b3`. Straddling, so **plain copy inside the install
window**: the object tap `0x0645bd` (crosses `0x0645c0`), which runs only in the frame loop like its AP twin [i].

**Q3 — what the AP classifier does not know.**

* TShips ids: `patch_tier_of_ship` already maps FL's 2151 (fighter), 2152 and 2153 (capital); their death rows are
  35 / 37 / 37. Rows outside the switch fall to the radius bands: `OBJ_RELAYBEACON` 2063 ×1 (SG_SH_M5, death row 35),
  the unnamed 2061 ×7 (SG_SH_M5, 35) and `0` ×5 [s data; band result i].
* TBullets data: the FL table is the stock one (version 55, 45 rows of 50 columns, every flight, impact and muzzle row
  present in FL Effects). It therefore brings back flag bits the Mayhem-flown classifier never saw in flight: `0x2000`
  continuous beams ×7, `0x800` ×4, `0x100` area / flak ×9, `0x1000` fragments ×4, `0x200` / `0x400` ×4 / ×2, and the
  untested `0x1` ×2 and `0x10` ×1. The classifier's cue list has no `0x2000` and no `0x800` [s data; their looks are
  for the design].
* No FL-only TBullets flag test was found in code. The FL-only `test [..+0x58]` immediates are `0x8` at `0x04fd81` and
  `0x2006` at `0x04f9ae` (both in the fire routine, where AP tests the same bits through a spilled record base) and
  `0x1` at `0x13b9f6` / `0x1423de` (library code, not a TBullets read) [s sites; i roles].
* The muzzle reach of section 2.5: Khaak, tug and repair shots have no muzzle-site event on FL.

## Unknown

- The use of the memory-accounting counters a non-accounted allocation would unbalance (moot once the clone row R'
  is stripped, section 5).
- The FL script-path callers of `0x015270` (`0x06e5e5`, `0x06e6d5`), which are not traced.
- Everything section 6 and "Unknown" of the AP note leave open, which FL inherits: the contact point's node `+0x180`,
  the missile extension, and the kill third argument on the script death path.
- Runtime confirmation: nothing here has run in the game.
