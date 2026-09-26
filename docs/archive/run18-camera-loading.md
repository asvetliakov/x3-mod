# Run 18: camera and loading verification

Run 18 accepts the installed 13-degree chase-camera angle, chase firing
correction, resource-reader equivalence, DAT-handle reuse and mesh-adjacency
equivalence on the visited X3 workload. The user reported that aiming worked
and saw no camera rendering problem. They prefer a 0.9 distance scale and a
slower response than the current 0.85 / 0.22-second rotation / 0.30-second
position settings; that is a follow-up tuning request rather than a failure of
this verification run.

The user also reported that the lead/fire-here HUD hint was absent in chase
view and that sector travel reset the selected view. The log shows successful
aim correction and safe camera re-entry, but it cannot establish why the HUD
hint was not drawn. These observations remain separate camera/UI follow-ups.
No repeat of run 1 is needed.

## Provenance and captured files

The immutable helper snapshot is
`/tmp/x3-bottleX3-run18/session-20260913-170604-212.log`: 11,913,444 bytes,
219,774 lines and 456.369 seconds through the last Present. The runtime reports
renderer version 0.4, schema 2 and a 32-bit process. The installed `d3d9.dll`
still has SHA-256
`b11bff61b2a2f798b565969d8684923781d2b1bec0e3b22cf0cbe2f024d45702`
and size 12,368,396 bytes, matching the
[installation record](../../verification/results/linear-material-install.json).
The recorded X3 executable and bottle-configuration hashes also still match
that record.

The snapshot contains the log and all 59 unique referenced shader files (22
vertex and 37 pixel shaders). Every file exists with the logged size; there are
no extra copied files. The log references no surface readback and no adjacency
mismatch dump. `--mesh-adjacency-dump` writes only mismatches, and this run had
none.

The requested configuration is present: chase camera, reader `verify`, DAT
handles, adjacency `verify` with mismatch dumping, and telemetry. The camera,
fire and four aim-trace sites installed successfully. The resource-reader site,
both DAT-handle sites, both shared mesh vtables and all 23 named loading hooks
also installed. Motion output, TAA, HDR and the scene hook were disabled, as
expected for run 1.

## Camera and firing

The chase-camera handler examined 27,586 applicable frames and applied the
camera on 21,337 of them. It recorded seven intentional state snaps and zero
write refusals. Its 29,120 timed calls used 0.661 seconds of CPU time in total
(about 22.7 microseconds per call), with one 2.444-millisecond maximum. The
6,249 refused frames are associated with non-chase view states rather than a
hook fault. An additional 1,534 handler calls were explicitly inactive during
a load transition and are outside that examined-frame total.

The firing hook performed 213 eligible chase overrides with zero stale,
identity, view, cursor or read refusals. Their total measured handler time was
0.491 milliseconds. The bounded aim trace observed 221 entries/rays and kept
117 detailed samples: 109 in chase mode 258 and eight in internal mode 1. All
117 samples were admitted. It reported zero read failures, orphans and thread
overflows. The user's successful left/centre/right firing check is therefore
supported by an exercised correction path, not just installed hook metadata.

Around the 44.905-second load ending near frame 16,819, camera applications stop
and the log records no active cockpit. On return the engine reports internal
mode 1; after chase mode 258 is selected again, the hook performs its fifth
state snap and resumes applying the camera. This supports the user's sector-view
reset observation and shows safe hook recovery. It does not show that the chase
hook caused the engine to select the internal view.

## Reader, handles and adjacency

The final cumulative counters provide substantial fast-mode admission:

| Path | Covered work | Equivalence and faults | Diagnostic CPU time |
| --- | --- | --- | ---: |
| Resource reader | 7,204 calls; 6,958 handled; 1.331 GB encoded input and 3.621 GB output | 6,958/6,958 equal; 0 mismatches; 0 null originals; 246 ordinary non-gzip fallbacks | candidate 19.643 s; original 22.698 s |
| DAT handles | 5,802 logical opens; 5,790 reused; 12 real opens | 0 errors; 0 full-table events; 12 held entries | not isolated |
| Mesh adjacency | 15,354 meshes; 14.219 million faces; 42.658 million compared entries | 15,354/15,354 equal; 0 mismatches, faults, native failures or fallbacks | candidate 1.762 s; native 6.201 s |

Reader verification deliberately executes both implementations, so its two
times add work and are not a 42.341-second load-time measurement. The largest
candidate reader call was 191.394 milliseconds. Adjacency verification likewise
executes candidate and native work; their largest calls were 12.337 and 32.808
milliseconds. The exact, non-fallback coverage is sufficient to admit run 5's
reader and adjacency `fast` modes. Run 5 remains a functional co-activation
check, not an isolated loading benchmark.

## Loading and stutter limits

The existing streaming loading analyzer found eight Present gaps over two
seconds. Its labels are mechanical and the log has no sampling-profiler blocks:

| Label | Gap | Hooked exclusive time |
| --- | ---: | ---: |
| Unlabelled startup | 2.819 s | 0.361 s |
| Menu load | 10.425 s | 4.717 s |
| Save load | 48.573 s | 19.383 s |
| Menu load | 9.777 s | 4.322 s |
| Save load during the sector-travel interval | 44.905 s | 14.572 s |
| Sector change | 6.798 s | 3.726 s |
| Sector change | 6.455 s | 3.905 s |
| Menu load | 10.042 s | 4.318 s |

The gaps total 139.794 seconds, of which the hooks attribute 55.304 seconds to
instrumented operations. Without `--profile`, the remaining time has no sampled
attribution. Across the full run, verify mode performed about 21.405 seconds of
candidate reader/adjacency work and 28.899 seconds of original reader/adjacency
work. This makes verification a credible contributor to pauses during loading,
but neither the user's minor-stutter report nor any individual gap can be
assigned to it from this run alone.

The one scheduled generic capture inspected 1,395 shader binds, wrote 59 shader
files without failure, and recorded a maximum captured-frame interval of 0.660
seconds. Shader file writes used 0.397 seconds in aggregate; capture work
overlaps these components and must not be added to them. No adjacency mismatch
file was written. This capture may explain a distinct early hitch, but it does
not explain the 44–49-second loading gaps. The run supplies diagnostic loading
coverage rather than FPS or controlled loading-time evidence.
