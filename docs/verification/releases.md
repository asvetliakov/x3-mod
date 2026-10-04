# Releases: verification ledger

Player zips built by `tools/release/release.py` (steps in its docstring and in
[config-file.md](../architecture/config-file.md)). Earlier releases are recorded only in their records:
[0.8.0](../../verification/results/release-0.8.0.json), [0.9.0](../../verification/results/release-0.9.0.json).

## Release 1.0.0

2026-10-04, `python3 tools/release/release.py --out /tmp/x3m-release-1.0.0` (dry run, then the real run: exit 0,
204.5 s), [record](../../verification/results/release-1.0.0.json); the tool record and every artifact stay
under `/tmp/x3m-release-1.0.0/`.

- Source: clean `c3cce57f`, the docs-only commit (status.md, user-runs.md) on top of the version bump `082e1d8c`;
  the DLL and regenerate inputs are those of `082e1d8c`. The stripped DLL carries `X3M_SOURCE_COMMIT=c3cce57f…`
  once and README.txt names the same commit.
- DLL: stripped `94d16fa8…` (39,856,549 B, shipped), unstripped `86ed43f4…` (59,097,196 B), `d3d9.debug`
  `e467b717…` (20,226,668 B). 0 build warnings; `check_no_x87` PASS, 0 violations, 766 reachable functions, on
  both DLLs; strip identity PASS (10 loaded sections, 17 exports, marker once, 9 debug sections removed,
  debuglink). No version resource (`.rsrc` holds RT_RCDATA only); the version is in x3m.ini, README.txt and the
  `x3-modern-renderer version=1.0` log row.
- x3m-regenerate: both binaries rebuilt (0.9.0 had reused 0.8.0's) because the tool gained the engine-bodies
  step. Host `38261796…` (12,286,224 B, smoke test only, not shipped): smoke PASS, exit 0 in 26.8 s.
  Windows `x3m-regenerate.exe` `4196d3e0…` (29,768,130 B), built and smoke-tested in bottle X3M-Build through
  `X3M_FIXTURE_BOTTLE=X3 wine_lock.py` (lock wait 0 s, child 84.3 s): smoke PASS, exit 0 in 19.6 s. Both
  smoke logs show `engine bodies summary: 1 bodies (red 1), 1 listed but not loadable` and `all done: fog
  families, LOD overlay and engine bodies regenerated`.
- Zip `x3m-1.0.0.zip` `dc130aad…` (59,764,939 B): the same 22 entries as 0.9.0 in package.py's order, CRC
  clean, DLL/exe/fonts re-hashed. x3m.ini (20,864 B) begins `; X3 Modern Renderer 1.0.0` and is byte-identical
  to `assets/x3m.ini`; `generate --check` PASS (256 settings, 107 in the template).
- Fixture gates reused from the [Run130 candidate](../../verification/results/run130-candidate-build.json)
  (`01114822`): `git diff --stat 01114822..082e1d8c -- src CMakeLists.txt tools assets` touches only
  CMakeLists.txt (VERSION) and assets/x3m.ini (version line). `manage.py launch --dry-run --direct --config
  --debug` exit 0 from the checkout; a dry run with the zip's DLL needs the install.
