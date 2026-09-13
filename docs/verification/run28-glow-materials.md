# Run 28: stronger glow and distance-dependent dark materials

User run 9 is complete on installed source `d9413fc`. The session is preserved
under `/tmp/x3-bottleX3-run28/`; bounded rendering and native-phase analysis is
in progress. The seven user screenshots remain local in the ignored
`screenshots/` directory.

## Screenshot observations and user feedback

`bloom1-on/off.png` compare an orange emissive panel on a nearby ship;
`bloom2-on/off.png` compare a station's orange light ring. Both ON images show
an obvious colored halo. The ring already has bright pale-yellow/white core
strips and a substantial soft halo. Framing changes slightly in the first
pair, so this is a visual comparison rather than pixel-exact A/B evidence.

The user asks whether to increase core/glow intensity slightly. The current
broad halo looks sufficient to the orchestrator; increasing total gain again
risks obscuring surface detail. A modest emphasis near the source is a
reasonable future tuning candidate, but no new gain or filter change is selected
from these screenshots alone. Keep authored gain 0.35 and the accepted Auto
ceiling of +1.5 EV while investigating the material transition separately.

`material-bug1/2/3.png` show a Federal Argon Shipyard's **docking-port
structure**, clarified by the user. Dark parts progressively become brighter
while approaching. The selected-object readout is 690 m in image 2 and 666 m
in image 3. These distances belong to the selected object, not necessarily
the individual submitted render nodes or the shader's vertex distances.
The user recalls a similar symptom elsewhere, without a remembered location;
the docking port is a reproduction, not an established boundary of the bug.

Treat this as a shared rendering/state investigation, not a port-specific
material override. Existing [distance-fade evidence](../architecture/linear-distance-fade.md)
shows that native blended draws can bypass the opaque linear-material route.
That is a plausible mechanism for inconsistent lighting as state changes, but
the screenshots alone do not identify exact shaders, render nodes, blend
states or a same-node transition. Geometry LOD, native fade, unconverted
materials and texture/lighting changes must not be conflated.

No additional gameplay run is requested while this session is being analyzed.

## Rendering and capture limits

The bounded rendering report is `/tmp/x3-run28-render-analysis.json`. Across
482 sampled reports, HDR redirects/tonemaps 474 times with zero fallback;
TAA resolves and uses history in 473. Motion apply/restore failures are zero.
All 95 sampled bloom prepares and commits succeed, with 85 ON and ten OFF.
Auto targets its +1.5-EV ceiling in 465/466 active reports, again mostly behaving
as a steady boost in this sector.

Material reports total 99,178 converted draws, including 45,360 bump draws,
and 1,531 refused opaque draws. There are no shader-bind, missing-variant,
HDR-mode or sampler-sRGB failures. Every explicit material refusal is the
not-yet-installed glass pair `c30104cb0efb6675/a66fb1981ba755b2`.
These counters do not include earlier opaque-state-gate bypasses.

F8 contains four consecutive frames, 4052–4055, with successful HDR, motion and
depth readbacks. These are pre-bloom images, and no resolved-TAA/final display
readback exists. The selected-target pointer remains unchanged through this
burst, but cannot yet be linked to render nodes. Several projected Argon BUMP
candidate nodes remain converted in all four frames, and the two unsupported
glass draws remain native in all four. Projection of a node's origin does not
identify its actual covered pixels.

All 821 captured draws in the inspected common-fog shader families have VS b0
false. One reviewed blended standard-BUMP draw per frame also has b0 false;
blending alone does not prove distance fog. This burst therefore shows no
same-node fade or material-admission transition explaining the screenshots.
Reviewed [capture-only target/root/parent and native fade reporting](../reverse-engineering/station-material-distance.md)
is ready in source to resolve that missing evidence in one combined test.

The log ends on a complete newline without a fatal/device-lost record, and the
game has exited. It has no explicit release/quit footer, so clean teardown is
not independently proved.

## Selection pause: stream creation isolated

The 24,040,121-byte session log has SHA-256
`17ed42692aea7f7d438e1209b4972d621250c4ba8152c486279ffeb1009655c4`.
The streaming reducer `/tmp/analyze_run28_selection.py` retains compact evidence
in `/tmp/x3-run28-selection-analysis.json`. All 63 slow frames and 87 slow calls
are retained, with 17 contiguous segments per frame and zero overflow, ordering,
clock, read, CPU-query or suppression errors. Startup/teardown unmatched and
foreign events do not overlap the causal witnesses.

Twelve active target changes occur. Ten have exact retained mode-3 publisher
records, all from input-side caller `0x0042dd6e`, inside the input/control body.
Each contains exactly one MOV voice playback, containing exactly one stream
creation. Their inclusive totals are:

| Bracket | Total wall time |
| --- | ---: |
| Ten target publishers | 3,851.451 ms |
| Nested playback calls | 3,846.840 ms |
| Nested stream creation | 3,846.670 ms |
| Publisher time outside playback | 4.611 ms |

These nested costs must not be added. Eight publishers take 424.128–512.375 ms;
the other two take 47.350 and 59.186 ms. The eight long publishers' enclosing
input segments total 3,753.767 ms wall and 3,710 ms thread CPU, with only
11.996 ms wall outside stream creation. This identifies CPU-active creation
work as the dominant descendant of these selection pauses. It does not yet
identify the expensive COM method, codec or file-processing operation inside it.
No seek or delayed-overlay call occurs in this run.

The two subthreshold publishers total 1.009 ms; associating each with the two
remaining target changes is inferred from counts, not retained exact intervals.
Another 24 playback/create pairs occur in pending VM. Some follow selections,
but their target relationship is temporal only. Four capture frames are
renderer-dominant and excluded from selection attribution. Other unexplained
slow-frame residuals remain; this is not a claim to explain every pause.

The diagnostic averages approximately 0.228 ms of CPU-query bracket wall time
per loop. Relevant input endpoint queries are at most 40.6 microseconds, far
below the measured creation stalls. Next work is targeted disassembly of the
stream creation and lifetime path, using the
[existing native sites](../reverse-engineering/selection-native-vm.md), rather
than another broad phase-tracing run.

The user confirms that **no target-name speech played**. Targeted
[voice-path disassembly](../reverse-engineering/voice-stream-creation.md) confirms
all 34 retained creations return null; both requested voice files exist, and
native caching already retains successfully created streams. A standalone
documented-API probe will isolate the first failing audio-construction stage.
Permanently suppressing speech would not resolve the underlying failure.
