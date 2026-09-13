# Standalone BloomPass fixture

2026-09-13. `verification/probe/bloom_pass_fixture.cpp` links the actual
`src/renderer/bloom_pass.cpp` with `X3M_BLOOM_PASS_FIXTURE`. It uses public D3D9
interfaces and the supplied native method table, with authored sources compiled
by D3DX only inside the fixture. The final RGB copy uses the existing
`src/temporal/hdr_writeback_ps.hlsl` source intended for the production bundle. This is not an installed renderer or game
integration. Native Windows runtime remains unverified.

## Current evidence

Build-only x86 cross-compilation passed using `-O2 -Wall -Wextra -Werror`, SSE2
floating point and the four-byte incoming-stack contract. Five host tests pass:
strict completion parsing, corpus coverage, skipped-sharpen sensitivity,
independent image acceptance and deterministic inputs. **No GPU execution has been performed for this fixture.**
The build-only summary deliberately records `passed=false` and
`gpu_execution_verified=false`; successful compilation is not runtime acceptance.

Build without Wine:

```
python3 verification/probe/run_bloom_pass.py --build-only
python3 -m unittest discover -s verification/analysis -p test_bloom_pass_fixture.py -v
```

Once the orchestrator has confirmed the game and other fixture runners are
absent, run serially through the required lock:

```
python3 verification/probe/wine_lock.py python3 verification/probe/run_bloom_pass.py
```

The runner uses CrossOver **Preview**, default fixture bottle **Steam**;
`X3M_FIXTURE_BOTTLE=X3` selects the arm64/FEX bottle. The summary records bottle
architecture and emulation environment. Neither command launches the game.
Runtime results go to the bottle-specific `bloom-pass-summary.json`, with raw
binary inputs, expanded sources, compiled bytecodes, images and stdout/stderr
retained in a fresh temporary directory. Existing output directories must be
empty. A failed or incomplete fixture cannot publish a pass.

## What the GPU gate exercises

Twenty-four final-image cases cross all three decoders, even 8×6 constant and
odd 9×7 structured inputs, bloom strengths zero/0.5 and sharpen zero/0.75. The
retained FP16 source uses DEFAULT-pool texture storage populated by a public
SYSTEMMEM upload. Each candidate is built by the actual extraction, reduction,
reconstruction, AgX and optional display RCAS programs. This executes all six
extraction variants across the corpus.

The final RGB oracle is the independently authored double-precision AgX,
nine-tap bloom and scalar RCAS reference. It compares against the ideal fused
result, excluding intermediate FP16 stores. The predeclared acceptance limit is
three 8-bit codes per RGB channel for this bounded moderate-radiance corpus;
alpha must match the simulated original exactly. The limit combines storage,
sampler, shader arithmetic and UNORM conversion and is not a universal error
bound or a replacement for the existing bloom sampler precision fixture.
Host corruption controls reject an unchanged original image, alpha corruption,
truncated images and missing/duplicate/malformed/nonterminal result records.

The first image also runs sixteen transaction controls (40 total transaction iterations
including the other 23 image cases and a post-Reset case):

| Control | Required result |
| --- | --- |
| Success | Actual RGB changes; exact genuine original alpha and outgoing state |
| Setup failure with armed rollback fault | No candidate write, no rollback copy, pristine original and state preserved |
| Candidate draw PS-setter failure with armed rollback fault | Setter failure observed, no native draw issued, no rollback, exact original/state |
| Backup failure | No main write, original pixels and state unchanged |
| Candidate failure after actual draw | Exact original RGBA rollback and state recovery |
| Candidate failure plus partial initial restoration | Original and state recover; pass disables |
| Candidate failure plus rollback failure | Real changed RGB remains; image failure reported and pass disables |
| Candidate failure plus both partial restoration faults | Original RGBA recovers; observable changed state remains and pass disables |
| Changed frame / changed Reset generation | Consume token without write |
| before_reset / a later prepare | Old token cannot write |
| Preparation draw failure | No ticket, exact baseline restoration |
| Preparation partial restoration failure | No ticket, unknown state reported and pass disables |
| Allocation / state-save failure | No ticket and exact baseline preserved |

Every commit is followed by a second attempt with the copied token; it must not
write. Public-return transient texture-view counts are zero. `before_reset`
removes every persistent image reference and allocation byte while retaining
only attach-time programs; `shutdown` has zero references. The final pass retains its attach-time programs across a real native device
`Reset`, after `before_reset` revokes a newly prepared live ticket and the fixture
releases its own default-pool objects and restores the swapchain binding. The
old ticket must remain invalid. A fresh-generation prepare/commit then recreates
images using those surviving programs and passes the independent image oracle
before shutdown. Thus shutdown cannot hide a leaked Reset-sensitive object.

The hostile state includes all supported MRT slots (RT1 has a distinct image),
known null depth, full initial and smaller outgoing viewport, scissor, software
VP on a mixed device, nonnull vertex/pixel shaders, FVF before preparation and
explicit declaration afterward, nonnull stream zero, s0/s1 textures and sampler
fields, c0–c31, blend/depth/stencil/fog/sRGB/clip/fill/color-mask states and
untouched s2/MRT color-mask sentinels. A public NPatch mode of 2.5 is seeded
when the backend accepts and retains it (confirmed by its getter) and is
compared on restoration; a rejection or ignored/clamped value records
that hostile-mode scope as untested. A native-table draw wrapper checks actual
`GetNPatchMode()==0` and adaptive tessellation disabled immediately before
forwarding every real injected draw. Adaptive tessellation starts enabled when
the setter accepts it and a getter confirms TRUE, with rejected or unretained
values explicitly scoped in the summary; its
exact render state is also checked after restoration.
This proves disabling independently of whether tessellation changes pixels. Exact state and reference identities are
compared before/after preparation and outgoing commit. The partial restoration
fault deliberately leaves a changed color mask so retry-from-the-original-state
is observable. The RT1 pixels must remain byte-exact. Required mixed VP and MRT
capability failures fail this gate rather than silently skipping controls.

## Limits and performance

The simulated original is genuine outgoing state setup and one explicit `ColorFill`; it does not prove the game's original CPU outputs, invocation
bridge, material-manager cache behavior, hooks, active-query/state-block
admission, two-device ownership, wrong-thread admission, non-null depth or lost
device handling. A saved-method wrapper injects one candidate draw `SetPixelShader` failure;
there are no partial driver `StretchRect` injections or exhaustive individual
getter/setter failures. The supplied pass fault
seams cover actual candidate draw writes and actual partial state restoration;
those are more limited than arbitrary driver failure qualification.

Fixture allocations, state readbacks, image downloads and CPU oracle work are
diagnostic. Production BloomPass has no CPU image readback or host allocations
inside its pyramid loop; the fixture does not claim measured GPU timings,
original-compositor cost, capture-mutex cost, game FPS or game acceptance.
