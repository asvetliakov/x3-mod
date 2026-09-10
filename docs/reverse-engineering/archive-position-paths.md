# Every archived vertex-position path

All **256 vertex shaders in the complete installed archive sweep** now have a
proven structural position category. None remains an unknown position shape.
This extends the earlier 21 captured-shader review without changing its report
or the **16-profile production whitelist**. Static classification does not by
itself establish live-pass eligibility, vertex storage, stable object/particle
identity, unchanged geometry or safe temporal history.

| Proven position category | Archive VS | Captured VS | Input contract |
| --- | ---: | ---: | --- |
| Homogeneous XYZ/W=1, four submitted row dots | 234 | 16 | POSITION0.xyz; stored input W is ignored |
| Direct clip XYZW | 18 | 3 | All four converted POSITION0 components are preserved |
| Direct clip XYZ, W forced to one | 2 | 1 | POSITION0.xyz; stored input W is ignored |
| View transform, XY billboard expansion, projection | 2 | 1 | POSITION0.xyz plus TEXCOORD0.xy |
| Unknown structural position path | **0** | **0** | Unknown remains an explicit fallback for new/unrecognized programs |
| **Total** | **256** | **21** | |

This does not contradict the earlier general semantic inventory's **102 unknown
VS position dependency summaries**. That deliberately broad interpreter marks
outputs unknown when it encounters control flow or unsupported operations,
including unrelated lighting/fog work. The dedicated position checker now proves
the narrow constructors, output writes and protected temporary liveness across
those unrelated instructions. It does **not** resolve every other output or all
lighting/fog semantics. The general inventory and its unknowns remain intact;
zero unknown position categories is not zero unknown GPU semantics.

The [complete position inventory](../../verification/results/archive-position-paths.json)
records every full-program SHA/FNV/length/version, exact alias basenames,
category, proof facts and captured status. The
[verification record](../../verification/results/archive-position-verification.json)
pins tool, test and report hashes. Copyrighted bytecode remains only in the
local archive sweep directory. The [full shader sweep](shader-sweep.md) still
defines installation scope and exact catalogue/path aliases.

## Complete model and matrix coverage

| Token model | Row dots | Direct XYZW | Direct XYZ/W=1 | Billboard |
| --- | ---: | ---: | ---: | ---: |
| VS1.1 | 124 | 0 | 2 | 2 |
| VS2.0 | 54 | 7 | 0 | 0 |
| VS2.1 / D3DX 2_x | 24 | 4 | 0 | 0 |
| VS3.0 | 32 | 7 | 0 | 0 |

The 234 row-dot programs use three layouts: **97 use c0–3, 70 use c6–9, and
67 use c24–27**. All construct homogeneous XYZ/W=1 from POSITION0 and then
write clip X/Y/Z/W once through unmodified, unconditional DP4 instructions.
No other direct instruction reads their four position-matrix rows. The 67
c24 profiles also read relative constants for point-light loops; the previously
documented bounded light-index requirement remains necessary for row-constant
jitter. A direct-reference check alone is insufficient for those programs.
CTAB names the rows WorldViewProjection in 224 programs and ViewProjection in
10; those names do not establish the input coordinate space or temporal rigidity.

The production whitelist deliberately remains the 16 captured, reviewed profiles.
The next expansion target is the full 234-profile row-dot set, **after independent
archive-proof review and the same per-draw input/conversion, history and pass
gates**, rather than treating the initial 16 as the final renderer scope. The
additional c6 layout must be supported explicitly. A matching code path cannot
supply missing runtime stream layout or authorize applying history to a changing
CPU-generated vertex buffer.

## What was proved for each category

[`inspect_archive_positions.py`](../../tools/analysis/inspect_archive_positions.py)
reuses the existing [rigid-position proof](rigid-position-profiles.md) for row dots.
It adds narrow token-level proofs for the other paths. Every candidate is
rechecked against the full shader sweep SHA, and comments are skipped using
instruction framing, rather than searching raw DWORD values or disassembly text.

For **direct XYZW**, there is exactly one position output write: a full-mask,
unmodified MOV from the declared POSITION0 input. All 18 programs have bloom
or bloom_0000 aliases. For **direct XYZ/W=1**, exactly one full-mask MAD uses
POSITION0.xyzx and a shader-local literal with X=1/Y=0 to form XYZ/W=1.
The only two programs are `999385ffc166e5f1` and `f36fc43f30b19d71`, with gui2d
aliases. Neither form has a submitted position matrix to substitute or jitter.

For **billboards**, the two programs are `2eea471bc86935f2` and
`36f98d151fd6b0c6`, with particles-family aliases. Both prove the following
ordinary finite-input algebra:

```text
p = (POSITION0.xyz, 1)
q = four dot products of p with c0–3
q.xy += TEXCOORD0.xy
clip = four dot products of q with c4–7
```

The proof checks the declared POSITION0/TEXCOORD0 inputs, the homogeneous
constructor, four complete intermediate rows, the sole XY addition, ordering
and liveness of every temporary through all position uses, four complete
projection rows, local-literal/matrix separation and absence of any later
position write. It rejects conditional/control-flow variants and unrelated
opcodes in these narrow exception proofs. A changed lane, source, modifier,
temporary write or operation order does not silently qualify.

The row-dot proof handles unrelated lighting/fog control flow while proving
position-source liveness; exception proofs deliberately support only the small
straight-line opcode set present in these 22 programs. Unsupported future shapes
remain unknown. Shader-local MAD constructors have the same finite-input
algebra caveat as the original rigid report; classification is not a universal
NaN/Inf or bitwise raster-equivalence claim.

## The five captured exclusions still need dedicated handling

All five must have an explicit place in the final temporal/composition pipeline.
They do **not** all need the ordinary rigid-motion algorithm. Temporary rejection
avoids false motion; it is not a claim that TAA coverage is complete.

| Captured VS | Required separate route and evidence still needed |
| --- | --- |
| `1279d081455f5815` | Bloom full-screen sampling geometry: preserve direct XYZW and its UV-offset constants. Resolve eligible scene history before the appropriate bloom/composition stage; do not manufacture object motion for the full-screen quad. |
| `6059306306203243` | Same direct-clip bloom contract, different sampling offsets. Validate read/write targets and composition order rather than injecting WVP. |
| `cbbf26102694c961` | Same direct-clip bloom contract with additional UV outputs. HDR bloom/composition has its own color/alpha contracts; no scene-mesh history should attach to this quad. |
| `f36fc43f30b19d71` | Direct clip XYZ/W=1. Actual changing post-bloom buffers include overlays/effects, so the name gui2d is insufficient to classify every draw as static HUD. Route verified UI after scene resolve; non-UI effects require screen-space history or validated reactive treatment. |
| `36f98d151fd6b0c6` | Particle center plus view-space XY expansion. Exact motion needs previous center/offset with particle identity and previous camera rows; current opaque depth/rigid WVP history is insufficient. Reactive handling must account for the actual RGB blend contribution. |

This is a handling specification, not a claim that those composition, reactivity
or particle-history routes are already implemented. The extra archive variants
in each category require the same routing policy; capture visibility is not a
prerequisite for keeping them in the design.

## Particle prior-input limits from the existing capture

The independent [particle input analysis](particle-motion-inputs.md) and its
[derived evidence](../../verification/results/particle-motion-inputs.json) inspect
the already completed iteration 0.4 session without another load. There are 16
recorded particle draws. They use **non-indexed DrawPrimitive TRIANGLELIST**, even
though an index buffer is bound. The bound IB is not consumed; draw identity and
correspondence must follow the actual draw method, not the presence of an IB.

The dynamic, write-only DEFAULT-pool VB has stride 32 and this actual layout:

| Input | Byte offset | Native declaration type | Position relevance |
| --- | ---: | --- | --- |
| POSITION0 | 0 | FLOAT3 | Particle center |
| TEXCOORD0 | 12 | FLOAT2 | XY billboard offset added between view and projection |
| TEXCOORD1 | 20 | FLOAT2 | Sample UV; affects appearance/coverage |
| COLOR0 | 28 | D3DCOLOR | Per-vertex color; affects contribution/coverage |

The last observed lock flag is DISCARD. The VB revision advances across each
adjacent captured gameplay frame; primitive counts vary from 42 to 122, and all
these draws lack scoped object identity. Current and previous view/projection
constant rows are recorded, but the trace contains **no particle vertex payload
or stable particle/generation IDs**. Preserving old buffer bytes would recover
prior values, but matching list order or draw range alone cannot establish which
particle survived, spawned or died. Exact motion needs a stable correspondence
source or a separately validated matching policy; a first practical route can
instead suppress uncertain history using the rendered contribution.

**Alpha alone is not that contribution.** The actual blend factors are SRCCOLOR
and INVSRCCOLOR, with ADD, RGB-only writes, alpha test off and depth writes off.
For the captured ordinary normalized-color path, the RGB operation is
`Cs * Cs + Cd * (1 - Cs)`. Pixel-shader alpha does not supply the destination RGB
weight. An alpha-only reactive mask could miss visible particle changes. A
particle route must measure or bound the RGB contribution/coverage under this
blend, with separate numerical validation if moved to an HDR target. It should
not claim opaque-surface motion for a depth-tested effect that does not write
its own retained depth.

The three bloom VS have stable captured quad buffers, but represent sampled
image operations. The direct-XYZ overlay path has changing buffers. Neither
case supplies ordinary object correspondence merely because POSITION0 exists.

## Reproduction and verification

```sh
python3 tools/analysis/inspect_archive_positions.py \
  --inventory verification/results/shader-sweep-inventory.json \
  --aliases verification/results/shader-sweep-aliases.json \
  --raw-directory /tmp/x3-shader-sweep/programs \
  --output /tmp/archive-position-paths.json
python3 -m unittest verification.analysis.test_archive_positions verification.analysis.test_rigid_positions
```

Seven new original-token tests and the nine existing rigid tests pass. They cover
all direct/billboard contracts and negative cases for absent literals, partial
or modified outputs, later rewrites, changed offset lanes/order, intermediate
mutation and conditional paths. These are structural proofs and parser tests;
GPU motion/raster parity remains a separate production gate. No game was
launched, no new user capture was requested, and no production whitelist was
expanded by this analysis.
