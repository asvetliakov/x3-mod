# Material radiance clamp preservation

The original synthetic SM3 fixture passes **42 structural/profile checks and 192
numeric GPU samples**, across a real device Reset and resource recreation. It
executes the production `apply_radiance_profile` transformation, then compares
the original and transformed programs on CrossOver Preview's builtin D3D9
backend with FP16 render targets. No game assets or executable are used.

This verifies a selective shader transformation. It is not an enabled HDR game
renderer: X3 still requires a validated FP16 scene path and correct material/pass
selection before these variants can be applied visually.

## Actual shaders and GPU checks

[The fixture](../../verification/probe/material_radiance_fixture.cpp) assembles an
original pixel shader containing two mutually exclusive `mov_sat_pp rN.xyz, v0`
sites selected by external boolean b0. COLOR0 comes from an original SM3
passthrough vertex shader. A local `DEF c0` contains an exact zero. The fixture
walks the actual assembled bytecode to obtain its complete-program fingerprint,
word count, two instruction offsets and zero-definition offset. It does not
label synthetic programs with game shader hashes.

The production transformer replaces both selected instructions with
`max_pp rN.xyz, v0, c0.x`. GPU execution retains the zero floor and partial
precision while allowing values above one. Both programs keep a separately
saturated alpha and an unrelated saturated value written to a second FP16 render
target. Original-program disassembly is included in the result log; no extracted
game program is distributed.

| Vertex RGB | Original output RGB | Variant output RGB |
| --- | --- | --- |
| -2 | 0 | 0 |
| 0 | 0 | 0 |
| 0.25 | 0.25 | 0.25 |
| 1 | 1 | 1 |
| 4 | 1 | 4 |
| 16 | 1 | 16 |

Every row runs through both b0 branches, both programs, alpha inputs 0.25 and
1.5, and two resource generations separated by a successful Reset. Output alpha
remains 0.25 or one respectively. The second target's red value remains
`saturate(2 * input_rgb)` and its remaining channels stay zero in both programs.
This separately proves that an unrelated clamp survives. All four channels of
both targets are checked at each sample with tolerance 0.002.

Before **every draw**, application PS register c0 is explicitly overwritten with
0.875 in all components. The shader-local DEF still supplies exact zero to the
new MAX instruction and to the unrelated target's zero channels. Thus the lower
floor does not depend on an unused application constant staying zero.

The fixture uses a hidden 16×16 pure device with two simultaneous FP16 targets.
It unbinds and releases its targets/shaders before Reset, recreates them, and
repeats all 96 numeric samples. There are 96 draws total and 192 target readbacks.
No game process, installation, bottle setting or visible output is changed.

## Structural and failure checks

The production known-profile dispatcher rejects the original synthetic program
as unknown; only its explicit synthetic profile permits transformation. The
fixture also checks:

- Wrong fingerprints, modified bytes and mismatched word counts fail atomically.
- Null input, missing END, oversized/truncated COMMENT payloads and a truncated
  final DEF fail without modifying the output vector.
- A profile cannot point inside COMMENT or DEF literal payloads. Positive controls
  place exact MOV/instruction operand bit patterns in both payload types and
  verify that the framing walk skips them correctly.
- Duplicate sites, an omitted second branch, invalid zero-component index,
  nonzero/negative-zero literals, a non-DEF zero source and a wrong RGB write mask
  are rejected.
- Input/output aliasing succeeds with the exact ordinary variant, while failed
  aliasing leaves the input/output vector unchanged.
- The result grows by exactly one DWORD per patched site; each replacement retains
  partial precision, the RGB mask and original input operand, and uses the
  shader-local zero. Every instruction word outside those sites is unchanged.

These checks cover this transformation's profile/framing contract, not general
validation of arbitrary D3D9 shader semantics. The independently reviewed
[real material profiles](../reverse-engineering/material-radiance.md) are a
separate source of evidence. Their
[host byte-comparison result](../../verification/results/material-radiance-profiles-summary.json)
does not substitute for this GPU fixture, and this fixture does not establish
the correctness of every future game profile.

## Reproduce

```sh
python3 verification/probe/run_material_radiance.py
```

The runner freshly builds with `-msse2 -mfpmath=sse` for the requested SSE2 CPU
arithmetic baseline, plus `-Wall -Wextra -Werror`. It preserves the Win32 ABI;
these flags are not a claim that linked runtimes or floating-point return ABI
mechanics contain no x87 instructions. The GPU programs remain original SM3
bytecode.

Source hashes are checked before and after compilation and execution, and the
executable hash is checked after the run. The runner requires the complete set
of 192 generation/program/branch/input/alpha/target combinations, all structural
checks, Reset and successful process exit. Its process timeout is 60 seconds.

- [Numeric samples and original disassembly](../../verification/results/material-radiance.txt)
- [Current source/executable provenance](../../verification/results/material-radiance-summary.json)
- [Backend diagnostics](../../verification/results/material-radiance-wine.log)
