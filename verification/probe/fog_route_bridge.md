# Production fog route bridge

This detached fixture compiles the entire `motion_output_fog_inc.h` byte for byte
against a thin synthetic owner and the real production FogPass, field decoder,
packaged RCDATA, and native D3D9 device. It reuses the frozen spatial fixture's
authored window, COM interception, state snapshot, scene, and readback utilities;
the reused correctness main is renamed and never called.

The owner, camera, sun, copied sector samples, cached shader identity, and card
geometry are explicit fixture inputs. Native state getters replace the production
cache adapters. It does not exercise the game reader, scene hook, selector,
shader recognition, or downstream full-MotionOutput dispatch. TAA coverage stops
at the invalidation request endpoint. Reset invokes real pass Reset edges and
synthetic owner reset state; native device Reset remains separate state-fixture
evidence. No game executable identity bypass or backend-private API is added.

The bridge witnesses current-frame authority, field/sector/generation warmup,
actual native card HRESULT and call count, LastError, suppressed source pixels,
final refusal pixels, zero injected transactions on refusal, once-only execution,
full native bindings and RT1/RT2 bytes, sticky replacement faults, real pass
reopen failure through production reconciliation, and Reset rewarm. Unknown
scene state deliberately leaves the old scene-open latch while poisoning the
route; no synthetic downstream draw is made after that poison.

Build without Wine (choose a new output directory for every frozen executable):

```sh
python3 verification/probe/fog_route_bridge_build.py \
  --production-root /Users/asvetl/x3-mod \
  --spatial-root /tmp/x3-fog-renderer-production \
  --asset-data /tmp/x3-fog-assets-integration-build/generated/fog_field \
  --output /tmp/x3-fog-route-bridge-r4
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_fog_route_bridge
```

`build.json` binds the executable, compiler command, copied fragment, local
include closure, resource inputs, builder, and checker. Sources are checked for
changes across compilation. The executable takes one existing spatial case-list
path (its first case supplies the authored input scene/depth); it does not bake
or copy raw case data. The owner queues execution only after review, through
`wine_lock.py`, with `X3M_FIXTURE_BOTTLE=X3` and the existing X3 environment.
No automatic runner or shared DLL rebuild is performed here.

Validate the retained completed stdout log:

```sh
python3 verification/probe/fog_route_bridge_check.py \
  --log /path/to/bridge.log --build /tmp/x3-fog-route-bridge-r4/build.json \
  --output /path/to/bridge-summary.json
```

A passing cross-compile is not a GPU result. CrossOver runtime success does not
establish native Windows runtime parity.

## Stored-density range (2026-09-21)

The bridge also runs the fragment with `fog_density_requested_` set: real worker thread, dynamic
cache, uploads and ramps behind the same synthetic owner (`fog_route_density_inc.h`). Legacy
frames print `IMAGE <name> <fnv64>`; `fog_route_bridge_build.py --baseline` builds today's
fixture against an older production tree (density witnesses compiled out) so
`fog_route_bridge_check.py --baseline-log` can require the legacy images to be bit-identical.
`fog_density_exit_dll.cpp` / `fog_density_exit_fixture.cpp` are the process-exit witness: child
processes with a watchdog, because a hang under the loader lock is the failure mode.

```sh
python3 verification/probe/fog_route_bridge_build.py --production-root . --spatial-root . --asset-data <build>/generated/fog_field --output OUT/build
python3 verification/probe/fog_route_bridge_build.py --baseline --production-root <old tree> --spatial-root <old tree> --asset-data <build>/generated/fog_field --output OUT/baseline-build
python3 verification/probe/fog_route_bridge_run.py build-exit --output OUT
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/fog_route_bridge_run.py run --output OUT --cases <spatial cases.txt>
python3 verification/probe/fog_route_bridge_run.py check --output OUT
```
