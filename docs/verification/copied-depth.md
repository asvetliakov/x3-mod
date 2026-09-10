# Original-preserving scene-depth snapshot

The production ownership layer can explicitly snapshot the original automatic
D24X8 surface to a separate D24X8 texture using the verified Preview RESZ path.
It never substitutes the application's surface. The texture exposes comparison
sampling on this backend; the separate [decoder](depth-decode.md) produces R32F
device depth for temporal inputs.

This is an opt-in, uninstalled component. `X3M_OWNERSHIP=1` with
`X3M_DEPTH_COPY=1` prepares storage; no game pass currently calls
`copy_auto_depth`. Automatic selection of the scene boundary, decoder dispatch,
jitter and temporal history integration remain outstanding.

## Content, compatibility and lifetime

The original [fixture](../../verification/probe/copied_depth_fixture.cpp) builds
the current ownership source, renders synthetic geometry through the wrappers,
calls the explicit copy, destroys the original depth by clearing it, and decodes
the retained snapshot with the production HLSL. It passes **634 checks and 32
numeric samples**, covering normal and pure devices, each before and after a
resizing Reset from 64×64 to 80×48.

- Both native and logical source surfaces remain D24X8. Both getter identities
  is unchanged. Ordinary D24X8 depth copies succeed in both directions.
- Native no-stencil behavior remains intact with stencil enabled and NEVER set.
- Copies are rejected when the source is unbound, a different source is bound,
  or an application state block is recording.
- The explicit copy works both outside and inside a scene, with no injected
  geometry. Sentinel texture zero and exact POINTSIZE bits are restored.
- The saved scene contains 0.25, 0.75 and clear-depth 1 after the original surface
  is cleared. A subsequent copy observes the newly cleared source instead.
- Source clear epochs advance while the saved copy retains its earlier epoch.
  These counters describe content; they are not temporal camera-history epochs.
- Holding the original application surface still makes Reset fail with
  D3DERR_INVALIDCALL. Copy storage
  is retired before that Reset; releasing the surface permits a resized retry.
  The device reaches final logical release after all child references are gone.

The fixture's decoder dispatch is verification scaffolding, not a production
state-preserving compositor. Copy restoration evidence applies to the two states
the copy changes. It does not prove arbitrary decoder caller-state restoration.

## Device loss review

Independent review found that loss during copy preflight could leave an earlier
snapshot marked valid. The fix retires snapshot and adopted renderer resources
when DEVICELOST or DEVICENOTRESET is observed, including source-binding queries,
Clear bookkeeping and state restoration. Loss takes precedence over an earlier
ordinary operation failure. No further ordinary state setters run after observed
loss. Rejected operations that never touch GPU state preserve a still-valid
earlier copy.

The separate [loss fixture](../../verification/probe/copy_depth_loss.cpp) passes
**357 checks across 33 cases**. It injects both loss codes at copy, initialization
and stateblock boundaries and verifies borrowed-view invalidation, resource
retirement, rejection of new history adoption and ordinary-failure controls.
Optional initialization failure preserves the application's successful Reset
result. Ordinary allocation failure disables the snapshot without declaring the
device lost. It complements actual GPU copy samples; synthetic loss injection
is not a reproduction of a physical GPU reset.

## Reproduce

```sh
python3 verification/probe/run_copied_depth.py
python3 verification/probe/run_copy_depth_loss.py
```

Both runners freshly compile their fixtures and record source/executable hashes.
They use hidden standalone devices in CrossOver Preview's Steam bottle with a
process-local override, without launching X3 or changing its installation.
Results are [numeric evidence](../../verification/results/copied-depth.txt),
[copy provenance](../../verification/results/copied-depth-summary.json), and
[loss evidence](../../verification/results/copy-depth-loss-summary.json).

The [RESZ experiments](depth-resolve.md) establish the exact installed-backend
route. A successful render-state HRESULT alone cannot establish that the internal
copy succeeded. The historical [INTZ substitution experiment](auto-depth.md)
was removed because it changed depth-copy compatibility; it is not the current
implementation.
