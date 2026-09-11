# Motion output in the original draw

The user approved prototyping motion output alongside scene color before adding
more live replay infrastructure. The preferred direction for the finished
material renderer is one geometry submission producing both outputs. This is an
implementation hypothesis to verify, not an enabled feature or measured speedup.
The reviewed replay implementation stays available as a numerical reference and
possible fallback.

## First prototype

Choose one observed opaque Shader Model 3 material pair from the existing archive
sweep. Preserve its current position and material work, supply previous-frame
submitted transform rows, and interpolate previous clip coordinates for a
second pixel-shader output containing motion correspondence. Reuse the existing
RGBA32F previous-UV/depth/validity calculation initially, so this comparison does
not also change the temporal consumer ABI. Use original synthetic geometry
and controlled history first; do not patch or install the game for this step.

Verify available constants, temporaries, interpolators and render-target outputs
from actual disassembly. Qualify the exact input program and refuse unsupported
variants. Keep extracted game shader bytes and modified bytecode local. The
transformer and original fixtures belong in source/verification respectively;
derived findings may be committed.

The comparison must cover original versus modified color and depth, stationary
and moving geometry/camera, perspective interpolation, the established viewport
and jitter convention, missing history, and ordinary native failure cleanup.
Measure completed-work cost with the same scene and target setup. Query GPU
timestamp support, but retained Preview evidence reports it unavailable; EVENT
completion with QPC includes CPU submission and cannot isolate GPU duration.
Separate setup and readback from the timed work. Compare replay only
when the workloads and produced motion represent the same work.

## Expected benefit and unresolved costs

The original draw already consumes the current vertex data and establishes
coverage. Writing motion there avoids a second geometry submission and the later
reread of geometry needed by replay. It still adds previous-position arithmetic,
interpolation and motion-target bandwidth. Register pressure or target-format
restrictions can affect the result; lower measured cost is not assumed.

Both approaches still need valid previous/current object correspondence, camera
history and special handling for changing geometry. A shader cannot reconstruct
missing previous particle identity. Transparent materials do not automatically
produce useful motion when their color blending is applied to the motion target.

D3D9 supports multiple render targets, with capability-dependent restrictions on
formats, bit depths and post-pixel operations. Start with ordinary opaque draws;
verify actual capabilities and alpha/transparent behavior separately. See
[Microsoft's MRT contract](https://learn.microsoft.com/en-us/windows/win32/direct3d9/multiple-render-targets).

Same-draw output may remove much of the need for deferred geometry leases and
replay-specific exclusion. It does not automatically make temporary shader,
constant or render-target substitutions safe under concurrent application calls.
Choose the live integration boundary after the prototype, retaining only the
admission/ownership mechanisms needed for its actual state and lifetime contract.

Native Windows/Direct3D and CrossOver Preview remain required targets. The first
local runtime evidence can only establish Preview behavior; use public D3D9
contracts without backend-private APIs or DLL-version allowlists.
