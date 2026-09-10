# Installed Preview backend: RESZ and depth sampling

The installed x86 D3D9/WineD3D backend implements RESZ. The failed D24X8→INTZ copy is explained by its depth/stencil format compatibility check, while native D24X8→D24X8 copies successfully but samples through automatic shadow comparison. A successful SetRenderState or format query alone cannot establish usable raw depth.

## Exact binary provenance

Inspected the Preview bundle's `Contents/SharedSupport/CrossOver/lib/wine/i386-windows/` binaries. Their hashes exactly match the Steam bottle's `drive_c/windows/syswow64/` copies, which supply x86 processes through Windows filesystem redirection. A reported `C:\windows\system32` path must not be confused with the host-visible x64 files in that directory.

| x86 module | SHA-256 | Preferred base | Image size |
| --- | --- | --- | --- |
| d3d9.dll | `58cc36cf74128ae4b6211100430d146c3692808146d8d2075e6c5d846162f8cf` | 0x10000000 | 0x2c000 |
| wined3d.dll | `f4997bc0465de7e87bac9921bf0274db00ac3b3ba0754fa03f1f33e309a8e863` | 0x10000000 | 0x2d0000 |

The standalone probes force builtin D3D9 with a process-local override and report the loaded modules. All addresses below are **RVAs in those exact binaries**, not hook addresses. Raw objdump output remains in `/tmp/x3-installed-{d3d9,wined3d}-disasm.txt`, outside the repository. No game or production file was edited for this analysis.

## Trigger and swallowed failure

D3D9's SetRenderState implementation starts at RVA 0x8992. At 0x89ed–0x89fb it explicitly requires state **154 / D3DRS_POINTSIZE** and value **0x7fa05000**. The `RESZ` FourCC 0x5a534552 is a format-query identifier, not this trigger value.

After the trigger, it reads texture stage zero, obtains its resource descriptor and accepts a small depth-format list. This includes INTZ, internal X8D24_UNORM (0x18) and D24_UNORM_S8_UINT (0x4b). The internal DF16/DF24 constants are also checked; that does not guarantee those resources can actually be created.

It obtains the current depth/stencil view, then calls imported `wined3d_device_context_resolve_sub_resource` at D3D9 RVA 0x8a8f. Named import parsing corroborates that call; it is not an inferred nearest-symbol label. The D3D9 function returns S_OK at 0x8a9c even if a required texture/view is absent or the internal operation fails.

WineD3D's exported resolve begins at RVA 0x4bca0. For supported 2D resources it invokes `wined3d_device_context_blt` at 0x4be2c and does not propagate its result. The blit implementation begins at 0x9f490. At 0x9f673–0x9f69e it compares the presence of **both depth and stencil** in source and destination. A mismatch branches to the rejection path at 0x9f6d9, returns `0x8876086c` / INVALIDCALL, and has the embedded diagnostic “Rejecting depth/stencil blit between incompatible formats.”

The installed format metadata confirms the mismatch:

| D3D format | Internal format | Channel metadata RVA | Depth bits | Stencil bits |
| --- | --- | --- | ---: | ---: |
| D24X8 (77) | X8D24_UNORM (0x18) | 0x21f9d8 | 24 | 0 |
| INTZ | INTZ (0x5a544e49) | 0x21fb58 | 24 | 8 |

D3D9's format conversion jump table at RVA 0x1f690 maps public D24X8 to internal 0x18; WineD3D's format-name switch independently names that ID. The channel rows contain the corresponding `24,0` and `24,8` bytes. Thus a dummy draw cannot repair this particular format mismatch.

## Why native D24X8 reads as one

Installed WineD3D sampler construction at RVA **0x90ef1** loads the texture/resource flags at +0x20, extracts bit 13, and writes the sampler's comparison-enable field. At **0x90f00** it writes comparison function **4 / LESSEQUAL**. This value is derived from the resource, not a public D3D9 sampler-state comparison toggle.

The GL sampler path at 0xa34ea–0xa34f9 selects `GL_COMPARE_REF_TO_TEXTURE` (0x884e) for `GL_TEXTURE_COMPARE_MODE` (0x884c). GLSL sampler declaration generation at 0x6a220–0x6a22c chooses `sampler2DShadow` when the corresponding comparison mask is set. These are observed installed instructions. Historical [Wine sampler construction source](https://raw.githubusercontent.com/wine-mirror/wine/wine-10.0/dlls/wined3d/stateblock.c) helps name the fields, but its exact flag layout is not assumed to match this installed build.

A zero comparison reference therefore produces one for nonnegative stored depth; that is not evidence that the copy left every pixel at depth one. The original assembly-shader probe varied reference z through 0.1, 0.5 and 0.9 and observed the expected comparisons against the copied near/far/clear values, even after clearing the original source. That establishes a real independent copy and shadow semantics.

No safe public D3D9 sampler-state escape to raw native-D24X8 reads was identified in this targeted inspection. Changing internal flags or relabeling a resource as INTZ would bypass backend invariants and is not a validated format reinterpretation mechanism.

## Numeric probe cross-checks

These independent original-mesh/shader tests were run by the camera-analysis work, not inside X3:

| Source → destination / operation | Observed result |
| --- | --- |
| D24X8 → INTZ, correct magic with and without dummy draw | Destination remains poisoned at 0.125; SetRenderState still succeeds |
| D24S8 → INTZ, correct magic | 36 numeric samples and 6 state-restoration checks pass, including Reset |
| D24S8 → INTZ, literal RESZ FourCC control | Does not perform the expected copy |
| D24X8 → DF24 | Destination CreateTexture returns INVALIDCALL |
| D24X8 → native D24X8, ordinary depth sampling shader | Returns one; inadequate raw-depth test because reference is zero |
| D24X8 → native D24X8, explicit reference-z comparison shader | 108 samples and 18 state checks pass, including Reset and source destruction by clear |

See `verification/results/depth-resolve-d24s8-intz-summary.json`, `depth-resolve-d24x8-intz-summary.json`, `depth-resolve-d24x8-df24-summary.json`, and `depth-resolve-d24x8-shadow-summary.json`. These tests establish the listed format/trigger cases at their synthetic size and sample count, not arbitrary game resource compatibility or multisample behavior.

## Actionable GPU path

For the existing D24X8 source, RESZ into a native D24X8 texture preserves the original engine depth allocation and produces a sampleable comparison image. Direct INTZ copying does not work for that format pair on this backend. An engine-wide change to D24S8/INTZ would be a separate, more invasive resource-ownership experiment.

A public-API, GPU-only candidate is to reconstruct raw depth into R32F using point comparison samples against successively refined reference values. Twenty-four binary-search comparisons can target the 24-bit depth resolution; the implementation must explicitly handle zero/one boundaries, comparison equality, quantization and float rounding. It would add texture-fetch cost, so precision and timing must be measured with an original fixture before considering game integration. It is a proposed reconstruction path, not yet established by the comparison-only evidence here.

This path does not require CPU depth readback or unsafe backend format changes. R32F output precision is independently validated by the camera fixture; the missing gate is faithful reconstruction and its GPU cost. Keep the original source bound only for RESZ, restore POINTSIZE and texture stage zero, then unbind the depth destination before sampling it. Integration still requires the already-identified scene boundary, state restoration and resource lifetime handling.

Subsequent checkpoint: the [GPU comparison decoder](../verification/depth-decode.md)
now passes 256 precision samples with explicit endpoint handling, and its CPU
arithmetic model distinguishes the near-one rounding counterexample found by
independent review. Cost measurements include event polling overhead and remain
bounded standalone observations. The earlier comparison-only evidence above
does not itself establish these later decoder results.
