# Combined glow, materials and selection diagnostics

2026-09-13. Retained production source `541e380`; installed in X3 after final
evidence review. The current build combines the
[authored-glow correction](../architecture/bloom-authored-glow.md), 162 material
pairs, 20 default-off SM2 emission pairs, and optional native game-phase tracing.
The [single install record](../../verification/results/linear-material-install.json) retains its binary,
scoped evidence and rollback identities. EXE and bottle configuration are unchanged.

## Failures resolved before installation

The first clean candidate (`cc3d25c`) failed the unchanged linked x87 audit:
light shader setters reached the MinGW formatter through the new XT availability
diagnostic. Reviewed `541e380` captures a bounded integer event and formats it
after Present or at final retirement. First-event identity and lifetime-once
behavior survive binding changes, Reset and reporting reentry. Two focused host
tests and strict x86 compilation pass; the rebuilt DLL passes the full linked
audit with 218 reachable functions.

The WRAP and XT live fixtures initially drew the same triangle twice with the
same indexed-draw identity. Production correctly poisoned ambiguous history,
so the fixture's next-frame matched-motion expectation failed. Reviewed fixture
commit `3d5c14e` uses two identical index triples at start indices 0 and 3. This
preserves geometry, ordering and all output assertions while giving the draws
distinct stable identities. Three affected tests pass. Only the fixture was
rebuilt; the production DLL and matching seam remain unchanged. The already
passed corpus and emission modes are unaffected and retain their original
fixture provenance.

## Scoped qualification

| Check | Result |
| --- | --- |
| Material corpus, ownership/TAA/material on and off | 8 configurations, 2,576 frames, 32,516 checks; exact native/alpha/temporal twins, sampler refusals, Reset and shader retirement pass |
| Scalar WRAP, depth on/off, per-draw/lazy, material on/off | 8 configurations, 144 frames, 3,208 checks; 46,128 matched motion pixels per configuration, exact alpha/RT1/RT2 twins and caller-state restoration pass |
| XT14 live matrix and perspective transport | 8 configurations, 628 frames, 10,836,608 checks; depth on/off, material on/off, per-draw/lazy, repaired DEFAULT fallback and two perspective transport diagnostics pass |
| SM2 emission20 live composition | 4 functional configurations, 208 frames, 2,185,516 checks; 51 exact TAA readbacks per configuration, accepted/disappearing coverage for all 20 pairs, Reset and source-failure recovery pass |
| Production DLL load | 8 checks and all 17 D3D9 exports pass |
| Bloom packaging | All nine qualified shader byte streams occur in the retained DLL; component GPU/state/Reset evidence is reused |

The corpus and emission records identify the original retained fixture; the
WRAP/XT records identify its reviewed history-key correction. Independent review
has accepted corpus, corrected WRAP, XT and emission results. Detailed numerical and
state claims belong to the canonical records:
[material corpus](../../verification/results/bottle-X3/linear-material-live.json),
[WRAP](../../verification/results/bottle-X3/linear-material-live-wrap.json), and
[emission](../../verification/results/bottle-X3/linear-emission-live-gpu.json), and
[XT](../../verification/results/bottle-X3/linear-material-live-xt.json).

Emission's eight-sample medians are 1.75765 ms native and 2.21305 ms enhanced,
a 0.4554 ms difference for this scoped workload. These are CPU/driver/GPU-event
completion times from separate processes, not GPU-only cost or gameplay FPS.
No production hot-path work was added by the diagnostic correction; setters
perform only bounded integer stores on the first unavailable event.

Native-Windows runtime, actual gameplay appearance and gameplay performance are
unverified. The emission fixture uses an explicit scene-owner seam; prior owner,
original-once and lifetime qualification remains applicable. Bloom's changed
extraction has component GPU evidence; emitter appearance still requires user
screenshots because F8 readbacks precede final bloom. The native selection trace
has CPU/ABI evidence but cannot identify the gameplay stall until a user run.
