# Background and stardust temporal coverage

2026-09-13. **The captured nebula cohort has camera-centered positional motion;
the stardust cohort does not justify a camera-only assumption. Neither shared
pixel-shader hash permits a static global exemption from reactive coverage.**
This is historical evidence, not a reusable runtime admission proof.

## Sources and bounded scope

Streamed source: `/tmp/x3-iteration05-completed-snapshot.log`, 216,605,445 bytes,
SHA-256 `e5beaa861d04659fe9c7df05a01845bd05d656a33c643f4b484ff379cf3ccaf8`,
also pinned by [material color inputs](material-color-inputs.md).
The 24 structurally accepted frames are 1794–1797, 1975–1978, 2435–2438,
2806–2809, 3047–3050 and 4096–4099. Background means the region between the
initial full color/depth Clear and the depth-only Clear, not a shader name.
The [ordering census](emission-draw-order.md) independently establishes these
regions. Their 120 background draws comprise 104 of the pair studied below
and 16 `37c34a7478544c14` / `5f82ecacd39529cd` draws not classified here.

Local derived queries: `/tmp/x3-background-contract.py`,
`/tmp/x3-background-contract.json`, `/tmp/x3-background-main-camera.json`.
Original programs were inspected under `/tmp/x3-shader-sweep/disassembly/`;
the complete archive manifest is `/tmp/x3-shader-sweep/manifest.json`.
Targeted read-only Ghidra output is `/tmp/x3-background-input-producers.c`;
the existing material-path output is `/tmp/x3-render-functions.c`.
EXE preferred base is `0x00400000`, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`.
Raw shader/decompiler outputs remain local and untracked.

## Captured camera-centered background

Pair `7b6393fe2d3e1d85` / `6109cf64c03529dd`, VS1.1/PS1.1, supplies all
104 studied background draws. Its VS transforms position through WVP c0–3
and UV through affine c4/c5; its PS returns sampled RGB and texture alpha
multiplied by uniform c0.x (`g_AlphaValue`). Neither program has a direct time
input. CPU changes to geometry, matrices, fade or texture data remain possible.

- All draws are scoped, with Z test/write off and RGB-only writes. Eighty use
  `SRCALPHA/ONE` with fade 0.9960784316062927; 24 disable blending and have zero
  captured PS c0. Float getters succeed with sparse-zero encoding.
- All 104 use identity UV c4/c5 and revision-1 VBs. Across 78 adjacent
  comparisons matched by node/handle/VB, UV, fade, texture identity and buffer
  revision do not change. Observed buffer revisions are not content hashes;
  texture identity does not certify immutable texture contents.
- Object world 3×3 remains fixed per node. World and view matrices change in
  48 of those comparisons, but world×view translation cancels within
  **3.71×10⁻⁷** in the captured float matrices. Thus the position changes are
  camera-centered in this sample, rather than ordinary finite-distance parallax.
- For all 24 frames, background and first scoped main-scene draw have exactly
  equal captured view 3×3 and projection x/y rows. This supports the existing
  [far-plane rotational mapping](../architecture/temporal-integration.md#camera-reprojection-for-sentinel-pixels)
  for this cohort; it does not prove every background view shares that camera.

The generic material routine `0x004c0150` resolves `g_TexMatrix` and chooses
helper `0x004b9280` or `0x004b92c0`. Targeted inspection confirms the first
supplies an identity-valued integer matrix (diagonal `0x10000`), while the
second forwards supplied matrix elements to `0x004b9110`. The latter helper's
conversion was not separately re-proved. This is a real alternative input path,
not evidence that these captured backgrounds animated their UVs.

## Changing stardust and late shared programs

The pre-bloom pair `5e484a06672e28fb` / `0a523f33ac47ae05` occurs in 24
unscoped draws, with Z test/write off, `SRCALPHA/INVSRCALPHA` and RGB-only
writes. Its stride-24 VB revision advances in **all 18 adjacent-frame
comparisons**. The VS transforms supplied position by view-projection c0–3,
forwards UV and vertex color; the PS returns texture RGB with texture alpha
times vertex alpha. Camera matrices cannot explain arbitrary changing vertex
positions/UVs/alpha, so this path needs conservative coverage unless a separate
correspondence is established. Revisions prove observed writes, not the exact
changed attributes. The older [motion study](iteration04-camera-motion.md#buffer-revisions-and-unsupported-motion-paths)
independently found the same changing-buffer limitation.

Targeted `0x004bfd40` inspection confirms a CPU-payload copy into a stride-24
dynamic buffer and a `g_mViewProjection` binding. This is a matching source-path
candidate, not a trace-proven assignment of every captured draw to that routine;
the upstream simulation/vertex-alpha producer remains untraced.
The late pair `f36fc43f30b19d71` / `0a523f33ac47ae05` occurs in 48 draws and
directly emits supplied clip-space positions and vertex color, without a camera
matrix. Its temporal role depends on the actual canonical input boundary.

## Consequence and smallest next proof

Preserve far-plane camera reprojection. A narrowly established stable background
class can avoid marking most of the image reactive; blanket background masking
would discard useful accumulation. Conversely, pre-bloom changing blends need
coverage even over opaque pixels whose depth/motion still describe the surface
behind them. Neither PS name/hash nor the presence of a view matrix proves safety.
The next offline proof should establish this background cohort's camera-centered
transform producer, UV/fade producers and texture lifetime/immutability conditions.
Stardust requires its own vertex/alpha producer evidence before any exemption.
No runtime classifier, new instrumentation, gameplay request or temporal-policy
change follows from this study.
