# Connected production media fixture

One standalone, root-built x86 fixture; no game or production admission. The
consumer calls receive authored owner-stack `Frame` values. Constructor/manager
metadata, native callback continuation and allocation are authored. This fixture
does **not** execute game continuation machine code or repeat the 24-site ABI
matrix. It executes actual Consumer/Adapter/MediaServices/Clock/LavWorker/package
reader/Destination/SurfaceLease/native D3D code. No synthetic worker or CopyBackend
is supplied, and no frame cap or extra scheduler controls the decoder.

Both records use source2/flags8 and separate real canonical 512x512 SYSTEMMEM D3D
surfaces. A starts at0. After its first real picture, B starts at10000/end10359 with
an actual engine rate transaction10000 (~0.1x). First-frame anchoring gives at least
approximately 3.60000009 seconds of playback after B becomes ready, regardless of cold graph startup.
The first B write immediately triggers A retirement before diagnostic I/O. B must
present again with unchanged operation/epoch. A's address is reused only after
actual service cancellation/quiescence and gets a new session/record generation;
A2 plays the audited suffix at1939320 through actual physical EOF. B's finite end
and A2's EOF each cause one authored native callback through the real Consumer
before/after guards. A retirement separately dispatches its original status1
notification before clearing fields; the checker distinguishes it from the two
natural endpoint callbacks. Final pixels must remain unchanged for >=250 ms and20 owner
passes. Final cancellation drains all assignments and local leases.

Readback is diagnostic and never drives clock selection. A bounded real Counter
trace records each selected/first-advancing/terminal pump and committed public
position. Build and runner audit the actual three-read path (Consumer publication,
Services publication, schedule); a terminal fourth publication cannot advance the
Clock after End. Runtime cardinality/order must match. The checker intersects
exact rational QPC/rate constraints using a conservative outward preimage of
both nearest-even binary64 conversions in legacy milliseconds. It preserves
open/closed interval endpoints and integer-ms conversion uncertainty and excluding cold preparation from clock time. B and A2 have frozen
target-aligned first pictures, whose observed initial position must equal the
requested start. Every selected readback must have exactly one scheduling observation. Its exact
source interval intersects the same unknown-anchor interval as every public
position observation. Finite end requires public milliseconds >10359, with the
corresponding outward conversion bound; physical EOF requires the exact final
source end1939560ms. Both constraints precede callback.
B position/operation/epoch/rate/generation/revision must remain identical across A
cancellation and the rejected immediate reassignment, and match B play/first
selected pump. All subsequent nonterminal generation/revision observations remain
unchanged; the terminal stop increments each exactly once. Draining->vacant->reuse is
logged explicitly. Actual startup/ready times and readback pitch/HRESULTs are kept.
 Frozen RGB references
validate every captured picture; suffix first and last pictures are required.
The native destinations are SYSTEMMEM surfaces: this is actual D3D copying, not a
GPU-rendered billboard, engine Reset or live-game acceptance. Private NativeWorkers
provides no new DD-identity trace, and its process-lifetime workers are not joined.
Existing worker/ABI/Reset evidence retains those scopes. General SEH/C++ unwinding
and native Windows execution remain unverified.

Root build, from the integrated fixture checkout:

```sh
python3 verification/probe/media_connected_build.py --output /tmp/x3-media-connected-v1/media_connected.exe --lav-provider-record /tmp/x3-lav-fixture-0.81/provider.json
```

The recipe refuses overwrite, compiles only canonical integrated source paths,
binds compiler dependency-file source/header hashes, and audits the fixed PE map.
The map symbol `_connected_map` is an explicit linker GC root: compiler `used`
alone emitted the input section but did not preserve it through `--gc-sections`.
The first unqualified v1 link exposed that omission and remains retained locally;
the corrected build must use a fresh output directory.
Prepare the same immutable, module-relative package under that EXE directory with
the existing package tooling; do not install a d3d9.dll beside this fixture. The
runner validates actual package selection against the original source/cohort and
existing original/strict/EOF references. It never rebuilds or transcodes.

Root run (all paths are existing qualified inputs; fresh output required):

```sh
X3M_FIXTURE_BOTTLE=X3 FEX_X87REDUCEDPRECISION=1 WINEMSYNC=1 python3 verification/probe/wine_lock.py --timings-json /tmp/x3-media-connected-v1-lock.json python3 verification/probe/media_connected_run.py --exe /tmp/x3-media-connected-v1/media_connected.exe --exe-sha256 ROOT_FROZEN_EXE_SHA256 --output /tmp/x3-media-connected-v1-run1 --media /tmp/x3-media-remux-mkv/00002.mkv --lav-provider-record /tmp/x3-lav-fixture-0.81/provider.json --lav-graph-provider-record /tmp/x3-lav-strict-graph-v1/graph-provider.json --derived-media-record /tmp/x3-media-remux-mkv/derived-record-v2.json --original-reference-result /tmp/x3-lav-graph-v1-original31/result.json --strict-reference-result /tmp/x3-lav-graph-v1-strict31/result.json --eof-reference-result /tmp/x3-lav-eof-reference-v2/run/result.json
```

Runner records X3/WineArch arm64, explicit FEX/Winemsync environment, full command,
source/package/reference identities, runtime duration and qualification; the outer
lock records lock wait separately. Protected EXE, bottle config and selected
assets are rehashed after execution. At most32 1MiB readbacks and4MiB logs; timeout
is fixture90s/outer100s, with10s active-consumer-call watchdog. Timing is fixture
latency including diagnostics, not game FPS.

Focused non-Wine verification:

```sh
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_media_connected
```

Fourteen tests qualify acceptance and rejection for missing/wrong pixels, stale
identities, overlap failure, repeated/missing/early callbacks, final-frame loss and
post-terminal hold changes. The source additionally passes strict i686 SSE2/stack4
object compilation. Actual runtime acceptance is pending the root's frozen build
and queue. The fixture now uses actual `media_root::Ingress` for Consumer memory and
retirement, binds the mandatory ingress after native owner binding, and requires
exclusive Reset registration. Root must import those reviewed source APIs before
freezing the build. Function/data-section GC discards unused Root startup/cue
composition from this standalone executable; no alternate ingress is implemented.
