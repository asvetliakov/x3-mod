# Fixed SM3 rigid replay program

The detached production [program](../../src/renderer/rigid_replay_program.cpp)
provides an original 54-DWORD vertex shader with no runtime HLSL compilation.
It preserves the reviewed constructor's exact literal bits and swizzles:
`MAD r0, v0.xyzx, c8.xxxy, c8.yyyx`, with local `c8=(1,+0,+0,+0)`.
Eight full-precision `DP4` instructions use **temporary first, row second**.
Current POSITION0 is written XYZW from submitted rows c0–3; previous TEXCOORD0
is written XYZW from submitted rows c4–7. The shader never consumes stored input
W, combines matrices, or adds/removes jitter. The local DEF is independent of
application constant-register values.

The module's qualification helper first performs the existing whole-program
byte lookup, then requires SM3, XYZW write order and the reviewed MAD constructor.
It accepts **32** installed-archive profiles and refuses **202** legacy row-dot
profiles, including six whose write order is WXYZ. A caller-created metadata
record cannot qualify a shader. This is an operation/model compatibility check,
**not automatic draw eligibility**. Native FLOAT3/FLOAT16_4 declaration conversion,
finite XYZ payload, stable resources, actual submitted rows, lifetime and frame
correspondence, depth/coverage, and unsupported-contributor handling remain
explicit caller requirements. The current rigid-motion pass and installed game
DLL are unchanged by this module.

The independent fixture passes:

| Check | Result |
| --- | --- |
| Textually assembled original program versus production tokens | Exact match after assembler comments are removed |
| SDK-mask token decoding | Exact DEF bits, MAD operands/modifiers, DP4 order/masks and instruction inventory |
| Local archive qualification | 32 accepted, 202 refused; 17,475 single-word mutation controls refused |
| Native FLOAT16_4 arithmetic | Every 16-bit encoding in each XYZ lane, 196,608 inputs total |
| Native FLOAT3 arithmetic | 22 edge payloads per XYZ lane, 66 inputs total |
| Current/previous output comparison | 1,573,392 component pairs, zero mismatches |
| Raster and preserved D24X8 depth | 134 cases, 80 with coverage and 54 clipped/empty; both EQUAL directions match |
| Incorrect current depth row | Deliberately differs under EQUAL, as required |

The arithmetic fixture redirects the production program's current POSITION
output to a varying and adds a separate finite raster-position stream. It leaves
the MAD and DP4 arithmetic tokens intact. Its independently assembled reference
uses different temporary/local-constant/row registers while preserving operation
order. Both current and previous results pass through varying interpolation, an
original pixel shader, RGBA32F targets and test-only readback. A third MRT writes
a constant coverage marker; **every submitted point must have that marker**.
This prevents two untouched clear texels from satisfying equality. Independent
finite numerical anchors validate both output streams and native half conversion.
Application constants overwrite both local-DEF registers before drawing.

The separate raster fixture uses the unmodified production shader and an
independently assembled original MAD/DP4 reference. It compares full coverage
images, then preserves reference-written depth for candidate EQUAL replay, and
repeats with candidate-written depth for reference EQUAL replay. This proves
agreement at stored D24 precision on covered samples, not bitwise clip-coordinate
or unquantized depth identity. Tests include
nontrivial perspective rows, half 0x3555 (approximately one third), ignored stored
W values including NaN/Inf, signed zero, smallest/largest subnormal, smallest
normal, largest finite, positive/negative infinity, and multiple NaN payloads.
It varies one vertex component at a time; this is representative raster testing,
not an exhaustive sweep of all triangle payload combinations.

## Evidence limits

The arithmetic result proves **conversion → shader → varying/pixel shader →
render target/readback parity on this backend with one fixed set of rows**.
It does not expose internal register bits. In this run, half Inf/NaN input produced
maximal finite float readback values, and no NaN output pairs were observed.
The harness permits matching NaN classifications if a backend returns NaNs; it
does not require identical NaN payload bits. Other output bits, including signed
zero if present, are compared exactly. This evidence cannot establish universal
NaN/Inf behavior or justify removing the finite-payload gate. There is also no
claim of equivalence for arbitrary denormal arithmetic or legacy shader models.
The raster evidence is synthetic; none of the 32 full game programs was executed
by this fixture, and no live replay or TAA integration is claimed.

Only original shaders/geometry and derived hashes/metadata are tracked. Actual
archive bytes used for qualification stay in the untracked local sweep directory.
Production performs no readback, creates no device/resources and owns no state;
resource lifetime, queries and restoration remain responsibilities of the caller's
existing rigid pass.

## Reproduction

Run `python3 verification/probe/run_rigid_replay.py` when the user game is stopped.
The runner fresh-builds the x86 SSE2/stack-aligned fixture and portable profile
checker, verifies all local shader bytes against the existing archive proof,
refuses to start GPU work when X3AP is active, and records source/executable/D3DX/
proof/input hashes before and after execution. It uses CrossOver Preview's native
D3D9 backend without installing a DLL or launching the game.

The [result manifest](../../verification/results/rigid-replay-summary.json) records
393,496 checks, zero mismatches, the 1,573,392 arithmetic component pairs and all
134 raster cases. The [short GPU report](../../verification/results/rigid-replay.txt)
also records representative exceptional-input readback bits. Build products remain
untracked. Independent review checks the fixture and frozen provenance separately.
