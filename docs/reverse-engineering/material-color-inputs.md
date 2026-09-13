# Material color inputs and native scaling

Targeted offline investigation, 2026-09-13, for the proposed
[first linear material slice](../architecture/scene-linear-materials.md).
No production change, Wine execution, game launch or installation occurred.

The shader-visible inputs have **different scaling contracts**. Light RGB is
an integer-channel conversion with a divisor of 256. Material emissive can
already include a separately authored strength multiplied by color before it
reaches the VS constant. Decoding every color-looking shader constant with a
power function would therefore exponentiate that material strength.

## Light-data producer and upload

Game EXE preferred base `0x00400000`, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`.
Read-only Ghidra inspection located `0x004bdbf0`, the updater of the light data
pointed to by scene-node `+0x16c`. Its allocator at `0x004bdd20` creates a
zeroed `0x6c`-byte block, attaches it to that field, sets the final tracking
word to -1, and calls the updater. Existing updater call sites follow changes
to node color, range or attenuation fields.

Targeted native x86 disassembly confirms the color conversion; the decompiler's
inferred `int*` output type is misleading and must not be treated as an integer
conversion. The instructions load signed words using MOVSX, convert their
integer values using FILD, multiply, and store single-precision floats using
FSTP:

| Node input | Light-data output | Conversion |
| --- | --- | --- |
| signed 16-bit `+0x150` | float RGB X at `+0x04` | channel × 1/256 |
| signed 16-bit `+0x152` | float RGB Y at `+0x08` | channel × 1/256 |
| signed 16-bit `+0x154` | float RGB Z at `+0x0c` | channel × 1/256 |

The conversion/store span is `0x004bdc3a`–`0x004bdc77`. The multiplier is the
exact double `0.00390625` at `0x00565568`. There is no gamma transfer, RGB clamp
or separate intensity multiplication in this producer. Signed input is a
machine-level fact: this function itself admits negative values and values
above 255. It does not establish which such values real content supplies.
Several callers initialize the node's three words to 255, producing
`0.99609375`, not 1.0; do not silently normalize these existing inputs to 255
instead of 256.

Range and attenuation are independent fields. The later span
`0x004bdcb2`–`0x004bdcee` computes the range from node `+0x158` and the owning
coordinate scale, then derives the three attenuation coefficients from node
`+0x15c/+0x160/+0x164`, a 1/65536 factor and range/range² divisors. Those are
not RGB intensity factors and must remain data inputs when colors are converted.
`0x004bdda0` separately updates position/direction; it does not alter the RGB
conversion above.

Material submission `0x004c0150` copies light-data `+4/+8/+12` to both
`LightDir_Color0/1` and the point-light color vectors without further color
arithmetic. The point and directional upload paths therefore share the same
producer convention. The [constant-upload study](constant-uploads.md) documents
how these effect values reach the device.

**Established encoding:** signed integer channels divided by 256, consumed in
the original gamma-space lighting arithmetic. **Not established:** an exact
sRGB or gamma-2.2 asset transfer function, physical intensity units, or a global
0–1 bound. Choosing a linearization curve for those legacy color codes remains
an explicit renderer policy, not a recovered engine transfer operation.

## Material emissive is sometimes color × strength

The original shader's `g_MatEmissiveColor` is not always an unscaled color. Four
binding paths in `0x004c0150` reference this exact parameter name; targeted
assembly establishes their arithmetic even where the large function's
Ghidra C output loses the switch-case details.

| Binding path | Source and operation | Uploaded alpha |
| --- | --- | --- |
| `0x004c1399`–`0x004c13fd`, nonzero strength | Signed RGB words at the selected material record `+0x0a/+0x0c/+0x0e`, multiplied by signed strength word `+0x1a`, then by float(1/25500) | 1 |
| `0x004c1419`–`0x004c1454`, zero strength | Explicit zero emissive RGB; another scalar is assigned to `g_MatDiffuseStrength` separately | 0 |
| `0x004c1533`–`0x004c159b`, nonzero strength | Material-table entry RGB **unsigned bytes** at `+9/+0x0a/+0x0b`, multiplied by **signed** strength word `+0x22`, then by double(1/25500) | 1 |
| `0x004c15fd`–`0x004c164b`, zero strength | The same table entry's **unsigned bytes** at `+0/+1/+2`, divided by 255 | 0 |

The material-table branch addresses entries with a 60-byte stride from
`0x00608db0`. The selected-record branch is a separate layout; the table above
names fields relative to their actual source record, not a universal material
structure. The nonzero test initially uses a zero-extended word, but the
arithmetic then sign-extends it; this distinction matters for an eventual hook.

The single-precision multiplier at `0x0056573c` is
`3.9215687138494104e-5`; the double at `0x00565728` is
`3.9215686274509805e-5`. Both represent their respective precision's 1/25500,
i.e. `(color / 255) * (strength / 100)`. The zero-strength table path uses the
double 1/255 at `0x00565670`. No power/transfer curve or clamp occurs in these
bindings. The signed products are formed before floating-point conversion.

`0x004b9060` resolves the named effect parameter and ultimately calls
`SetVector`; `0x004b90e0` supplies its three RGB arguments with alpha zero.
Thus these are effect-value writes, not merely unused CPU color calculations.
The alpha distinction here does not itself affect the selected material's
COLOR0 alpha: its VS independently computes the latter from material alpha and
fog. The RGB terms do feed the VS point-light-plus-emissive sum.

Consequences for a replacement shader:

- A power applied to the combined emissive constant transforms the native
  strength as well as the tint. The shader constant alone cannot generally
  recover their original decomposition.
- A new renderer emissive gain can multiply this supplied amplitude linearly.
  It must not be placed inside the color transfer function.
- Source-decomposed tint conversion would require a later guarded binding hook
  or separately retained source material information. A nonzero material
  constant cannot be ignored merely because one recorded scene supplied zero.

## Baked Argon defaults and the lightmap input

A bounded reader inspected all **24** compiled Argon effects in the SM3
archive directories: 16 root `01.cat` entries and eight `addon/01.cat` entries,
covering base, `2s`, `_0000`, `_0001` and the available color-toggle directories.
Container boundaries were cross-checked with the existing effect-pass parser;
no raw effects were extracted into the repository.

| Parameter | Default across inspected effects |
| --- | --- |
| `g_MatEmissiveColor` | Zero RGB; 16 effects declare four zero components, eight declare three |
| `g_EnableGlow` | 0 |
| `g_AlphaValue` | 1 |
| `LightDir_Color0` | (1, 1, 1) |
| `LightDir_Color1`, where declared | (0, 0, 0) |
| `t_LightMapTexture` | Referenced texture-object payload has zero bytes; no texture image/default asset is embedded there |

These are **effect defaults**, not the game's later material bindings. In
particular, the empty texture-object payload is not proof that the game uses a
black, white or null lightmap for a particular draw.

Instruction review of all six selected Argon PS programs establishes that s2
RGB is independently added to the material result. Its alpha participates in
an interpolation selected by `g_EnableGlow`; that scalar does not gate the
RGB addition. There is no separate lightmap intensity constant in this
contract. A replacement's lightmap emissive gain is consequently a new
explicit linear scale applied after decoding the sampled color, not a
recovered native scalar. The sampled RGB may contain baked illumination as
well as emissive appearance; the instruction role alone does not classify
every texture's artistic contents.

## Bounded capture ranges

The existing iteration-05 trace was streamed through a targeted scanner;
no full log was printed or loaded into the model. Source SHA-256
`e5beaa861d04659fe9c7df05a01845bd05d656a33c643f4b484ff379cf3ccaf8`
matches the [motion-profile evidence](motion-output-profiles.md). Only the
10 selected archive pairings were admitted. Present/draw success, complete
frame draw counts, query success, float sparse-zero encoding and covered
register ranges, and full integer-count encoding were checked before treating
omitted float registers as zero. All admitted draws use the base VS and the
two expected PS programs; no unseen toggle program is claimed as exercised.

| Measurement | Result |
| --- | --- |
| Complete captured frames | 28 |
| Admitted Argon DEFAULT draws | 2,512: 2,180 one-sided and 332 two-sided |
| Rejected selected draws | 0 |
| VS material emissive c40 | (0, 0, 0, 0) on all 2,512 draws |
| Active point count i0.x | 0 on all 2,512 draws; stale float-array entries were ignored |
| Directional color 0 | (170, 200, 150)/256 on all selected draws |
| Directional color 1 | (33, 66, 55)/256 on all selected draws |
| `g_EnableGlow` | 1 on 1,844 draws; 0 on 668 |
| s2 lightmap bindings | 35 distinct nonnull resources, all reported DXT5; dimensions span 32×32 to 2048×2048 |

This capture does not contain sampled lightmap texel readbacks or an asset-name
mapping for those resource identities. It cannot establish their actual RGB
value distribution. DXT5's normalized sample representation does not supply a
measured texture histogram. It also cannot demonstrate the nonzero material
emissive binding paths above or active point lighting on these Argon draws.

Separately, the existing [iteration-04 light summary](../../verification/results/iteration04-light-summary.json)
contains two distinct active point RGB values across its retained payload
witnesses: (0, 125, 146)/256 and (255, 255, 255)/256. They corroborate the
producer's integer/256 convention. Those observations are not a claim of Argon
DEFAULT coverage or a global upper bound on light color.

## First-slice policy and remaining limits

The bounded shader-only iteration can treat the already-scaled
`g_MatEmissiveColor` as **legacy emissive amplitude/tint**, preserve that supplied
amplitude and multiply it by our optional linear gain. Do not apply pow to the
combined constant. This preserves native strength scaling while explicitly
leaving the tint's authored colorimetry unrecovered. Texture lightmap RGB can
follow the separately declared legacy-color decode and linear gain policy.
No new material-binding hook is needed for this choice.

Offline fixtures can exercise native emissive amplitudes and HDR gains even
though this capture used zero. A later user-run scene is needed to establish
which nonzero paths and textured emissive features are actually encountered,
and to assess their appearance. Neither this study nor a shader-only change
recovers physical light calibration or makes the entire scene linear.

## Local reproducibility

Private investigation products are untracked under `/tmp`:

- `x3-material-light-producer.txt` and `.asm`: targeted Ghidra output and native
  disassembly for the light-data producer.
- `x3-material-emissive-bindings.asm`, `x3-material-setvector-helpers.txt`:
  exact emissive arithmetic and effect-vector helper checks.
- `x3-material-effect-defaults.py` / `.json`: bounded archive-default reader
  and derived per-effect hashes/defaults.
- `x3-material-capture-ranges.py` / `.json`: streaming capture scanner and
  compact derived ranges/binding counts.

Ghidra used the existing `/tmp/x3-ghidra-research/X3Render` database with
`-process X3AP.exe -readOnly -noanalysis` and the existing
[`X3ConstantUploads.java`](../../tools/analysis/X3ConstantUploads.java) script.
`i686-w64-mingw32-objdump` was restricted to the address ranges above; direct PE
reads verified the conversion constants. Raw decompiler/disassembly output
must remain local. These are static/captured-input findings, not executed
verification of replacement shaders.
