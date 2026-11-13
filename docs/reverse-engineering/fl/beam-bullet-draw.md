# FL beam bullet draw: per-frame placement, stretch, fade and the draw path

Bounded offline contract, 2026-11-13, for re-shading beam-type bullets (TBullets col 12 bits `0x2` beam A, `0x4` beam
B, `0x8` fixed length, `0x2000` continuous). Inputs: `X3FL.exe` SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8` (`/mnt/c/X3FL/X3FL.exe`), `X3AP.exe` SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` (`/mnt/c/X3AP-orig/X3AP.exe`, the same bytes as
`/mnt/c/X3/X3AP.exe`) for the twins, the FL installation's catalogues (`/mnt/c/X3FL`, root then `addon2`). **Every FL
address is an RVA** (VA at the preferred base `0x400000` minus `0x400000`); AP addresses are VAs. **[s]** static
instruction or data evidence, **[i]** inference (a decompiler reading or reasoning not checked instruction by
instruction), **[u]** not established. No production edit, no build, no Wine, no game. A fresh Ghidra 12.1.4 import of
the FL EXE and the existing AP project `~/x3-ghidra-research` / `X3Render` (read-only) supplied the decompiles; the
project and all raw output stay in the session scratchpad (untracked).

Builds on [weapon-effect-spawn.md](../weapon-effect-spawn.md) (AP: the beam flag table, the fire routine, `ext` layout),
[weapon-effect-spawn.md](weapon-effect-spawn.md) (FL sites), [bullet-draw-batch.md](bullet-draw-batch.md) (the batch,
its admission, the lone path, the INSTANCE_BULLETS pair), [classic-materials.md](classic-materials.md) and
[muzzle-flash-draw.md](muzzle-flash-draw.md) (the classic material submit and its blend).

**Answer.** A flag-2 beam (PAL, Tri-Beam, Fusion Beam, Tug, Kyon α/β/γ, DUMMY2, Repair, PBC) is re-placed and
re-stretched **every frame** by the refresh `0x0514f0` (AP `0x00447ff0`), called from the sector object update at
`0x0641a2`, and again on each fire tick. The refresh re-derives the position from the **owner ship's node**:
`node+0x30 = ship.pos + R(ship) · ext+0x10 + L · forward`. It sets the axis scales to **(1, 1, L · 2^16 / node scale)**
through `0x0a3ae0`; the node scale itself stays at the body's object size. Every beam body's mesh spans z −1..+1, so
the drawn body runs from the muzzle to muzzle + 2L along the shot. **L = range / 2**, where range = col 11 × col 10 /
1000, the speed times the lifetime (`0x04dcd0`). In a frame where the hit test `0x066660` found a target, L is half the
muzzle-to-hit distance instead. `ext+0x78` plays no part in it.

The refresh also writes the node alpha `+0x13c`: a fade-in from 16 over the first quarter of the life, a fade-out to 1
over the last quarter, and a flicker of × (0.8..1.0) every frame. Node `+0x124` (col 26) is the bullet's **sound
volume**, not a draw input.

Nothing else differs from a plain bolt at the draw. The same two paths draw it, with the same matrix (`0x0e8600`, which
honours `+0x80..+0x88`):

* the batch `0x097af8` → `0x0e0b40` (effects `INSTANCE_BULLETS`), whenever two or more nodes of the body are live in a
  layer and the budget allows;
* otherwise the lone material submit `0x0e7142` → `0x0e0f30`.

The batch draws a **classic** (MATERIAL3/5) bullet body too. It then uses the `effects` INSTANCE_BULLETS pair with the
classic diffuse texture, unlit, ONE / INVSRCCOLOR, and the fade in COLOR0.a. Only alone does a classic beam draw as
`standard_lighting` DEFAULT. That VS has no vertex colour and applies `g_TexMatrix` (c37..c38) to the UV. Its fade
`g_AlphaValue` (c39.x) = node `+0x13c` / 255 lands only in COLOR.a, so under the classic ONE / ONE blend it does not
reach the frame [i].

## Reproduce

```sh
python3 -I verification/results/fl-beam-bullet-draw/fl_beam_bullet_draw.py \
  > verification/results/fl-beam-bullet-draw/fl_beam_bullet_draw_out.txt     # about 2 s; exit 0 = every anchor holds
python3 -I verification/results/fl-beam-bullet-draw/beam_program_inputs.py \
  > verification/results/fl-beam-bullet-draw/beam_program_inputs_out.txt
```

[`fl_beam_bullet_draw.py`](../../../verification/results/fl-beam-bullet-draw/fl_beam_bullet_draw.py) →
[`_out.txt`](../../../verification/results/fl-beam-bullet-draw/fl_beam_bullet_draw_out.txt) checks both hashes.
It asserts **100 FL and 13 AP whole-instruction anchors, 0 failures**: every instruction this note quotes. It then
prints:

* the TBullets beam rows (range, L, col 18, col 26);
* the text-body census of the four beam bodies the batch census could not parse: the loader-normalised z range, the
  object size and the faces per material.

[`beam_program_inputs.py`](../../../verification/results/fl-beam-bullet-draw/beam_program_inputs.py) →
[`_out.txt`](../../../verification/results/fl-beam-bullet-draw/beam_program_inputs_out.txt) covers the FL 3_0 archive
pairs: `standard_lighting` / `standard_lighting2s` DEFAULT and `effects` INSTANCE_BULLETS. For each it prints the
declarations, the CTAB names and the instructions that write an output lane, with operands named by CTAB, and no
bytes. Both outputs are byte-identical on a rerun. The pbd bodies' figures (faces, sub-mesh limit, z range,
`lod0_value`) are those of [`fl_bullet_draw_batch_out.txt`](../../../verification/results/fl-bullet-draw-batch/fl_bullet_draw_batch_out.txt).

## 1. Per-frame order and the refresh sites

| RVA | Site | What runs |
| --- | --- | --- |
| `0x043b42` | `call 0x066de0` | **beam collision pass**: every live object of list 0 whose TBullets row has `0x2` (`0x066e8f`) gets `0x066660` (`0x066ea1`) and, on a hit, `0x067f10` (hit processing) [s call; i role of `0x067f10`] |
| `0x043b76` | `call 0x063000` | **sector object update** (AP `0x004596e0`); its class-0 branch picks the per-flag refresh below |
| `0x0641a2` | `call 0x0514f0` | flag `0x2`: beam A refresh, every frame [s] |
| `0x0643d9` | `call 0x0517f0` | flag `0x4`: beam B (chain) refresh, every frame [s] |
| `0x0642a5` | `call 0x051430` | flag `0x2000` while charging (ext `+0x74 & 2` and now < ext `+0x90`) [s call, i condition] |
| `0x04f52a` / `0x04f56d` / `0x04f57c` | fire routine `0x04e310`, new beam A shot | refresh, hit test, refresh [s] |
| `0x04faf9` / `0x04fb32` / `0x04fb3d` | fire routine, live beam (laser `ext+0xc`) when the muzzle offset or aim changed | it writes ext `+0x10` (muzzle offset), `+0x50..` and the relative orientation `+0x20..`, then refresh, hit test, refresh [i: decompile] |
| `0x04f593`, `0x04fb6e` | fire routine | beam B refresh, new shot / live beam [s] |

So a beam's draw sees the state of the refresh in the same frame's update, after that frame's hit test. The fire
routine stores the gun's muzzle offset and aim in the ship frame. The per-frame refresh carries them along with the
ship between fire ticks, so a turning ship keeps the beam on its gun. A turret's own turn shows only at the next fire
tick that changes the offset or aim [i].

## 2. Beam A geometry: the writers of position, scale and axis scales (question 1)

Refresh **`0x0514f0`**: fastcall, ECX = bullet, plain `ret`. It returns at once unless TBullets col 12 has `0x2`
(`0x05150d`), the owner `ext+4` is set and the owner has a node.

| Step | RVA | Field and formula | Tag |
| --- | --- | --- | --- |
| muzzle | `0x05153a`, `0x05153e` | node `+0x30` = R(ship node `+0x40` basis) · **ext `+0x10`** (the muzzle offset in the ship frame, stored by the fire routine) via `0x10e0c0` | s (call, operands); i (routine roles) |
| orientation | `0x051555`, `0x051559` | node `+0x40..+0x6c` = **ext `+0x20`** (relative orientation) composed with the ship basis via `0x10ebe0` | s / i |
| ship position | `0x05158c` | node `+0x30..+0x38` += ship node `+0x30..+0x38` | s |
| range | `0x05159e` → `0x04dcd0` | range = TBullets `+0x54` (col 11, speed) × `+0x50` (col 10, life ms) / 1000 (`0x04dcf0`, `0x04dcf7`, `0x04dd04`); 0 for a non-class-0 object | s |
| half length | `0x0515ae` | **L = range / 2** | s |
| hit | `0x0515b0..0x05161f` | if ext `+0x74 & 1` (set by the hit test this frame): clear it; d = \|node `+0x180..+0x188`\|, negated when `+0x188 ≤ 0` (`0x0515f7`); **L = (bullet `+0xa4` + d) / 2**, at least 1 | s |
| keep | `0x0516a5` | bullet `+0xa4` = L (the next frame's "previous L") | s |
| alpha | `0x051660..0x05169b` | node `+0x13c`: age = now − ext `+8` < col 10 / 4 → `0x019fc0` ramp **16 → 255**; else remaining = ext `+0` − now < col 10 / 4 → ramp **1 → 255**; else 255 | s (branch, constants); i (`0x019fc0` = linear clamp, AP `0x0042fb20` read) |
| flicker | `0x0516b4..0x0516e7` | node `+0x13c` ×= (65536 − rand · 0x3333 / 0x8000) / 65536, i.e. × 0.8..1.0, rounded | s |
| **axis scales** | `0x0516f1..0x051739` | z = **(L << 16) / node `+0x70`** (16.16); if L / 16384 > node `+0x70`, z = `0x40000000` (16384.0); `0x0a3ae0(node; 0x10000, 0x10000, z)` writes **`+0x80` = `+0x84` = 1.0, `+0x88` = z**, `+0x8c` = 0, radius `+0xa0` = max · scale, `+0xa4` = −1, and recurses into children with `+0x12c & 0x40` | s |
| centre | `0x051743..0x051797` | node `+0x30..+0x38` += L × node row 2 (`+0x60..+0x68`, the forward axis, 16.16) | s |
| draw matrix | `0x0517a2`, `0x0517b0..` | world translation `+0xb0..+0xbc` = `+0x30..`; world basis `+0xc0..+0xec` = `+0x40..+0x6c` | s |

After the refresh the update's flag-2 branch also writes node `+0x100..+0x108` = 4 · (centre − ext `+0x80..`, last
frame's centre) · 1000 / dt per component (zeroed for a component of a second vector at or above `0x83126`), then
ext `+0x80..` = the centre [i: decompile]. That is the beam's node velocity, which no draw path reads [i].

**Node scale `+0x70` is not written by the refresh.** It is the body's object size: the body bind `0x0a35e0` stores
LOD 0 `+0x14` (`0x0a3668`, `0x0a366b`), and the creation path makes it 100 only when that is ≤ 0 (`0x04a6de`). The
update's flag-2 branch calls nothing else that writes it [s, decompile of `0x063000`: the `0x0a3920` node-scale calls
sit in the plain, fixed-length and continuous branches only].

**Drawn length.** The draw matrix ([bullet-draw-batch.md](bullet-draw-batch.md) section 4; `0x0e8600` and the lone
submit's own build at `0x0e0f30`, decompile lines with `+0x70`, `+0x80..+0x88`, `+0xc0..+0xe8`, `+0xb0..+0xb8`)
scales a model z of 1 to node scale × z / 2^16 = **L**. The text records are normalised to max |coordinate| 1.0
([body-text-loader.md](../body-text-loader.md) section 4), and every beam body spans model z **−1..+1**:
v/10385, `tol_bullet` and `bullet_repair` in the batch census, and v/00525, 00526, 00527 and 00300 here. So the drawn
beam runs **from the muzzle (centre − L) to muzzle + 2L** along the gun's forward axis, in scene units after × scene
`+0x2c`. Its cross-section is the mesh x/y × the object size, with axis scales 1, unchanged from a bolt. Without a hit,
2L is the range (speed × life). With a hit, 2L is the muzzle-to-hit distance (section 3).

| Row (FL) | Weapon | Body | Range = 2L (units) | L | col 26 (volume) |
| --- | --- | --- | --- | --- | --- |
| 17 | PAL | v/10385 | 3,188,445 | 1,594,222 | 255 |
| 26 | Tri-Beam | v/10385 | 3,096,500 | 1,548,250 | 255 |
| 27 | Fusion Beam (`0x802`) | v/10385 | 3,316,647 | 1,658,323 | 255 |
| 25 | PBC | `TOL_bullet` | 2,927,100 | 1,463,550 | 255 |
| 29 | Tug | v/00525 | 1,000,000 | 500,000 | 255 |
| 31, 32 | DUMMY2, Kyon α | v/00525 | 899,098 | 449,549 | 180 |
| 33 | Kyon β | v/00526 | 1,709,008 | 854,504 | 255 |
| 34 | Kyon γ | v/00527 | 2,428,600 | 1,214,300 | 255 |
| 35 | Repair (`0xa2`) | `bullet_Repair` | 1,100,000 | 550,000 | 180 |

All [s data, producer]. At 500 units per metre that is 1.8 km (Kyon α) to 6.6 km (Fusion) [i: the catalogue
convention]. The `0x40000000` clamp is never reached by these rows: L / 16384 ≤ 102, against object sizes of at least
870 (`TOL_bullet` `lod0_value` 870, v/00525 1000) [s data].

**`ext+0x78`** for flag 2 is written once at creation and feeds only the collision quad ([bullet-draw-batch.md](bullet-draw-batch.md)
section 4); no draw path reads it [s, as there].

## 3. The hit truncation (question 1, length source)

The hit test **`0x066660`** (ECX = bullet; AP `0x0045cab0`) runs per frame from the collision pass and on fire ticks.
It moves the node temporarily so that its centre sits at range / 2 along the shot. It then collects up to 32 nearby
objects, sorts them, and runs the pair test `0x0a61a0` (which returns the hit z in node `+0x188`) on each, keeping
the nearest. It restores the node. On a hit it sets:

* ext `+0x74 |= 1` (`0x066ae9`) and ext `+0x7c` = the victim (`0x066af0`);
* node `+0x180` = `+0x184` = 0 (`0x066af6`, `0x066b03`);
* node `+0x188` = hit z − bullet `+0xa4` + range / 2 (`0x066b14..`).

It then calls `0x066b50(victim)` [s stores; i ordering and roles from the decompile]. The next refresh (`0x0515b0..`)
then makes L = (L_prev + hit z − L_prev + range / 2) / 2 = **D / 2**, with D the muzzle-to-hit distance [i: algebra
over the stores]. The bit is consumed every refresh, so the beam is cut at the target only in frames whose collision
pass found one. In other frames it is full length [s for the clear; i for the visual consequence].

## 4. The other beam-like rows, and the plain bolt for contrast

| Kind | Refresh | Per-frame scale and position | Drawn shape |
| --- | --- | --- | --- |
| plain bolt | update `0x0644f8..0x064549` | `0x0a3920(min(node +0x70 + speed · dt / 2000, LOD 0 +0x14))` (`0x06450e`, `0x064536`, `0x064549`): **resets axis scales to 1** and, since the node starts at the object size, keeps it there; fade-out 1 → 255 over the last third of the life (`0x064562..0x064579`) | mesh × object size, moved by its velocity [s] |
| beam A `0x2` | `0x0514f0` | section 2 | stretched to 2L along the shot, at the gun [s] |
| beam B `0x4` (Ion Disruptor, v/00300) | `0x0517f0` (AP `0x004482a0`) | root (bullet node): scale = (1000 << 16) / col 18, axis z = col 18 (`0x0519d1`, `0x0519f5`), so the half-length is 1000; then up to 20 child segments (created by `0x0a2200`, same body id, `0x0a35e0`) chained toward the nearest objects within the range: each segment scale = (d << 16) / col 18, axis z = col 18 (`0x051ea2`, `0x051ed7`), so its half-length is d = half the distance to the next target (10,000 for a free segment), with a random jitter of roll and direction (`rand`) | a jagged chain of z-stretched copies of the body [s calls; i chain semantics] |
| fixed length `0x8` (PSG, v/00225) | update `0x064440..0x0644d9` | uniform scale = t² · range + 100 (t = elapsed / life, 16.16), alpha ramped by the remaining life every frame, collision length = scale × ext `+0x78` | a uniformly growing shell [i: decompile] |
| continuous `0x2000` (flak, PPC, IonC, IBL, PSP, DUMMY1) | `0x051430` (AP `0x00447f20`) while charging | held at the gun: position re-derived from the ship (orientation unchanged); node scale = object size · (1 + col 35 · elapsed / col 27) (`0x064354`); then released as a bolt | a charged orb, uniform scale, **no axis stretch** [s calls; i semantics] |

So the only draw-side difference of a beam from a plain bolt is the node's state: axis scales (1, 1, z ≠ 1), a
position re-derived from the gun each frame, and the per-frame alpha. Path, programs, vertex layout and matrix code
are the same [s for the matrix code reading `+0x80..+0x88` on both paths].

**Node `+0x124` is the sound volume.** The update writes `+0x124` = TBullets `+0x90` (col 26) for every class-0
object (`0x064cce`, `0x064cd5`); for flag 2 it is scaled by node `+0x13c` / 255 (`0x064cf5..`). Its reader `0x095930`
passes it twice with `+0x138 + 4500` (`0x095a45`, `0x095a4c`, `0x095a52`) to `0x0f6750`. That routine scales it to
`vol · 5000 / 256 − 5000` and calls the DirectSound buffer's vtable `+0x3c` (`IDirectSoundBuffer::SetVolume`) [s offsets;
i role]. Node `+0x138` = 10,000 for flag-2 beams (`0x04b173`, AP `0x00441c7e`) is therefore a sound distance parameter.
The AP note's "glow/alpha [i]" for col 26 ([weapon-effect-spawn.md](../weapon-effect-spawn.md) section 1) should read
"sound volume". Neither field is a draw input.

## 5. The draw path of a beam and what each program reads (question 2)

**Admission to the batch** (`0x085660`, called by pass 1 `0x085700` and pass 2 `0x0857f0` for nodes with
`+0x130 & 0x800000`, which every bullet carries). It refuses **body 225** (`0x085668`, `0x08566f`: the PSG shell
v/00225) and any body with an LOD sub-mesh of more than `0x37` triangles (`0x0856c0`). It does not test the material
type. A beam is a single node per gun, so a body's beams are batched when **at least two nodes of that body** are in
the view layer and the `0x78f0`-vertex budget admits it. Otherwise the beam is traversed and drawn alone
([bullet-draw-batch.md](bullet-draw-batch.md) section 3). Two PAL-family guns firing (rows 17, 26 and 27 share
v/10385) batch; a single beam is a lone draw [i: by the count rule].

**The batch for a classic body.** Pass 2 takes the texture from the classic record: a MAT3 body's global Materials row
`+0xc`, or a body record's `+2` diffuse id when body `+0x50 & 0x10` (decompile of `0x0857f0`). The submit then selects
the effect at `0x0e0c1f..0x0e0c64`. It uses the material's own effect name only for body `+0x50 & 0x10` with an
effect object at record `+0x5c` and a non-empty name (`0x0e0c25`, `0x074ab0` against the empty string `0x1740e0`).
Otherwise it uses **`"effects"`** (`0x0e0c4b`, `0x0e0c5f` → string `0x174344`), and a classic record has no effect
object ([classic-materials.md](classic-materials.md), "the classic branch clears `+0x5c`"). So a batched classic
beam is an **INSTANCE_BULLETS** draw (VS `5e484a06672e28fb`, PS `ec1f5c4a2f4e1445`, FL 3_0) [s code; s pair from the
producer]. It shows the classic diffuse texture, ONE / INVSRCCOLOR, with no lighting. Its vertices are transformed on
the CPU by `0x0e8600` with the axis scales, and COLOR0 = node `+0x13c` << 24 (0 → 255, `0x0e0886`). The PS is
`texel.rgb × COLOR0.a`, so the fade and the flicker reach the frame.

**The lone draw of a classic body.** The material submit `0x0e0f30` (AP `0x004c0150`) selects `standard_lighting`, or
its two-sided instance `standard_lighting2s` for `MPF_2SIDED`, technique DEFAULT
([muzzle-flash-draw.md](muzzle-flash-draw.md) section 3). Its classic blended branch sets `g_AlphaValue` = 1.0.
Then, in the common non-z-only block, when node `+0x13c` ≠ 0 (`0x0e4abd`) it does the following:

* ZWriteEnable = 0 and AlphaBlendEnable = 1 (handles `+0x148`, `+0x170`).
* Only if the material had not already enabled blending (`+0x1a4` = 0, `0x0e4b0e`): SrcBlend = SRCALPHA, and DestBlend
  = ONE unless it is ONE or INVSRCALPHA (`0x0e4b2c..`).
* **`g_AlphaValue` = `+0x13c` × 1/255** (`0x0e4ad9`, constant `0x191a88` = 0.0039216; handle `+0x188` cached at
  `0x0e3cc6` from `0x18fdf8` "g_AlphaValue"; SetFloat at `0x0e4b7d..`) [s].

Every beam body's classic material is `MPF_DESTINATIONBLEND` (`+0x1a4` = 1), so the blend stays the classic ONE / ONE
([muzzle-flash-draw.md](muzzle-flash-draw.md) section 3) [i: same rule, not re-traced per body].

| Program (FL 3_0) | Inputs | Fade and texture transform |
| --- | --- | --- |
| VS `494fe349b8bc12ec` (vs_3_0, both instances) | `v0` POSITION, `v1` TEXCOORD, `v2` NORMAL: **no vertex colour**; CTAB `g_LightPoint` c0..c23, `g_nNumLightPoint` i0, `g_mWorldViewProjection` c24..c27, `g_mWorld` c28..c30, `g_mWorldIT` c31..c33, `g_mViewInverse` c34..c36, **`g_TexMatrix` c37..c38**, **`g_AlphaValue` c39**, `g_MatEmissiveColor` c40, `g_FogClip` c41, `g_EnableFog` b0 | COLOR (o1).xyz = point-light sum + `g_MatEmissiveColor`; **o1.w = `g_AlphaValue` · fog factor** (or `g_AlphaValue`); TEXCOORD0 (o2).xy = (u, v, 1) · `g_TexMatrix` (identity unless the node has a texture-animation record for this texture, `0x0e0f30` decompile, `0x0d9580("g_TexMatrix", …)`) [s interface; i branch roles] |
| PS `ced7edef48059e84` (one-sided; v/00525, 00526, 00527, 00225) / `0d5848c8489b6630` (2s, `MPF_2SIDED`; v/10385, v/00300, v/10458) | `v0` COLOR (xyz saturated as the vertex light, w the alpha), `v1.xy` UV, `v2..v4` light/eye vectors, `vFace` (2s); s0 diffuse, s1 specular, s2 light map, s3 cube; c4 `g_EnableGlow`, c5..c8 `LightDir_*`, c9..c13 `g_Mat*` (incl. `g_MatQualityColor` c12) | **oC0.rgb = lighting sum; oC0.a = lerp(texel.a, light.a, `g_EnableGlow`) × v0.w**: the fade reaches only the output alpha, so under ONE / ONE it does not change the frame [s writes; i visual consequence] |
| INSTANCE_BULLETS VS `5e484a06672e28fb` / PS `ec1f5c4a2f4e1445` (batch) | world-space POSITION, TEXCOORD0, COLOR0 (alpha = node `+0x13c`); `g_mViewProjection` c0..c3 only | `oD0 = v2`; PS `texel.rgb × v0.a`; no texture matrix |

**The effects bodies** (`TOL_bullet`, `bullet_Repair`) take the same two paths. Batched, they are the same
INSTANCE_BULLETS draw. Alone, they are the effects DEFAULT pair `d5e1c753…` / `8360f422…`
([bullet-draw-batch.md](bullet-draw-batch.md) section 7): fade = `g_AlphaValue` in `oD0.x` and `g_TexMatrix`, here
with `g_AlphaValue` = node `+0x13c` / 255 from the same common block. The blend is the material's: ONE / ONE for
`TOL_bullet`, ONE / INVSRCCOLOR for `bullet_Repair` (section 7's census). So the lone effects beam **does** show the
fade in RGB (`texel × v0.x`).

| Body | Rows | Material | Faces (largest sub-mesh) | Batch-eligible | Lone pair |
| --- | --- | --- | --- | --- | --- |
| v/10385 | PAL, Tri-Beam, Fusion | MAT5, tex 796, `DESTINATIONBLEND \| 2SIDED` | 16 (16) | yes [s] | `494fe349…` / `0d5848c8…` |
| v/00525 | Tug, DUMMY2, Kyon α | MAT3, tex 155, `DESTINATIONBLEND` | 48, one material | yes [i: sub-mesh = faces per material] | `494fe349…` / `ced7edef…` |
| v/00526 | Kyon β | MAT3, tex 155 | 48, one material | yes [i] | `494fe349…` / `ced7edef…` |
| v/00527 | Kyon γ | MAT3, tex 155 | 94, one material | **no** if that is one sub-mesh (> 55) [i]: always lone | `494fe349…` / `ced7edef…` |
| `TOL_bullet` | PBC | effects.fx, ONE / ONE | 16 (16) | yes [s] | effects DEFAULT |
| `bullet_Repair` | Repair | effects.fx | 24 (24) | yes [s] | effects DEFAULT |
| v/00300 | Ion Disruptor (beam B), Starburst | MAT3, tex 70, `DESTINATIONBLEND \| 2SIDED` | 4 | yes [i] | `494fe349…` / `0d5848c8…` |

**Recognition consequences** (for a re-shade; nothing implemented here):

* A batched beam is already an INSTANCE_BULLETS draw inside the claimed `0x097af8` scope. The scope stores its body id
  in `x3m_bullet_body` ([bullet-draw-batch.md](bullet-draw-batch.md) section 6), so the bullet re-shade sees v/10385
  and v/00525 beams today and applies the bolt law to them. The stage texture table has no entry for their classic
  textures (796, 155), which means the 2.75 fallback normaliser with one `bullet_reshade_texture` row [i].
* A lone classic beam binds the `standard_lighting` DEFAULT pair, which **every** opaque classic body shares. As for the
  muzzle flash body 445 ([muzzle-flash-draw.md](muzzle-flash-draw.md)), it can be told apart only CPU-side: the FL
  object-trace scope's node `+0x144` (body id) inside `0x0e0f30`, plus ADD ONE / ONE. Its inputs are those of the
  table above. The fade is in c39.x (`g_AlphaValue`) as the application wrote it. The geometry needs no engine read:
  the stretch is already in `g_mWorld` (c28..c30) through the axis scales.
* The beam's axis in a program is model z (`g_mWorld` column 2), with the muzzle at model z −1 and the far end at
  +1 [s geometry; i for a hit frame, where +1 is the hit point].

## 6. AP counterparts (question 3)

Resolved by direct comparison: the same flag tests, constants, field offsets and call shape in both decompiles and
anchors. Only the AP rows marked "label" come from `tools/analysis/x3ap_function_labels.json`, whose 310 labels name
3 of these.

| FL RVA | Role | AP VA | How |
| --- | --- | --- | --- |
| `0x0514f0` | beam A refresh | `0x00447ff0` (cdecl `[esp+4]`, `ret 4`; FL fastcall ECX) | AP note; anchors `0x0044800d`, `0x0044809e`, `0x004480ab`, `0x00448172`, `0x004481f0`, `0x0044820f` [s] |
| `0x0517f0` | beam B refresh | `0x004482a0` | AP note; same structure (col 18, 20 segments, `0x3e80000`) [i] |
| `0x051430` | continuous (`0x2000`) hold at the gun | `0x00447f20` | update branch position [i] |
| `0x04dcd0` | range = col 11 × col 10 / 1000 (FL ECX, AP EAX) | `0x00444bd0` | decompile [s] |
| `0x066660` | beam A hit test | `0x0045cab0` | `or [eax+0x74],1` at `0x0045cefa` [s] |
| `0x066de0` | per-frame beam collision pass | `0x0045d250` (caller `0x0043a38f`) | caller of `0x0045cab0` [s] |
| `0x063000` | sector object update | `0x004596e0` | label; `0x0045a809` → `0x00447ff0`, `0x0045aa6c` → `0x004482a0` [s] |
| `0x0a3ae0` | axis-scale setter (`+0x80..+0x88`) | `0x00488270` | label; `0x00488286`, `0x00488292` [s] |
| `0x0a3920` | node-scale setter (`+0x70`, axes reset to 1) | `0x004880e0` | `0x0048812d` [s] |
| `0x0a35e0` | body bind (`+0x70` = LOD 0 `+0x14`) | `0x00487e30` | label ("object body bind") [i: the `+0x14` store not re-read on AP] |
| `0x04e310` | fire routine | `0x00445170` | AP note [s] |
| `0x085660` | batch admission (body 225, ≤ `0x37` triangles) | `0x0046ce20` | `cmp [esi+0x140],0xe1` at `0x0046ce58` [s] |
| `0x0857f0` / `0x0e0700` / `0x0e0b40` | batch pass 2 / instance writer / submit | `0x0046cef0` / `0x004bf960` / `0x004bfd40` | [effects-engine-remaining-emission.md](../effects-engine-remaining-emission.md) [s there] |
| `0x0e0f30` | material submit (node alpha → `g_AlphaValue`) | `0x004c0150` | [dust-light.md](dust-light.md) [s there]; the AP alpha block not compared [u] |
| `0x0e8600` | node draw matrix | not resolved | — |
| `0x095930` | node sound (volume `+0x124`) | not resolved | — |

## Unknown

- Whether a beam B root that the batch takes still draws its chain: the batch collects only the root's own LOD, and a
  taken root is not traversed [i]. This would need two Ion Disruptor beams in one layer, and whether that occurs in
  play is not established [u].
- The sub-mesh split of the text bodies v/00525..00527, which decides the 55-triangle test. Settle it by tracing the
  text loader's group builder, or with a flight's `bullet_reshade_lone_body` rows for body 527 [u].
- Whether the fire routine extends a live beam's expiry ext `+0`. No write was found in its live-beam branch, so a beam
  lives col 10 ms with the quarter-life fades and is respawned by the next fire tick after expiry [i].
- The exact roles of `0x067f10` (hit processing), `0x066b50` and `0x0a61a0`, beyond the stores quoted [i].
- `0x019fc0`'s exact interpolation (taken from its AP twin `0x0042fb20`'s reading) [i].
- How often beams take the batch versus the lone path in a fight [u]; a flight's `bullet_reshade_frame` counts per body
  would settle it.
