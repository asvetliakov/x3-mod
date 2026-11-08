# FL bullet draw batch: per-body identity at the draw, draw shape, suppression, bullet record

Bounded offline contract, 2026-11-08, for the projectile stage's "replace per body" rule
([projectile-look.md](../../architecture/projectile-look.md) section 3 and the section 9 rows "How a bullet draw's body
is identified at the hook", "Whether the game's batch for a body also carries other bodies' rows" and "The engine's
drawn bolt length"). Inputs: `X3FL.exe` SHA-256 `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`
(`/mnt/c/X3FL/X3FL.exe`), `X3AP.exe` SHA-256 `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`
(`/mnt/c/X3AP-orig/X3AP.exe`) for the twins, the FL installation's catalogues (`/mnt/c/X3FL`, root then `addon2`).
**Every FL address is an RVA** (VA at the preferred base `0x400000` minus `0x400000`); a hook adds the running module
base. AP addresses are VAs. **[s]** static instruction or data evidence, **[i]** inference, **[u]** not established.
No production edit, no build, no Wine, no game.

The AP twin is the instanced-batch path of [view-submit-hot-path.md](../view-submit-hot-path.md) ("The
instanced-batch path") and [effects-engine-remaining-emission.md](../effects-engine-remaining-emission.md) ("Bullet
vertex buffer writer"). The FL bullet record offsets are those of [weapon-effect-spawn.md](weapon-effect-spawn.md)
section 3; this note adds only what the draw needs.

**Answer.** On FL the AP flush `0x0046d080` is inlined into the view submit `0x0978e0`. Its batch loop iterates
**EBX = batch record** and **ESI = material record**, and makes one call per (batch, texture) at **`0x097af8` →
`0x0e0b40`**. Every draw holds exactly one body: batches are looked up by the node's body id, and one VB record is
kept per (body, texture). The game's draw is a **`DrawPrimitive`, not a `DrawIndexedPrimitive`**: TRIANGLELIST,
StartVertex 0, stride 24, 66 vertices per IRE bolt. Nothing global names the batch at that draw: the batch pointer
survives only as the caller's EBX saved in the callee's frame. The cheapest safe identity is CPU-side: claim the call
`0x097af8`, compare **EDX + `0x0c`** (the batch's body id) with the IRE's body id (word `+0x0` of its TBullets
record), and skip the call for that body. Every other body is forwarded byte for byte. A D3D-only rule needs the UV
set in the staged prefix, because the texture is shared by four bullet bodies and the 66-vertex period by two. It
also misses the engine's **traversal fallback**: a body with fewer than two bolts in a layer, or one cut by the
vertex budget, is drawn through the ordinary material submit. There the existing FL object-trace scope gives the node
body id `+0x144`. The design note's bolt length rule is corrected: `0.1 × speed / node scale` is the length of the
body's **collision** hull. The drawn bolt is the body mesh at the node's scale, with no speed term.

## Reproduce

```sh
python3 -I verification/results/fl-bullet-draw-batch/fl_bullet_draw_batch.py \
  > verification/results/fl-bullet-draw-batch/fl_bullet_draw_batch_out.txt     # about 1.5 s
```

[Producer](../../../verification/results/fl-bullet-draw-batch/fl_bullet_draw_batch.py) →
[`_out.txt`](../../../verification/results/fl-bullet-draw-batch/fl_bullet_draw_batch_out.txt). It reuses the PE /
Capstone / `.reloc` reader of `fl_weapon_effect_sites.py` and checks both hashes. For the call `0x097af8` it prints
the window (masked bytes, FNV-1a 32, relocations, qword geometry, branch targets, image dwords, callers) and every
relocated dword of the loop and of the submit routine. It then decodes the instancing vertex declaration and the
technique and parameter strings from the image. It asserts **124 whole-instruction FL anchors and 14 AP twins, 0
failures**. Last, it runs a census of the 30 distinct FL bullet bodies of the 45 TBullets rows: per texture, the LOD-0
faces, vertices per instance, the 0x37-triangle instancing limit, and a hash of the first instance's UV words in the
writer's face order. The output is deterministic: two runs are byte-identical. A Ghidra 12.1.4 import of the FL EXE
(project and decompiler output in the session scratchpad, untracked) supplied the record layouts (`0x0978e0`,
`0x085390`, `0x0852d0`, `0x0857f0`, `0x0e0700`, `0x098140`, `0x0e8600`). All figures below are **[s]** from that
output or those decompiles unless tagged otherwise.

## 1. The batch loop and what identifies a batch at the draw (question 1)

**View submit `0x0978e0`**: thiscall, ECX = scene, kept in EBX and `[esp+0x14]` (`0x0978ea`, `0x0978f4`). The stack
holds view, layer and flags, and the routine ends in `ret 0xc`. It has 5 E8 callers (`0x08ba73`, `0x08ba7f`,
`0x08bca9`, `0x097892`, `0x09789d`), one per view and layer, so a body's batch can be drawn several times per frame.
Per call it runs these steps:

1. It resets every batch (flags `&= ~3`, count `+0x24 = 0`) and, per material, the VB record's vertex count
   (`0x0e0660` at `0x097945`).
2. Pass 1 over the root list, `0x085700` (`0x097973`): eligibility and batch lookup, then count `+= 1`.
3. Priority, sort and select, `0x085780` (`0x097983`).
4. Pass 2, `0x0857f0` (`0x0979ac`): collect the instance; a nonzero result means taken. A root that is not taken goes
   to the ordinary traversal `0x0968c0` (`0x097aa1`, `0x097ac5`).
5. The flush **`0x097ad9..0x097b0e`**, which is AP's `0x0046d080` inlined:

| RVA | Instruction | Role |
| --- | --- | --- |
| `0x097ad9` | `mov ebx,[ebx+0x40]` | **EBX = batch** (the scene's batch list) |
| `0x097ae1` | `test byte [ebx+0x10],2` | batch collected in this call |
| `0x097ae7` | `mov esi,[ebx+0x30]` | **ESI = material record** (the batch's texture list) |
| `0x097af0..0x097af7` | `mov ecx,[esp+0x14]`; `mov edx,ebx`; `push edi`; `push esi` | ECX = scene, EDX = batch, `[esp]` = material, `[esp+4]` = view |
| **`0x097af8`** | `call 0x0e0b40` | one submit per (batch, texture); caller pops 8 at `0x097aff` |
| `0x097afd`, `0x097b07` | `mov esi,[esi]`; `mov ebx,[ebx]` | next material, next batch |

**Records** (`0x085390`, `0x0852d0`, `0x0e0660`, `0x0e0700`):

| Record | Offset | Field |
| --- | --- | --- |
| batch (`0x3c`, list `scene+0x40`) | `+0x00/+0x04` | next / prev |
| | `+0x08` | priority = vertices · 100 + count when count ≥ 2, else 0 (`0x085793`, `0x085798`) |
| | **`+0x0c`** | **body id = node `+0x144`**; the batch is looked up by it (`0x085390`) |
| | `+0x10` | bit 1 selected, bit 2 collected |
| | `+0x14` / `+0x18` / `+0x1c` | node `+0x12c` / node `+0x130` of the first node / body `+0x50` |
| | `+0x20` | capacity: `0x200` if `+0x14 & 0x4000000`, else `0x100` if `& 0x800`, else `0x400` |
| | `+0x24` / `+0x28` / `+0x2c` | pass-1 count / vertices per instance (all textures, **summed over all LODs**) / accepted |
| | `+0x30` | material list (sentinel at `+0x34`, tail `+0x38`) |
| material (`0x1c`) | `+0x08` | **texture id**, int16; the material is looked up by it (`0x0852d0`) |
| | `+0x0c` / `+0x10` | material flags / vertices per instance for this texture (all LODs) |
| | `+0x14` | descriptor: a global material record (stride `0x3c`, table at RVA `0x21b418`) or a body material record (stride `0x60`, body `+0x58`) |
| | `+0x18` | VB record |
| VB record (`0x10`) | `+0x00` / `+0x04` / `+0x08` / `+0x0c` | `IDirect3DVertexBuffer9*` / system copy / bytes / vertices this call |

**Submit `0x0e0b40`**: ECX = scene, EDX = batch, `[ebp+8]` = material, `[ebp+0xc]` = view. It installs an SEH frame,
returns with a plain `ret` and EAX = 1 or 0. It has one E8 caller, `0x097af8`. In order it makes these calls:

1. VB `Lock(0, bytes, &p, D3DLOCK_DISCARD)` (`0x0e0be9`), a memcpy of `count · 24` bytes (`0x0e0c04`), `Unlock`
   (`0x0e0c11`).
2. The effect lookup, `"effects"` unless batch `+0x1c & 0x10` (`0x0e0c1f`). No draw if material `+0x10` is 0 (`0x0e0c73`).
3. `GetTechniqueByName("INSTANCE_BULLETS")` when batch `+0x18 & 0x20000000`, else `"INSTANCE"`; FindNextValid and
   SetTechnique.
4. `t_DiffuseTexture` from texture record `0x1124f0(material +0x08)` `+0x34` through the cached setter `0x0da490`
   (effect vtable `+0xd0`). It is skipped when the record has `+0x10 & 0x2000000`: the previous texture then stays
   bound [u what that flag means].
5. `g_mViewProjection`, `SetMatrix`, `Begin`.
6. The engine state wrapper `[[0x1aaf94]+0x1c]`: declaration `[0x1aaed8]` (`+0x60`), stream 0 = VB, offset 0, stride
   `0x18` (`+0x64`), indices 0 (`+0x68`).
7. `BeginPass(0)`; then **`DrawPrimitive(D3DPT_TRIANGLELIST, 0, count / 3)` at `0x0e0e62`** (`call [ecx+0x144]` on
   the device `[[[0x1aaf94]+0x18]]`, return address `0x0e0e68`); `EndPass`; `End`.

**What identifies the batch at the draw.** The routine writes no global naming the batch, the body or the texture.
Its only global stores are two constants into `[0x1aae78]` `+0x28`/`+0x38` (`0x0e0c82`, `0x0e0c89`). At `0x0e0e62`
the registers hold EBX = device, ESI = the `ID3DXEffect`, EDI = the VB record, and on the stack `[ebp+8]` = material.
The batch pointer survives only as the caller's EBX pushed at `0x0e0b5e` (`[ebp-0x130]`). `[ebp-0x14]` held it until
`0x0e0d19` overwrote it with a parameter handle. A D3D hook could reach it only by walking the game's frame from its
own entry. That stack read is fragile, so this contract does not use it. The identity therefore has to be published
at the call site (section 5). The FL object-trace scope ([farnhams-legacy-object-identity.md](../../architecture/farnhams-legacy-object-identity.md))
is open only inside the material submit `0x0e0f30`, which `0x0e0b40` never calls. At an instanced bullet draw its
`current()` has no bullet node [i].

**Relocated words.** The loop `0x097ad9..0x097b17` has none, and the window of `0x097af8` has none. The submit
routine has 17, every one an absolute operand of a single instruction:

| Operand target RVA | What it is |
| --- | --- |
| `0x14ef20` | SEH handler |
| `0x1aaf94` | renderer object (×6) |
| `0x1740e0`, `0x174344` (×2) | the material-effect name compare and the `"effects"` name |
| `0x1aae78` (×2) | constant / state block |
| `0x18fc74`, `0x18fc88`, `0x18fa2c`, `0x18fc94` | the four strings |
| `0x150340` | `D3DXMatrixMultiply` IAT slot |
| `0x1aaed8` | declaration slot |

The bullet type table pointer sits at slot RVA `0x1a83c8`, which is a relocated slot. Its content, the table, is
heap memory and is not relocated.

## 2. Draw shape of a bullet batch (question 2)

| Item | Value | Source |
| --- | --- | --- |
| call | `IDirect3DDevice9::DrawPrimitive` (vtable `+0x144`), non-indexed; indices set to 0 before it | `0x0e0e62`, `0x0e0e3f` |
| primitive | `D3DPT_TRIANGLELIST` (4), StartVertex 0, PrimitiveCount = vertices / 3 = instances · 22 for the IRE | `0x0e0e50..0x0e0e5f` |
| stream 0 | the (body, texture) VB, offset 0, **stride 24** | `0x0e0e1d..0x0e0e2b` |
| declaration | `POSITION0` FLOAT3 @0, `TEXCOORD0` FLOAT2 @12, `COLOR0` D3DCOLOR @20 (elements at RVA `0x17c410`, created at `0x0eb05e` into slot `0x1aaed8`) | decoded |
| vertex | position already in world/scene space (`D3DXVec3Transform` by the node matrix `0x0e8600`), UV = model int × `2^-16`, colour = `alpha << 24` (alpha node `+0x13c` or `0xff`, RGB 0) | `0x0e0700` |
| VB | usage `0x208` (DYNAMIC, WRITEONLY; `0x218` under software VP), pool DEFAULT, `capacity · vertices · 24` bytes, created once per (body, texture) | `0x0e07af`, `0x0e07b4`; `0x0e0700` |
| lock | one whole-buffer `D3DLOCK_DISCARD` per submit, `count · 24` valid leading bytes | `0x0e0bca..0x0e0c11` |
| technique / texture | `effects` `INSTANCE_BULLETS`; `t_DiffuseTexture` = the material's texture; VS sees only `g_mViewProjection` | strings, section 1 |
| IRE body | `objects/effects/weapons/bullet_LS_alpha.pbb` (`01.cat`), 1 LOD, 22 triangles in one group (≤ `0x37`, instanced), **66 vertices per bolt**, texture `effects\weapons\fx_bullets_diff.tga`, effect material `effects.fx`, ONE/INVSRCCOLOR, Z write off, model z range −1..+1 (symmetric about the node origin) | census |
| IRE VB size | 1024 · 66 · 24 = 1,622,016 bytes, assuming capacity `0x400` (no `0x800` / `0x4000000` on the bullet node) [i] | arithmetic |
| per-draw cap | the selected batches together hold at most `0x78f0` = 30,960 vertices (`0x0857d2`), which is 469 IRE bolts when nothing else is selected | arithmetic |

**Recognising the IRE draw from D3D state alone.** The shape above holds for every instanced bullet body. Of the
identifying inputs, only the UV words separate the IRE from every other bullet body.

| Input | Shared with | Separates the IRE? |
| --- | --- | --- |
| texture `fx_bullets_diff.tga` | `bullet_PL_alpha` (HEPT, row 6), `bullet_PR_alpha` (PAC, row 1), `TOL_bullet` (PBC, row 25) | no |
| (texture, 66 vertices per instance) | `bullet_PL_alpha` | no |
| UV signature of the first instance (`b8237bbfe385`, against PL_alpha `8ec5dbcaebac`) | none of the 25 parsable bullet bodies | yes |

The five text MATERIAL3 bodies (`v/10436`, `v/00300`, `v/00525`, `v/00526`, `v/00527`) are not modelled
offline [u]. The proxy already sees these vertices: the locked-prefix scan reads the written prefix at Unlock
(`locked_prefix_core.h`). So the D3D-only test is: the INSTANCE_BULLETS pair, the stride-24 non-indexed draw, period
66, and a match of the first instance's 66 TEXCOORD0 pairs against the IRE's set. It rests on two
**[u]** assumptions:

* the runtime model's UV ints equal the file's words, so the floats are exact multiples of `2^-16`; the loader
  `0x09ae00` was not traced;
* no texture-animation remap (material flag `0x8000000` or node `+0x130 & 0x1000000`, `0x0e0700`) applies. The IRE
  file material flag is `0x2000000` and lacks `0x8000000` [i].

It does not cover the traversal fallback of section 3.

## 3. One body per draw; suppression; the traversal fallback (question 3)

**One body per batch draw.** `0x085390` finds the batch by `+0x0c == node +0x144` and creates one per body. Within a
batch, `0x0852d0` keeps one material record, with its own VB, per texture id. So a draw holds the instances of
exactly one body and one texture. The IRE body is shared by four TBullets rows: 0 `SS_BULLET_IRE`, 37 and 38 (the
cluster-flak fragments, size 12, flag `0x100`) and 39 (`FLAK_ALPHA_FRAG`). Their bolts land in the same draw [s data].
A per-body rule therefore replaces all four, which matches the design note's family decision (all four rows are
fighter tracers).

**Suppressing the IRE batch leaves the others intact.** Each (batch, texture) has its own call, VB, Lock and draw. The
submit sets everything it uses itself, every call: technique, texture, matrix, declaration, stream source, indices and
the two `[0x1aae78]` constants. So skipping the IRE call, or dropping its draw, changes no other body's state. The
skipped VB is not read by anyone else [s]. One effect-side difference remains: the cached setter `0x0da490` keeps
the last texture per (effect, handle). Skipping a call leaves that cache as the previous batch set it. That is the
state the next batch would find anyway.

**The traversal fallback.** In a given `0x0978e0` call the batch path takes an IRE bolt only when all three of
these hold:

* the body's batch has **at least two** bolts in that layer (`0x085793`: count < 2 gives priority 0);
* the batch fits the **`0x78f0`-vertex budget** of the selected batches, ranked by priority (`0x0857d2`);
* the bolt is **within capacity** (`0x085873`).

Otherwise `0x0857f0` returns 0 (`0x08586b`, `0x085876`), and the bolt is distance-culled and traversed by `0x0968c0`
(`0x097aa1`/`0x097ac5`). Traversal draws it through the ordinary material submit, `0x0e6ef0` → `0x0e7142` →
`0x0e0f30` ([material-submit.md](material-submit.md) section 1). That draw uses the body's own material:
`effects.fx` technique index 1 per the file [i], ONE/INVSRCCOLOR, and the body's own buffers. Its shader pair is not
the INSTANCE_BULLETS pair [i]. **A lone IRE bolt is therefore common** (the first shot of a burst, a single distant
shooter) [i]. A batch-only suppression would show the game's bolt beside the stage's for those frames. That draw runs
inside the FL object-trace scope, whose `current()` reads node `[0, 0x154)`, which includes the body id at `+0x144`.
So the D3D hook can drop it on a CPU-side test it already has.

## 4. The bullet record fields and the bolt length rule (question 4)

All offsets are the AP `layout::` / `bolt_layout::` constants of `src/proxy/weapon_effects_core.h` and
`weapon_effects_bolt.h`, identical on FL ([weapon-effect-spawn.md](weapon-effect-spawn.md) section 3). The flight
site `0x014eaa` passes the bullet in EDI, and `[esp+8]` at the spawner.

| Field | FL read | Unit and writer |
| --- | --- | --- |
| position | node (`obj+0x70`) `+0x30..+0x38`, int32 | engine units (500 per metre, the catalogue convention [i]). It is the **body origin**: the IRE mesh is symmetric in z (−1..+1), so the drawn bolt is centred on it, not headed by it [i] |
| velocity | node `+0x100..+0x108`, int32 | **engine units per second**. The fire path writes a 16.16 direction (the forward row [i]) × col 11 plus the shooter's `obj+0x60..` + node `+0x100..` (`0x04f738`, `0x04f891`, `0x04f8ce` → `0x012c00`, store `0x04f915`); `obj+0x10/+0x14` = its length (`0x04f960`). The mover scales it by the frame step cfg `+0x704` (ms) / 1000 (`0x014da1..0x014e05`). The IRE magnitude is 581,000 plus the shooter's velocity, about 1,162 m/s [i] |
| axis | the velocity direction; node basis row 2 at `+0x60` (16.16) as the fallback | unit vector |
| birth | ext (`obj+0x50`) `+8` = cfg `+0x708` (`0x049b4d`) | game ms |
| end | ext `+0` = cfg `+0x708` + col 10 (`0x04f96b..0x04f985`, fire path) | game ms. Creation first writes now + col 10 + col 27 (`0x049b24..0x049b3d`), and the fire fill overwrites it. **Remaining life = ext+0 − cfg `+0x708`**; IRE col 10 = 1,200 ms |
| body id | node `+0x144` = word TBullets `+0x0` (`0x049b70` → `0x0a35e0`, store `0x0a365d`) | the key of section 5 |
| ext `+0x78` (the tap's `length`) | ordinary bullets: col 11 · `0x1999` / node `+0x70` (int64 product, truncating `idiv`, `0x04a7b4..0x04a7da`); node `+0x70` = 100 when ≤ 0 (`0x04a6de`) | 16.16 model length, see below |

**Bolt length rule: corrected.** The `0x1999` term is `0.1 · 2^16`, so ext `+0x78` is the distance flown in **0.1 s
divided by the node scale**, not multiplied by it. It feeds `0x098140(node; w = col 16, h = col 17, len, record)`
(`0x04a7f6`). That routine builds two crossed quads into **body `+0x5c` once per body**, from the first bullet of that
body only, and sets node `+0x12c |= 0x1000000` (`0x0982b6`). The reader of body `+0x5c` is the collision pair test
`0x0982d0` (`0x0982d8`, `0x0982e3`, reached from `0x0a61a0`, gated by node `+0x12c & 0x1000000` at `0x0a620b`). No
draw path reads it: an after-call scan of every `0x0a1a40` (body lookup) call finds only the three creators
`0x097b20`/`0x097f00`/`0x098140` and that reader [s, scan of 30 instructions, so not exhaustive].

So the speed-proportional quad is the bullet's **collision hull**. In world space, after the node's ×`+0x70`, it
spans about 0.1 s of flight: 58,100 units = 116 m for the IRE [i]. The design note's "about 116 m dart" is that hull.
The **drawn** bolt is the body mesh transformed by `0x0e8600`: the basis rows `+0xc0..+0xe8` (16.16) × node scale
`+0x70` × axis scales `+0x80/+0x84/+0x88` (16.16) × scene `+0x2c` (XMM2, `0x0e08bb`), plus the translation
`+0xb0..+0xb8`. Its size is fixed per body and has **no speed term**. Its world length in metres needs the runtime
node scales [u]. `length_cap` should therefore be defined against the drawn mesh length or a pixel figure, not
against ext `+0x78`.

## 5. Questions

**Q1. Is per-body replacement feasible on FL, and does it need a CPU-side read?** Yes, and it needs a CPU-side read
for completeness. Both reads are engine hooks that use documented Win32/D3D9 mechanisms only (an E8 call-site claim;
the existing TLS scope), so they are as portable to native Windows as the rest of the FL hooks.

* The **instanced** draw has a D3D-only signature: the INSTANCE_BULLETS pair, stride 24, period 66, and the IRE's UV
  set in the locked prefix (section 2). It rests on two [u] assumptions and costs a 132-float compare per candidate
  draw.
* The **traversal fallback** draw (section 3) is not an INSTANCE_BULLETS draw. There the identity is the object-trace
  scope's node `+0x144`, already published for FL TAA. **A D3D-only rule alone would leave lone IRE bolts drawn by
  the game.**

**Q2. The cheapest safe way to know "this draw is the IRE batch".** Do not test it at the draw. Claim the call
**`0x097af8`** (`e8 43 90 04 00`, rel32 → `0x0e0b40`) and test **`[EDX+0x0c]` == IRE body id** before forwarding.
When equal and the stage owns the frame, skip the call (stub `ret`, EAX = 1). Otherwise forward ECX/EDX and the two
stack words unchanged. This saves the IRE's Lock, memcpy and effect work, and the D3D hook never sees the draw. Read
the IRE body id once per session from word `+0x0` of TBullets row 0. The table pointer is at module base + slot RVA
`0x1a83c8`; class 0 is the bullets table and the row stride is `0xdb8`. Rows 37 to 39 name the same body, so they resolve to the same id [i]. Pair it
with the scope test `current()` node `+0x144` == the same id at the material-submit draw. Together the two cover both
paths with one integer compare each.

Hook-site suitability of `0x097af8`:

| Check | Finding |
| --- | --- |
| instruction boundary | whole 5-byte call; window `0x097af0..0x097b07`, masked FNV-1a 32 `0xf86f890b`, no relocated dword |
| atomic claim | the five bytes lie in one aligned qword, so a `lock cmpxchg8b` claim works |
| branches and pointers | the only direct branch into the window targets its first byte `0x097af0` (from `0x097b05`); none lands in the call bytes; no image dword names them |
| callers | the target has 1 E8 caller |
| live at entry | ECX = scene and EDX = batch (callee inputs), `[esp]` = material, `[esp+4]` = view; EBX = batch, used at `0x097b07`; ESI = material, used at `0x097afd`; EDI = view; EBP = the aligned frame |
| dead after return | EAX, ECX, EDX and EFLAGS. `0x097af0` reloads ECX/EDX, `add esp,8` writes the flags, and the five callers of `0x0978e0` overwrite ECX/EDX next and never test EAX (first instruction after each call) [s]; the whole exit path [i] |
| stack | caller-cleaned (`add esp,8`), so the skip path is a plain `ret` |
| preserve | EBX/ESI/EDI/EBP and LastError. `0x0e0b40` uses XMM internally, so the stub must use none, or save what it uses. No x87 value is live [i] |
| reentrancy | render thread only. `0x0e0b40` calls only the device, the effect, the VB and the engine helpers `0x0db5b0`, `0x1124f0`, `0x0da490`, `0x0e83b0` and `0x074ab0`, none of which reaches `0x0978e0` [i]. The proxy's own D3D wrappers run inside it and must not re-enter the stub |
| SEH | a test-and-forward stub needs no frame. A stub that publishes a scope for the draw hook instead needs the object-trace dispatcher's unwind cleanup |
| rate | per (selected batch, texture) per `0x0978e0` call: a few dozen calls per frame [i] |

**Q3. What differs from AP.**

| Topic | AP | FL |
| --- | --- | --- |
| flush | separate function `0x0046d080` (stdcall scene, view; `ret 8`), called at `0x0047eb7a` | inlined at `0x097ad9` |
| per-batch submit call | `0x0046d0a7` → `0x004bfd40`, cdecl (scene, batch, material, view), caller pops `0x10` | `0x097af8` → `0x0e0b40` with ECX = scene, EDX = batch, stack (material, view), caller pops 8 |
| draw | `call eax` at `0x004c008a` | `call [ecx+0x144]` at `0x0e0e62` |
| batch body id source | node `+0x140` | node `+0x144` (same batch `+0x0c`) |
| instancing exclusion | none | body id `0xe1` is never instanced (`0x08566f`) |
| collision length | `0x00469a30` muldiv call + `0x0047f0a0(w, h, len)` with EDI = node | inlined `imul`/`idiv` (`0x04a7cb`) + thiscall `0x098140(node; w, h, len, record)` |
| renderer globals | (AP slots) | relocated: device holder `0x1aaf94`, declaration `0x1aaed8` |

Unchanged on both: the records, capacity, the count ≥ 2 rule, the `0x78f0` budget, the DISCARD lock, stride 24, the
declaration and the technique switch.

## Unknown

- Whether the runtime model's UV ints equal the file words, which the D3D-only signature relies on. Settle it by
  logging the first 66 TEXCOORD0 pairs of one period-66 INSTANCE_BULLETS draw in a flight, or by tracing loader
  `0x09ae00`.
- Whether FL's `effects.fb` INSTANCE_BULLETS compiles to the AP hashes the proxy's bullet pair list holds
  (`screen_emission_admission.h`). This matters only for the D3D-only route.
- The meaning of texture record flag `0x2000000`, under which the previous texture stays bound.
- How often IRE bolts take the traversal fallback in a fight. Settle it by counting call-site skips against
  scope-test drops in the first flight.
- Which technique the traversal draw of the IRE material uses (`effects.fx` technique index 1).
- The drawn bolt's world length in metres: node `+0x70`, `+0x80..+0x88` and scene `+0x2c` at run time. Settle it by
  reading them for one tapped IRE bullet; the tap already holds the node.
- Whether any draw path reads body `+0x5c`. The after-call scan is not exhaustive.
- The IRE VB capacity (`0x400` assumed): the bullet node's `+0x12c` bits `0x800` / `0x4000000` were not traced.
