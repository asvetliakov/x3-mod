# Historical automatic-depth substitution experiment

At commit `22146a1`, the experimental ownership layer could replace a newly created single-sample
D24X8 automatic depth surface with private INTZ storage while preserving its
application-facing wrapper, descriptor and container behavior. This is an input
experiment for TAA, **not a gameplay-ready feature**. It is disabled by default
and was never installed into X3. The active implementation now uses a copy of the
original D24X8 source; the rejected substitution code and its fixture have been
removed from the current tree. Their exact source remains in that commit.

## Verified behavior

The original standalone fixture exercises the production wrapper directly, using
CrossOver Preview and a hidden window. Its final frozen-source run passes **758
API/lifetime checks and 48 numeric depth samples** on ordinary and pure devices.
The samples cover two resource generations, including a resize from 64×64 to
80×48. They establish:

- Canonical repeated depth getters and parent identity; the logical descriptor
  remains D24X8 with the requested dimensions, and no private texture container
  escapes. Logical private data remains readable.
- Null and unrelated depth bindings stay distinct. Applying a completed native
  stateblock does not restore depth bindings, matching native behavior.
- Main-scene triangles write 0.25 and 0.75; a later triangle at 0.875 fails the
  existing nearer depth test. Untouched pixels retain clear depth 1.
- A GPU copy sampled into FP16 before the overlay clear keeps the scene values
  afterward. Sampling the live depth after that clear yields 1 instead. These
  are simple normalized-depth values, not a proof of full 24-bit precision or
  world-space reconstruction accuracy.
- A caller-held original automatic depth reference still makes Reset fail.
  Releasing it permits resized Reset and a new storage generation.
- Backend fault injection at texture allocation, surface-level acquisition and
  replacement binding preserves the original D24X8 allocation; subsequent Reset
  successfully retries selection. Injection changes disposable object-local
  vtables only and is absent from production code.
- D16, D24S8, missing automatic depth, MSAA and disabled options keep their native
  allocation. No broad matching-by-size policy is used.

The backend accepts a stencil-only clear on D24X8 as a no-op. An initial assumption
that it should return INVALIDCALL was disproved by the native control and fixed.
The implementation calibrates this behavior before application rendering. With
logical stencil enabled and comparison NEVER, the native D24X8 and guarded INTZ
paths both produce the same magenta pixel and retain the caller's logical state.

## Material compatibility failures

The same fixture deliberately records a failed compatibility gate:

| Full-surface depth StretchRect | Original D24X8 | INTZ substitution |
| --- | --- | --- |
| Automatic source to unrelated D24X8 destination | S_OK | 0x8876086c |
| Unrelated D24X8 source to automatic destination | S_OK | 0x8876086c |

Mapping content operations to the physical allocation avoids silently copying
stale original contents, but does not make those format pairs compatible. The
report's successful numeric/lifetime checks do **not** override
`compatibility.depth_copy_parity=false` and `gameplay_ready=false`.

There is also an unresolved stateblock-recording case: temporarily disabling
physical stencil around a draw does not safely reproduce a draw made inside
BeginStateBlock/EndStateBlock recording. Recording can divert SetRenderState from
live state. The fixture tests applying completed stateblocks, not this case.
Actual device loss, concurrent callers and gameplay remain unverified.

These findings prevent enabling substitution in the game at this checkpoint.
A separately investigated RESZ copy into a compatible private texture may avoid
substitution and its state/format translation entirely. Capability-query success
alone cannot establish that such a copy transfers real depth values.

## Reproduction

```sh
git worktree add --detach /tmp/x3-intz-substitution-history 22146a1
cd /tmp/x3-intz-substitution-history
sh verification/probe/build_auto_depth.sh
python3 verification/probe/run_auto_depth.py
```

`verification/results/auto-depth-summary.json` records source and executable
hashes and separates the numeric result from compatibility gates. The text log
contains the two native/experimental controls and three injected failure stages.
The runner uses a process-local builtin-D3D9 override and a 60-second timeout;
it does not launch X3, install a DLL or edit bottle configuration. CPU readback
exists only to verify the synthetic values, not as a proposed game-frame path.
