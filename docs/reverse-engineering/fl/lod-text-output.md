# FL text-preserving LOD output

Static feasibility and synthetic host witness, 2026-10-28. Target FL EXE SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
No native/game execution or hook qualification.

`verification/results/fl-port/lod-text-output/feasibility.py` verifies nine
instruction windows and 21 operands; `feasibility.json` retains hashes and the
synthetic counterexample. MATERIAL6's classic/effect distinction is per-record:
flags are read/tested at `0x49e100..118`, with bit `0x02000000` selecting the
effect path and its absence jumping to classic `0x49e296`. Both paths advance
0x60-byte records and rejoin the next-material loop (`0x49e291`, `0x49e878`).
The outer kind check (`0x49e064..09b`) requires the first kind throughout;
classic/effect MATERIAL6 may mix, MATERIAL5/MATERIAL6 may not. The material
limit is 100 (`0x49e256`, `0x49e875`). Classic switches retain the established
post-row-overwrite order in [the classic contract](classic-materials.md).

Text points share only with the same nonzero smoothing value, equal positions,
and all four UV differences at most one fixed-point unit
(`0x499985..0x499a1d`, comparisons at `0x4999a1/b2/c3/d4`). New normals start at
zero (`0x499a98`); final normalization calls `0x482350` and stores three signed
shorts after shifting by two (`0x4a0d0c..51`). Text group tangent records are
explicitly zero (`0x4a0793/79a`). The host grammar and detailed AP arithmetic
are documented in [the text loader note](../body-text-loader.md); these new FL
anchors establish sharing and output constraints, not complete arithmetic parity.

A mixed MATERIAL6 witness has two noncoplanar triangles sharing an edge: one
kept two-sided classic and one atlased effect. Source has four points; atlas
and emitted text have six. Triangle positions and material assignments remain
unchanged and atlas UV validation reports zero failures, yet text reloading
changes four of six corner normals by 45 degrees, including two kept classic
corners. `N:` comments cannot restore these normals. A general binary-tree text
formatter therefore cannot establish preservation merely by reparsing.

Accepted implementation boundary: emit text with an exact semantic validator;
when atlas UV changes alter normals, keep the entire affected source smoothing
component (closed over materials) and replan. Independent components may still
atlas. Retain original declarations, flags and switch provenance; respect the
100-material text budget by reducing merges rather than dropping declarations
that could affect registration or the model tangent declaration. Text-source
zero tangent records remain zero. An input needing real precomputed binary
tangents cannot use this fallback without a separately qualified alternative.
The ordinary AP serializer remains unchanged.
