# Camera and lighting register evidence

Read from CTAB metadata in the **runtime-captured** shader bytes using
`tools/analysis/shader_constants.py`. Full metadata is in
`verification/results/shader-registers.json`. No binary shader assets are stored
in this repository. Names/register ranges alone do not prove engine matrix layout,
handedness, camera jitter behavior or light units.

## Common material vertex shaders

Observed hashes include `53a0a641107ed76c`, `37c34a7478544c14`,
`4944d81dfe531b37`, `494fe349b8bc12ec`, `b0602757fce6e870`,
`c30104cb0efb6675`, and `167eb2d5629ab9d3`.

| Parameter | Register range | Metadata |
| --- | --- | --- |
| g_LightPoint | float c0–c23 | Structure array, 24 registers total |
| g_mWorldViewProjection | float c24–c27 | Matrix, parameter class 3 |
| g_mWorld | float c28–c30 | Matrix, 3 registers |
| g_mWorldIT | float c31–c33 | Matrix, 3 registers |
| g_mViewInverse | float c34–c36 | Matrix, 3 registers |
| g_nNumLightPoint | integer i0 | Separate integer register namespace |

The integer count is **not captured** by the current float-only register snapshot.
Do not mistake it for float c0. Extending typed constant capture is a prerequisite
for reliably decoding the engine light list.

## Other camera paths

- Particle VS `36f98d151fd6b0c6`: `g_mView` c0–c3, `g_mProj` c4–c7.
- Shared GUI/nebula VS `7b6393fe2d3e1d85`: WVP c0–c3. This shared shader reinforces
  that shader hash alone cannot decide which draws receive temporal jitter.
- VS `be199829a9bb78db`: WVP c0–c3, world c4–c6, world inverse transpose c7–c9,
  view inverse c10–c12, directional light `LightDir_Dir0` c13.
- VS `89193868c61c3846`: view-projection c0–c3, world c4–c6, view inverse c7–c9.

## Temporal implementation gate

Capture at least two controlled frames with camera movement and stationary
geometry. Decode the relevant float bit patterns and verify multiplication/order
against known screen motion. Then separate per-object world change from camera
change, identify stable geometry identity, and locate depth before postprocessing.
Test jitter on one known scene path and confirm GUI/particles/sky requirements
individually. A single full-screen history blend without this evidence is not the
required TAA implementation.

## Sources

The local MinGW `d3dx9shader.h` defines CTAB's 28-byte header, 20-byte constant
records, 16-byte type records and register-set enums. The analysis tool validates
all offsets against the enclosing comment block. Production code does not rely
on these metadata offsets; this is an offline research tool.
