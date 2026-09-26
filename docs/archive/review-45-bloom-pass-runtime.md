# Review 45: standalone BloomPass Steam and X3 runtimes

2026-09-13. Independent Sol high review accepted the final standalone
[BloomPass fixture](../verification/bloom-pass-fixture.md) result. The retained
`x3-bloom-pass-74fh27u7/summary.json` and copied
[`bloom-pass-summary.json`](../../verification/results/bloom-pass-summary.json)
are byte-identical (SHA-256
`222785ad77adf119cfaaa695bb285f02b9c2d8caa0e4fecf51367052673a45c0`).
They record a complete passing run in the CrossOver Preview Steam fixture
bottle, exit code zero, unchanged inputs and no game launch or installed-DLL
change.

The exact terminal record covers 24 ordinary image cases, sixteen transaction
controls, forty boundary iterations, one real Reset and one post-Reset case.
All 25 independently evaluated images pass: 4,185 RGB channels have maximum
error one 8-bit code against the fixed three-code ideal AgX, nine-tap bloom and
scalar-RCAS envelope, with zero alpha errors. The fixture made 277 native-draw
checks for disabled N-patch and adaptive tessellation. Adaptive hostile state
was retained and tested; this backend accepted but did not retain nonzero
N-patch, which the result explicitly leaves untested.

The audit recomputed all 50 current input hashes and found no missing or changed
input. The retained and copied summaries match; their stdout/stderr hashes match
the retained files; all 206 retained BGRA8 files match the readback manifest.
The loaded `d3d9.dll`, `wined3d.dll` and `d3dx9_37.dll` paths and hashes are
recorded, along with the executable, input bundle, expanded sources and twelve
compiled shader hashes. The paired five host acceptance tests pass.

A follow-up independent audit accepted the X3-bottle result retained at
`x3-bloom-pass-gcvrwt92/summary.json` and copied to
[`bottle-X3/bloom-pass-summary.json`](../../verification/results/bottle-X3/bloom-pass-summary.json).
Those files are byte-identical (SHA-256
`e4236e599ab8e2e181d2275702a1edaea4a6d3e39df6ab83050ebbd74d49805a`).
The command uses CrossOver Preview with bottle X3; the record and current bottle
configuration agree on arm64 Wine, `FEX_X87REDUCEDPRECISION=1` and
`WINEMSYNC=1`. All three loaded runtime-module files still match their recorded
hashes, as do all 50 X3-run inputs.

The X3 log has the same strict 16-control, 40-iteration, 24-plus-one-image,
real-Reset terminal result and 277 draw-state witnesses. Its 33 repository input
hashes, twelve compiled bytecodes, 206 raw readback hashes, image verdicts and
fill diagnostics are identical to Steam. The rebuilt fixture executable differs
between retained directories and is independently hash-bound in each record; no
cross-bottle executable identity is claimed.

Independent byte comparisons confirmed that every ordinary success changed the
main image while preserving its observed alpha, every applicable recovery
returned the exact observed original, the deliberate unrecovered-write control
remained changed, every MRT remained exact, every copied-token retry preserved
the immediate postcommit image, and every published image matched that retained
postcommit readback. The post-Reset image satisfies the same checks.

The diagnostic correction did not replace the original with a fitted oracle.
The neutral `ColorFill` control is byte-exact at requested `6b193957`. Under the
hostile outgoing state, all 48 pixels were instead observed as `6b58829e`; the
fixture now captures that genuine precommit image and requires exact rollback
to it. Candidate RGB still uses the independent numeric oracle, and alpha uses
the observed original at each pixel.

The first attempt remains preserved at
`x3-bloom-pass-ta8yv8qb/summary.json` (SHA-256
`380c2b500c333259962d6f641c0f940e49c9d748ed8c39adb7958c1bb8e2ed25`).
It records a failed, unverified run with unchanged inputs; its terminal log says
`Original backup not recovered exactly`. This was the invalid requested-DWORD
assertion corrected above, not production evidence.

These results qualify the actual standalone production executor on the recorded
Steam and X3 D3D9 backends under CrossOver Preview. Native Windows runtime, game
integration and appearance, non-null depth, lost-device behavior,
two-device/wrong-thread admission,
arbitrary per-setter or partial-copy driver failures, and game performance
remain outside its scope.
