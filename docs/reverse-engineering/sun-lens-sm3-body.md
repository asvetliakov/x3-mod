# The AP sun lens body as an SM3 pair: register roles and the clip-wrap plan

> **Implementation cancelled (2026-11-02):** the own sun replaced the lens wraps ([own-sun-pass.md](../architecture/own-sun-pass.md) section 14, step 2); the SM3 wrap plan below was never merged and stays as reverse-engineering record only.

2026-11-01. Offline only: the proxy's dumps `vs_494fe349b8bc12ec.bin` (2,104 B, 526 words) and
`ps_7c83ed50c9894e44.bin` (5,192 B, 1,298 words) under `/mnt/c/X3/x3-modern-captures/`, read in place with the
token parser of `verification/results/hull-widen-native-validator/sm3_rules.py` and a CTAB reader; FL programs from
`/mnt/c/X3FL/x3-modern-captures/` and its archives. No game launch, no Wine run, no production code change; no shader
words or disassembly listing are tracked here. Owning feature: [sun partial occlusion](../architecture/sun-partial-occlusion.md);
the flight evidence that motivates this note is the 2026-11-01 run216 entry of
[the sun-occlusion ledger](../verification/sun-occlusion.md) and `verification/results/run216-sun-occlusion/`.

**Answer: the pair takes the existing clip pair with a change only in where the wrap declares and addresses the extra
interpolator.** The vertex program writes its position with four `dp4 o0.<lane>, r1, c24..c27` at depth 0, with
`r1 = (v0.xyz, 1)` built by the mad shape that `origin_known` already recognises. Six of its twelve outputs are free. The
pixel program is a flat ps_3_0 (no flow control, no `texkill`, no predication) with five inputs, four samplers and
constants c0-c12. The wrap's four `def` registers, two samplers and six temporaries therefore have room without
collisions. A token-level prototype of the plan below passes the native D3D9 validator for the AP pair and the FL
equivalent with no messages, and four broken variants of it fail with the expected messages (section 5). Measured
refusal today: 4,114 `sun_lens_body_unclassifiable ... reason=no_free_texcoord` / `other_reason=no_free_texcoord` rows for
this pair in `/mnt/c/X3/x3m.log` (`grep -a 494fe349 /mnt/c/X3/x3m.log | grep -ao 'reason=[a-z_]*' | sort | uniq -c`).

## 1. Vertex program `vs_494fe349b8bc12ec` (measured)

Version `0xfffe0300` (vs_3_0). Layout in words: a 298-word CTAB comment at 1, a one-word `PRES` comment at 300, `def c42`
at 302, nine `dcl` at 308-332, instructions from 335, END at 525. 62 instructions: 1 `def`, 9 `dcl`, **52 body
instructions** (6 mad, 10 dp4, 7 dp3, 9 mov, 5 mul, 5 add, 2 rsq, 2 rcp, mova, rep/endrep, if/else/endif); 57 slots
by the ps_2_0 slot table the wrap already uses (an estimate; the loop runs `i0` times). No predicated instruction; flow
control is one `rep i0` (the point-light loop, relative reads `c0[a0.w]`, `c1[a0.w]`, `c2[a0.w]`) and one `if b0`.

| Register | Declaration / role (names from the CTAB, D3DX 9.15.779) |
| --- | --- |
| v0 | `dcl_position0`: object position |
| v1 | `dcl_texcoord0`: uv |
| v2 | `dcl_normal0` |
| o0 | `dcl_position0 o0`, mask `.xyzw` (dcl token `0xe00f0000`): **the clip position** |
| o1 | `dcl_color0`: rgb = point-light sum + `g_MatEmissiveColor` (c40); w = `g_AlphaValue` (c39), faded by `g_FogClip` (c41) when `b0` |
| o2 | `dcl_texcoord0 o2.xy` (centroid flag set): uv through `g_TexMatrix` (c37, c38) |
| o3, o4, o5 | `dcl_texcoord1..3 .xyz`: view vector, world normal, reflection vector |
| c0-c23 | `g_LightPoint` (24), read relatively from `a0.w` = 3 x loop counter |
| c24-c27 | **`g_mWorldViewProjection`** (4 rows) |
| c28-c30 / c31-c33 / c34-c36 | `g_mWorld` / `g_mWorldIT` / `g_mViewInverse` (3 rows each; c34-c36 `.w` = camera position) |
| c42 | `def (1, 0, 3, 0)` |
| i0 / b0 | `g_nNumLightPoint` / `g_EnableFog` |
| temporaries | r0-r6 |

**Position:** `mad r1, v0.xyzx, c42.xxxy, c42.yyyx` at word 335 builds `(v0.xyz, 1)` (mul lanes (1,1,1,0), add lanes
(0,0,0,1)), then `dp4 o0.x/.y/.z/.w, r1, c24/c25/c26/c27` at words 450, 454, 458, 462, consecutive, at depth 0 (after
`endrep`, before `if b0`), unmodified sources, no relative addressing. This is exactly the four-dp4 shape and the
`origin_known` mad shape of `lens_visibility_vertex_variant`, with `matrix_register = 24`; the same dword offsets are in
the pair's `motion_output_profiles_inc.h` row (`ReferenceRegisters, 24, 1, {450, 454, 458, 462}`) and
`rigid_position_profiles_inc.h` tags it `MadXYZIdentityWFromX`. c24-c27 is the clip-row window the route already
shadows for most profiled pairs, so `window_of(24)` resolves (inferred from that row; the pair was never classified
because the scan refused it first: every run216 row has `matrix_register=-1 rows_known=0`).

**Free output space:** o6-o11 are undeclared (vs_3_0 has 12 output registers); texcoord semantic indices 0-3 are
declared, 4-15 free. Declared outputs with unused components: o2.zw and o3.w, o4.w, o5.w. The plan uses a whole new
register: the clip needs x, y and w.

## 2. Pixel program `ps_7c83ed50c9894e44` (measured)

Version `0xffff0300` (ps_3_0). Layout: a 185-word CTAB comment, `def c12` at 1091, nine `dcl` at 1097-1121, body
1124-1296, END at 1297. 52 instructions: 1 `def`, 9 `dcl`, **42 body instructions** (12 mul, 7 mad, 6 dp3, 4 add, 4
texld, 3 dp4, 2 nrm, 2 pow, lrp, mov): 47 arithmetic + 4 texture slots by the ps_2_0 table, i.e. about 10 % of the
512-slot spec minimum and 0.2 % of the 32,768 plan cap. **No `texkill`, no flow control, no predication, no `vPos` /
`vFace`.** `oC0` is written twice, both at depth 0: `add_pp oC0.xyz` at word 1289 and `mul_pp oC0.w` at 1293 (mask
union `.xyzw`, which `scan_pixel` requires). Every instruction carries `_pp`.

| Register | Declaration / role |
| --- | --- |
| v0 | `dcl_color0` (`_pp`): lit emissive + point lights (rgb), alpha |
| v1 | `dcl_texcoord0 .xy` (`_pp`, centroid) |
| v2, v3, v4 | `dcl_texcoord1..3 .xyz` (`_pp`): view vector, normal, reflection vector |
| s0 | 2D `DiffuseTexSampler` (the 2048² X8R8G8B8 texture of the lens draw, `tex0` in the run216 rows) |
| s1 | 2D `SpecularTexSampler` (32² DXT1 in the run216 rows) |
| s2 | 2D `LightMapTexSampler` (glow / light map) |
| s3 | cube `CubeMapTexSampler` |
| c0-c2 | not in the CTAB: a 3x4 transform of `(diffuse.rgb, 1)` (role from the code; plausibly the hue-shift matrix of the `hueshift` effect variants, inferred) |
| c3 | `g_EnableGlow` (lerp weight between light-map alpha and diffuse alpha) |
| c4 / c5, c6 / c7 | `LightDir_Dir0` / `_Color0`, `LightDir_Dir1` / `_Color1` |
| c8, c9, c10, c11 | `g_MatSpecularStrength`, `g_MatSpecularPower`, `g_MatReflectionStrength`, `g_MatDiffuseStrength` |
| c12 | `def (1, 0, 3, 0)` |
| temporaries | r0-r4 |

**Colour:** not a texture times a constant. rgb = (two directional lights' diffuse x c11 + specular x `s1.x` x c8 +
`saturate(v0.rgb)`) x `M(c0..c2)·(s0.rgb, 1)` + `M·(s0.rgb, 1)` x c10 x `s1.x` x `s3(v4)` + `s2.rgb`; alpha =
`lerp(c3.x, s2.a, s0.a)` x `v0.a`. The lens draw writes rgb only (`colorwrite=7`) with ONE/ONE, so alpha does not
reach the picture; the wrap's `Rgb` scale mode covers it.

**Insertion point for the clip:** after the last body instruction (word 1293's `mul_pp`, before END), at depth 0. This is
where the existing wrap appends its tail once both `oC0` writes are redirected to `rO`; no point inside the body is needed.

## 3. The plan: token-level transformation

The SM2 wrap's data flow is unchanged: the vertex program copies its clip position into an extra interpolator, and the
pixel program computes `uv = (t.xy / t.w) * (0.5, -0.5) + (0.5 + dx/2, 0.5 + dy/2)`, takes nine RT2 taps, and scales `rO`.
Only the declaration and register encoding of that interpolator change, and the position destination is found by
semantic.

### 3.1 Encoding differences against the SM2 wrap's assumptions

| Item | SM2 (what the code assumes) | SM3 (this pair) |
| --- | --- | --- |
| Position write | `RastOut` type 4, number 0: `oPos` = `0xc00f0000` | `Output` type 6 (same type value as `TexCrdOut`), the register declared `dcl_position0` (here o0): `0xe00f0000 \| n`; `0xc00f0000` is invalid in vs_3_0 |
| Extra vs output | `oTn` = `0xe00f0000 \| n`, implicit (no dcl); n is also the ps index | `oN` = `0xe00f0000 \| N` **plus** `dcl_texcoordK oN`: `0x0200001f, 0x80000005 \| (K << 16), 0xe00f0000 \| N`. Register N and semantic K are independent; the stages link by K |
| ps input dcl | `dcl tn`: `0x0200001f, 0x80000000, 0xb00f0000 \| n` (type 3 `Texture`) | `dcl_texcoordK vM`: `0x0200001f, 0x80000005 \| (K << 16), 0x900f0000 \| M` (type 1 `Input`); type 3 is invalid in a ps_3_0 dcl |
| ps input read | `src_texcoord` = `0xb0000000 \| swz << 16 \| n` | `0x90000000 \| swz << 16 \| M` |
| Limits | 8 `oT`, 8 `t`, 12 / 32 temps (vs / ps_2_x) | 12 `o#` (vs), 10 `v#` (ps), 32 temps each, c0-c223 (ps), 16 samplers |
| Output usage scan | `TexCrdOut` destinations of instructions | `dcl` tokens: usage (bits 0-4), usage index (bits 16-19), register (type 6 / type 1) |
| Sampler `dcl_2d`, `def`, `texld`, `cmp`, `oC0` | `0x90000000` / `0x05000051` / op 66 / op 88 / `0x800f0800` | identical |

Where the current code breaks on this pair, in call order: `lens_visibility_free_texcoord` returns 8 for a ps_3_0 or a
vs not 2_0 / 2_x (the measured `no_free_texcoord`); `lens_visibility_vertex_variant` refuses `0xfffe0300`
(`unsupported_version`), and with the version gate removed it would see no `RastOut` write and return `no_output`. Its
`TexCrdOut && number >= 8` refusal would also misfire on a vs_3_0 writing o8-o11. `build_pixel` with `clip` returns
`unsupported_version` for `scan.sm3`. `scan_pixel` records no ps_3_0 inputs: its `Texture && !sm3` branch skips them.

### 3.2 Vertex program (`lens_visibility_vertex_variant`, SM3 branch)

1. Accept `0xfffe0300`. In the `Dcl` case, for `register_type(p[1]) == 6`: set `used_output |= 1 << n` (refuse n >= 12);
   when usage is 0 and the index is 0, `position_output = n`; when usage is 5, mark semantic index `(p[0] >> 16) & 15` in
   `used_semantic`. Keep the existing input-`dcl_position0` capture for `origin_known`.
2. Position writes: test `register_type(p[0]) == 6 && register_number(p[0]) == position_output` instead of
   `RastOut && 0`, with the same four-dp4 rules (one lane each, depth 0, no relative destination, sources `r` / `c` with
   `.xyzw` and no modifier). Refuse any `Output`-type destination with the relative bit (13) set. Skip the SM2
   `TexCrdOut` index refusal. No `dcl_position0` output gives `NoOutput`.
3. Choose `N = highest_free(used_output, 12)` = **o11** here, `P = highest_free(used_temp, 32)` = **r31** (or 12 as today
   = r11; both free). `K` comes in as `texcoord` and must be clear in `used_semantic`, otherwise `ResourceLimit`.
4. Emit: the original up to `body`, then **`dcl_texcoordK oN`** (3 words) at `body` (word 302, ahead of `def c42`; dcl
   and def both precede the first instruction, which vs_3_0 requires); the body with the four `dp4 o0.<lane>`
   destinations rewritten to `0x80000000 | (token & 0x00ff0000) | P` (words 451, 455, 459, 463 become
   `0x8001001f / 0x8002001f / 0x8004001f / 0x8008001f`); then `mov o0, rP` (`0x02000001, 0xe00f0000 | position_output,
   0x80e40000 | P`) and `mov oN, rP` (`0x02000001, 0xe00f0000 | N, 0x80e40000 | P`), END. 526 → **535 words** (+9),
   54 body instructions (prototype, measured).

### 3.3 Pixel program (`scan_pixel` and `build_pixel`, SM3 branch)

1. `scan_pixel`: for a `Dcl` with `register_type(p[1]) == 1` record `used_input |= 1 << n` (n < 10); when usage is 5, mark
   the usage index in `used_semantic`. Here inputs v0-v4 and semantics texcoord0-3 (color0 is v0).
2. `build_pixel` with `clip` and `scan.sm3`: `K = texcoord` must be clear in `used_semantic`;
   `M = highest_free(used_input, 10)` = **v9**. Emit everything exactly as the ps_2_x clip pair does, with two changes:
   - the input dcl becomes `0x0200001f, 0x80000005 | (K << 16), 0x900f0000 | M` (non-centroid, full mask);
   - the three uv instructions read `vM`: `rcp rQ.w, vM.w`; `mul rQ, vM, rQ.w`;
     `mad rQ, rQ, cC.xyzz, cC.zwzz`.
   The f fetch, the nine taps (`texld` + `cmp`, eight `add` offsets from `cD` / `cW`), the `core_f` choice and the
   `mul rO.mask, rO, rT.y; mov oC0, rO` tail are unchanged. Every added instruction reads at most one `c#` and one `v#`.
3. The ps_2_0 slot check stays `!scan.sm3` only. Per the slot-budget rule there is no SM3 refusal: the wrapped program is
   **84 body instructions** (79 arithmetic + 14 texture slots by the same table), words 1298 → **1506** (`core_f`) /
   1505 (clip only).

Registers the highest-free selection picks for this pair (prototype, measured):

| Stage | Body uses | Wrap takes | Collision |
| --- | --- | --- | --- |
| vs constants | c0-c42 (c0-c23 relative), i0, b0 | none | none |
| vs temporaries | r0-r6 | r31 (P) | none; 8 of 32 |
| vs outputs | o0-o5; texcoord0-3 | o11, `TEXCOORD7` | none; 7 of 12 |
| ps constants | c0-c12 | c223 (K: 0.5, 0.5, 0, 1/9), c222 (C), c221 (D), c220 (W) | none |
| ps samplers | s0-s3 | s15 (1x1 fraction), s14 (RT2 lane) | none |
| ps temporaries | r0-r4 | r31 (O), r30 (T), r29 (Q), r28 (S), r27 (T2), r26 (E) | none; 11 of 32 |
| ps inputs | v0-v4; color0, texcoord0-3 | v9, `TEXCOORD7` | none; 6 of 10 |

`lens_visibility_free_texcoord`: for an SM3 pair (vs `0xfffe0300` **and** ps `0xffff0300`; a mixed pair returns 8),
return the highest texcoord semantic index below 8 that neither the vs output dcls nor the ps input dcls use (**7**
here), and return 8 if the vs has no free `o#` below 12 or the ps no free `v#` below 10. `Pair::texcoord` then
means "semantic index" for SM3 and "`oTn` / `tn` index" for SM2; `SunOcclusionPass` needs no change: the constants,
samplers and blocks come through `LensVisibilityLayout` as today, and `build_pair_vertex` still compares
`matrix_register` (24). The architecture note's sentence "a vs_3_0 / ps_3_0 pair is refused for the clip" then changes.

## 4. Validation offline before a flight

The native validator applies (all observed on this host's `SysWOW64\d3d9.dll`, section 5): one distinct `c#` read per
instruction (message id 584); at most one `v#`; `dcl` only on `v#`, `s#`, `vPos`, `vFace` in a ps (id 299); every
`v#` component read must be declared (id 984); every `o#` component written must be declared (id 704); valid
destination register types (id 718, e.g. `oPos` in vs_3_0); `dcl` / `def` before the first instruction; register
ranges (ps: r < 32, c < 224, v < 10, s < 16; vs: o < 12); `texld` sources (r / v / c coordinate, a declared sampler,
no source modifier). It validates each program alone: **it does not check vs-output / ps-input semantic linkage.** A
generator assertion must cover that: the same K in both dcls, both full-mask, and K unused elsewhere in both programs.
The host fixture `verification/probe/sun_occlusion_host.cpp` drives the three wrap entry points and should gain the
SM3 cases (the refusals: a mixed pair, no free `o#` / `v#`, a used semantic, a relative position write).

Command shape (the `rerun_round3.sh` pattern; the generator is a new host program linking the production wrap, which
the implementer writes; captures stay in place, outputs in a scratch dir):

```sh
S=<scratch>; V=verification/results/hull-widen-native-validator
g++ -std=c++17 -O2 -Isrc/renderer <gen_lens_sm3>.cpp src/renderer/lens_visibility_variant.cpp -o $S/gen
i686-w64-mingw32-g++ -O1 -static $V/native_validate.cpp -o $S/native_validate.exe
(cd $S && ./gen /mnt/c/X3/x3-modern-captures out && ./native_validate.exe out/*.bin)
python3 $V/sm3_rules.py $S/out/ps_*.bin    # ps_3_0 subset of the same rules, offline
```

`native_validate.exe` runs on the native Windows host through WSL interop (no device, no window, nothing launched,
no Wine); on a CrossOver host the same executable runs under
`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py ...`, where it exercises wined3d's validator, not
native D3D9's.

## 5. Prototype evidence (measured, scratch only)

A ~120-line scratch Python implemented sections 3.2 and 3.3 token for token (the ps tail mirrored from `build_pixel`,
dx = 1/5120, dy = 1/1440, `Rgb` mask) and wrote the programs to a scratch directory (not tracked).

| Program | Words | `native_validate.exe` (SysWOW64 d3d9) | `sm3_rules.py` rows |
| --- | --- | --- | --- |
| AP vs original / wrapped | 526 / 535 | End `S_OK`, 0 messages / End `S_OK`, 0 messages | n/a (vs) |
| AP ps original / clip `core_f` / clip only | 1298 / 1506 / 1505 | End `S_OK`, 0 messages each | 0 |
| FL vs wrapped (FL vs byte-identical to AP's, `cmp`) | 535 | End `S_OK`, 0 messages | n/a |
| FL `ps_ced7edef48059e84` clip `core_f` / clip only | 1626 / 1625 | End `S_OK`, 0 messages each | 0 |
| Negative: vs final `mov` to `0xc00f0000` (SM2 `oPos`) | 535 | `80004005`, id 718 "Invalid reg type for dest param" | - |
| Negative: vs without the inserted `dcl_texcoord7 o11` | 532 | `80004005`, id 704 undeclared components of o11 | - |
| Negative: ps input declared SM2-style (`0x80000000`, `0xb00f0009`) | 1506 | `80004005`, id 299 + 2 x id 984 (v9 not declared) | - |
| Negative: clip `mad` reading cC and c221 | 1506 | `80004005`, id 584 (2 distinct c#) | - |

A structural diff of the wrapped vs against the original: prefix (words 0-301) identical, 3 inserted words
(`0200001f 80070005 e00f000b`), exactly 4 changed words (the dp4 destinations), 6-word tail plus END.

## 6. FL

FL's `addon2/01.cat` carries the identical vs (FNV and bytes) but not `ps_7c83ed50`. Its `shader/3_0/standard_lighting.fb`
(and the `hue_lights_off` variant) resolves to three FL-only pixel programs, `ps_1b54cbfa2155b61c`,
`ps_3c39d45636d95744` and `ps_ced7edef48059e84`, against AP's `ps_0c1f3f0f440e4a0c`, `ps_7c83ed50c9894e44` and
`ps_99153c144030c396` (measured with `fl-shader-coverage/inventory.py`'s `effective_programs` over `/mnt/c/X3AP-orig` and
`/mnt/c/X3FL`). In the profile tables `494fe349` pairs with `ps_7c83ed50` (AP) and with `ps_ced7edef48059e84` (FL,
`motion_output_profiles_fl_inc.h`), and the other two FL programs pair with `vs_4944d81d`. So FL's sun body, if drawn with the
same material, would be `vs 494fe349 / ps ced7edef` (inferred; FL has no sun-occlusion rows, `/mnt/c/X3FL/x3m.log` has no
`sun_lens_draw`). That ps has the same input / sampler layout (v0-v4 = color0, texcoord0-3; s0-s2 2D, s3 cube), 52
body instructions, constants c0-c15 (c14, c15 `def`), `oC0` at words 1409 / 1413, and wraps cleanly (section 5).

## 7. Unknown

- Whether `core::classify_body` places this body at the sun: the scan never reached classification
  (`matrix_register=-1` in every run216 row). The body is a lit standard-material quad; whether its local origin (the `.w`
  column of c24-c27) is the quad centre is not established offline. The first log-only flight with the SM3 scan answers it
  (`centre_dist_u` in `sun_lens_draw`).
- Whether stock AP draws its sun with this pair (the ledger's open question): only Mayhem's install has been flown with it.
- wined3d's behaviour for the wrapped pair (texcoord semantic 7 linkage, `cmp` on an R-format lane) is not tested;
  the native validator is.
- The role of ps c0-c2 (absent from the CTAB) is read from the code only.
- Alternative not planned: ps_3_0 can read `vPos`, which would remove the vertex wrap, but the uv would then depend on
  the viewport origin and size (extra constants per draw) and on `vPos` pixel-centre parity between native D3D9 and
  wined3d. Neither is established in this project.
