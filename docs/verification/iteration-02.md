# Iteration 02: temporal and lighting inputs

## Changes and evidence

- Capture 0.2 records typed shader constants, process-local allocation/device IDs,
  buffer bindings, draw ranges/results and surface/texture parent relationships.
  It preserves rendering and does not claim object identity from buffer identity.
- `capture_state_fixture.exe` exercises two sequential devices (normal and pure),
  stateblock restoration, nonzero and cleared integer/BOOL values, signed-zero
  floats, recreated buffers, texture-backed targets, failed indexed draws and UP
  data. Backend baseline and proxy each report zero failures. The capture verifier
  checks 10 actual frames and four distinct VB allocation identities.
- Existing proxy smoke again reports zero failures, including COM identity,
  shader bytecode, reset and final device/factory releases.
- The independent INTZ depth probe passes 16 numeric depth/coverage samples across
  RGBA8/FP16 and reset/recreation. See [depth verification](depth-sampling.md).
- Offline camera factorization fits 105 draws in each prior flight capture and
  identifies three coordinate regimes. See [numerical evidence](../reverse-engineering/camera-numerics.md).
- CTAB parsing now records nested structure members: the common light array has
  eight entries containing pos float3, color float3 and atten float4, occupying 24
  float registers in total. The user has now supplied two four-frame version 2 turning captures.
  Their integer-count and motion analysis is recorded in the following checkpoint.

The full offline suite currently passes 31 tests. SDK ABI assertions compile;
production builds without warnings. The installed DLL SHA-256 is
`fe08ea20b3fa95810a81b29c24b5961f58d103d3616ac75311bbfca0deb3e3df`.
The original game EXE, archives and bottle configuration are unchanged.

## Repeat the extended capture fixture

```sh
sh verification/probe/build.sh
mkdir -p verification/probe/build/capture-v2
cp build/d3d9.dll verification/probe/build/capture_state_fixture.exe verification/probe/build/capture-v2/
X3M_CAPTURE_START=1 X3M_CAPTURE_FRAMES=5 \
  '/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine' \
  --bottle Steam --no-update --dll d3d9=n,b \
  --workdir "$PWD/verification/probe/build/capture-v2" \
  "$PWD/verification/probe/build/capture-v2/capture_state_fixture.exe"
python3 verification/probe/verify_capture_state.py PATH_TO_NEW_CAPTURE_LOG \
  --output verification/results/capture-state-v2-summary.json
```

The committed `capture-state-v2-capture.log` comes from our synthetic fixture,
not extracted game shader code. Runtime failure HRESULTs inside it are deliberate
missing-index-buffer draws. `capture-state-v2-summary.json` records the verifier's
acceptance checks. Game logs and bytecode remain local/untracked.

## User game verification

The user restarted with `--direct --capture-start 999999 --capture-frames 4` and
provided two turning bursts in `session-20260910-210505-212.log`. All eight captured
frames have matching draw counts and successful draw/present results (762 draws).
All 45 session shaders match archived effects. No gameplay launch was performed
autonomously. Detailed motion/light analysis follows as a separate checkpoint.

No HDR/TAA/AgX or other requested visual renderer upgrade is enabled by this
checkpoint. INTZ sampling and camera/register recovery make those implementations
more concrete; scene depth substitution, lifetime ownership, pass classification,
object motion, history rejection and true scene-linear radiance remain work.
