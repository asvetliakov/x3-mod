# Live motion route (checkpoint B1) verification

Synthetic verification of the live same-draw route through the actual proxy
DLL under CrossOver Preview's Steam bottle with the process-local `d3d9=n,b`
override. No game launch; the reviewed shader pair is read from local files
and never enters the repository or the reports.

```sh
python3 verification/probe/run_motion_row_history.py     # host unit fixture, release + ASan/UBSan
python3 verification/probe/run_motion_output.py          # fresh build + four DLL runs
python3 verification/probe/check_no_x87.py               # light setter hooks reach no x87 code
```

`check_no_x87.py` disassembles `build/d3d9.dll`, walks the static call graph
from the six `LightCallBoundary` setter hooks and fails on any x87 opcode
other than the project's own fnsave/frstor transport pairs and the MXCSR
transfers; it backs the boundary choice recorded in
[review 12](review-12.md).

## Fixture

`verification/probe/motion_output_fixture.cpp` creates a 64×64 windowed
A8R8G8B8 device with a D24X8 auto depth surface, the reviewed VS/PS pair, an
original flat ps_3_0 and two original triangles (an oversized one covering the
right of the viewport and a small one in the upper left), each with its own
synthetic node scope. Every frame: hostile render states, scissor and stream
bindings; color+depth Clear (the route latches and schedules the fill); a
background draw with the reviewed VS and the flat PS (the fill runs inside this
hook and every touched state is compared before and after); scene states;
depth-only Clear; scene draws with known submitted rows (`c24–27`), each
followed by a full state comparison; EndScene; color readback hash; motion
readback; Present.

Two DLLs are exercised:

- **production** – `build/d3d9.dll` unchanged. The default signatures do not
  recognize the synthetic background, so the selector never enters the scene
  phase: this proves fill, restoration, Reset with RT1 owned, readback files
  and device release without routing.
- **seam** – the production objects linked with `capture.cpp` and
  `motion_output.cpp` compiled under `X3M_MOTION_OUTPUT_FIXTURE`, exporting
  `x3m_motion_output_fixture_configure` (fixture background signature and
  per-draw synthetic scope) and `x3m_motion_output_fixture_readback`. The
  game observers cannot run in a synthetic process, and the selector's
  signatures are game hashes, so this seam is the only way to reach gates 5–6.

The seam script (DLL frame numbers): f0 first frame (mode 0 sentinel), f1
matched with the application writing `c252–255` and PS `c216–217` first, f2
scope withheld (sentinel-only mode, gate 5), f3 nothing recorded in f2 (gate
6), f4 matched plus one blend-enabled and one flat-PS draw that must not route
(gates 4 and 3), f5 duplicate key consumed once, f6 the duplicate poisoned the
key, f7 a `D3DSBT_ALL` state block Apply rebinding the flat PS is honored
(gate 3, flat PS still bound afterwards), f8 no f7 record, Reset, f9 history
restarted, f10 matched, shader release/recreate, f11 matched.

The CPU oracle replays each frame's draws per pixel with the depth test using
D3D9's integer raster sample convention, writing the previous-UV / previous
clip Z/W / validity ABI for matched routed draws and the sentinel otherwise;
pixels within 1.5 px of a coverage edge are skipped. Tolerances: 0.01 px UV,
4e-6 previous depth.

## Results

| Case | Fixture checks | Restoration comparisons | Motion pixels | Matched pixels |
| --- | ---: | ---: | ---: | ---: |
| production off / on | 6 / 6 | 39 / 39 | – | – |
| seam off / on | 6 / 30 | 39 / 39 | – / 44,284 | – / 10,261 |

All 39 restoration comparisons per run report zero differences. Seam-on
maximum error: 0.0016 px UV, 3.7e-8 previous depth. Color hashes of all 12
frames are identical with the route off and on for both DLLs. The DLL's
per-frame gate histogram and per-draw `motion_route` decisions in capture
frames 1–8 match the script exactly (routed 2/2/2/2/3/2/1/2, matched
2/0/0/2/2/1/1/1). Frames 1–8 also write `motion_<device>_<frame>.rgba32f`
readback files (65,536 bytes) containing only ABI values. The final device
Release returns zero because the route drops its owned objects first.

The device gate reports the mixed-format MRT self test passing on the
Preview backend (`color_errors=0 motion_errors=0`), four MRTs, 256 VS
constants and `D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS`.

Existing suites after the change: material-motion structure (8 groups, 57,152
mutations, 6 aliases), material-motion GPU (1,182 checks, 2,952 samples, 82
configurations), ownership integration (26 cases), fallback (23 production
objects) and verification all pass.

Exact hashes, commands and case inventories: `verification/results/motion-output-summary.json`;
fixture output: `motion-output.txt`; capture logs of the two enabled runs:
`motion-output-production-on-capture.log`, `motion-output-seam-on-capture.log`;
host unit summary: `motion-row-history-summary.json`.

## Limits

Synthetic device program only: no gameplay, no TAA, no temporal consumer, one
reviewed pair. Object scope is injected; the game observers are not exercised.
Native Windows is cross-compiled but not executed. Setter-hook cost in the game
is unmeasured.
