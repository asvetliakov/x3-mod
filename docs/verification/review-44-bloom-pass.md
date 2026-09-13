# Review 44: production bloom GPU executor

2026-09-13. Independent Sol high source review approved
`src/renderer/bloom_pass.h/.cpp` after the fixes below. The
[executor contract](../architecture/bloom-pass-runtime.md) remains separate from
the game callsite, owner/thread lifetime and Reset barriers. The source is not
yet linked into the proxy or installed. Runtime fixture acceptance is pending.

| Finding | Status |
| --- | --- |
| Inherited N-patch/adaptive tessellation could change the fullscreen triangle strip. | Fixed: save the exact N-patch float through public slots 79/80 and the adaptive render state; disable both before injected draws and restore them afterward. |
| Setup failure could trigger a recovery copy even though main was untouched, exposing a clean frame to a new copy failure. | Fixed: keep state-only cleanup until a native DrawPrimitiveUP is actually issued. The follow-up review also caught setters failing inside the draw helper; its issued flag now changes immediately before the native draw. Retry only the same state snapshot when no write occurred. |

The reviewer checked RGB-only commit and actual outgoing state restoration,
backup/recovery classifications, token consumption and generation invalidation,
surface-only persistent ownership, bounded transient texture views, capability
checks and public ABI slots. Persistent resources are reused at a fixed layout;
allocation and validation stay outside the pyramid draw loop. No per-scene-draw
work, host allocations, locks or readbacks were introduced by this executor.
These are source-level cost properties, not measured GPU/CPU timings.

Production, fixture-enabled and GCC `-fanalyzer` x86 cross-compilation passed
with the required SSE2 and four-byte incoming-stack flags. Object inspection
found two x87 stores needed to receive `GetNPatchMode` and `exp2f` float returns;
no x87 arithmetic was added.

The separately reviewed [GPU fixture](bloom-pass-fixture.md) exercises the actual
executor, including real pixel writes before simulated failure, hostile state,
alpha, same-image recovery, nonterminal setup/setter failures, and a retained
pass across native Reset. It compares the composed image to independent AgX,
nine-tap bloom and scalar RCAS calculations. Native Windows, non-null depth,
full owner/retirement integration, real lost-device behavior, performance and
game appearance remain outside the current qualification.

Fixture review fixed four evidence gaps before execution: keep an attached pass
across actual Reset and validate recreation; bind exit/stdout/stderr even on
failure; require getter-observed hostile tessellation values before claiming
coverage; and compile the exact production copy shader. Five host controls and
the final fixture cross-compilation passed independently. The corpus has 24
images plus one post-Reset image, sixteen transaction controls and forty
iterations. Its [build-only record](../../verification/results/bloom-pass-build-review.json)
is explicitly not a GPU pass. Runtime preflight found the user's active game,
so no Wine fixture was launched; the first runtime test remains queued.
