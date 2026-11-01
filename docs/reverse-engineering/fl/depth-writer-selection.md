# FL depth-writer selection and bounded jitter targets

Offline study, 2026-10-28. FL executable `/mnt/c/X3FL-orig/X3FL.exe`, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`;
preferred image base `0x400000`. No production changes, builds, Wine, game,
installation or commits. This refines the coverage question in
[renderer integration](../../architecture/farnhams-legacy-renderer-integration.md).

**The archive's unknown depth candidates are not all main-scene writers.**
Normal FL story setup puts planet primary geometry and generated nebula/stars
in the galaxy camera, before the sector camera's depth clear. That is a real
phase exclusion when the actual clear succeeds on the same main depth surface.
It is not a shader-hash exemption: generic material submission can use the same
program in another scene. There are still authored opaque `adeffects` materials
and an instanced path with mutable effect state. Consequently the `3_0` directory
alone does not close coverage. A finite jitter extension is available below;
its raster coverage is separate from RT2/motion completeness.

## Evidence and reproduction

Local derived evidence and producers are under
`verification/results/fl-port/depth-writer-selection/`:

```sh
python3 verification/results/fl-port/depth-writer-selection/derive.py
python3 verification/results/fl-port/depth-writer-selection/placement.py
python3 verification/results/fl-port/depth-writer-selection/material_witness.py
python3 verification/results/fl-port/depth-writer-selection/effects_materials.py
python3 verification/results/fl-port/depth-writer-selection/material_placement.py
```

`derive.py` checks 12 native operand anchors, hashes seven native windows,
reparses 15 VS and 30 selected top-level `3_0` effects. `placement.py` checks
13 native anchors, nine KC calls and seven member/priority operands against
FL `addon2/L/x3story.obj`, selected from `addon2/03.cat`, SHA-256
`dbb732716e1acab6b8cc7f87f5d31324fb4764dfc22d1cec808749a4f45ec7b9`.
Raw listings/decompiler output remain ignored in this directory; raw shader
programs remain outside the repository in `/tmp/x3-fl-port-shaders/fl`.
Counts below are measured static counts; gameplay reachability is explicitly
qualified. The producer preserves exact lengths, versions, hashes and offsets.

## 1. Actual effect and technique selection

The existing [material seam](material-submit.md) remains
`0x4e7142 → 0x4e0f30`: ECX descriptor, EDX node, stack camera/flags/two lights.
This target is not restricted to high-model hull materials:

* Named material effect string is selected at `0x4e175a..0x4e17aa`;
  `max_standard_lighting` normalizes to `standard_lighting`. Ordinary lookup
  goes through `0x4e1848 → 0x4db5b0`, with the optional two-character `2s`
  attempt at `0x4e17fb` and base-name fallback at `0x4e180b`.
* `DEFAULT` versus `BUMPMAP` is selected at `0x4e1865..0x4e18b4` from available
  bump input and node flags. Node+0x130 bit `0x20000` instead selects `z_only`
  and `Z_Only_Fast/Alpha` at `0x4e16dd..0x4e1747` or `0x4e1961..0x4e19d4`.
  Legacy material fallback selects `effects` for engine+0x64's scene,
  `standard_lighting` otherwise (`0x4e19ef..0x4e1a14`).
* GetTechniqueByName at `0x4e1ac8` can fall back to FindNextValidTechnique at
  `0x4e1ae8`; SetTechnique is `0x4e1b02`. A requested technique name alone is
  not the selected program. Pass BeginPass and indexed draw are
  `0x4e601a/0x4e604d`.
* Lookup `0x4db5b0` accepts ECX name, EDX variant index, and reuses a cached
  resource/effect slot. Loader `0x4db2c0` formats `shader\\%s\\%s` at `0x4db337`
  using device record+0x38's profile string and resource+4's name, then reads
  `.fb` and calls D3DXCreateEffect at `0x4db49a`. Missing file/creation failure
  is not evidence of an automatic switch to another profile directory.

The profile is genuinely selected: `0x4f2d1e..0x4f2d2d` compares the selected
profile against `ps_3_0`; `0x4f2d35` supplies `3_0`, and `0x4f2d55` publishes
its composed string to device record+0x38; `0x4f2d6b` stores `3.0` into
config+0x754. Adjacent branches select `2_a` (`0x4f2cbb/0x4f2cdb`) and `1_1`
(`0x4f2da5` onward). This is **not an unconditional SM3 runtime contract**.
The selected-profile follow-up in section 6 reconstructs the quality arbitration
and device lifetime. Lower-quality selections remain outside the bounded `3_0`
admission; the live selected string, not a launcher preference, is authoritative.
The nine SM3 unknown pairs are bloom; the same directory also contains 20
non-bloom candidate pairs using 15 distinct SM1/2 VS. `_0000/_0001` selection
remains unresolved; they are retained as conservative targets, not excluded.

## 2. State transport and positive/negative witnesses

Material property application at `0x4e2436 → 0x4d9730` resolves the property
name and applies the supplied typed value. For integer values, that helper
gets the name from parameter+4 (inline/heap according to +0x18), value from
+0x20 and calls effect SetInt (+0x68) at `0x4d9791`. The general material list
loop is `0x4e1f12..0x4e244f`; the routine also records/reapplies parameter blocks
(see [native recovery](ui-render-mutation.md)). These are mutable
values on the cached effect, not immutable state inferred from the shader hash.

The alpha-test override forces blend off and **ZWrite=1** at
`0x4e2455..0x4e24db`. In the legacy branch, material flags tested against
`0x4c7` at `0x4e2eda` select ZWrite=0 at `0x4e2f2a` or ZWrite=1 at
`0x4e345a`. Thus neither an effects-family name nor lower shader model is an
engine-level nonwriter guarantee. The compiled material-family effects carry
`g_ZEnable=1`, `g_ZWriteEnable=1` defaults, with expression-driven pass states.
The FXLC expressions themselves were not evaluated here; defaults and setter
arguments are not substituted for actual successful D3D state application.

| Family/path | Measured evidence | Consequence |
| --- | --- | --- |
| `adeffects` | 11 opaque materials explicitly supply ZEnable=ZWrite=1; example `objects/others/argon_adsign_A.pbb`, material 8. One additional sampled material is blended with ZWrite=0. | `ac2319bc3953efc6` is a positive authored depth candidate; no camera-phase exclusion established for advertisement geometry. |
| `effects` / `engine` | 165+2 effects materials and 66 engine materials explicitly disable ZWrite; one `effects` material, `objects/others/arrow_indicator.pbd` material 0, enables it. | Common engine glow material state supports nonwriter draws, not a hash-wide rule. The arrow's actual camera role is unproven. |
| Instanced mesh `0x497af8 → 0x4e0b40` | Group+0x1c bit0x10 selects material effect; otherwise/failure uses `effects` (`0x4e0c1f..0x4e0c64`). Group+0x18 bit0x20000000 chooses `INSTANCE_BULLETS`, otherwise `INSTANCE` (`0x4e0cc0..0x4e0cd8`). | `INSTANCE` shares the cached effect and this routine sets no ZWrite parameter. Prior parameter values/defaults remain relevant. It runs inside scene submission, before deferred drain and compositor. |
| `effects.INSTANCE_BULLETS` | Compiled pass explicitly sets ZEnable=1, ZWrite=0. | Successful application excludes depth writing. This does not exclude `effects.INSTANCE`, whose depth state is dynamic. |
| Native particles `0x48bae1 → 0x4dff50` | Executed per ordinary camera when camera+0x270 bit0x4000 is set. Native SetInt request ZWrite=0 at `0x4e0156/0x4e015c`; base `particles.DEFAULT` pass has literal ZWrite=0. | Base particle pass cannot write depth after successful state application. Old `_0000/1` pass has dynamic depth state and no `g_ZWriteEnable` parameter in its top-level parameter census; do not transfer the base exclusion to it. |
| `nebulafog.DEFAULT` | Literal ZEnable=0 and ZWrite=0. | Successful pass state excludes depth writing, although VS hash is shared with nebula/UI. |
| `planet_haze.DEFAULT` | Literal ZEnable=1, ZWrite=0. | Same exclusion; colour still contributes before composition. |
| Bloom | Exact compositor first unbinds depth at `0x4e68c3`; named call is after the scene boundary `0x48b929`. | Its nine unknown SM3 pairs do not belong to the pre-compositor main-depth contract. This exclusion is by phase/actual surface state, not archive basename alone. |

The effects material-header census inspected 3,553 effective `objects/**/*.pbb/pbd`
members, found 246 matching material records and reported 154 unsupported text
headers. These are lower bounds, not an exhaustive proof that no other authored
writer exists. The separate planet/nebula mesh census examined 607 members,
found 915 matching material records, and reported 11 unsupported mesh/scene
records. Its explicit opaque ZWrite=1 counts are nebula 308, planet_v 79, moon
61. These values do not establish which depth epoch receives those writes.

The bounded follow-up name-reference scan inspected 3,616 members. The opaque
advertisement body is registered in FL `addon2/types/TSpecial.pck` as
`SS_SPECIAL_SIGN1` (record 76, class 20). Class 20 follows the ordinary +0x130
scene attachment branch above, **if instantiated**. The base universe map
contains that identifier, but FL's addon2 universe/flight-school maps do not.
Which map layers or scripted creation are active was not established; this is
an available main-scene writer, not an observed FL flight draw. The arrow name
has no separate archive reference in this bounded scan. Its camera placement
remains unknown. The producer retains map counts and member hashes without
publishing asset contents.

## 3. Normal Background versus Scene placement

The following establishes the **ordinary stock flight construction**, not every
possible script-modified scene or every use of a shared material.

1. FL story `2001::Activate` (`CODE 0x1075d9`) creates the galaxy scene object
   into member 43 at `0x107678`. Its GetScene3D result is assigned by
   `SA_SetSpaceGalaxyScene` at `0x1076c4`. Native opcode 0x129 is
   `0x46e94c`; `0x46e977` stores sector+0x134. Getter opcode 0x12c reads that
   same field at `0x4701a7`. Sector scene is the separate +0x130 field.
2. Native `SA_StartObjectInSpace`, opcode 6, `0x46c14b → 0x452b70`, selects
   sector+0x134 for object classes 3 and 4 at `0x452c56..0x452c6a`; other
   primary geometry uses +0x130. The object's primary node at +0x70 is attached
   by `0x452c78 → 0x4a56b0`. Attachment stores node+0x1c at `0x4a56f6` and
   propagates scene membership to descendants. Class 4 planet primary meshes,
   including their moon/planet_v materials, therefore enter the galaxy scene.
   The class-4 auxiliary at class-data+0x10 is separately added to +0x130 at
   `0x452d58`, but its birth `0x44ad0d → 0x4a3da0` is the **light allocator**
   (same target as `B3D_LightAlloc`, `0x4b009b`), not another planet mesh.
3. `2001::DoGalaxyTake` (`CODE 0x1081e2`) obtains star/nebula body IDs at
   `0x1081ec/0x108272`, sets bodies at `0x10820a/0x108290`, and attaches both
   to member43.GetScene3D at `0x10822b/0x1082b1`. Node pass numbers -10/+10
   order geometry **inside that scene**; they are not camera priorities.
   Native background reset `0x45c301..0x45c385` likewise walks +0x134's nodes
   and swaps the chosen star/background model IDs.
4. `606::ShowSpace` (`CODE 0x1884c3`) sets sector camera priority member11-4 at
   `0x18852c`, galaxy priority member11-5 at `0x188681`. Native
   `B3D_CameraSetPri` stores camera+0x29c at `0x4b1205`. The complete pass
   sorts that field ascending, so galaxy precedes sector.
5. Galaxy camera flags explicitly include 0x4|0x10|0x20 at `0x1886cf`;
   sector flags include 0x4|0x20 at `0x188581`. Sector adds 0x10 only when
   no galaxy scene exists (`0x18858b..0x1885a6`). With enabled depth clearing,
   the [native Clear contract](compositor.md#2-clear-arguments-and-camera-timing)
   yields galaxy color+depth clear, galaxy draws, sector depth clear, sector
   draws. Actual main-pair/viewport/success checks remain mandatory.

Therefore these normal galaxy planet/moon/nebula draws can remain outside
**Scene** jitter and private-depth routing: their depth is cleared before the
main scene. Retain structural Background admission; do not convert these
constructor witnesses into a global shader allowlist. Alternate camera users,
loading/restored scene membership, or materials reused on station meshes must
still pass actual phase classification. Background colour persists and needs
its own temporal policy: this study does not prove FL camera-centered motion,
UV/texture stability or a blanket reactive exemption. AP's captured background
policy is contextual precedent, not FL execution evidence.

## 4. Exact finite jitter-only targets

All 15 lower-model unknown VS in the `3_0` candidate census emit exactly four
unpredicated DP4 position components from a single constant matrix. There are
no DEF/writes to the matrix registers. No direct reads of those rows serve
another output. Length includes comments and END; full fingerprints include
CTAB, matching production identity. This is a register contract, not approval
to jitter Background/UI or to route their pixel programs.

| VS FNV-1a | DWORDs | Model | Clip rows | Family / qualification |
| --- | ---: | --- | --- | --- |
| `089091aab2d5eb13` | 125 | 2.0 | c0–3 | effects old DEFAULT |
| `2b2dc4bf90cd4167` | 488 | 2.0 | c0–3 | planet_v old, unrolled lighting |
| `2eea471bc86935f2` | 128 | 1.1 | c4–7 | particles old; projection after view transform |
| `32e75459998d0388` | 253 | 2.0 | c0–3 | engine old DEFAULT; fog branch after position |
| `369ce62e02a24ac0` | 95 | 2.0 | c0–3 | adeffects old |
| `5b7a3ccd9e7df00a` | 221 | 2.0 | c0–3 | engine old INSTANCE; fog branch after position |
| `72f8dbb8567bbf88` | 536 | 2.0 | c24–27 | planet_v; relative-light bound required |
| `7b6393fe2d3e1d85` | 128 | 1.1 | c0–3 | gui2d/nebula; phase restriction required |
| `89193868c61c3846` | 221 | 2.0 | c0–3 | effects/engine INSTANCE; fog branch after position |
| `a520be365951c9dc` | 94 | 2.0 | c0–3 | effects old INSTANCE |
| `ac2319bc3953efc6` | 128 | 2.0 | c0–3 | adeffects |
| `b54f95f833833568` | 536 | 2.0 | c24–27 | planet_v old; relative-light bound required |
| `d5e1c75351ed3f04` | 253 | 2.0 | c0–3 | effects/engine DEFAULT; fog branch after position |
| `ececc28e00592d35` | 128 | 1.1 | c0–3 | gui2d/nebula old; phase restriction required |
| `f80f7af59b667bb7` | 502 | 2.0 | c24–27 | moon; relative-light bound required |

For the three c24 programs, the lighting loop is `rep i0`, indexing
`c[3*k+0..2]`, k starts at zero and increments by one. **Require a known i0.x
in 0..8** before constant-row modification, or separately prove the engine
clamp: the ninth light aliases c24/25. CTAB's eight-element light array alone
is not a runtime bound. The remaining twelve have no relative reads. Fog
branches occur after position and do not select an alternate clip matrix.

For an admitted Scene draw, the bounded extension can use the existing
`row0 += jx*row3; row1 += jy*row3` operation, leaving z/w unchanged, and restore
all four original rows after submission. Match hash/length/version at creation;
require known submitted rows and preserve nested draw/failed-upload/restore
invalidation. Unknown device state or unsupported profile cannot be repaired
by counting an unjittered writer after earlier jitter was already injected.

## 5. Raster jitter is not private depth/motion completeness

Current `MotionOutput::evaluate_draw` applies jitter before Gate 3, but an
unregistered pair then returns without routing. `route.depth` is assigned only
after an accepted pair. Thus adding the table above alone does **not** write
motion or RT2 depth for those draws. RT2 is a separate colour texture, not the
application's depth attachment. An unknown opaque writer can leave RT2 at -1,
or leave a previously routed surface's depth behind it. Resolve's sentinel
camera/far-plane policy (`src/temporal/resolve.hlsl`) does not reconstruct that
finite surface's true depth or object motion. A non-depth-writing blend can
also need reactive coverage even though it passes the depth-writer audit.

Practical next work is therefore: preserve the proven Background exclusion;
qualify the actual remaining Scene `adeffects`/generic effect writers with
jitter plus either reviewed motion/depth output or conservative current-only
coverage; explicitly admit selected quality/profile; and test unknown writer
handling before the first affected jittered draw. For ordinary engine-glow and
bullet/particle nonwriters, preserve their actual state and separately account
for temporal colour/reactivity. Do not replace this work with a permanent FL
TAA disable or claim a complete TAA port from the 15-row raster extension.

## 6. Selected profile, lifetime and bounded admission

Follow-up, 2026-10-28. Reproduction is
`python3 verification/results/fl-port/depth-writer-selection/selected_profile.py`.
The producer checks 19 exact native anchors, six selector/name strings, the
D3DX import and direct-call/field-reference censuses, then uses the qualified
FL resource resolver to read the effective domain. This is source/asset evidence,
not observation of a running device. Optional repeated `--mod NAME` arguments
must match the launcher's ordered effective ModName selection.

### Authoritative selection and configuration

The selector belongs to device initializer **0x4f1e10**, whose measured sole
direct caller is `0x4f3e0a` inside display setup `0x4f3a30` (itself called at
`0x5168c5`). It allocates a **0x1a8-byte device record**, publishes it through
`(*(0x5aaf94))+0x18` at `0x4f1ec1`, and creates the D3D device into record+0 at
`0x4f2489`. It calls **D3DXGetPixelShaderProfile** through import `0x550364` at
`0x4f26cf`, stores the queried profile string at record+8, and applies the
configured quality to a local copy. It subsequently stores the chosen pixel
profile at record+0x20 and the actual effect-directory string at **record+0x38**.
These are three distinct 0x18-byte string objects.

`G=*(0x5a83b0)`, `G+0x744` is **VideoShaderQuality**: default **2** at
`0x4d70fc`, registry read publication `0x4d7611`, registry write readback
`0x4d8110`, launcher dialog publication `0x4fac84`. Labels in `0x4f5050` prove
0=low, 1=medium, 2=high. The complete direct +0x744 access census finds only
those three stores; the native system interface at `0x4b3c92` is a getter.
No `P_SetSysShaderQuality` name exists in this EXE. This bounded census does
not claim arbitrary alias writes or injected private calls are impossible.

| Quality | Branch and result |
| --- | --- |
| 0 low | `0x4f27a8..0x4f289c` selects `ps_1_1` for the recognized supported profiles. Directory `1_1`. |
| 1 medium | `0x4f289e..0x4f2906` replaces `ps_2_0/2_a/2_b/3_0` with `ps_1_4`; lower profiles remain as queried. |
| 2 high | `0x4f28a1 → 0x4f2908` preserves the queried profile. When D3DX returns `ps_3_0`, selected base directory is `3_0`. This is the default modern-capable path, conditional on the actual D3DX result. |

An out-of-range quality also takes the preserve branch; do not interpret that
as a valid high setting. Admission should inspect the resulting directory.
`G+0x754` contains derived display text (`"3.0"` at `0x4f2d6b`), not the
selection input or a complete directory: it omits the suffix.

At `0x4f2a2f..0x4f2a77`, bits of `G+0xf0` add exactly one suffix:

| Bits 0x1800 | Suffix | Full `3_0` string length |
| --- | --- | ---: |
| 0 | empty | 3 |
| 0x800 | `\hueshift_off` | 16 |
| 0x1000 | `\v_lights_off` | 16 |
| 0x1800 | `\hue_lights_off` | 18 |

The six profile branches publish `1_1`, `1_4`, `2_0`, `2_a`, `2_b` or `3_0`
plus that suffix. The `3_0` assignment is `0x4f2d55 → 0x474480`. A targeted
whole-text reference census finds six nearby-global-derived +0x38 destinations
(all these publication branches) and one reader, the effect loader at
`0x4db327`. This is a bounded syntactic census, not unrestricted alias analysis.

### Lifetime, Reset and the safe snapshot boundary

Ordinary lost-device recovery **0x4f3740** is called from `0x4f3a18`; it releases
and reacquires surfaces, calls device **Reset** at `0x4f37cd`, and resets effect
resources. It neither calls `0x4f1e10` nor reselects/replaces record+0x38.
Thus a successful ordinary Reset retains the native selected directory, while
mod-owned GPU readiness must still be invalidated and restored. A later full
device creation runs selection again. Full teardown releases the device at
`0x4f4bd2`, zeros record+0 at `0x4f4bdd`, frees the record at `0x4f4ce2`, clears
owner+0x18 at `0x4f4d03`, and eventually clears global `0x5aaf94` at `0x4f4d6d`.
Adapter/probe cleanup also has a record free at `0x4f17e4`; no pointer may be
borrowed across setup/teardown just because the global was once non-null.

**CreateDevice return is too early for this snapshot.** The selection follows
CreateDevice, and even the initializer's initial BeginScene at `0x4f24d5`
precedes profile publication. A first-BeginScene latch would therefore also
be wrong. The existing admitted **outer complete-pass entry**, before Scene
jitter, is the narrow suitable integration point; no new broad engine hook is
required. On the native owner thread, with existing render/load/close exclusion:

1. Read the validated loaded-module global, then owner+0x18; require the native
   device at record+0 to match the proxy's current underlying device/generation.
2. Copy the full record+0x38 string immediately into a small owned enum/value.
   String length is +0x10, capacity +0x14; capacity below 16 means inline bytes,
   otherwise the first word is a heap pointer. Admit only the four exact `3_0`
   values above, including length and terminator. Do not retain the native pointer.
3. Combine that copied value with the launch preflight's reviewed resource-domain
   result and current resource readiness **before the first jittered Scene draw**.
   Invalid, incomplete or different-device snapshots deny that scene. Reset or
   device replacement invalidates admission; refresh at the next safe pass.

The copy itself must make no native, COM, allocation, logging or message-pumping
calls. Under this bounded native lifecycle there is no callback edge inside a
plain copy, so same-thread reentrancy cannot free its owner midway. The existing
hook's CPU/flags/LastError/FP-SIMD and exception rules still apply. Read only
while the existing owner-thread/render admission is valid; this is not permission
for unsynchronized worker-thread polling or delayed borrowed-pointer access.
A configuration change alone is not proof that the current selected string has
changed. The reviewed ordinary Reset path does not apply a new shader quality.

### Effective resource domain and stable-install contract

The [effect-reader contract](resource-callers.md#effect-reader-abi-and-safety-limits)
constructs `shader\<full-profile>\<effect-name>` at `0x4db337`, calls extension
resolver `0x505e00` at `0x4db3bf` with **fb** and user-directory flag zero, then
passes the selected payload to D3DXCreateEffect at `0x4db49a`. It does not retry
`addon2/shader`, `addon/shader`, another profile directory or a lower winner on
payload failure. The [qualified resolver](resource-resolver.md)
therefore gives this exact order: matching root-relative loose file first;
otherwise highest mounted slot containing the exact requested member. Root
numbered catalogs mount ascending to their first gap, then addon2 numbered
catalogs independently, then selected mods in ordered-list order. Later slots
win. Physical addon2 mounting does **not** add an addon2 member-name prefix.

Using that resolver directly, the no-selected-mod read-only installation yields
**392 effective effects, 67 distinct VS and 148 distinct PS**, with zero
nonprogram VS assignments (12 explicit null PS assignments belong to z_only): **98 effects in each of the four directories**
above, all 392 selected from `addon2/01.cat`. No matching loose override wins.
This reproduces the design census using actual effect-loader precedence rather
than its temporary archive view. The canonical sorted `(key, payload SHA-256)`
domain digest is
`88d554d8c8075975419b7e7f1b5fdf78f1a8befe0587271c3a62b33aac3d83fd`.
The compact result records source/catalog identities, all 67 VS fingerprints,
counts and the digest; no shader bytes or raw disassembly are published.

This supplies a finite supported-install domain: preflight all four `3_0`
directories once using actual ordered mod selection and loose-file precedence;
admit only a reviewed effective result, then combine it with the authoritative
runtime directory snapshot. Retain the project's stable-resource assumption for
that process. A resource/mod change requires restart or safe requalification;
no per-draw filesystem scan, hashing, file watcher or registry walk is needed.
The current result does not attest an unprovided ModName list or a live device.
Inherited/null VS, fixed-function paths and existing Scene state classification
remain distinct negative-admission obligations; this domain is not a theorem
about arbitrary injected shaders or mutable external resources.

## ABI, lifetime and proof limits

No new hook site is proposed. Reuse the exact material CALL boundary and
complete-pass/camera/compositor ownership in the linked notes, including live
ECX/EDX/stack words, all GPR/EFLAGS/LastError/FP-SIMD preservation, four-byte
incoming alignment, nested scopes and foreign-SEH unwind cleanup. Cached
native effect pointers and material parameter-block owners have native lifetime;
this study grants no delayed dereference permission. Public D3D shader/constant/
render-state shadows should carry copied values under the current draw/phase.
Effect setters and Begin/BeginPass can fail or reenter; a requested zero is not
a successful-state exclusion, and a mutable shared effect is not per-draw storage.

The static placement proof covers the named stock constructors and ordinary
ShowSpace camera ordering, not all script mutations, restored scene permutations,
mods, settings or actual HRESULTs. FXLC dynamic-state evaluation, `_0000/_0001` selection, and private depth/motion treatment for
remaining Scene writers are unresolved. The practical gap is finite and named.
