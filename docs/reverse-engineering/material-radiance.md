# Material vertex-radiance clamp profiles

The five captured material pixel shaders identified in
[position-shaders.md](position-shaders.md) contain **eight RGB-only clamp sites**.
They can be replaced with a lower-bound-only operation using each program's
existing literal-zero definition. This preserves vertex lighting/emissive values
above one while retaining the original zero floor. It does not implement an FP16
scene, HDR presentation, or a new lighting model.

These findings come from the captured full `GetFunction` bytecode and the local
game D3DX disassembler. The raw original and modified programs/disassembly remain
outside the repository. The [derived manifest](material-radiance-profiles.json)
contains complete original/modified hashes, sizes, minimal instruction spans and
offsets. The production profile initializer is
[`material_radiance_profiles_inc.h`](../../src/renderer/material_radiance_profiles_inc.h).

## Exact source programs

Every program starts with pixel shader 3.0 version DWORD `0xffff0300`, ends in
`0x0000ffff`, and has no bytes after END. Counts include all comment/preshader
payloads. FNV-1a-64 uses offset basis 14695981039346656037 and prime 1099511628211
over the entire original little-endian byte string.

| Original FNV-1a-64 | Bytes / DWORDs | Full-program SHA-256 |
| --- | ---: | --- |
| `5f82ecacd39529cd` | 7060 / 1765 | `e62d0f041430e54fdea5800ef6576d812a2486f12a980f921553e7292349d8c2` |
| `fffdabd910793aba` | 6592 / 1648 | `8fc110cf9cf4631ac0f7052b8f61ec6c5908bcaa7be830853570404ddfa95b72` |
| `7c83ed50c9894e44` | 5192 / 1298 | `a171c7d7e3dcdfc87816fc651bf93918399594ec457ce1822dcd47a0cff2374a` |
| `8759c7838bbc86c2` | 5040 / 1260 | `9fd15484fe419295cfb3534bd4f978efc8855c1e3e6a06e776533497dad48dc0` |
| `f1b0e820c7b488c3` | 7164 / 1791 | `8f6517d6730a53d52712a335253d272663e34d05e3cdcdccc93c05249014515e` |

## Replacement specification

All offsets below are **zero-based original DWORD indices from the version
token**, including comments. Multiply by four for byte offsets. The destination
is the DWORD immediately after the listed instruction. Three profiles need both
mutually exclusive `if b1` / `else` branch sites replaced as one operation.

| Profile prefix | Instruction indices | Destination indices | Expected destination(s) | Literal-zero DEF index | Zero source | Encoded zero source |
| --- | --- | --- | --- | ---: | --- | --- |
| `5f82` | 1692, 1709 | 1693, 1710 | `80370005`, `80370004` | 1307 | c24.y | `a0550018` |
| `fffd` | 1575, 1592 | 1576, 1593 | `80370005`, `80370004` | 1268 | c22.y | `a0550016` |
| `7c83` | 1252 | 1253 | `80370004` | 1091 | c12.y | `a055000c` |
| `8759` | 1206 | 1207 | `80370001` | 1041 | c8.y | `a0550008` |
| `f1b0` | 1718, 1735 | 1719, 1736 | `80370005`, `80370004` | 1307 | c24.z | `a0aa0018` |

Each original three-DWORD span is:

```text
02000001  <expected destination>  90e40000
```

Replace it with the four-DWORD span:

```text
0300000b  <expected destination & ~00100000>  90e40000  <encoded zero source>
```

This changes `MOV_SAT_PP temp.xyz, v0` into
`MAX_PP temp.xyz, v0, local_zero`. The zero comes from an existing six-DWORD DEF
instruction (`05000051`, constant destination, four literal components); the
selected component is exactly positive zero (`00000000`). It is shader-local,
so no application constant register is borrowed or overwritten. The original
RGB mask, partial-precision modifier and temporary register remain intact.
Instruction count and control-flow structure stay unchanged; storage grows by
one DWORD per site. Structured IF/ELSE contains no relative byte offset to fix.

Simply clearing destination bit `00100000` would remove **both** bounds, admitting
negative RGB. The MAX form instead gives `max(v0.rgb, 0)` for ordinary finite
values. It preserves the intended nonnegative lighting range while allowing
values greater than one. This is not a new guarantee for NaNs/infinities or an
upgrade from partial precision to full precision.

## What remains unchanged

All five programs declare interpolated COLOR0 as v0. The selected RGB moves
consume the upstream vertex point-light accumulation plus emissive term.
Three profiles route it through material mixing in two branches; the simpler
`7c83`/`8759` programs add it directly to the existing material/directional RGB.
There is no subsequent SAT on the combined RGB output after these paths join.
Later texture multiplication, occlusion, reflection and lightmap arithmetic can
still reduce or increase brightness. An 8-bit target would still clamp the result.

The targeted destinations write `.xyz`, never `.w`. Inspection of the subsequent
dataflow shows no dependency from those modified RGB lanes into alpha: output
alpha remains the material texture-alpha interpolation multiplied by v0.w.
No alpha instruction or its input lanes are changed. The 36 other saturation
instructions across the five programs remain byte-identical: six each in
`7c83`/`8759`, eight each in the other three. They belong to the normal/directional
diffuse, specular and material/decal arithmetic, not this interpolated COLOR0
RGB clamp. Removing them would be a separate change to material response.

## Validation and integration boundaries

The original inspection helper and D3DX module provenance are recorded in
[position-shader-inspection.json](../../verification/results/position-shader-inspection.json).
The derivation script
[`inspect_material_radiance.py`](../../tools/analysis/inspect_material_radiance.py)
checks the pinned full SHA and FNV, walks SM3 instruction boundaries while
skipping comment payloads, checks all eight sites and existing zero literals,
then constructs local variants. Reversing just the replacement spans reproduces
every original DWORD exactly. All five variants were accepted by the installed
`D3DXDisassembleShader` (`S_OK`, five successes, zero failures). Complete text
comparison changes only the eight intended MOV lines into the corresponding MAX
lines; all other disassembly, declarations, constants and instruction-slot
counts remain identical. This is offline syntax/dataflow validation, not an
executed game or a numerical render of these copyrighted shaders.
The [derived inspection record](../../verification/results/material-radiance-inspection.json)
pins the manifest, inspector, disassembler and raw local text hashes without
redistributing those programs or their disassembly.

| Profile prefix | Modified DWORDs | Modified FNV-1a-64 |
| --- | ---: | --- |
| `5f82` | 1767 | `647b5ca721f98b29` |
| `fffd` | 1650 | `f2c8aa734b90dca2` |
| `7c83` | 1299 | `5ffaea78cd5bdf44` |
| `8759` | 1261 | `7aa76c58f9169be6` |
| `f1b0` | 1793 | `3179b22d42ce48b3` |

A runtime consumer must reject unknown profiles and validate complete program
length/fingerprint, framing, every expected instruction span and the selected DEF
before constructing any variant. Offsets refer to the original stream: build a
new stream in order or replace from the highest offset downward. Do not patch
one branch if the other fails validation. Keep the original shader available as
fallback if variant creation fails. This profile identification does not itself
prove scene-pass eligibility or authorize replacing every use of a shader.

Reproduce the offline derivation without changing source captures:

```sh
python3 tools/analysis/inspect_material_radiance.py \
  "$HOME/Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/x3-modern-captures" \
  --json /tmp/material-radiance-profiles.json \
  --local-patched-directory /tmp/x3-material-radiance-patched
```

Pass that local directory as both input/output to the existing standalone
`disassemble_shaders.exe` using the same D3DX path as position-shaders.md. It does
not create a D3D device or launch the game. Raw outputs must remain untracked.
