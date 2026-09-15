# Linear-material decoupling audit (2026-09-15)

The user plays without `--linear-materials` (original hull shading) and wants the
features gated on it to work without it. This note is the audit of every gate,
the decision per feature and the evidence for the one feature decoupled here:
`--screen-emission`. Critic context: [material-investment.md](material-investment.md)
item 7 and its "retain or decouple" reasoning; the packed route itself is
[screen-emission-region.md](screen-emission-region.md) steps C–E.

Dependency kinds: **(a) runtime** — the feature consumes linear-variant shaders, the
material admission/recovery state (`linear_material_refusal`, `material_contract`,
`xt_default` readiness, cutout capability) or the cutout/fade routes; **(b) gate** —
only a launcher/DLL option check names `--linear-materials`.

## Audit

| Feature | Where it is gated | Kind | Decision |
| --- | --- | --- | --- |
| `--linear-distance-fade` (default on with materials + TAA) | `tools/manage.py` 193–195; `capture.cpp` `linear_distance_fade_requested = fade && linear_material_requested && taa`; `motion_output.cpp` `configure_linear_distance_fade` (`requested && linear_material_requested_`) | (a): `linear_distance_fade_variant` transforms the six Asteroid source-over pixel programs with `linear_material_config_` (the linear lobe sum, fill); the in-place fade bracket composes a *linear* source over the compatibility-encoded scene, so without the material law there is no linear source to compose | Stays gated (by construction). `--fade-witness` no longer requires it: the witness also serves the packed route (`fade_witness_interval_ = distance_fade || screen_emission`), so the launcher now accepts `--fade-witness` with `--screen-emission` alone |
| `--sun-shadow-lane` | `manage.py` 200–201; `capture.cpp` 1845 (`asked && motion && taa && hdr && linear_material_requested`) | (a): the lane's `sun_material_variant` / `sun_xt_variant` are extracted from the linear material variants (`linear_material_pixel_variant_sun_share`, `motion_output.cpp` 2597–2619); its `sun_motion_variant` fallback exists only inside `entry.variant` registration, whose coverage/contract is the material one | Stays gated; an original-shading sun response is the separate design question of material-investment.md |
| Material gains (`--material-*-gain`, `--lightmap-emissive-gain`) and `--material-fill` | `manage.py` 226–235; `capture.cpp` `material_gain(...)` into `LinearMaterialConfig` | (a): DEFs of the linear material transformer only | Stay gated (meaningless without the variants) |
| Cutout arm (`linear_cutout.h`) | `motion_output.cpp` `probe_cutout_caps` 754, `cutout_arm_configured` 823, `cutout_pair` 3609 (`linear_material_requested_ && …`); the header itself reads no option | (a): substitutes the linear cutout pair for the native one and owns its coverage verdict | Untouched |
| Fade-band route (`fade_route_core.h`, `X3M_FADE_ROUTE`) | `capture.cpp` 2111 logs `enabled` with `linear_material_requested`; the draw gate lives in the material route (`motion_output.cpp` 3838 onwards) | (a): a material-route arm | Untouched |
| `--screen-emission` (packed bullet bracket, policy 8) | `manage.py` 212–213; `capture.cpp` 2116 `asked && linear_material_requested && taa`; `motion_output.cpp` 567 `requested && linear_material_requested_` | **(b) plus one shared shadow.** The route consumes: the `PackedScreen` producer (an SM1 emitter promotion, `linear_emission_sm1.cpp`, no material config), the composition pass (`LinearEmissionPass`, gated per frame and per draw on TAA + HDR AgX + gamma2.2, never on materials), the locked-prefix bound, the HDR FP16 scene `hdr_->target()` and the M coverage plane. It reads no material variant, contract, refusal or cutout state. The one linear-material by-product it used was the **stage-0 `D3DSAMP_SRGBTEXTURE` shadow** at its readiness gate (`motion_output.cpp` 3442): that shadow was fed by the sampler-state hook (installed only with mip bias or linear materials, `capture.cpp` 1875) and by `resync_samplers` (queried only with linear materials, 725). Without them the gate refuses every bullet as readiness (refusal 2) | **Decoupled** (this change): the two option gates now require the HDR/TAA prerequisites of the additive emission route (`--taa --motion-output --ownership --hdr --hdr-tonemap`, gamma2.2 decode); the DLL gate (`capture.cpp`) also reads `X3M_OWNERSHIP` itself, so a raw-environment launch without the Unlock scan is refused and logged (`screen_emission_mode … ownership=0`) rather than admitting nothing silently (previously a launcher-only check); the sampler hook is installed when the option is on; `resync_samplers` queries stage 0's sRGB flag when the option is on. Gain (default 1), brackets, hull bound, fade route and the step E law are unchanged |

## Why step E holds without linear hulls

Step E composes `C = encode(g·decode(B) + (1−g)·decode(A))` on the scene target. That
target is the HDR pass's FP16 `A16B16G16R16F` scene, and its encoding is the game's
native gamma code value in both configurations: the linear material variants
*compatibility-encode* their linear result back into that encoding
([linear-bump-materials.md](linear-bump-materials.md), [linear-distance-fade.md](linear-distance-fade.md)
"A is the compatibility-encoded HDR scene"), and without them the original shaders write
the same native code values directly. The tonemap decodes with gamma 2.2 either way
(`X3M_HDR_DECODE=gamma2.2`, enforced by both gates). So the packed bracket's inputs (native
ONE/INVSRCCOLOR accumulation in the red lane, `decode(A)` in the green lane) and its
publication are identical; only the hull pixels *around* the bolt differ in value, which
the law never reads except as `A`. At g = 1 the composed pixel equals native by construction
in both configurations.

## Evidence

- Host: `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_screen_emission_live verification.analysis.test_fade_region verification.analysis.test_linear_distance_fade_report` (launcher tests: the option is refused without `--ownership`, `--hdr-tonemap`, `--taa` or gamma2.2 decode and accepted without `--linear-materials`, publishing `X3M_LINEAR_MATERIALS=0`, `X3M_LINEAR_DISTANCE_FADE=0`, `X3M_SCREEN_EMISSION=1`).
- Live: `run_linear_distance_fade_live.py --screen-emission` gained the trio
  `screen-nomaterials-functional` / `-off` / `-gain2` (`X3M_LINEAR_MATERIALS=0`,
  `X3M_LINEAR_DISTANCE_FADE=0`, `X3M_TAA_MIP_BIAS=0` so the sampler hook owes its
  installation to the option alone). The runner requires the no-materials packed totals to
  equal the materials-on totals, the witness `outside=0`, and validates the step E chain
  against the no-materials native twin and gain run (`result['chain_nomaterials']`). Numbers
  are in [../verification/screen-emission.md](../verification/screen-emission.md).

## Oracle assumption of the live run

`screen_expected_sources` (`run_linear_distance_fade_live.py`, the `refused` term) assumes that
with the fade route off the fixture's Asteroid fade draws are plain non-producer pairs in
`prepare_composition`: `fade_sampler_mask` is zero without `distance_fade_requested_`, so
the draw takes the `!fade && !emitter && !screen` branch (histogram bit 0, native, no frame
stop), exactly as a bullet pair without the option does. The live run agreed with the
oracle on every source of the no-materials trio (per-source `refused`, the frame's refusal
histogram, `preparation`/`readiness` counts and the M footprint); it is an assumption about
the DLL's branch, not an independent measurement of it.

## Performance

With `--screen-emission` and no linear materials the launcher's mip bias defaults off, so
the option alone installs the `SetSamplerState` hook (`capture.cpp`, hook 69) in the user's
configuration. The runner's extra timing pair (`screen-timing-1280x768-pair0-screen{0,1}-nomaterials`,
materials 0, bias 0) measures that configuration off/on; the numbers are in the ledger row.

## Boundary

Nothing here builds a hybrid material route. `--linear-distance-fade` and
`--sun-shadow-lane` would need new non-material shader variants and their own coverage /
recovery proofs to run without linear hulls; that is a design decision, not a gate change.

## Review notes retained (2026-09-15, second review)

Two pre-existing low-severity Reset-path observations, not fixed at this
checkpoint: `before_reset()` clears the emission pair/variant shadows but not
the screen ones (harmless because `after_reset` resyncs the shadow before any
draw); a failed Reset leaves stage-0 `srgb_known=false` without resync, so
screen draws refuse (reason 2, native) until the game rewrites the sampler
state or a later Reset succeeds. Both are cleanup candidates.
