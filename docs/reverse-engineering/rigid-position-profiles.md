# Reviewed vertex-position contracts for rigid motion

The production registry now covers **all 234 proved archive row-dot programs**.
The captured subset below remains the original 16-profile proof and input-layout
record. See [archive shader lookup](shader-profile-registry.md) for deterministic
generation, all 256 position categories, pixel coverage lookup and current tests.

**16 of the 21 captured vertex shaders** have the following exact algebraic
position path for ordinary finite inputs:

```text
p = float4(POSITION0.x, POSITION0.y, POSITION0.z, 1)
clip.x = dot(p, submitted_row0)
clip.y = dot(p, submitted_row1)
clip.z = dot(p, submitted_row2)
clip.w = dot(p, submitted_row3)
```

The other five are **three direct-clip bloom shaders, one direct-position
GUI/effects shader, and one particle billboard shader**. They require separate
composition or temporal handling; they are not omitted from the renderer scope.
The [full archive position review](archive-position-paths.md) now accounts for
all 256 installed vertex shaders, and the
[temporal coverage plan](../architecture/temporal-coverage.md) describes each route.

There is no input-W dependency, billboard expansion, position deformation,
position saturation, partial-precision modifier, divide or additional position
offset in these paths. This establishes **position arithmetic**, not temporal
rigidity, opaque-scene eligibility or bitwise raster equivalence of a replacement
shader. Object/mesh identity, unchanged vertex content, pass state and a GPU
parity fixture remain separate gates.

The [reviewed metadata](../../verification/results/rigid-position-profiles.json)
records full SHA-256/FNV/length/version, declared inputs, exact original DWORD
offsets, matrix registers, constructor and literal definitions, exclusions and
observed input layouts. Raw shader code remains local. FNV is over the full
little-endian program, including comments and END, using the existing capture
algorithm. The owner's exact-profile registry entries are in
[`rigid_position_profiles_inc.h`](../../src/renderer/rigid_position_profiles_inc.h);
lookup alone does not make a draw eligible for motion or jitter.

## Proven position paths

All qualified shaders declare POSITION0 at v0. **P/T/N/B/G/C** below denote
POSITION0, TEXCOORD0, NORMAL0, BINORMAL0, TANGENT0 and COLOR0, respectively, in
consecutive input registers starting at v0. These declarations do not encode
the vertex buffer's storage type. Input layout must come from the actual D3D
vertex declaration.

| VS FNV-1a-64 | Bytes / DWORDs | Matrix rows | Position output | Protected temporary / literal | Declared inputs | Role/eligibility caution |
| --- | ---: | --- | --- | --- | --- | --- |
| `167eb2d5629ab9d3` | 2264 / 566 | c24–27 | o0.xyzw | r2 / c42 | P,T,N,B,G | Asteroid material; pass gate still required |
| `37c34a7478544c14` | 3072 / 768 | c24–27 | o0.xyzw | r2 / c47 | P,T,N,B,G | Extended material families |
| `4944d81dfe531b37` | 2224 / 556 | c24–27 | o0.xyzw | r2 / c42 | P,T,N,B,G | Multiple material aliases |
| `494fe349b8bc12ec` | 2104 / 526 | c24–27 | o0.xyzw | r1 / c42 | P,T,N | Standard/extended material aliases |
| `53a0a641107ed76c` | 2104 / 526 | c24–27 | o0.xyzw | r1 / c42 | P,T,N | Multiple material aliases |
| `b0602757fce6e870` | 2080 / 520 | c24–27 | o0.xyzw | r1 / c42 | P,T,N | Asteroid material |
| `c30104cb0efb6675` | 2200 / 550 | c24–27 | o0.xyzw | r1 / c42 | P,T,N | Glass; transparency must not enter opaque motion implicitly |
| `41c960621d22671f` | 408 / 102 | c0–3 | oPos.xyzw | r0 / c4 | P,T,C | Stardust; temporal/scene role unproved by arithmetic |
| `5e484a06672e28fb` | 404 / 101 | c0–3 | oPos.xyzw | r0 / c4 | P,T,C | CTAB calls rows **ViewProjection**, input space requires separate evidence |
| `7b6393fe2d3e1d85` | 512 / 128 | c0–3 | oPos.xyzw | r0 / c6 | P,T | Shared GUI/nebula/effects; hash alone cannot classify scene |
| `803ebfd17f79e413` | 380 / 95 | c0–3 | oPos.xyzw | r0 / c4 | P,T | z_only alias; coverage/state still matter |
| `89193868c61c3846` | 884 / 221 | c0–3 | oPos.xyzw | r0 / c12 | P,T | CTAB **ViewProjection**; fog work is after position |
| `ac2319bc3953efc6` | 512 / 128 | c0–3 | oPos.xyzw | r0 / c6 | P,T | adeffects; pass gate required |
| `be199829a9bb78db` | 1124 / 281 | c0–3 | oPos.xyzw | r1 / c14 | P,N | Planet haze; interleaved lighting does not alter position source |
| `c78b4c68a87fce74` | 356 / 89 | c0–3 | oPos.xyzw | r0 / c4 | P | z_only alias; no texture input required by VS |
| `d5e1c75351ed3f04` | 1012 / 253 | c0–3 | oPos.xyzw | r0 / c14 | P,T | Effects/engine; observed sharing with overlays requires pass gate |

CTAB calls the four rows `g_mWorldViewProjection` except for the two explicitly
marked ViewProjection programs. Those names are supporting metadata, not a
replacement for the inspected arithmetic or proof of input coordinate space.
The exact archive aliases are in the complete shader sweep. The two shaders
`41c9...` and `8919...` occur in the cumulative shader dump collection but not
the latest session's declaration records.

## Proof beyond the four-DP4 motif

The new [inspector](../../tools/analysis/inspect_rigid_positions.py) parses actual
SM1/2/3 instruction boundaries and skips whole comment blocks. It verifies:

- Exactly one POSITION0 input and the correct model-specific position output.
- Exactly four position writes, covering X/Y/Z/W once each, using unmodified,
  unconditional DP4 instructions with consecutive directly addressed constants.
- One homogeneous constructor writes the complete temporary before every dot.
  Its literal register has shader-defined X=1 and Y=0, and its swizzles implement
  XYZ × 1 + 0 and W = input X × 0 + 1. Application constants cannot replace those
  local literals. No matrix row overlaps a shader-local DEF.
- No instruction, including instructions inside light/fog control flow, writes
  any lane of that temporary again before its final position use. No later
  instruction rewrites position. Calls, returns, predication and unsupported
  control-flow forms are rejected.
- All other direct uses of the position rows and any relative constant reads
  are separately exposed for the jitter decision.

All 16 profiles pass those checks. An independent full-body manual read of all
21 captured VS agrees with the classification and verifies the same liveness
facts. The seven material light loops leave the protected position temporary
untouched and finish before its output writes. Fog branches cannot affect those
writes. `8919...` writes W/X/Y/Z before its fog IF; `d5e1...` writes W/X/Y, then
unrelated UV arithmetic, then Z before its IF. `be19...` interleaves position
and lighting; its later r1.w overwrite occurs **after** the final position use.

The homogeneous constructor is a MAD, not a literal assignment plus bitwise
copy. Its ordinary finite-input algebra is sufficient for the intended geometry
contract; arbitrary NaNs/infinities, signed-zero edge behavior and denormal rules
are not silently promoted to bitwise equivalence. The motion fixture must test
the actual compiler/backend path and native declaration conversion.

## Five explicit exclusions

| VS FNV-1a-64 | Why it is outside this contract |
| --- | --- |
| `1279d081455f5815` | Bloom: copies input XYZW directly, no submitted position matrix |
| `6059306306203243` | Bloom: same direct XYZW position contract |
| `cbbf26102694c961` | Bloom: same direct XYZW position contract; constants alter UVs |
| `f36fc43f30b19d71` | GUI: direct input XYZ with forced W=1, no submitted matrix |
| `36f98d151fd6b0c6` | Particles: view transform, input TEXCOORD0 XY expansion, then projection |

These are real exclusions, not failed shader parsing. The particle path needs
its own previous center/expansion contract. Direct clip-position paths must not
receive a fictitious WVP or automatic scene jitter.

## Actual POSITION0 storage: half-floats dominate

The immutable log snapshot `session-20260910-234001-212.log` is hashed in the
metadata. Among the structurally qualified profiles, it contains **13,287
POSITION0 declaration occurrences with type 16 (`FLOAT16_4`) and only 16 with
type 2 (`FLOAT3`)**. Every material c24 profile uses FLOAT16_4. POSITION0 is
stream 0, element offset 0, method DEFAULT in these observations. Bound stream
offset is zero and stream frequency is one. Material stride is generally 40;
`494fe349b8bc12ec` also appears 132 times with stride 24. The metadata retains
each shader's exact observed combination/count.

Consequently a FLOAT3-only motion declaration would reject the intended scene,
and interpreting the half-float bytes as three 32-bit floats would be incorrect.
For the half-float route, preserve a native FLOAT16_4 POSITION declaration and
let the backend perform the same conversion. The replacement VS should consume
the converted XYZ and force W to one. A stored fourth component must not become
homogeneous W. FLOAT3's default fourth component is likewise immaterial because
the original constructor does not read it. Buffer offsets, stride, element
storage type and frequency belong to each draw; they must not be inferred from
the VS hash. Packed formats or other conversion paths require separate tests.

These are recorded declarations, not a statement that all those draws qualify
for motion. Their successful state/coverage/object-history eligibility is owned
by the runtime classifier. The latest snapshot has i0.x values 0–2 for the seven
material profiles; active point lights occur in `4944...` and `c301...`.

## Matrix provenance and jitter

Use the four **actual submitted float constant rows** for current and previous
draws. Do not reconstruct WVP from the world/view/projection names for motion:
the engine's relative camera regimes and higher-precision preparation can differ
from multiplying separately captured matrices. Likewise, do not substitute
c28–30 world-position/lighting rows for c24–27 clip rows. ViewProjection-named
programs may receive CPU-pretransformed positions; their previous input/buffer
coordinate contract requires independent history evidence.

For a reviewed eligible draw, a desired pixel shift `(dx,dy)` in a viewport of
size `(W,H)` has the following algebraic constant-row form:

```text
jx =  2 * dx / W
jy = -2 * dy / H
row0_jittered = row0 + jx * row3
row1_jittered = row1 + jy * row3
row2_jittered = row2
row3_jittered = row3
```

The inspected shaders contain no clamp of clip Z/W or projection divide after
the dots. Ordinary D3D homogeneous clipping and the viewport/depth transform
still apply. Preserve the original near/far behavior and viewport MinZ/MaxZ;
do not add an NDC/depth clamp to the replacement position path.

The nine c0-row profiles have no other direct or relative reads of those rows.
The seven c24-row profiles have no other **direct** reads, but their point-light
loop reads c0/c1/c2 relative to three times the light index. With the reviewed
0–8 light count, those reads stay within c0–23. An unbounded count of nine or
more can alias c24–27. Therefore row-constant jitter for these profiles requires
the integer i0.x count guard **0 ≤ i0.x ≤ 8** (and the reviewed loop/hash), or a
separately validated output-position-only injection. A CTAB array name/count by
itself is not that runtime guard. The current snapshot's 0–2 counts satisfy it.

Current rerasterization must match the position/depth-producing draw, including
its jitter convention, retained depth, culling, viewport and coverage state.
Define whether velocity uses jittered or unjittered current/previous clip
coordinates consistently with temporal resolve; applying the jitter delta twice
would produce incorrect motion. Alpha-tested/blended/glass/HUD or other coverage
exceptions remain excluded until their own policy exists. An exact position
profile does not permit ignoring those states.

## Runtime registry contract

The 234-entry archive exact-profile registry attests only: full byte hash + length,
`FloatXYZForceWOneSubmittedRowDots`, POSITION0 input register, first matrix row,
and a CTAB WVP-name hint. Source metadata now also retains shader version,
position write order and the exact homogeneous MAD constructor category for
separate replay qualification; these fields do not broaden ordinary finite-input
or raster guarantees. The relative-light-read jitter guard is documented
separately and must be enforced by any constant-row jitter owner. The owning draw gate
must additionally establish matching object/instance, draw range and buffers,
unchanged known buffer revisions across history, supported input declaration,
valid submitted rows, scene/depth epoch and supported coverage/raster state.
Missing or ambiguous evidence should produce invalid motion rather than a
guessed correspondence. The original shader remains responsible for shading;
this contract concerns detached motion rerasterization only.

Reproduce the structural proof and declaration evidence without launching X3:

```sh
python3 tools/analysis/inspect_rigid_positions.py \
  --inventory verification/results/shader-sweep-inventory.json \
  --raw-directory /tmp/x3-shader-sweep/programs \
  --output /tmp/rigid-position-profiles.json \
  --capture-log "$HOME/Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/x3-modern-captures/session-20260910-234001-212.log"
python3 -m unittest verification.analysis.test_rigid_positions
```

Nine original token-fixture tests cover the accepted models, unrelated loops,
conditional temporary mutation, post-use reuse, later output rewrites, position
modifiers, wrong row order, literal/input-W changes, conditional position output,
early return, comments and truncation. These validate the narrow proof checker;
they do not replace the GPU motion/raster parity fixture.

The production lookup is now freshly compiled for Win32 with SSE2 and checked
against all **751 archive programs**: all 256 VS categories agree, 234 row-dot
profiles match their expected matrix registers/name hints, and 494 of 495 PS
match the separate no-discard/no-depth-output coverage registry. Every DWORD's
low bit is individually mutated: all **547,927 variants** reject. Null, zero,
oversized, truncated and appended lengths also reject. Run
`python3 verification/probe/run_rigid_position_profiles.py`; the
[lookup report](../../verification/results/rigid-position-lookup-summary.json)
records source/executable/input hashes and commands. This fixture creates no
D3D device and launches no game; GPU raster parity remains a separate test.
