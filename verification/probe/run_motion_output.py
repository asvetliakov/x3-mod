#!/usr/bin/env python3
"""Fresh-build live motion route integration through the actual DLL; no game launch.

Eighteen runs of one original synthetic device program under CrossOver Preview
Wine with a process-local d3d9 override: the production build/d3d9.dll with the
route off and on (fill of RT1/RT2, exact restoration, Reset with the targets
owned, capture-frame readback of motion and depth, device release), and a seam
DLL (production objects + capture.cpp and motion_output.cpp compiled with
X3M_MOTION_OUTPUT_FIXTURE) off and on, which also exercises scene recognition,
variant routing, history, sentinel-only mode, the R32F current-depth target and
state block resynchronization against a CPU oracle. The four runs repeat in
three environments the gameplay diagnostic launch uses: the ownership wrapper
(X3M_OWNERSHIP=1), the wrapper with the copy-depth/scene-depth path
(X3M_DEPTH_COPY=1 X3M_SCENE_DEPTH_CAPTURE=1) and the wrapper with the
admission monitor (X3M_ADMISSION=1). Two more plain runs enable the per-draw
jitter (X3M_MOTION_JITTER=1): coverage must follow the Halton offsets, the
readback must still match the oracle from unjittered history rows with zero
prior jitter uploaded, and every application row must be restored bit-exactly.
Four TAA runs (X3M_TAA=1, temporal step 3; production and seam, plain and under
the wrapper) end every frame at the game's pre-bloom boundary: the resolve runs
inside the StretchRect hook, the bloom copy must receive the resolved image,
every touched state is restored, frames without usable history keep the 8-bit
color bit-identical, the seam's main target equals a reference TemporalPass run
on a plain second device from the same read-back inputs byte for byte (the
X3M_TAA_DEBUG FP16 files equal the reference FP16 output), and the device still
reaches zero references. Four bench runs time the boundary with the resolve
off and on at 1280x768 and 5120x1440 (EVENT-synchronized QPC, CPU-inclusive).
The resolve's jitter convention (history read at the content's previous
unjittered UV plus the CURRENT jitter, never the previous one) is covered by
the reference comparison only indirectly, because the reference runs the same
production TemporalPass bytecode; the stationary-stability evidence (zero
change between jitter phases, zero centroid drift, edge coverage converging)
lives in run_temporal_pass.py. The seam script cannot host that check: its
objects alternate between two poses every frame (a moves between t=.75/p=0
and t=.8/p=.125, b between the origin and t=-.05/zo=.1), so no frame is
stationary with respect to the previous one, and the cut schedule leaves at
most two consecutive history frames (1-2, 10-11), far short of the ~48 frames
the weight-0.9 accumulation needs to converge.
Lazy RT binding (X3M_MOTION_RT_MODE=lazy, an experiment that keeps RT1/RT2
bound across consecutive routed draws): four regular-script runs repeat with
the mode on (production and seam, plain, seam under the wrapper and seam with
TAA) and must be indistinguishable from the per-draw runs (colour hashes,
readback files, checks, restorations), and four "burst" runs (both DLLs, both
modes) run consecutive routed draws with no application getter between them,
interleaved with gate-3/gate-4 draws and an application SetRenderTarget or
depth Clear inside the scene: colour, the fixture's state signature, the
seam's RT1/RT2 hashes and the DLL's readback files must be identical between
the modes, the final state must equal the pre-burst state, and the DLL's
per-frame SetRenderTarget count must drop from 20 to 12 in the lazy
frames without capture diagnostics (capture frames restore before every
draw's diagnostics and count 20 in both modes).
Mip LOD bias (X3M_TAA_MIP_BIAS, the TAA blur fix's sampler half): the
"mipbias" script binds a 1024x1024 LOD-ramp texture with a full mip chain
(level i a constant grey 16 + 20 i; object A maps it at 8 texels per pixel,
LOD 3) on stage 0 (MIPFILTER LINEAR) and stage 4
(POINT), the same texture with MIPFILTER NONE on stage 1, an unmipped texture
with LINEAR on stage 2, the unmipped cube on stage 3 and nothing on stage 5,
then reads D3DSAMP_MIPMAPLODBIAS back through GetSamplerState (not hooked)
after every step: routed draws (sentinel-only, both DLLs) must see the bias
on stages 0 and 4 only, a flat (gate-3) draw must find every stage restored,
rebinding stage 4 to an unmipped texture or switching stage 0 to MIPFILTER
NONE must take the bias off that stage, an application write of the bias
must stand, be re-biased by the next routed draw and be the value the next
restore puts back, every frame must end (Present) unbiased, and a Reset after
frame 3 must make the DLL re-read its saved values. Even frames read back the
same material draw of A routed (biased) and unrouted (gate 4, ONE/ZERO
blending): without the bias the two images are identical, with -0.5 the
routed one is darker (finer ramp levels) and with -1.0 twice as much darker
(the trilinear sample of a per-level ramp is linear in the LOD). The DLL's
per-frame set/restore/read counts must equal the fixture's model of the
restore points and the hand-derived anchors; the unset run must equal the
X3M_TAA_MIP_BIAS=0 run byte for byte; regular-script twins with the bias on
(TAA, lazy, jitter-only) must equal their unbiased twins, since they bind no
mip-mapped texture.
Camera reprojection of sentinel pixels (X3M_TAA_SENTINEL, seam only): four
runs install the fixture's own projection/view buffers as the engine camera
globals (X3M_FIXTURE_CAMERA=rotate: one degree of yaw per frame, a 30-degree
jump at frame 7). With the switch on (auto) the DLL must reproject the
background through the camera on every frame with a previous view, declare the
camera cut at frame 7 and still equal the reference resolve byte for byte;
with X3M_TAA_SENTINEL=1 the colour must equal the run without a camera (the
switch off changes nothing); the strict mode (2) must skip the resolve on every
frame without a readable camera and nothing else; the DLL's camera_state lines
must carry the fixture's matrices and decisions. One "envmap" run inserts the
frame routine's environment-map sequence (mid-frame EndScene, six cube-face
target changes with their own Clear, view and draws, BeginScene) between
routed frames, before the scene's depth Clear and before the initial Clear:
nothing of those frames routes, the scene camera is never read, the resolve
does not run, and the history is dropped.
Render-state shadow (X3M_STATE_SHADOW=1; six runs with it off): the route
answers its per-draw render-state queries from a shadow fed by the
SetRenderState hook instead of GetRenderState. With X3M_STATE_SHADOW=0 (or
unset, the production default) and no other hook reason the hybrid unhook
applies (docs/architecture/state-call-fast-path.md, step 5): the
SetRenderState/SetSamplerState hooks are not installed and the route reads at
the draw, once per state per draw (`rs_mode=get`); lazy RT mode holds the
bindings but never a write mask (route-per-draw-cost.md, lever 3), so it keeps
the same unhooked configuration (`state_hooks installed=0 reason=none`). The
regular script on both DLLs, the seam with TAA and in lazy mode, and the burst
script in both binding modes repeat with the shadow off and must equal their
shadow-on twins in colour, readback files, per-draw route decisions, checks
and restorations; the DLL's per-frame counters must show zero route
GetRenderState calls with the shadow on (every query a hit, outside the frames
with a state block Apply/EndStateBlock resynchronization, where each shadowed
state is read once more), one per query in native mode, and one per state per
draw in get mode (every query a within-draw hit or one native read). The regular script also writes a state
through a state block Apply and records one between BeginStateBlock and
EndStateBlock (never applied); the burst script writes and reads back
COLORWRITEENABLE1 between routed draws in lazy mode (the lazy-mode hole).
Engine scene-end hook (X3M_SCENE_HOOK, seam, TAA; the "hook" script): the
fixture builds a frame-routine stub whose five-byte E8 callsite the seam DLL
patches through the same install path as the game's 0x004721b1 -> 0x004c4750
patch (identity gate bypassed); a fake compositor stands in for the glow pass.
With the patch installed the DLL must refuse a CALL to another target and a
site that is not a CALL without touching a byte, patch the verified site to
its own trampoline, signal exactly once per callsite call before the
compositor with ESI/EDI/EBX/EBP preserved, resolve at the hook in glow-on and
glow-off frames (byte for byte the reference resolve), fall back to the copy
path when the only signal arrives outside the Scene phase (a logged
disagreement) and restore the original bytes at shutdown; the same script
unpatched (X3M_SCENE_HOOK=0) resolves at the copy only, so its glow-off frames
skip and the colour of the frames both runs resolve is identical. Since review
26 the switch defaults to on with the route: the "hook-default" run leaves
X3M_SCENE_HOOK unset and must behave as the patched run (the production
install path still fails closed on the fixture executable, executable_mismatch).
Every other case sets X3M_SCENE_HOOK=0 explicitly: the fixture executable
cannot take the production patch, so the selector/copy boundary those cases
verify is what the game gets whenever the patch is refused.
FP16 HDR scene path, stage 1 (X3M_HDR=1; docs/architecture/hdr-scene-path.md):
the route binds an owned A16B16G16R16F target as RT0 at the latching Clear and
writes it back into the game's 8-bit main target with the identity tonemap at
the scene end. Twin runs of existing scripts with the switch on (regular
script on both DLLs, under the ownership wrapper, with TAA, the hook script,
the environment-map script) must present the same frames as their twins: the
per-frame presented_<frame>.bgra8 dumps are compared per pixel and must agree
exactly, the seam's RT1/RT2 readback files must be identical (the routed
draws write the four-format MRT: FP16 RT0 + RGBA32F + R32F), every hdr_frame
line must report the redirect, its end point (bloom copy, hook, or Present
after the EndScene flush) and no unwind, and the device must still reach zero
references. "hdrvalues" proves 2.0 and 8.0 drawn plainly and additively reach
the FP16 target as 10.0/8.0 with alpha carried while the presented frame holds
the clamped codes, across a Reset with a dimension change and a mid-scene RT0
switch; "hdrfault" injects one failure per frame into the write-back ladder
and requires a valid presented frame and recovery; two runs force the FP16
render-target capability and the self test absent and require the feature to
disable itself. Four bench runs time the boundary with the redirect on.
Reviewed shader bytes are read from local files and never enter the repository
or the reports.
"""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
from verify_ownership_integration import verify_admission  # noqa: E402
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import analyze_motion_readback as readback_analysis  # noqa: E402
import agx_reference as agx_ref  # noqa: E402  (stage 2: the tonemap oracle)
import exposure_reference as exposure_ref  # noqa: E402  (stage 2: the meter/adaptation oracle)
import shadow_replay_candidates as candidates_analysis  # noqa: E402  (caster-candidate counter lines)
import shadow_replay_depth as depth_replay  # noqa: E402  (cascade-0 depth replay lines and the CPU projection of the map)
import shadow_retention as retention_analysis  # noqa: E402  (caster-retention frame, resight and caster lines)
import sun_shadow_apply as sun_apply  # noqa: E402  (the CPU twin of the sun-shadow apply quad)
sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_guard import game_running  # noqa: E402
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory
PROBE = ROOT / 'verification/probe'
BUILD = PROBE / 'build'
RESULTS = bottle.results_dir(ROOT)
EXE = BUILD / 'motion_output_fixture.exe'
SEAM = BUILD / 'motion-output-seam/d3d9.dll'
DLL = ROOT / 'build/d3d9.dll'
WINE = Path('/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine')
RAW = {
    Path('/tmp/x3-shader-sweep/programs/vs_53a0a641107ed76c.bin'): 'bc402d1c2bfbbcb9fedd98890db845dab2a24da8cfb5a88a74c4eafa40f7a50c',
    Path('/tmp/x3-shader-sweep/programs/ps_8759c7838bbc86c2.bin'): '9fd15484fe419295cfb3534bd4f978efc8855c1e3e6a06e776533497dad48dc0'}
# Environments beyond the route switch. 'plain' is the original four runs; the
# other three wrap the device exactly as tools/manage.py does for the gameplay
# diagnostic run (--ownership --object-trace --object-lifetime --motion-output),
# with the copy-depth path and the admission monitor added because the launcher
# may enable them alongside the route. The route's native-slot calls then land
# in the wrapper's methods, so these runs cover its bookkeeping, refcount model,
# Reset ordering and HRESULT observation against the route's own calls.
VARIANTS = {
    'plain': {},
    'ownership': dict(X3M_OWNERSHIP='1'),
    'depth': dict(X3M_OWNERSHIP='1', X3M_DEPTH_COPY='1', X3M_SCENE_DEPTH_CAPTURE='1'),
    'admission': dict(X3M_OWNERSHIP='1', X3M_ADMISSION='1')}
def case(name, mode, variant='plain', enabled='1', jitter=False, taa=False, bench=None, lazy=False, burst=False, camera=False, sentinel=None, envmap=False,
         hook=None, shadow=True, hdr=False, hdr_fault=None, hdr_env=None, mip_bias=None, mipbias=False, cutout=None):
    return dict(name=name, mode=mode, variant=variant, enabled=enabled, jitter=jitter, taa=taa, bench=bench, lazy=lazy, burst=burst,
                camera=camera, sentinel=sentinel, envmap=envmap, hook=hook, shadow=shadow, hdr=hdr, hdr_fault=hdr_fault, hdr_env=hdr_env or {},
                mip_bias=mip_bias, mipbias=mipbias, cutout=cutout)


CASES = [case(f'{dll}-{state}' if variant == 'plain' else f'{dll}-{variant}-{state}', dll, variant, enabled)
         for variant in VARIANTS for dll in ('production', 'seam') for state, enabled in (('off', '0'), ('on', '1'))]
CASES += [case(f'{dll}-jitter-on', dll, jitter=True) for dll in ('production', 'seam')]
# Depth-only prepass parity (asteroid-fog-temporal.md, run 47): the z_only
# vs_1_1 prepass and the later jittered LESSEQUAL draw of the same geometry
# must agree; the frame line's unjittered_depth_writers must stay 0.
CASES += [case(f'{dll}-zonly', 'zonly', jitter=True) for dll in ('production', 'seam')]
# Temporal step 3: the resolve at the boundary (jitter implied by the DLL),
# plain and through the wrapper, with the debug readbacks of the resolved image.
CASES += [case(f'{dll}-taa-on', dll, jitter=True, taa=True) for dll in ('production', 'seam')]
CASES += [case(f'{dll}-ownership-taa-on', dll, 'ownership', jitter=True, taa=True) for dll in ('production', 'seam')]
BENCH_SIZES = ('1280x768', '5120x1440')
CASES += [case(f'bench-{size}-taa-{state}', 'bench', jitter=True, taa=state == 'on', bench=size) for size in BENCH_SIZES for state in ('off', 'on')]
# Lazy RT binding: the regular script (equivalent by construction, every
# getter restores) and the burst script (consecutive routed draws).
CASES += [case(f'{dll}-lazy-on', dll, lazy=True) for dll in ('production', 'seam')]
CASES += [case('seam-ownership-lazy-on', 'seam', 'ownership', lazy=True), case('seam-taa-lazy-on', 'seam', jitter=True, taa=True, lazy=True)]
CASES += [case(f'{dll}-burst-{rt}', dll, lazy=rt == 'lazy', burst=True) for dll in ('production', 'seam') for rt in ('perdraw', 'lazy')]
# Selected-only additions: preserve the existing default suite inventory.
WRAP_CASES = [case(f'seam-burst-{rt}-wrap', 'seam', lazy=rt == 'lazy', burst=True,
                   hdr_env={'X3M_FIXTURE_WRAP': '1'}) for rt in ('perdraw', 'lazy')]
# Hook-free lazy RT (route-per-draw-cost.md lever 3): the burst with both write
# masks written under a held binding, a hold that starts masked, a mid-scene
# StretchRect (frames 2, 5, 8), an application depth-surface change (same size,
# smaller, original) and a Reset inside the scene under a held
# binding (frame 4); per-draw twin, production hybrid unhook (`auto`), the
# explicit shadow and the ownership wrapper. Part of the default suite (six
# short burst runs); compare_mask_twins runs in the full and the selected path.
MASK_ENV = {'X3M_FIXTURE_BURST_MASK': '1'}
MASK_CASES = [case('seam-burst-perdraw-mask', 'seam', burst=True, shadow='auto', hdr_env=MASK_ENV),
              case('seam-burst-lazy-mask', 'seam', lazy=True, burst=True, shadow='auto', hdr_env=MASK_ENV),
              case('seam-burst-lazy-mask-shadow', 'seam', lazy=True, burst=True, shadow=True, hdr_env=MASK_ENV),
              case('seam-ownership-burst-lazy-mask', 'seam', 'ownership', lazy=True, burst=True, shadow='auto', hdr_env=MASK_ENV),
              case('production-burst-perdraw-mask', 'production', burst=True, shadow='auto', hdr_env=MASK_ENV),
              case('production-burst-lazy-mask', 'production', lazy=True, burst=True, shadow='auto', hdr_env=MASK_ENV)]
# Each lazy run is compared with the per-draw run of the same DLL (the wrapper run with the plain seam twin).
MASK_TWINS = {'seam-burst-lazy-mask': 'seam-burst-perdraw-mask', 'seam-burst-lazy-mask-shadow': 'seam-burst-perdraw-mask',
              'seam-ownership-burst-lazy-mask': 'seam-burst-perdraw-mask', 'production-burst-lazy-mask': 'production-burst-perdraw-mask'}
CASES += MASK_CASES
MASK_RESET_FRAME = 4
# Camera reprojection of sentinel pixels (seam): the switch in its three
# positions with the fixture's rotating camera, the strict mode without a
# camera, and the environment-map exclusion script.
CASES += [case('seam-taa-camera-on', 'seam', jitter=True, taa=True, camera=True),
          case('seam-taa-camera-sentinel1-on', 'seam', jitter=True, taa=True, camera=True, sentinel='1'),
          case('seam-taa-camera-sentinel2-on', 'seam', jitter=True, taa=True, camera=True, sentinel='2'),
          case('seam-taa-sentinel2-nocamera-on', 'seam', jitter=True, taa=True, sentinel='2'),
          case('seam-taa-envmap', 'seam', jitter=True, taa=True, camera=True, envmap=True)]
# Caster-candidate counter (docs/architecture/shadow-replay-gates.md,
# "Implemented"; X3M_SHADOW_REPLAY_CANDIDATES=1 needs the route and the
# ownership wrapper only): the camera run through the wrapper, so the rows'
# origin distance is defined; the seam's X3M_FIXTURE_SLICE_NEAR lowers the
# slice-0 near bound to the fixture's unit-distance triangles (production
# keeps 6). The runner checks the per-frame line's identities, one line per
# routed frame, the managed/leased population and the 16-witness cap.
CANDIDATES_ENV = dict(X3M_SHADOW_REPLAY_CANDIDATES='1', X3M_FIXTURE_SLICE_NEAR='0.5')
CASES += [case('seam-ownership-taa-camera-candidates-on', 'seam', 'ownership', jitter=True, taa=True, camera=True, hdr_env=CANDIDATES_ENV)]
# One-cascade depth replay (shadow-replay-gates.md, "Implemented: cascade-0
# depth replay fixture"; X3M_SHADOW_REPLAY_DEPTH=1 with the counter's two
# prerequisites): the "shadowreplay" script through the ownership wrapper with
# the rotating camera, with and without the resolve, each with an option-off
# twin whose presented frames must be byte-identical; the seam narrows cascade
# 0 to the fixture's unit-size geometry (X3M_FIXTURE_SHADOW_EXTENT, half-extent
# 8 units centred on the camera) and a 256-texel map keeps the CPU projection
# small. The caster-count cases time the transaction at the production size.
# X3M_SHADOW_REPLAY_CAP = casters: the script's two bounds objects lie beyond
# the origin rule (L across the box, drawn first; F outside it, drawn last), so
# frame 0 (no extents yet) admits the casters alone and later frames L and the
# casters, of which the cap drops the last submitted caster (capped=1).
SHADOW_REPLAY_BOUNDS_OBJECTS = 2
SHADOW_REPLAY_ENV = dict(X3M_SHADOW_REPLAY_DEPTH='1', X3M_SHADOW_REPLAY_SIZE='256', X3M_FIXTURE_SLICE_NEAR='0.5', X3M_FIXTURE_SHADOW_EXTENT='8',
                         X3M_SHADOW_REPLAY_CAP='2')
SHADOW_REPLAY_OFF_ENV = dict(X3M_SHADOW_REPLAY_DEPTH='0', X3M_FIXTURE_SLICE_NEAR='0.5', X3M_FIXTURE_SHADOW_EXTENT='8')
SHADOW_REPLAY_TWINS = {'seam-ownership-shadow-replay-on': 'seam-ownership-shadow-replay-off',
                       'seam-ownership-taa-shadow-replay-on': 'seam-ownership-taa-shadow-replay-off'}
SHADOW_REPLAY_CASTERS = (2, 8, 20)
SHADOW_REPLAY_FRAMES, SHADOW_REPLAY_LOCK_FRAME, SHADOW_REPLAY_RESET_FRAME = 8, 2, 4
SHADOW_REPLAY_NO_SUN_FRAME, SHADOW_REPLAY_MULTISTREAM_FRAME = 5, 7  # one frame of another LightDir_Dir0 (refused sun_changing); caster 0 under a two-stream declaration
# The frame's one validated sun (docs/verification/directional-shadows.md, "Run
# 38 A (run111) diagnosis", cause 1): with X3M_FIXTURE_SHADOW_SUN_PROGRAMS the
# script draws a glow-style pair (c4 = g_EnableGlow = (1,0,0,0), LightDir_Dir0
# at c5) and a detail-style pair (LightDir_Dir0 at c0) first on every frame;
# the latch takes the sun from c5 and the map's basis is the true sun's.
SHADOW_SUN_PROGRAMS = ('vs_494fe349b8bc12ec.bin', 'ps_fffdabd910793aba.bin', 'vs_b0602757fce6e870.bin', 'ps_517540ae6d5e5410.bin')
SHADOW_SUN_PROGRAMS_DIRECTORY = Path('/tmp/x3-shader-sweep/programs')
SHADOW_SUN_PROGRAM_OBJECTS = 2
CASES += [case('seam-ownership-shadow-replay-on', 'shadowreplay', 'ownership', camera=True, hdr_env=SHADOW_REPLAY_ENV),
          case('seam-ownership-shadow-replay-off', 'shadowreplay', 'ownership', camera=True, hdr_env=SHADOW_REPLAY_OFF_ENV),
          case('seam-ownership-taa-shadow-replay-on', 'shadowreplay', 'ownership', jitter=True, taa=True, camera=True, hdr_env=SHADOW_REPLAY_ENV),
          case('seam-ownership-taa-shadow-replay-off', 'shadowreplay', 'ownership', jitter=True, taa=True, camera=True, hdr_env=SHADOW_REPLAY_OFF_ENV)]
CASES += [case(f'seam-ownership-shadow-replay-casters-{n}', 'shadowreplay', 'ownership', camera=True,
               hdr_env=dict(SHADOW_REPLAY_ENV, X3M_SHADOW_REPLAY_SIZE='1024', X3M_FIXTURE_SHADOW_CASTERS=str(n), X3M_SHADOW_REPLAY_CAP=str(n))) for n in SHADOW_REPLAY_CASTERS]
# Tunable cascade (X3M_SHADOW_REPLAY_EXTENT / _DEPTH_HALF, no fixture narrowing;
# F moved to 900 units by X3M_FIXTURE_SHADOW_FAR_T=720, vertices at 825): the
# wide case (half-extent 1000, depth half-range 2048, 2048^2 map) admits F by
# bounds from frame 1 on and, with the cap at casters + 2, replays L, the
# casters and F; the far-refused case keeps the production 250 / 512 box
# (1024^2 map) and refuses the same object as the narrow cases do.
SHADOW_REPLAY_WIDE_ENV = dict(X3M_SHADOW_REPLAY_DEPTH='1', X3M_SHADOW_REPLAY_SIZE='2048', X3M_SHADOW_REPLAY_EXTENT='1000', X3M_SHADOW_REPLAY_DEPTH_HALF='2048',
                              X3M_FIXTURE_SLICE_NEAR='0.5', X3M_FIXTURE_SHADOW_FAR_T='720', X3M_SHADOW_REPLAY_CAP='4')
SHADOW_REPLAY_FAR_ENV = dict(X3M_SHADOW_REPLAY_DEPTH='1', X3M_SHADOW_REPLAY_SIZE='1024', X3M_FIXTURE_SLICE_NEAR='0.5', X3M_FIXTURE_SHADOW_FAR_T='720', X3M_SHADOW_REPLAY_CAP='2')
SHADOW_REPLAY_SUN_ENV = dict(SHADOW_REPLAY_ENV, X3M_FIXTURE_SHADOW_SUN_PROGRAMS='Z:' + str(SHADOW_SUN_PROGRAMS_DIRECTORY))
# Cascades through the DLL (docs/architecture/shadow-cascades.md, section 4):
# two cascades, the seam narrowing them to 8 and 400 units around the script's
# unit-size geometry (X3M_FIXTURE_SHADOW_CASCADES; the production list only
# sets the count), caps 2 and 8, budget 5 below the frame's 6 issues, so the
# far cascade replays on even frames only. Per frame the runner asserts c<i>,
# capped<i>, draws<i>, far_replayed and far_frame, the retained far map on the
# odd frames, its loss at every refusal and at the Reset, and every replayed
# map against the CPU projection of the draws that cascade kept.
SHADOW_REPLAY_CASCADES_ENV = dict(X3M_SHADOW_REPLAY_DEPTH='1', X3M_SHADOW_REPLAY_SIZE='256', X3M_FIXTURE_SLICE_NEAR='0.5', X3M_SHADOW_CASCADES='250,1500', X3M_FIXTURE_SHADOW_CASCADES='8,400',
                                  X3M_SHADOW_CASCADE_SIZES='256', X3M_SHADOW_CASCADE_CAPS='2,8', X3M_SHADOW_CASCADE_BUDGET='5')
CASES += [case('seam-ownership-shadow-replay-sun-programs', 'shadowreplay', 'ownership', camera=True, hdr_env=SHADOW_REPLAY_SUN_ENV),
          case('seam-ownership-shadow-replay-cascades', 'shadowreplay', 'ownership', camera=True, hdr_env=SHADOW_REPLAY_CASCADES_ENV),
          case('seam-ownership-shadow-replay-cascades-casters-20', 'shadowreplay', 'ownership', camera=True,
               hdr_env=dict(SHADOW_REPLAY_CASCADES_ENV, X3M_SHADOW_CASCADE_SIZES='1024', X3M_FIXTURE_SHADOW_CASTERS='20', X3M_SHADOW_CASCADE_CAPS='32,32', X3M_SHADOW_CASCADE_BUDGET='640'))]
# The sun as a polled world position (docs/verification/directional-shadows.md,
# "Sun at finite distance"): the cascade script with the fixture's own render
# context behind the poll seam (X3M_FIXTURE_SHADOW_POLL). agree: the sun node
# 100,000 units away and every draw's LightDir_Dir0 = normalize(light - its
# origin): the poll validates and every cascade's basis is normalize(light -
# centre) held within 1 / size radians. null / disagree: the poll is
# unavailable / refused by the cross-check, the latch stays the source, counted
# by reason, and every other expectation is the plain cascade case's.
SHADOW_POLL_MODES = ('agree', 'null', 'disagree', 'refusals')  # refusals: a layout, pointer or content defect per frame
# The at-rest sun-shadow A/B (docs/architecture/comparison-hotkeys.md, "Sun
# shadows at rest"): the cascade script with the Ctrl+Shift+F12 seam pressed at
# four frame boundaries, off at 1 and 4, on at 3 and 6. A frame of an off
# window carries no shadow_replay_depth line at all (no map cleared or drawn;
# the Reset before frame 4 falls inside the second window, so the maps stay
# released) and every cascade reads back invalid there: the retained far basis
# is voided by the press and never republished stale. Frame 3 is the load-
# bearing ON edge: odd, with 6 issues against a budget of 5, so the budget's
# alternate-frame rule alone would leave the far cascade absent; it must replay
# in full anyway. Frame 6 is the ON edge after a Reset.
SHADOW_TOGGLE_PRESSES = (1, 3, 4, 6)  # off, on, off, on
CASES += [case('seam-ownership-shadow-replay-cascades-toggle', 'shadowreplay', 'ownership', camera=True,
               hdr_env=dict(SHADOW_REPLAY_CASCADES_ENV, X3M_FIXTURE_SHADOW_TOGGLE=','.join(str(v) for v in SHADOW_TOGGLE_PRESSES)))]
# The same A/B on the single-map path, with the capture window over the off
# frames: an F8 while the shadows are off must dump no map at all and report
# the basis invalid, never the last replayed frame's content.
# The capture window must sit before the script's Reset, which closes it for
# good (capture.cpp clears `remaining` and the start frame has passed), so the
# press pair is 2 off / 3 on and F8 covers exactly those two frames: the
# negative control (off: no dump, basis invalid) and the positive one (the same
# F8 on the frame back on dumps its map).
SHADOW_TOGGLE_SINGLE_PRESSES = (2, 3)
CASES += [case('seam-ownership-shadow-replay-toggle-single', 'shadowreplay', 'ownership', camera=True,
               hdr_env=dict(SHADOW_REPLAY_ENV, X3M_FIXTURE_SHADOW_TOGGLE=','.join(str(v) for v in SHADOW_TOGGLE_SINGLE_PRESSES),
                            X3M_CAPTURE_START='2', X3M_CAPTURE_FRAMES='2'))]
CASES += [case(f'seam-ownership-shadow-replay-cascades-poll-{m}', 'shadowreplay', 'ownership', camera=True, hdr_env=dict(SHADOW_REPLAY_CASCADES_ENV, X3M_FIXTURE_SHADOW_POLL=m)) for m in SHADOW_POLL_MODES]
# Own-ship-adaptive cascade 0 (shadow-cascade-extents.md, section 5): the
# cascade script under four cascades narrowed to 8 / 48 / 240 / 800 (the set R
# ratios), K = 1.5, with two hulls drawn every frame: H1 (radius about 1.69
# through its rows under the script's camera) and H2 (about 33.7). `small`
# names H1 as the player ship: E0 stays 8 and every cascade stays active;
# `big` names H2: E0 = 1.5 x 33.7 = 50.6 from frame 2 (its first measured
# frame commits at the boundary after frame 1), cascade 1 (48 < 3 x 50.6)
# dropped, 2 and 3 kept; `swap` names H1 until frame 5 and H2 from it: one
# re-anchor event at the frame-5 boundary, the far cascades untouched.
SHADOW_ADAPTIVE_ENV = dict(X3M_SHADOW_REPLAY_DEPTH='1', X3M_SHADOW_REPLAY_SIZE='256', X3M_FIXTURE_SLICE_NEAR='0.5', X3M_SHADOW_CASCADES='250,1500,7500,25000',
                           X3M_FIXTURE_SHADOW_CASCADES='8,48,240,800', X3M_SHADOW_CASCADE_SIZES='256', X3M_SHADOW_CASCADE_CAPS='8', X3M_SHADOW_CASCADE_BUDGET='640',
                           X3M_SHADOW_CASCADE_ADAPTIVE_C0='1.5')
# `reuse`: H1 is the ship and H2's node is declared its part until frame 4, from which the same
# address carries another handle (a freed part reused by a non-own node): the (node, handle)
# cache misses, H2 no longer counts, the frame radius drops to H1's while the committed E0
# holds under the hysteresis (no inflation, no re-anchor).
# The ladder behind E0 (shadow-cascade-extents.md, section 5, the sliding ladder): while E0 is
# above the configured 8, cascade i becomes max(configured, E0 x R^i), R = 5 (the DLL's default,
# X3M_SHADOW_CASCADE_LADDER_RATIO unset), capped at the last cascade's extent, and a cascade
# whose slid extent reaches the next one's is dropped. `big` (E0 50.59): 50.59 / 252.9 / 800
# (cascade 2 capped at the ceiling and dropped), every slid cascade voided at the commit.
SHADOW_ADAPTIVE_K, SHADOW_ADAPTIVE_SWAP_FRAME, SHADOW_ADAPTIVE_REUSE_FRAME, SHADOW_ADAPTIVE_RATIO = 1.5, 5, 4, 5.0
CASES += [case(f'seam-ownership-shadow-replay-adaptive-{m}', 'shadowreplay', 'ownership', camera=True, hdr_env=dict(SHADOW_ADAPTIVE_ENV, X3M_FIXTURE_OWN_SHIP=m)) for m in ('small', 'big', 'swap')]
CASES += [case('seam-ownership-shadow-replay-adaptive-reuse', 'shadowreplay', 'ownership', camera=True,
               hdr_env=dict(SHADOW_ADAPTIVE_ENV, X3M_FIXTURE_OWN_SHIP='reuse', X3M_CAPTURE_START='5', X3M_CAPTURE_FRAMES='4'))]  # capture lines on frames 5-7 (the Reset before frame 4 clears a window opened on it)
# The ladder cases on the five-cascade 250 / 1,500 / 7,500 / 37,500 / 150,000 set narrowed by
# 1/31.25 to 8 / 48 / 240 / 1,200 / 4,800: `corvette` names H3 (radius 14.5, the 450-unit M6 at
# that scale): E0 21.7 and the full slid set 21.7 / 108.5 / 542 / 2,712 / 4,800, every cascade
# kept and evenly spaced (ratio 5 up to the ceiling), no 11x gap behind cascade 0; `destroyer`
# names H4 (radius 182, an M2 at 5,700 units; E0 274 clears the far object's 240-unit reach,
# so F's box membership is settled): 274 / 1,369, then 25 E0 = 6,845 is capped at the 4,800
# ceiling and reaches it, so cascades 2 and 3 are dropped and the set is 274 / 1,369 / 4,800;
# `shrink` names H3 until frame 5 and H1 from it: the configured set returns in one
# commit at the frame-5 boundary (cascades 0-3 re-anchored, cascade 4 untouched).
# Map sizes 256 / 256 / 512 / 1,024 / 2,048: at the fixture scale the far object L (19 x 16 units at 256) is one texel of a 256-texel
# 1,200 or 4,800 map; the finer far maps give the twin clear texels to compare (texel 2.3 at 1,200, 4.7 at 4,800).
SHADOW_LADDER_ENV = dict(SHADOW_ADAPTIVE_ENV, X3M_SHADOW_CASCADES='250,1500,7500,37500,150000', X3M_FIXTURE_SHADOW_CASCADES='8,48,240,1200,4800', X3M_SHADOW_CASCADE_SIZES='256,256,512,1024,2048')
CASES += [case(f'seam-ownership-shadow-replay-ladder-{m}', 'shadowreplay', 'ownership', camera=True, hdr_env=dict(SHADOW_LADDER_ENV, X3M_FIXTURE_OWN_SHIP=m)) for m in ('corvette', 'destroyer', 'shrink')]
# The policies slide with the extents: `corvette-static` configures static-only from cascade 3 (1,200 / 4,800). Under the slid set the
# 542 cascade matches the configured 1,200 (the 16,875 cascade of the brief matching 37,500) and the static mask moves to cascades 2-4:
# every draw of the script moves (or is a first sighting), so those cascades refuse every draw (static_only_refused<i>) and replay
# nothing, the far object F (in the static cascades alone) is not leased, and the configured mask returns on frames 0-1 only.
CASES += [case('seam-ownership-shadow-replay-ladder-corvette-static', 'shadowreplay', 'ownership', camera=True, hdr_env=dict(SHADOW_LADDER_ENV, X3M_FIXTURE_OWN_SHIP='corvette', X3M_SHADOW_CASCADE_STATIC_FROM='3'))]
CASES += [case('seam-ownership-shadow-replay-wide', 'shadowreplay', 'ownership', camera=True, hdr_env=SHADOW_REPLAY_WIDE_ENV),
          case('seam-ownership-shadow-replay-far-refused', 'shadowreplay', 'ownership', camera=True, hdr_env=SHADOW_REPLAY_FAR_ENV)]
# Sun-shadow caster retention (docs/architecture/shadow-caster-retention.md,
# "Fixture and twin cases"): the "shadowretention" script under the three
# settings of the DLL. Live: every compared cascade map equals the CPU twin of
# the submitted draws plus the draws the script says a live store must replay
# (each placed by the camera of the frame it was recorded on). Census and off:
# the submitted draws only (the control of every case); the census runs the same
# store without references or replay. The three present byte-identical frames.
# The capture window covers the first retained frames (F8 diagnostics); the
# per-draw telemetry is off (the capacity case submits about 10,000 draws).
SHADOW_RETENTION_ENV = dict(X3M_SHADOW_REPLAY_DEPTH='1', X3M_SHADOW_REPLAY_SIZE='256', X3M_FIXTURE_SLICE_NEAR='0.5', X3M_SHADOW_CASCADES='250,1500', X3M_FIXTURE_SHADOW_CASCADES='8,400',
                            X3M_SHADOW_CASCADE_SIZES='256', X3M_SHADOW_CASCADE_CAPS='1024,1024', X3M_SHADOW_CASCADE_BUDGET='640', X3M_SHADOW_CASTER_RETENTION_AGE='640',
                            X3M_TELEMETRY_DRAW='0', X3M_CAPTURE_START='10', X3M_SHADOW_RETENTION_TIMING='1')
SHADOW_RETENTION_CASES = {'seam-ownership-shadow-retention-live': dict(X3M_SHADOW_CASTER_RETENTION='1'), 'seam-ownership-shadow-retention-census': dict(X3M_SHADOW_RETENTION_CENSUS='1'),
                          'seam-ownership-shadow-retention-off': {}}
SHADOW_RETENTION_SCRIPT = ('a_turn_away', 'b_moving', 'c_retired', 'd_lod_swap', 'e_shared_mesh', 'f_buffer_lock', 'g_release_first', 'h_reset', 'm_observer', 'j_excluded', 'i_capacity', 'k_age_and_sun', 'teardown')
CASES += [case(name, 'shadowretention', 'ownership', camera=True, hdr_env=dict(SHADOW_RETENTION_ENV, **extra)) for name, extra in SHADOW_RETENTION_CASES.items()]
# The same script live under the positional sun (X3M_FIXTURE_SHADOW_POLL=agree): every cascade's basis
# from the polled light, retained records on the point source, then a source switch (the context
# pointer goes null before case k) flushes the store like a re-latch. Its presented frames differ from
# the three above (the draws upload their own LightDir_Dir0), so it is not one of their twins.
SHADOW_RETENTION_POLL_CASE = 'seam-ownership-shadow-retention-live-poll'
CASES += [case(SHADOW_RETENTION_POLL_CASE, 'shadowretention', 'ownership', camera=True, hdr_env=dict(SHADOW_RETENTION_ENV, X3M_SHADOW_CASTER_RETENTION='1', X3M_FIXTURE_SHADOW_POLL='agree'))]
# Far-cascade caster pool control (docs/architecture/shadow-cascade-extents.md,
# "Caster pool control"): the "shadowpool" script (motion_output_shadow_pool_inc.h)
# under two cascades of 8 and 40 units. static: cascade 1 static-only, the
# classification by the ring (store off), by the store's verdict (census and
# live: S enters cascade 1 on frame 9); the three present byte-identical
# frames. importance: cascade 1 capped at 4 keeps the four largest of eight
# under a rotated submission order. records: 4,096 records on cascade 1,
# capped at 4,095 under the importance order (the scene-end selection timed
# at the full list), the far cascade alternating over the budget.
SHADOW_POOL_ENV = dict(X3M_SHADOW_REPLAY_DEPTH='1', X3M_SHADOW_REPLAY_SIZE='256', X3M_FIXTURE_SLICE_NEAR='0.5', X3M_SHADOW_CASCADES='250,1500', X3M_FIXTURE_SHADOW_CASCADES='8,40',
                       X3M_SHADOW_CASCADE_SIZES='256', X3M_SHADOW_CASCADE_BUDGET='640', X3M_TELEMETRY_DRAW='0')
# importance: ids 3 and 4 share one scale; 3 sits at the cap boundary and moves 0.02 row units farther on
# even frames (a few % smaller than 4): the hysteresis keeps it, so the kept set never flips.
# The static script's W (a 200-unit moving sliver, the capital-hull stand-in) enters cascade 1 under
# --shadow-cascade-large-min 100 on the three store settings and never in the strict case.
SHADOW_POOL_STATIC_CASES = {'seam-ownership-shadow-pool-static-off': dict(X3M_SHADOW_CASCADE_LARGE_MIN='100'),
                            'seam-ownership-shadow-pool-static-census': dict(X3M_SHADOW_RETENTION_CENSUS='1', X3M_SHADOW_CASCADE_LARGE_MIN='100'),
                            'seam-ownership-shadow-pool-static-live': dict(X3M_SHADOW_CASTER_RETENTION='1', X3M_SHADOW_CASCADE_LARGE_MIN='100'),
                            'seam-ownership-shadow-pool-static-strict': {}}
CASES += [case(name, 'shadowpool', 'ownership', camera=True, hdr_env=dict(SHADOW_POOL_ENV, X3M_FIXTURE_SHADOW_POOL='static', X3M_SHADOW_CASCADE_CAPS='1024', X3M_SHADOW_CASCADE_STATIC_FROM='1', **extra))
          for name, extra in SHADOW_POOL_STATIC_CASES.items()]
# The run 40 A (run116) cases (directional-shadows.md, "Run 40 A (run116) diagnosis"; motion_output_shadow_pool_inc.h):
# cycle (cause 1: the store/ring feedback cycle, census and live), jitter (cause 3: the texel-scaled eps of a
# static-only cascade, at extents 8 / 200 so cascade 1's eps is 0.195; J steps 0.156 world units) and hull (cause 4: a sliver whose origin lies
# behind the camera plane is admitted by its extent). Each fails on the pre-fix seam DLL (POOL_EXPECT differs).
SHADOW_POOL_RUN116_CASES = {'seam-ownership-shadow-pool-cycle-census': dict(X3M_FIXTURE_SHADOW_POOL='cycle', X3M_SHADOW_CASCADE_STATIC_FROM='1', X3M_SHADOW_RETENTION_CENSUS='1', X3M_SHADOW_RETENTION_TIMING='1'),
                            'seam-ownership-shadow-pool-cycle-live': dict(X3M_FIXTURE_SHADOW_POOL='cycle', X3M_SHADOW_CASCADE_STATIC_FROM='1', X3M_SHADOW_CASTER_RETENTION='1', X3M_SHADOW_RETENTION_TIMING='1'),
                            'seam-ownership-shadow-pool-jitter-off': dict(X3M_FIXTURE_SHADOW_POOL='jitter', X3M_SHADOW_CASCADE_STATIC_FROM='1', X3M_FIXTURE_SHADOW_CASCADES='8,200'),
                            'seam-ownership-shadow-pool-jitter-live': dict(X3M_FIXTURE_SHADOW_POOL='jitter', X3M_SHADOW_CASCADE_STATIC_FROM='1', X3M_FIXTURE_SHADOW_CASCADES='8,200', X3M_SHADOW_CASTER_RETENTION='1'),
                            'seam-ownership-shadow-pool-hull': dict(X3M_FIXTURE_SHADOW_POOL='hull')}
CASES += [case(name, 'shadowpool', 'ownership', camera=True, hdr_env=dict(SHADOW_POOL_ENV, X3M_SHADOW_CASCADE_CAPS='1024', **extra)) for name, extra in SHADOW_POOL_RUN116_CASES.items()]
CASES += [case('seam-ownership-shadow-pool-importance', 'shadowpool', 'ownership', camera=True,
               hdr_env=dict(SHADOW_POOL_ENV, X3M_FIXTURE_SHADOW_POOL='importance', X3M_SHADOW_CASCADE_CAPS='16,4', X3M_SHADOW_CASCADE_DROP_ORDER='importance')),
          case('seam-ownership-shadow-pool-records', 'shadowpool', 'ownership', camera=True,
               hdr_env=dict(SHADOW_POOL_ENV, X3M_FIXTURE_SHADOW_POOL='records', X3M_SHADOW_CASCADE_CAPS='1024,4095', X3M_SHADOW_CASCADE_RECORDS='1024,4096',
                            X3M_SHADOW_CASCADE_DROP_ORDER='importance', X3M_CAPTURE_START='1000'))]
# Sun-shadow apply quad (legacy-sun-application.md, section 3.3): the
# production SunShadowApplyPass linked into the fixture and driven directly
# (no proxy wiring; the DLL is passive): synthetic A32B32G32R32F RT2 and R32F map of
# a box on a plane at three sun elevations against the CPU twin of the program
# within one FP16 code, hard-shadow edges within the kernel's reach, a far map
# byte-identical, a Reset between two identical frames byte-identical.
SUN_APPLY_FRAMES, SUN_APPLY_RESET_FRAME, SUN_APPLY_FAR_FRAME, SUN_APPLY_ZERO_FRAME = 6, 5, 0, 1
# The receiver depth (docs/architecture/shadow-receiver-depth.md, flipped 2026-09-18 after run 41
# A2): the RT2 is A32B32G32R32F (.b = the view depth) and every SUNAPPLY line says
# depth_encoding=linear; the RT2 dump is rt2.rgba32f. The `<name>-fixture.json` records are the
# former `-linear` siblings; the device records (G32R32F, the z/w law) are gone with the path.
SUN_APPLY_ENCODING = 'linear'


def sun_apply_cases(name, **env):
    return [case(name, 'sunapply', enabled='0', hdr_env=dict(env))]


def rt2_file(depth_encoding):
    assert depth_encoding == SUN_APPLY_ENCODING, depth_encoding
    return 'rt2.rgba32f'


def rt2_lanes(fixture_line, hdr_env):
    """The fixture's RT2 lanes: the line's depth_encoding must be linear (the only encoding; hdr_env unused)."""
    encoding = sun_apply.parse_depth_encoding(fixture_line)
    assert encoding == SUN_APPLY_ENCODING, (encoding, hdr_env)
    return 4


CASES += sun_apply_cases('sun-shadow-apply')
# The wide configuration: the same scene scaled by 200 (a 400-unit box, the
# camera 1,500 units away) under the production cascade 1000 / 2048 / 2048^2
# (world texel 0.977) with the bias resolved from world units
# (sun_shadow_apply_bias, default X3M_SUN_SHADOW_BIAS_UNITS).
SUN_APPLY_WIDE_ENV = dict(X3M_FIXTURE_SUNAPPLY_WIDE='1', X3M_SHADOW_REPLAY_SIZE='2048', X3M_SHADOW_REPLAY_EXTENT='1000', X3M_SHADOW_REPLAY_DEPTH_HALF='2048')
CASES += sun_apply_cases('sun-shadow-apply-wide', **SUN_APPLY_WIDE_ENV)
# Sun-shadow cascades (docs/architecture/shadow-cascades.md, section 4): the
# production ShadowReplayPass (three 256^2 maps, one transaction) and the
# cascade apply program driven directly on real geometry, against the twin:
# (a) far plane, (b) seam, (c) sun column, (d) retained far cascade, (e) Reset,
# (f) pancaked occluder beyond the light-side range. The script, mirrored from
# motion_output_sun_apply_cascades_inc.h: case letter per frame; the far
# cascade yields to the budget on the odd frames of (d) and (e).
SUN_APPLY_CASCADES_ENV = dict(X3M_FIXTURE_SUNAPPLY_CASCADES='1')
# g: the box at eight sub-texel phases (the half-texel witness); h: the sun at finite distance (a point light, per-cascade
# suns); i: the half-pixel receiver witness (case a at scale 170, 30 degrees, jittered, eight phases): RT2 is synthesised
# under the D3D9 raster and the analytic shadow is evaluated at the true pixel centres, so the quad's receiver must carry
# the pixel-centre term of its latch (quad_pixel_centre_m20/m21); X3M_FIXTURE_SUNAPPLY_LEGACY_LATCH=1 drops it and fails.
SUN_CASCADE_SCRIPT = 'aabbccddeeef' + 'g' * 8 + 'h' + 'i' * 8
SUN_CASCADE_FAR_SKIPPED, SUN_CASCADE_RESET_FRAME, SUN_CASCADE_IDENTITY = (7, 9), 9, (8, 10)
CASES += sun_apply_cases('sun-shadow-apply-cascades', **SUN_APPLY_CASCADES_ENV)
# Five cascades (shadow-cascade-extents.md, section 3): the 30 km set
# 250 / 1,500 / 7,500 / 37,500 / 150,000 on five 256^2 maps through the fifth
# sampler and branch of the apply program. The script, mirrored from
# motion_output_sun_apply_cascades_inc.h (cascade_script_five): (letter, owning
# cascade) per frame; 'r' places the box's shadow at that cascade's range, owned
# by it alone; 's' runs it through that cascade's blend band into the next; 'd'
# and 'e' are the budget pair and the Reset triple of the three-cascade script
# on the fifth cascade (skipped on the odd frames 11 and 13).
SUN_CASCADE5_EXTENTS = (250.0, 1500.0, 7500.0, 37500.0, 150000.0)
SUN_CASCADE5_SCRIPT = (('r', 0), ('r', 1), ('r', 2), ('r', 3), ('r', 4), ('s', 0), ('s', 1), ('s', 2), ('s', 3), ('r', 0), ('d', 4), ('d', 4), ('e', 4), ('e', 4), ('e', 4))
SUN_CASCADE5_FAR_SKIPPED, SUN_CASCADE5_RESET_FRAME, SUN_CASCADE5_IDENTITY = (11, 13), 13, (12, 14)
SUN_CASCADE5_EPS_RANGE = 15512.0  # the three-cascade script's cascade-0 depth range, whose EPS_DEPTH the twin keeps in world units here
CASES += sun_apply_cases('sun-shadow-apply-cascades-5', X3M_FIXTURE_SUNAPPLY_CASCADES='5')
# The run 40 A (run116) faces case (directional-shadows.md, "Run 40 A (run116) diagnosis", cause 2;
# motion_output_sun_apply_cascades_inc.h, the faces script): the fifth cascade's box under the eight
# jitter offsets, eight control frames (front-face maps: the box's lit faces darken themselves and
# re-roll with the jitter) and eight fixed frames (back-face maps on every cascade of the texel
# law): the lit faces stay lit, the faces away
# from the sun stay shadowed. With X3M_FIXTURE_SUNAPPLY_FACES_FIX=0 the second half is the control
# too, and the validator fails: the pre-fix witness.
SUN_FACES_FRAMES, SUN_FACES_FIXED_FROM = 16, 8
CASES += sun_apply_cases('sun-shadow-apply-cascades-5-faces', X3M_FIXTURE_SUNAPPLY_CASCADES='5', X3M_FIXTURE_SUNAPPLY_FACES='1')
# The slope-scaled margin (directional-shadows.md, "Run 40 B (run119) near flicker"): the three cascade cases again with
# X3M_SUN_SHADOW_BIAS_SLOPE_TEXELS at the production default 0.2 (the fixture's own default stays 0, so the records above
# are the baseline law), as sibling records for both receiver encodings; the same validators, the GPU against the twin's slope term.
SUN_APPLY_SLOPE = '0.2'
for _name, _env in (('sun-shadow-apply-cascades', SUN_APPLY_CASCADES_ENV), ('sun-shadow-apply-cascades-5', dict(X3M_FIXTURE_SUNAPPLY_CASCADES='5')),
                    ('sun-shadow-apply-cascades-5-faces', dict(X3M_FIXTURE_SUNAPPLY_CASCADES='5', X3M_FIXTURE_SUNAPPLY_FACES='1'))):
    CASES += [dict(c, name=c['name'].replace(_name, _name + '-slope')) for c in sun_apply_cases(_name, **dict(_env, X3M_SUN_SHADOW_BIAS_SLOPE_TEXELS=SUN_APPLY_SLOPE))]
# Render-state shadow A/B (X3M_STATE_SHADOW=0): twins of shadow-on runs.
SHADOW_TWINS = {'production-shadow-off': 'production-on', 'seam-shadow-off': 'seam-on', 'seam-taa-shadow-off': 'seam-taa-on',
                'seam-lazy-shadow-off': 'seam-lazy-on', 'seam-burst-perdraw-shadow-off': 'seam-burst-perdraw', 'seam-burst-lazy-shadow-off': 'seam-burst-lazy'}
CASES += [case('production-shadow-off', 'production', shadow=False), case('seam-shadow-off', 'seam', shadow=False),
          case('seam-taa-shadow-off', 'seam', jitter=True, taa=True, shadow=False), case('seam-lazy-shadow-off', 'seam', lazy=True, shadow=False),
          case('seam-burst-perdraw-shadow-off', 'seam', burst=True, shadow=False), case('seam-burst-lazy-shadow-off', 'seam', lazy=True, burst=True, shadow=False)]
# Engine scene-end hook script (seam, TAA): the callsite patched and unpatched.
CASES += [case('seam-taa-hook-on', 'seam', jitter=True, taa=True, hook='1'), case('seam-taa-hook-unpatched', 'seam', jitter=True, taa=True, hook='0'),
          case('seam-taa-hook-default', 'seam', jitter=True, taa=True, hook='default')]  # X3M_SCENE_HOOK unset: on with the route
# FP16 HDR scene path, stage 1 (X3M_HDR=1): twins of existing runs, the value
# and fault scripts, the forced-absent capability runs and the bench.
HDR_TWINS = {'production-hdr-on': 'production-on', 'seam-hdr-on': 'seam-on', 'production-ownership-hdr-on': 'production-ownership-on',
             'seam-ownership-hdr-on': 'seam-ownership-on', 'production-taa-hdr-on': 'production-taa-on', 'seam-taa-hdr-on': 'seam-taa-on',
             'seam-taa-hook-hdr-on': 'seam-taa-hook-on', 'seam-taa-envmap-hdr': 'seam-taa-envmap'}
CASES += [case('production-hdr-on', 'production', hdr=True), case('seam-hdr-on', 'seam', hdr=True),
          case('production-ownership-hdr-on', 'production', 'ownership', hdr=True), case('seam-ownership-hdr-on', 'seam', 'ownership', hdr=True),
          case('production-taa-hdr-on', 'production', jitter=True, taa=True, hdr=True), case('seam-taa-hdr-on', 'seam', jitter=True, taa=True, hdr=True),
          case('seam-taa-hook-hdr-on', 'seam', jitter=True, taa=True, hook='1', hdr=True),
          case('seam-taa-envmap-hdr', 'seam', jitter=True, taa=True, camera=True, envmap=True, hdr=True),
          case('seam-hdr-values', 'hdrvalues', hdr=True), case('seam-hdr-fault', 'hdrfault', hdr=True),
          case('seam-hdr-caps-absent', 'seam', hdr=True, hdr_fault='1'), case('seam-hdr-selftest-absent', 'seam', hdr=True, hdr_fault='3')]
CASES += [case(f'bench-{size}-hdr-on-taa-{state}', 'bench', jitter=True, taa=state == 'on', bench=size, hdr=True) for size in BENCH_SIZES for state in ('off', 'on')]
# FP16 HDR scene path, stage 2 (X3M_HDR_TONEMAP=agx): the AgX ramp against the
# Python reference per look, decode mode, clamp and manual EV (seam and
# production DLL), the exposure meter/adaptation script, the tonemap fault
# script, the tonemap program forced absent at attach, and the bench with the
# tonemap and the auto-exposure meter on.
AGX = dict(X3M_HDR_TONEMAP='agx')
# Legacy cases own their exposure mode; production defaults no longer imply Auto.
AGX_AUTO = dict(AGX, X3M_HDR_EXPOSURE='auto')
AGX_MANUAL = dict(AGX, X3M_HDR_EXPOSURE='manual')
RAMP_CASES = {'seam-hdr-ramp-none': dict(AGX_MANUAL, X3M_HDR_EV_MANUAL='0'),
              'seam-hdr-ramp-golden': dict(AGX_MANUAL, X3M_HDR_EV_MANUAL='0', X3M_HDR_LOOK='golden'),
              'seam-hdr-ramp-punchy': dict(AGX_MANUAL, X3M_HDR_EV_MANUAL='0', X3M_HDR_LOOK='punchy'),
              'seam-hdr-ramp-decode-none': dict(AGX_MANUAL, X3M_HDR_EV_MANUAL='0', X3M_HDR_DECODE='none'),
              'seam-hdr-ramp-decode-srgb': dict(AGX_MANUAL, X3M_HDR_EV_MANUAL='0', X3M_HDR_DECODE='srgb'),
              'seam-hdr-ramp-clamp4': dict(AGX_MANUAL, X3M_HDR_EV_MANUAL='0', X3M_HDR_CLAMP='4'),
              'seam-hdr-ramp-ev-minus2': dict(AGX_MANUAL, X3M_HDR_EV_MANUAL='-2'),
              'seam-hdr-ramp-ev-plus1-punchy': dict(AGX_MANUAL, X3M_HDR_EV_MANUAL='1', X3M_HDR_LOOK='punchy', X3M_HDR_CLAMP='16'),
              'production-hdr-ramp-none': dict(AGX_MANUAL, X3M_HDR_EV_MANUAL='0'),
              'seam-hdr-ramp-identity': dict(X3M_HDR_EXPOSURE='manual', X3M_HDR_EV_MANUAL='0')}  # tonemap off: the stage-1 conversion on the same ramp
CASES += [case(name, 'hdrramp', hdr=True, hdr_env=env) for name, env in RAMP_CASES.items()]
# Auto-exposure ceiling (X3M_HDR_EV_MAX): the DLL's production default moved
# from +1.5 EV to +1.0 (3df7b9f) and then to +1.3 (b391635, with the runner's
# HDR_EV_MAX_DEFAULT mirror). The ceiling engages from frame 2 of the exposure
# scripts, so every unpinned Auto case's presented frames drifted away from the
# committed record, which was taken at +1.5. These cases pin the ceiling their
# record was written for instead of following the production default; the
# production ceiling has its own coverage in the exposure component fixtures.
RECORD_EV_MAX = '1.5'
EXPOSURE_CASES = {'seam-hdr-exposure': dict(AGX_AUTO, X3M_HDR_DT_MS='16', X3M_HDR_EV_MAX=RECORD_EV_MAX),
                  # The offset script's level stimulus is designed around a
                  # +2 EV ceiling: with the +1 EV offset and the DLL's own
                  # former ev_max default of 1.5 (75dbbed) made levels A..C clamp onto the
                  # grey target and the script stops exercising the dead band,
                  # so the case pins the ceiling it was written for.
                  'seam-hdr-exposure-offset': dict(AGX_AUTO, X3M_HDR_DT_MS='33', X3M_HDR_EV='1', X3M_HDR_EV_MAX='2', X3M_HDR_ADAPT_UP='0.2', X3M_HDR_ADAPT_DOWN='0.6', X3M_HDR_LOOK='golden')}
CASES += [case(name, 'hdrexposure', hdr=True, hdr_env=env) for name, env in EXPOSURE_CASES.items()]
# The meter chain's level surfaces and readback surfaces through the ownership wrapper (reference accounting at teardown).
CASES += [case('seam-ownership-hdr-exposure', 'hdrexposure', 'ownership', hdr=True, hdr_env=EXPOSURE_CASES['seam-hdr-exposure'])]
CASES += [case('seam-hdr-tonemap-fault', 'hdrtonemapfault', hdr=True, hdr_env=dict(AGX_AUTO, X3M_HDR_DT_MS='16', X3M_HDR_EV_MAX=RECORD_EV_MAX)),
          case('seam-hdr-meter-selftest-unlock', 'hdrtonemapfault', hdr=True, hdr_fault='16', hdr_env=dict(AGX_AUTO, X3M_HDR_DT_MS='16', X3M_HDR_EV_MAX=RECORD_EV_MAX)),
          case('seam-hdr-tonemap-shader-absent', 'hdrtonemapfault', hdr=True, hdr_fault='12', hdr_env=dict(AGX_AUTO, X3M_HDR_DT_MS='16', X3M_HDR_EV_MAX=RECORD_EV_MAX))]
CASES += [case(f'bench-{size}-hdr-tonemap-taa-{state}', 'bench', jitter=True, taa=state == 'on', bench=size, hdr=True, hdr_env=dict(AGX_AUTO, X3M_MOTION_FRAME_LOG='4')) for size in BENCH_SIZES for state in ('off', 'on')]  # frame lines every 4 frames: the adapted state of a timed frame
# FP16 HDR scene path, stage 3 (TAA on HDR): the resolve consumes the FP16
# scene target and the write-back presents tonemap(resolve(HDR)). The HDR
# twins above already run the identity write-back with k = 0 on the FP16
# path (the migration identity in the live route: the resolved FP16 image
# equals the reference pass byte for byte, the presented frames within one
# code of the 8-bit twins). These run the AgX write-back with k = exp2(EV):
# manual EV 0 (k = 1) and 1 (k = 2), auto exposure (k follows the adapted
# EV), the X3M_TAA_K=0 override, the engine hook, the ownership wrapper, the
# production DLL, and the fault script with the resolve failing (fault 14).
TAA_HDR = dict(AGX_MANUAL, X3M_HDR_EV_MANUAL='0', X3M_HDR_DT_MS='16')
CASES += [case('seam-taa-hdr-tonemap-on', 'seam', jitter=True, taa=True, hdr=True, hdr_env=TAA_HDR),
          case('seam-taa-hdr-tonemap-ev1', 'seam', jitter=True, taa=True, hdr=True, hdr_env=dict(TAA_HDR, X3M_HDR_EV_MANUAL='1')),
          case('seam-taa-hdr-tonemap-auto', 'seam', jitter=True, taa=True, hdr=True, hdr_env=dict(AGX_AUTO, X3M_HDR_DT_MS='16')),
          case('seam-taa-hdr-tonemap-k0', 'seam', jitter=True, taa=True, hdr=True, hdr_env=dict(TAA_HDR, X3M_TAA_K='0')),
          case('seam-ownership-taa-hdr-tonemap-on', 'seam', 'ownership', jitter=True, taa=True, hdr=True, hdr_env=TAA_HDR),
          case('production-taa-hdr-tonemap-on', 'production', jitter=True, taa=True, hdr=True, hdr_env=TAA_HDR),
          case('seam-taa-hook-hdr-tonemap-on', 'seam', jitter=True, taa=True, hook='1', hdr=True, hdr_env=TAA_HDR),
          case('seam-taa-hdr-tonemap-fault', 'hdrtonemapfault', jitter=True, taa=True, hdr=True, hdr_env=dict(AGX_AUTO, X3M_HDR_DT_MS='16'))]
# Mip LOD bias (X3M_TAA_MIP_BIAS): the "mipbias" script with the switch unset,
# at 0 (must be byte-identical to unset), at the intended -0.5 (both DLLs,
# lazy RT mode too) and at -1.0 (a second level step for the linearity of the
# evidence), plus regular-script twins with the bias on: they bind no
# mip-mapped texture, so the bias must change nothing (colour, readback
# files, checks) while the hooks are installed and the per-frame line counts.
MIPBIAS_TWINS = {'seam-taa-mipbias-on': 'seam-taa-on', 'production-taa-mipbias-on': 'production-taa-on',
                 'seam-jitter-mipbias-zero': 'seam-jitter-on', 'seam-taa-lazy-mipbias-on': 'seam-taa-lazy-on'}
CASES += [case('production-mipbias-off', 'production', jitter=True, mipbias=True),
          case('production-mipbias-zero', 'production', jitter=True, mipbias=True, mip_bias='0'),
          case('production-mipbias-on', 'production', jitter=True, mipbias=True, mip_bias='-0.5'),
          case('production-mipbias-on1', 'production', jitter=True, mipbias=True, mip_bias='-1.0'),
          case('seam-mipbias-on', 'seam', jitter=True, mipbias=True, mip_bias='-0.5'),
          case('seam-mipbias-lazy-on', 'seam', jitter=True, lazy=True, mipbias=True, mip_bias='-0.5'),
          case('seam-taa-mipbias-on', 'seam', jitter=True, taa=True, mip_bias='-0.5'),
          case('production-taa-mipbias-on', 'production', jitter=True, taa=True, mip_bias='-0.5'),
          case('seam-jitter-mipbias-zero', 'seam', jitter=True, mip_bias='0'),
          case('seam-taa-lazy-mipbias-on', 'seam', jitter=True, taa=True, lazy=True, mip_bias='-0.5')]
# Post-resolve sharpen (X3M_TAA_SHARPEN; docs/architecture/temporal-integration.md
# "Post-resolve sharpen", docs/verification/taa-sharpen.md): twins of the
# unsharpened runs. Off (0) is byte-identical to its twin (history files and
# presented frames). On, the resolved FP16 history equals the twin's byte for
# byte (the sharpen never reaches it) and every presented frame is the Python
# RCAS reference of the resolved image (8-bit route), of the identity
# write-back's clamp of it (HDR, identity) or of its AgX tonemap (HDR,
# tonemap: sharpened AFTER the tonemap), within one code, inside the 3x3
# min/max of the unsharpened display image, alpha carried. Bench: both
# routes at both sizes with the strongest setting.
SHARPEN_TWINS = {'seam-taa-sharpen-off': 'seam-taa-on', 'seam-taa-sharpen-on': 'seam-taa-on', 'seam-taa-sharpen-half': 'seam-taa-on',
                 'seam-taa-hdr-sharpen-on': 'seam-taa-hdr-on', 'seam-taa-hdr-tonemap-sharpen-on': 'seam-taa-hdr-tonemap-on'}
CASES += [case('seam-taa-sharpen-off', 'seam', jitter=True, taa=True, hdr_env=dict(X3M_TAA_SHARPEN='0')),
          case('seam-taa-sharpen-on', 'seam', jitter=True, taa=True, hdr_env=dict(X3M_TAA_SHARPEN='1')),
          case('seam-taa-sharpen-half', 'seam', jitter=True, taa=True, hdr_env=dict(X3M_TAA_SHARPEN='0.5')),
          case('seam-taa-hdr-sharpen-on', 'seam', jitter=True, taa=True, hdr=True, hdr_env=dict(X3M_TAA_SHARPEN='1')),
          case('seam-taa-hdr-tonemap-sharpen-on', 'seam', jitter=True, taa=True, hdr=True, hdr_env=dict(TAA_HDR, X3M_TAA_SHARPEN='1'))]
CASES += [case(f'bench-{size}-taa-sharpen-on', 'bench', jitter=True, taa=True, bench=size, hdr_env=dict(X3M_TAA_SHARPEN='1')) for size in BENCH_SIZES]
# Native-Windows fixes (docs/architecture/native-windows-audit-2026-09-12.md
# D1-D3): twins of seam-taa-on. seam-taa-quad-fvf draws every proxy quad
# through the previous XYZRHW fixed-function path (X3M_FIXTURE_QUAD_FVF=1, a
# fixture-only switch of the seam DLL and the fixture's reference pass) and
# must be byte-identical to the vs_3_0 quads in presented frames, RT1/RT2
# readbacks and FP16 history. seam-taa-copy-draw fails the attach-time
# StretchRect round trip (X3M_FIXTURE_STRETCH_FAULT=1) so the route copies the
# 8-bit target to FP16 and back by same-format StretchRect plus identity draws
# (taa_copy=draw); the history must equal the stretch twin's byte for byte and
# the presented frames within one code (exact expected). seam-msaa presents a
# 2-sample back buffer: the route must refuse the frame (msaa=2, routed 0,
# no jitter, taa_skip 11) with one motion_output_msaa_refused line.
QUAD_TWINS = {'seam-taa-quad-fvf': 'seam-taa-on'}
COPY_TWINS = {'seam-taa-copy-draw': 'seam-taa-on'}
CASES += [case('seam-taa-quad-fvf', 'seam', jitter=True, taa=True, hdr_env=dict(X3M_FIXTURE_QUAD_FVF='1')),
          case('seam-taa-copy-draw', 'seam', jitter=True, taa=True, hdr_env=dict(X3M_FIXTURE_STRETCH_FAULT='1')),
          case('seam-msaa', 'msaa', jitter=True, taa=True)]
# Device references the pass holds after its lazy initialization: the resolve
# program, its mask-snapshot program (split out of the resolve, taa-flicker-
# suppression.md step 0), the identity copy program, the quad vertex program
# and its declaration; the sharpen program joins with the switch on.
TAA_BASE_REFERENCES = 5
CASES += [case(f'bench-{size}-hdr-tonemap-taa-sharpen-on', 'bench', jitter=True, taa=True, bench=size, hdr=True, hdr_env=dict(AGX_AUTO, X3M_MOTION_FRAME_LOG='4', X3M_TAA_SHARPEN='1')) for size in BENCH_SIZES]
SHARPEN_MAX_CODE_ERROR = 1   # GPU rcp/mad against the double-precision reference, plus the 8-bit rounding
HDR_MODES = ('hdrvalues', 'hdrfault', 'hdrramp', 'hdrexposure', 'hdrtonemapfault')
# Cutout steady-state scripts (motion_output_cutout_inc.h, X3M_FIXTURE_CUTOUT_SCRIPT):
# twelve static frames, one exact-pair draw per frame refused at the motion gate.
# `blended` is the game's source-over cutout pass (blend=1 src=5 dst=6 atest=1
# mask=7 zwrite=0): an ordinary native colour draw, never a coverage miss, the
# frame's TAA history retained (the shimmer root cause of runs 11/14). `opaque`
# is a gate refusal of the opaque pass (ALPHAFUNC GREATER): a coverage miss that
# invalidates the frame's history every frame, as documented.
CUTOUT_FRAMES = 12
CUTOUT_ENV = dict(X3M_HDR_TONEMAP='agx', X3M_HDR_DECODE='gamma2.2', X3M_HDR_EXPOSURE='manual', X3M_HDR_EV_MANUAL='0', X3M_HDR_CLAMP='0', X3M_HDR_BLOOM='0',
                  # Material fill (X3M_MATERIAL_FILL): the DLL gained a nonzero
                  # production default (0.03 in 3df7b9f, 0.05 in b391635), which
                  # adds the fill term to every converted material draw of these
                  # scripts while the composite oracle here (run_linear_material
                  # .expected, fill=0) and the committed record are fill-free.
                  # The cutout and fade-route cases pin the fill their oracle and
                  # record were written for; the fill itself is covered by the
                  # linear-material fill oracle cases.
                  X3M_LINEAR_MATERIALS='1', X3M_MATERIAL_FILL='0', X3M_MATERIAL_DIRECT_GAIN='1', X3M_MATERIAL_EMISSIVE_GAIN='1', X3M_LIGHTMAP_EMISSIVE_GAIN='1',
                  X3M_LINEAR_DISTANCE_FADE='0', X3M_LINEAR_EMISSIONS='0', X3M_OWNERSHIP='1', X3M_TAA_SENTINEL='2', X3M_TAA_SHARPEN='0', X3M_TAA_MIP_BIAS='0',
                  X3M_FIXTURE_MOTION_DEPTH='1', X3M_FIXTURE_CAMERA='rotate', X3M_MOTION_FRAME_LOG='1', X3M_TAA_DEBUG='1',
                  X3M_CAPTURE_START='1000000', X3M_CAPTURE_FRAMES='0', X3M_FIXTURE_CUTOUT_MIXED='0', X3M_FIXTURE_CUTOUT_ORDINARY='0')
CASES += [case(f'seam-taa-cutout-{script}', 'cutout', jitter=True, taa=True, lazy=True, hdr=True, cutout=script,
               hdr_env=dict(CUTOUT_ENV, X3M_FIXTURE_CUTOUT_SCRIPT=script)) for script in ('blended', 'opaque')]
# The opaque script per-draw with the hooks off (`rs_mode=get`): the gate reads
# ALPHATESTENABLE and the eight cutout states, then the candidate marker reads
# them again, so the per-draw cache must serve repeat reads (rs_hits > 0) and
# the coverage verdicts must not change.
CASES += [case('seam-taa-cutout-opaque-get', 'cutout', jitter=True, taa=True, lazy=False, hdr=True, cutout='opaque', shadow=False,
               hdr_env=dict(CUTOUT_ENV, X3M_FIXTURE_CUTOUT_SCRIPT='opaque'))]
# Fade-band motion arm scripts (motion_output_fade_route_inc.h, X3M_FIXTURE_FADE_SCRIPT;
# docs/architecture/linear-distance-fade-region.md "Fade-band route"): twelve
# static frames over the eight jitter phases with the rotating camera (cut at
# frame 7), the routed full-screen A plus two quads of the exact fade pair in
# the engine's fade-band state (blend on, SRCALPHA/INVSRCALPHA, Z-write off,
# mask 7). `routed`: fraction 1000 permille, the arm routes both quads (route
# record gate 0, own RT1 rows with alpha 1, M clear, no bracket); the raw
# per-frame shift of the ramp quad Q equals the jitter delta while the
# resolved shift stays a fraction of it. `masked`: fraction 390 (below the
# default threshold 500), the fade bracket composes both quads exactly as the
# live fade oracle predicts (bit-identical composite), M covers them and the
# resolved shift equals the raw one (current-only: the run-49 trembling
# reproduced). `sentinel`: the routed inputs with A scissored to the bottom
# half, both quads over the route's sentinel fill under X3M_TAA_SENTINEL=2
# (far-plane reprojection of the fill, the quads' own RT1 rows): resolved
# shift stable as for `routed`.
# `hover`: g_AlphaValue per frame so the estimate runs 507, 449, 449, 390,
# 449, 449, 507, 449, 390, 507, 449, 449 permille: the hysteresis holds a
# node admitted at >= 500 down to 400 (fade_held), 390 disarms it, 449 then
# stays refused until 507 arms it again; the composite switch step between a
# held (native encoded-space mix) and a refused (bracket, linear source-over)
# frame at 449 is measured at the P samples. The routed script runs in both
# RT binding modes. `original`: the sentinel inputs under original shading
# (X3M_LINEAR_MATERIALS=0, X3M_LINEAR_DISTANCE_FADE=0: no bracket, no
# composition, no M, no linear_material_frame line), the run-125 production
# configuration in which the arm was inert because the cutout capability probe
# only ran with linear materials requested (asteroid-fog-temporal.md, "Run
# 125"), on the hover schedule over the sentinel fill: the DLL's verdict is
# Ready, the arm routes, holds and refuses exactly the hover frames without a
# bracket (a refused frame is the plain native draw over the far-plane path,
# stable under the rotating camera), the routed frames' resolved shift stays a
# fraction of the raw one, and the `fade_route_frame` line carries the counters.
# `behind`: the original script with the quads' clip rows placing the object
# origin behind the camera plane (w -1 at distance 1, the vertices' raster
# unchanged), run 130's leg-2 station module that the arm refused at its
# origin-distance step every frame (asteroid-fog-temporal.md, "Run 130"): the
# hover schedule routes, holds and refuses exactly as `original`, and the
# capture rows attribute every refused fade draw `unmatched=fade_threshold`.
# `overlay`: run 130's distant-object class, the hull (cutout) pair
# 53a0a641107ed76c/63f96eba9eea7880 drawn as the same node's source-over
# sub-mesh (the glass/window layer) right after the node's routed opaque draw
# (P and Q carry A's scope with their own vertex buffers, over A, original
# shading): the overlay arm routes both quads every frame at permille 1000
# (overlay_routed 2, fade_routed 0), RT2 masked (depth target unchanged),
# and the resolved shift of Q stays a fraction of the raw one. `foreign`: the
# overlay script with the quads on their own nodes, the arm's fail-safe: both
# refused every frame (gate 4, `unmatched=overlay_node`, overlay_refused 2).
FADE_ROUTE_FRAMES = 12
FADE_ROUTE_CUT_FRAME = 7
FADE_ROUTE_CAPTURE = (2, 3, 4)
FADE_ROUTE_THRESHOLD = 500
FADE_ROUTE_HOVER_ALPHA = (.8125, .71875, .71875, .625, .71875, .71875, .8125, .71875, .625, .8125, .71875, .71875)  # g_AlphaValue.x per frame (exact in float)
FADE_ROUTE_HOVER_PERMILLE = tuple(int(a * .625 * 1000) for a in FADE_ROUTE_HOVER_ALPHA)  # 507, 449, 390: fraction .625 * alpha at distance 1, truncated
FADE_ROUTE_HOVER_BAND = 400  # threshold - fade_route::Hysteresis::band
FADE_ROUTE_HOVER_SCRIPTS = ('hover', 'original', 'behind')  # the hover schedule
FADE_ROUTE_ORIGINAL_SCRIPTS = ('original', 'behind', 'overlay', 'foreign')  # original shading: no bracket, no composition, no M
FADE_ROUTE_FULL_SCRIPTS = ('routed', 'sentinel', 'overlay')  # routed every frame (fraction 1000; overlay: the same-node rule)
FADE_ROUTE_OVERLAY_PAIR = ('53a0a641107ed76c', '63f96eba9eea7880')  # the hull (cutout) pair the overlay script draws


def fade_route_routed(script, frame):
    """The arm's decision for the script's frame (motion_output_fade_route_inc.h)."""
    if script in FADE_ROUTE_HOVER_SCRIPTS:
        return FADE_ROUTE_HOVER_PERMILLE[frame] >= FADE_ROUTE_THRESHOLD or (fade_route_routed(script, frame - 1) and FADE_ROUTE_HOVER_PERMILLE[frame] >= FADE_ROUTE_HOVER_BAND)
    return script in FADE_ROUTE_FULL_SCRIPTS


def fade_route_permille(script, frame):
    return FADE_ROUTE_HOVER_PERMILLE[frame] if script in FADE_ROUTE_HOVER_SCRIPTS else 1000 if script in FADE_ROUTE_FULL_SCRIPTS else 0 if script == 'foreign' else 390


def fade_route_held(script, frame):
    return fade_route_routed(script, frame) and fade_route_permille(script, frame) < FADE_ROUTE_THRESHOLD
FADE_ROUTE_PAIR = ('b0602757fce6e870', '517540ae6d5e5410')
FADE_ROUTE_QUAD_PIXELS = 14 * 14          # interior pixels of one quad (columns/rows 9..22 or 41..54)
FADE_ROUTE_WINDOW = (42, 54, 10, 22)      # Lucas-Kanade window inside quad Q (x0, x1, y0, y1; one-pixel gradient margin)
FADE_ROUTE_SAMPLES = ((12, 12), (20, 20)) # composite oracle pixels inside quad P
FADE_ROUTE_MIN_SHIFT = 0.2                # px: an axis whose jitter delta is smaller carries no shift evidence
FADE_ROUTE_RAW_TOLERANCE = 0.12           # px: raw shift versus the jitter delta (FP16 raster, linear ramp)
FADE_ROUTE_STABLE_FRACTION = 0.3          # routed: |resolved shift| <= fraction * |raw shift| + noise; masked: |resolved - raw| <= the same
FADE_ROUTE_NOISE = 0.08                   # px: FP16 raster and resolve
FADE_ROUTE_MIN_EVIDENCE = 4               # (frame, axis) samples per image kind
FADE_ROUTE_ENV = dict(CUTOUT_ENV, X3M_LINEAR_DISTANCE_FADE='1', X3M_CAPTURE_START=str(FADE_ROUTE_CAPTURE[0]), X3M_CAPTURE_FRAMES=str(len(FADE_ROUTE_CAPTURE)))
FADE_ROUTE_CASES = (('routed', True), ('routed-perdraw', False), ('masked', True), ('sentinel', True), ('hover', True), ('original', True), ('behind', True), ('overlay', True), ('foreign', True))
# original shading: linear materials and the fade bracket off (the run-125 production configuration).
FADE_ROUTE_ORIGINAL_ENV = dict(X3M_LINEAR_MATERIALS='0', X3M_LINEAR_DISTANCE_FADE='0')
CASES += [case(f'seam-taa-fade-route-{name}', 'faderoute', jitter=True, taa=True, lazy=lazy, hdr=True,
               hdr_env=dict(FADE_ROUTE_ENV, X3M_FIXTURE_FADE_SCRIPT=name.split('-')[0], **(FADE_ROUTE_ORIGINAL_ENV if name in FADE_ROUTE_ORIGINAL_SCRIPTS else {})))
          for name, lazy in FADE_ROUTE_CASES]
# Light-map far fade (motion_output_lightmap_fade_inc.h, --light-map-far-fade;
# docs/architecture/taa-distant-line-fade.md section 11): the original hull
# pair under X3M_HULL_LIGHTMAP_GAIN=4 drawn at origin view depths 1, 128 and 64
# (identical raster) with the fixture camera (P[0] 0.8, width 64: footprint
# w / 25.6). P0,P1 = footprints of w 32 and 96, so near is the configured gain
# exactly, far is G exactly and mid is t = 0.5. '-off' is the same script with
# the option unset: every F4-on image is the near image bit for bit and the
# route's c217.w upload stays 0 (today's records). The near and base images of
# each fade case must equal the off case's bit for bit (checked when both ran).
LIGHTMAP_FADE_GAIN = 4.0
LIGHTMAP_FADE_P = (2 * 32 / (0.8 * 64), 2 * 96 / (0.8 * 64))  # 1.25, 3.75 units/px
LIGHTMAP_FADE_ENV = dict(X3M_HULL_LIGHTMAP_GAIN=repr(LIGHTMAP_FADE_GAIN), X3M_LINEAR_MATERIALS='0', X3M_HDR_CLAMP='0', X3M_HDR_BLOOM='0', X3M_MOTION_FRAME_LOG='1')
LIGHTMAP_FADE_CASES = {'seam-lightmap-far-fade-off': None, 'seam-lightmap-far-fade-on': 1.0, 'seam-lightmap-far-fade-floor2': 2.0,
                       'seam-ownership-lightmap-far-fade-on': 1.0}
CASES += [case(name, 'lightmapfade', 'ownership' if 'ownership' in name else 'plain', camera=True, hdr=True,
               hdr_env=dict(LIGHTMAP_FADE_ENV, **({} if floor is None else {'X3M_LIGHT_MAP_FAR_FADE': '%g,%g,%g' % (*LIGHTMAP_FADE_P, floor)})))
          for name, floor in LIGHTMAP_FADE_CASES.items()]
# The overlay script (the hull cutout pair and its fade-band/overlay arm, original shading, TAA) under the
# light-map gain, without and with the far fade at a near footprint: the arm never binds a gained variant and
# the routed hull parent gets the full gain, so every FADE_ROUTE line of the twins is identical.
LIGHTMAP_FADE_OVERLAY_CASES = {'seam-taa-fade-route-overlay-lightmap': {}, 'seam-taa-fade-route-overlay-lightmap-far-fade': {'X3M_LIGHT_MAP_FAR_FADE': '1.25,3.75,1'}}
CASES += [case(name, 'faderoute', jitter=True, taa=True, lazy=True, hdr=True,
               hdr_env=dict(FADE_ROUTE_ENV, X3M_FIXTURE_FADE_SCRIPT='overlay', X3M_HULL_LIGHTMAP_GAIN=repr(LIGHTMAP_FADE_GAIN), **FADE_ROUTE_ORIGINAL_ENV, **extra))
          for name, extra in LIGHTMAP_FADE_OVERLAY_CASES.items()]
LIGHTMAP_FADE_STEPS = (('near', 1, True), ('far', 128, True), ('mid', 64, True), ('near_off', 1, False), ('far_off', 128, False),
                       ('mid_on', 64, True), ('mid_reset', 64, True), ('near_reset', 1, True))


def write_lightmap_fade_record(result):
    """The tracked compact record of a selected light-map far fade run (docs/verification/motion-output.md);
    capture logs stay local beside it (motion-output-<case>-capture.log) and in each case's build directory."""
    names = [n for n in list(LIGHTMAP_FADE_CASES) + list(LIGHTMAP_FADE_OVERLAY_CASES) + ['seam-hdr-on'] if n in result['cases']]
    if not any(n in result['cases'] for n in LIGHTMAP_FADE_CASES):
        return
    cases = {}
    for n in names:
        c = result['cases'][n]
        row = dict(exit=c.get('exit'), checks=c.get('checks'), dll_sha256=c.get('dll_sha256'), exe_sha256=c.get('exe_sha256'),
                   trace_sha256=c.get('trace_sha256'), directory=c.get('directory'), capture_log=f'motion-output-{n}-capture.log (untracked)')
        if 'steps' in c:
            row.update(floor=c['floor'], near_matches_off=c.get('near_matches_off'), steps=c['steps'])
        for key in ('lightmap', 'matches_constant_gain_twin'):
            if key in c: row[key] = c[key]
        cases[n] = row
    (RESULTS / 'lightmap-far-fade-seam.json').write_text(json.dumps(dict(
        bottle=result['bottle'], binaries=result.get('binaries'), selected=sorted(result['selected_cases']), cases=cases), indent=1) + '\n')


def validate_lightmap_fade(name, floor, text, trace):
    lines = text.splitlines()
    assert 'RESET PASS' in lines, name
    rows = [fields(l) for l in lines if l.startswith('LIGHTMAP_FADE ')]
    images = [fields(l) for l in lines if l.startswith('LIGHTMAP_FADE_IMAGE ')]
    assert not any(l.startswith('LIGHTMAP_FADE_DIFF ') for l in lines), name
    assert [(r['kind'], float(r['w']), r['on'] == '1') for r in rows] == list(LIGHTMAP_FADE_STEPS), (name, rows)
    assert len(images) == len(rows) == 8, name
    checks = 0
    gains = {}
    for row, image in zip(rows, images):
        kind, w, on = row['kind'], float(row['w']), row['on'] == '1'
        t = 0.0 if floor is None else min(1.0, max(0.0, (w / 25.6 - LIGHTMAP_FADE_P[0]) / (LIGHTMAP_FADE_P[1] - LIGHTMAP_FADE_P[0])))
        law = LIGHTMAP_FADE_GAIN + ((floor if floor is not None else 1.0) - LIGHTMAP_FADE_GAIN) * t
        upload = 0.0 if floor is None else law
        assert abs(float(row['abi_gain']) - upload) <= 1e-5 * max(1.0, upload), (name, kind, row['abi_gain'], upload)
        if t in (0.0, 1.0):
            assert float(row['abi_gain']) == upload, (name, kind, 'the ends are exact')
        assert int(row['gained_draws']) == int(on) and int(row['fade']) == int(floor is not None), (name, row)
        gain = law if on else 1.0
        assert abs(float(image['gain']) - gain) <= 1e-6 and int(image['mismatches']) == 0 and int(image['pixels']) >= 100, (name, image)
        assert int(image['lit']) == 3 * int(image['pixels']), (name, image)
        gains[kind] = dict(gain=float(image['gain']), abi_gain=float(row['abi_gain']), max_codes=int(image['max_codes']), hdr_hash=row['hdr_hash'])
        checks += 6
    hashes = {k: v['hdr_hash'] for k, v in gains.items()}
    # Near is the configured gain, the F4-off steps are the base, Reset and the toggle change nothing else.
    assert hashes['near'] == hashes['near_reset'] and hashes['mid'] == hashes['mid_on'] == hashes['mid_reset'], (name, hashes)
    assert hashes['near_off'] == hashes['far_off'] and hashes['near'] != hashes['near_off'], (name, hashes)
    if floor is None:
        assert hashes['far'] == hashes['mid'] == hashes['near'], (name, 'option off: the constant gain at every distance', hashes)
        assert 'hull_lightmap_far_fade_frame' not in trace and 'light_map_far_fade_mode' not in trace, name
    else:
        assert (hashes['far'] == hashes['near_off']) == (floor == 1.0), (name, 'G = 1 is the base image bit for bit', hashes)
        assert len({hashes['near'], hashes['mid'], hashes['far']}) == 3, (name, hashes)
        assert 'light_map_far_fade_configured accepted=1' in trace, name
        mode = [fields(l) for l in trace.splitlines() if l.startswith('light_map_far_fade_mode ')]
        assert len(mode) == 1 and mode[0]['enabled'] == '1' and float(mode[0]['floor']) == floor, (name, mode)
        frames = [fields(l) for l in trace.splitlines() if l.startswith('hull_lightmap_far_fade_frame ')]
        # One gain draw per F4-on frame; faded on the far and mid frames, camera latched.
        assert len(frames) == 6 and all(f['admitted'] == '1' and f['camera'] == '1' for f in frames), (name, frames)
        assert [f['faded'] for f in frames] == ['0', '1', '1', '1', '1', '0'], (name, frames)
        assert float(frames[1]['min_gain']) == floor, (name, frames)
    variants = [fields(l) for l in trace.splitlines() if l.startswith('hull_lightmap_variant ')]
    assert any(v['original'] == '7c83ed50c9894e44' and v['gain_applied'] == '1' and v['create'] == '00000000' for v in variants), (name, variants)
    return dict(checks=checks + 8, floor=floor, steps=gains)


# Mip-bias script (motion_output_fixture.cpp run_mipbias): eight frames,
# capture in frame 5 only (the capture diagnostics restore the bias before
# every draw, so that frame re-sets it per routed draw), the frame line every
# frame, a Reset after frame 3. Even frames add the evidence pair (a routed
# and an unrouted material draw of A read back separately). Per-frame
# SetSamplerState counts of the DLL for the -0.5 run, derived by hand (the
# fixture's own model of the restore points must agree, frame by frame):
# odd frames 9 sets / 8 restores, even frames 11 / 10, the capture frame
# (odd) 15 / 14. Route counters: the background draw stops at gate 2, the two
# flat draws at gate 3, the evidence's blended draw at gate 4, every routed
# draw is sentinel-only (gate 5: no scope in this script).
MIPBIAS_FRAMES = 8
MIPBIAS_CAPTURE = (5,)
# An application write over a held stage bias first restores that stage.
MIPBIAS_ANCHORS = {'odd': (9, 9), 'even': (11, 11), 'capture': (15, 15)}
MIPBIAS_EXPECT = {'odd': dict(draws=12, routed=9, matched=0, gate2=1, gate3=2, gate4=0, gate5=9, gate6=0, apply_failures=0, restore_failures=0),
                  'even': dict(draws=14, routed=10, matched=0, gate2=1, gate3=2, gate4=1, gate5=10, gate6=0, apply_failures=0, restore_failures=0)}
MIPBIAS_STAGES = '0011'  # stages 0 and 4 carry the bias (hex mask of the frame line)
MIPBIAS_GAME_WRITES = 2  # application MIPMAPLODBIAS writes per frame (0.25, then 0)
# hdr_frame expectations: end point per script (HdrEnd names), write-backs
# per frame (the EndScene flush, the bloom-copy or hook end; the fixture's
# GetRenderTargetData before the TAA boundary flushes first).
HDR_VALUES_FRAMES = 6  # frames 1 and 4 draw B with the in-range (.75, .25, .375, .625); a Reset while redirected precedes frame 5
HDR_FAULT_SCRIPT = {0: dict(fault=0, redirected=1, unwind=0, source='shader', recheck='none', blocked=0, ldr=0),
                    1: dict(fault=4, redirected=1, unwind=1, source='stretch', reason='draw', recheck='none', blocked=0, ldr=0),
                    2: dict(fault=7, redirected=1, unwind=1, source='shader', reason='restore', recheck='pass', blocked=0, ldr=0),
                    3: dict(fault=6, redirected=1, unwind=1, source='restore', reason='stretch', recheck='pass', blocked=0, ldr=0),
                    4: dict(fault=5, redirected=1, unwind=1, source='restore', reason='lost', recheck='pass', blocked=0, ldr=0),
                    5: dict(fault=0, redirected=1, unwind=0, source='shader', recheck='pass', blocked=0, ldr=0),
                    6: dict(fault=8, redirected=0, unwind=0, source='none', recheck='none', blocked=0, ldr=1, target_create='8007000e'),
                    7: dict(fault=0, redirected=0, unwind=0, source='none', recheck='none', blocked=0, ldr=1),
                    8: dict(fault=0, redirected=1, unwind=0, source='shader', recheck='none', blocked=0, ldr=0),
                    9: dict(fault=9, redirected=0, unwind=0, source='none', recheck='none', blocked=0, ldr=1, latch_bind='80004005'),
                    10: dict(fault=0, redirected=1, unwind=0, source='shader', recheck='none', blocked=0, ldr=0),
                    11: dict(fault=4, redirected=1, unwind=1, source='stretch', reason='draw', recheck='none', blocked=0, ldr=0),
                    12: dict(fault=3, redirected=0, unwind=0, source='none', recheck='fail', blocked=1, ldr=1),
                    13: dict(fault=0, redirected=1, unwind=0, source='shader', recheck='none', blocked=0, ldr=0),
                    14: dict(fault=10, redirected=1, unwind=0, source='none', recheck='none', blocked=0, ldr=0, end='clear_failed', writebacks='0')}
# Render-state shadow: native GetRenderState calls the route issues per frame
# besides shadow misses (the sentinel fill's touched-state save), the number
# of shadowed states (the most misses one resynchronization can cause) and the
# regular script's resynchronizations per frame (frame 7: two state block
# Applies; frame 8: EndStateBlock).
RS_FILL_GETS = 14


# The shadow switch per case: True (X3M_STATE_SHADOW=1, hooks and shadow on),
# False (=0: hooks off, `rs_mode=get`) or 'auto' (unset, the production
# configuration: hooks off, `rs_mode=get`). Lazy RT mode no longer keeps the
# hooks (lever 3); the `lazy` argument stays for the callers.
def shadow_request(shadow):
    """The motion_output_mode line's state_shadow (the request as parsed)."""
    return 'auto' if shadow == 'auto' else str(int(shadow))


def shadow_effective(shadow, lazy):
    """The device and per-frame lines' state_shadow (the cross-draw shadow is on)."""
    return '1' if shadow is True else '0'


def shadow_mode(shadow, lazy):
    """The per-frame line's rs_mode."""
    return 'shadow' if shadow is True else 'get'
RS_SHADOW_STATES = 32  # eight gate/mask states, WRAP0..15, eight cutout states; invalidated by resync
SEAM_RESYNCS = {7: 2, 8: 1}
# Hook script: seven frames (glow on, outside-Scene signal, glow off) and the
# per-frame expectations with the patch installed / unpatched.
HOOK_FRAMES = 7
HOOK_SCRIPT = {0: dict(glow=1, outside=0), 1: dict(glow=1, outside=0), 2: dict(glow=1, outside=1), 3: dict(glow=1, outside=0),
               4: dict(glow=0, outside=0), 5: dict(glow=0, outside=0), 6: dict(glow=1, outside=0)}
HOOK_SOURCE = {True: {0: 'hook', 1: 'hook', 2: 'stretchrect', 3: 'hook', 4: 'hook', 5: 'hook', 6: 'hook'},
               False: {0: 'stretchrect', 1: 'stretchrect', 2: 'stretchrect', 3: 'stretchrect', 4: 'none', 5: 'none', 6: 'stretchrect'}}
HOOK_HISTORY = {True: {1, 2, 3, 4, 5, 6}, False: {1, 2, 3}}
HOOK_SKIPPED = {True: set(), False: {4, 5}}
# SceneEndCheck (motion_output.h): 1 Agree, 2 HookOnly, 3 StretchOnly, 4 Disagree.
HOOK_CHECK = {True: {0: '1', 1: '1', 2: '4', 3: '1', 4: '2', 5: '2', 6: '1'}, False: {0: '3', 1: '3', 2: '3', 3: '3', 4: '0', 5: '0', 6: '3'}}
HOOK_CHECKS = {True: 141, False: 126}  # one presented-image check per frame included
# Frames whose keyed draws are matched but whose camera turned 31 degrees: a
# camera cut (auto and strict), so no history; frame 7 of the camera script.
CAMERA_CUT_FRAMES = {7}
CAMERA_M00, CAMERA_M11 = 0.8, 4 / 3
ENVMAP_FRAMES = 5
ENVMAP_CAPTURE = (1, 2, 3, 4)
# Burst script: nine frames, capture in frames 7-8 only (capture diagnostics
# restore the lazy binding before every draw), the frame line every frame.
BURST_FRAMES = 9
BURST_CAPTURE = (7, 8)
BURST_EXPECT = dict(draws=9, routed=5, matched=0, gate2=2, gate3=1, gate4=1, gate5=5, gate6=0, depth_routed=5)
BURST_SET_RT = {'perdraw': 20, 'lazy': 12}   # 5 routed draws x 4 versus 3 bind/flush pairs x 4
BURST_FLUSHES = {'perdraw': 0, 'lazy': 3}
# Seam script: frames whose keyed draws miss (cut) and frames the resolve blends
# with history (a previous resolved frame since Reset and no cut).
SEAM_TAA_CUTS = {0, 3, 5, 6, 8, 9}
SEAM_TAA_HISTORY = {1, 2, 4, 7, 10, 11}
JITTER_SAMPLES = 8
# Application depth clears per fixture frame with the original depth bound
# (color+depth Clear, depth-only Clear): the wrapper's source_epoch must count
# exactly these, so the route's own depth unbind inside the fill never reached one.
DEPTH_CLEARS_PER_FRAME = 2
# Expected per-frame route counters for captured frames 1..8 of the seam-on
# script (see motion_output_fixture.cpp run()). gate2 counts the background draw.
SEAM_FRAMES = {
    1: dict(draws=3, routed=2, matched=2, gate2=1, gate3=0, gate4=0, gate5=0, gate6=0),
    2: dict(draws=3, routed=2, matched=0, gate2=1, gate3=0, gate4=0, gate5=2, gate6=0),
    3: dict(draws=3, routed=2, matched=0, gate2=1, gate3=0, gate4=0, gate5=0, gate6=2),
    4: dict(draws=5, routed=2, matched=2, gate2=1, gate3=1, gate4=1, gate5=0, gate6=0),
    5: dict(draws=4, routed=3, matched=2, gate2=1, gate3=0, gate4=0, gate5=0, gate6=1),
    6: dict(draws=3, routed=2, matched=1, gate2=1, gate3=0, gate4=0, gate5=0, gate6=1),
    7: dict(draws=3, routed=1, matched=1, gate2=1, gate3=1, gate4=0, gate5=0, gate6=0),
    8: dict(draws=3, routed=2, matched=1, gate2=1, gate3=0, gate4=0, gate5=0, gate6=1)}
SENTINEL = (0.0, 0.0, 0.0, -1.0)
# Cut detector verdict per seam frame: the missing-key fraction exceeds 0.25
# wherever a keyed draw found no previous entry (gate 6); the matched draws'
# origins move 0.05 NDC = 1.6 px, below the 48 px * 64/1280 = 2.4 px bound.
SEAM_CUTS = {1: 0, 2: 0, 3: 1, 4: 0, 5: 1, 6: 1, 7: 0, 8: 1}
ORIGIN_DISPLACEMENT_PX = 1.6


def halton(index, base):
    fraction, result = 1.0, 0.0
    while index:
        fraction /= base
        result += fraction * (index % base)
        index //= base
    return result


def expected_jitter(frame):
    index = frame % JITTER_SAMPLES
    return index, halton(index + 1, 2) - 0.5, halton(index + 1, 3) - 0.5


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fields(line):
    return dict(re.findall(r'(\w+)=([^\s]+)', line))


def sources():
    paths = [p for folder in ('src/proxy', 'src/renderer', 'src/ownership', 'src/temporal', 'cmake')
             for p in (ROOT / folder).glob('*') if p.suffix in ('.cpp', '.h', '.hlsl', '.def', '.cmake')]
    paths += [ROOT / 'CMakeLists.txt', PROBE / 'motion_output_fixture.cpp', PROBE / 'build_motion_output.sh',
              PROBE / 'run_motion_output.py', PROBE / 'abi_check.cpp']
    # Bind the numerical oracles and the complete local import closure of the
    # admission validator, plus the process-serialization entrypoint. These
    # are execution inputs, even though they do not compile into the DLL.
    paths += [ROOT / 'tools' / 'analysis' / name for name in (
        'exposure_reference.py', 'agx_reference.py', 'analyze_motion_readback.py', 'summarize_capture.py')]
    paths += [PROBE / name for name in (
        'verify_ownership_integration.py', 'run_ownership_integration.py', 'verify_capture_state.py',
        'bottle.py', 'game_guard.py', 'wine_lock.py', 'shadow_replay_depth.py', 'sun_shadow_apply.py', 'motion_output_sun_apply_inc.h', 'motion_output_sun_apply_cascades_inc.h', 'motion_output_lightmap_fade_inc.h',
        'motion_output_shadow_retention_inc.h', 'motion_output_shadow_pool_inc.h')]
    paths += [ROOT / 'tools' / 'analysis' / 'shadow_retention.py']
    return {str(p.relative_to(ROOT)): sha(p) for p in sorted(paths)}


def no_game():
    assert not game_running(), 'X3AP running; no synthetic GPU run'


def read_motion(path, width=64, height=64):
    data = path.read_bytes()
    assert len(data) == width * height * 16, f'{path}: unexpected size {len(data)}'
    return [struct.unpack_from('<4f', data, i * 16) for i in range(width * height)]


def read_depth(path, width=64, height=64):
    data = path.read_bytes()
    assert len(data) == width * height * 4, f'{path}: unexpected size {len(data)}'
    return list(struct.unpack('<%df' % (width * height), data))


def validate_shadow_replay_candidates(name, trace, expected_witnesses=0):
    """Caster-candidate counter lines (docs/architecture/shadow-replay-gates.md,
    "Implemented"): the parser refuses a malformed line or a broken sum identity;
    one line per frame with routed draws, its routed count equal to the frame
    line's; the fixture's routed draws are z-writing MANAGED triangles at unit
    distance (X3M_FIXTURE_SLICE_NEAR), so routed == zwrite == slice0 == managed
    == leased == quiet, no other bucket, no lock between draw and scene end
    (no witness) and never more than 16 witness lines per device."""
    frames, witnesses = candidates_analysis.parse_text(trace)
    candidates_analysis.check_frame_uniqueness(frames)
    caps = candidates_analysis.check_witness_cap(witnesses)
    # The frame line comes at the capture frames and every X3M_MOTION_FRAME_LOG-th
    # frame; the counter line comes at every scene end, so the comparison is
    # over the frames that have both, and a frame line without routed draws
    # must not have a counter line claiming any.
    summary_frames = {int(fields(l)['frame']): fields(l) for l in trace.splitlines() if l.startswith('motion_output_frame ')}
    routed_frames = {f: int(r['routed']) for f, r in summary_frames.items() if int(r['routed']) > 0}
    by_frame = {r['frame']: r for r in frames}
    assert routed_frames and set(routed_frames) <= set(by_frame), (name, sorted(set(routed_frames) - set(by_frame))[:8])
    assert all(by_frame[f]['routed'] == 0 for f in summary_frames if f not in routed_frames and f in by_frame), name
    assert len(frames) >= len(routed_frames) and all(r['routed'] > 0 for r in frames), (name, len(frames))
    checks = 0
    for f, routed in routed_frames.items():
        r = by_frame[f]
        assert r['routed'] == routed == r['zwrite'] == r['slice0'] == r['managed'] == r['leased'] == r['quiet'], (name, f, r)
        assert r['dynamic'] == r['default_pool'] == r['excluded'] == r['unknown'] == r['shadow_mismatch'] == 0, (name, f, r)
        assert r['serial_changed'] == r['readonly_after'] == r['writable_after'] == r['pending'] == r['in_flight'] == r['cold_thread'] == r['stale'] == 0, (name, f, r)
        assert r['waiting'] == r['nested'] == r['overflow'] == 0, (name, f, r)
        checks += 4
    assert sum(caps.values()) == expected_witnesses and all(n <= candidates_analysis.WITNESS_CAP for n in caps.values()), (name, caps)
    mode = [l for l in trace.splitlines() if l.startswith('shadow_replay_candidates_mode ')]
    assert mode == ['shadow_replay_candidates_mode requested=1 enabled=1 motion_output=1 ownership=1'], (name, mode)
    summary = candidates_analysis.summarize(frames, witnesses)
    predicates = summary['predicates']
    assert predicates['lease_contract'] and predicates['single_thread'] and predicates['promotion_possible'], (name, summary)
    return dict(frames=len(frames), witnesses=caps, checks=checks + 3, summary=summary)


def validate_sun_apply(name, text, directory, env):
    """The apply-quad script: the fixture's own checks passed (skip paths,
    restoration, far-map identity, Reset identity), every frame's readback
    against the CPU twin of the program on the same RT2 and map within one
    FP16 code (ambiguous rounding calls excluded and counted), frame 1 with
    f = 0 on every valid pixel, the hard shadow against the analytic box
    shadow with every disagreement within the kernel's reach of an edge, and
    the quad's EVENT-synchronized time."""
    assert 'RESULT PASS' in text, f'{name}: fixture failed'
    result_line = next(l for l in text.splitlines() if l.startswith('RESULT PASS'))
    checks = int(fields(result_line)['checks'])
    device = [fields(l) for l in text.splitlines() if l.startswith('SUNAPPLY_DEVICE ')]
    assert len(device) == 1 and device[0]['attached'] == '1' and device[0]['references'] == '3', (name, device)
    # The configuration line: the default run keeps the literal fixture bias
    # (.003 / .01, byte-identical output); the wide run resolves the world-unit
    # bias exactly as the twin does from the production variables.
    config = [fields(l) for l in text.splitlines() if l.startswith('SUNAPPLY_CONFIG ')]
    assert len(config) == 1, (name, config)
    config = config[0]
    wide = env.get('X3M_FIXTURE_SUNAPPLY_WIDE') == '1'
    assert config['wide'] == ('1' if wide else '0'), (name, config)
    if wide:
        extent, depth_half, size_env = float(env['X3M_SHADOW_REPLAY_EXTENT']), float(env['X3M_SHADOW_REPLAY_DEPTH_HALF']), int(env['X3M_SHADOW_REPLAY_SIZE'])
        bias_units = float(env.get('X3M_SUN_SHADOW_BIAS_UNITS', sun_apply.BIAS_UNITS_DEFAULT))
        clamp_texels = float(env.get('X3M_SUN_SHADOW_BIAS_CLAMP_TEXELS', sun_apply.BIAS_CLAMP_TEXELS))
        resolved = sun_apply.resolve_bias(bias_units, extent, depth_half, size_env, clamp_texels)
        assert (float(config['extent']), float(config['depth_half']), int(config['map_size']), float(config['scale'])) == (extent, depth_half, size_env, extent / 5.0), (name, config)
        assert (float(config['bias_units']), float(config['clamp_texels'])) == (bias_units, clamp_texels), (name, config)
        assert abs(float(config['bias_constant']) - resolved['bias_constant']) <= 1e-9 and abs(float(config['bias_max']) - resolved['bias_max']) <= 1e-8, (name, config, resolved)
        assert abs(float(config['texel_world']) - 2.0 * extent / size_env) <= 1e-6, (name, config)
    else:
        assert abs(float(config['bias_constant']) - .003) < 1e-9 and abs(float(config['bias_max']) - .01) < 1e-9 and (float(config['scale']), int(config['map_size'])) == (1.0, 256), (name, config)  # the float32 literals
    frames = {int(fields(l)['frame']): fields(l) for l in text.splitlines() if l.startswith('SUNAPPLY ')}
    times = {int(fields(l)['frame']): fields(l) for l in text.splitlines() if l.startswith('SUNAPPLY_TIME ')}
    assert sorted(frames) == list(range(SUN_APPLY_FRAMES)) and sorted(times) == sorted(frames), (name, sorted(frames), sorted(times))
    assert all(t['applied'] == '1' and t['result'] == '00000000' and float(t['us']) > 0 for t in times.values()), (name, times)
    width, height, size = (int(frames[0][k]) for k in ('width', 'height', 'map_size'))
    comparisons, images = {}, {}
    for frame in range(SUN_APPLY_FRAMES):
        f = frames[frame]
        read = lambda suffix, count: (directory / f'sunapply_{frame}_{suffix}').read_bytes()
        before, after = read('before.rgba16f', 8), read('after.rgba16f', 8)
        params, scene = sun_apply.parse_params(f)
        encoding, lanes = params['depth_encoding'], rt2_lanes(f, env)
        rt2, sun_map = read(rt2_file(encoding), lanes * 4), read('map.r32f', 4)
        assert len(before) == len(after) == width * height * 8 and len(rt2) == width * height * lanes * 4 and len(sun_map) == size * size * 4, (name, frame)
        images[frame] = (before, after)
        assert (params['bias_constant'], params['bias_max']) == (float(config['bias_constant']), float(config['bias_max'])), (name, frame, params, config)
        d, s = sun_apply.unpack_rt2(rt2, width, height, encoding)
        # The wide run holds the plane receivers (the box-on-plane shadow) to
        # the two-texel edge rule; the box faces at their camera silhouette are
        # non-planar quads whose fallback bias (the 21-texel clamp) can light a
        # few pixels: reported and bounded.
        comparison = sun_apply.compare_frame(sun_apply.unpack_rgba16f(before, width, height), sun_apply.unpack_rgba16f(after, width, height),
                                             d, s, sun_apply.unpack_map(sun_map, size), params, scene if f['map_mode'] == '2' else None, strict_box=not wide)
        comparison.update(map_mode=int(f['map_mode']), elevation=float(f['elevation']), jitter_index=int(f['jitter_index']), exponent=float(f['exponent']),
                          us=float(times[frame]['us']), receivers=int(f['receivers']), share_free=int(f['share_free']), sentinels=int(f['sentinels']),
                          raster_m20=scene['raster_m20'], raster_m21=scene['raster_m21'], legacy_latch=scene['legacy_latch'])
        # The latch is the raster's jitter plus the pixel-centre term (quad_pixel_centre_m20/m21) unless the legacy witness dropped it.
        centre = sun_apply.pixel_centre_terms(width, height)
        assert abs(params['m20'] - scene['raster_m20'] - (0.0 if scene['legacy_latch'] else centre[0])) < 1e-7 and abs(params['m21'] - scene['raster_m21'] - (0.0 if scene['legacy_latch'] else centre[1])) < 1e-7, (name, frame, params, scene)
        assert comparison['ok'], (name, frame, comparison)
        assert comparison['compared'] > 0 and comparison['ambiguous'] < comparison['valid'] // 10, (name, frame, comparison)
        if frame == SUN_APPLY_FAR_FRAME:
            assert after == before and comparison['shadowed'] == 0, (name, frame, comparison)
        if frame == SUN_APPLY_ZERO_FRAME:
            # Every valid pixel is fully shadowed: the readback is C (1 - s) within one code.
            expected = sun_apply.expected_factor(d, s, sun_apply.unpack_map(sun_map, size), params)
            import numpy as np
            assert not np.any(expected['valid'] & (expected['f'] > 0.0)), (name, frame, int(np.count_nonzero(expected['valid'] & (expected['f'] > 0.0))))
            assert comparison['shadowed'] == comparison['compared'], (name, frame, comparison)
        if f['map_mode'] == '2':
            assert comparison['shadowed'] > 100 and comparison['penumbra'] > 0 and comparison['analytic']['shadowed_analytic'] > 100, (name, frame, comparison)
            assert comparison['analytic']['plane_beyond_two_texels'] == 0 and comparison['analytic']['box_beyond_two_texels'] <= (16 if wide else 0), (name, frame, comparison)
        comparisons[frame] = comparison
    assert images[SUN_APPLY_RESET_FRAME][1] == images[SUN_APPLY_RESET_FRAME - 1][1] and images[SUN_APPLY_RESET_FRAME][0] == images[SUN_APPLY_RESET_FRAME - 1][0], f'{name}: the frame after the Reset differs'
    us = sorted(c['us'] for c in comparisons.values())
    validator_checks = 7 + 6 * SUN_APPLY_FRAMES  # device line, configuration, frame sets, timing, Reset identity, far identity, zero-map law; per frame: sizes, bias, latch law, twin, ambiguity bound, scene predicates
    edge_texels = {'texel_world': float(config['texel_world']) if wide else 2.0 * 5.0 / 256, 'within_one_texel_fraction': 0.0}
    mismatches = sum(c['analytic']['mismatch'] for c in comparisons.values() if 'analytic' in c)
    if mismatches:
        edge_texels['within_one_texel_fraction'] = sum(c['analytic']['within_one_texel'] for c in comparisons.values() if 'analytic' in c) / mismatches
    return {'checks': checks + validator_checks, 'fixture_checks': checks, 'validator_checks': validator_checks, 'frames': SUN_APPLY_FRAMES, 'width': width, 'height': height, 'map_size': size,
            'legacy_latch': any(c['legacy_latch'] for c in comparisons.values()), 'wide': wide, 'config': {k: config[k] for k in ('extent', 'depth_half', 'map_size', 'scale', 'bias_units', 'clamp_texels', 'texel_world', 'bias_constant', 'bias_max')}, 'edge_texels': edge_texels,
            'program_slots': int(device[0]['slots']), 'frames_detail': comparisons,
            'worst_codes': max(c['worst_codes'] for c in comparisons.values()), 'ambiguous_max': max(c['ambiguous'] for c in comparisons.values()),
            'edge_mismatch': {k: sum(c['analytic'][k] for c in comparisons.values() if 'analytic' in c)
                              for k in ('mismatch', 'within_one_texel', 'within_two_texels', 'beyond_two_texels', 'plane_mismatch', 'plane_within_one_texel', 'plane_beyond_two_texels', 'box_beyond_two_texels')},
            'us': {'min': us[0], 'median': us[len(us) // 2], 'max': us[-1]}}


def validate_sun_apply_cascades(name, text, directory, env):
    """The cascade script (shadow-cascades.md, section 4): the fixture's own
    checks passed (the production set and policy, halving, refusals, state
    restoration, retention across the budgeted frames, the Reset, the pancaked
    map); per frame the counters (c<i>, draws<i>, far_replayed, far_frame, the
    valid flags) against the script, the readback against the cascade twin on
    the fixture's own map readbacks within one FP16 code, the f >= 0.5 edge of
    every plane receiver within one texel of its owning cascade, solid
    interiors through the blend band, and the per-case predicates."""
    import numpy as np
    assert 'RESULT PASS' in text, f'{name}: fixture failed'
    checks = int(fields(next(l for l in text.splitlines() if l.startswith('RESULT PASS')))['checks'])
    device = [fields(l) for l in text.splitlines() if l.startswith('SUNAPPLY_DEVICE ')]
    assert len(device) == 1 and device[0]['attached'] == '1' and device[0]['references'] == '4' and int(device[0]['cascade_slots']) > int(device[0]['slots']), (name, device)
    replay_device = [fields(l) for l in text.splitlines() if l.startswith('SUNAPPLY_REPLAY_DEVICE ')]
    assert len(replay_device) == 1 and replay_device[0]['maps'] == '3' and replay_device[0]['readable'] == '1' and replay_device[0]['halved'] == '0', (name, replay_device)
    config = [fields(l) for l in text.splitlines() if l.startswith('SUNAPPLY_CONFIG ')]
    assert len(config) == 1 and config[0]['cascades'] == '3' and config[0]['extents'] == '250,1500,7500' and config[0]['depth_light'] == '15000' and config[0]['depth_behind'] == '512,3000,15000' \
        and config[0]['caps'] == '128,512,1024' and abs(float(config[0]['margin']) - sun_apply.CASCADE_MARGIN) < 1e-6 and abs(float(config[0]['band']) - sun_apply.CASCADE_BAND) < 1e-6, (name, config)  # the float32 constants
    config = config[0]
    size, count = int(config['map_size']), 3
    assert float(config['slope_texels']) == float(env.get('X3M_SUN_SHADOW_BIAS_SLOPE_TEXELS', '0')), (name, 'the slope margin the fixture ran', config['slope_texels'])  # the -slope siblings: 0.2
    frames = {int(fields(l)['frame']): fields(l) for l in text.splitlines() if l.startswith('SUNAPPLY_CASCADES ')}
    times = {int(fields(l)['frame']): fields(l) for l in text.splitlines() if l.startswith('SUNAPPLY_TIME ')}
    assert sorted(frames) == sorted(times) == list(range(len(SUN_CASCADE_SCRIPT))), (name, sorted(frames), sorted(times))
    bench = [fields(l) for l in text.splitlines() if l.startswith('SUNAPPLY_BOUNDS_BENCH ')]
    assert len(bench) == 1 and float(bench[0]['verdict_ns']) > 0 and float(bench[0]['mask_ns']) > 0, (name, bench)
    comparisons, images = {}, {}
    for frame, letter in enumerate(SUN_CASCADE_SCRIPT):
        f, t = frames[frame], times[frame]
        assert f['case'] == letter and t['applied'] == '1' and t['result'] == '00000000' and float(t['us']) > 0 and float(t['replay_us']) > 0, (name, frame, f['case'], t)
        width, height = int(f['width']), int(f['height'])
        params, scene, extents, map_frames = sun_apply.parse_cascade_params(f)
        # The counters, per frame: every record's mask, the issues, what each
        # cascade drew, the far cascade's budgeted frames and what it retains.
        masks = [int(v) for v in f['masks'].split(',')][:min(3, len(scene['boxes']) + 1)] + ([int(f['mask3'])] if 'mask3' in f else [])  # the plane and the boxes (case h draws three)
        records = [sum(1 for m in masks if m >> c & 1) for c in range(count)]
        far_skipped = frame in SUN_CASCADE_FAR_SKIPPED
        assert [int(f[f'c{c}']) for c in range(count)] == records and int(f['issues']) == sum(records), (name, frame, records, f['issues'])
        assert [int(f[f'draws{c}']) for c in range(count)] == [0 if far_skipped and c == count - 1 else records[c] for c in range(count)], (name, frame)
        assert int(t['issues']) == sum(records) - (records[-1] if far_skipped else 0), (name, frame, t)
        assert (f['far_replayed'] == '1') == (not far_skipped), (name, frame, f['far_replayed'])
        expected_far = -1 if frame == SUN_CASCADE_RESET_FRAME else frame - 1 if far_skipped else frame
        assert int(f['far_frame']) == expected_far and map_frames[:2] == [frame, frame] and map_frames[2] == expected_far, (name, frame, f['far_frame'], map_frames)
        assert [c['valid'] for c in params['cascades']] == [True, True, frame != SUN_CASCADE_RESET_FRAME] and int(t['bound']) == (2 if frame == SUN_CASCADE_RESET_FRAME else 3), (name, frame)
        assert (int(f['budget']) < int(f['issues'])) == (letter in 'de'), (name, frame, f['budget'], f['issues'])
        for c in range(count):  # the bias each cascade resolved is the law's at its own texel and depth range
            resolved = sun_apply.resolve_bias(float(config['bias_units']), extents[c], .5 * (float(config['depth_light']) + float(config['depth_behind'].split(',')[c])), size, float(config['clamp_texels']))
            assert abs(float(params['cascades'][c]['slope_texels']) - float(config['slope_texels'])) <= 1e-7 * max(1.0, float(config['slope_texels'])), (name, frame, c, 'the slope margin on this cascade (the float32 the pass received)', params['cascades'][c]['slope_texels'], config['slope_texels'])
            assert abs(params['cascades'][c]['bias_constant'] - resolved['bias_constant']) <= 1e-9 and abs(params['cascades'][c]['bias_max'] - resolved['bias_max']) <= 1e-8 \
                and abs(float(f[f'texel_world{c}']) - resolved['texel_world']) <= 1e-6, (name, frame, c, resolved)
        read = lambda suffix, at=frame: (directory / f'sunapply_{at}_{suffix}').read_bytes()
        encoding, lanes = params['depth_encoding'], rt2_lanes(f, env)
        before, after, rt2 = read('before.rgba16f'), read('after.rgba16f'), read(rt2_file(encoding))
        assert len(before) == len(after) == width * height * 8 and len(rt2) == width * height * lanes * 4, (name, frame)
        images[frame] = (before, after)
        maps = [sun_apply.unpack_map(read(f'map{c}.r32f', map_frames[c]), size) if params['cascades'][c]['valid'] else None for c in range(count)]
        d, s = sun_apply.unpack_rt2(rt2, width, height, encoding)
        comparison = sun_apply.compare_frame_cascades(sun_apply.unpack_rgba16f(before, width, height), sun_apply.unpack_rgba16f(after, width, height), d, s, maps, params, scene, extents)
        # The F8 self-check on the fixture's own frame: the receiver-minus-map residual of own-surface receivers per
        # cascade, in world units against the cascade's depth range (depth_light + depth_behind<c>).
        ranges = [float(config['depth_light']) + float(config['depth_behind'].split(',')[c]) for c in range(count)]
        own_surface = sun_apply.own_surface_residual(sun_apply.expected_factor_cascades(d, s, maps, params), maps, params, ranges)
        comparison.update(case=letter, scale=float(f['scale']), elevation=float(f['elevation']), us=float(t['us']), replay_us=float(t['replay_us']), issues=int(t['issues']),
                          far_replayed=f['far_replayed'] == '1', far_frame=int(f['far_frame']), records=records, own_surface=own_surface,
                          raster_m20=scene['raster_m20'], raster_m21=scene['raster_m21'], legacy_latch=scene['legacy_latch'])
        # The latch is the raster's jitter plus the pixel-centre term (quad_pixel_centre_m20/m21) unless the legacy witness dropped it.
        centre = sun_apply.pixel_centre_terms(width, height)
        assert abs(params['m20'] - scene['raster_m20'] - (0.0 if scene['legacy_latch'] else centre[0])) < 1e-7 and abs(params['m21'] - scene['raster_m21'] - (0.0 if scene['legacy_latch'] else centre[1])) < 1e-7, (name, frame, params, scene)
        assert comparison['ok'], (name, frame, comparison)
        # Ambiguity: up to 14 % here against the apply script's 10 % bound, because this scene shows the plane out to its horizon,
        # where the per-pixel view-depth step sits at the plane-fit threshold (the twin's planar_step band); measured, not an error class.
        assert comparison['compared'] > 0 and comparison['ambiguous'] < comparison['valid'] // 5, (name, frame, comparison)
        if letter == 'h':
            # The sun at finite distance (24,000 units): the analytic shadow is the point light's. A (cascade 0) and B
            # (cascade 1, near its centre) lie within one texel of it; C (cascade 1's edge, 500 units above its shadow)
            # shows the orthographic residual h r / D, while against the parallel shadow of its cascade's own sun every
            # region is within one texel (the map is right for what it is).
            a, b, c_region = comparison['regions']
            assert f['point_rederived'] == '3' and float(f['point_agreement_deg']) < .01 and f['suns'].split(';')[0] != f['suns'].split(';')[1] == f['suns'].split(';')[2], (name, frame, f['suns'])
            assert (a['owner'], b['owner'], c_region['owner']) == (0, 1, 1) and min(a['shadowed_point'], b['shadowed_point'], c_region['shadowed_point']) >= 8, (name, frame, comparison['regions'])
            assert a['beyond_one_point'] == 0 and b['beyond_one_point'] == 0 and a['residual_texels'] < .5 and b['residual_texels'] < .5, (name, frame, a, b)
            assert all(region['beyond_one_parallel'] == 0 for region in comparison['regions']), (name, frame, comparison['regions'])
            assert c_region['residual_texels'] > 1.5 and c_region['beyond_one_point'] > 0, (name, frame, c_region)
        else:
            assert comparison['edge_beyond_one'] == 0 and comparison['interior_wrong'] == 0, (name, frame, comparison)
        per = comparison['cascades']
        if letter in 'agi':  # the far plane: the box's shadow is cascade 1's alone (g, i: the same scene at eight sub-texel phases)
            assert per['1']['shadowed_analytic'] > 100 and per['0']['shadowed_analytic'] == 0 and per['2']['shadowed_analytic'] == 0, (name, frame, per)
        if letter == 'i':    # the half-pixel receiver: cascade 1's own-surface residual median is within a quarter texel (the legacy latch: over it)
            own = own_surface[1]
            assert own['own_surface'] >= 1000 and abs(own['median']) < .25 * per['1']['texel_world'], (name, frame, own, per['1']['texel_world'])
        elif letter == 'b':  # the seam: the shadow runs through cascade 0's band into cascade 1
            assert per['0']['band_shadowed'] > 20 and per['0']['shadowed_analytic'] > 50 and per['1']['shadowed_analytic'] > 50, (name, frame, per)
        elif letter in 'cf':  # the sun column: the second caster's shadow exists on cascade 0 and is dark
            second = comparison['second_caster']
            assert f['legacy_high'] == '1' and masks[2] & 1 and second['interior'] > 20 and second['interior_dark'] == second['interior'] and second['darkened'] > second['pixels'] // 2, (name, frame, second)
            covered = int(np.count_nonzero(maps[0] == 0.0))
            assert (covered >= 50) if letter == 'f' else covered == 0, (name, frame, covered)  # only the pancaked occluder lies on the near plane
        elif letter in 'de':  # the far cascade: replayed, retained through a camera move, absent after the Reset
            far = per['2']
            assert far['owned'] > 1000, (name, frame, far)
            if frame == SUN_CASCADE_RESET_FRAME:
                assert far['shadowed'] == 0 and far['texel_world'] is None, (name, frame, far)
            else:
                assert far['shadowed_analytic'] > 100 and far['edge_beyond_one'] == 0, (name, frame, far)
        # The half-texel witness on real rasterized maps: the shift (0.25-texel steps of the owning cascade) at which the
        # analytic shadow best matches the quad's f >= 0.5 shadow is within a quarter texel on both axes, while the
        # pre-fix lookup rule evaluated on the same maps sits half a texel away (the fit's sensitivity).
        comparisons[frame] = comparison
    # The half-texel witness on real rasterized maps (case g, eight sub-texel phases of the box summed): the shift, in
    # 0.25-texel steps of the owning cascade, at which the analytic shadow best matches the quad's f >= 0.5 shadow is
    # within a quarter texel on both axes; the pre-fix lookup rule evaluated on the same maps is not (the fit's sensitivity).
    phase_frames = [k for k, letter in enumerate(SUN_CASCADE_SCRIPT) if letter == 'g']
    shift_fit = sun_apply.sum_shift_fits([comparisons[k]['shift_fit'] for k in phase_frames])
    shift_fit_legacy = sun_apply.sum_shift_fits([comparisons[k]['shift_fit_legacy_rule'] for k in phase_frames])
    assert sum(comparisons[k]['shift_fit']['edge_pixels'] for k in phase_frames) >= 4000, (name, [comparisons[k]['shift_fit']['edge_pixels'] for k in phase_frames])
    assert max(abs(v) for v in shift_fit['shift']) <= .25, (name, shift_fit)
    assert max(abs(v) for v in shift_fit_legacy['shift']) >= .5 and shift_fit_legacy['mismatch_at_zero'] > 1.2 * shift_fit['mismatch_at_zero'], (name, shift_fit_legacy, shift_fit)
    # The half-pixel receiver witness (case i, eight phases, the sloped receiver 800-1,250 units out): the same fit against
    # the analytic shadow at the D3D9 pixel centres is within a quarter texel; the pre-fix latch (the receiver z / (W m00)
    # beside the sampled surface) moves the edge by half a texel or more (X3M_FIXTURE_SUNAPPLY_LEGACY_LATCH=1 witnesses it).
    receiver_frames = [k for k, letter in enumerate(SUN_CASCADE_SCRIPT) if letter == 'i']
    receiver_fit = sun_apply.sum_shift_fits([comparisons[k]['shift_fit'] for k in receiver_frames])
    assert sum(comparisons[k]['shift_fit']['edge_pixels'] for k in receiver_frames) >= 2000, (name, [comparisons[k]['shift_fit']['edge_pixels'] for k in receiver_frames])
    assert max(abs(v) for v in receiver_fit['shift']) <= .25, (name, receiver_fit)
    legacy_latch = any(c['legacy_latch'] for c in comparisons.values())
    for c in comparisons.values():  # the per-frame grids stay out of the record
        c['shift_fit'].pop('grid'); c['shift_fit_legacy_rule'].pop('grid')
    first, second = SUN_CASCADE_IDENTITY
    assert images[first] == images[second], f'{name}: the replay after the Reset frames differs from the frame before it'
    assert frames[7]['camera'] != frames[6]['camera'] and frames[7]['rows2'] != frames[6]['rows2'], (name, 'the retained far map is sampled through the moved camera')
    us, replay_us = sorted(c['us'] for c in comparisons.values()), sorted(c['replay_us'] for c in comparisons.values())
    validator_checks = 10 + 10 * len(SUN_CASCADE_SCRIPT)
    return {'checks': checks + validator_checks, 'fixture_checks': checks, 'validator_checks': validator_checks, 'frames': len(SUN_CASCADE_SCRIPT), 'map_size': size, 'cascades': count,
            'legacy_latch': legacy_latch,
            'config': {k: config[k] for k in ('extents', 'depth_light', 'depth_behind', 'caps', 'bias_units', 'clamp_texels', 'slope_texels', 'margin', 'band')},
            'program_slots': int(device[0]['slots']), 'cascade_program_slots': int(device[0]['cascade_slots']), 'max_texture': device[0]['max_texture'], 'ps30_slots': int(device[0]['ps30_slots']),
            'depth_format': int(replay_device[0]['depth_format']), 'depth_size': int(replay_device[0]['depth_size']),
            'bounds_bench_ns': {'single_verdict': float(bench[0]['verdict_ns']), 'cascade_mask_4': float(bench[0]['mask_ns']), 'cascade_mask_4_per_cascade_suns': float(bench[0]['split_mask_ns']),
                                'cascade_mask_4_with_size': float(bench[0]['sized_mask_ns']), 'static_classification': float(bench[0]['class_ns'])},
            'select_bench_us': {'records_4096_cap_2048': float(bench[0]['select_4096_half_us']), 'records_4096_cap_4095': float(bench[0]['select_4096_one_us'])},
            'half_texel': {'shift_fit': shift_fit, 'shift_fit_legacy_rule': shift_fit_legacy, 'frames': phase_frames},
            'half_pixel_receiver': {'shift_fit': receiver_fit, 'frames': receiver_frames, 'own_surface_median_c1': [comparisons[k]['own_surface'][1]['median'] for k in receiver_frames]},
            'point_light': {'frame': SUN_CASCADE_SCRIPT.index('h'), 'regions': comparisons[SUN_CASCADE_SCRIPT.index('h')]['regions']},
            'frames_detail': comparisons, 'worst_codes': max(c['worst_codes'] for c in comparisons.values()), 'ambiguous_max': max(c['ambiguous'] for c in comparisons.values()),
            'edge_mismatch': {'mismatch': sum(v['edge_mismatch'] for c in comparisons.values() for v in c['cascades'].values()), 'beyond_one_texel': sum(c['edge_beyond_one'] for c in comparisons.values())},
            'us': {'min': us[0], 'median': us[len(us) // 2], 'max': us[-1]},
            'replay_us': {'min': replay_us[0], 'median': replay_us[len(replay_us) // 2], 'max': replay_us[-1],
                          'per_issue_median': sorted(c['replay_us'] / max(1, c['issues']) for c in comparisons.values())[len(comparisons) // 2]}}


def validate_sun_apply_cascades_faces(name, text, directory, env):
    """The faces script: sixteen 'r' frames of the fifth cascade under the
    eight jitter offsets; per frame the readback against the cascade twin
    within one FP16 code, the box's plane shadow against the analytic shadow
    (the closed box casts the same footprint from its back faces; the fixed
    half may lose the contact line, at most 5 % of the shadow beyond one
    texel), and SUNAPPLY_FACES: on the fixed half the lit-face receivers stay
    lit (no interior pixel darkened, none flipping between consecutive
    frames; the silhouette pixels, whose taps leave the face, at most 3 %) and
    the map holds the back faces (the faces away from the sun, the back faces
    themselves, are no longer self-shadowed: at most 40 % darkened against at
    least 90 % on the control half; in the game those faces have sun share 0
    and no verdict shows). A fixed half run unfixed
    (X3M_FIXTURE_SUNAPPLY_FACES_FIX=0) fails the back-face signature and the
    flags. At 256^2 maps the fixture's flat box does not reproduce run116's
    knife edge on faceted hulls at 4096^2 (its 1-texel constant bias covers a
    flat face's quantisation); the control's lit-face numbers are recorded."""
    import numpy as np
    assert 'RESULT PASS' in text, f'{name}: fixture failed'
    checks = int(fields(next(l for l in text.splitlines() if l.startswith('RESULT PASS')))['checks'])
    count = len(SUN_CASCADE5_EXTENTS)
    config = [fields(l) for l in text.splitlines() if l.startswith('SUNAPPLY_CONFIG ')]
    assert len(config) == 1 and config[0]['cascades'] == '5', (name, config)
    config = config[0]
    size = int(config['map_size'])
    assert float(config['slope_texels']) == float(env.get('X3M_SUN_SHADOW_BIAS_SLOPE_TEXELS', '0')), (name, 'the slope margin the fixture ran', config['slope_texels'])  # the -slope siblings: 0.2
    frames = {int(fields(l)['frame']): fields(l) for l in text.splitlines() if l.startswith('SUNAPPLY_CASCADES ')}
    times = {int(fields(l)['frame']): fields(l) for l in text.splitlines() if l.startswith('SUNAPPLY_TIME ')}
    faces = {int(fields(l)['frame']): fields(l) for l in text.splitlines() if l.startswith('SUNAPPLY_FACES ')}
    assert sorted(frames) == sorted(times) == sorted(faces) == list(range(SUN_FACES_FRAMES)), (name, sorted(frames), sorted(faces))
    comparisons, control, fixed = {}, [], []
    # Cascades 1-4 replay back faces at 256^2 maps (their texels 11.7 .. 1,171 units; cascade 0's is 1.95): the texel law.
    expected_mask = sum(1 << c for c in range(count) if 2.0 * SUN_CASCADE5_EXTENTS[c] / size >= 8.0)
    for frame in range(SUN_FACES_FRAMES):
        f, t, face = frames[frame], times[frame], faces[frame]
        is_fixed = frame >= SUN_FACES_FIXED_FROM
        assert f['case'] == 'r' and int(f['owner']) == 4 and t['applied'] == '1' and t['result'] == '00000000' and int(f['jitter_index']) == frame % 8, (name, frame, f['case'], t)
        assert int(face['fixed']) == int(is_fixed) and int(face['backface_mask']) == (expected_mask if is_fixed else 0), (name, frame, face, expected_mask)
        assert [f[f'backface{c}'] for c in range(count)] == [('1' if is_fixed and expected_mask >> c & 1 else '0') for c in range(count)], (name, frame, f)
        width, height = int(f['width']), int(f['height'])
        params, scene, extents, map_frames = sun_apply.parse_cascade_params(f)
        for c, cascade in enumerate(params['cascades']):
            cascade['eps_depth'] = sun_apply.EPS_DEPTH * SUN_CASCADE5_EPS_RANGE / (float(config['depth_light']) + float(config['depth_behind'].split(',')[c]))
        params['band_eps'] = sun_apply.EPS_SELECT
        read = lambda suffix, at=frame: (directory / f'sunapply_{at}_{suffix}').read_bytes()
        encoding, lanes = params['depth_encoding'], rt2_lanes(f, env)
        before, after, rt2 = read('before.rgba16f'), read('after.rgba16f'), read(rt2_file(encoding))
        assert len(before) == len(after) == width * height * 8 and len(rt2) == width * height * lanes * 4, (name, frame)
        maps = [sun_apply.unpack_map(read(f'map{c}.r32f', map_frames[c]), size) if params['cascades'][c]['valid'] else None for c in range(count)]
        d, s = sun_apply.unpack_rt2(rt2, width, height, encoding)
        comparison = sun_apply.compare_frame_cascades(sun_apply.unpack_rgba16f(before, width, height), sun_apply.unpack_rgba16f(after, width, height), d, s, maps, params, scene, extents)
        assert comparison['ok'] and comparison['compared'] > 0, (name, frame, 'the readback differs from the twin', {k: comparison[k] for k in ('ok', 'compared', 'worst_codes', 'violations') if k in comparison})
        far = comparison['cascades'][str(count - 1)]
        # The plane shadow of the box: exact on the control half; on the fixed half the back-face map loses the
        # contact line (the ray through a plane point near the box's foot leaves the box through the side face
        # facing that point, within the constant bias of the point itself: about one texel of contact shadow at
        # this elevation, the trade-off of shadow-cascade-extents.md; 17 of 872 shadowed pixels here), never more.
        contact_limit = 0 if not is_fixed else int(.05 * far['shadowed_analytic'])
        assert comparison['edge_beyond_one'] <= contact_limit and far['shadowed_analytic'] > 100, (name, frame, 'the plane shadow of the box', comparison['edge_beyond_one'], contact_limit, far)
        lit, lit_dark, dark, dark_dark, common, flips, interior, interior_dark, interior_common, interior_flips = (
            int(face[k]) for k in ('lit', 'lit_darkened', 'dark', 'dark_darkened', 'lit_common', 'lit_flips', 'interior', 'interior_darkened', 'interior_common', 'interior_flips'))
        assert lit > 200 and dark > 50 and interior > 100, (name, frame, 'the box must be a receiver on both kinds of face', face)
        entry = dict(frame=frame, fixed=is_fixed, jitter_index=int(f['jitter_index']), lit=lit, lit_darkened=lit_dark / lit, interior=interior, interior_darkened=interior_dark / interior,
                     dark=dark, dark_shadowed=dark_dark / dark, lit_flips=(flips / common) if common else None, interior_flips=(interior_flips / interior_common) if interior_common else None,
                     worst_codes=comparison['worst_codes'], ambiguous=comparison['ambiguous'], plane_shadowed=far['shadowed_analytic'], plane_edge_beyond_one=comparison['edge_beyond_one'],
                     us=float(t['us']), replay_us=float(t['replay_us']))
        (fixed if is_fixed else control).append(entry)
        comparisons[frame] = entry
    for e in fixed:
        assert e['interior_darkened'] == 0.0 and (e['interior_flips'] is None or e['interior_flips'] == 0.0) and e['lit_darkened'] <= .03 and (e['lit_flips'] is None or e['lit_flips'] <= .03), (name, 'the fixed half: lit faces', e)
        assert e['dark_shadowed'] <= .4, (name, 'the fixed half: the map holds the back faces', e)
    for e in control:
        assert e['dark_shadowed'] >= .9, (name, 'the control half: the map holds the front faces', e)
    return {'checks': checks + 6 * SUN_FACES_FRAMES, 'fixture_checks': checks, 'frames': SUN_FACES_FRAMES, 'map_size': size, 'cascades': count, 'backface_mask': expected_mask,
            'control': {'lit_darkened_max': max(e['lit_darkened'] for e in control), 'interior_darkened_max': max(e['interior_darkened'] for e in control),
                        'lit_flips_max': max(e['lit_flips'] or 0.0 for e in control), 'interior_flips_max': max(e['interior_flips'] or 0.0 for e in control), 'dark_shadowed_min': min(e['dark_shadowed'] for e in control)},
            'fixed': {'lit_darkened_max': max(e['lit_darkened'] for e in fixed), 'interior_darkened_max': max(e['interior_darkened'] for e in fixed),
                      'lit_flips_max': max(e['lit_flips'] or 0.0 for e in fixed), 'interior_flips_max': max(e['interior_flips'] or 0.0 for e in fixed), 'dark_shadowed_max': max(e['dark_shadowed'] for e in fixed)},
            'frames_detail': comparisons, 'worst_codes': max(e['worst_codes'] for e in comparisons.values()), 'ambiguous_max': max(e['ambiguous'] for e in comparisons.values()),
            'edge_mismatch': None, 'us': {'median': sorted(e['us'] for e in comparisons.values())[SUN_FACES_FRAMES // 2]}}


def validate_sun_apply_cascades_five(name, text, directory, env):
    """The five-cascade script (shadow-cascade-extents.md, section 3): the
    30 km set builds with the fifth cap default, the cascade program fits the
    device's ps_3_0 slots, five maps attach; per frame the counters against
    the script (the box in its owning cascade and every farther one, the fifth
    cascade skipped by the budget on the odd 'd' / 'e' frames), the readback
    against the cascade twin within one FP16 code, the f >= 0.5 edge of every
    plane receiver within one texel of its owning cascade, solid interiors
    and monotone bands; 'r' frames shadow the owner alone, 's' frames run
    through the owner's band into the next cascade, the Reset frame leaves
    the fifth cascade absent and lit and the frame after repeats the frame
    before it byte for byte; the bounds bench for five cascades."""
    assert 'RESULT PASS' in text, f'{name}: fixture failed'
    checks = int(fields(next(l for l in text.splitlines() if l.startswith('RESULT PASS')))['checks'])
    count = len(SUN_CASCADE5_EXTENTS)
    device = [fields(l) for l in text.splitlines() if l.startswith('SUNAPPLY_DEVICE ')]
    assert len(device) == 1 and device[0]['attached'] == '1' and device[0]['references'] == '4' \
        and int(device[0]['slots']) < int(device[0]['cascade_slots']) <= int(device[0]['ps30_slots']), (name, device)
    replay_device = [fields(l) for l in text.splitlines() if l.startswith('SUNAPPLY_REPLAY_DEVICE ')]
    assert len(replay_device) == 1 and replay_device[0]['maps'] == '5' and replay_device[0]['readable'] == '1' and replay_device[0]['halved'] == '0', (name, replay_device)
    config = [fields(l) for l in text.splitlines() if l.startswith('SUNAPPLY_CONFIG ')]
    assert len(config) == 1 and config[0]['cascades'] == '5' and [float(v) for v in config[0]['extents'].split(',')] == list(SUN_CASCADE5_EXTENTS) and config[0]['depth_light'] == '300000' \
        and config[0]['depth_behind'] == '512,3000,15000,75000,300000' and config[0]['caps'] == '128,512,1024,1024,1024' \
        and abs(float(config[0]['margin']) - sun_apply.CASCADE_MARGIN) < 1e-6 and abs(float(config[0]['band']) - sun_apply.CASCADE_BAND) < 1e-6, (name, config)
    config = config[0]
    size = int(config['map_size'])
    assert float(config['slope_texels']) == float(env.get('X3M_SUN_SHADOW_BIAS_SLOPE_TEXELS', '0')), (name, 'the slope margin the fixture ran', config['slope_texels'])  # the -slope siblings: 0.2
    frames = {int(fields(l)['frame']): fields(l) for l in text.splitlines() if l.startswith('SUNAPPLY_CASCADES ')}
    times = {int(fields(l)['frame']): fields(l) for l in text.splitlines() if l.startswith('SUNAPPLY_TIME ')}
    assert sorted(frames) == sorted(times) == list(range(len(SUN_CASCADE5_SCRIPT))), (name, sorted(frames), sorted(times))
    bench = [fields(l) for l in text.splitlines() if l.startswith('SUNAPPLY_BOUNDS_BENCH ')]
    assert len(bench) == 1 and float(bench[0]['verdict_ns']) > 0 and float(bench[0]['mask_ns']) > 0 and float(bench[0]['mask5_ns']) > 0, (name, bench)
    comparisons, images = {}, {}
    for frame, (letter, owner) in enumerate(SUN_CASCADE5_SCRIPT):
        f, t = frames[frame], times[frame]
        assert f['case'] == letter and int(f['owner']) == owner and t['applied'] == '1' and t['result'] == '00000000' and float(t['us']) > 0 and float(t['replay_us']) > 0, (name, frame, f['case'], t)
        width, height = int(f['width']), int(f['height'])
        params, scene, extents, map_frames = sun_apply.parse_cascade_params(f)
        assert extents == list(SUN_CASCADE5_EXTENTS) and len(scene['boxes']) == 1, (name, frame, extents)
        # The twin's compare ambiguity is EPS_DEPTH of normalized depth over the three-cascade set's 15,512-unit
        # range (1.55 world units); a cascade of this set spans 300,512..600,000 units, so the same world tolerance.
        for c, cascade in enumerate(params['cascades']):
            cascade['eps_depth'] = sun_apply.EPS_DEPTH * SUN_CASCADE5_EPS_RANGE / (float(config['depth_light']) + float(config['depth_behind'].split(',')[c]))
        params['band_eps'] = sun_apply.EPS_SELECT  # the band weight's uncertainty from the position's (a quarter FP16 code flips the rounding)
        # The latch is the raster's jitter plus the pixel-centre term (quad_pixel_centre_m20/m21), as the three-cascade validator checks.
        centre = sun_apply.pixel_centre_terms(width, height)
        assert not scene['legacy_latch'] and abs(params['m20'] - scene['raster_m20'] - centre[0]) < 1e-7 and abs(params['m21'] - scene['raster_m21'] - centre[1]) < 1e-7, (name, frame, params['m20'], params['m21'], scene)
        masks = [int(v) for v in f['masks'].split(',')][:2]  # the plane and the box
        assert masks[0] == (1 << count) - 1 and masks[1] >> owner & 1 and not masks[1] & ((1 << owner) - 1), (name, frame, masks, owner)
        records = [sum(1 for m in masks if m >> c & 1) for c in range(count)]
        far_skipped = frame in SUN_CASCADE5_FAR_SKIPPED
        assert [int(f[f'c{c}']) for c in range(count)] == records and int(f['issues']) == sum(records), (name, frame, records, f['issues'])
        assert [int(f[f'draws{c}']) for c in range(count)] == [0 if far_skipped and c == count - 1 else records[c] for c in range(count)], (name, frame)
        assert int(t['issues']) == sum(records) - (records[-1] if far_skipped else 0), (name, frame, t)
        assert (f['far_replayed'] == '1') == (not far_skipped), (name, frame, f['far_replayed'])
        expected_far = -1 if frame == SUN_CASCADE5_RESET_FRAME else frame - 1 if far_skipped else frame
        assert int(f['far_frame']) == expected_far and map_frames[:count - 1] == [frame] * (count - 1) and map_frames[count - 1] == expected_far, (name, frame, f['far_frame'], map_frames)
        assert [c['valid'] for c in params['cascades']] == [True] * (count - 1) + [frame != SUN_CASCADE5_RESET_FRAME] and int(t['bound']) == (count - 1 if frame == SUN_CASCADE5_RESET_FRAME else count), (name, frame)
        assert (int(f['budget']) < int(f['issues'])) == (letter in 'de'), (name, frame, f['budget'], f['issues'])
        for c in range(count):  # the bias each cascade resolved is the law's at its own texel and depth range
            resolved = sun_apply.resolve_bias(float(config['bias_units']), extents[c], .5 * (float(config['depth_light']) + float(config['depth_behind'].split(',')[c])), size, float(config['clamp_texels']))
            assert abs(float(params['cascades'][c]['slope_texels']) - float(config['slope_texels'])) <= 1e-7 * max(1.0, float(config['slope_texels'])), (name, frame, c, 'the slope margin on this cascade (the float32 the pass received)', params['cascades'][c]['slope_texels'], config['slope_texels'])
            assert abs(params['cascades'][c]['bias_constant'] - resolved['bias_constant']) <= 1e-9 and abs(params['cascades'][c]['bias_max'] - resolved['bias_max']) <= 1e-8 \
                and abs(float(f[f'texel_world{c}']) - resolved['texel_world']) <= 1e-6, (name, frame, c, resolved)
        read = lambda suffix, at=frame: (directory / f'sunapply_{at}_{suffix}').read_bytes()
        encoding, lanes = params['depth_encoding'], rt2_lanes(f, env)
        before, after, rt2 = read('before.rgba16f'), read('after.rgba16f'), read(rt2_file(encoding))
        assert len(before) == len(after) == width * height * 8 and len(rt2) == width * height * lanes * 4, (name, frame)
        images[frame] = (before, after)
        maps = [sun_apply.unpack_map(read(f'map{c}.r32f', map_frames[c]), size) if params['cascades'][c]['valid'] else None for c in range(count)]
        d, s = sun_apply.unpack_rt2(rt2, width, height, encoding)
        comparison = sun_apply.compare_frame_cascades(sun_apply.unpack_rgba16f(before, width, height), sun_apply.unpack_rgba16f(after, width, height), d, s, maps, params, scene, extents)
        comparison.update(case=letter, owner=owner, scale=float(f['scale']), elevation=float(f['elevation']), us=float(t['us']), replay_us=float(t['replay_us']), issues=int(t['issues']),
                          far_replayed=f['far_replayed'] == '1', far_frame=int(f['far_frame']), records=records)
        assert comparison['ok'] and comparison['compared'] > 0 and comparison['ambiguous'] < comparison['valid'] // 5, (name, frame, comparison)
        assert comparison['edge_beyond_one'] == 0 and comparison['interior_wrong'] == 0 and comparison['monotone_violations'] == 0, (name, frame, comparison)
        per = comparison['cascades']
        if letter == 'r':    # the range: the shadow is the owner's alone
            assert per[str(owner)]['shadowed_analytic'] > 100 and all(per[str(c)]['shadowed_analytic'] == 0 for c in range(count) if c != owner), (name, frame, per)
        elif letter == 's':  # the seam: the shadow runs through the owner's band into the next cascade
            assert per[str(owner)]['band_shadowed'] > 20 and per[str(owner)]['shadowed_analytic'] > 50 and per[str(owner + 1)]['shadowed_analytic'] > 50, (name, frame, per)
        else:                # the fifth cascade: replayed, retained through a camera move, absent after the Reset
            far = per[str(count - 1)]
            assert far['owned'] > 1000, (name, frame, far)
            if frame == SUN_CASCADE5_RESET_FRAME:
                assert far['shadowed'] == 0 and far['texel_world'] is None, (name, frame, far)
            else:
                assert far['shadowed_analytic'] > 100 and far['edge_beyond_one'] == 0, (name, frame, far)
        for key in ('shift_fit', 'shift_fit_legacy_rule'):  # the per-frame grids stay out of the record
            comparison[key].pop('grid')
        comparisons[frame] = comparison
    first, second = SUN_CASCADE5_IDENTITY
    assert images[first] == images[second], f'{name}: the replay after the Reset frames differs from the frame before it'
    assert frames[11]['camera'] != frames[10]['camera'] and frames[11]['rows4'] != frames[10]['rows4'], (name, 'the retained fifth map is sampled through the moved camera')
    us, replay_us = sorted(c['us'] for c in comparisons.values()), sorted(c['replay_us'] for c in comparisons.values())
    validator_checks = 7 + 11 * len(SUN_CASCADE5_SCRIPT)
    return {'checks': checks + validator_checks, 'fixture_checks': checks, 'validator_checks': validator_checks, 'frames': len(SUN_CASCADE5_SCRIPT), 'map_size': size, 'cascades': count,
            'config': {k: config[k] for k in ('extents', 'depth_light', 'depth_behind', 'caps', 'bias_units', 'clamp_texels', 'slope_texels', 'margin', 'band')},
            'program_slots': int(device[0]['slots']), 'cascade_program_slots': int(device[0]['cascade_slots']), 'max_texture': device[0]['max_texture'], 'ps30_slots': int(device[0]['ps30_slots']),
            'depth_format': int(replay_device[0]['depth_format']), 'depth_size': int(replay_device[0]['depth_size']),
            'bounds_bench_ns': {'single_verdict': float(bench[0]['verdict_ns']), 'cascade_mask_4': float(bench[0]['mask_ns']), 'cascade_mask_4_per_cascade_suns': float(bench[0]['split_mask_ns']),
                                'cascade_mask_5': float(bench[0]['mask5_ns'])},
            'frames_detail': comparisons, 'worst_codes': max(c['worst_codes'] for c in comparisons.values()), 'ambiguous_max': max(c['ambiguous'] for c in comparisons.values()),
            'edge_mismatch': {'mismatch': sum(v['edge_mismatch'] for c in comparisons.values() for v in c['cascades'].values()), 'beyond_one_texel': sum(c['edge_beyond_one'] for c in comparisons.values())},
            'us': {'min': us[0], 'median': us[len(us) // 2], 'max': us[-1]},
            'replay_us': {'min': replay_us[0], 'median': replay_us[len(replay_us) // 2], 'max': replay_us[-1],
                          'per_issue_median': sorted(c['replay_us'] / max(1, c['issues']) for c in comparisons.values())[len(comparisons) // 2]}}


SUN_LINE_FIELDS = ('device', 'frame', 'verdict', 'register', 'samples', 'agree', 'disagree', 'invalid', 'no_register', 'unlatched', 'bounds_state', 'bounds_unavailable', 'extent_refused', 'sun')


def validate_shadow_replay_sun(name, trace, routed, sun_programs, expected_sun):
    """The frame's one validated sun (shadow_replay_sun.h): one
    `shadow_replay_sun` line per frame; every routed z-writing draw sampled its
    own program's LightDir_Dir0 register and agreed, except on the
    sun-changing frame where every sample disagreed and the latch kept the
    sun; one latch event, from c5 of the glow program when the sun-register
    pairs are drawn (it is the first routed draw), else from c4 of the hull
    program; the bounds rows were available for every extent-known draw and
    the extent cache refused no store."""
    lines = [fields(l) for l in trace.splitlines() if l.startswith('shadow_replay_sun ')]
    assert [int(l['frame']) for l in lines] == list(range(SHADOW_REPLAY_FRAMES)) and all(tuple(l) == SUN_LINE_FIELDS for l in lines), (name, lines[:2])
    register = 5 if sun_programs else 4
    for l in lines:
        frame = int(l['frame'])
        changing = frame == SHADOW_REPLAY_NO_SUN_FRAME
        counts = tuple(int(l[k]) for k in ('samples', 'agree', 'disagree', 'invalid', 'no_register', 'unlatched'))
        assert l['verdict'] == ('changing' if changing else 'sampled') and int(l['register']) == register, (name, l)
        # Frame 0: the first draw's sample waits (unlatched), the second agrees and latches the first one's value.
        assert counts == ((routed, 0, routed, 0, 0, 0) if changing else (routed, routed - 1, 0, 0, 0, 1) if frame == 0 else (routed, routed, 0, 0, 0, 0)), (name, l, routed)
        assert int(l['bounds_unavailable']) == 0 and int(l['extent_refused']) == 0, (name, l)
        assert all(abs(float(v) - e) < 1e-5 for v, e in zip(l['sun'].split(','), expected_sun)), (name, l, expected_sun)  # the latched value never follows the flip
    events = [fields(l) for l in trace.splitlines() if l.startswith('shadow_replay_sun_latch ')]
    program = 'fffdabd910793aba' if sun_programs else '8759c7838bbc86c2'
    assert len(events) == 1 and (events[0]['event'], events[0]['frame'], int(events[0]['register']), events[0]['program']) == ('latch', '0', register, program), (name, events)
    return {'frames': len(lines), 'register': register, 'program': program, 'changing_frame': SHADOW_REPLAY_NO_SUN_FRAME, 'samples_per_frame': routed}


SUN_POINT_FIELDS = ('device', 'frame', 'source', 'reason', 'poll', 'light', 'native', 'distance', 'checks', 'disagreements', 'agreement_deg', 'rederived', 'carried', 'candidates', 'directional',
                    'slot_admitted', 'rule', 'rules_agree', 'admission_native', 'score', 'second_score', 'flags', 'record_scale', 'poll_us', 'frames_point')
SUN_POINT_SELECTION = ('4', '3', '3', 'engine', '1', '955', '896')  # the fixture's block: the sun 187 + 0x300 before the forced-directional node 128 + 0x300, both rules on the same node


def validate_shadow_replay_poll(name, text, trace, mode, count, sizes, cameras, maps, routed):
    """The sun-position poll (shadow_replay_sun_point.h): one
    `shadow_replay_sun_point` line per frame. Without the seam the poll is
    unavailable (the fixture is not the verified executable): source latch,
    reason unavailable. agree: source point on every frame, the light as
    installed, 1..8 checks without a disagreement within 0.1 degrees (none on
    the sun-changing frame, whose validation is carried), every cascade's
    direction within 1 / size radians of normalize(light - camera), held bit
    for bit unless the line reports a re-derivation, and every replayed map's
    basis forward is minus that direction. null: unavailable / null_context.
    disagree: disagrees on frame 0 with every check disagreeing, cooldown after."""
    lines = [fields(l) for l in trace.splitlines() if l.startswith('shadow_replay_sun_point ')]
    assert [int(l['frame']) for l in lines] == list(range(SHADOW_REPLAY_FRAMES)) and all(tuple(l)[:len(SUN_POINT_FIELDS)] == SUN_POINT_FIELDS for l in lines), (name, lines[:1])
    events = [(int(fields(l)['frame']), fields(l)['source'], fields(l)['reason']) for l in trace.splitlines() if l.startswith('shadow_replay_sun_source ')]
    poll = [fields(l) for l in text.splitlines() if l.startswith('SHADOW_POLL ')]
    reasons, worst, held, rederivations, poll_us, phases, previous_point = {}, 0.0, None, 0, [], [], None
    for l in lines:
        frame = int(l['frame'])
        reasons[l['reason']] = reasons.get(l['reason'], 0) + 1
        dirs = [tuple(float(v) for v in l[f'dir{c}'].split(',')) for c in range(count)]
        selection = (l['candidates'], l['directional'], l['slot_admitted'], l['rule'], l['rules_agree'], l['score'], l['second_score'])
        if mode == 'refusals':
            expected = fields([x for x in text.splitlines() if x.startswith(f'SHADOW_POLL_REFUSAL frame={frame} ')][0])['expect']
            assert expected == ('layout', 'layout', 'layout', 'unreadable', 'no_directional', 'null_context', 'null_context', 'null_context')[frame], (name, frame, expected)
            assert (l['source'], l['reason'], l['poll'], l['checks']) == ('latch', 'no_light' if expected == 'no_directional' else 'unavailable', expected, '0'), (name, l)
        elif mode in (None, 'null'):
            assert (l['source'], l['reason'], l['checks']) == ('latch', 'unavailable', '0') and l['poll'] in (('null_context',) if mode else ('disabled', 'executable_mismatch')), (name, l)
        elif mode == 'disagree':
            assert l['source'] == 'latch' and l['reason'] == ('disagrees' if frame == 0 else 'cooldown') and l['poll'] == 'ok' and selection == SUN_POINT_SELECTION, (name, l)
            if frame != SHADOW_REPLAY_NO_SUN_FRAME:
                assert int(l['checks']) == min(8, routed - (1 if frame == 0 else 0)) == int(l['disagreements']) and float(l['agreement_deg']) > 10.0, (name, l)
        else:
            light = tuple(float(v) for v in poll[0]['light'].split(','))
            assert l['source'] == 'point' and l['reason'] == 'point' and l['poll'] == 'ok' and selection == SUN_POINT_SELECTION and l['carried'] == '0' and l['admission_native'] == l['native'], (name, l)
            assert all(abs(float(v) - e) <= 6e-3 for v, e in zip(l['light'].split(','), light)) and abs(float(l['record_scale']) - .01) < 1e-6 and int(l['frames_point']) == frame + 1, (name, l)
            changing = frame == SHADOW_REPLAY_NO_SUN_FRAME
            assert int(l['disagreements']) == 0 and int(l['checks']) == (0 if changing else min(8, routed - (1 if frame == 0 else 0))), (name, l)
            if not changing:
                assert 0.0 <= float(l['agreement_deg']) < 0.1, (name, l)
                worst = max(worst, float(l['agreement_deg']))
            cam = cameras[frame]
            r, t = [float(v) for v in cam['r'].split(',')], [float(v) for v in cam['t'].split(',')]
            position = [-sum(t[j] * r[i * 3 + j] for j in range(3)) for i in range(3)]
            ideal = [light[i] - position[i] for i in range(3)]
            norm = math.sqrt(sum(v * v for v in ideal))
            assert abs(float(l['distance']) - norm) < 1e-3 * norm, (name, l, norm)
            for c in range(count):
                error = math.sqrt(sum((dirs[c][i] - ideal[i] / norm) ** 2 for i in range(3)))
                assert error <= 1.0 / sizes[c] + 1e-6, (name, frame, c, error)
                m = maps[(frame, c)]
                if m['valid'] == '1' and int(float(m['replayed_frame'])) == frame:
                    assert all(abs(-float(v) - d) < 1e-6 for v, d in zip(m['forward'].split(','), dirs[c])), (name, frame, c, m['forward'], dirs[c])
            assert (int(l['rederived']) > 0) == (held != dirs) and (frame > 0 or int(l['rederived']) == count), (name, l, held)
            # The grid anchor: a re-derivation moves each cascade's anchor to a point of its OLD grid (the old axes from the
            # old direction by the basis law), so the grid phase at the centre survives the turn.
            anchors = [tuple(float(v) for v in l[f'anchor{c}'].split(',')) for c in range(count)]
            if frame and int(l['rederived']) and previous_point:
                for c in range(count):
                    f_axis = [-v for v in previous_point[0][c]]
                    hint = (1.0, 0.0, 0.0) if abs(f_axis[1]) > .99 else (0.0, 1.0, 0.0)
                    right = [hint[1] * f_axis[2] - hint[2] * f_axis[1], hint[2] * f_axis[0] - hint[0] * f_axis[2], hint[0] * f_axis[1] - hint[1] * f_axis[0]]
                    norm_r = math.sqrt(sum(v * v for v in right)); right = [v / norm_r for v in right]
                    up = [f_axis[1] * right[2] - f_axis[2] * right[1], f_axis[2] * right[0] - f_axis[0] * right[2], f_axis[0] * right[1] - f_axis[1] * right[0]]
                    texel = 2.0 * float(maps[(frame, c)]['extent']) / sizes[c] if maps[(frame, c)]['available'] == '1' else None
                    if texel and anchors[c] != previous_point[1][c]:
                        for axis in (right, up):
                            u = sum((a - b) * x for a, b, x in zip(anchors[c], previous_point[1][c], axis)) / texel
                            assert abs(u - round(u)) < 1e-2, (name, frame, c, u)
                            phases.append(abs(u - round(u)))
            previous_point = (dirs, anchors)
            held = dirs; rederivations += int(l['rederived'])
        poll_us.append(float(l['poll_us']))
    expected_events = {None: [(0, 'latch', 'unavailable')], 'null': [(0, 'latch', 'unavailable')], 'agree': [(0, 'point', 'point')], 'disagree': [(0, 'latch', 'disagrees'), (1, 'latch', 'cooldown')],
                       'refusals': [(0, 'latch', 'unavailable'), (4, 'latch', 'no_light'), (5, 'latch', 'unavailable')]}[mode]
    assert events == expected_events, (name, events)
    statuses = {}
    for l in lines:
        statuses[l['poll']] = statuses.get(l['poll'], 0) + 1
    assert mode != 'agree' or len(phases) >= 2, (name, 'a re-derivation between two replays of one cascade', phases)
    return {'mode': mode or 'no_seam', 'reasons': reasons, 'poll_statuses': statuses, 'rederivation_grid_phase_texels': max(phases) if phases else None, 'events': events, 'worst_agreement_deg': worst, 'rederivations': rederivations, 'poll_us_median': sorted(poll_us)[len(poll_us) // 2], 'poll_us_max': max(poll_us)}


def toggle_windows(name, text, trace_lines, setting):
    """The at-rest A/B script (comparison-hotkeys.md, "Sun shadows at rest"):
    presses alternate off, on, off, on ... Returns the frames the shadows were
    off on and the frames an ON edge landed on. Every press is asserted in both
    the fixture's own line and the DLL's accepted `sun_shadow_toggle` event."""
    presses = [int(v) for v in setting.split(',')]
    expected = [(frame, index & 1) for index, frame in enumerate(presses)]
    script = [fields(l) for l in text.splitlines() if l.startswith('SHADOW_TOGGLE ')]
    assert [(int(t['frame']), int(t['state'])) for t in script] == expected, (name, script)
    events = [fields(l) for l in trace_lines if l.startswith('sun_shadow_toggle ')]
    assert [(int(e['frame']), int(e['state'])) for e in events] == expected, (name, events)
    assert all(e['accepted'] == '1' for e in events), (name, 'the script runs on a device that has the replay', events)
    off_window, on_frames = set(), set()
    for index, frame in enumerate(presses):
        if index & 1:
            on_frames.add(frame)
        else:
            off_window |= set(range(frame, presses[index + 1] if index + 1 < len(presses) else SHADOW_REPLAY_FRAMES))
    return off_window, on_frames


def validate_shadow_replay_cascades(name, text, trace, directory, env, taa):
    """The cascade script through the DLL (shadow-cascades.md, section 4): the
    mode, device and target lines; per frame c<i>/capped<i> on the counter
    line and draws<i>/far_replayed/far_frame/issues/budget on the depth line
    against the script (frame 0 by the origin rule per cascade, later frames by
    the one bounds pass; the far cascade on even frames while the issues exceed
    the budget; refusals and the Reset void what was retained); every
    replayed map against the CPU projection of the draws its cascade kept
    within the depth gate, a retained far map byte-identical on the odd
    frames, every map unchanged by a refused frame."""
    assert 'RESULT PASS' in text, f'{name}: fixture failed'
    checks = int(fields(next(l for l in text.splitlines() if l.startswith('RESULT PASS')))['checks'])
    casters = int(env.get('X3M_FIXTURE_SHADOW_CASTERS', '2'))
    extents = [float(v) for v in env['X3M_FIXTURE_SHADOW_CASCADES'].split(',')]
    count = len(extents)
    sizes = [int(v) for v in env['X3M_SHADOW_CASCADE_SIZES'].split(',')] * (count if ',' not in env['X3M_SHADOW_CASCADE_SIZES'] else 1)
    caps = [int(v) for v in env['X3M_SHADOW_CASCADE_CAPS'].split(',')]
    budget = int(env['X3M_SHADOW_CASCADE_BUDGET'])
    assert count == 2 and len(caps) == 2 and extents[0] < 16 < 230 < extents[1], (name, 'the script expects casters and L inside cascade 0, F inside cascade 1 only')
    tl = trace.splitlines()
    mode = [fields(l) for l in tl if l.startswith('shadow_cascades_mode ')]
    assert len(mode) == 1 and (mode[0]['enabled'], mode[0]['reason'], mode[0]['cascades'], mode[0]['budget']) == ('1', 'ok', '2', str(budget)) \
        and mode[0]['caps'].split(',')[:2] == [str(c) for c in caps] and mode[0]['sizes'].split(',')[:2] == [str(v) for v in sizes], (name, mode)
    device = [fields(l) for l in tl if l.startswith('shadow_replay_cascades_device ')]
    assert len(device) == 1 and (device[0]['attached'], device[0]['cascades'], device[0]['halved'], device[0]['depth_size']) == ('1', '2', '0', str(max(sizes))) \
        and device[0]['sizes'].split(',')[:2] == [str(v) for v in sizes], (name, device)
    depth_rows, refused, targets = depth_replay.parse_text(trace)
    candidate_rows, _ = candidates_analysis.parse_text(trace)
    by_frame = {r['frame']: r for r in candidate_rows}
    # The at-rest A/B (comparison-hotkeys.md, "Sun shadows at rest"): the frames
    # of the off window carry no replay line at all; the candidate counter, the
    # lane and the presented frames keep running.
    toggle = env.get('X3M_FIXTURE_SHADOW_TOGGLE')
    off_window, on_frames = set(), set()
    if toggle:
        off_window, on_frames = toggle_windows(name, text, tl, toggle)
    depth_by_frame = {r['frame']: r for r in depth_rows}
    assert [r['frame'] for r in depth_rows] == [f for f in range(SHADOW_REPLAY_FRAMES) if f not in off_window], (name, sorted(depth_by_frame))
    assert sorted(by_frame) == list(range(SHADOW_REPLAY_FRAMES)), (name, sorted(by_frame))
    assert all(r.get('shadow_toggle') == 1 for r in depth_rows), (name, 'every replay line records the state it ran under')
    # The script's objects in submission order with their cascade masks: frame 0
    # by the origin rule per cascade (the casters at unit distance: both; L at
    # 256 and F at 300 units: the far cascade), later frames by bounds (L
    # crosses cascade 0's box; F's vertices lie 225 units away).
    order = [('L', casters)] + [(('A', 'B')[i & 1], i) for i in range(casters)] + [('F', casters + 1)]
    def masks_of(frame):
        return {key: (3 if key[0] in 'AB' else 2) if frame == 0 else (2 if key[0] == 'F' else 3) for key in order}
    refused_frames = {SHADOW_REPLAY_LOCK_FRAME: ('lease', 'bookends'), SHADOW_REPLAY_NO_SUN_FRAME: ('state', 'sun_changing'), SHADOW_REPLAY_MULTISTREAM_FRAME: ('state', 'multistream')}
    kept_by_frame, far_frame, expectations = {}, -1, {}
    for frame in range(SHADOW_REPLAY_FRAMES):
        records, capped, kept = [0] * count, [0] * count, [[] for _ in range(count)]
        dropped = 0
        for key in order:
            mask, survived = masks_of(frame)[key], 0
            for c in range(count):
                if not mask >> c & 1:
                    continue
                if records[c] >= caps[c]:
                    capped[c] += 1
                else:
                    survived |= 1 << c
            for c in range(count):
                if survived >> c & 1:
                    records[c] += 1; kept[c].append(key)
            dropped += survived == 0
        issues = sum(records)
        is_off = frame in off_window
        is_refused = frame in refused_frames and not is_off  # an off frame runs no transaction, so it refuses nothing
        # The frame after an ON edge replays every cascade with casters
        # whatever the budget's alternate-frame rule says: its retained basis
        # was voided by the press.
        far_replays = not is_off and not is_refused and records[-1] > 0 and (frame in on_frames or issues <= budget or frame % 2 == 0)
        # Both edges of the A/B void every retained basis, as the Reset and a
        # refusal do: an off interval never leaves a far map to be republished.
        if frame == SHADOW_REPLAY_RESET_FRAME or is_refused or is_off or frame in on_frames:
            far_frame = -1
        if far_replays:
            far_frame = frame
        draws = [0 if is_off or is_refused or (c == count - 1 and not far_replays) else records[c] for c in range(count)]
        expectations[frame] = dict(records=records, capped=capped, dropped=dropped, issues=issues, draws=draws, far_replayed=int(far_replays), far_frame=far_frame, toggle=int(not is_off))
        kept_by_frame[frame] = kept
        c_row = by_frame[frame]
        groups = {k: v for k, v in c_row['cascades'].items() if k not in ('flip', 'period2', 'flip_untracked', 'flip_reset')}
        assert groups == {'count': count, 'records': records, 'capped': capped} and c_row['capped'] == dropped and c_row['leased'] == len(order) - dropped, (name, frame, c_row, expectations[frame])
        # Cascade-membership flips (shadow-caster-retention.md, "Membership
        # flips"): on every cascade frame line. The first frame is seeded
        # (flip_reset=1, no counts); a counted frame cannot flip more bits than
        # the two frames' records hold, a period-2 count is bounded by its
        # flips, and this fixture's handful of casters always fit the table.
        flip, period2 = c_row['cascades']['flip'], c_row['cascades']['period2']
        assert len(flip) == count and len(period2) == count and all(0 <= p <= f for f, p in zip(flip, period2)), (name, frame, c_row['cascades'])
        assert c_row['cascades']['flip_untracked'] == 0 and c_row['cascades']['flip_reset'] in (0, 1), (name, frame, c_row['cascades'])
        if frame == 0:
            assert c_row['cascades']['flip_reset'] == 1 and flip == [0] * count and period2 == [0] * count, (name, frame, c_row['cascades'])
        elif not c_row['cascades']['flip_reset'] and frame - 1 in expectations:
            assert all(f <= records[c] + expectations[frame - 1]['records'][c] for c, f in enumerate(flip)), (name, frame, c_row['cascades'], expectations[frame - 1])
        if is_off:
            assert frame not in depth_by_frame, (name, frame, 'the A/B off: no replay transaction and no line')
            continue
        d_row = depth_by_frame[frame]
        assert d_row['cascades'] == {'count': count, 'draws': draws, 'far_replayed': int(far_replays), 'far_frame': far_frame, 'issues': 0 if False else d_row['cascades']['issues'], 'budget': budget}, (name, frame, d_row, expectations[frame])
        # issues counts the quiet leased records' cascades: the Lock frame's refused record and the multistream one are not among them.
        expected_issues = issues - (sum(1 for c in range(count) if masks_of(frame)[('A', 0)] >> c & 1 and ('A', 0) in kept[c]) if frame in (SHADOW_REPLAY_LOCK_FRAME, SHADOW_REPLAY_MULTISTREAM_FRAME) else 0)
        assert d_row['cascades']['issues'] == expected_issues, (name, frame, d_row, expected_issues)
        assert d_row['draws'] == len(order) - dropped and d_row['replayed'] == (0 if is_refused else d_row['draws']), (name, frame, d_row)
    assert [(r['reason'], r['detail'], int(r['frame'])) for r in refused] == [(v[0], v[1], k) for k, v in sorted(refused_frames.items()) if k not in off_window], (name, refused)
    # The Reset's targets are recreated by the next transaction; inside an off
    # window that is the first frame back on.
    recreated = next(f for f in range(SHADOW_REPLAY_RESET_FRAME, SHADOW_REPLAY_FRAMES) if sum(expectations[f]['draws']))
    assert [(t['frame'], t['allocations'], t['size']) for t in targets] == [(0, 1, max(sizes)), (recreated, 2, max(sizes))], (name, targets)
    # The maps.
    maps = {}
    for l in text.splitlines():
        if l.startswith('SHADOW_CASCADE_MAP '):
            m = fields(l); maps[(int(m['frame']), int(m['cascade']))] = m
    assert sorted(maps) == [(f, c) for f in range(SHADOW_REPLAY_FRAMES) for c in range(count)], (name, sorted(maps)[:4])
    cameras = {int(fields(l)['frame']): fields(l) for l in text.splitlines() if l.startswith('SHADOW_CAMERA ')}
    suns = {int(fields(l)['frame']): fields(l) for l in text.splitlines() if l.startswith('SHADOW_SUN ')}
    draws = {}
    for l in text.splitlines():
        if l.startswith('SHADOW_DRAW '):
            f = fields(l); draws.setdefault(int(f['frame']), []).append({'caster': int(f['caster']), 'shape': f['shape'], 't': float(f['t']), 'p': float(f['p']), 'zo': float(f['zo'])})
    comparisons, previous = {}, {}
    for frame in range(SHADOW_REPLAY_FRAMES):
        e = expectations[frame]
        for c in range(count):
            m = maps[(frame, c)]
            data = (directory / f'shadow_{frame}_c{c}.r32f').read_bytes() if m['available'] == '1' else None
            replayed_now = e['draws'][c] > 0
            valid = replayed_now or (c == count - 1 and e['far_frame'] >= 0)
            # Only an off window may lose the surface itself: the Reset inside
            # it releases the maps and no transaction recreates them.
            assert (m['available'] == '1' or (frame in off_window and frame >= SHADOW_REPLAY_RESET_FRAME)) and (m['valid'] == '1') == valid, (name, frame, c, m, e)
            if m['available'] == '1':
                assert int(m['width']) == sizes[c], (name, frame, c, m)
            if valid:
                assert int(float(m['replayed_frame'])) == (frame if replayed_now else e['far_frame']), (name, frame, c, m, e)
            if frame in off_window:
                # No map was cleared or drawn: the content is the last replay's,
                # or gone with the Reset's release.
                assert data is None or (c in previous and data == previous[c]), f'{name}: frame {frame} cascade {c} changed with the A/B off'
                comparisons[f'{frame}/{c}'] = {'toggled_off': True, 'valid': valid, 'available': m['available'] == '1'}
                continue
            if not replayed_now:
                # Refused, or the far cascade skipped by the budget: the map is untouched.
                assert c in previous and data == previous[c], f'{name}: frame {frame} cascade {c} changed without a replay'
                comparisons[f'{frame}/{c}'] = {'unchanged': True, 'valid': valid}
                continue
            previous[c] = data
            triple = lambda key: tuple(float(v) for v in m[key].split(','))
            basis = {'right': triple('right'), 'up': triple('up'), 'forward': triple('forward'), 'center': triple('center'), 'extent': float(m['extent']),
                     'depth_light': float(m['depth_light']), 'depth_behind': float(m['depth_behind'])}
            expected_sun = tuple(float(v) for v in suns[0]['direction'].split(','))  # the latch keeps frame 0's first constant
            if env.get('X3M_FIXTURE_SHADOW_POLL') != 'agree':  # agree: the basis is the polled light's (validate_shadow_replay_poll)
                assert all(abs(-basis['forward'][i] - expected_sun[i]) < 1e-5 for i in range(3)), (name, frame, c, basis['forward'])
            assert (basis['extent'], basis['depth_light'], basis['depth_behind']) == (extents[c], 2.0 * extents[-1], 2.0 * extents[c]), (name, frame, c, basis)
            cam = cameras[frame]
            camera = {'m00': float(cam['m00']), 'm11': float(cam['m11']), 'r': [float(v) for v in cam['r'].split(',')], 't': [float(v) for v in cam['t'].split(',')]}
            kept = [d for d in draws[frame] if (d['shape'], d['caster']) in kept_by_frame[frame][c]]
            assert len(kept) == e['draws'][c], (name, frame, c, len(kept), e)
            comparison = depth_replay.compare_map(struct.unpack(f'<{sizes[c] * sizes[c]}f', data), kept, camera, basis, sizes[c])
            assert comparison['ok'] and comparison['covered_cpu'] >= 1, (name, frame, c, comparison)
            comparisons[f'{frame}/{c}'] = comparison
    sun = validate_shadow_replay_sun(name, trace, len(order), False, tuple(float(v) for v in suns[0]['direction'].split(',')))
    sun_point = validate_shadow_replay_poll(name, text, trace, env.get('X3M_FIXTURE_SHADOW_POLL'), count, sizes, cameras, maps, len(order))
    case = {'checks': checks + 6 + 6 * SHADOW_REPLAY_FRAMES + 3 * SHADOW_REPLAY_FRAMES * count + 1 + 4 * SHADOW_REPLAY_FRAMES + (3 + len(off_window) if toggle else 0),
            'toggle': {'off_frames': sorted(off_window), 'on_frames': sorted(on_frames)} if toggle else None, 'sun_point': sun_point, 'depth': True, 'taa': taa, 'casters': casters, 'cascades': count, 'extents': extents, 'sizes': sizes, 'caps': caps, 'budget': budget,
            'frames': SHADOW_REPLAY_FRAMES, 'per_frame': expectations, 'refusals': refused, 'targets': targets, 'sun': sun,
            'map': {'frames': comparisons, 'max_depth_error': max(v.get('max_depth_error', 0.0) for v in comparisons.values()), 'covered_texels': sum(v.get('covered_gpu', 0) for v in comparisons.values())}}
    case['us'] = depth_replay.us_summary(depth_rows)
    replayed_issues = [sum(r['cascades']['draws']) for r in depth_rows if r['replayed'] and sum(r['cascades']['draws'])]
    case['us']['per_issue_median'] = case['us']['median'] / (sorted(replayed_issues)[len(replayed_issues) // 2]) if replayed_issues else None
    case['us']['issues_per_frame'] = replayed_issues
    return case


def shadow_hull_radius(shape, camera):
    """The DLL's own-ship radius law for a hull drawn under the fixture's rows
    (t, p = .125): the largest AABB corner distance through the rows' linear
    part in view units (x / m00, y / m11, w = .125 x)."""
    vertices = depth_replay.shape_vertices(shape)
    lo = [min(v[i] for v in vertices) for i in range(3)]; hi = [max(v[i] for v in vertices) for i in range(3)]
    best = 0.0
    for corner in range(8):
        x, y = (hi[0] if corner & 1 else lo[0]), (hi[1] if corner & 2 else lo[1])
        best = max(best, math.sqrt((x / camera['m00']) ** 2 + (y / camera['m11']) ** 2 + (.125 * x) ** 2))
    return best


def shadow_ladder_extents(extents, e0, ratio):
    """The DLL's sliding ladder (shadow_cascade_adapt_c0) for a configured set
    and a cascade-0 extent: the live extents of every cascade and the active
    mask (cascade i kept when its extent is above cascade 0's and, the last one
    aside, below the next cascade's). E0 at the configured value: the
    configured set."""
    count = len(extents)
    live = list(extents)
    live[0] = e0
    if e0 > extents[0]:
        for i in range(1, count - 1):
            live[i] = min(max(extents[i], e0 * ratio ** i), extents[-1])
    active = 1
    for i in range(1, count):
        if live[i] > live[0] and (i + 1 == count or live[i] < live[i + 1]):
            active |= 1 << i
    return live, active


def shadow_ladder_policy(extents, live, active, static_from):
    """The DLL's slid policies (shadow_cascade_ladder_policy) for a live set:
    each live cascade's match (the configured cascade its extent is closest to
    in ratio, a tie to the larger), the first static-only live cascade (the
    first active one matching a configured static-only cascade, None for none)
    and the caps (the match's, 0 for a dropped cascade; the fixture's caps
    are all equal, so no scaling applies). An unslid set keeps its own."""
    count = len(extents)
    if live == list(extents):
        return list(range(count)), static_from, [8 if active >> i & 1 or True else 0 for i in range(count)]
    matched = []
    for e in live:
        best = min(range(count), key=lambda j: (max(e / extents[j], extents[j] / e), -j))
        matched.append(best)
    first = next((i for i in range(1, count) if active >> i & 1 and static_from is not None and matched[i] >= static_from), None)
    return matched, first, [8 if active >> i & 1 else 0 for i in range(count)]


def validate_shadow_replay_adaptive(name, text, trace, directory, env, taa):
    """The own-ship-adaptive cascade 0 and the sliding ladder behind it
    (shadow-cascade-extents.md, section 5) through the DLL: the
    shadow_cascade_set events (one on the first frame's node, one when the
    hull's first measured radius commits, one more at the swap), E0 = max(8,
    K x radius) with every cascade's live extent, the ladder's active mask and
    slid/changed masks on every line, every changed cascade re-anchored at the
    commit (its readback extent moves, its basis is void, its map untouched
    until the next replay) and every other cascade keeping its map, a dropped
    cascade never replayed, and every replayed map against the CPU projection
    of the draws its cascade kept (the hulls included)."""
    assert 'RESULT PASS' in text, f'{name}: fixture failed'
    checks = int(fields(next(l for l in text.splitlines() if l.startswith('RESULT PASS')))['checks'])
    mode = env['X3M_FIXTURE_OWN_SHIP']
    casters = int(env.get('X3M_FIXTURE_SHADOW_CASTERS', '2'))
    extents = [float(v) for v in env['X3M_FIXTURE_SHADOW_CASCADES'].split(',')]
    count = len(extents)
    sizes = [int(v) for v in env['X3M_SHADOW_CASCADE_SIZES'].split(',')]
    sizes = sizes * len(extents) if len(sizes) == 1 else sizes  # one value, or one per cascade (the ladder cases: finer far maps, the far object at 256 texels is a texel)
    size = sizes[0]
    k = float(env['X3M_SHADOW_CASCADE_ADAPTIVE_C0'])
    ratio = float(env.get('X3M_SHADOW_CASCADE_LADDER_RATIO', SHADOW_ADAPTIVE_RATIO))
    assert extents in ([8.0, 48.0, 240.0, 800.0], [8.0, 48.0, 240.0, 1200.0, 4800.0]) and k == SHADOW_ADAPTIVE_K, (name, 'the script is written for the narrowed set R and its five-cascade extension')
    big = {'small': 'H2', 'big': 'H2', 'swap': 'H2', 'reuse': 'H2', 'corvette': 'H3', 'shrink': 'H3', 'destroyer': 'H4'}[mode]
    tl = trace.splitlines()
    mode_line = [fields(l) for l in tl if l.startswith('shadow_cascades_mode ')]
    assert len(mode_line) == 1 and (mode_line[0]['enabled'], mode_line[0]['cascades'], float(mode_line[0]['adaptive_c0']), float(mode_line[0]['ladder_ratio'])) == ('1', str(count), k, ratio), (name, mode_line)
    script_mode = fields(next(l for l in text.splitlines() if l.startswith('SHADOW_MODE ')))
    assert script_mode['own_ship'] == mode and script_mode['big_hull'] == big, (name, script_mode)
    cameras = {int(fields(l)['frame']): fields(l) for l in text.splitlines() if l.startswith('SHADOW_CAMERA ')}
    camera_of = lambda cam: {'m00': float(cam['m00']), 'm11': float(cam['m11']), 'r': [float(v) for v in cam['r'].split(',')], 't': [float(v) for v in cam['t'].split(',')]}
    radius = {h: shadow_hull_radius(h, camera_of(cameras[0])) for h in ('H1', big)}
    assert 1.5 < radius['H1'] < 2.0 and {'H2': 30.0, 'H3': 14.0, 'H4': 175.0}[big] < radius[big] < {'H2': 40.0, 'H3': 15.0, 'H4': 190.0}[big], (name, radius)  # about 1.69, 33.7, 14.5, 182 under the script's camera
    own_lines = {int(fields(l)['frame']): fields(l) for l in text.splitlines() if l.startswith('SHADOW_OWN_SHIP ')}
    own = {f: l['hull'] for f, l in own_lines.items()}
    def big_named(f):
        return mode in ('big', 'corvette', 'destroyer') or (mode == 'swap' and f >= SHADOW_ADAPTIVE_SWAP_FRAME) or (mode == 'shrink' and f < SHADOW_ADAPTIVE_SWAP_FRAME)
    assert sorted(own) == list(range(SHADOW_REPLAY_FRAMES)) and all(own[f] == (big if big_named(f) else 'H1') for f in own), (name, own)
    assert all((own_lines[f]['part'] == '1') == (mode == 'reuse' and f < SHADOW_ADAPTIVE_REUSE_FRAME) for f in own), (name, own_lines)
    # The law: E0 of a frame is what the boundary before it committed; the first
    # frame of a ship measures nothing (its extents are read at that scene end)
    # and commits nothing; its first measured frame commits the ship at once.
    # The ladder slides with E0 (shadow_ladder_extents): a frame's live set.
    def e0_of(hull):
        return max(extents[0], k * radius[hull])
    # The ship's measured radius per frame: its hull, or in `reuse` both hulls while H2 is its part (frames < 3), H1 alone after.
    def measured(f):
        return radius['H2'] if mode == 'reuse' and f < SHADOW_ADAPTIVE_REUSE_FRAME else radius[own[f]]
    def set_of(e0):
        return shadow_ladder_extents(extents, e0, ratio)
    expected_events, e0_by_frame, committed = [], {}, extents[0]
    hull_seen = None
    for f in range(SHADOW_REPLAY_FRAMES):
        e0_by_frame[f] = committed
        if own[f] != hull_seen and f >= 1:  # frame 0: no extent known yet, held
            hull_seen = own[f]
            committed = max(extents[0], k * measured(f)); expected_events.append((f, 'node', measured(f), committed))
    live_by_frame = {f: set_of(e0_by_frame[f])[0] for f in e0_by_frame}
    active_by_frame = {f: set_of(e0_by_frame[f])[1] for f in e0_by_frame}
    static_from = int(env['X3M_SHADOW_CASCADE_STATIC_FROM']) if 'X3M_SHADOW_CASCADE_STATIC_FROM' in env else None
    policy_by_frame = {f: shadow_ladder_policy(extents, live_by_frame[f], active_by_frame[f], static_from) for f in live_by_frame}
    static_mask_of = lambda f: sum(1 << c for c in range(count) if policy_by_frame[f][1] is not None and c >= policy_by_frame[f][1])
    def changed_mask(f):  # the cascades whose extent or active bit the boundary ending frame f moved (a dropped-and-restored cascade re-anchors too)
        after, active_after = live_by_frame.get(f + 1, live_by_frame[f]), active_by_frame.get(f + 1, active_by_frame[f])
        return sum(1 << c for c in range(count) if after[c] != live_by_frame[f][c]) | (active_after ^ active_by_frame[f])
    events = [fields(l) for l in tl if l.startswith('shadow_cascade_set ')]
    commits = [(int(e['frame']), e['reason'], float(e['own_radius']), float(e['e0'])) for e in events if e['reason'] != 'capture']
    assert len(commits) == len(expected_events) and all(a[:2] == b[:2] and abs(a[2] - b[2]) < 1e-3 and abs(a[3] - b[3]) < 1e-3 for a, b in zip(commits, expected_events)), (name, commits, expected_events)
    last_changed = 0
    for e in events:  # every line: the live extents and active mask of its E0, the apply slots (the active cascades in order: each seam blends into the next active), the slid mask, the texel of its E0 at the map size, cascade 0's depth behind 2 E0 (the unchecked law)
        live, mask = set_of(float(e['e0']))
        printed = [float(v) for v in e['extents'].split(',')]
        assert len(printed) == count and all(abs(a - b) < 1e-3 * max(1.0, abs(b)) for a, b in zip(printed, live)), (name, e, live)
        assert int(e['active_mask']) == mask and e['apply_slots'] == ','.join(str(c) for c in range(count) if mask >> c & 1), (name, e)
        assert int(e['slid']) == sum(1 << c for c in range(count) if abs(live[c] - extents[c]) > 1e-6) and float(e['ratio']) == ratio and float(e['k']) == k, (name, e)
        f = int(e['frame'])
        if e['reason'] != 'capture':
            last_changed = changed_mask(f)
        assert int(e['changed']) == last_changed, (name, e, last_changed)  # a capture line repeats the last commit's mask
        matched, first_static, caps = shadow_ladder_policy(extents, live, mask, static_from)
        assert [int(v) for v in e['caps'].split(',')] == caps and e['static_from'] == (str(first_static) if first_static is not None else 'none') and float(e['large_min']) == 0.0, (name, e, caps, first_static)
        assert abs(float(e['texel0']) - 2 * float(e['e0']) / size) < 1e-6 and abs(float(e['depth_behind0']) - 2 * float(e['e0'])) < 1e-3, (name, e)
        # the frame the line closes: its measured radius (0 on frame 0, whose extents are read at that scene end; both hulls are known from frame 1, so the swap measures at once)
        assert abs(float(e['frame_radius']) - (0.0 if f == 0 else measured(f))) < 1e-3, (name, e)
        assert int(e['own_walk_deferred']) == 0 and int(e['own_draws']) == (0 if f == 0 else 2 if mode == 'reuse' and f < SHADOW_ADAPTIVE_REUSE_FRAME else 1), (name, e)
    # Frame 0 measured nothing (its extents are read at that scene end): E0 held once before the first commit. In `reuse` the
    # capture window covers the frames from the reuse frame on: the measured radius drops to H1's, pends under the hysteresis
    # (pending_frames counting up, below the 8 that would commit) and the committed E0 and radius stay H2's: no inflation, no re-anchor.
    captures = {int(e['frame']): e for e in events if e['reason'] == 'capture'}
    assert int(events[0]['held_frames']) == 1 and int(events[0]['frame']) == 1, (name, events[0])
    if mode == 'reuse':  # the capture window opens the frame after the Reset frame (a Reset clears an open window)
        assert sorted(captures) == list(range(SHADOW_ADAPTIVE_REUSE_FRAME + 1, SHADOW_REPLAY_FRAMES)), (name, sorted(captures))
        for f, e in captures.items():
            assert abs(float(e['pending_radius']) - radius['H1']) < 1e-3 and int(e['pending_frames']) == f - SHADOW_ADAPTIVE_REUSE_FRAME + 1 and abs(float(e['e0']) - e0_of('H2')) < 1e-3 and abs(float(e['own_radius']) - radius['H2']) < 1e-3, (name, e)
    # Per frame: the counter and replay lines, the maps and their bases.
    depth_rows, refused, targets = depth_replay.parse_text(trace)
    candidate_rows, _ = candidates_analysis.parse_text(trace)
    by_frame = {r['frame']: r for r in candidate_rows}; depth_by_frame = {r['frame']: r for r in depth_rows}
    assert sorted(by_frame) == list(range(SHADOW_REPLAY_FRAMES)) and sorted(depth_by_frame) == list(range(SHADOW_REPLAY_FRAMES)), (name, sorted(by_frame), sorted(depth_by_frame))
    refused_frames = {SHADOW_REPLAY_LOCK_FRAME: ('lease', 'bookends'), SHADOW_REPLAY_NO_SUN_FRAME: ('state', 'sun_changing'), SHADOW_REPLAY_MULTISTREAM_FRAME: ('state', 'multistream')}
    assert [(r['reason'], r['detail'], int(r['frame'])) for r in refused] == [(v[0], v[1], f) for f, v in sorted(refused_frames.items())], (name, refused)
    # Submission order and masks: L, the casters, F, then H1 and the big hull.
    # Frame 0 (the configured set) by the origin rule: L at 256 and F at 300
    # units join the cascades whose extent reaches them, the rest at about a
    # unit join every cascade; later frames by bounds: L crosses the near box
    # and the hulls straddle it (every active cascade), F's vertices lie 225-
    # 230 units along the view x axis, which the yaw-only camera keeps at least
    # 65 degrees from the sun: F is inside every active box of extent >= 240
    # and outside every box of extent <= 150 (its sun-space offset is at least
    # 0.67 x 227 = 152 on one axis); no live extent may fall in between.
    order = [('L', casters)] + [(('A', 'B')[i & 1], i) for i in range(casters)] + [('F', casters + 1), ('H1', casters + 4), (big, casters + 5)]
    for f in live_by_frame:
        assert not any(150.0 < e < 240.0 for e in live_by_frame[f]), (name, f, live_by_frame[f], 'an extent the far object F could straddle')
    assert not any(250.0 <= e <= 310.0 for e in extents), (name, extents, 'an extent the origin rule could straddle')
    def masks_of(frame):
        active, live = active_by_frame[frame], live_by_frame[frame]
        out = {}
        for key in order:
            if frame == 0 and key[0] in ('L', 'F'):
                out[key] = sum(1 << c for c in range(count) if extents[c] >= (256.0 if key[0] == 'L' else 300.0))
            elif key[0] == 'F':
                out[key] = sum(1 << c for c in range(count) if active >> c & 1 and live[c] >= 240.0)
            else:
                out[key] = active
        return out
    maps = {}
    for l in text.splitlines():
        if l.startswith('SHADOW_CASCADE_MAP '):
            m = fields(l); maps[(int(m['frame']), int(m['cascade']))] = m
    assert sorted(maps) == [(f, c) for f in range(SHADOW_REPLAY_FRAMES) for c in range(count)], (name, sorted(maps)[:4])
    suns = {int(fields(l)['frame']): fields(l) for l in text.splitlines() if l.startswith('SHADOW_SUN ')}
    draws = {}
    for l in text.splitlines():
        if l.startswith('SHADOW_DRAW '):
            d = fields(l); draws.setdefault(int(d['frame']), []).append({'caster': int(d['caster']), 'shape': d['shape'], 't': float(d['t']), 'p': float(d['p']), 'zo': float(d['zo'])})
    comparisons, previous, expectations, coarse_maps = {}, {}, {}, []
    for frame in range(SHADOW_REPLAY_FRAMES):
        masks = masks_of(frame)
        # A static-only cascade (the slid mask) refuses every draw of this script (all move, or are first sightings): its records are
        # the refusals, and a draw refused in every cascade it met (F, in the far cascades alone) is not leased at all.
        static_mask = static_mask_of(frame)
        refused_static = [sum(1 for key in order if masks[key] >> c & 1) if static_mask >> c & 1 else 0 for c in range(count)]
        for key in order:
            masks[key] &= ~static_mask
        records = [sum(1 for key in order if masks[key] >> c & 1) for c in range(count)]
        leased = sum(1 for key in order if masks[key])
        is_refused = frame in refused_frames
        c_row, d_row = by_frame[frame], depth_by_frame[frame]
        assert c_row['cascades']['count'] == count and c_row['cascades']['records'] == records and c_row['cascades']['capped'] == [0] * count and c_row['leased'] == leased, (name, frame, c_row, records, leased)
        assert ('static_only_refused' in c_row['cascades']) == (static_from is not None) and c_row['cascades'].get('static_only_refused', refused_static) == refused_static, (name, frame, c_row['cascades'], refused_static)
        draws_expected = [0 if is_refused else records[c] for c in range(count)]
        far_replays = not is_refused and records[-1] > 0
        assert d_row['cascades']['draws'] == draws_expected and d_row['cascades']['far_replayed'] == int(far_replays) and d_row['cascades']['budget'] == int(env['X3M_SHADOW_CASCADE_BUDGET']), (name, frame, d_row, draws_expected)
        assert d_row['replayed'] == (0 if is_refused else leased), (name, frame, d_row)
        # The readback follows the Present, so it sees the set after the boundary
        # that ends this frame: on a commit frame every changed cascade reads back
        # with its new extent and a void basis (its map still holds this frame's
        # replay); the cascades the commit left alone keep their basis and map.
        live_next = live_by_frame.get(frame + 1, live_by_frame[frame])
        changed = changed_mask(frame)
        expectations[frame] = dict(e0=e0_by_frame[frame], extents=live_by_frame[frame], active=active_by_frame[frame], records=records, draws=draws_expected,
                                   voided_at_boundary=changed != 0, changed_at_boundary=changed)
        for c in range(count):
            m = maps[(frame, c)]
            voided = changed >> c & 1 != 0
            valid = not is_refused and records[c] > 0 and not voided
            assert m['available'] == '1' and int(m['width']) == sizes[c] and (m['valid'] == '1') == valid, (name, frame, c, m, valid)
            data = (directory / f'shadow_{frame}_c{c}.r32f').read_bytes()
            assert abs(float(m['extent']) - live_next[c]) < 1e-3 and abs(float(m['depth_behind']) - 2.0 * live_next[c]) < 1e-3, (name, frame, c, m, live_next)
            if not valid:
                if voided and not is_refused and records[c] > 0:
                    previous[c] = data; comparisons[f'{frame}/{c}'] = {'voided_at_boundary': True}; continue
                # Refused, or dropped by the ladder: the map is untouched (the
                # Reset before frame 4 recreates it cleared: the new baseline).
                if frame == SHADOW_REPLAY_RESET_FRAME:
                    previous[c] = data
                elif c in previous:
                    assert data == previous[c], f'{name}: frame {frame} cascade {c} changed without a replay'
                comparisons[f'{frame}/{c}'] = {'unchanged': True, 'dropped': not active_by_frame[frame] >> c & 1}
                continue
            previous[c] = data
            assert int(float(m['replayed_frame'])) == frame, (name, frame, c, m)
            triple = lambda key: tuple(float(v) for v in m[key].split(','))
            basis = {'right': triple('right'), 'up': triple('up'), 'forward': triple('forward'), 'center': triple('center'), 'extent': float(m['extent']),
                     'depth_light': float(m['depth_light']), 'depth_behind': float(m['depth_behind'])}
            expected_sun = tuple(float(v) for v in suns[0]['direction'].split(','))
            assert all(abs(-basis['forward'][i] - expected_sun[i]) < 1e-5 for i in range(3)), (name, frame, c, basis['forward'])
            e = live_by_frame[frame][c]
            assert abs(basis['extent'] - e) < 1e-3 and basis['depth_light'] == 2.0 * extents[-1] and abs(basis['depth_behind'] - 2.0 * e) < 1e-3, (name, frame, c, basis, e)
            kept = [d for d in draws[frame] if masks[(d['shape'], d['caster'])] >> c & 1]
            assert len(kept) == records[c], (name, frame, c, len(kept), records)
            comparison = depth_replay.compare_map(struct.unpack(f'<{sizes[c] * sizes[c]}f', data), kept, camera_of(cameras[frame]), basis, sizes[c])
            # A far map at the fixture scale (texel above 2 units: 1,200 / 4,800 at 1,024 / 2,048) can hold the thin
            # footprints (the planes stand nearly edge-on to the sun) in edge texels alone: coverage must still agree
            # texel for texel with nothing to compare in depth; such a map is recorded as coarse.
            coarse = not comparison['ok'] and comparison['compared'] == 0 and comparison['coverage_disagreements'] == 0 and comparison['finite'] \
                and abs(comparison['covered_cpu'] - comparison['covered_gpu']) <= max(1, depth_replay.COVERAGE_TOLERANCE * comparison['covered_cpu']) and 2.0 * e / sizes[c] > 2.0
            assert (comparison['ok'] or coarse) and comparison['covered_cpu'] >= 1, (name, frame, c, comparison)
            if coarse:
                comparison['coarse'] = True; coarse_maps.append(f'{frame}/{c}')
            comparisons[f'{frame}/{c}'] = comparison
    # The commits re-anchor the changed cascades alone: on the first frame under a new set the untouched cascades replay as on any frame.
    voided_frames = [f for f in expectations if expectations[f]['voided_at_boundary']]
    if mode == 'reuse':
        assert voided_frames == [1], (name, 'the reused address never re-anchors')
    if mode in ('swap', 'shrink'):
        first = SHADOW_ADAPTIVE_SWAP_FRAME + 1
        after = big if mode == 'swap' else 'H1'
        assert expectations[first]['e0'] == e0_of(after) and all((maps[(first, c)]['valid'] == '1') == (active_by_frame[first] >> c & 1 != 0) for c in range(count)), (name, expectations[first])
        assert voided_frames == ([SHADOW_ADAPTIVE_SWAP_FRAME] if mode == 'swap' else [1, SHADOW_ADAPTIVE_SWAP_FRAME]), (name, voided_frames)
    if mode == 'shrink':  # the fighter commit returns the configured set in one commit: cascades 0-3 re-anchored, the last (the ceiling, never slid) is not in the changed mask
        assert expectations[SHADOW_ADAPTIVE_SWAP_FRAME]['changed_at_boundary'] == (1 << (count - 1)) - 1 and live_by_frame[first] == extents and active_by_frame[first] == (1 << count) - 1, (name, expectations[SHADOW_ADAPTIVE_SWAP_FRAME])
    if mode in ('corvette', 'destroyer'):  # the brief's sets at the fixture scale (1/31.25): 675 / 3,375 / 16,875 / 84,375 / 150,000 and 6,000 / 30,000 / (150,000 dropped) x2 / 150,000
        e0 = e0_of(big)
        want = [e0, 5 * e0, 25 * e0, min(125 * e0, extents[-1]), extents[-1]] if mode == 'corvette' else [e0, 5 * e0, extents[-1], extents[-1], extents[-1]]
        assert all(abs(a - b) < 1e-6 for a, b in zip(live_by_frame[2], want)) and active_by_frame[2] == (31 if mode == 'corvette' else 19) and voided_frames == [1], (name, live_by_frame[2], want, voided_frames)
        assert all(maps[(3, c)]['valid'] == ('1' if active_by_frame[3] >> c & 1 and not static_mask_of(3) >> c & 1 else '0') for c in range(count)), (name, 'every kept cascade replays on the first unrefused frame under the slid set (frame 2 is the lease-refused frame), a dropped or static-only (nothing static here) one never')
        if static_from is not None:  # the slid static mask: cascades 2-4 (542 matches the configured 1,200) from frame 2, the configured 3-4 before
            assert policy_by_frame[0][1] == static_from and policy_by_frame[3][1] == 2 and policy_by_frame[3][0] == [1, 2, 3, 4, 4], (name, policy_by_frame[3])
        if mode == 'corvette':
            assert all(live_by_frame[2][c + 1] / live_by_frame[2][c] <= ratio + 1e-6 for c in range(count - 1)), (name, 'no gap wider than the ratio behind cascade 0')
    sun = validate_shadow_replay_sun(name, trace, len(order), False, tuple(float(v) for v in suns[0]['direction'].split(',')))
    case = {'checks': checks + 3 + 3 * len(events) + 5 * SHADOW_REPLAY_FRAMES + 3 * SHADOW_REPLAY_FRAMES * count, 'depth': True, 'taa': taa, 'casters': casters, 'cascades': count, 'extents': extents,
            'mode': mode, 'k': k, 'radius': radius, 'events': commits, 'e0_by_frame': e0_by_frame, 'per_frame': expectations, 'refusals': refused, 'targets': targets, 'sun': sun,
            'ladder': {'ratio': ratio, 'big_hull': big, 'sizes': sizes, 'extents_by_frame': live_by_frame, 'active_by_frame': active_by_frame, 'voided_frames': voided_frames, 'coarse_maps': coarse_maps,
                       'static_from': static_from, 'static_from_by_frame': {f: policy_by_frame[f][1] for f in policy_by_frame}, 'matched_by_frame': {f: policy_by_frame[f][0] for f in policy_by_frame}},
            'frames': SHADOW_REPLAY_FRAMES, 'map': {'frames': comparisons, 'max_depth_error': max(v.get('max_depth_error', 0.0) for v in comparisons.values()),
                                                   'covered_texels': sum(v.get('covered_gpu', 0) for v in comparisons.values())}}
    case['us'] = depth_replay.us_summary(depth_rows)
    return case


def validate_shadow_retention(name, text, trace, directory, env):
    """The caster-retention script (shadow-caster-retention.md, "Fixture and
    twin cases") under one setting of the DLL. The fixture asserted the store's
    levels, counters and its own buffers' COM reference counts through the
    seam; here: every case of the script passed; the DLL's lines parse with
    their identities (none at all with the option off); every compared cascade
    map equals the CPU twin of the submitted draws plus, live only, the kept
    draws placed by the camera of the frame that recorded them, and is invalid
    when that list is empty; the Reset and teardown flushes released what was
    held before the device went; the cost fields are recorded."""
    assert 'RESULT PASS' in text, f'{name}: fixture failed'
    lines, tl = text.splitlines(), trace.splitlines()
    checks = int(fields(next(l for l in lines if l.startswith('RESULT PASS')))['checks'])
    mode = int(fields(next(l for l in lines if l.startswith('RETENTION_MODE ')))['mode'])
    assert mode == (2 if env.get('X3M_SHADOW_CASTER_RETENTION') == '1' else 1 if env.get('X3M_SHADOW_RETENTION_CENSUS') == '1' else 0), (name, mode)
    passed = [fields(l)['name'] for l in lines if l.startswith('RETENTION_CASE ') and ' state=PASS ' in l]
    assert passed == list(SHADOW_RETENTION_SCRIPT), (name, passed)
    sizes, count = [int(env['X3M_SHADOW_CASCADE_SIZES'])] * 2, 2
    extents = [float(v) for v in env['X3M_FIXTURE_SHADOW_CASCADES'].split(',')]
    frames = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('RETENTION_FRAME ')}
    assert sorted(frames) == list(range(len(frames))) and len(frames) > 1200, (name, len(frames))
    depth_rows, refused, _ = depth_replay.parse_text(trace)
    assert [r['frame'] for r in depth_rows] == sorted(frames), (name, len(depth_rows), len(frames))
    # The DLL's lines.
    retention_lines = [l for l in tl if 'shadow_retention' in l]
    frame_rows, resights, casters, flushes, replay_flags = retention_analysis.parse_lines(tl)
    case = {'mode': ('off', 'census', 'live')[mode], 'frames': len(frames), 'cases': passed}
    if mode == 0:
        assert not retention_lines and replay_flags == {'0': 0, '1': 0} and not any('retention' in r for r in depth_rows), (name, retention_lines[:2], replay_flags)
    else:
        label = case['mode']
        mode_line = [fields(l) for l in tl if l.startswith('shadow_retention_mode ')]
        device = [fields(l) for l in tl if l.startswith('shadow_retention_device ')]
        assert len(mode_line) == 1 and (mode_line[0]['enabled'], mode_line[0]['mode'], mode_line[0]['age_cap']) == ('1', label, env['X3M_SHADOW_CASTER_RETENTION_AGE']), (name, mode_line)
        assert len(device) == 1 and (device[0]['enabled'], device[0]['mode'], device[0]['reason']) == ('1', label, 'ok'), (name, device)
        assert [r['frame'] for r in frame_rows] == sorted(frames) and all(r['mode'] == label for r in frame_rows), (name, len(frame_rows))
        assert sum(1 for r in frame_rows if r['known'] == 0) == 1, (name, 'case m makes the observer unavailable for exactly one frame')
        assert len(resights) == (len(frames) - 1) // 300, (name, len(resights))
        assert all(('retention' in r) == (mode == 2) for r in depth_rows), name
        assert all(not r['refs_held'] and not r['buffer_orphaned'] for r in frame_rows) if mode == 1 else max(r['refs_held'] for r in frame_rows) >= 3, name
        # F8 frames: one shadow_retention_caster line per retained record; the live flag on every replay caster line.
        assert any(c['class'] == 'static' and int(c['unseen']) >= 1 for c in casters) and replay_flags['0'] >= 1 and (replay_flags['1'] >= 1) == (mode == 2), (name, len(casters), replay_flags)
        resets = [f for f in flushes if f['reason'] == 'reset']
        assert len(resets) == 2 and all(int(f['nodes']) == 2 and int(f['refs']) == (3 if mode == 2 else 0) for f in resets), (name, flushes)
        teardown = [i for i, l in enumerate(tl) if l.startswith('shadow_retention_flush ') and ' reason=teardown ' in l]
        destroyed = [i for i, l in enumerate(tl) if l.startswith('device_destroy ')]
        assert len(teardown) == 1 and len(destroyed) == 1 and teardown[0] < destroyed[0], (name, teardown, destroyed)
        assert fields(tl[teardown[0]])['nodes'] == '2' and fields(tl[teardown[0]])['refs'] == ('3' if mode == 2 else '0'), (name, tl[teardown[0]])
        summary = retention_analysis.summary(frame_rows, resights, float(env.get('X3M_SHADOW_CASTER_RETENTION_EPS', '0.05')))
        # Case i: the frame that fills the table evicts the reserve (8) at its scene end and the 1,025th node one more; cases c, m and i overflow the ring once each.
        assert summary['levels']['nodes_live'] + 0 <= 1024 and summary['totals']['evicted'] == 9 and summary['totals']['journal_overflow'] == 3 and summary['drift']['frames_over_eps'] == 1, (name, summary['totals'], summary['drift'])
        assert summary['totals']['refused'] == 0 and summary['totals']['release_queue_full'] == 0 and summary['totals']['revalidate_context_lost'] == 4 and summary['totals']['reclassified_after_unseen'] == 1, (name, summary['totals'])
        assert (summary['totals']['far_alternate_due_to_retained'] >= 1) == (mode == 2) and summary['totals']['admitted_checked'] >= (7000 if mode == 2 else 1), (name, summary['totals'])
        poll = env.get('X3M_FIXTURE_SHADOW_POLL') == 'agree'
        assert summary['flushes'] == {'epoch': 2, 'reset': 2, 'device': 1, 'teardown': 0, 'sun': 2 if poll else 1, 'observer': 2, 'idle': 0}, (name, summary['flushes'])
        sources = [(int(fields(l)['frame']), fields(l)['source'], fields(l)['reason']) for l in tl if l.startswith('shadow_replay_sun_source ')]
        if poll:
            switch = int(fields(next(l for l in lines if l.startswith('RETENTION_SOURCE_SWITCH ')))['frame'])
            assert sources[:2] == [(0, 'point', 'point'), (switch, 'latch', 'unavailable')], (name, sources[:3], switch)
            case['source_switch_frame'] = switch
        else:
            assert sources == [(0, 'latch', 'unavailable')], (name, sources)
        full = [r for r in frame_rows if r['nodes_live'] + r['nodes_unseen'] >= 1000]
        case['retention'] = {'summary': summary, 'full_store_frames': len(full), 'full_store_us_median': sorted(r['us'] for r in full)[len(full) // 2] if full else None,
                             'full_store_walk_us_max': max((r['walk_us'] for r in full), default=None), 'overflow_frame_us': max(r['us'] for r in frame_rows if r['journal_overflow']),
                             'burst_frame': next(({'retired': r['retired'], 'us': r['us'], 'journal_us': r['journal_us']} for r in frame_rows if r['retired'] >= 590), None)}
        assert case['retention']['burst_frame'] is not None, name
    assert len([i for i, l in enumerate(tl) if l.startswith('device_destroy ')]) == 1, f'{name}: the device was not destroyed'
    case['failed_reset'] = fields(next(l for l in lines if l.startswith('RETENTION_FAILED_RESET ')))['result']
    case['lock_drop_frames'] = int(fields(next(l for l in lines if l.startswith('RETENTION_LOCK_DROP ')))['frames'])
    orphan = fields(next(l for l in lines if l.startswith('RETENTION_ORPHAN_DROP ')))
    case['orphan_drop_frames'], case['orphan_probe'] = int(orphan['frames']), int(orphan['orphan_probe'])
    # The maps against the twin.
    maps, cameras, suns, draws, kept = {}, {}, {}, {}, {}
    for l in lines:
        if l.startswith('SHADOW_CASCADE_MAP '):
            m = fields(l); maps[(int(m['frame']), int(m['cascade']))] = m
        elif l.startswith('SHADOW_CAMERA '):
            cameras[int(fields(l)['frame'])] = fields(l)
        elif l.startswith('SHADOW_SUN '):
            suns[int(fields(l)['frame'])] = fields(l)
        elif l.startswith('SHADOW_DRAW ') or l.startswith('RETENTION_KEPT '):
            f = fields(l); d = {'caster': int(f['caster']), 'shape': f['shape'], 't': float(f['t']), 'p': float(f['p']), 'zo': float(f['zo'])}
            if l.startswith('RETENTION_KEPT '):
                d['camera'] = {'m00': float(f['m00']), 'm11': float(f['m11']), 'r': [float(v) for v in f['r'].split(',')], 't': [float(v) for v in f['ct'].split(',')]}
                d['placed_frame'] = int(f['placed_frame'])
            (kept if 'camera' in d else draws).setdefault(int(f['frame']), []).append(d)
    compared = sorted(f for f, row in frames.items() if row['compare'] == '1')
    comparisons, absent, retained_frames = {}, 0, 0
    for frame in compared:
        expected = draws.get(frame, []) + (kept.get(frame, []) if mode == 2 else [])
        retained_frames += bool(mode == 2 and kept.get(frame))
        row = depth_rows[frame]
        if mode == 2:
            assert sum(row['retention']['retained']) == 2 * len(kept.get(frame, [])), (name, frame, row, kept.get(frame))
        for c in range(count):
            m = maps[(frame, c)]
            if not expected:
                assert m['valid'] == '0', (name, frame, c, m); absent += 1
                continue
            assert m['available'] == '1' and m['valid'] == '1' and int(float(m['replayed_frame'])) == frame and int(m['width']) == sizes[c], (name, frame, c, m)
            data = (directory / f'shadow_{frame}_c{c}.r32f').read_bytes()
            triple = lambda key: tuple(float(v) for v in m[key].split(','))
            basis = {'right': triple('right'), 'up': triple('up'), 'forward': triple('forward'), 'center': triple('center'), 'extent': float(m['extent']),
                     'depth_light': float(m['depth_light']), 'depth_behind': float(m['depth_behind'])}
            expected_sun = tuple(float(v) for v in suns[frame]['direction'].split(','))
            # The point source: the fixture prints the direction from the camera position to the light, the DLL's is from the
            # cascade centre (snapped) or, after the switch, the latch of the draws' own directions: a few units apart at 100,000+.
            tolerance = 1e-4 if env.get('X3M_FIXTURE_SHADOW_POLL') == 'agree' else 1e-5
            assert all(abs(-basis['forward'][i] - expected_sun[i]) < tolerance for i in range(3)) and basis['extent'] == extents[c], (name, frame, c, basis, expected_sun)
            cam = cameras[frame]
            camera = {'m00': float(cam['m00']), 'm11': float(cam['m11']), 'r': [float(v) for v in cam['r'].split(',')], 't': [float(v) for v in cam['t'].split(',')]}
            comparison = depth_replay.compare_map(struct.unpack(f'<{sizes[c] * sizes[c]}f', data), expected, camera, basis, sizes[c])
            # The unit-size casters are smaller than a texel of the 400-unit cascade (3.1 units): there the twin may
            # cover no sample at all, and then the map must not either; cascade 0 (0.0625-unit texels) carries the blobs.
            sub_texel = c > 0 and comparison['covered_cpu'] == 0 and comparison['covered_gpu'] == 0 and comparison['coverage_disagreements'] == 0 and comparison['finite']
            assert (comparison['ok'] and comparison['covered_cpu'] >= 1) or sub_texel, (name, frame, c, frames[frame]['case'], comparison)
            comparisons[f'{frame}/{c}'] = {k: comparison[k] for k in ('covered_cpu', 'covered_gpu', 'coverage_disagreements', 'max_depth_error')}
    assert len(compared) >= 30 and (retained_frames >= 20) == (mode == 2), (name, len(compared), retained_frames)
    colors = [l for l in lines if l.startswith('COLOR ')]
    assert len(colors) == len(frames), (name, len(colors))
    case.update(checks=checks + len(passed) + 3 * len(compared) + 8, compared_frames=len(compared), retained_compared_frames=retained_frames, absent_maps=absent,
                color_sha256=hashlib.sha256('\n'.join(colors).encode()).hexdigest(),
                map={'max_depth_error': max((v['max_depth_error'] for v in comparisons.values()), default=0.0), 'covered_texels': sum(v['covered_gpu'] for v in comparisons.values()),
                     'coverage_disagreements': sum(v['coverage_disagreements'] for v in comparisons.values()), 'maps': len(comparisons)},
                us=depth_replay.us_summary(depth_rows))
    issue_rows = [r for r in depth_rows if r['replayed'] and sum(r['cascades']['draws']) >= 1000]
    case['us']['bulk'] = {'frames': len(issue_rows), 'issues_median': sorted(sum(r['cascades']['draws']) for r in issue_rows)[len(issue_rows) // 2] if issue_rows else 0,
                          'us_median': sorted(r['us'] for r in issue_rows)[len(issue_rows) // 2] if issue_rows else None}
    return case


def validate_shadow_pool(name, text, trace, directory, env):
    """The caster pool script (shadow-cascade-extents.md, "Caster pool
    control"): per frame the counter line's c<i>, capped<i>,
    static_only_refused<i> and leased against POOL_EXPECT, the depth line's
    draws<i> / far cadence against the kept counts and the budget, every
    compared cascade map against the CPU twin of POOL_KEPT (the casters the
    policy must keep, with their scales); importance: dropped_min_size1 > 0
    and identical on every sized frame, the kept set identical across the
    rotated orders; records: 4,096 records per frame with no overflow."""
    assert 'RESULT PASS' in text, f'{name}: fixture failed'
    lines, tl = text.splitlines(), trace.splitlines()
    checks = int(fields(next(l for l in lines if l.startswith('RESULT PASS')))['checks'])
    script = env['X3M_FIXTURE_SHADOW_POOL']
    mode_line = fields(next(l for l in lines if l.startswith('POOL_MODE ')))
    assert mode_line['script'] == script and int(mode_line['store']) == (2 if env.get('X3M_SHADOW_CASTER_RETENTION') == '1' else 1 if env.get('X3M_SHADOW_RETENTION_CENSUS') == '1' else 0), (name, mode_line)
    sizes, count = [int(env['X3M_SHADOW_CASCADE_SIZES'])] * 2, 2
    extents = [float(v) for v in env['X3M_FIXTURE_SHADOW_CASCADES'].split(',')]
    caps = [int(v) for v in env['X3M_SHADOW_CASCADE_CAPS'].split(',')]; caps = caps * (count if len(caps) == 1 else 1)
    budget = int(env['X3M_SHADOW_CASCADE_BUDGET'])
    mode = [fields(l) for l in tl if l.startswith('shadow_cascades_mode ')]
    assert len(mode) == 1 and (mode[0]['enabled'], mode[0]['reason'], mode[0]['cascades']) == ('1', 'ok', '2'), (name, mode)
    assert (mode[0]['records'], mode[0]['static_from'], mode[0]['drop_order'], float(mode[0]['large_min'])) == (env.get('X3M_SHADOW_CASCADE_RECORDS', '1024,1024') + ',1024,1024,1024',  # the mode line lists shadow_cascade_max (5) slots
                                                                                    env.get('X3M_SHADOW_CASCADE_STATIC_FROM', 'none'), env.get('X3M_SHADOW_CASCADE_DROP_ORDER', 'submission'), float(env.get('X3M_SHADOW_CASCADE_LARGE_MIN', '0'))), (name, mode)
    store_on = int(mode_line['store']) != 0
    frames = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('POOL_FRAME ')}
    expect = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('POOL_EXPECT ')}
    assert sorted(frames) == sorted(expect) == list(range(len(frames))) and len(frames) >= 6, (name, sorted(frames))
    depth_rows, refused, _ = depth_replay.parse_text(trace)
    candidate_rows, _ = candidates_analysis.parse_text(trace)
    by_frame = {r['frame']: r for r in candidate_rows}
    depth_by_frame = {r['frame']: r for r in depth_rows}
    assert sorted(by_frame) == sorted(depth_by_frame) == sorted(frames) and not refused, (name, sorted(by_frame), refused)
    static_on, importance = env.get('X3M_SHADOW_CASCADE_STATIC_FROM') is not None, env.get('X3M_SHADOW_CASCADE_DROP_ORDER') == 'importance'
    far_frame, dropped_sizes = -1, []
    for frame in sorted(frames):
        e, c_row, d_row = expect[frame], by_frame[frame], depth_by_frame[frame]
        records = [int(e['c0']), int(e['c1'])]
        capped = [0, int(e['capped1'])]
        cascades = c_row['cascades']
        assert (cascades['count'], cascades['records'], cascades['capped']) == (count, records, capped), (name, frame, c_row, e)
        # A drop from cascade 1 drops the whole draw only where the draw met no other cascade (the records script's nodes).
        # POOL_EXPECT leased= (-1: every draw) names the draws that are candidates at all: a draw refused from every
        # cascade (the cycle script's S2 on frame 0, the hull script's W before its extent is read) is not leased.
        expected_leased = int(e.get('leased', -1))
        if expected_leased < 0:
            expected_leased = int(frames[frame]['drawn']) - c_row['capped']
        assert c_row['leased'] == expected_leased and c_row['overflow'] == 0 and c_row['capped'] == (capped[1] if script == 'records' else 0), (name, frame, c_row, expected_leased)
        assert ('static_only_refused' in cascades) == static_on and ('dropped_min_size' in cascades) == importance, (name, frame, cascades)
        if static_on:
            assert cascades['static_only_refused'] == [0, int(e['static_only_refused1'])] and cascades['large_admitted'] == [0, int(e['large_admitted1'])], (name, frame, cascades, e)
            classified, drawn = cascades['classified'], int(frames[frame]['drawn'])
            assert sum(classified.values()) + cascades['class_miss'][1] == drawn and cascades['class_miss'][0] == 0, (name, frame, classified, cascades['class_miss'])
            # Frame 0: every draw misses (no anchor yet). Later the store answers the nodes it is informative about
            # (the anchor node's class is excluded from it): a verified mover from frame 2 (M and W of the static
            # script), a node promoted at its tier from frame 9 (S; the cycle script's S and S2; the jitter script's J
            # at cascade 1's tier); a fresh node or one whose streak is accruing goes to the ring, which also answers
            # everything with the store off; nothing misses after frame 0.
            if script == 'static':
                expected_store = 0 if not store_on or frame <= 1 else 2 if frame <= 8 else 3
            elif script == 'cycle':  # M2 verified moved from frame 1's scene end; S and S2 promoted at frame 8's
                expected_store = 0 if not store_on or frame <= 1 else 1 if frame <= 8 else 3
            elif script == 'jitter':
                expected_store = 1 if store_on and frame >= 9 else 0
            else:
                expected_store = drawn - 1 if store_on and frame >= 1 else 0
            assert classified == {'class_store': expected_store, 'class_ring': 0 if frame == 0 else drawn - expected_store} and cascades['class_miss'][1] == (drawn if frame == 0 else 0), (name, frame, classified)
        if importance:
            assert cascades['select_us'] >= 0, (name, frame, cascades)
            if frame >= 1:
                assert cascades['dropped_min_size'][1] > 0, (name, frame, cascades)
                dropped_sizes.append(cascades['dropped_min_size'][1])
            else:
                assert cascades['dropped_min_size'][1] == 0, (name, frame, cascades)
        issues = sum(records)
        far_replays = records[1] > 0 and (issues <= budget or frame % 2 == 0)
        if far_replays:
            far_frame = frame
        draws = [records[0], records[1] if far_replays else 0]
        assert d_row['cascades'] == {'count': count, 'draws': draws, 'far_replayed': int(far_replays), 'far_frame': far_frame, 'issues': issues, 'budget': budget}, (name, frame, d_row, records)
        assert d_row['draws'] == c_row['leased'] and d_row['replayed'] == d_row['draws'], (name, frame, d_row)
    if importance:
        assert len(set(dropped_sizes)) == 1, (name, 'dropped_min_size1 must be identical on every sized frame', dropped_sizes)
    # The maps against the twin of the kept casters.
    maps, cameras, suns, draws, kept = {}, {}, {}, {}, {}
    for l in lines:
        if l.startswith('SHADOW_CASCADE_MAP '):
            m = fields(l); maps[(int(m['frame']), int(m['cascade']))] = m
        elif l.startswith('SHADOW_CAMERA '):
            cameras[int(fields(l)['frame'])] = fields(l)
        elif l.startswith('SHADOW_SUN '):
            suns[int(fields(l)['frame'])] = fields(l)
        elif l.startswith('SHADOW_DRAW '):
            f = fields(l); draws.setdefault(int(f['frame']), []).append({'caster': int(f['caster']), 'shape': f['shape'], 't': float(f['t']), 'p': float(f['p']), 'zo': float(f['zo']), 'scale': float(f['scale']), 'w0': float(f.get('w0', 1.0))})
        elif l.startswith('POOL_KEPT '):
            f = fields(l); kept[(int(f['frame']), int(f['cascade']))] = [] if f['casters'] == '-' else [int(v) for v in f['casters'].split(',')]
    compared = sorted(f for f, row in frames.items() if row['compare'] == '1')
    comparisons, absent = {}, 0
    for frame in compared:
        cam = cameras[frame]
        camera = {'m00': float(cam['m00']), 'm11': float(cam['m11']), 'r': [float(v) for v in cam['r'].split(',')], 't': [float(v) for v in cam['t'].split(',')]}
        expected_sun = tuple(float(v) for v in suns[frame]['direction'].split(','))
        for c in range(count):
            m, ids = maps[(frame, c)], kept[(frame, c)]
            assert len(ids) == int(expect[frame][f'c{c}']) and ids == sorted(ids), (name, frame, c, ids, expect[frame])
            expected = [d for d in draws[frame] if d['caster'] in ids]
            assert len(expected) == len(ids), (name, frame, c, ids)
            if not expected or (c == count - 1 and depth_by_frame[frame]['cascades']['far_frame'] < 0):
                assert m['valid'] == '0', (name, frame, c, m); absent += 1
                continue
            replayed_now = depth_by_frame[frame]['cascades']['draws'][c] > 0
            assert m['available'] == '1' and m['valid'] == '1' and int(m['width']) == sizes[c], (name, frame, c, m)
            assert int(float(m['replayed_frame'])) == (frame if replayed_now else depth_by_frame[frame]['cascades']['far_frame']), (name, frame, c, m)
            if not replayed_now:
                comparisons[f'{frame}/{c}'] = {'retained': True}
                continue
            data = (directory / f'shadow_{frame}_c{c}.r32f').read_bytes()
            triple = lambda key: tuple(float(v) for v in m[key].split(','))
            basis = {'right': triple('right'), 'up': triple('up'), 'forward': triple('forward'), 'center': triple('center'), 'extent': float(m['extent']),
                     'depth_light': float(m['depth_light']), 'depth_behind': float(m['depth_behind'])}
            assert all(abs(-basis['forward'][i] - expected_sun[i]) < 1e-5 for i in range(3)) and basis['extent'] == extents[c], (name, frame, c, basis, expected_sun)
            comparison = depth_replay.compare_map(struct.unpack(f'<{sizes[c] * sizes[c]}f', data), expected, camera, basis, sizes[c])
            assert comparison['ok'] and comparison['covered_cpu'] >= 1, (name, frame, c, frames[frame]['case'], comparison)
            comparisons[f'{frame}/{c}'] = {k: comparison[k] for k in ('covered_cpu', 'covered_gpu', 'coverage_disagreements', 'max_depth_error')}
    if script != 'records':
        assert len(compared) == len(frames) and len(comparisons) >= len(frames), (name, len(compared), len(comparisons))
    colors = [l for l in lines if l.startswith('COLOR ')]
    assert len(colors) == len(frames), (name, len(colors))
    case = {'script': script, 'store': int(mode_line['store']), 'frames': len(frames), 'checks': checks + 4 * len(frames) + 2, 'compared_frames': len(compared), 'absent_maps': absent,
            'color_sha256': hashlib.sha256('\n'.join(colors).encode()).hexdigest(), 'per_frame': {f: {'c': by_frame[f]['cascades'], 'draws': depth_by_frame[f]['cascades']['draws']} for f in sorted(frames)},
            'map': {'max_depth_error': max((v.get('max_depth_error', 0.0) for v in comparisons.values()), default=0.0), 'covered_texels': sum(v.get('covered_gpu', 0) for v in comparisons.values()),
                    'coverage_disagreements': sum(v.get('coverage_disagreements', 0) for v in comparisons.values()), 'maps': len(comparisons)},
            'us': depth_replay.us_summary(depth_rows)}
    if importance:
        case['dropped_min_size1'] = dropped_sizes[0]
        case['select_us'] = {'median': sorted(r['cascades']['select_us'] for r in candidate_rows)[len(candidate_rows) // 2], 'max': max(r['cascades']['select_us'] for r in candidate_rows)}
    if store_on and env.get('X3M_SHADOW_RETENTION_TIMING') == '1':
        # The static gate's refused-draw sightings (run 40 A, cause 1) and their cost per sighting (the IB view, the
        # geometry queries and the store's seen path), from the retention frame lines.
        rows = [retention_analysis.parse_frame_line(l) for l in tl if l.startswith('shadow_retention_frame ')]
        sightings = sum(r['gate_sightings'] for r in rows)
        case['gate_sightings'] = sightings
        case['gate_us_per_sighting'] = (sum(r['gate_us'] for r in rows) / sightings) if sightings else None
    if static_on:
        case['classified'] = {k: sum(r['cascades']['classified'][k] for r in candidate_rows) for k in candidates_analysis.CLASS_FIELDS}
        case['class_miss'] = [sum(r['cascades']['class_miss'][i] for r in candidate_rows) for i in range(count)]
        case['large_admitted'] = [sum(r['cascades']['large_admitted'][i] for r in candidate_rows) for i in range(count)]
        if script == 'static':
            large = fields(next(l for l in lines if l.startswith('POOL_LARGE_MIN ')))
            case['large_min'] = float(large['units'])
            assert (case['large_admitted'][1] > 0) == (large['wide_admitted'] == '1'), (name, case['large_admitted'], large)
    return case


def validate_shadow_replay(name, text, trace, directory, env, taa):
    """The depth replay script: the DLL's per-frame lines against the fixture's
    script (frame 0 replays the casters, every later frame L and the casters
    but the last submitted one, which the cap drops; the READONLY-Lock frame is
    refused as a lease failure and leaves the map unchanged; the Reset recreates
    the map), the caster-candidate lines (casters by bounds: both bounds
    objects lie beyond the origin rule, so frame 0 admits the casters alone by
    the origin rule and reads every extent at its scene end; from frame 1 on L
    and the casters are admitted by bounds, F is not, one caster is capped and
    origin < bounds), the CPU projection of the
    fixture's geometry against every replayed map within one depth code, and
    the transaction time."""
    assert 'RESULT PASS' in text, f'{name}: fixture failed'
    result_line = next(l for l in text.splitlines() if l.startswith('RESULT PASS'))
    checks = int(fields(result_line)['checks'])
    mode = [fields(l) for l in text.splitlines() if l.startswith('SHADOW_MODE ')]
    assert len(mode) == 1 and mode[0]['export'] == '1', (name, mode)
    depth_on = env.get('X3M_SHADOW_REPLAY_DEPTH') == '1'
    casters = int(env.get('X3M_FIXTURE_SHADOW_CASTERS', '2'))
    sun_programs = 'X3M_FIXTURE_SHADOW_SUN_PROGRAMS' in env
    assert mode[0]['depth'] == ('1' if depth_on else '0') and int(mode[0]['casters']) == casters and mode[0]['taa'] == ('1' if taa else '0') and mode[0]['sun_programs'] == ('1' if sun_programs else '0'), (name, mode)
    color_hashes = {int(fields(l)['frame']): fields(l)['hash'] for l in text.splitlines() if l.startswith('COLOR ')}
    assert sorted(color_hashes) == list(range(SHADOW_REPLAY_FRAMES)), (name, sorted(color_hashes))
    maps = {int(fields(l)['frame']): fields(l) for l in text.splitlines() if l.startswith('SHADOW_MAP ')}
    assert sorted(maps) == list(range(SHADOW_REPLAY_FRAMES)), (name, sorted(maps))
    depth_rows, refused, targets = depth_replay.parse_text(trace)
    tl = trace.splitlines()
    mode_lines = [l for l in tl if l.startswith('shadow_replay_depth_mode ')]
    case = {'checks': checks, 'depth': depth_on, 'casters': casters, 'taa': taa, 'color_hashes': color_hashes, 'frames': SHADOW_REPLAY_FRAMES}
    if not depth_on:
        assert not depth_rows and not refused and not targets and not mode_lines, (name, len(depth_rows), len(refused), len(targets), mode_lines)
        assert all(m['available'] == '0' for m in maps.values()), (name, maps)
        case['checks'] += 4
        return case
    size = int(env['X3M_SHADOW_REPLAY_SIZE'])
    extent, depth_half, cap = float(env.get('X3M_SHADOW_REPLAY_EXTENT', '250')), float(env.get('X3M_SHADOW_REPLAY_DEPTH_HALF', '512')), int(env.get('X3M_SHADOW_REPLAY_CAP', '512'))  # the case dict; the runner's base defaults
    assert mode_lines == [f'shadow_replay_depth_mode requested=1 enabled=1 size={size} extent={extent:.9g} depth_half={depth_half:.9g} cap={cap} motion_output=1 ownership=1'], (name, mode_lines)
    device = [fields(l) for l in tl if l.startswith('shadow_replay_depth_device ')]
    assert len(device) == 1 and device[0]['attached'] == '1' and device[0]['readable'] == '1' and device[0]['map_format'] == '114' \
        and device[0]['depth_format'] in ('77', '80') and device[0]['size'] == str(size), (name, device)  # R32F, D24X8 or D16
    # The cascade the DLL used: the fixture narrowing (X3M_FIXTURE_SHADOW_EXTENT,
    # depth +-2E) or the production box from the two variables; F (origin at
    # far_t / m00, vertices 60 rows nearer) is admitted by bounds only when its
    # vertices lie inside the box.
    narrowed = env.get('X3M_FIXTURE_SHADOW_EXTENT')
    box_extent, box_depth_half = (float(narrowed), 2.0 * float(narrowed)) if narrowed else (extent, depth_half)
    far_t = float(env.get('X3M_FIXTURE_SHADOW_FAR_T', '240'))
    far_vertices = (far_t - 60.0) / 0.8
    far_admitted = far_vertices + 134.5 < box_extent and far_vertices + 134.5 < box_depth_half  # sun-space offset from the centre 128 units ahead, any sun direction
    assert far_admitted or far_vertices > box_extent, (name, 'F within a margin of the box: the verdict would depend on the camera')
    # One line per frame: frame 0 replays the casters (the origin rule, no
    # extent yet), every later frame the objects admitted by bounds in
    # submission order (L, the casters, F when inside the box) up to the cap
    # (the narrow cases' cap = casters drops the last caster), except the Lock
    # frame (lease refused, nothing replayed); the target is created for the
    # first replay and again after the Reset.
    admitted_order = [('L', casters)] + [(('A', 'B')[i & 1], i) for i in range(casters)] + ([('F', casters + 1)] if far_admitted else [])
    kept = admitted_order[:cap]
    steady_bounds, steady_draws = len(admitted_order), len(kept)
    frame0_draws = min(cap, casters)
    draws_of = lambda frame: frame0_draws if frame == 0 else steady_draws
    replayed_draws = steady_draws
    # The at-rest A/B on the single map: an off frame runs no transaction, so it
    # has no line, refuses nothing and publishes nothing (comparison-hotkeys.md).
    toggle = env.get('X3M_FIXTURE_SHADOW_TOGGLE')
    off_window = toggle_windows(name, text, tl, toggle)[0] if toggle else set()
    assert [r['frame'] for r in depth_rows] == [f for f in range(SHADOW_REPLAY_FRAMES) if f not in off_window], (name, [r['frame'] for r in depth_rows])
    assert all(r.get('shadow_toggle') == 1 for r in depth_rows), (name, 'every replay line records the state it ran under')
    refused_frames = {SHADOW_REPLAY_LOCK_FRAME, SHADOW_REPLAY_NO_SUN_FRAME, SHADOW_REPLAY_MULTISTREAM_FRAME} - off_window
    for r in depth_rows:
        draws = draws_of(r['frame'])
        assert r['draws'] == draws, (name, r, draws)
        if r['frame'] == SHADOW_REPLAY_LOCK_FRAME:
            assert (r['replayed'], r['skipped_lease'], r['skipped_state'], r['skipped_caps']) == (0, 1, 0, 0), (name, r)
        elif r['frame'] == SHADOW_REPLAY_NO_SUN_FRAME:      # a frame-level refusal counts every record
            assert (r['replayed'], r['skipped_lease'], r['skipped_state'], r['skipped_caps']) == (0, 0, draws, 0), (name, r)
        elif r['frame'] == SHADOW_REPLAY_MULTISTREAM_FRAME:  # a record-level refusal counts the offending record
            assert (r['replayed'], r['skipped_lease'], r['skipped_state'], r['skipped_caps']) == (0, 0, 1, 0), (name, r)
        else:
            assert (r['replayed'], r['skipped_lease'], r['skipped_state'], r['skipped_caps']) == (draws, 0, 0, 0) and r['us'] > 0, (name, r)
    # Casters by bounds (shadow-replay-gates.md): the counter line of every
    # frame. routed = L + casters + F, all z-writing; the origin rule admits
    # the casters alone (L's origin at 256 units, F's at 300 or beyond). Frame 0
    # knows no extent: the casters fall back to the origin rule, nothing is
    # capped, and the scene end reads every extent (casters + 2). From frame 1
    # on L and the casters are admitted by bounds (origin < bounds: the run-36
    # station direction), F only inside a wide box, the cap drops what exceeds
    # it and nothing is read again (the Reset keeps the cache: managed
    # buffers survive it).
    candidate_rows, _ = candidates_analysis.parse_text(trace)
    by_frame = {r['frame']: r for r in candidate_rows}
    routed = casters + SHADOW_REPLAY_BOUNDS_OBJECTS + (SHADOW_SUN_PROGRAM_OBJECTS if sun_programs else 0)  # G and D: routed z-writers outside every box (origins 325 and 350 units)
    assert sorted(by_frame) == list(range(SHADOW_REPLAY_FRAMES)), (name, sorted(by_frame))
    for frame, r in sorted(by_frame.items()):
        assert r['routed'] == r['zwrite'] == routed and r['origin'] == casters, (name, frame, r)
        if frame == 0:
            assert (r['bounds'], r['fallback'], r['slice0'], r['managed'], r['leased'], r['capped'], r['reads']) == (0, casters, casters, casters, frame0_draws, casters - frame0_draws, routed), (name, frame, r)
        else:
            assert (r['bounds'], r['fallback'], r['slice0'], r['managed'], r['leased'], r['capped'], r['reads']) == (steady_bounds, 0, steady_bounds, steady_bounds, steady_draws, steady_bounds - steady_draws, 0), (name, frame, r)
            assert r['origin'] < r['bounds'], (name, frame, r)
        assert r['quiet'] == draws_of(frame) - (1 if frame == SHADOW_REPLAY_LOCK_FRAME else 0), (name, frame, r)
    case['candidates'] = {'frames': len(by_frame), 'routed': routed, 'replayed_draws': replayed_draws, 'far_admitted': far_admitted, 'far_vertices': far_vertices,
                          'box': {'extent': box_extent, 'depth_half': box_depth_half, 'size': size, 'texel_world': 2.0 * box_extent / size, 'cap': cap},
                          'frame0': {k: by_frame[0][k] for k in ('bounds', 'origin', 'fallback', 'capped', 'reads')},
                          'steady': {k: by_frame[1][k] for k in ('bounds', 'origin', 'fallback', 'capped', 'reads')}}
    case['checks'] += 3 * SHADOW_REPLAY_FRAMES + 1
    case['sun'] = validate_shadow_replay_sun(name, trace, routed, sun_programs, shadow_sun_expected(text))
    case['checks'] += 4 * SHADOW_REPLAY_FRAMES + 1
    assert [(r['reason'], r['detail'], int(r['frame'])) for r in refused] == [(reason, detail, frame) for reason, detail, frame in
                                                                             (('lease', 'bookends', SHADOW_REPLAY_LOCK_FRAME), ('state', 'sun_changing', SHADOW_REPLAY_NO_SUN_FRAME),
                                                                              ('state', 'multistream', SHADOW_REPLAY_MULTISTREAM_FRAME)) if frame not in off_window], (name, refused)
    # The Reset's target is recreated by the next transaction: inside an off
    # window that is the first frame back on.
    recreated = next(f for f in range(SHADOW_REPLAY_RESET_FRAME, SHADOW_REPLAY_FRAMES) if f not in off_window)
    assert [(t['frame'], t['allocations'], t['size'], t['map_format']) for t in targets] == [(0, 1, size, 114), (recreated, 2, size, 114)], (name, targets)
    case['us'] = depth_replay.us_summary(depth_rows)
    case['us']['per_draw_median'] = case['us']['median'] / replayed_draws
    case['refusals'] = refused; case['targets'] = targets
    # The map against the CPU projection of the replayed geometry through the
    # same chain (frames that replayed): frame 0 the casters (up to the cap),
    # later frames the admitted objects the cap kept (the narrow cases: L and
    # the casters but the last submitted; the wide case: L, the casters and
    # F). The Lock frame's map equals the previous frame's byte for byte.
    def replayed(frame, draws_of_frame):
        if frame == 0:
            return [d for d in draws_of_frame if d['shape'] in ('A', 'B') and d['caster'] < frame0_draws]
        return [d for d in draws_of_frame if (d['shape'], d['caster']) in kept]
    cameras = {int(fields(l)['frame']): fields(l) for l in text.splitlines() if l.startswith('SHADOW_CAMERA ')}
    suns = {int(fields(l)['frame']): fields(l) for l in text.splitlines() if l.startswith('SHADOW_SUN ')}
    draws = {}
    for l in text.splitlines():
        if l.startswith('SHADOW_DRAW '):
            f = fields(l); draws.setdefault(int(f['frame']), []).append({'caster': int(f['caster']), 'shape': f['shape'], 't': float(f['t']), 'p': float(f['p']), 'zo': float(f['zo'])})
    comparisons = {}
    previous = None
    for frame in range(SHADOW_REPLAY_FRAMES):
        m = maps[frame]
        if frame in off_window:
            # Nothing was cleared or drawn and the basis is voided: the export
            # reports the map invalid, and after the Reset it is gone entirely.
            assert (m['valid'] == '0' if m['available'] == '1' else frame >= SHADOW_REPLAY_RESET_FRAME and m['reason'] == 'no_map'), (name, frame, m)
            if m['available'] == '1':
                data = (directory / f'shadow_{frame}.r32f').read_bytes()
                assert previous is not None and data == previous, f'{name}: frame {frame} changed the map with the A/B off'
                previous = data
            comparisons[frame] = {'toggled_off': True, 'available': m['available'] == '1'}
            continue
        assert m['available'] == '1' and int(m['width']) == size and int(m['height']) == size and m['valid'] == '1', (name, frame, m)
        data = (directory / f'shadow_{frame}.r32f').read_bytes()
        assert len(data) == size * size * 4, (name, frame, len(data))
        if frame in refused_frames:
            assert previous is not None and data == previous, f'{name}: the refused frame {frame} changed the map'
            comparisons[frame] = {'unchanged': True}
            previous = data
            continue
        triple = lambda key: tuple(float(v) for v in m[key].split(','))
        basis = {'right': triple('right'), 'up': triple('up'), 'forward': triple('forward'), 'center': triple('center'),
                 'extent': float(m['extent']), 'depth_half': float(m['depth_half'])}
        expected_sun = tuple(float(v) for v in suns[frame]['direction'].split(','))
        assert all(abs(-basis['forward'][i] - expected_sun[i]) < 1e-5 for i in range(3)), (name, frame, basis['forward'], expected_sun)  # never c4 of the glow program, (1, 0, 0)
        assert (basis['extent'], basis['depth_half']) == (box_extent, box_depth_half), (name, frame, basis, box_extent, box_depth_half)
        c = cameras[frame]
        camera = {'m00': float(c['m00']), 'm11': float(c['m11']), 'r': [float(v) for v in c['r'].split(',')], 't': [float(v) for v in c['t'].split(',')]}
        actual = struct.unpack(f'<{size * size}f', data)
        comparison = depth_replay.compare_map(actual, replayed(frame, draws[frame]), camera, basis, size)
        # Coverage floor: hundreds of texels on the narrow fixture cascades
        # (texel 1/16 unit); on a production-scale texel (0.5-1 unit) the
        # unit-size casters cover a handful (frame 0) and L tens (48 at 1000 /
        # 2048), the count identity with the CPU rasterization being the check.
        floor = 100 if 2.0 * box_extent / size < 0.1 else (16 if frame else 1)
        assert comparison['ok'] and comparison['covered_cpu'] >= floor, (name, frame, comparison, floor)
        comparisons[frame] = comparison
        previous = data
    case['map'] = {'size': size, 'frames': comparisons,
                   'max_depth_error': max(v.get('max_depth_error', 0.0) for v in comparisons.values()),
                   'covered_texels': sum(v.get('covered_gpu', 0) for v in comparisons.values())}
    case['checks'] += 10 + 3 * SHADOW_REPLAY_FRAMES
    if toggle:
        # F8 while the shadows are off: the DLL's capture readback must dump no
        # map at all and report the basis invalid, never the content of the
        # last frame that replayed (comparison-hotkeys.md, "Sun shadows at rest").
        start, frames = int(env['X3M_CAPTURE_START']), int(env['X3M_CAPTURE_FRAMES'])
        captured = [f for f in range(start, start + frames) if f < SHADOW_REPLAY_FRAMES]
        off_captured = [f for f in captured if f in off_window]
        replaying = [f for f in captured if f not in off_window and f not in refused_frames]
        assert off_captured and replaying, (name, 'the capture window must cover off frames and a frame back on', captured)
        basis_lines = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('shadow_replay_map_basis ')}
        dumped = {int(fields(l)['frame']) for l in tl if l.startswith('shadow_replay_map_readback ')}
        for frame in off_captured:  # nothing published: an invalid basis (or, after the Reset released the map, no line), no dump at all
            assert basis_lines.get(frame, {'valid': '0'})['valid'] == '0', (name, frame, basis_lines.get(frame))
            assert frame not in dumped, (name, frame, 'a stale map was dumped with the A/B off')
        for frame in replaying:     # the control: the same F8 on a replayed frame dumps its map
            assert basis_lines.get(frame, {}).get('valid') == '1' and frame in dumped, (name, frame, basis_lines.get(frame), sorted(dumped))
        case['toggle'] = {'off_frames': sorted(off_window), 'captured': captured, 'off_captured': off_captured,
                          'dumped': sorted(dumped), 'replaying_captured': replaying}
        case['checks'] += 2 * len(captured) + 1
    return case


def shadow_sun_expected(text):
    return tuple(float(v) for v in fields(next(l for l in text.splitlines() if l.startswith('SHADOW_SUN ')))['direction'].split(','))


def validate_ownership(name, variant, enabled, trace):
    """Wrapper-side witnesses: adoption once, no fallback, depth storage and
    epochs, scene-depth adapter, admission roots/vetoes; balanced retirement is
    proven by the fixture's zero final device/factory Release, which through the
    wrapper requires every child wrapper (the route's included) to be gone."""
    tl = trace.splitlines()
    env = VARIANTS[variant]
    wrapped = env.get('X3M_OWNERSHIP') == '1'
    depth_mode = env.get('X3M_DEPTH_COPY') == '1'
    admission = env.get('X3M_ADMISSION') == '1'
    result = {'wrapped': wrapped, 'depth_copy': depth_mode, 'admission': admission}
    factory = [l for l in tl if l.startswith('ownership_factory ')]
    assert len(factory) == int(wrapped) and all('mode=wrapped' in l for l in factory), (name, factory)
    assert 'mode=native_fallback' not in trace, name
    modes = [l for l in tl if l.startswith('ownership_mode ')]
    if wrapped:
        assert modes == [f'ownership_mode requested=1 depth_copy_requested={int(depth_mode)} depth_copy_enabled={int(depth_mode)} scope=normal9 fallback=native'], (name, modes)
    else:
        assert not modes, (name, modes)
    depth = [fields(l) for l in tl if l.startswith('ownership_copy_depth ')]
    phases = {}
    for d in depth:
        phases.setdefault(d['phase'], []).append(d)
    if wrapped:
        assert len(phases.get('create_after', [])) == len(phases.get('reset_after', [])) == 1, (name, sorted(phases))
        assert all(d['result'] == '00000000' and d['requested'] == str(int(depth_mode)) for d in depth), (name, depth)
        if depth_mode:
            assert all(d['available'] == '1' and d['source_bound'] == '1' and d['copy_valid'] == '0' and d['copy_epoch'] == '0'
                       and d['source_format'] == '77' for d in depth), (name, depth)
            # Reset retires the storage (one generation) and allocates anew (another).
            generations = (int(phases['create_after'][0]['generation']), int(phases['reset_after'][0]['generation']))
            assert generations[0] == 1 and generations[1] > generations[0], (name, generations)
            result['depth_generations'] = generations
        else:
            assert all(d['available'] == '0' for d in depth), (name, depth)
    else:
        assert not depth, (name, depth)
    # Per-capture-frame epochs are logged only while the route is requested.
    present = {int(d['frame']): d for d in phases.get('present', [])}
    if wrapped and enabled:
        assert sorted(present) == list(range(1, 9)), (name, sorted(present))
        if depth_mode:
            for frame, d in present.items():
                assert int(d['source_epoch']) == DEPTH_CLEARS_PER_FRAME * (frame + 1), (name, frame, d)
                assert d['generation'] == '1' and d['source_bound'] == '1' and d['copy_valid'] == '0' and d['copy_epoch'] == '0', (name, frame, d)
        result['source_epochs'] = {f: int(d['source_epoch']) for f, d in present.items()}
    else:
        assert not present, (name, sorted(present))
    scene = [fields(l) for l in tl if l.startswith('scene_depth_frame ')]
    if depth_mode:
        begins = sorted(int(s['frame']) for s in scene if s['phase'] == 'begin')
        ends = [s for s in scene if s['phase'] == 'end']
        assert begins == list(range(1, 9)) and sorted(int(s['frame']) for s in ends) == begins, (name, begins)
        assert all(s['attempted'] == s['copied'] == s['confirmed'] == '0' and s['present'] == '00000000' for s in ends), (name, ends)
        assert 'scene_depth_copy ' not in trace and 'scene_depth_boundary ' not in trace, 'synthetic frames must not select a game boundary'
    else:
        assert not scene, (name, len(scene))
    result['admission'] = verify_admission(trace, admission, final_count=1, device_count=1)
    metrics = [fields(l) for l in tl if l.startswith('admission_metric ')]
    if admission:
        # Present logs the metric in captured frames and every 300th frame (frame 0).
        assert sorted(int(m['frame']) for m in metrics) == list(range(0, 9)), (name, len(metrics))
        assert all(m['veto_bits'] == '0' and m['first_reason'] == '0' and m['waiting_roots'] == '0' and m['replay_active'] == '0' for m in metrics), (name, metrics)
        assert result['admission']['final_vetoes'] == 0, (name, result['admission'])
    else:
        assert not metrics, (name, len(metrics))
    return result


def validate_bench(name, taa, size, text, trace, hdr=False, tonemap=False, sharpen=0.0):
    lines = text.splitlines()
    assert lines and lines[-1].startswith('RESULT PASS '), f'{name}: bench did not pass'
    mode_line = fields([l for l in lines if l.startswith('MODE ')][0])
    width, height = size.split('x')
    assert (mode_line['bench'], mode_line['taa'], mode_line['width'], mode_line['height'], mode_line['enabled'], mode_line['hdr']) == ('1', '1', width, height, '1', str(int(hdr))), (name, mode_line)
    summary = fields([l for l in lines if l.startswith('BENCH_SUMMARY ')][0])
    samples = [float(fields(l)['boundary_ms']) for l in lines if l.startswith('BENCH ')]
    assert len(samples) == 24 and int(summary['frames']) == 20, (name, summary)
    frames = {int(fields(l)['frame']): fields(l) for l in trace.splitlines() if l.startswith('motion_output_frame ')}
    # Frame 0 is logged by telemetry: the boundary was recognized and, with the
    # switch on, resolved (current-only, first frame); off, nothing ran.
    assert 0 in frames and frames[0]['latched'] == frames[0]['filled'] == '1' and frames[0]['selector_state'] == '9', (name, frames.get(0))
    assert frames[0]['taa'] == str(int(taa)) and frames[0]['taa_resolved'] == str(int(taa)), (name, frames[0])
    if taa:
        assert frames[0]['taa_attempted'] == '1' and frames[0]['taa_skip'] == '0' and frames[0]['taa_result'] == '00000000', (name, frames[0])
        assert frames[0]['taa_hdr'] == str(int(hdr)) and frames[0]['taa_copy'] == ('00000001' if hdr or sharpen else '00000000'), (name, frames[0])  # stage 3: no copy-back on the HDR path; the sharpen draws in its place
        assert frames[0]['taa_sharpen'] == str(int(sharpen > 0)), (name, frames[0])
    assert not any(l.startswith(('motion_output_taa_failed', 'motion_output_fill_failed', 'motion_output_apply_failed', 'motion_output_restore_failed')) for l in trace.splitlines()), name
    result = {'mode': 'bench', 'taa': taa, 'hdr': hdr, 'sharpen': sharpen, 'width': int(width), 'height': int(height), 'frames_timed': int(summary['frames']),
              'boundary_ms': {'min': float(summary['min_ms']), 'median': float(summary['median_ms']), 'max': float(summary['max_ms'])},
              'samples_ms': samples, 'timing': summary['timing']}
    hdr_frames = {int(fields(l)['frame']): fields(l) for l in trace.splitlines() if l.startswith('hdr_frame ')}
    if hdr:
        # Frame 0 (telemetry cadence): redirected, written back once at the bloom copy, no unwind; the target's size and bytes.
        assert 0 in hdr_frames and (hdr_frames[0]['redirected'], hdr_frames[0]['end'], hdr_frames[0]['unwind'], hdr_frames[0]['writeback_source']) == ('1', 'bloom_copy', '0', 'shader'), (name, hdr_frames.get(0))
        assert hdr_frames[0]['target'] == f'{width}x{height}' and int(hdr_frames[0]['target_bytes']) == int(width) * int(height) * 8, (name, hdr_frames[0])
        result['hdr_frame0'] = {k: hdr_frames[0][k] for k in ('end', 'writebacks', 'flushes', 'writeback_source', 'target', 'target_bytes', 'redirect_us', 'writeback_us', 'writeback_draw_us',
                                                                'tonemap', 'exposure', 'meter', 'readback', 'meter_us', 'readback_us', 'chain_bytes', 'sharpen', 'sharpened')}
        assert hdr_frames[0]['sharpened'] == str(int(sharpen > 0)) and hdr_frames[0]['sharpen'] == ('ok' if sharpen > 0 else 'off'), (name, hdr_frames[0])
        result['target_bytes'] = int(hdr_frames[0]['target_bytes'])
        result['tonemap'] = tonemap
        assert hdr_frames[0]['tonemap'] == ('agx' if tonemap else 'identity'), (name, hdr_frames[0])
        if tonemap:
            # Auto exposure: the chain ran in frame 0 (no readback yet); the last timed frame stepped on the previous frame's meter.
            last = hdr_frames[max(hdr_frames)]
            assert hdr_frames[0]['meter'] == '00000000' and hdr_frames[0]['exposure'] == 'auto' and int(hdr_frames[0]['chain_bytes']) > 0, (name, hdr_frames[0])
            assert max(hdr_frames) >= 20 and last['stepped'] == '1' and last['readback'] == '00000000' and int(last['steps']) == max(hdr_frames), (name, last)
            result['chain_bytes'] = int(hdr_frames[0]['chain_bytes'])
            result['hdr_frame_last'] = {k: last[k] for k in ('frame', 'ev', 'ev_adapted', 'ev_target', 'avg_log_l', 'dt_ms', 'steps', 'meter_us', 'readback_us', 'writeback_draw_us')}
    else:
        assert not hdr_frames, (name, 'hdr_frame lines without the switch')
    return result


def camera_expectations(lines):
    """The fixture's per-frame camera decision (CAMERA_EXPECT lines), keyed by frame."""
    return {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('CAMERA_EXPECT ')}


def check_camera_log(name, trace, expects, camera, sentinel, frames_logged):
    """The DLL's camera_state lines carry the fixture's matrices and decisions."""
    tl = trace.splitlines()
    # The loader's `camera_state active=.. status=..` line carries no frame.
    states = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('camera_state ') and 'frame=' in l}
    assert sorted(states) == sorted(frames_logged), (name, sorted(states))
    mode = {None: '0', 'auto': '0', '1': '1', '2': '2'}[sentinel]
    for frame, state in states.items():
        e = expects[frame]
        assert state['status'] == ('fixture' if camera else 'executable_mismatch') and state['mode'] == mode and state['cut_deg'] == '20.00', (name, frame, state)
        assert state['valid'] == e['installed'] and state['background_valid'] == e['installed'], (name, frame, state)
        assert (state['policy'], state['reason'], state['camera_cut']) == (e['policy'], e['reason'], e['cut']), (name, frame, state, e)
        assert abs(float(state['rotation_deg']) - float(e['rotation_deg'])) <= 1e-3, (name, frame, state, e)
        if camera:
            for key in ('p00', 'p11', 'r00', 'r01', 'r02', 'r10', 'r11', 'r12', 'r20', 'r21', 'r22'):
                assert abs(float(state[key]) - float(e[key])) <= 1e-6, (name, frame, key, state[key], e[key])
            assert (state['p20'], state['p21'], state['t']) == ('0', '0', '12.5,-3,1000'), (name, frame, state)
            assert state['read_failure'] == state['failure'] == '0' and state['reads'] == '2', (name, frame, state)
            # The background view is the same pose (read at the latch, before the frame's yaw): its rotation
            # against the scene view is the per-frame step (one degree, 31 at the jump, 0 on the first).
            if e['reason'] in ('0', '4'):  # the decision reports a rotation only when it compared two views
                assert abs(float(state['background_rotation_deg']) - float(e['rotation_deg'])) <= 1e-3, (name, frame, state)
            assert state['history_view_valid'] == e['resolve'], (name, frame, state, e)
        else:  # no camera available: the route never attempts a read
            assert state['read_failure'] == '0' and state['reads'] == '0' and state['history_view_valid'] == '0', (name, frame, state)
        # The history's view as the sentinel policy saw it, before this frame's
        # resolve relatched it: valid exactly when the decision compared two
        # views (reasons 0, 4, 5), never on reason 3, and otherwise (switch off
        # or unreadable camera) when the previous frame's resolve latched a
        # readable view (frame 0 and the first frame after Reset have none).
        expected_previous = '1' if state['reason'] in ('0', '4', '5') else '0' if state['reason'] == '3' else str(int(camera and frame not in (0, 9)))
        assert state['prev_valid_at_policy'] == expected_previous, (name, frame, state, expected_previous)
    return {f: {'policy': int(s['policy']), 'reason': int(s['reason']), 'cut': int(s['camera_cut']), 'rotation_deg': float(s['rotation_deg'])} for f, s in states.items()}


def invalidate_sites(trace):
    """`taa_invalidate device=.. frame=F site=<name>` lines, one set of site names per frame
    (the DLL logs each site at most once per frame, at the frame's end or at the frame's
    (re)begin; a Reset carries the frame the proxy began at the preceding Present)."""
    sites = {}
    for l in trace.splitlines():
        if l.startswith('taa_invalidate '):
            f = fields(l)
            sites.setdefault(int(f['frame']), set()).add(f['site'])
    return sites


def mip_bias_text(mip_bias):
    """The fixture's MODE line prints the DLL's bias with %g."""
    return '%g' % float(mip_bias or 0)


def validate_case(name, mode, variant, enabled, jitter, taa, text, trace, directory, lazy=False, camera=False, sentinel=None, shadow=True, hdr=False, hdr_fault=None, mip_bias=None, sharpen=0.0, copy_draw=False, quad_fvf=False):
    lines = text.splitlines()
    assert lines and lines[-1].startswith('RESULT PASS '), f'{name}: fixture did not pass'
    assert 'FAIL' not in text and text.count('RESULT ') == 1, f'{name}: failures reported'
    terminal = fields(lines[-1])
    seam = mode == 'seam'
    live = seam and enabled
    rt_mode = 'lazy' if lazy else 'perdraw'
    mode_line = fields([l for l in lines if l.startswith('MODE ')][0])
    sentinel_switch = sentinel  # the readback loop below reuses the name `sentinel` for a pixel count
    sentinel_mode = {None: '0', 'auto': '0', '1': '1', '2': '2'}[sentinel]
    assert mode_line == {'seam': str(int(seam)), 'enabled': str(int(enabled)), 'jitter': str(int(jitter)),
                         'jitter_samples': str(JITTER_SAMPLES), 'taa': str(int(taa)), 'bench': '0', 'width': '64', 'height': '64',
                         'dll': mode_line['dll'], 'burst': '0', 'rt_mode': rt_mode, 'camera': str(int(camera)), 'sentinel': sentinel_mode,
                         'envmap': '0', 'hook': '0', 'state_shadow': '0' if shadow is False else '1', 'hdr': str(int(hdr)), 'hdrvalues': '0', 'hdrfault': '0', 'hdrramp': '0', 'hdrexposure': '0', 'hdrtonemapfault': '0',
                         'mipbias': '0', 'mip_bias': mip_bias_text(mip_bias if enabled else None), 'sharpen': f'{sharpen:g}', 'msaa': '0'}, (name, mode_line)
    # The camera script: the 31-degree jump at frame 7 is a cut unless the
    # switch is off; strict mode without a camera skips every frame.
    strict_skip = sentinel == '2' and not camera
    camera_cuts = CAMERA_CUT_FRAMES if camera and sentinel != '1' else set()
    history_frames = set() if strict_skip else SEAM_TAA_HISTORY - camera_cuts
    skipped = 12 if strict_skip else 0
    # Per frame: the coverage oracle's background sample and verdict (every
    # case) plus, live, motion/depth dimensions, the pixel-ABI upload, the
    # motion oracle and the depth oracle. TAA: per frame the bloom copy, the
    # history verdict and (frames without history) the bit-identical color,
    # plus the script totals (history frames, skipped frames, reference
    # frames); seam adds the reference device, the byte-exact comparison and
    # the FP16 file per resolved frame and the reference teardown, one check
    # per skipped frame; the camera adds the pose validation per frame. Frame
    # 8 checks that a recorded render-state write never reached the device.
    # Every frame writes its presented image for the runner (one check each).
    expected_checks = 31 + 12 + (60 if live else 0)
    if taa:
        expected_checks += 12 * 2 + (12 - len(history_frames) if live else 12) + 4 + skipped + (1 + 2 * (12 - skipped) + 2 if live else 0) + (12 if camera else 0)
    # A live mip bias adds one check per frame at the bloom copy (39b31d5:
    # the copy is a restore point, so every stage must hold the application's
    # own bias again). The copy only runs in the TAA script.
    if taa and enabled and float(mip_bias or 0) != 0:
        expected_checks += 12
    # Stage 3 with the AgX write-back: the coverage oracle reads raster
    # colours and is skipped on tonemapped frames (two checks per frame).
    agx = any(l.startswith('hdr_tonemap ') and fields(l).get('tonemap') == '1' for l in trace.splitlines())
    if agx:
        expected_checks -= 24
    # Post-resolve sharpen: the bit-identical check of the frames without
    # history is waived (the display image is sharpened on every frame).
    if taa and sharpen:
        expected_checks -= 12 - len(history_frames) if live else 12
    assert int(terminal['checks']) == expected_checks, (name, terminal, expected_checks)
    restorations = 39 + (12 if taa else 0)
    assert int(terminal['restorations']) == restorations and int(terminal['frames']) == 12, (name, terminal)
    assert text.count('RESET PASS') == 1
    restores = [fields(l) for l in lines if l.startswith('RESTORE ')]
    assert len(restores) == restorations and all(r['differences'] == '0' for r in restores), f'{name}: restoration differences'
    colors = {int(fields(l)['frame']): fields(l)['hash'] for l in lines if l.startswith('COLOR ')}
    assert sorted(colors) == list(range(12)), f'{name}: color inventory'
    motion = [fields(l) for l in lines if l.startswith('MOTION ')]
    coverage = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('COVERAGE ')}
    result = {'mode': mode, 'enabled': enabled, 'jitter': jitter, 'taa': taa, 'lazy': lazy, 'checks': int(terminal['checks']), 'restorations': restorations, 'frames': 12,
              'color_hashes': colors, 'motion_pixels': int(terminal['motion_pixels']),
              'matched_pixels': int(terminal['matched_pixels']), 'max_uv_error_pixels': float(terminal['max_uv_pixels']),
              'max_previous_depth_error': float(terminal['max_depth_error']),
              'depth_pixels': int(terminal['depth_pixels']), 'depth_written_pixels': int(terminal['depth_written']),
              'max_current_depth_error': float(terminal['max_current_depth_error']),
              'coverage_frames': int(terminal['coverage_frames']), 'coverage_pixels': int(terminal['coverage_pixels']),
              'coverage_ambiguous_pixels': int(terminal['coverage_ambiguous'])}
    # Rasterized coverage agrees with the CPU reference at the (jittered)
    # sample positions in every frame of every case (route off: the oracle's
    # own control); the reference uses the route's Halton offsets, so a sign
    # or scale error in the jitter would fail here.
    # (Stage 3, AgX write-back: the oracle reads raster colours and the fixture
    # skips it on tonemapped frames; the presented image is then checked
    # against the AgX reference of the resolved FP16 image instead.)
    assert sorted(coverage) == ([] if agx else list(range(12))), (name, sorted(coverage))
    assert all(c['mismatches'] == '0' and int(c['checked']) > 3000 and int(c['background']) > 0 for c in coverage.values()), (name, coverage)
    assert len({c['background_color'] for c in coverage.values()}) == (0 if agx else 1), (name, 'background colour differs between frames')
    assert all(c['jitter'] == str(int(jitter)) for c in coverage.values())
    for frame, c in coverage.items():
        _, jx, jy = expected_jitter(frame) if jitter else (0, 0.0, 0.0)
        assert abs(float(c['jx']) - jx) < 1e-6 and abs(float(c['jy']) - jy) < 1e-6, (name, frame, c)
    assert (result['coverage_frames'] == 0) if agx else (result['coverage_frames'] == 12 and result['coverage_pixels'] > 40000)
    taa_lines = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('TAA ')}
    if taa:
        # Fixture verdicts per frame: the bloom copy equals the main target,
        # history follows the script, no-history frames are bit-identical.
        assert sorted(taa_lines) == list(range(12)), (name, sorted(taa_lines))
        history = {f for f, t in taa_lines.items() if t['history'] == '1'}
        cuts = {f for f, t in taa_lines.items() if t['cut'] == '1'}
        assert history == (history_frames if live else set()) and cuts == ((SEAM_TAA_CUTS | camera_cuts) if live else set()), (name, history, cuts)
        if not sharpen:  # the sharpen changes the presented image on every frame (the fixture waives the check; the runner compares against the reference)
            assert all(t['changed'] == '0' for f, t in taa_lines.items() if f not in history), (name, 'no-history frame changed the color')
        assert (terminal['taa'], terminal['taa_frames'], terminal['taa_history_frames'], terminal['taa_reference_frames'], terminal['taa_skipped_frames']) == \
               ('1', '12', str(len(history)), str(12 - skipped) if live else '0', str(skipped)), (name, terminal)
        # The fixture's decision per frame: policy 2 exactly on the frames with a previous view
        # and no camera cut (auto and strict), never with the switch off or without a camera.
        policies = {f: int(t['policy']) for f, t in taa_lines.items()}
        expects = camera_expectations(lines)
        assert sorted(expects) == list(range(12)) and all(policies[f] == int(expects[f]['policy']) for f in policies), (name, policies)
        assert all(t['skipped'] == ('1' if strict_skip else '0') for t in taa_lines.values()), (name, 'skipped frames')
        # Consolidated invalidation diagnostic: the Reset after frame 8's
        # Present is the only history drop of a resolving script; the proxy
        # begins frame 9 at that Present, so the site carries frame 9 (flushed
        # by after_reset's re-begin, before motion_output_reset). The strict
        # skip drops the history twice per frame, at the refused resolve
        # (skip) and at the frame's end (not_resolved). Nothing else fires: no
        # cutout miss, composition stop, restore failure or failed Present.
        expected_sites = {f: {'skip', 'not_resolved'} for f in range(12)} if strict_skip else {}
        expected_sites.setdefault(9, set()).add('reset')
        sites = invalidate_sites(trace)
        assert sites == expected_sites, (name, sites, expected_sites)
        result['taa_invalidate_sites'] = {f: sorted(v) for f, v in sorted(sites.items())}
        if camera and sentinel != '1':
            assert {f for f, p in policies.items() if p == 2} == set(range(12)) - {0, 9} - CAMERA_CUT_FRAMES, (name, policies)
            assert all(expects[f]['cut'] == str(int(f in CAMERA_CUT_FRAMES)) for f in expects), (name, expects)
        else:
            assert all(p == 1 for p in policies.values()), (name, policies)
        result['taa_history_frames'] = sorted(history)
        result['camera_policy'] = policies
        result['taa_changed_pixels'] = {f: int(t['changed']) for f, t in taa_lines.items()}
        result['color_hashes_before_boundary'] = {int(fields(l)['frame']): fields(l)['hash'] for l in lines if l.startswith('COLOR_BEFORE ')}
    else:
        assert not taa_lines and terminal['taa'] == '0'
    if live:
        assert len(motion) == 12 and all(m['mismatches'] == '0' and m['depth_mismatches'] == '0' and int(m['checked']) > 3000 for m in motion), f'{name}: oracle'
        assert result['motion_pixels'] > 40000 and result['matched_pixels'] > 10000
        assert result['max_uv_error_pixels'] <= .01 and result['max_previous_depth_error'] <= 4e-6
        assert result['depth_pixels'] == result['motion_pixels'] and result['depth_written_pixels'] > 20000
        assert result['max_current_depth_error'] <= 4e-6
        # Frame 7 routes only the small triangle (the A draw rebinds the flat PS
        # through the state block, gate 3): about 170 pixels; other frames > 2,600.
        assert all(int(m['depth_written']) > 100 for m in motion), f'{name}: every frame writes RT2 for its routed draws'
        assert sum(int(m['matched']) for m in motion if int(m['frame']) in (0, 2, 3, 9)) == 0, 'sentinel-only frames carried motion'
    else:
        assert not motion and result['motion_pixels'] == 0 and result['depth_pixels'] == 0
    # Capture log: mode line, device gate, variants, target lifecycle, frames, readback.
    tl = trace.splitlines()
    assert sum(l.startswith('device_hooked ') for l in tl) == sum(l.startswith('device_destroy ') for l in tl) == 1
    modes = [fields(l) for l in tl if l.startswith('motion_output_mode ')]
    assert len(modes) == 1 and modes[0]['requested'] == str(int(enabled)) and modes[0]['temporal_consumer'] == modes[0]['taa'] == str(int(taa)) and modes[0]['taa_debug'] == str(int(taa))
    devices = [fields(l) for l in tl if l.startswith('motion_output_device ')]
    variants = [fields(l) for l in tl if l.startswith('motion_output_variant ')]
    targets = [l for l in tl if l.startswith('motion_output_target ')]
    frames = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_frame ')}
    readbacks = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_readback ')}
    depth_readbacks = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_depth_readback ')}
    cuts = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_cut ')}
    routes = [fields(l) for l in tl if l.startswith('motion_route ')]
    modes = [fields(l) for l in tl if l.startswith('motion_output_mode ')]
    assert modes[0]['jitter'] == str(int(jitter)) and modes[0]['jitter_samples'] == str(JITTER_SAMPLES), (name, modes)
    assert modes[0]['rt_mode'] == rt_mode and modes[0]['frame_log'] == '60', (name, modes)
    assert modes[0]['state_shadow'] == shadow_request(shadow) and modes[0]['scene_hook'] == '0' and modes[0]['hdr'] == str(int(hdr)), (name, modes)
    taa_readbacks = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_taa_readback ')}
    color_readbacks = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_color_readback ')}
    present_readbacks = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_present_readback ')}
    taa_lines_log = [fields(l) for l in tl if l.startswith('motion_output_taa ')]
    if not enabled:
        assert not devices and not variants and not targets and not frames and not readbacks and not depth_readbacks and not cuts and not routes, f'{name}: disabled route logged activity'
        assert not any(l.startswith('motion_output_release ') for l in tl)
        assert not taa_readbacks and not color_readbacks and not present_readbacks and not taa_lines_log
        return result
    assert len(devices) == 1 and devices[0]['enabled'] == '1' and devices[0]['reason'] == 'ok', (name, devices)
    assert devices[0]['rt_mode'] == rt_mode, (name, devices)
    assert devices[0]['state_shadow'] == shadow_effective(shadow, lazy) and devices[0]['scene_hook'] == '0', (name, devices)
    assert devices[0]['hdr'] == str(int(hdr and hdr_fault is None)), (name, devices)
    # The HDR redirect: device gate, per-frame lines, readbacks (or the feature disabling itself with a forced-absent capability).
    result['hdr'] = validate_hdr(name, trace, directory, hdr, hdr_fault, frames=range(0, 9), capture_frames=range(1, 9),
                                 end='bloom_copy' if taa else 'present', taa=taa and not strict_skip)
    assert devices[0]['history_available'] == '0', 'synthetic process must not claim game observers'
    # Three-format self test (A8R8G8B8 + A32B32G32R32F + R32F) on this backend.
    assert devices[0]['depth'] == '1' and devices[0]['depth_reason'] == 'ok' and devices[0]['r32f'] == '00000000', (name, devices)
    assert devices[0]['detail'] == 'stage=compare' and 'color_errors=0 motion_errors=0 depth_errors=0 targets=3' in trace
    assert devices[0]['jitter'] == str(int(jitter)) and devices[0]['jitter_samples'] == str(JITTER_SAMPLES)
    assert devices[0]['taa'] == str(int(taa)) and devices[0]['taa_reason'] == ('ok' if taa else 'off') and devices[0]['taa_debug'] == str(int(taa)), (name, devices)
    # D1: the copy mode and its attach-time round trip (the adapter query is
    # accepted unconditionally on Wine, so the stretch mode is the outcome
    # unless the seam faulted the round trip); D2: the quad twin switch.
    assert devices[0]['taa_copy'] == ('draw' if taa and copy_draw else 'stretch' if taa else 'off'), (name, devices)
    assert devices[0]['taa_stretch_test'] == ('fault' if taa and copy_draw else 'pass' if taa else 'off') and devices[0]['taa_stretch_query'] == '00000000', (name, devices)
    assert devices[0]['quad_fvf'] == str(int(quad_fvf)), (name, devices)
    result['taa_copy'] = devices[0]['taa_copy']
    # The camera read is gated on the exact executable (never this synthetic
    # process) unless the seam installed the fixture's globals; the switch is parsed.
    assert devices[0]['camera'] == ('fixture' if camera else 'executable_mismatch' if taa else 'disabled'), (name, devices)
    assert (devices[0]['sentinel'], devices[0]['camera_cut_deg'], devices[0]['camera_log']) == (sentinel_mode, '20.00', '300'), (name, devices)
    assert (modes[0]['sentinel'], modes[0]['camera_cut_deg'], modes[0]['camera_log']) == ({'0': 'auto', '1': '1', '2': '2'}[sentinel_mode], '20.00', '300'), (name, modes)
    assert [(v['kind'], v['transform'], v['create'], v['depth']) for v in variants] == [('vs', '0', '00000000', '1'), ('ps', '0', '00000000', '1')] * 2, (name, variants)
    assert all(v['original'] in ('53a0a641107ed76c', '8759c7838bbc86c2') for v in variants)
    assert len(targets) == 2 and all('create=00000000 level=00000000 depth=1 depth_create=00000000 depth_level=00000000' in t for t in targets), 'targets created at first latch and after Reset'
    assert 'motion_output_reset device=1 result=00000000 generation=2' in trace
    assert not any(l.startswith('motion_output_taa_failed') for l in tl), f'{name}: resolve failures logged'
    if taa:
        # One lazy initialization holding one device reference (the resolve
        # shader); after Reset only that reference remains until the next run.
        # The pass holds one device reference per created program: the resolve, plus the sharpen program with the switch on.
        taa_references = str(TAA_BASE_REFERENCES + (1 if sharpen else 0))
        assert [t['initialize'] for t in taa_lines_log] == ['00000000'] and taa_lines_log[0]['references'] == taa_references, (name, taa_lines_log)
        assert taa_lines_log[0]['copy'] == ('draw' if copy_draw else 'stretch'), (name, taa_lines_log)
        assert f'generation=2 taa_references={taa_references}' in trace, name
    else:
        assert not taa_lines_log and not taa_readbacks and not color_readbacks and not present_readbacks
    assert sum(l.startswith('motion_output_release ') for l in tl) == 1, 'owned objects released before the final device Release'
    assert not any(l.startswith(('motion_output_fill_failed', 'motion_output_apply_failed', 'motion_output_restore_failed')) for l in tl)
    assert sorted(frames) == list(range(0, 9)), (name, sorted(frames))  # frame 0 via telemetry, 1..8 via capture
    assert sorted(cuts) == list(range(1, 9)), (name, sorted(cuts))
    render_state = {}
    for frame, summary in frames.items():
        assert summary['latched'] == summary['filled'] == '1' and summary['fill_result'] == summary['fill_restore'] == '00000000'
        assert summary['apply_failures'] == summary['restore_failures'] == '0' and summary['committed'] == '1'
        assert summary['present'] == '00000000'
        assert summary['depth'] == '1' and summary['depth_routed'] == summary['routed'], (name, frame, summary)
        # Cost fields: every routed draw of this script binds and releases RT1
        # and RT2 (four SetRenderTarget calls) in both modes, because the
        # fixture's state snapshot after each draw (a getter) restores a lazy
        # binding: one flush per routed draw. Timing is CPU QPC (telemetry on).
        assert summary['rt_mode'] == rt_mode and summary['timing'] == 'cpu_qpc', (name, frame, summary)
        assert int(summary['set_rt']) == 4 * int(summary['routed']), (name, frame, summary)
        assert int(summary['lazy_flushes']) == (int(summary['routed']) if lazy else 0), (name, frame, summary)
        assert int(summary['jitter_writes']) == 2 * int(summary['jittered']), (name, frame, summary)
        # Motion + depth; with X3M_TAA_DEBUG the pre-resolve colour, the resolved FP16 image and the presented main target; the FP16 readback of the HDR path adds one.
        assert int(summary['readbacks']) == (0 if frame == 0 else (5 if taa and not strict_skip else 2) + int(hdr and hdr_fault is None)), (name, frame, summary)
        # Render-state shadow: every route query is a shadow hit except after a
        # resynchronization (at most one native read per shadowed state); the
        # native reads are those misses plus the fill's touched-state save. Off:
        # no hits, one native read per query.
        rs = check_render_state(name, frame, summary, shadow, SEAM_RESYNCS.get(frame, 0))
        render_state[frame] = rs
        # No engine hook in this process: the scene end comes from the copy
        # (TAA runs; StretchOnly is not a disagreement without the patch) or nowhere.
        assert (summary['scene_hook'], summary['hook_signals'], summary['hook_outside_scene'], summary['draws_after_hook']) == ('0', '0', '0', '0'), (name, frame, summary)
        assert (summary['scene_end_source'], summary['scene_end_check'], summary['bloom_copy_seen']) == (('stretchrect', '3', '1') if taa else ('none', '0', '0')), (name, frame, summary)
        cost_fields = ('gate_us', 'route_draw_us', 'set_rt_us', 'lazy_flush_us', 'jitter_us', 'fill_us', 'taa_run_us', 'taa_capture_us',
                       'taa_copy_color_us', 'taa_copy_depth_us', 'taa_draw_us', 'taa_apply_us', 'taa_copy_back_us', 'readback_us')
        costs = {k: float(summary[k]) for k in cost_fields}
        assert all(v >= 0 for v in costs.values()) and costs['fill_us'] > 0 and costs['route_draw_us'] > 0, (name, frame, costs)
        # The five phases nest inside the run (0.1 us rounding per field).
        assert (costs['taa_run_us'] > 0) == (taa and not strict_skip) and costs['taa_run_us'] + 0.5 >= costs['taa_capture_us'] + costs['taa_copy_color_us'] + costs['taa_copy_depth_us'] + costs['taa_draw_us'] + costs['taa_apply_us'], (name, frame, costs)
        assert (costs['readback_us'] > 0) == (frame > 0), (name, frame, costs)
        # The resolve ran at the boundary of every TAA frame (frame 0 included)
        # with the history use the fixture expects; the fixture's depth rebind
        # then rejects the selector (9), which must not drop the history.
        # Without the switch nothing was attempted (skip 1).
        assert summary['taa'] == str(int(taa)) and summary['active_queries'] == '0', (name, frame, summary)
        if taa:
            # The pass reports whether it blended a valid history: the seam's
            # script frames; the production DLL from frame 1 on (the sentinel
            # policy establishes history although every pixel resolves
            # current-only), except the first frame after Reset (9, not captured).
            expect_history = int(frame in history_frames) if live else int(frame not in (0, 9))
            if strict_skip:  # X3M_TAA_SENTINEL=2 without a readable camera: attempted, skipped (9), never resolved
                assert (summary['taa_attempted'], summary['taa_resolved'], summary['taa_history'], summary['taa_skip']) == ('1', '0', '0', '9'), (name, frame, summary)
            else:
                assert (summary['taa_attempted'], summary['taa_resolved'], summary['taa_history'], summary['taa_skip']) == ('1', '1', str(expect_history), '0'), (name, frame, summary)
            # The route's decision per frame equals the fixture's (same builder, same inputs).
            e = expects[frame]
            assert (summary['camera_policy'], summary['camera_reason'], summary['camera_cut']) == (e['policy'], e['reason'], e['cut']), (name, frame, summary, e)
            assert abs(float(summary['camera_rotation_deg']) - float(e['rotation_deg'])) <= 1e-3, (name, frame, summary, e)
            assert summary['camera_valid'] == summary['camera_background_valid'] == str(int(camera)) and summary['camera_reads'] == ('2' if camera else '0'), (name, frame, summary)
            # The frame line is written after Present, outside the application's scene.
            if strict_skip:  # nothing ran: the counters keep their S_FALSE defaults
                assert (summary['taa_result'], summary['taa_restore'], summary['taa_copy'], summary['scene_open']) == ('00000001', '00000000', '00000001', '0'), (name, frame, summary)
            else:
                assert summary['taa_result'] == summary['taa_restore'] == '00000000' and summary['scene_open'] == '0', (name, frame, summary)
                # Stage 3: with the redirect active the resolve consumes the FP16
                # scene (taa_hdr=1) and no copy-back runs (taa_copy S_FALSE): the
                # write-back samples the resolved image instead.
                hdr_live = hdr and hdr_fault is None
                assert summary['taa_hdr'] == str(int(hdr_live)) and summary['taa_copy'] == ('00000001' if hdr_live or sharpen or copy_draw else '00000000'), (name, frame, summary)  # draw copy mode: the pass wrote the display, no copy-back
                assert summary['msaa'] == '0', (name, frame, summary)
                # Post-resolve sharpen: the display image of every resolved frame is the sharpened one (the pass's draw or the write-back's program).
                assert summary['taa_sharpen'] == str(int(sharpen > 0)), (name, frame, summary)
            assert int(summary['taa_references']) >= 1, (name, frame, summary)
        else:
            assert (summary['taa_attempted'], summary['taa_resolved'], summary['taa_skip']) == ('0', '0', '1'), (name, frame, summary)
        # Jitter: the sequence index advances at every latch whether or not the
        # switch is on; the sample values and the previous latch's values are
        # the Halton offsets with jitter on and zero otherwise, and every scene
        # draw (all draw the table VS) is jittered only with the switch on.
        index, jx, jy = expected_jitter(frame)
        _, pjx, pjy = expected_jitter(frame - 1) if frame else (0, 0.0, 0.0)
        if not jitter:
            jx = jy = pjx = pjy = 0.0
        assert summary['jitter'] == str(int(jitter)) and int(summary['jitter_index']) == index, (name, frame, summary)
        assert abs(float(summary['jitter_x']) - jx) < 1e-6 and abs(float(summary['jitter_y']) - jy) < 1e-6, (name, frame, summary)
        assert abs(float(summary['jitter_previous_x']) - pjx) < 1e-6 and abs(float(summary['jitter_previous_y']) - pjy) < 1e-6, (name, frame, summary)
        assert int(summary['jittered']) == (int(summary['draws']) - 1 if jitter else 0), (name, frame, summary)
        # Cut detector: samples are the matched draws, the median their 1.6 px
        # origin displacement; the verdict follows the missing-key fraction.
        assert int(summary['cut_samples']) == int(summary['matched']), (name, frame, summary)
        if int(summary['matched']):
            assert abs(float(summary['cut_median_px']) - ORIGIN_DISPLACEMENT_PX) < 1e-3, (name, frame, summary)
        if live and frame in SEAM_FRAMES:
            got = {k: int(summary[k]) for k in SEAM_FRAMES[frame]}
            assert got == SEAM_FRAMES[frame], (name, frame, got, SEAM_FRAMES[frame])
            assert summary['selector_state'] == ('9' if taa else '2'), 'seam frames end inside the Scene phase (rejected after the copy with the TAA boundary: the fixture rebinds depth without a bloom sequence)'
            assert int(summary['cut']) == SEAM_CUTS[frame], (name, frame, summary)  # the displacement/missing-key verdict alone; the camera cut is separate
            cut = cuts[frame]
            assert int(cut['cut']) == SEAM_CUTS[frame] and cut['samples'] == summary['cut_samples'] and abs(float(cut['bound_px']) - 2.4) < 1e-6 and cut['bound_missing'] == '0.250'
        elif not live:
            # Production DLL: the structural selector enters the synthetic frame's
            # scene phase too, but without the game observers every scene draw
            # that passes gates 3-4 routes in sentinel-only mode (gate 5).
            got = {k: int(summary[k]) for k in ('draws', 'routed', 'matched', 'gate2', 'gate3', 'gate4', 'gate5', 'gate6')}
            assert got['matched'] == 0 and got['gate2'] == 1 and got['gate6'] == 0, (name, frame, got)
            assert got['routed'] == got['gate5'] == got['draws'] - 1 - got['gate3'] - got['gate4'], (name, frame, got)
            if frame in SEAM_FRAMES:
                assert (got['gate3'], got['gate4']) == (SEAM_FRAMES[frame]['gate3'], SEAM_FRAMES[frame]['gate4']), (name, frame, got)
            assert summary['selector_state'] == ('9' if taa else '2'), 'production frames also end inside the Scene phase (rejected after the copy with the TAA boundary)'
    assert sorted(readbacks) == sorted(depth_readbacks) == list(range(1, 9)), (name, sorted(readbacks), sorted(depth_readbacks))
    depth_stats = {}
    for frame, r in readbacks.items():
        assert r['result'] == '00000000' and r['bytes'] == '65536' and r['width'] == r['height'] == '64'
        pixels = read_motion(directory / 'x3-modern-captures' / r['file'])
        valid = sum(p[3] == 1.0 for p in pixels)
        sentinel = sum(p == SENTINEL for p in pixels)
        assert valid + sentinel == len(pixels), f'{name}: frame {frame} readback holds values outside the ABI'
        if live and frame in (1, 4):
            assert valid > 2000, f'{name}: frame {frame} readback lacks matched motion'
        elif not live or frame in (2, 3):
            assert sentinel == len(pixels), f'{name}: frame {frame} readback is not all sentinel'
        # RT2: device depth where a routed draw covered the pixel (matched or
        # sentinel-only alike), -1 elsewhere; every motion-valid pixel has depth.
        d = depth_readbacks[frame]
        assert d['result'] == '00000000' and d['bytes'] == '16384' and d['width'] == d['height'] == '64' and d['format'] == 'r32f_row_major'
        assert d['file'] == f'depth_1_{frame}.r32f'
        depth = read_depth(directory / 'x3-modern-captures' / d['file'])
        written = sum(1 for v in depth if v != -1.0)
        assert all(v == -1.0 or 0.0 <= v <= 1.0 for v in depth), f'{name}: frame {frame} depth outside [0,1] or sentinel'
        assert written > (100 if frame == 7 else 1000), f'{name}: frame {frame} depth target lacks routed coverage'
        assert all(depth[i] != -1.0 for i, p in enumerate(pixels) if p[3] == 1.0), f'{name}: frame {frame} motion-valid pixel without depth'
        depth_stats[frame] = {'written': written, 'min': min(v for v in depth if v != -1.0), 'max': max(v for v in depth if v != -1.0)}
    result['depth_readbacks'] = depth_stats
    if taa:
        result['camera_log'] = check_camera_log(name, trace, expects, camera, sentinel_switch, range(0, 9))
    if taa and strict_skip:
        assert not taa_readbacks and not color_readbacks and not present_readbacks, (name, 'skipped frames wrote debug readbacks')
    elif taa:
        # X3M_TAA_DEBUG: the resolved FP16 image and the pre-resolve color of
        # frames 1-8. Seam: the FP16 bytes equal the reference pass's output.
        # Both: the analyzer's sanity signal loads them (finite; production is
        # current-only, so no pixel differs from the pre-resolve color).
        assert sorted(taa_readbacks) == sorted(color_readbacks) == list(range(1, 9)), (name, sorted(taa_readbacks), sorted(color_readbacks))
        for frame in range(1, 9):
            t, c = taa_readbacks[frame], color_readbacks[frame]
            assert t['result'] == c['result'] == '00000000' and t['bytes'] == '32768' and c['bytes'] == '16384', (name, frame, t, c)
            assert t['file'] == f'taa_1_{frame}.rgba16f' and c['file'] == f'color_1_{frame}.bgra8' and t['format'] == 'rgba16f_row_major' and c['format'] == 'bgra8_row_major'
            resolved = (directory / 'x3-modern-captures' / t['file']).read_bytes()
            assert len(resolved) == 32768
            if live:
                reference = (directory / f'reference_taa_{frame}.rgba16f').read_bytes()
                assert resolved == reference, f'{name}: frame {frame} resolved FP16 image differs from the reference pass'
        analysis = readback_analysis.analyze(next((directory / 'x3-modern-captures').glob('session-*.log')),
                                             directory / 'x3-modern-captures', readback_analysis.default_options(draw_details=False))
        taa_check = analysis['checks']['taa_image']
        assert taa_check['status'] == 'pass' and taa_check['frames'] == list(range(1, 9)), (name, taa_check)
        if not live and not agx:  # under AgX the 8-bit pre-resolve colour is tonemapped: not comparable to the resolved FP16 image
            assert all(fraction == 0 for fraction in taa_check['differing_fraction'].values()), (name, taa_check)
        result['taa_image'] = {'differing_fraction': taa_check['differing_fraction'], 'max_difference': taa_check['max_difference']}
        # X3M_TAA_DEBUG: present_1_<frame>.bgra8 is the main target after the
        # sharpen draw / copy-back (8-bit route) or after the write-back (HDR
        # route) of a resolved frame: byte for byte the image the fixture
        # presented. Unsharpened and untonemapped, it is the resolved FP16
        # image through the 8-bit conversion (one code); sharpened, RCAS of it
        # (validate_sharpen); tonemapped, AgX of it (validate_hdr_taa).
        assert sorted(present_readbacks) == list(range(1, 9)), (name, sorted(present_readbacks))
        present_stats = {}
        for frame in range(1, 9):
            r = present_readbacks[frame]
            assert r['result'] == '00000000' and r['bytes'] == '16384' and r['file'] == f'present_1_{frame}.bgra8' and r['format'] == 'bgra8_row_major', (name, frame, r)
            present = (directory / 'x3-modern-captures' / r['file']).read_bytes()
            assert present == read_presented(directory, frame), f'{name}: frame {frame} present_1_{frame}.bgra8 differs from the presented image'
            entry = {'equals_presented': True}
            if not sharpen and not agx:
                resolved = read_half_image(directory / 'x3-modern-captures' / taa_readbacks[frame]['file'], 64, 64)
                errors, alpha = [], []
                for i, (cr, cg, cb, ca) in enumerate(resolved):
                    pr, pg, pb, pa = bgra8(present, i)
                    errors.append(max(abs(p - 255.0 * min(max(c, 0.0), 1.0)) for p, c in zip((pr, pg, pb), (cr, cg, cb))))
                    alpha.append(abs(pa - 255.0 * min(max(ca, 0.0), 1.0)))
                entry.update(max_code_error_vs_resolved=max(errors), mean_code_error_vs_resolved=sum(errors) / len(errors), alpha_max_error_vs_resolved=max(alpha))
                assert entry['max_code_error_vs_resolved'] <= 1.0, (name, frame, entry)  # the copy-back's 8-bit quantisation of the FP16 image
            present_stats[frame] = entry
        result['present_readbacks'] = present_stats
    # Per-draw decisions logged in capture frames must agree with the fixture's script.
    expects = [fields(l) for l in lines if l.startswith('EXPECT ') and 1 <= int(fields(l)['frame']) <= 8]
    assert len(routes) == len(expects) == sum(SEAM_FRAMES[f]['draws'] - 1 for f in SEAM_FRAMES), (name, len(routes), len(expects))
    assert all(r['jittered'] == e['jittered'] for e in expects for r in routes if r['frame'] == e['frame'] and r['index'] == e['index']), f'{name}: jittered flags'
    assert all(r['depth'] == r['routed'] for r in routes), f'{name}: every routed draw of the reviewed row binds RT2'
    if live:
        for e in expects:
            match = [r for r in routes if r['frame'] == e['frame'] and r['index'] == e['index']]
            assert len(match) == 1 and match[0]['routed'] == e['routed'] and match[0]['matched'] == e['matched'], (name, e, match)
            assert match[0]['result'] == '00000000'
    else:
        # Every production route is sentinel-only: routed exactly when scope is
        # unknown (gate 5), never matched, and the fixture's own EXPECT lines
        # (which assume no live routing) still name each scene draw once.
        assert all(r['matched'] == '0' and r['routed'] == str(int(r['gate'] == '5')) and r['result'] == '00000000' for r in routes), f'{name}: production routes must be sentinel-only'
        assert {(r['frame'], r['index']) for r in routes} == {(e['frame'], e['index']) for e in expects}, f'{name}: production routes must cover the scene draws'
    result.update(variants=len(variants), frames_logged=len(frames), readbacks=len(readbacks), routes=len(routes), render_state=render_state,
                  route_decisions=[(r['frame'], r['index'], r['gate'], r['routed'], r['matched']) for r in routes])
    return result


def validate_msaa(name, text, trace, samples=2):
    """The msaa script (D3 of the native-Windows audit): three frames on a 2-sample back buffer. The selector never
    latches a multisampled RT0 and the route refuses the frame by name at its initial Clear: one
    motion_output_msaa_refused line, every frame line msaa=2 with nothing routed, jittered or filled, no RT1/RT2
    created, and the resolve skipped with reason 11 (Msaa) instead of 2 (NotReached)."""
    lines = text.splitlines()
    assert lines and lines[-1].startswith('RESULT PASS '), f'{name}: fixture did not pass'
    assert 'FAIL' not in text and text.count('RESULT ') == 1, f'{name}: failures reported'
    mode_line = fields([l for l in lines if l.startswith('MODE ')][0])
    assert mode_line['msaa'] == str(samples) and mode_line['enabled'] == '1' and mode_line['taa'] == '1', (name, mode_line)
    terminal = fields(lines[-1])
    assert int(terminal['frames']) == 3 and terminal['taa_frames'] == '0', (name, terminal)
    tl = trace.splitlines()
    assert 'msaa=%u' % samples in [l for l in tl if l.startswith('create_device ')][0], (name, 'create_device')
    devices = [fields(l) for l in tl if l.startswith('motion_output_device ')]
    assert len(devices) == 1 and devices[0]['enabled'] == '1' and devices[0]['taa'] == '1' and devices[0]['taa_copy'] == 'stretch', (name, devices)
    refused = [fields(l) for l in tl if l.startswith('motion_output_msaa_refused ')]
    assert len(refused) == 1 and refused[0] == {'device': '1', 'frame': refused[0]['frame'], 'msaa': str(samples), 'width': '64', 'height': '64'}, (name, refused)
    frames = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_frame ')}
    assert sorted(frames) == [0, 1, 2], (name, sorted(frames))
    for frame, summary in frames.items():
        # The selector never latches a multisampled RT0 (latched=0, selector_state 9 = Rejected); the route names the reason.
        assert summary['latched'] == '0' and summary['msaa'] == str(samples) and summary['filled'] == '0' and summary['selector_state'] == '9', (name, frame, summary)
        assert summary['routed'] == '0' and summary['jittered'] == '0' and summary['jitter'] == '0' and summary['depth_routed'] == '0', (name, frame, summary)
        assert int(summary['draws']) >= 3 and summary['gate1'] == summary['draws'], (name, frame, summary)  # every draw stops at gate 1
        assert (summary['taa_attempted'], summary['taa_resolved'], summary['taa_skip']) == ('0', '0', '11'), (name, frame, summary)
        assert summary['apply_failures'] == summary['restore_failures'] == '0' and summary['present'] == '00000000', (name, frame, summary)
    assert not any(l.startswith('motion_output_target ') for l in tl), f'{name}: RT1/RT2 were created for a multisampled main target'
    assert not any(l.startswith(('motion_output_taa_failed', 'motion_output_fill_failed', 'motion_output_apply_failed', 'motion_output_restore_failed')) for l in tl), name
    assert sum(l.startswith('motion_output_release ') for l in tl) == 1, name
    return {'mode': 'msaa', 'samples': samples, 'frames': 3, 'checks': int(terminal['checks']), 'restorations': int(terminal['restorations']),
            'refused': refused[0], 'frame_lines': {f: {k: v[k] for k in ('msaa', 'routed', 'jittered', 'taa_skip', 'gate1', 'draws')} for f, v in frames.items()}}


ZONLY_FRAMES, ZONLY_CONTROL_FRAMES, ZONLY_VS = 9, {2, 4, 6}, 'c78b4c68a87fce74'
# The capture window opens at X3M_CAPTURE_START=1 and the script's Reset after frame 3 closes it for good
# (capture.cpp Device::Reset clears `capture`/`remaining`, and the start frame has already passed), so the
# per-draw motion_route records and the two readbacks exist on frames 1-3 only. The prepass invariants
# themselves (unjittered_depth_writers, jittered, holes) are asserted on all nine frames from the frame
# lines and the fixture's own oracle.
ZONLY_CAPTURE_FRAMES = (1, 2, 3)


def validate_zonly(name, text, trace):
    """The zonly script (asteroid-fog-temporal.md, run 47): nine frames, each a depth-only prepass with the z_only
    vs_1_1 program (null PS, ZWRITEENABLE on, COLORWRITEENABLE 0) followed by the blended, z-write-off material draw
    of the same sloped geometry. Regular frames: zero interior holes (the route jittered both draws, so the LESSEQUAL
    test against the prepass depth passes everywhere) and the coverage oracle agrees. Control frames (jx > 0): the
    prepass is pre-shifted so the route's jitter cancels; the material draw must then lose more than half its interior.
    Every frame line reports unjittered_depth_writers=0 with both scene draws jittered and none routed; the capture
    frames' route records show the prepass at gate 3 (no pair) and the material draw at gate 4, both jittered
    (the capture window is frames 1-3: the Reset after frame 3 closes it)."""
    lines = text.splitlines()
    assert lines and lines[-1].startswith('RESULT PASS '), f'{name}: fixture did not pass'
    assert 'FAIL' not in text and text.count('RESULT ') == 1, f'{name}: failures reported'
    terminal = fields(lines[-1])
    mode_line = fields([l for l in lines if l.startswith('MODE ')][0])
    assert mode_line['enabled'] == '1' and mode_line['jitter'] == '1' and mode_line['taa'] == '0', (name, mode_line)
    assert int(terminal['frames']) == ZONLY_FRAMES and text.count('RESET PASS') == 1, (name, terminal)
    restores = [fields(l) for l in lines if l.startswith('RESTORE ')]
    assert len(restores) == 3 * ZONLY_FRAMES and all(r['differences'] == '0' for r in restores), f'{name}: restoration differences'
    zonly = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('ZONLY ')}
    assert sorted(zonly) == list(range(ZONLY_FRAMES)), (name, sorted(zonly))
    coverage = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('COVERAGE ')}
    holes = {}
    for frame, z in zonly.items():
        control = frame in ZONLY_CONTROL_FRAMES
        assert z['control'] == str(int(control)) and int(z['pixels']) > 500, (name, frame, z)
        _, ejx, ejy = expected_jitter(frame)
        assert abs(float(z['jx']) - ejx) < 1e-5 and abs(float(z['jy']) - ejy) < 1e-5, (name, frame, z, ejx, ejy)
        holes[frame] = int(z['holes'])
        if control:
            assert float(z['jx']) > 0 and holes[frame] > int(z['pixels']) // 2, (name, frame, z)
            assert frame not in coverage, (name, frame)  # the oracle is skipped on purpose
        else:
            assert holes[frame] == 0 and coverage[frame]['mismatches'] == '0' and int(coverage[frame]['material']) > 0, (name, frame, z, coverage.get(frame))
    tl = trace.splitlines()
    frames = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_frame ')}
    assert sorted(frames) == list(range(ZONLY_FRAMES)), (name, sorted(frames))
    for frame, summary in frames.items():
        assert summary['jitter'] == '1' and summary['latched'] == '1', (name, frame, summary)
        assert (summary['draws'], summary['routed'], summary['jittered'], summary['unjittered_depth_writers']) == ('3', '0', '2', '0'), (name, frame, summary)
        assert (summary['gate3'], summary['gate4']) == ('1', '1'), (name, frame, summary)
        assert summary['apply_failures'] == summary['restore_failures'] == '0', (name, frame, summary)
        assert int(summary['readbacks']) == (2 if frame in ZONLY_CAPTURE_FRAMES else 0), (name, frame, summary)
    routes = [fields(l) for l in tl if l.startswith('motion_route ')]
    expects = [fields(l) for l in lines if l.startswith('EXPECT ') and int(fields(l)['frame']) in ZONLY_CAPTURE_FRAMES]
    assert len(expects) == 2 * len(ZONLY_CAPTURE_FRAMES), (name, len(expects))
    assert {(r['frame'], r['index']) for r in routes} == {(e['frame'], e['index']) for e in expects}, (name, len(routes), len(expects))
    for e in expects:
        match = [r for r in routes if r['frame'] == e['frame'] and r['index'] == e['index']]
        assert len(match) == 1 and match[0]['routed'] == '0' and match[0]['jittered'] == e['jittered'] == '1' and match[0]['result'] == '00000000', (name, e, match)
        prepass = e.get('prepass') == '1'
        assert match[0]['gate'] == ('3' if prepass else '4'), (name, e, match)
        assert (match[0]['vs'] == ZONLY_VS and match[0]['ps'] == '0' * 16) == prepass, (name, e, match)
    assert not any(l.startswith(('motion_output_apply_failed', 'motion_output_restore_failed')) for l in tl), name
    return {'mode': 'zonly', 'frames': ZONLY_FRAMES, 'checks': int(terminal['checks']), 'restorations': int(terminal['restorations']),
            'holes': holes, 'control_frames': sorted(ZONLY_CONTROL_FRAMES), 'unjittered_depth_writers': {f: int(v['unjittered_depth_writers']) for f, v in frames.items()},
            'capture_frames': list(ZONLY_CAPTURE_FRAMES), 'routes': len(routes)}


def validate_hdr(name, trace, directory, hdr, hdr_fault, frames, capture_frames, end, taa, ends=None, redirected=None, width=64, height=64):
    """The route's hdr_device gate, the per-frame hdr_frame lines and the
    capture-frame FP16 readbacks. `end`: the expected end point of every frame
    (or `ends` per frame); `redirected`: per-frame expectation (default all)."""
    tl = trace.splitlines()
    devices = [fields(l) for l in tl if l.startswith('hdr_device ')]
    hdr_frames = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('hdr_frame ')}
    targets = [fields(l) for l in tl if l.startswith('hdr_target ')]
    readbacks = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('hdr_readback ')}
    unwinds = [l for l in tl if l.startswith('hdr_unwind=')]
    if not hdr:
        assert not devices and not hdr_frames and not targets and not readbacks and not unwinds, (name, 'HDR activity without the switch')
        return {'enabled': False}
    assert len(devices) == 1, (name, devices)
    d = devices[0]
    if hdr_fault is not None:
        # A forced-absent capability (1: the FP16 render-target format, 3: the self test) disables the feature at attach: no redirect anywhere.
        assert d['enabled'] == '0' and d['reason'] == {'1': 'fp16_target', '3': 'self_test'}[hdr_fault], (name, d)
        assert not hdr_frames and not targets and not readbacks and not unwinds, (name, 'a disabled feature redirected')
        return {'enabled': False, 'reason': d['reason'], 'device': d}
    assert d['enabled'] == '1' and d['reason'] == 'ok' and d['route'] == '1' and d['depth'] == '1', (name, d)
    assert d['fp16_target'] == d['fp16_blending'] == d['fp16_sampling'] == '00000000', (name, d)
    # The self test's four checks plus the emergency StretchRect rung (exercised whenever the conversion is granted, review 22).
    assert d['self_test_targets'] == '3' and 'scene_errors=0 sum_errors=0 motion_errors=0 depth_errors=0 copy_errors=0 stretch_errors=0 targets=3' in trace, (name, d)
    assert d['stretch_conversion'] == '00000000' and d['stretch'] == '00000000' and d['main_format'] == '21', (name, d)  # A8R8G8B8 back buffer
    assert sorted(hdr_frames) == list(frames), (name, sorted(hdr_frames), list(frames))
    assert not unwinds, (name, unwinds)
    for frame, h in hdr_frames.items():
        expect_redirected = 1 if redirected is None else redirected[frame]
        expect_end = (ends or {}).get(frame, end)
        assert h['hdr'] == '1' and h['redirected'] == str(expect_redirected), (name, frame, h)
        assert (h['unwind'], h['unwind_reason'], h['blocked'], h['recheck'], h['refused_msaa'], h['dirty_at_present']) == ('0', 'none', '0', 'none', '0', '0'), (name, frame, h)
        assert h['caps'] == 'ok' and h['timing'] == 'cpu_qpc', (name, frame, h)
        if expect_redirected:
            assert h['end'] == expect_end and h['writeback_source'] == 'shader' and int(h['writebacks']) >= 1, (name, frame, h, expect_end)
            assert h['target'] == f'{width}x{height}' and int(h['target_bytes']) == width * height * 8, (name, frame, h)
            assert float(h['writeback_us']) > 0 and float(h['writeback_draw_us']) > 0 and float(h['redirect_us']) > 0, (name, frame, h)
            assert h['target_create'] == '00000000' and h['latch_bind'] == '00000000', (name, frame, h)
        else:
            assert h['end'] == 'none' and h['writebacks'] == '0', (name, frame, h)
    # Capture frames read the FP16 image back before the first write-back of the frame.
    expected_readbacks = [f for f in capture_frames if (1 if redirected is None else redirected[f])]
    assert sorted(readbacks) == expected_readbacks, (name, sorted(readbacks), expected_readbacks)
    for frame, r in readbacks.items():
        assert r['result'] == '00000000' and r['file'] == f'hdr_1_{frame}.rgba16f' and int(r['bytes']) == width * height * 8 and r['format'] == 'rgba16f_row_major', (name, frame, r)
        assert (directory / 'x3-modern-captures' / r['file']).stat().st_size == width * height * 8, (name, frame)
    assert all(t['create'] == '00000000' and int(t['bytes']) == int(t['width']) * int(t['height']) * 8 for t in targets), (name, targets)
    return {'enabled': True, 'device': {k: d[k] for k in ('reason', 'fp16_target', 'fp16_blending', 'fp16_filter', 'fp16_sampling', 'stretch_conversion', 'mrt_blending', 'self_test_targets')},
            'targets': [(int(t['width']), int(t['height']), int(t['bytes'])) for t in targets],
            'frames': {f: {k: h[k] for k in ('redirected', 'end', 'writebacks', 'flushes', 'writeback_source', 'suspended', 'resumed', 'redirect_us', 'writeback_us', 'writeback_draw_us', 'bind_us')} for f, h in hdr_frames.items()},
            'readbacks': sorted(readbacks), 'taa': taa}


def read_presented(directory, frame, width=64, height=64):
    data = (directory / f'presented_{frame}.bgra8').read_bytes()
    assert len(data) == width * height * 4, (directory, frame, len(data))
    return data


# The fixture's clear colour (0xff203040) and flat program colour (0xff8040bf) as stored BGRA8 bytes.
PRESENTED_BACKGROUND, PRESENTED_FLAT = bytes.fromhex('403020ff'), bytes.fromhex('bf4080ff')
# The FP16 intermediate rounds an unquantized shader output to 11 significant
# bits before the 8-bit conversion; a value near a rounding boundary of the
# 8-bit code can land one code away from the direct path (double rounding).
# Accepted: at most one code per channel, on lit material pixels only, alpha
# exact, and at least 98% of all pixels exact; the numbers are recorded.
HDR_TWIN_MAX_CODE_DIFFERENCE = 1
HDR_TWIN_MIN_EXACT_FRACTION = 0.98
# TAA twins (stage 3): the HDR run resolves the unquantized FP16 scene into an
# FP16 history while the 8-bit twin re-quantizes its history through the
# 8-bit copy-back every frame, so accumulated values differ by up to half a
# code before the final rounding: still at most one code, background, flat
# and alpha exact, only material pixels, and more of them the more frames
# carry history (measured: seam script 97.6% exact, 4.2% of the material
# pixels over 6 history frames of 12; hook script 92.4%, 11.6% over 6 of 7).
HDR_TWIN_MIN_EXACT_FRACTION_TAA = 0.90
HDR_TWIN_MAX_MATERIAL_DIFFERING_FRACTION_TAA = 0.15
# What the double rounding can touch: a channel differs only when its value
# lies within half an FP16 ulp of an 8-bit code boundary, i.e. at most
# (2^-12) / (1/255) = 6.2% of the samples per channel for values in [0.5, 1)
# and half that per octave below; a systematic offset (a bias, a sampling
# shift) would differ on every material pixel. Measured: 1.8-2.0%.
HDR_TWIN_MAX_MATERIAL_DIFFERING_FRACTION = 0.10


def compare_presented(name, twin, dir_a, dir_b, frames, width=64, height=64):
    """Per-pixel comparison of the presented frames of two runs: exact matches,
    the largest per-channel code difference and the class of every differing
    pixel (background, flat or material, by the twin's value)."""
    total = exact = 0
    max_difference = 0
    differing_frames = []
    classes = {'background': [0, 0], 'flat': [0, 0], 'material': [0, 0]}
    channels = [0, 0, 0, 0]  # differing samples per stored channel (b, g, r, a)
    for frame in frames:
        a, b = read_presented(dir_a, frame, width, height), read_presented(dir_b, frame, width, height)
        pixels = width * height
        total += pixels
        same = 0
        for i in range(pixels):
            pa, pb = a[i * 4:i * 4 + 4], b[i * 4:i * 4 + 4]
            cls = 'background' if pb == PRESENTED_BACKGROUND else 'flat' if pb == PRESENTED_FLAT else 'material'
            classes[cls][0] += 1
            if pa == pb:
                same += 1
                continue
            classes[cls][1] += 1
            for k in range(4):
                if pa[k] != pb[k]:
                    channels[k] += 1
                    max_difference = max(max_difference, abs(pa[k] - pb[k]))
        exact += same
        if same != pixels:
            differing_frames.append(frame)
    return {'twin': twin, 'frames': list(frames), 'pixels': total, 'exact': exact, 'exact_fraction': exact / total if total else 1.0,
            'max_code_difference': max_difference, 'differing_frames': differing_frames, 'identical': exact == total,
            'classes': {k: {'pixels': v[0], 'differing': v[1]} for k, v in classes.items()},
            'differing_channels_bgra': channels}


def accept_hdr_twin(name, comparison, taa=False):
    """The tolerance above; identical is the ideal, not the requirement."""
    assert comparison['max_code_difference'] <= HDR_TWIN_MAX_CODE_DIFFERENCE, f'{name}: presented frames differ by more than one code from the twin: {comparison}'
    minimum = HDR_TWIN_MIN_EXACT_FRACTION_TAA if taa else HDR_TWIN_MIN_EXACT_FRACTION
    assert comparison['exact_fraction'] >= minimum, f'{name}: fewer than {minimum:.0%} of the presented pixels equal the twin: {comparison}'
    assert comparison['classes']['background']['differing'] == 0 and comparison['classes']['flat']['differing'] == 0, f'{name}: a background or flat pixel differs from the twin: {comparison}'
    assert comparison['differing_channels_bgra'][3] == 0, f'{name}: alpha differs from the twin: {comparison}'
    assert comparison['classes']['background']['pixels'] > 0 and comparison['classes']['material']['pixels'] > 0, (name, comparison)
    material = comparison['classes']['material']
    bound = HDR_TWIN_MAX_MATERIAL_DIFFERING_FRACTION_TAA if taa else HDR_TWIN_MAX_MATERIAL_DIFFERING_FRACTION
    assert material['differing'] <= bound * material['pixels'], f'{name}: more material pixels differ than FP16 rounding can explain: {comparison}'


def validate_hdrvalues(name, text, trace, directory, hdr_env=None, hdr_fault=None):
    lines = text.splitlines()
    assert lines and lines[-1].startswith('RESULT PASS ') and 'FAIL' not in text and text.count('RESULT ') == 1, f'{name}: fixture did not pass'
    terminal = fields(lines[-1])
    mode_line = fields([l for l in lines if l.startswith('MODE ')][0])
    assert (mode_line['seam'], mode_line['enabled'], mode_line['hdr'], mode_line['hdrvalues'], mode_line['jitter'], mode_line['taa']) == ('1', '1', '1', '1', '0', '0'), (name, mode_line)
    values = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('HDR_VALUES ')}
    assert sorted(values) == list(range(HDR_VALUES_FRAMES)) and int(terminal['frames']) == HDR_VALUES_FRAMES, (name, sorted(values), terminal)
    # Two Resets: the dimension change between frames 1 and 2, and the one
    # issued while the redirect is active before frame 5 (HDR_RESET_ACTIVE).
    assert text.count('RESET PASS') == 2 and text.count('HDR_RESET_ACTIVE frame=5') == 1, name
    for frame, v in values.items():
        assert v['mismatches'] == '0' and float(v['max_error']) <= 2e-3, (name, frame, v)
        assert (v['width'], v['height']) == (('64', '64') if frame < 2 else ('48', '40')), (name, frame, v)
        assert v['switch'] == str(int(frame == 3)) and v['mid'] == str(int(frame in (1, 4))), (name, frame, v)
    tl = trace.splitlines()
    hdr_frames = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('hdr_frame ')}
    targets = [fields(l) for l in tl if l.startswith('hdr_target ')]
    resets = [fields(l) for l in tl if l.startswith('motion_output_reset ')]
    assert sorted(hdr_frames) == list(range(HDR_VALUES_FRAMES)), (name, sorted(hdr_frames))
    assert [r['result'] for r in resets] == ['00000000', '00000000'], (name, resets)
    # Three targets: 64x64 before the first Reset, 48x40 after it (the dimension change) and again after the Reset while redirected.
    assert [(t['width'], t['height'], t['bytes'], t['create']) for t in targets] == [('64', '64', '32768', '00000000'), ('48', '40', '15360', '00000000'), ('48', '40', '15360', '00000000')], (name, targets)
    for frame, h in hdr_frames.items():
        assert (h['redirected'], h['end'], h['unwind'], h['writeback_source'], h['blocked']) == ('1', 'present', '0', 'shader', '0'), (name, frame, h)
        assert h['target'] == ('64x64' if frame < 2 else '48x40'), (name, frame, h)
        # Frame 3 switches RT0 mid-scene: the scene so far is written back at the switch
        # (a flush), the redirect suspends and resumes, and the EndScene flush writes the rest.
        assert (h['suspended'], h['resumed'], h['writebacks'], h['flushes']) == (('1', '1', '2', '2') if frame == 3 else ('0', '0', '1', '1')), (name, frame, h)
    assert not any(l.startswith(('hdr_unwind=', 'motion_output_fill_failed', 'motion_output_apply_failed', 'motion_output_restore_failed')) for l in tl), name
    return {'mode': 'hdrvalues', 'frames': HDR_VALUES_FRAMES, 'checks': int(terminal['checks']), 'restorations': int(terminal['restorations']),
            'values': {f: {k: v[k] for k in ('width', 'height', 'checked', 'a', 'b', 'background', 'max_error', 'alpha_b_128', 'switch', 'mid')} for f, v in values.items()},
            'targets': [(int(t['width']), int(t['height']), int(t['bytes'])) for t in targets],
            'hdr_frames': {f: {k: h[k] for k in ('end', 'writebacks', 'flushes', 'suspended', 'resumed', 'target')} for f, h in hdr_frames.items()}}


def validate_hdrfault(name, text, trace, directory, hdr_env=None, hdr_fault=None):
    lines = text.splitlines()
    assert lines and lines[-1].startswith('RESULT PASS ') and 'FAIL' not in text and text.count('RESULT ') == 1, f'{name}: fixture did not pass'
    terminal = fields(lines[-1])
    mode_line = fields([l for l in lines if l.startswith('MODE ')][0])
    assert (mode_line['seam'], mode_line['enabled'], mode_line['hdr'], mode_line['hdrfault']) == ('1', '1', '1', '1'), (name, mode_line)
    faults = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('HDR_FAULT ')}
    assert sorted(faults) == sorted(HDR_FAULT_SCRIPT) and int(terminal['frames']) == len(HDR_FAULT_SCRIPT), (name, sorted(faults), terminal)
    assert text.count('RESET PASS') == 3
    tl = trace.splitlines()
    hdr_frames = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('hdr_frame ')}
    unwinds = [fields(l.replace('hdr_unwind=', 'reason=', 1)) for l in tl if l.startswith('hdr_unwind=')]
    rechecks = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('hdr_recheck ')}
    assert sorted(hdr_frames) == sorted(HDR_FAULT_SCRIPT), (name, sorted(hdr_frames))
    stale = {}
    for frame, e in HDR_FAULT_SCRIPT.items():
        h, f = hdr_frames[frame], faults[frame]
        assert f['fault'] == str(e['fault']) and f['ldr'] == str(e['ldr']), (name, frame, f)
        assert (h['redirected'], h['unwind'], h['writeback_source'], h['recheck'], h['blocked']) == (str(e['redirected']), str(e['unwind']), e['source'], e['recheck'], str(e['blocked'])), (name, frame, h, e)
        if e['unwind']:
            assert h['unwind_reason'] == e['reason'], (name, frame, h)
        if 'target_create' in e:
            assert h['target_create'] == e['target_create'], (name, frame, h)
        if 'latch_bind' in e:
            assert h['latch_bind'] == e['latch_bind'], (name, frame, h)
        assert h['end'] == e.get('end', 'present' if e['redirected'] else 'none'), (name, frame, h)
        if 'writebacks' in e:
            assert (h['writebacks'], h['flushes']) == (e['writebacks'], e['writebacks']), (name, frame, h)
        # The frames without a copy rung (both copies refused, device lost) and
        # the failed latching Clear: the binding is restored (the fixture's
        # state comparison) and the image is what the swap chain holds; with
        # DISCARD that is undefined. Recorded.
        if e['source'] == 'restore' or e.get('end') == 'clear_failed':
            stale[frame] = {'previous_equal': int(f['previous_equal']), 'black_pixels': int(f['black']), 'pixels': int(f['pixels'])}
    # One unwind line per injected write-back failure, naming the rung that produced the image.
    assert [(u['reason'], u['source']) for u in unwinds] == [('draw', 'stretch'), ('restore', 'shader'), ('stretch', 'restore'), ('lost', 'restore'), ('draw', 'stretch')], (name, unwinds)
    assert {f: r['passed'] for f, r in rechecks.items()} == {2: '1', 3: '1', 4: '1', 5: '1', 12: '0'}, (name, rechecks)
    return {'mode': 'hdrfault', 'frames': len(HDR_FAULT_SCRIPT), 'checks': int(terminal['checks']), 'restorations': int(terminal['restorations']),
            'script': HDR_FAULT_SCRIPT, 'unwinds': [(u['reason'], u['source'], u['draw'], u['restore'], u['stretch'], u['bind']) for u in unwinds],
            'rechecks': {f: r['passed'] for f, r in rechecks.items()}, 'frames_without_copy_rung': stale,
            'hdr_frames': {f: {k: h[k] for k in ('redirected', 'end', 'unwind', 'unwind_reason', 'writeback_source', 'recheck', 'blocked', 'target_create', 'latch_bind')} for f, h in hdr_frames.items()},
            'color_hashes': {int(fields(l)['frame']): fields(l)['hash'] for l in lines if l.startswith('COLOR ')}}


def check_render_state(name, frame, summary, shadow, resyncs):
    """The DLL's per-frame render-state counters against the shadow switch."""
    lazy = summary.get('rt_mode') == 'lazy'
    assert summary['state_shadow'] == shadow_effective(shadow, lazy), (name, frame, summary)
    mode = summary.get('rs_mode')
    if mode is None:
        mode = 'shadow' if shadow_effective(shadow, lazy) == '1' else 'native'  # a DLL from before the hybrid unhook: the hooks were always installed
    else:
        assert mode == shadow_mode(shadow, lazy), (name, frame, mode, summary.get('rt_mode'))
    q, h, g, r = (int(summary[k]) for k in ('rs_queries', 'rs_hits', 'rs_gets', 'rs_resyncs'))
    # Failed application setters drop single shadow entries without a re-read;
    # they are counted apart so a resync still has to show a shadow miss.
    i = int(summary.get('rs_invalidations', 0))
    assert r == resyncs and q >= 2 * int(summary['draws']), (name, frame, q, h, g, r)
    assert g == RS_FILL_GETS + q - h, (name, frame, q, h, g)
    if mode == 'shadow':
        if not r and not i:
            assert h == q, (name, frame, q, h)
        else:
            assert (0 < q - h if r else 0 <= q - h) and q - h <= RS_SHADOW_STATES * r + i, (name, frame, q, h, r, i)
    elif mode == 'native':
        assert h == 0, (name, frame, q, h)
    else:
        # Hybrid unhook: no hooks, so nothing to invalidate; the per-draw cache
        # answers repeats within a draw and every state costs one read per draw.
        assert i == 0 and 0 <= h < q, (name, frame, q, h, i)
    return {'queries': q, 'hits': h, 'gets': g, 'resyncs': r, 'invalidations': i, 'mode': mode}


# ---- FP16 HDR scene path, stage 2 -------------------------------------------
RAMP_MAX_CODE_ERROR = 1.0     # section 8 gate: <= 1 code max ...
RAMP_MEAN_CODE_ERROR = 0.5    # ... and <= 1/512 (half a code) mean, per channel
EXPOSURE_METER_TOLERANCE = 0.01   # 1% of the reference avg_log_l (floor 0.005 log2 units)
EXPOSURE_EV_TOLERANCE = 1e-3      # EV, adaptation replayed on the measured meters
HALF_RELATIVE = 2.0 ** -10        # one FP16 ulp: the backend truncates (measured 8.8e-4 > the 2^-11 of round-to-nearest) when storing the fixture's float inputs


def read_half_image(path, width, height):
    """hdr_<device>_<frame>.rgba16f: row-major RGBA halves -> list of (r, g, b, a) floats."""
    data = path.read_bytes()
    assert len(data) == width * height * 8, (path, len(data))
    values = struct.unpack('<%de' % (width * height * 4), data)
    return [values[i:i + 4] for i in range(0, len(values), 4)]


def half_round(x):
    """The FP16 value the render target stores for a float input (round to nearest even)."""
    return struct.unpack('<e', struct.pack('<e', x))[0]


def bgra8(data, index):
    b, g, r, a = data[index * 4:index * 4 + 4]
    return r, g, b, a


# The DLL's own auto-exposure ceiling in capture.cpp is below the reference
# module's EV_MAX; the runner
# must use the DLL default when X3M_HDR_EV_MAX is unset, because the reference
# adaptation clamps with it.
HDR_EV_MAX_DEFAULT = 1.3


def hdr_env_params(hdr_env):
    """The reference's arguments from the run's X3M_HDR_* environment."""
    decode = hdr_env.get('X3M_HDR_DECODE', 'gamma2.2')
    decode = 'none' if decode == 'none' else 'srgb' if decode == 'srgb' else 'gamma2.2'
    return dict(agx=hdr_env.get('X3M_HDR_TONEMAP') in ('agx', '1'), decode=decode, look=hdr_env.get('X3M_HDR_LOOK', 'none'),
                clamp=float(hdr_env.get('X3M_HDR_CLAMP', '0')), ev_manual=hdr_env.get('X3M_HDR_EV_MANUAL'),
                ev_offset=float(hdr_env.get('X3M_HDR_EV', hdr_env.get('X3M_HDR_EV_OFFSET', '0'))),
                tau_up=float(hdr_env.get('X3M_HDR_ADAPT_UP', exposure_ref.TAU_UP)), tau_down=float(hdr_env.get('X3M_HDR_ADAPT_DOWN', exposure_ref.TAU_DOWN)),
                key=float(hdr_env.get('X3M_HDR_KEY', exposure_ref.KEY)), dt=float(hdr_env.get('X3M_HDR_DT_MS', '0')) / 1000.0,
                ev_min=float(hdr_env.get('X3M_HDR_EV_MIN', exposure_ref.EV_MIN)), ev_max=float(hdr_env.get('X3M_HDR_EV_MAX', HDR_EV_MAX_DEFAULT)),
                meter_bg=float(hdr_env.get('X3M_HDR_METER_BG', exposure_ref.METER_BG)), meter_min_lit=float(hdr_env.get('X3M_HDR_METER_MIN_LIT', exposure_ref.METER_MIN_LIT)),
                white_target=float(hdr_env.get('X3M_HDR_WHITE_TARGET', exposure_ref.WHITE_TARGET)), key_pull=float(hdr_env.get('X3M_HDR_KEY_PULL', exposure_ref.KEY_PULL)),
                ev_deadband=float(hdr_env.get('X3M_HDR_EV_DEADBAND', exposure_ref.EV_DEADBAND)), edge_weight=float(hdr_env.get('X3M_HDR_METER_EDGE_WEIGHT', exposure_ref.EDGE_WEIGHT)))


def reference_codes(rgb_engine, ev, params):
    """The presented RGB the reference expects for one engine-space FP16 input under the run's switches (0..255 floats)."""
    if params['agx']:
        out = agx_ref.tonemap_engine(rgb_engine, exposure=2.0 ** ev, decode_mode=params['decode'], look_name=params['look'], clamp_max=params['clamp'])
    else:
        out = tuple(min(max(c, 0.0), 1.0) for c in rgb_engine)
    return tuple(255.0 * c for c in out)


def code_errors(presented_rgb, reference_rgb):
    return [abs(p - r) for p, r in zip(presented_rgb, reference_rgb)]


def hdr_stage2_lines(trace):
    tl = trace.splitlines()
    return {'device': [fields(l) for l in tl if l.startswith('hdr_device ')],
            'tonemap': [fields(l) for l in tl if l.startswith('hdr_tonemap ')],
            'frames': {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('hdr_frame ')},
            'unwinds': [fields(l.replace('hdr_unwind=', 'reason=', 1)) for l in tl if l.startswith('hdr_unwind=')],
            'rechecks': {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('hdr_recheck ')},
            'disabled': [fields(l) for l in tl if l.startswith('hdr_tonemap_disabled ')],
            'targets': [fields(l) for l in tl if l.startswith('hdr_target ')]}


def validate_hdrramp(name, text, trace, directory, hdr_env, hdr_fault=None):
    """The compiled AgX program (or the identity write-back) over the 65-row
    ramp against the Python reference on the exact FP16 inputs of the
    capture-frame readback: per-channel code error max <= 1 and mean <= 0.5,
    alpha carried, every cell uniform, the inputs within FP16 rounding of
    ramp_values()."""
    lines = text.splitlines()
    mode_line = fields(next(l for l in lines if l.startswith('MODE ')))
    assert (mode_line['enabled'], mode_line['hdr'], mode_line['hdrramp'], mode_line['width'], mode_line['height']) == ('1', '1', '1', '64', '65'), (name, mode_line)
    terminal = fields(lines[-1])
    assert lines[-1].startswith('RESULT PASS'), (name, lines[-1])
    params = hdr_env_params(hdr_env)
    ev = float(params['ev_manual'])
    s2 = hdr_stage2_lines(trace)
    assert len(s2['device']) == 1 and s2['device'][0]['enabled'] == '1' and len(s2['tonemap']) == 1, (name, s2['device'], s2['tonemap'])
    tm = s2['tonemap'][0]
    assert tm['tonemap'] == str(int(params['agx'])) and tm['exposure'] == 'manual' and float(tm['ev_manual']) == ev, (name, tm)
    # The ramp scripts run with a manual EV, but since 75dbbed the DLL sets
    # HdrConfig::allow_auto_toggle unconditionally, so meter_requested() holds
    # in fixed mode too and the meter capability is prepared whenever the
    # tonemap program exists; it stays unused (exposure=manual, stepped=0 below).
    # Without the tonemap (the identity script) no meter is prepared at all.
    assert tm['tonemap_reason'] == ('ok' if params['agx'] else 'off') and tm['look'] == params['look'] and tm['decode'] == params['decode'], (name, tm)
    # Prepared with the tonemap, except under decode=none: there the meter
    # self-test's GPU level-0 value disagrees with meter_level0() beyond 1e-4
    # and the pass refuses the meter (fail closed). The ramp scripts never use
    # it (manual EV, stepped=0 below), so the verdict is recorded, not required.
    meter_expected = ('0', 'off') if not params['agx'] else ('0', 'self_test') if params['decode'] == 'none' else ('1', 'ok')
    assert (tm['meter'], tm['meter_reason']) == meter_expected, (name, tm, meter_expected)
    assert sorted(s2['frames']) == [0, 1, 2] and not s2['unwinds'], (name, sorted(s2['frames']), s2['unwinds'])
    for frame, h in s2['frames'].items():
        assert (h['redirected'], h['unwind'], h['writeback_source'], h['tonemap'], h['exposure'], h['fallback']) == ('1', '0', 'shader', 'agx' if params['agx'] else 'identity', 'manual', '0'), (name, frame, h)
        assert abs(float(h['ev']) - ev) < 1e-6 and h['stepped'] == '0' and h['target'] == '64x65', (name, frame, h)
    ramp = {}
    for l in lines:
        if l.startswith('RAMP '):
            f = fields(l); ramp[(int(f['frame']), int(f['row']), int(f['col']))] = f
    assert len(ramp) == 3 * 65 * 4 and all(f['uniform'] == '1' for f in ramp.values()), (name, len(ramp))
    reference_rows = agx_ref.ramp_values()
    assert len(reference_rows) == 65
    stats = {}
    for frame in (1, 2):
        inputs = read_half_image(directory / 'x3-modern-captures' / f'hdr_1_{frame}.rgba16f', 64, 65)
        presented = read_presented(directory, frame, 64, 65)
        errors = [[], [], []]; alpha_errors = []; input_errors = []
        for row in range(65):
            for col in range(4):
                index = row * 64 + col * 16 + 8
                r, g, b, a = inputs[index]
                x = reference_rows[row]
                expected_input = [(x, x, x), (x, 0.0, 0.0), (0.0, x, 0.0), (0.0, 0.0, x)][col]
                for actual, ideal in zip((r, g, b), expected_input):
                    input_errors.append(abs(actual - ideal) / ideal if ideal else abs(actual))
                assert abs(a - row / 64.0) < 1e-6, (name, frame, row, col, a)
                pr, pg, pb, pa = bgra8(presented, index)
                assert f'{pa:02x}{pr:02x}{pg:02x}{pb:02x}' == ramp[(frame, row, col)]['presented'], (name, frame, row, col)
                ref = reference_codes((r, g, b), ev, params)
                for k, e in enumerate(code_errors((pr, pg, pb), ref)):
                    errors[k].append(e)
                alpha_errors.append(abs(pa - round(255.0 * a)))
        assert max(input_errors) <= HALF_RELATIVE, (name, frame, max(input_errors))
        per_channel = {c: {'max': max(errors[k]), 'mean': sum(errors[k]) / len(errors[k])} for k, c in enumerate('rgb')}
        overall = [e for k in range(3) for e in errors[k]]
        stats[frame] = {'channels': per_channel, 'max': max(overall), 'mean': sum(overall) / len(overall),
                        'alpha_max': max(alpha_errors), 'cells': 65 * 4, 'input_max_relative_error': max(input_errors)}
        assert stats[frame]['max'] <= RAMP_MAX_CODE_ERROR, f'{name}: frame {frame} ramp max error {stats[frame]["max"]} codes exceeds 1: {per_channel}'
        assert stats[frame]['mean'] <= RAMP_MEAN_CODE_ERROR, f'{name}: frame {frame} ramp mean error {stats[frame]["mean"]} codes exceeds 0.5: {per_channel}'
        assert stats[frame]['alpha_max'] <= 1, (name, frame, stats[frame])
    assert stats[1]['channels'] == stats[2]['channels'], (name, 'the ramp is not deterministic between frames')
    return {'mode': 'hdrramp', 'agx': params['agx'], 'look': params['look'], 'decode': params['decode'], 'clamp': params['clamp'], 'ev': ev,
            'checks': int(terminal['checks']), 'frames': 3, 'cells': 65 * 4, 'ramp': stats[1], 'ramp_frame2': stats[2],
            'tonemap_line': {k: tm[k] for k in ('tonemap', 'tonemap_reason', 'meter', 'meter_reason', 'look', 'decode', 'clamp', 'exposure', 'ev_manual')},
            'self_test': s2['device'][0]['self_test'],
            'color_hashes': {int(fields(l)['frame']): fields(l)['hash'] for l in lines if l.startswith('COLOR ')}}


def parse_float(text):
    """float() tolerant of the CRT spellings of the non-finite values the hazard blocks print (1.#INF, -1.#IND, nan, inf)
    and of the IEEE-bits form (0x%08x) the fixture prints for every non-finite value since the FEX bottle's CRT
    prints NaN and the infinities as finite numbers (docs/verification/bottles.md, limitation 2)."""
    t = text.strip().lower()
    if t.startswith('0x'):
        return struct.unpack('<f', struct.pack('<I', int(t, 16)))[0]
    if '#inf' in t or t.lstrip('+-') == 'inf':
        return float('-inf') if t.startswith('-') else float('inf')
    if '#' in t or 'nan' in t:
        return float('nan')
    return float(t)


EXPOSURE_FRAMES = 120
EXPOSURE_HAZARD_FRAMES = range(30, 35)   # blocks -1 / +inf / -inf / 0.18: the finite negative is deterministic (floor, black); the
                                         # infinities are unspecified on this backend (review 23: handled like NaN, recorded not compared)
EXPOSURE_POISON_FRAMES = range(35, 40)   # a NaN block: the backend's min/max may swallow it or not; the host keeps the state finite
# The space-aware scenes (frames 40..119, EXPOSURE_SCENE lines): what each proves.
EXPOSURE_SCENES = {'sky': range(40, 50), 'menu': range(50, 60), 'sparks': range(60, 70), 'grey': range(70, 80),
                   'level_a': range(80, 90), 'level_b': range(90, 95), 'level_c': range(95, 100), 'level_d': range(100, 110), 'emitter': range(110, 120)}
EXPOSURE_STATISTIC_TOLERANCE = 0.01     # log2 units, the DLL's tile statistic against the reference tile image (FP16-truncated inputs)
EXPOSURE_TARGET_TOLERANCE = 1e-4        # EV, ev_key / ev_limit / ev_fresh recomputed by the reference on the DLL's own statistic


def exposure_frame_pixels(frame, blocks, scenes):
    """The 64x64 engine-space image (RGBA tuples, FP16-rounded) the fixture drew in `frame`, or None for a block frame
    with a non-finite block."""
    if frame in blocks:
        values = blocks[frame]
        if any(not math.isfinite(c) for v in values for c in v[:3]):
            return None
        return [values[(y // 32) * 2 + x // 32] for y in range(64) for x in range(64)]
    scene = scenes[frame]
    pixels = [scene['bg']] * 4096
    for x0, y0, x1, y1, rgba in scene['patches']:
        for y in range(y0, y1):
            for x in range(x0, x1):
                pixels[y * 64 + x] = rgba
    return pixels


def validate_hdrexposure(name, text, trace, directory, hdr_env, hdr_fault=None):
    """The meter chain and the DLL's space-aware statistic against the reference tile image of every
    deterministic frame (avg_log_l, the lit count, the centre-weighted lit median, the p99 tile maximum:
    <= 0.01 log2 units), the key rule, the highlight limit and the fresh target recomputed by the reference
    on the DLL's own statistic (<= 1e-4 EV), the dead band (the held target holds through the small
    changes of frames 90..99 and moves at 100), the adaptation sequence replayed by the reference on the
    measured targets (<= 1e-3 EV), the fixed dt, the EV offset and time constants of the run, the meter
    clip on the sun block, the presented blocks and scene patches against the reference tonemap at the
    consumed EV (<= 1 code), the design's acceptance on the scenes (the black-sky patch lifted by the key
    rule only, the menu pulled down gently, the sparks engaging the limit, the grey frame at the clamp,
    the centre object at the key against the edge emitter), and the hazard frames: a finite negative
    block metered and presented as the reference says; infinite and NaN blocks (unspecified on this
    backend) never reaching the exposure state (every state value finite; a step, if any, consumed an
    in-range meter), their stored FP16 values, presented codes and meters recorded."""
    lines = text.splitlines()
    mode_line = fields(next(l for l in lines if l.startswith('MODE ')))
    assert (mode_line['enabled'], mode_line['hdr'], mode_line['hdrexposure']) == ('1', '1', '1'), (name, mode_line)
    assert lines[-1].startswith('RESULT PASS'), (name, lines[-1])
    terminal = fields(lines[-1])
    params = hdr_env_params(hdr_env)
    assert params['agx'] and params['dt'] > 0 and params['ev_manual'] is None
    s2 = hdr_stage2_lines(trace)
    tm = s2['tonemap'][0]
    assert (tm['tonemap'], tm['meter'], tm['meter_reason'], tm['exposure']) == ('1', '1', 'ok', 'auto'), (name, tm)
    assert abs(float(tm['fixed_dt_ms']) / 1000.0 - params['dt']) < 1e-6 and float(tm['ev_offset']) == params['ev_offset'], (name, tm)
    for key, value in (('key', params['key']), ('ev_min', params['ev_min']), ('ev_max', params['ev_max']), ('meter_bg', params['meter_bg']), ('meter_min_lit', params['meter_min_lit']),
                       ('white_target', params['white_target']), ('key_pull', params['key_pull']), ('ev_deadband', params['ev_deadband']), ('edge_weight', params['edge_weight'])):
        assert abs(float(tm[key]) - value) <= 1e-4 * max(1.0, abs(value)), (name, key, tm[key], value)
    assert tm['chain_format'] in ('G32R32F', 'A32B32G32R32F') and int(tm['tile_max']) == exposure_ref.TILE_MAX, (name, tm)
    texel_bytes = 8 if tm['chain_format'] == 'G32R32F' else 16
    device_line = next(l for l in trace.splitlines() if l.startswith('hdr_device '))
    assert 'tonemap_errors=0 meter=00000000 meter_errors=0' in device_line, (name, device_line)
    # 64x64 -> one level-0 draw into the 16x16 tile image (the ring): two ring targets and two readback surfaces.
    assert s2['targets'] and int(s2['targets'][0]['chain_levels']) == 1 and int(s2['targets'][0]['chain_bytes']) == 4 * 16 * 16 * texel_bytes, (name, s2['targets'])
    states = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('EXPOSURE_STATE ')}
    blocks = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('EXPOSURE_BLOCKS ')}
    presented = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('EXPOSURE_PRESENTED ')}
    scene_lines = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('EXPOSURE_SCENE ')}
    scene_presented = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('EXPOSURE_SCENE_PRESENTED ')}
    frames = EXPOSURE_FRAMES
    assert sorted(states) == list(range(frames)) == sorted(s2['frames']), (name, sorted(states))
    assert sorted(blocks) == sorted(presented) == list(range(40)) and sorted(scene_lines) == sorted(scene_presented) == list(range(40, frames)), (name, sorted(blocks), sorted(scene_lines))
    assert not s2['unwinds'] and not s2['disabled'], (name, s2['unwinds'])
    block_values = {}
    for frame, b in blocks.items():
        block_values[frame] = [tuple(half_round(parse_float(c)) for c in b[f'b{i}'].split(',')) for i in range(4)]
    scenes = {}
    for frame, s in scene_lines.items():
        patches = []
        for i in range(int(s['patches'])):
            x0, y0, x1, y1, r, g, b, a = s[f'p{i}'].split(',')
            patches.append((int(x0), int(y0), int(x1), int(y1), tuple(half_round(parse_float(c)) for c in (r, g, b, a))))
        scenes[frame] = {'bg': tuple(half_round(parse_float(c)) for c in s['bg'].split(',')), 'patches': patches}
    for frame in EXPOSURE_HAZARD_FRAMES:
        assert block_values[frame][0][0] == -1.0 and block_values[frame][1][0] == float('inf') and block_values[frame][2][0] == float('-inf'), (name, frame, block_values[frame])
    for frame in EXPOSURE_POISON_FRAMES:
        assert math.isnan(block_values[frame][0][0]), (name, frame, block_values[frame])
    # The reference tile image and statistic of every deterministic frame (64x64 -> 16x16 tiles of 4x4
    # pixels, exact means); not built for the frames with an infinite or NaN block (unspecified).
    reference_stats = {}; unclipped_meter = {}
    kw_meter = dict(decode_mode=params['decode'], meter_bg=params['meter_bg'], meter_min_lit=params['meter_min_lit'], edge_weight=params['edge_weight'])
    kw_target = dict(key=params['key'], ev_offset=params['ev_offset'], ev_min=params['ev_min'], ev_max=params['ev_max'], white_target=params['white_target'], key_pull=params['key_pull'])
    kw_adapt = dict(tau_up=params['tau_up'], tau_down=params['tau_down'], ev_min=params['ev_min'], ev_max=params['ev_max'])
    for frame in range(frames):
        pixels = exposure_frame_pixels(frame, block_values, scenes)
        if pixels is None:
            continue
        reference_stats[frame] = exposure_ref.meter_image([p[:3] for p in pixels], 64, 64, **kw_meter)
        if frame < 40:
            unclipped_meter[frame] = exposure_ref.reduce_mean([exposure_ref.meter_level0(p[:3], params['decode'], meter_clip=1e9) for p in pixels])
    # What the reference says about the hazard blocks: the negative and -inf blocks meter at the floor, +inf at the clip.
    hazard = block_values[EXPOSURE_HAZARD_FRAMES[0]]
    assert exposure_ref.meter_level0(hazard[0][:3], params['decode']) == exposure_ref.meter_level0(hazard[2][:3], params['decode']) == math.log2(exposure_ref.METER_FLOOR), (name, hazard)
    assert exposure_ref.meter_level0(hazard[1][:3], params['decode']) == math.log2(exposure_ref.METER_CLIP), (name, hazard)
    meter_errors = {}; meter_relative = {}; statistic_errors = {}; target_errors = {}; poison_stepped = {}
    meter_low, meter_high = math.log2(exposure_ref.METER_FLOOR) - 1e-3, math.log2(exposure_ref.METER_CLIP) + 1e-3
    state_keys = ('ev', 'ev_adapted', 'ev_target', 'avg_log_l', 'exposure', 'k', 'lit_fraction', 'lit_median_log', 'p99_max_log', 'ev_key', 'ev_limit', 'lit_mean_log')
    steps = 0
    for frame in range(1, frames):
        h = s2['frames'][frame]
        st = states[frame]
        assert h['readback'] == '00000000' and h['meter'] == '00000000', (name, frame, h)
        assert all(math.isfinite(parse_float(st[k])) for k in state_keys), (name, frame, st)
        assert int(st['tiles']) == 256 and 0 <= int(st['lit']) <= 256, (name, frame, st)
        for key_state, key_frame in (('ev', 'ev'), ('avg_log_l', 'avg_log_l'), ('ev_target', 'ev_target'), ('ev_key', 'ev_key'), ('ev_limit', 'ev_limit')):
            assert abs(float(h[key_frame]) - float(st[key_state])) < 1e-4, (name, frame, key_state, h, st)
        assert abs(float(h['lit_fraction']) - float(st['lit_fraction'])) < 1e-4 and int(h['lit']) == int(st['lit']), (name, frame, h, st)
        assert abs(float(h['luma_lit']) - 2.0 ** float(st['lit_median_log'])) <= 1e-4 * max(1.0, 2.0 ** float(st['lit_median_log'])), (name, frame, h, st)
        assert abs(float(h['luma_p99']) - 2.0 ** float(st['p99_max_log'])) <= 1e-4 * max(1.0, 2.0 ** float(st['p99_max_log'])), (name, frame, h, st)
        if frame - 1 in reference_stats:
            assert h['stepped'] == '1', (name, frame, h)
            ref = reference_stats[frame - 1]
            measured = float(st['avg_log_l'])
            meter_errors[frame] = abs(measured - ref['avg_log_l'])
            meter_relative[frame] = abs(measured - ref['avg_log_l']) / abs(ref['avg_log_l']) if ref['avg_log_l'] else abs(measured - ref['avg_log_l'])
            assert meter_errors[frame] <= max(0.005, EXPOSURE_METER_TOLERANCE * abs(ref['avg_log_l'])), (name, frame, measured, ref['avg_log_l'])
            # The statistic: the lit count exactly (no tile of any scene sits near the background floor), the
            # centre-weighted median, the weighted mean and the p99 maximum within the FP16 input truncation.
            assert int(st['lit']) == ref['lit'] and abs(float(st['lit_fraction']) - ref['lit_fraction']) < 1e-6, (name, frame, st['lit'], ref['lit'])
            errors = {k: abs(float(st[k]) - ref[k]) for k in ('lit_median_log', 'lit_mean_log', 'p99_max_log')}
            statistic_errors[frame] = errors
            assert max(errors.values()) <= EXPOSURE_STATISTIC_TOLERANCE, (name, frame, errors, st, {k: ref[k] for k in errors})
            # The target arithmetic on the DLL's own statistic (isolates the host rule from the chain).
            measured_stats = dict(ref, lit=int(st['lit']), lit_median_log=float(st['lit_median_log']), p99_max_log=float(st['p99_max_log']),
                                  neutral=int(st['lit']) < params['meter_min_lit'] * 256)
            target = exposure_ref.exposure_target(measured_stats, **kw_target)
            target_errors[frame] = {k: abs(float(st[k]) - target[k]) for k in ('ev_key', 'ev_limit')}
            target_errors[frame]['ev_fresh'] = abs(float(h['ev_fresh']) - target['ev_target'])
            assert max(target_errors[frame].values()) <= EXPOSURE_TARGET_TOLERANCE, (name, frame, target_errors[frame], st, target)
        else:
            # An infinite or NaN block: either the chain's result was rejected by
            # the host (no step, the state held) or the backend swallowed the
            # value and the step consumed a meter inside the clamp range.
            assert h['stepped'] in ('0', '1'), (name, frame, h)
            poison_stepped[frame] = int(h['stepped'])
            if h['stepped'] == '1':
                assert meter_low <= float(st['avg_log_l']) <= meter_high, (name, frame, st)
        steps += int(h['stepped'])
        assert int(h['steps']) == steps, (name, frame, h, steps)
        if h['stepped'] == '1':
            assert abs(float(st['dt']) - params['dt']) < 1e-6, (name, frame, st)
    h0 = s2['frames'][0]
    assert h0['stepped'] == '0' and h0['meter'] == '00000000' and float(states[0]['ev']) == 0.0, (name, h0, states[0])
    # Adaptation replayed on the measured held targets (isolates the host arithmetic; a frame without a
    # step holds), the dead band replayed on the measured fresh targets, and, for the deterministic
    # frames, the whole model on the reference statistics (end to end).
    expected = [0.0]; held = None; deadband_errors = []
    for frame in range(1, frames):
        h = s2['frames'][frame]
        if h['stepped'] == '1':
            held = exposure_ref.apply_deadband(held, float(h['ev_fresh']), params['ev_deadband'])
            deadband_errors.append(abs(held - float(h['ev_target'])))
            held = float(h['ev_target'])
            expected.append(exposure_ref.adapt(expected[-1], float(h['ev_target']), params['dt'], **kw_adapt))
        else:
            expected.append(expected[-1])
    ev_errors = [abs(float(states[f]['ev']) - expected[f]) for f in range(frames)]
    assert max(ev_errors) <= EXPOSURE_EV_TOLERANCE, (name, max(ev_errors), ev_errors)
    assert max(deadband_errors) <= EXPOSURE_TARGET_TOLERANCE, (name, max(deadband_errors))
    deterministic = EXPOSURE_HAZARD_FRAMES[0]   # frames whose consumed meter is deterministic: 0 .. 29 (frame 30 consumed frame 29's)
    end_to_end = exposure_ref.simulate([reference_stats[f] for f in range(deterministic)], [params['dt']] * deterministic, 0.0,
                                       ev_deadband=params['ev_deadband'], **kw_target, tau_up=params['tau_up'], tau_down=params['tau_down'])
    end_to_end_errors = [abs(float(states[f]['ev']) - end_to_end[f]) for f in range(deterministic)]
    ev_sequence = [float(states[f]['ev']) for f in range(frames)]
    held_targets = [float(states[f]['ev_target']) for f in range(frames)]
    fresh_targets = [float(s2['frames'][f]['ev_fresh']) for f in range(frames)]
    # Direction: the dark scene raises the EV, the bright one lowers it, the clipped sun block bounds the drop.
    assert ev_sequence[9] > ev_sequence[1] > ev_sequence[0] and ev_sequence[19] < ev_sequence[11], (name, ev_sequence)
    assert reference_stats[20]['avg_log_l'] < unclipped_meter[20] - 1.0, (name, reference_stats[20]['avg_log_l'], unclipped_meter[20])  # the clip mattered by more than one stop
    # The design's acceptance on the scenes (the state at frame f+1 carries frame f's meter): the sky
    # patch is lifted by the key rule only, its limit far above; the menu is pulled down gently; the sparks'
    # limit is the clip's; the grey frame asks for the clamp; the small changes hold the target, the large
    # one moves it; the centre object stays at the key against the edge emitter.
    def state_of(scene):
        return states[EXPOSURE_SCENES[scene][-1] + 1] if EXPOSURE_SCENES[scene][-1] + 1 < frames else states[EXPOSURE_SCENES[scene][-1]]
    sky = state_of('sky'); patch_log = exposure_ref.meter_level0(scenes[40]['patches'][0][4][:3], params['decode'])
    assert abs(float(sky['ev_key']) - (math.log2(params['key']) - patch_log + params['ev_offset'])) < 0.02 and float(sky['ev_limit']) > float(sky['ev_key']) + 3.0, (name, sky)
    assert int(sky['lit']) == 16, (name, sky)
    menu = state_of('menu')
    assert abs(float(menu['ev_key']) - (params['key_pull'] * math.log2(params['key']) + params['ev_offset'])) < 0.02 and float(menu['ev_key']) > -1.0, (name, menu)
    sparks = state_of('sparks')
    assert abs(float(sparks['ev_limit']) - (math.log2(params['white_target'] * exposure_ref.TONEMAP_WHITE) - math.log2(exposure_ref.METER_CLIP))) < 0.02, (name, sparks)
    assert float(sparks['ev_limit']) < float(sparks['ev_key']) and abs(float(s2['frames'][EXPOSURE_SCENES['sparks'][-1] + 1]['ev_fresh']) - float(sparks['ev_limit'])) < 1e-4, (name, sparks)
    grey = state_of('grey')
    assert float(grey['ev_key']) > params['ev_max'] and abs(float(s2['frames'][EXPOSURE_SCENES['grey'][-1] + 1]['ev_fresh']) - params['ev_max']) < 1e-4, (name, grey)
    # Prove the synthetic stimulus itself has the intended margins for this
    # case's offset/clamp, independently of the measured held-target history.
    reference_level_targets = {label: exposure_ref.exposure_target(reference_stats[EXPOSURE_SCENES[label][0]], **kw_target)['ev_target']
                               for label in ('grey', 'level_a', 'level_b', 'level_c', 'level_d')}
    a_target = reference_level_targets['level_a']
    assert abs(a_target - reference_level_targets['grey']) > params['ev_deadband'], (name, 'A must escape the preceding clamp', reference_level_targets)
    assert all(0 < abs(reference_level_targets[label] - a_target) <= params['ev_deadband'] for label in ('level_b', 'level_c')), (name, 'B/C must lie within A band', reference_level_targets)
    assert abs(reference_level_targets['level_d'] - a_target) > params['ev_deadband'], (name, 'D must leave A band', reference_level_targets)
    first_a = EXPOSURE_SCENES['level_a'][0] + 1
    assert abs(held_targets[first_a] - held_targets[first_a - 1]) > params['ev_deadband'] and s2['frames'][first_a]['ev_target'] == s2['frames'][first_a]['ev_fresh'], (name, 'initial A did not escape clamp', first_a)
    band = [held_targets[f + 1] for f in EXPOSURE_SCENES['level_a']] + [held_targets[f + 1] for f in EXPOSURE_SCENES['level_b']] + [held_targets[f + 1] for f in EXPOSURE_SCENES['level_c']]
    assert max(band) - min(band) < 1e-6, (name, 'the held target moved inside the dead band', band)
    band_fresh = [fresh_targets[f + 1] for f in list(EXPOSURE_SCENES['level_b']) + list(EXPOSURE_SCENES['level_c'])]
    assert all(0.0 < abs(v - band[0]) <= params['ev_deadband'] for v in band_fresh), (name, 'the small changes were not inside the band', band[0], band_fresh)
    moved = held_targets[EXPOSURE_SCENES['level_d'][0] + 1]
    # Compare equally formatted fields: the seam state prints six decimals,
    # whereas hdr_frame prints five (their rounding alone can differ by 5e-6).
    moved_frame = s2['frames'][EXPOSURE_SCENES['level_d'][0] + 1]
    assert abs(moved - band[0]) > params['ev_deadband'] and moved_frame['ev_target'] == moved_frame['ev_fresh'], (name, 'the large change did not move the target', band[0], moved, moved_frame)
    emitter = state_of('emitter'); object_log = exposure_ref.meter_level0(scenes[110]['patches'][1][4][:3], params['decode'])
    assert abs(float(emitter['lit_median_log']) - object_log) < 0.02 and abs(float(emitter['ev_key']) - exposure_ref.ev_key(object_log, params['key'], params['ev_offset'], params['key_pull'])) < 0.02, (name, emitter)
    unweighted = exposure_ref.exposure_target(exposure_ref.meter_image([p[:3] for p in exposure_frame_pixels(110, block_values, scenes)], 64, 64, **dict(kw_meter, edge_weight=1.0)), **kw_target)
    assert abs(unweighted['ev_target'] - (params['key_pull'] * math.log2(params['key']) + params['ev_offset'])) < 0.02, (name, 'unweighted the emitter would have set the target', unweighted)
    # Presented blocks and scene patches against the reference tonemap at the consumed EV (infinite and NaN blocks are not compared: unspecified).
    presented_errors = []
    for frame in range(frames):
        ev = float(states[frame]['ev'])
        if frame < 40:
            for i in range(4):
                code = int(presented[frame][f'p{i}'], 16)
                pr, pg, pb, pa = (code >> 16) & 255, (code >> 8) & 255, code & 255, code >> 24
                assert abs(pa - round(255.0 * block_values[frame][i][3])) <= 1, (name, frame, i, pa)
                if any(not math.isfinite(c) for c in block_values[frame][i][:3]):
                    continue
                ref = reference_codes(block_values[frame][i][:3], ev, params)
                presented_errors.extend(code_errors((pr, pg, pb), ref))
            assert presented[frame]['nonuniform'] == '0', (name, frame)
        else:
            samples = [(scene_presented[frame]['bg'], scenes[frame]['bg'])] + [(scene_presented[frame][f'p{i}'], patch[4]) for i, patch in enumerate(scenes[frame]['patches'])]
            for code, rgba in samples:
                code = int(code, 16)
                pr, pg, pb, pa = (code >> 16) & 255, (code >> 8) & 255, code & 255, code >> 24
                assert abs(pa - round(255.0 * rgba[3])) <= 1, (name, frame, pa, rgba)
                presented_errors.extend(code_errors((pr, pg, pb), reference_codes(rgba[:3], ev, params)))
            assert scene_presented[frame]['nonuniform'] == '0', (name, frame)
    assert max(presented_errors) <= RAMP_MAX_CODE_ERROR, (name, max(presented_errors))
    # The record of the unspecified inputs: what the FP16 target stored (the capture-frame readback), what was presented and metered.
    hazard_record = {}
    for frame in (EXPOSURE_HAZARD_FRAMES[0], EXPOSURE_POISON_FRAMES[0]):
        stored = read_half_image(directory / 'x3-modern-captures' / f'hdr_1_{frame}.rgba16f', 64, 64)
        centres = [stored[(((i // 2) * 32 + 16) * 64) + (i % 2) * 32 + 16] for i in range(4)]
        for i in range(4):
            expected = block_values[frame][i]
            for actual, ideal in zip(centres[i], expected):
                # Finite values: the backend truncates to FP16 (one ulp, HALF_RELATIVE); the infinities and the NaN must be stored as such.
                assert (math.isnan(ideal) and math.isnan(actual)) or actual == ideal \
                    or (math.isfinite(ideal) and abs(actual - ideal) <= HALF_RELATIVE * abs(ideal)), (name, frame, i, centres[i], expected)
        hazard_record[frame] = {'stored': [[str(c) for c in px] for px in centres], 'presented': [presented[frame][f'p{i}'] for i in range(4)],
                                'meter_consumed_next_frame': float(states[frame + 1]['avg_log_l']), 'stepped_next_frame': int(s2['frames'][frame + 1]['stepped'])}
    scene_record = {scene: {k: float(state_of(scene)[k]) for k in ('ev', 'ev_target', 'ev_key', 'ev_limit', 'lit_fraction', 'lit_median_log', 'p99_max_log', 'avg_log_l')}
                    for scene in EXPOSURE_SCENES}
    return {'mode': 'hdrexposure', 'frames': frames, 'checks': int(terminal['checks']), 'dt_s': params['dt'], 'ev_offset': params['ev_offset'],
            'tau_up': params['tau_up'], 'tau_down': params['tau_down'], 'look': params['look'], 'chain_format': tm['chain_format'],
            'meter_max_abs_error_log2': max(meter_errors.values()), 'meter_max_relative_error': max(meter_relative.values()),
            'statistic_max_error_log2': {k: max(e[k] for e in statistic_errors.values()) for k in ('lit_median_log', 'lit_mean_log', 'p99_max_log')},
            'target_max_error_ev': {k: max(e[k] for e in target_errors.values()) for k in ('ev_key', 'ev_limit', 'ev_fresh')},
            'ev_max_error_replayed': max(ev_errors), 'ev_max_error_end_to_end': max(end_to_end_errors), 'deadband_max_error_ev': max(deadband_errors),
            'ev_sequence': ev_sequence, 'held_targets': held_targets, 'fresh_targets': fresh_targets,
            'reference_meter': [reference_stats[f]['avg_log_l'] if f in reference_stats else None for f in range(frames)],
            'measured_meter': [float(states[f]['avg_log_l']) for f in range(frames)],
            'scenes': scene_record, 'emitter_unweighted_target': unweighted['ev_target'],
            'sun_clip_effect_log2': unclipped_meter[20] - reference_stats[20]['avg_log_l'],
            'hazard_frames': list(EXPOSURE_HAZARD_FRAMES), 'poison_frames': list(EXPOSURE_POISON_FRAMES), 'unspecified_stepped': poison_stepped, 'hazard': hazard_record,
            'presented_max_code_error': max(presented_errors), 'chain_bytes': int(s2['targets'][0]['chain_bytes']),
            'phase_us': {k: [float(s2['frames'][f][k]) for f in range(frames)] for k in ('meter_us', 'readback_us', 'writeback_draw_us')},
            'color_hashes': {int(fields(l)['frame']): fields(l)['hash'] for l in lines if l.startswith('COLOR ')}}

TONEMAP_FAULT_SCRIPT = {0: dict(fault=0, tonemapped=1, unwind=0, reason='none', recheck='none', meter='00000000', stepped=0),
                        1: dict(fault=11, tonemapped=0, unwind=1, reason='tonemap', recheck='none', meter='00000000', stepped=1),
                        2: dict(fault=15, tonemapped=1, unwind=0, reason='none', recheck='pass', meter='00000000', stepped=0),
                        3: dict(fault=13, tonemapped=1, unwind=0, reason='none', recheck='none', meter='80004005', stepped=1),
                        4: dict(fault=0, tonemapped=1, unwind=0, reason='none', recheck='none', meter='00000000', stepped=0),
                        5: dict(fault=11, tonemapped=0, unwind=1, reason='tonemap', recheck='none', meter='00000000', stepped=1),
                        6: dict(fault=11, tonemapped=0, unwind=1, reason='tonemap', recheck='pass', meter='00000000', stepped=1),
                        7: dict(fault=0, tonemapped=0, unwind=0, reason='none', recheck='pass', meter='00000001', stepped=0),
                        8: dict(fault=0, tonemapped=0, unwind=0, reason='none', recheck='none', meter='00000001', stepped=0)}


def compare_image_to_reference(directory, frame, ev, params, identity):
    """Every pixel of presented_<frame> against the reference of hdr_1_<frame>.rgba16f (identity: the clamped conversion)."""
    inputs = read_half_image(directory / 'x3-modern-captures' / f'hdr_1_{frame}.rgba16f', 64, 64)
    presented = read_presented(directory, frame, 64, 64)
    p = dict(params, agx=not identity)
    errors = []; alpha = []
    for i, (r, g, b, a) in enumerate(inputs):
        pr, pg, pb, pa = bgra8(presented, i)
        errors.extend(code_errors((pr, pg, pb), reference_codes((r, g, b), ev, p)))
        alpha.append(abs(pa - round(255.0 * min(max(a, 0.0), 1.0))))
    return {'max': max(errors), 'mean': sum(errors) / len(errors), 'alpha_max': max(alpha), 'pixels': len(inputs)}


def validate_hdr_taa(name, text, trace, directory, hdr_env):
    """Stage 3 (HDR + TAA, seam script): the presented frame is
    tonemap(resolve(HDR)). validate_case already required the DLL's resolved
    FP16 image to equal the reference pass's output byte for byte; here every
    presented 8-bit frame is compared against the AgX reference of that
    resolved image at the EV the frame consumed (one code per channel, alpha
    carried), k on the hdr_frame line equals exp2(EV) (or the X3M_TAA_K
    override) and the frame line reports the HDR resolve with that k."""
    params = hdr_env_params(hdr_env)
    s2 = hdr_stage2_lines(trace)
    tl = trace.splitlines()
    frames = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_frame ')}
    taa_readbacks = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_taa_readback ')}
    override = hdr_env.get('X3M_TAA_K')
    images, ks, evs = {}, {}, {}
    for frame in range(1, 9):
        h, f = s2['frames'][frame], frames[frame]
        ev = float(h['ev'])
        expected_k = float(override) if override is not None else (2.0 ** ev if params['agx'] else 0.0)
        tolerance = 1e-4 * max(1.0, expected_k)
        assert abs(float(h['k']) - expected_k) <= tolerance, (name, frame, h['k'], expected_k)
        assert f['taa_hdr'] == '1' and f['taa_resolved'] == '1' and abs(float(f['taa_k']) - expected_k) <= tolerance, (name, frame, f)
        assert h['tonemapped'] == str(int(params['agx'])) and h['writeback_source'] == 'shader' and h['unwind'] == '0', (name, frame, h)
        resolved = read_half_image(directory / 'x3-modern-captures' / taa_readbacks[frame]['file'], 64, 64)
        presented = read_presented(directory, frame, 64, 64)
        errors, alpha = [], []
        for i, (r, g, b, a) in enumerate(resolved):
            pr, pg, pb, pa = bgra8(presented, i)
            errors.extend(code_errors((pr, pg, pb), reference_codes((r, g, b), ev, params)))
            alpha.append(abs(pa - round(255.0 * min(max(a, 0.0), 1.0))))
        images[frame] = {'max': max(errors), 'mean': sum(errors) / len(errors), 'alpha_max': max(alpha)}
        assert images[frame]['max'] <= 1 and images[frame]['alpha_max'] <= 1, (name, frame, images[frame])
        ks[frame], evs[frame] = float(h['k']), ev
    hdr_taa = [fields(l) for l in text.splitlines() if l.startswith('TAA_HDR ')]
    assert len(hdr_taa) >= 8 and all(int(t['mismatches']) == 0 and t['agx'] == str(int(params['agx'])) for t in hdr_taa), (name, hdr_taa[:3])
    return {'frames': sorted(images), 'images': images, 'k': ks, 'ev': evs, 'fixture_worst_code': max(int(t['worst_code']) for t in hdr_taa),
            'history_frames': [f for f in range(1, 9) if frames[f]['taa_history'] == '1'],
            'max_code_error': max(v['max'] for v in images.values()), 'mean_code_error': max(v['mean'] for v in images.values())}


def rcas_reference(display, width, height, gain):
    """RCAS of a display-referred float RGB image (rows of (r, g, b) in [0, 1],
    clamp addressed) as src/temporal/rcas.hlsl computes it, in double: the
    saturated five-tap cross, the luma noise detector, the guarded peak-range
    limiter, the single lobe scaled by `gain` (exp2(-stops)) and the clamp to
    the taps' own min/max."""
    def tap(x, y):
        return display[min(max(y, 0), height - 1) * width + min(max(x, 0), width - 1)]
    out = []
    for y in range(height):
        for x in range(width):
            taps = [tuple(min(max(c, 0.0), 1.0) for c in t) for t in (tap(x, y - 1), tap(x - 1, y), tap(x, y), tap(x + 1, y), tap(x, y + 1))]
            b, d, e, f, h = taps
            lumas = [0.5 * t[0] + t[1] + 0.5 * t[2] for t in taps]
            nz = min(max(abs(0.25 * (lumas[0] + lumas[1] + lumas[3] + lumas[4]) - lumas[2]) / max(max(lumas) - min(lumas), 1.0 / 256.0), 0.0), 1.0)
            nz = 1.0 - 0.5 * nz
            lobe_rgb = []
            for c in range(3):
                mn4, mx4 = min(b[c], d[c], f[c], h[c]), max(b[c], d[c], f[c], h[c])
                hit_min = mn4 / max(4.0 * mx4, 1.0 / 4096.0)
                hit_max = (1.0 - mx4) / min(4.0 * mn4 - 4.0, -1.0 / 4096.0)
                lobe_rgb.append(max(-hit_min, hit_max))
            lobe = max(-0.1875, min(max(lobe_rgb), 0.0)) * gain * nz
            pix = []
            for c in range(3):
                v = ((b[c] + d[c] + f[c] + h[c]) * lobe + e[c]) / (4.0 * lobe + 1.0)
                pix.append(min(max(v, min(b[c], d[c], f[c], h[c], e[c])), max(b[c], d[c], f[c], h[c], e[c])))
            out.append(tuple(pix))
    return out


def sharpen_gain(sharpen):
    """X3M_TAA_SHARPEN -> the RCAS gain (sharpen.h: stops = 2 * (1 - s), gain = exp2(-stops))."""
    return 2.0 ** (-2.0 * (1.0 - sharpen))


def neighbourhood_bounds(codes, width, height):
    """Per pixel and channel the min/max 8-bit code of the clamp-addressed 3x3 neighbourhood."""
    bounds = []
    for y in range(height):
        for x in range(width):
            lo, hi = [255] * 3, [0] * 3
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    t = codes[min(max(y + dy, 0), height - 1) * width + min(max(x + dx, 0), width - 1)]
                    for c in range(3):
                        lo[c] = min(lo[c], t[c]); hi[c] = max(hi[c], t[c])
            bounds.append((tuple(lo), tuple(hi)))
    return bounds


def validate_sharpen(name, text, trace, directory, hdr_env, hdr, sharpen, width=64, height=64):
    """Post-resolve sharpen, frames 1-8 of the seam script: the presented
    8-bit frame equals the Python RCAS reference of the display-referred
    resolved image within one code per channel (8-bit route: the resolved
    FP16 values themselves; HDR identity: their clamp; HDR AgX: the tonemap
    at the EV the frame consumed, i.e. sharpened after the tonemap), lies
    inside the 3x3 min/max of the unsharpened display codes, carries alpha,
    and differs from the unsharpened image somewhere. With the tonemap the
    other order, AgX(RCAS(resolved)), is evaluated too and must be told
    apart by the presented frame (pixels differing by more than one code)."""
    params = hdr_env_params(hdr_env)
    tl = trace.splitlines()
    frames = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_frame ')}
    taa_readbacks = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_taa_readback ')}
    present_readbacks = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_present_readback ')}
    s2 = hdr_stage2_lines(trace) if hdr else None
    gain = sharpen_gain(sharpen)
    images, worst_alt = {}, 0
    for frame in range(1, 9):
        f = frames[frame]
        assert f['taa_resolved'] == '1' and f['taa_sharpen'] == '1' and f['taa_hdr'] == str(int(hdr)), (name, frame, f)
        ev = 0.0
        if hdr:
            h = s2['frames'][frame]
            ev = float(h['ev'])
            assert h['sharpened'] == '1' and h['sharpen'] == 'ok' and h['sharpen_fallback'] == '0' and h['writeback_source'] == 'shader' and h['unwind'] == '0', (name, frame, h)
            assert h['tonemapped'] == str(int(params['agx'])), (name, frame, h)
        resolved = read_half_image(directory / 'x3-modern-captures' / taa_readbacks[frame]['file'], width, height)
        display = [tuple(c / 255.0 for c in reference_codes((r, g, b), ev, params)) for r, g, b, a in resolved]
        expected = rcas_reference(display, width, height, gain)
        unsharpened = [tuple(int(round(255.0 * c)) for c in px) for px in display]
        bounds = neighbourhood_bounds(unsharpened, width, height)
        presented = read_presented(directory, frame, width, height)
        # The DLL's own readback of the main target after the sharpen draw
        # (8-bit route) or the write-back (HDR route) is the presented image.
        present_line = present_readbacks[frame]
        assert present_line['result'] == '00000000' and present_line['file'] == f'present_1_{frame}.bgra8', (name, frame, present_line)
        assert (directory / 'x3-modern-captures' / present_line['file']).read_bytes() == presented, f'{name}: frame {frame} present_1_{frame}.bgra8 differs from the presented image'
        errors, alpha, outside, changed = [], [], 0, 0
        for i, px in enumerate(expected):
            pr, pg, pb, pa = bgra8(presented, i)
            errors.append(max(abs(p - 255.0 * e) for p, e in zip((pr, pg, pb), px)))
            alpha.append(abs(pa - round(255.0 * min(max(resolved[i][3], 0.0), 1.0))))
            lo, hi = bounds[i]
            if any(p < l or p > h for p, l, h in zip((pr, pg, pb), lo, hi)):
                outside += 1
            if (pr, pg, pb) != unsharpened[i]:
                changed += 1
        entry = {'max_code_error': max(errors), 'mean_code_error': sum(errors) / len(errors), 'alpha_max': max(alpha), 'outside_3x3': outside, 'changed': changed,
                 'present_readback': present_line['file'], 'present_equals_presented': True}
        if params['agx']:
            # The other order: sharpen the engine-space image, then tonemap it.
            alternative = rcas_reference([tuple(min(max(c, 0.0), 1.0) for c in (r, g, b)) for r, g, b, a in resolved], width, height, gain)
            distinct = 0
            for i, px in enumerate(alternative):
                pr, pg, pb, _ = bgra8(presented, i)
                if max(abs(p - r) for p, r in zip((pr, pg, pb), reference_codes(px, ev, params))) > 1.0:
                    distinct += 1
            entry['pixels_distinct_from_sharpen_before_tonemap'] = distinct
            worst_alt = max(worst_alt, distinct)
        images[frame] = entry
        assert entry['max_code_error'] <= SHARPEN_MAX_CODE_ERROR + 0.5 and entry['alpha_max'] <= 1 and entry['outside_3x3'] == 0 and entry['changed'] > 0, (name, frame, entry)
    if params['agx']:
        assert worst_alt > 0, (name, 'the presented frames cannot be told from sharpen-before-tonemap')
    lines = [fields(l) for l in text.splitlines() if l.startswith('TAA_SHARPEN ')]
    assert len(lines) >= 8 and all(int(t['mismatches']) == 0 and float(t['sharpen']) == sharpen for t in lines), (name, lines[:3])
    return {'sharpen': sharpen, 'gain': gain, 'frames': sorted(images), 'images': images,
            'max_code_error': max(v['max_code_error'] for v in images.values()), 'mean_code_error': max(v['mean_code_error'] for v in images.values()),
            'changed_fraction': sum(v['changed'] for v in images.values()) / (8.0 * width * height),
            'present_readbacks_equal_presented': all(v['present_equals_presented'] for v in images.values()),
            'history_frames': [f for f in range(1, 9) if frames[f]['taa_history'] == '1']}


def validate_identity_k(name, trace, hdr_env):
    """Stage 3 with the identity write-back (no exposure model): the DLL must
    derive k = 0, the unweighted resolve, on every frame, and the frame line
    must report that k for every HDR resolve. The fixture's reference pass
    takes the DLL's k from the exposure export, so the derivation itself is
    checked only here on these cases (review 24); an X3M_TAA_K override is
    the value it names."""
    tl = trace.splitlines()
    tonemap = [fields(l) for l in tl if l.startswith('hdr_tonemap ')]
    hdr_frames = [fields(l) for l in tl if l.startswith('hdr_frame ')]
    resolves = [f for f in (fields(l) for l in tl if l.startswith('motion_output_frame ')) if f.get('taa_hdr') == '1']
    expected = float((hdr_env or {}).get('X3M_TAA_K', 0.0))
    assert tonemap and all(t['tonemap'] == '0' for t in tonemap), (name, tonemap[:1])
    assert hdr_frames and all(float(h['k']) == expected for h in hdr_frames), (name, sorted({h['k'] for h in hdr_frames}))
    assert resolves and all(float(f['taa_k']) == expected for f in resolves), (name, sorted({f['taa_k'] for f in resolves}))
    return {'hdr_frames': len(hdr_frames), 'hdr_resolves': len(resolves), 'k': expected}


# The tonemap-fault script with the resolve on the FP16 scene (stage 3):
# f1/f7 the resolve fails (fault 14: the unresolved scene is written back,
# the history drops), f3 the tonemap draw fails (consumed by the flush the
# fixture's pre-boundary read triggers: the identity fallback, an unwind, the
# end write-back tonemaps the resolved image again), f5 the meter fails (the
# flush's chain; the end's chain succeeds).
TONEMAP_TAA_FAULT_SCRIPT = {0: dict(fault=0, resolved=1, history=0, unwind=0, recheck='none'),
                            1: dict(fault=14, resolved=0, history=0, unwind=0, recheck='none'),
                            2: dict(fault=0, resolved=1, history=0, unwind=0, recheck='none'),
                            3: dict(fault=11, resolved=1, history=1, unwind=1, recheck='none'),
                            4: dict(fault=0, resolved=1, history=1, unwind=0, recheck='pass'),
                            5: dict(fault=13, resolved=1, history=1, unwind=0, recheck='none'),
                            6: dict(fault=0, resolved=1, history=1, unwind=0, recheck='none'),
                            7: dict(fault=14, resolved=0, history=0, unwind=0, recheck='none'),
                            8: dict(fault=0, resolved=1, history=0, unwind=0, recheck='none'),
                            # a Reset before f9: the FP16 target and the pass's histories are re-created, f9 resolves current-only
                            9: dict(fault=0, resolved=1, history=0, unwind=0, recheck='none'),
                            10: dict(fault=0, resolved=1, history=1, unwind=0, recheck='none'),
                            11: dict(fault=0, resolved=1, history=1, unwind=0, recheck='none')}


def validate_hdrtonemapfault_taa(name, text, trace, directory, hdr_env):
    lines = text.splitlines()
    terminal = fields(lines[-1])
    params = hdr_env_params(hdr_env)
    s2 = hdr_stage2_lines(trace)
    tl = trace.splitlines()
    frames = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_frame ')}
    failed = [fields(l) for l in tl if l.startswith('motion_output_taa_failed ')]
    taa_readbacks = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_taa_readback ')}
    faults = {int(fields(l)['frame']): int(fields(l)['fault']) for l in lines if l.startswith('HDR_TONEMAP_FAULT ')}
    assert sorted(s2['frames']) == sorted(TONEMAP_TAA_FAULT_SCRIPT), (name, sorted(s2['frames']))
    images = {}
    for frame, expect in TONEMAP_TAA_FAULT_SCRIPT.items():
        h, f = s2['frames'][frame], frames[frame]
        assert faults[frame] == expect['fault'], (name, frame, faults)
        assert (f['taa_attempted'], f['taa_hdr'], f['taa_resolved'], f['taa_history']) == ('1', '1', str(expect['resolved']), str(expect['history'])), (name, frame, f)
        assert f['taa_result'] == ('80004005' if expect['fault'] == 14 else '00000000'), (name, frame, f)
        assert (h['redirected'], h['writeback_source'], h['tonemapped'], h['unwind'], h['recheck']) == ('1', 'shader', '1', str(expect['unwind']), expect['recheck']), (name, frame, h)
        # The presented frame: AgX of the resolved image, or of the unresolved
        # scene when the resolve failed (capture frames 1..8 carry the readbacks).
        if expect['resolved']:
            if frame not in taa_readbacks:
                continue
            source = taa_readbacks[frame]['file']
        else:
            source = f'hdr_1_{frame}.rgba16f'
            if not (directory / 'x3-modern-captures' / source).is_file():
                continue
        inputs = read_half_image(directory / 'x3-modern-captures' / source, 64, 64)
        presented = read_presented(directory, frame, 64, 64)
        errors = []
        for i, (r, g, b, a) in enumerate(inputs):
            pr, pg, pb, pa = bgra8(presented, i)
            errors.extend(code_errors((pr, pg, pb), reference_codes((r, g, b), float(h['ev']), params)))
        images[frame] = {'max': max(errors), 'mean': sum(errors) / len(errors), 'source': source}
        assert images[frame]['max'] <= 1, (name, frame, images[frame])
    assert len(failed) == 2 and all(x['hdr'] == '1' and x['result'] == '80004005' for x in failed) and sorted(int(x['frame']) for x in failed) == [1, 7], (name, failed)
    resets = [fields(l) for l in tl if l.startswith('motion_output_reset ')]
    assert len(resets) == 1 and resets[0]['result'] == '00000000' and 'RESET PASS' in text, (name, resets)
    assert len(s2['targets']) == 2 and all(t['create'] == '00000000' for t in s2['targets']), (name, s2['targets'])  # the target before and after the Reset
    assert [u['reason'] for u in s2['unwinds']] == ['tonemap'] and sorted(s2['rechecks']) == [4] and s2['rechecks'][4]['passed'] == '1', (name, s2['unwinds'], s2['rechecks'])
    return {'mode': 'hdrtonemapfault', 'taa': True, 'checks': int(terminal['checks']), 'restorations': int(terminal['restorations']),
            'frames': len(TONEMAP_TAA_FAULT_SCRIPT), 'images': images, 'failed_resolves': [int(x['frame']) for x in failed],
            'hdr_frames': {fr: {k: h[k] for k in ('tonemap', 'tonemapped', 'unwind', 'unwind_reason', 'fallback', 'recheck', 'meter', 'stepped', 'ev', 'k')} for fr, h in s2['frames'].items()},
            'taa_frames': {fr: {k: f[k] for k in ('taa_resolved', 'taa_history', 'taa_result', 'taa_hdr', 'taa_k')} for fr, f in frames.items()}}


def validate_hdrtonemapfault(name, text, trace, directory, hdr_env, hdr_fault=None):
    """The tonemap ladder: a failed tonemap draw takes the identity draw (one
    hdr_unwind=tonemap line, recheck at the next latch), a failed meter chain
    leaves the tonemap and holds the exposure, three draw failures disable the
    tonemap; presented frames checked against the reference of the FP16
    readback. With the program forced absent at attach (fault 12) every frame
    is the identity write-back."""
    lines = text.splitlines()
    mode_line = fields(next(l for l in lines if l.startswith('MODE ')))
    assert (mode_line['seam'], mode_line['enabled'], mode_line['hdr'], mode_line['hdrtonemapfault']) == ('1', '1', '1', '1'), (name, mode_line)
    assert lines[-1].startswith('RESULT PASS'), (name, lines[-1])
    if mode_line['taa'] == '1':
        return validate_hdrtonemapfault_taa(name, text, trace, directory, hdr_env)
    terminal = fields(lines[-1])
    params = hdr_env_params(hdr_env)
    s2 = hdr_stage2_lines(trace)
    tm = s2['tonemap'][0]
    states = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('EXPOSURE_STATE ')}
    result = {'mode': 'hdrtonemapfault', 'checks': int(terminal['checks']), 'restorations': int(terminal['restorations']),
              'color_hashes': {int(fields(l)['frame']): fields(l)['hash'] for l in lines if l.startswith('COLOR ')}}
    if hdr_fault == '16':
        # The real self-test copy/lock/unlock completes, then the seam reports
        # an unlock failure. Correct pixels alone must not enable the meter.
        assert (tm['tonemap'], tm['tonemap_reason'], tm['meter'], tm['meter_reason']) == ('1', 'ok', '0', 'self_test'), (name, tm)
        device_line = next(l for l in trace.splitlines() if l.startswith('hdr_device '))
        assert 'meter=80004005 meter_errors=0' in device_line, (name, device_line)
        assert sorted(s2['frames']) == [0, 1, 2] and not s2['unwinds'] and not s2['disabled'], (name, s2)
        for frame, h in s2['frames'].items():
            assert (h['tonemap'], h['tonemapped'], h['stepped'], h['steps']) == ('agx', '1', '0', '0'), (name, frame, h)
            assert float(states[frame]['ev']) == 0.0, (name, frame, states[frame])
        images = {frame: compare_image_to_reference(directory, frame, 0.0, params, identity=False) for frame in (1, 2)}
        assert all(v['max'] <= 1 and v['alpha_max'] <= 1 for v in images.values()), (name, images)
        result.update(frames=3, meter_selftest_unlock_refused=True, images=images)
        return result
    if hdr_fault == '12':
        assert (tm['tonemap'], tm['tonemap_reason'], tm['meter'], tm['meter_reason'], tm['tonemap_shader']) == ('0', 'shader', '0', 'tonemap', '80004005'), (name, tm)
        assert sorted(s2['frames']) == [0, 1, 2] and not s2['unwinds'] and not s2['disabled'], (name, sorted(s2['frames']))
        for frame, h in s2['frames'].items():
            assert (h['tonemap'], h['tonemapped'], h['unwind'], h['writeback_source'], h['stepped']) == ('identity', '0', '0', 'shader', '0'), (name, frame, h)
        images = {frame: compare_image_to_reference(directory, frame, 0.0, params, identity=True) for frame in (1, 2)}
        assert all(v['max'] <= 1 and v['alpha_max'] <= 1 for v in images.values()), (name, images)
        result.update(frames=3, shader_absent=True, tonemap_line={k: tm[k] for k in ('tonemap', 'tonemap_reason', 'meter', 'meter_reason', 'tonemap_shader')}, images=images)
        return result
    assert (tm['tonemap'], tm['tonemap_reason'], tm['meter'], tm['meter_reason']) == ('1', 'ok', '1', 'ok'), (name, tm)
    assert sorted(s2['frames']) == sorted(TONEMAP_FAULT_SCRIPT), (name, sorted(s2['frames']))
    faults = {int(fields(l)['frame']): int(fields(l)['fault']) for l in lines if l.startswith('HDR_TONEMAP_FAULT ')}
    for frame, expect in TONEMAP_FAULT_SCRIPT.items():
        h = s2['frames'][frame]
        assert faults[frame] == expect['fault'], (name, frame, faults)
        assert (h['redirected'], h['writeback_source'], h['blocked']) == ('1', 'shader', '0'), (name, frame, h)
        assert (int(h['tonemapped']), int(h['unwind']), h['unwind_reason'], h['recheck'], h['meter'], int(h['stepped'])) == \
            (expect['tonemapped'], expect['unwind'], expect['reason'], expect['recheck'], expect['meter'], expect['stepped']), (name, frame, h, expect)
        assert h['fallback'] == str(int(expect['reason'] == 'tonemap')), (name, frame, h)
        assert h['tonemap'] == ('identity' if frame >= 6 else 'agx'), (name, frame, h)  # disabled inside frame 6's write-back
        if expect['reason'] == 'tonemap':
            assert h['tonemap_draw'] == '80004005' and h['unwind_draw'] == '00000000', (name, frame, h)
    assert [u['reason'] for u in s2['unwinds']] == ['tonemap'] * 3 and all(u['source'] == 'shader' for u in s2['unwinds']), (name, s2['unwinds'])
    assert sorted(s2['rechecks']) == [2, 6, 7] and all(r['passed'] == '1' for r in s2['rechecks'].values()), (name, s2['rechecks'])
    assert len(s2['disabled']) == 1 and int(s2['disabled'][0]['frame']) == 6 and s2['disabled'][0]['reason'] == 'draw_failures', (name, s2['disabled'])
    # Fault 15 overrides the unlock HRESULT only after the real UnlockRect.
    # The available candidate must not publish any exposure state; the next
    # successful readback must advance again (reject-all is not a passing fix).
    assert s2['frames'][2]['readback'] == '80004005' and s2['frames'][3]['readback'] == '00000000', (name, s2['frames'][2], s2['frames'][3])
    for key in states[1]:
        if key != 'frame':
            assert states[2][key] == states[1][key], (name, 'failed unlock published state', key, states[1], states[2])
    assert int(s2['frames'][2]['steps']) == int(s2['frames'][1]['steps']) and int(s2['frames'][3]['steps']) == int(s2['frames'][2]['steps']) + 1, (name, s2['frames'])
    # The exposure held across the failed meter: frame 4 consumed no step, its EV equals frame 3's.
    assert abs(float(states[4]['ev']) - float(states[3]['ev'])) < 1e-9 and float(states[3]['ev']) != float(states[2]['ev']), (name, states[2], states[3], states[4])
    images = {1: compare_image_to_reference(directory, 1, float(states[1]['ev']), params, identity=True),
              2: compare_image_to_reference(directory, 2, float(states[2]['ev']), params, identity=False),
              4: compare_image_to_reference(directory, 4, float(states[4]['ev']), params, identity=False),
              7: compare_image_to_reference(directory, 7, float(states[7]['ev']), params, identity=True)}
    assert all(v['max'] <= 1 and v['alpha_max'] <= 1 for v in images.values()), (name, images)
    result.update(frames=len(TONEMAP_FAULT_SCRIPT), unwinds=[{k: u[k] for k in ('reason', 'frame', 'source', 'draw', 'restore')} for u in s2['unwinds']],
                  rechecks=sorted(s2['rechecks']), disabled_frame=6, images=images, readback_unlock_failure_held=True,
                  hdr_frames={f: {k: h[k] for k in ('tonemap', 'tonemapped', 'unwind', 'unwind_reason', 'fallback', 'recheck', 'meter', 'stepped', 'ev')} for f, h in s2['frames'].items()})
    return result


def validate_envmap(name, text, trace, directory, hdr=False):
    """Environment-map exclusion: frames 1 and 3 carry the six-face sequence (before
    the depth Clear, before the initial Clear); frames 0, 2 and 4 are routed."""
    lines = text.splitlines()
    assert lines and lines[-1].startswith('RESULT PASS ') and 'FAIL' not in text, f'{name}: fixture did not pass'
    terminal = fields(lines[-1])
    mode_line = fields([l for l in lines if l.startswith('MODE ')][0])
    assert (mode_line['seam'], mode_line['taa'], mode_line['camera'], mode_line['envmap'], mode_line['sentinel'], mode_line['hdr']) == ('1', '1', '1', '1', '0', str(int(hdr))), (name, mode_line)
    assert (int(terminal['checks']), int(terminal['restorations']), int(terminal['frames'])) == (67, 18, ENVMAP_FRAMES), (name, terminal)
    assert (terminal['taa_frames'], terminal['taa_history_frames'], terminal['taa_reference_frames'], terminal['taa_skipped_frames'], terminal['taa_changed_pixels']) == ('5', '0', '3', '2', '0'), (name, terminal)
    taa_lines = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('TAA ')}
    assert sorted(taa_lines) == list(range(ENVMAP_FRAMES)) and all(t['history'] == '0' and t['changed'] == '0' for t in taa_lines.values()), (name, taa_lines)
    assert {f for f, t in taa_lines.items() if t['skipped'] == '1'} == {1, 3}, (name, taa_lines)
    motion = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('MOTION ')}
    assert sorted(motion) == [0, 2, 4] and all(m['mismatches'] == '0' and m['depth_mismatches'] == '0' and m['matched'] == '0' for m in motion.values()), (name, motion)
    expects = [fields(l) for l in lines if l.startswith('EXPECT ')]
    assert all(e['routed'] == '0' for e in expects if int(e['frame']) in (1, 3)) and sum('face' in e for e in expects) == 12, (name, 'env frames must expect no routing')
    tl = trace.splitlines()
    frames = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_frame ')}
    states = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('camera_state ') and 'frame=' in l}
    routes = [fields(l) for l in tl if l.startswith('motion_route ')]
    readbacks = {int(fields(l)['frame']) for l in tl if l.startswith('motion_output_readback ')}
    assert sorted(frames) == sorted(states) == list(range(ENVMAP_FRAMES)), (name, sorted(frames), sorted(states))
    # The two rejected frames never reach the resolve: only the frame-end
    # site fires (no skip: nothing was attempted); the routed frames drop nothing.
    sites = invalidate_sites(trace)
    assert {f: v for f, v in sites.items() if f < ENVMAP_FRAMES} == {1: {'not_resolved'}, 3: {'not_resolved'}}, (name, sites)
    for frame in (1, 3):
        f, s = frames[frame], states[frame]
        # Nine draws (background, six faces, two scene draws) all stopped at gate 2:
        # the selector rejected the frame at the first face's SetRenderTarget.
        assert (f['routed'], f['gate2'], f['draws'], f['selector_state'], f['taa_attempted'], f['taa_resolved'], f['taa_skip']) == ('0', '9', '9', '9', '0', '0', '2'), (name, frame, f)
        # The scene camera was never read (the depth-only Clear never advanced the
        # selector); the frame before the initial Clear did not even latch.
        assert (f['camera_valid'], f['camera_policy'], f['camera_reads'], f['latched'], f['filled']) == ('0', '1', '1' if frame == 1 else '0', '1' if frame == 1 else '0', '1' if frame == 1 else '0'), (name, frame, f)
        assert (s['valid'], s['history_view_valid'], s['reads']) == ('0', '0', f['camera_reads']), (name, frame, s)
        assert not any(r['frame'] == str(frame) for r in routes), (name, frame, 'a draw of an environment-map frame reached the scene gate')
    for frame in (0, 2, 4):
        f, s = frames[frame], states[frame]
        assert (f['routed'], f['gate2'], f['selector_state'], f['taa_attempted'], f['taa_resolved'], f['taa_history'], f['taa_skip']) == ('2', '1', '9', '1', '1', '0', '0'), (name, frame, f)
        # The history's view was dropped with the history: every routed frame starts without a previous view.
        assert (f['camera_valid'], f['camera_policy'], f['camera_reason'], f['camera_reads']) == ('1', '1', '3', '2'), (name, frame, f)
        assert (s['valid'], s['history_view_valid'], s['history_view_frame']) == ('1', '1', str(frame)), (name, frame, s)
        if frame:
            assert f['cut'] == '1' and f['cut_missing'] == '1.0000', (name, frame, f)  # the rejected frame recorded no rows
    # Capture frames 1-4: the fill of frame 1 was read back (all sentinel, no routed draw); frame 3 never filled.
    assert readbacks == {1, 2, 4}, (name, readbacks)
    pixels = read_motion(directory / 'x3-modern-captures' / 'motion_1_1.rgba32f')
    assert all(p == SENTINEL for p in pixels), f'{name}: the environment-map frame wrote motion'
    assert not any(l.startswith(('motion_output_taa_failed', 'motion_output_fill_failed', 'motion_output_apply_failed', 'motion_output_restore_failed')) for l in tl), name
    # HDR: frames 0, 2, 4 end at the bloom copy; frame 1 (faces mid-scene, selector rejected: no bloom copy is recognized)
    # suspends and resumes around the faces and ends at Present; frame 3 (faces before the initial Clear) never latches
    # (the selector rejected the frame before any Clear), so nothing is redirected there. Frame 1's readback happens at the switch flush.
    hdr_summary = validate_hdr(name, trace, directory, hdr, None, frames=range(ENVMAP_FRAMES), capture_frames=ENVMAP_CAPTURE, end='bloom_copy', taa=True,
                               ends={1: 'present'}, redirected={0: 1, 1: 1, 2: 1, 3: 0, 4: 1})
    if hdr:
        assert (hdr_summary['frames'][1]['suspended'], hdr_summary['frames'][1]['resumed']) == ('1', '1'), (name, hdr_summary['frames'][1])
        assert all((hdr_summary['frames'][f]['suspended'], hdr_summary['frames'][f]['resumed']) == ('0', '0') for f in (0, 2, 3, 4)), (name, hdr_summary['frames'])
    return {'mode': 'envmap', 'frames': ENVMAP_FRAMES, 'checks': int(terminal['checks']), 'restorations': int(terminal['restorations']),
            'rejected_frames': [1, 3], 'routed_per_frame': {f: int(frames[f]['routed']) for f in sorted(frames)},
            'camera_valid_per_frame': {f: int(frames[f]['camera_valid']) for f in sorted(frames)},
            'color_hashes': {int(fields(l)['frame']): fields(l)['hash'] for l in lines if l.startswith('COLOR ')}, 'hdr': hdr_summary}


def finish_case(name, mode, variant, enabled, jitter, taa, text, trace, directory, lazy=False, camera=False, sentinel=None, shadow=True, hdr=False, hdr_fault=None, mip_bias=None, sharpen=0.0, copy_draw=False, quad_fvf=False):
    result = validate_case(name, mode, variant, enabled, jitter, taa, text, trace, directory, lazy, camera, sentinel, shadow, hdr, hdr_fault, mip_bias, sharpen, copy_draw, quad_fvf)
    result['variant'] = variant
    result['ownership'] = validate_ownership(name, variant, enabled, trace)
    if mip_bias is not None:
        result['mip_bias'] = validate_mip_bias_lines(name, trace, mip_bias, frames=None, expect_sets=False)
    return result


def validate_mip_bias_lines(name, trace, mip_bias, frames, expect_sets):
    """The DLL's mip-bias fields on the configuration and per-frame lines.
    Regular scripts (`expect_sets` False) bind no mip-mapped texture: the bias
    is configured (hooks installed) but never set."""
    tl = trace.splitlines()
    bias = float(mip_bias or 0)
    modes = [fields(l) for l in tl if l.startswith('motion_output_mode ')]
    devices = [fields(l) for l in tl if l.startswith('motion_output_device ')]
    assert len(modes) == 1 and float(modes[0]['mip_bias']) == bias, (name, modes)
    assert len(devices) == 1 and float(devices[0]['mip_bias']) == bias, (name, devices)
    frame_lines = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_frame ')}
    assert frame_lines, name
    for frame, summary in frame_lines.items():
        assert float(summary['mip_bias']) == bias, (name, frame, summary['mip_bias'])
        assert summary['mip_bias_failures'] == '0' and summary['mip_bias_biased_now'] == '0000', (name, frame, summary)
        if not expect_sets:
            assert all(summary[k] == '0' for k in ('mip_bias_sets', 'mip_bias_restores', 'mip_bias_draws', 'mip_bias_reads', 'mip_bias_game_writes')), (name, frame, summary)
            assert summary['mip_bias_stages'] == '0000', (name, frame, summary)
    summaries = [fields(l) for l in tl if l.startswith('motion_output_mip_bias_summary ')]
    assert len(summaries) == (1 if bias else 0), (name, summaries)
    if bias and not expect_sets:
        assert summaries[0]['sets'] == summaries[0]['restores'] == summaries[0]['game_writes'] == summaries[0]['failures'] == '0', (name, summaries)
    assert not any(l.startswith('motion_output_mip_bias_game_write ') for l in tl) or expect_sets, name
    return {'bias': bias, 'frames': len(frame_lines), 'summary': summaries[0] if summaries else None}


def validate_mipbias(name, mode, lazy, mip_bias, text, trace, directory):
    """Mip-bias script (see the module docstring): the fixture's per-step
    GetSamplerState verdicts, the evidence pairs, the DLL's per-frame counts
    against the fixture's model and the hand-derived anchors."""
    lines = text.splitlines()
    assert lines and lines[-1].startswith('RESULT PASS '), f'{name}: fixture did not pass'
    assert 'FAIL' not in text and text.count('RESULT ') == 1, f'{name}: failures reported'
    terminal = fields(lines[-1])
    seam = mode == 'seam'
    rt_mode = 'lazy' if lazy else 'perdraw'
    bias = float(mip_bias or 0)
    live = bias != 0
    mode_line = fields([l for l in lines if l.startswith('MODE ')][0])
    assert mode_line == {'seam': str(int(seam)), 'enabled': '1', 'jitter': '1', 'jitter_samples': str(JITTER_SAMPLES), 'taa': '0', 'bench': '0',
                         'width': '64', 'height': '64', 'dll': mode_line['dll'], 'burst': '0', 'rt_mode': rt_mode, 'camera': '0', 'sentinel': '0', 'envmap': '0',
                         'hook': '0', 'state_shadow': '1', 'hdr': '0', 'hdrvalues': '0', 'hdrfault': '0', 'hdrramp': '0', 'hdrexposure': '0', 'hdrtonemapfault': '0',
                         'mipbias': '1', 'mip_bias': mip_bias_text(mip_bias), 'sharpen': '0', 'msaa': '0'}, (name, mode_line)
    assert int(terminal['frames']) == MIPBIAS_FRAMES and text.count('RESET PASS') == 1, (name, terminal)
    restores = [fields(l) for l in lines if l.startswith('RESTORE ')]
    assert len(restores) == MIPBIAS_FRAMES and all(r['differences'] == '0' and r['label'] == 'fill' for r in restores), (name, restores)
    # Every step's verdict: the bias sits on exactly the expected stages (none
    # without the bias), every frame ends and starts unbiased.
    verdicts = [fields(l) for l in lines if l.startswith('MIPBIAS ')]
    assert verdicts and all(v['ok'] == '1' and v['actual'] == v['expected'] for v in verdicts), (name, [v for v in verdicts if v['ok'] != '1'][:4])
    # The after_present verdict of frame f prints after Present advanced the
    # fixture's frame counter, so it carries f + 1.
    labels = [v['label'] for v in verdicts if int(v['frame']) == 1]
    assert labels == ['after_present', 'before_routed', 'routed', 'routed_again', 'unrouted', 'rerouted', 'stage4_unmipped', 'stage0_mipfilter_none', 'eligible_again',
                      'after_game_write', 'reapplied_after_game_write', 'restored_to_game_value', 'routed_after_second_write', 'last_routed'], (name, labels)
    assert sorted(int(v['frame']) for v in verdicts if v['label'] == 'after_present') == list(range(1, MIPBIAS_FRAMES + 1)), name
    if live:
        assert {v['expected'] for v in verdicts if v['label'] == 'routed'} == {'11'}, name
        assert all(v['nonzero'] in ('00', '01') for v in verdicts if v['label'] in ('after_present', 'unrouted', 'before_routed')), name  # 01: the application's own 0.25 on stage 0
    else:
        assert all(v['expected'] == '00' and v['nonzero'] in ('00', '01') for v in verdicts), name
    # Evidence (even frames): identical images without the bias; darker
    # routed pixels (finer ramp levels) with it.
    evidence = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('MIPBIAS_EVIDENCE ')}
    assert sorted(evidence) == [0, 2, 4, 6], (name, sorted(evidence))
    for frame, e in evidence.items():
        assert float(e['bias']) == bias and int(e['pixels']) > 500, (name, frame, e)
        if live:
            assert int(e['differing']) > int(e['pixels']) // 2 and float(e['delta']) > 1.0, (name, frame, e)
        else:
            assert e['differing'] == '0' and float(e['delta']) == 0.0, (name, frame, e)
    colors = {int(fields(l)['frame']): fields(l)['hash'] for l in lines if l.startswith('COLOR ')}
    coverage = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('COVERAGE ')}
    assert sorted(colors) == sorted(coverage) == list(range(MIPBIAS_FRAMES)), (name, sorted(colors), sorted(coverage))
    assert all(c['mismatches'] == '0' and int(c['checked']) > 3000 for c in coverage.values()), (name, coverage)
    # The DLL: configuration, per-frame counts against the fixture's model of
    # the restore points and the hand-derived anchors, the route counters.
    tl = trace.splitlines()
    assert not any(l.startswith(('motion_output_fill_failed', 'motion_output_apply_failed', 'motion_output_restore_failed', 'motion_output_taa_failed')) for l in tl), name
    frames = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_frame ')}
    assert sorted(frames) == list(range(MIPBIAS_FRAMES)), (name, sorted(frames))
    model = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('MIPBIAS_EXPECT ')}
    assert sorted(model) == list(range(MIPBIAS_FRAMES)), (name, sorted(model))
    sets, restores_per_frame, reads = {}, {}, {}
    for frame, summary in frames.items():
        expect = MIPBIAS_EXPECT['even' if frame % 2 == 0 else 'odd']
        got = {k: int(summary[k]) for k in expect}
        assert got == expect, (name, frame, got, expect)
        assert float(summary['mip_bias']) == bias and summary['rt_mode'] == rt_mode and summary['jitter'] == '1', (name, frame, summary)
        assert summary['mip_bias_failures'] == '0' and summary['mip_bias_biased_now'] == '0000', (name, frame, summary)
        m = model[frame]
        assert m['capture'] == str(int(frame in MIPBIAS_CAPTURE)), (name, frame, m)
        got = tuple(int(summary[k]) for k in ('mip_bias_sets', 'mip_bias_restores', 'mip_bias_draws', 'mip_bias_reads'))
        want = tuple(int(m[k]) for k in ('sets', 'restores', 'draws', 'reads'))
        assert got == want, (name, frame, 'dll', got, 'fixture model', want)
        sets[frame], restores_per_frame[frame], reads[frame] = got[0], got[1], got[3]
        if live:
            anchor = MIPBIAS_ANCHORS['capture' if frame in MIPBIAS_CAPTURE else 'even' if frame % 2 == 0 else 'odd']
            assert (got[0], got[1]) == anchor, (name, frame, (got[0], got[1]), anchor)
            assert summary['mip_bias_stages'] == MIPBIAS_STAGES and int(summary['mip_bias_game_writes']) == MIPBIAS_GAME_WRITES, (name, frame, summary)
            assert int(summary['mip_bias_game_writes_total']) == MIPBIAS_GAME_WRITES * (frame + 1), (name, frame, summary)
            # The saved value is read once per stage (frame 0) and again after the Reset (frame 4).
            assert got[3] == (2 if frame in (0, 4) else 0), (name, frame, got)
        else:
            assert got == (0, 0, 0, 0) and summary['mip_bias_stages'] == '0000' and summary['mip_bias_game_writes'] == '0', (name, frame, summary)
    summaries = [fields(l) for l in tl if l.startswith('motion_output_mip_bias_summary ')]
    game_writes = [fields(l) for l in tl if l.startswith('motion_output_mip_bias_game_write ')]
    if live:
        assert len(summaries) == 1 and int(summaries[0]['sets']) == sum(sets.values()) and int(summaries[0]['restores']) == sum(restores_per_frame.values()), (name, summaries, sets, restores_per_frame)
        assert int(summaries[0]['game_writes']) == MIPBIAS_GAME_WRITES * MIPBIAS_FRAMES and summaries[0]['failures'] == '0' and summaries[0]['biased_now'] == '0000', (name, summaries)
        assert float(summaries[0]['bias']) == bias, (name, summaries)
        # One line per application write reaches the log until the cap (16).
        assert 1 <= len(game_writes) <= 16 and all(float(w['bias']) in (0.25, 0.0) for w in game_writes), (name, len(game_writes))
    else:
        assert not summaries and not game_writes, (name, summaries, game_writes)
    modes = [fields(l) for l in tl if l.startswith('motion_output_mode ')]
    assert len(modes) == 1 and float(modes[0]['mip_bias']) == bias and modes[0]['rt_mode'] == rt_mode, (name, modes)
    assert sum(l.startswith('motion_output_release ') for l in tl) == 1, name
    return {'mode': mode, 'mipbias': True, 'lazy': lazy, 'bias': bias, 'checks': int(terminal['checks']), 'frames': MIPBIAS_FRAMES,
            'color_hashes': colors, 'sets_per_frame': sets, 'restores_per_frame': restores_per_frame, 'reads_per_frame': reads,
            'evidence': {f: {'pixels': int(e['pixels']), 'differing': int(e['differing']), 'routed_mean': float(e['routed_mean']),
                             'unrouted_mean': float(e['unrouted_mean']), 'delta': float(e['delta'])} for f, e in sorted(evidence.items())},
            'verdicts': len(verdicts), 'summary': summaries[0] if summaries else None, 'game_write_lines': len(game_writes),
            'coverage_pixels': int(terminal['coverage_pixels'])}


def compare_mask_twins(result, save):
    """Lever 3 equivalence: every lazy mask run present must equal the per-draw
    twin of the same DLL; a lazy run without its twin is an error, never a
    silent skip."""
    selected_lazy = [n for n in MASK_TWINS if n in result['cases']]
    if not selected_lazy:
        if any(entry['name'] in result['cases'] for entry in MASK_CASES):
            print('mask burst: equivalence comparison SKIPPED (no lazy mask case selected)')
        return
    for lazy_name in selected_lazy:
        twin = MASK_TWINS[lazy_name]
        assert twin in result['cases'], f'{lazy_name}: select the per-draw twin {twin} in the same run'
        per, lz = result['cases'][twin], result['cases'][lazy_name]
        for key in ('color_hashes', 'state_hashes', 'motion_hashes', 'readback_sha256'):
            assert per[key] == lz[key], f'{lazy_name}: {key} differs from {twin}'
    result['mask_equivalence'] = {'twins': {n: MASK_TWINS[n] for n in selected_lazy}, 'identical': True,
                                  'set_rt_per_frame': {n: result['cases'][n]['set_rt_per_frame'] for n in selected_lazy + sorted({MASK_TWINS[n] for n in selected_lazy})}}
    save()
    print(f'mask burst: {len(selected_lazy)} lazy run(s) equal their per-draw twin')


def validate_burst(name, mode, lazy, text, trace, directory, shadow=True, wrap=False, mask=False):
    """Burst script (see the module docstring): per-frame counters of the DLL,
    the fixture's own restoration and oracle verdicts, and the signatures the
    cross-mode comparison in main() uses."""
    lines = text.splitlines()
    assert lines and lines[-1].startswith('RESULT PASS '), f'{name}: fixture did not pass'
    assert 'FAIL' not in text and text.count('RESULT ') == 1, f'{name}: failures reported'
    terminal = fields(lines[-1])
    seam = mode == 'seam'
    rt_mode = 'lazy' if lazy else 'perdraw'
    mode_line = fields([l for l in lines if l.startswith('MODE ')][0])
    assert mode_line == {'seam': str(int(seam)), 'enabled': '1', 'jitter': '0', 'jitter_samples': str(JITTER_SAMPLES), 'taa': '0', 'bench': '0',
                         'width': '64', 'height': '64', 'dll': mode_line['dll'], 'burst': '1', 'rt_mode': rt_mode, 'camera': '0', 'sentinel': '0', 'envmap': '0',
                         'hook': '0', 'state_shadow': '0' if shadow is False else '1', 'hdr': '0', 'hdrvalues': '0', 'hdrfault': '0', 'hdrramp': '0', 'hdrexposure': '0', 'hdrtonemapfault': '0',
                         'mipbias': '0', 'mip_bias': '0', 'sharpen': '0', 'msaa': '0'}, (name, mode_line)
    # Per frame: the fill and the burst restoration comparisons, the coverage
    # oracle (both DLLs), the COLORWRITEENABLE1 read-back between routed draws
    # and, seam, the motion/depth oracle.
    assert int(terminal['frames']) == BURST_FRAMES and int(terminal['restorations']) == 2 * BURST_FRAMES + mask, (name, terminal)  # mask: the frame restarted after the Reset fills twice
    wrap_modes = [l for l in lines if l.startswith('WRAP ')]
    wrap_checks = [l for l in lines if l.startswith('CHECK ') and 'WRAP' in l]
    assert wrap_modes == (['WRAP mode=hostile motion_texcoord=4 depth_texcoord=5 native_texcoord=0'] if wrap else []), (name, wrap_modes)
    assert wrap_checks == (['CHECK application reads exact WRAP4 after routed draw PASS'] * BURST_FRAMES if wrap else []), (name, wrap_checks)
    mask_checks = [l for l in lines if l.startswith('CHECK MASK ')]
    assert mask_checks == (['CHECK MASK the application reads back both write masks after a routed draw under a held binding PASS',
                            'CHECK MASK the application reads back COLORWRITEENABLE2 after a hold that started masked PASS',
                            'CHECK MASK the application changes its depth surface (same size, smaller, original) under a held binding PASS'] * BURST_FRAMES if mask else []), (name, mask_checks)
    assert lines.count('MASK reset inside the scene under a held binding') == int(mask) and lines.count('RESET PASS') == int(mask), name
    assert int(terminal['checks']) == (86 if seam else 41) + (BURST_FRAMES if wrap else 0) + (3 * BURST_FRAMES if mask else 0), (name, terminal)  # one presented-image check per frame
    restores = [fields(l) for l in lines if l.startswith('RESTORE ')]
    assert len(restores) == 2 * BURST_FRAMES + mask and all(r['differences'] == '0' for r in restores), f'{name}: restoration differences'
    labels = ['fill', 'burst'] * BURST_FRAMES
    if mask:
        labels.insert(2 * MASK_RESET_FRAME, 'fill')
    assert [r['label'] for r in restores] == labels, (name, [r['label'] for r in restores])
    colors = {int(fields(l)['frame']): fields(l)['hash'] for l in lines if l.startswith('COLOR ')}
    states = {int(fields(l)['frame']): fields(l)['hash'] for l in lines if l.startswith('STATE ')}
    motion_hashes = {int(fields(l)['frame']): (fields(l)['motion'], fields(l)['depth']) for l in lines if l.startswith('MOTION_HASH ')}
    coverage = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('COVERAGE ')}
    motion = [fields(l) for l in lines if l.startswith('MOTION ')]
    assert sorted(colors) == sorted(states) == sorted(coverage) == list(range(BURST_FRAMES)), (name, sorted(colors), sorted(states), sorted(coverage))
    assert all(c['mismatches'] == '0' and int(c['checked']) > 3000 for c in coverage.values()), (name, coverage)
    if seam:
        assert sorted(motion_hashes) == list(range(BURST_FRAMES)) and len(motion) == BURST_FRAMES, (name, sorted(motion_hashes), len(motion))
        assert all(m['mismatches'] == '0' and m['depth_mismatches'] == '0' and m['matched'] == '0' and int(m['depth_written']) > 1000 for m in motion), (name, motion)
        assert int(terminal['matched_pixels']) == 0 and int(terminal['depth_written']) > 10000, (name, terminal)
    else:
        assert not motion_hashes and not motion
    expects = [fields(l) for l in lines if l.startswith('EXPECT ')]
    assert len(expects) == (9 * BURST_FRAMES + 2 if mask else 8 * BURST_FRAMES) and all(e['matched'] == '0' and e['jittered'] == '0' for e in expects), (name, len(expects))
    tl = trace.splitlines()
    modes = [fields(l) for l in tl if l.startswith('motion_output_mode ')]
    assert len(modes) == 1 and modes[0]['rt_mode'] == rt_mode and modes[0]['frame_log'] == '1' and modes[0]['state_shadow'] == shadow_request(shadow), (name, modes)
    devices = [fields(l) for l in tl if l.startswith('motion_output_device ')]
    assert len(devices) == 1 and devices[0]['enabled'] == '1' and devices[0]['rt_mode'] == rt_mode and devices[0]['depth'] == '1', (name, devices)
    assert devices[0]['state_shadow'] == shadow_effective(shadow, lazy) and devices[0]['scene_hook'] == '0', (name, devices)
    assert not any(l.startswith(('motion_output_fill_failed', 'motion_output_apply_failed', 'motion_output_restore_failed', 'motion_output_taa_failed')) for l in tl), name
    # Lever 3: lazy RT mode is no reason to install the setter hooks.
    hooks = [fields(l) for l in tl if l.startswith('state_hooks ')]
    assert len(hooks) == 1 and (hooks[0]['installed'], hooks[0]['reason']) == (('1', 'explicit') if shadow is True else ('0', 'none')), (name, hooks)
    frames = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_frame ')}
    assert sorted(frames) == list(range(BURST_FRAMES)), (name, sorted(frames))  # X3M_MOTION_FRAME_LOG=1
    # The mask script routes one more draw per frame. The DLL's frame counters
    # restart at Reset, so the frame restarted after the mid-scene Reset counts
    # like every other; the fixture's RESET PASS and that frame's oracles are
    # the evidence that the Reset hook released the held bindings.
    expect = dict(BURST_EXPECT, draws=10, routed=6, gate5=6, depth_routed=6) if mask else BURST_EXPECT
    set_rt, flushes, costs, render_state = {}, {}, {}, {}
    for frame, summary in frames.items():
        render_state[frame] = check_render_state(name, frame, summary, shadow, 0)
        assert (summary['scene_hook'], summary['hook_signals'], summary['scene_end_source'], summary['scene_end_check']) == ('0', '0', 'none', '0'), (name, frame, summary)
        got = {k: int(summary[k]) for k in BURST_EXPECT}
        assert got == expect, (name, frame, got)
        # The application SetRenderTarget (even frames) or depth Clear (odd)
        # inside the scene rejects the selector; the draw after it stops at gate 2.
        assert summary['selector_state'] == '9' and summary['latched'] == summary['filled'] == '1', (name, frame, summary)
        assert summary['apply_failures'] == summary['restore_failures'] == '0' and summary['present'] == '00000000', (name, frame, summary)
        assert summary['rt_mode'] == rt_mode and summary['timing'] == 'cpu_qpc' and summary['jitter_writes'] == '0', (name, frame, summary)
        captured = frame in BURST_CAPTURE
        expected_set_rt = BURST_SET_RT['perdraw'] if captured else BURST_SET_RT[rt_mode]
        expected_flushes = (5 if captured else BURST_FLUSHES['lazy']) if lazy else 0
        if mask:
            # One more routed draw, inside the second run: no new bind/flush
            # pair in lazy mode.
            expected_set_rt = 24 if captured or not lazy else BURST_SET_RT['lazy']
            expected_flushes = (6 if captured else BURST_FLUSHES['lazy']) if lazy else 0
        # Lazy routed draws that met an application write mask other than 15
        # (one count per draw): the draw after the blend draw (a hold that
        # starts masked) and, mask script, the draw under the held binding.
        assert int(summary['lazy_mask_writes']) == ((2 if mask else 1) if lazy else 0), (name, frame, summary)
        assert int(summary['set_rt']) == expected_set_rt and int(summary['lazy_flushes']) == expected_flushes, (name, frame, summary)
        assert int(summary['readbacks']) == (2 if captured else 0) and (float(summary['readback_us']) > 0) == captured, (name, frame, summary)
        set_rt[frame] = int(summary['set_rt']); flushes[frame] = int(summary['lazy_flushes'])
        costs[frame] = {k: float(summary[k]) for k in ('gate_us', 'route_draw_us', 'set_rt_us', 'lazy_flush_us', 'fill_us', 'readback_us')}
        assert all(v >= 0 for v in costs[frame].values()) and costs[frame]['set_rt_us'] > 0 and costs[frame]['fill_us'] > 0, (name, frame, costs[frame])
        assert (costs[frame]['lazy_flush_us'] > 0) == lazy, (name, frame, costs[frame])
    routes = [fields(l) for l in tl if l.startswith('motion_route ')]
    # Capture frames log the seven scene draws that reach gate 2 (the draw
    # after the rejection is not a scene draw); five route sentinel-only.
    assert len(routes) == (7 + mask) * len(BURST_CAPTURE) and sum(r['routed'] == '1' for r in routes) == (5 + mask) * len(BURST_CAPTURE), (name, len(routes))
    assert all(r['matched'] == '0' and r['result'] == '00000000' and r['depth'] == r['routed'] for r in routes), name
    readbacks = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_readback ')}
    depth_readbacks = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_depth_readback ')}
    assert sorted(readbacks) == sorted(depth_readbacks) == list(BURST_CAPTURE), (name, sorted(readbacks), sorted(depth_readbacks))
    files = {}
    for frame in BURST_CAPTURE:
        assert readbacks[frame]['result'] == depth_readbacks[frame]['result'] == '00000000', (name, frame)
        files[frame] = {kind: sha(directory / 'x3-modern-captures' / r[frame]['file']) for kind, r in (('motion', readbacks), ('depth', depth_readbacks))}
    assert sum(l.startswith('motion_output_release ') for l in tl) == 1, name
    return {'mode': mode, 'burst': True, 'lazy': lazy, 'wrap': wrap, 'mask': mask, 'mask_checks': len(mask_checks), 'wrap_readback_checks': len(wrap_checks), 'checks': int(terminal['checks']), 'restorations': int(terminal['restorations']),
            'frames': BURST_FRAMES, 'color_hashes': colors, 'state_hashes': states, 'motion_hashes': motion_hashes,
            'readback_sha256': files, 'set_rt_per_frame': set_rt, 'lazy_flushes_per_frame': flushes, 'costs_us_per_frame': costs,
            'coverage_pixels': int(terminal['coverage_pixels']), 'depth_written_pixels': int(terminal['depth_written']), 'render_state': render_state,
            'route_decisions': [(r['frame'], r['index'], r['gate'], r['routed'], r['matched']) for r in routes]}


def validate_cutout(name, script, text, trace):
    """Cutout steady-state script: every frame's exact-pair draw is refused at the gate and
    forwarded native (no route, no motion write; the fixture's pixel oracle and restoration
    checks are its own). `blended` keeps TAA history on every non-cut frame with no
    `cutout_missed` invalidation; `opaque` misses coverage and invalidates every frame."""
    blended = script == 'blended'
    lines = text.splitlines()
    terminal = [fields(l) for l in lines if l.startswith('RESULT PASS ')]
    assert len(terminal) == 1 and not any(l.startswith('RESULT FAIL') for l in lines), f'{name}: fixture did not complete'
    assert int(terminal[0]['frames']) == CUTOUT_FRAMES and int(terminal[0]['checks']) > 0, (name, terminal)
    summary = [fields(l) for l in lines if l.startswith('CUTOUT_CHECKS ')]
    assert summary == [dict(frames=str(CUTOUT_FRAMES), benchmark='0', pairs='2')], (name, summary)
    live = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('CUTOUT_LIVE ')}
    assert sorted(live) == list(range(CUTOUT_FRAMES)), (name, sorted(live))
    # Frame 0's begin_frame latch precedes the first capability verdict (probed at the
    # frame's HDR latch): the arm is inactive there and no script misses in frame 0.
    missed = lambda frame: int(not blended and frame > 0)
    for frame, row in live.items():
        expected = dict(pair='0', step='1', wrong='-1' if blended else '0', blended=str(int(blended)), material='1', depth='1', routed='0', matched='0',
                        owned='0', cap='0', cap_status='1', routed_delta='0', missed_delta=str(missed(frame)), unavailable=str(missed(frame)))
        actual = {k: row[k] for k in expected}
        assert actual == expected, (name, frame, actual, expected)
        assert int(row['accepted']) > 0 and int(row['holes']) > 0, (name, frame, 'the refused pair must cover pixels and leave holes')
    taa = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('TAA ')}
    assert sorted(taa) == list(range(CUTOUT_FRAMES)), (name, sorted(taa))
    assert all(r['skipped'] == '0' for r in taa.values()), (name, 'every frame resolves')
    history = {f: int(r['history']) for f, r in taa.items()}
    if blended:
        for f, r in taa.items():
            assert history[f] == int(f > 0 and r['cut'] == '0'), (name, f, 'blended refusal must retain history on every non-cut frame', r)
        assert sum(history.values()) >= CUTOUT_FRAMES // 2, (name, 'history must accumulate', history)
    else:
        assert not any(history.values()), (name, 'opaque miss must run current-only every frame', history)
    sites = invalidate_sites(trace)
    missed_frames = sorted(f for f, v in sites.items() if 'cutout_missed' in v)
    assert missed_frames == ([] if blended else list(range(1, CUTOUT_FRAMES))), (name, 'cutout_missed invalidation frames', missed_frames)
    materials = {int(fields(l)['frame']): fields(l) for l in trace.splitlines() if l.startswith('linear_material_frame ')}
    assert sorted(materials) == list(range(CUTOUT_FRAMES)), (name, sorted(materials))
    for f, row in materials.items():
        assert (row['cutout_routed'], row['cutout_missed'], row['cutout_unavailable']) == ('0', str(missed(f)), str(missed(f))), (name, f, row)
    frames = {int(fields(l)['frame']): fields(l) for l in trace.splitlines() if l.startswith('motion_output_frame ')}
    assert sorted(frames) == list(range(CUTOUT_FRAMES)), (name, sorted(frames))
    for f, row in frames.items():
        assert (row['draws'], row['routed'], row['taa_resolved'], row['apply_failures'], row['restore_failures']) == ('3', '1', '1', '0', '0'), (name, f, row)
    return dict(script=script, frames=CUTOUT_FRAMES, checks=int(terminal[0]['checks']), history_frames=sum(history.values()),
                cutout_missed_frames=len(missed_frames), invalidate_sites={f: sorted(v) for f, v in sorted(sites.items())},
                accepted_pixels=[int(live[f]['accepted']) for f in range(CUTOUT_FRAMES)])


def read_f32(path, width, height, components):
    data = path.read_bytes()
    assert len(data) == width * height * components * 4, (path, len(data))
    return struct.unpack('<%df' % (width * height * components), data)


def fade_route_shift(previous, current, window, width):
    """Displacement (dx, dy) in pixels of the content between two RGB images
    (pixel -> (r, g, b) callables) over the window: one Lucas-Kanade step of
    current(x) ~ previous(x - d), least squares over the pixels and channels
    with the mean gradient of both images. Exact for a linear ramp."""
    x0, x1, y0, y1 = window
    a = b = c = 0.0; e = f = 0.0  # normal equations [[a, b], [b, c]] d = [e, f]
    for y in range(y0, y1):
        for x in range(x0, x1):
            p, q = previous(x, y), current(x, y)
            for k in range(3):
                gx = (previous(x + 1, y)[k] - previous(x - 1, y)[k] + current(x + 1, y)[k] - current(x - 1, y)[k]) / 4
                gy = (previous(x, y + 1)[k] - previous(x, y - 1)[k] + current(x, y + 1)[k] - current(x, y - 1)[k]) / 4
                dv = q[k] - p[k]
                a += gx * gx; b += gx * gy; c += gy * gy; e -= dv * gx; f -= dv * gy
    det = a * c - b * b
    assert det > 1e-12, 'fade route: the ramp window carries no gradient'
    return ((c * e - b * f) / det, (a * f - b * e) / det)


def fade_route_images(directory, frame, width=64, height=64):
    """The raw FP16 scene after the quads (fade_route_color_<f>.f32, RGBA floats)
    and the resolved FP16 image (reference_taa_<f>.rgba16f: the fixture's
    reference resolve of the DLL's own RT1/M/colour inputs, which the fixture
    requires the DLL's presented frame to equal within one 8-bit code), as
    (r, g, b) samplers. The 8-bit presented frame quantises the masked
    composite's faint ramp below one code per pixel."""
    raw = read_f32(directory / f'fade_route_color_{frame}.f32', width, height, 4)
    data = (directory / f'reference_taa_{frame}.rgba16f').read_bytes()
    assert len(data) == width * height * 8, (directory, frame, len(data))
    shown = struct.unpack('<%de' % (width * height * 4), data)
    return (lambda x, y: raw[4 * (y * width + x):4 * (y * width + x) + 3],
            lambda x, y: shown[4 * (y * width + x):4 * (y * width + x) + 3])


def fade_route_fp16_ulp(value):
    return math.ldexp(1, (math.frexp(abs(value))[1] - 1) - 10) if value else math.ldexp(1, -24)


def validate_fade_route(name, script, lazy, text, trace, directory):
    """Fade-band arm script (motion_output_fade_route_inc.h): the arm decision per
    frame, the route records of the capture frames, the composite at the oracle
    pixels of quad P, and the raw versus resolved shift of the ramp quad Q."""
    import run_linear_distance_fade_live as live_fade  # the fade composite oracle (imports the material reference)
    routed_at = {f: fade_route_routed(script, f) for f in range(FADE_ROUTE_FRAMES)}
    held_at = {f: fade_route_held(script, f) for f in range(FADE_ROUTE_FRAMES)}
    matched_at = {f: routed_at[f] and f > 0 and routed_at[f - 1] for f in range(FADE_ROUTE_FRAMES)}  # the row history keeps one frame
    original = script in FADE_ROUTE_ORIGINAL_SCRIPTS  # original shading: no composition (mask_valid 0), no linear oracle, no linear_material_frame line
    overlay = script in ('overlay', 'foreign')  # the overlay arm's counters (overlay_routed/overlay_refused), never the fade arm's
    lines = text.splitlines()
    terminal = [fields(l) for l in lines if l.startswith('RESULT PASS ')]
    assert len(terminal) == 1 and not any(l.startswith('RESULT FAIL') for l in lines), f'{name}: fixture did not complete'
    assert int(terminal[0]['frames']) == FADE_ROUTE_FRAMES and int(terminal[0]['checks']) > 0, (name, terminal)
    checks = [fields(l) for l in lines if l.startswith('FADE_ROUTE_CHECKS ')]
    assert checks == [dict(frames=str(FADE_ROUTE_FRAMES), script=script, quads='2')], (name, checks)
    live = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('FADE_ROUTE ')}
    assert sorted(live) == list(range(FADE_ROUTE_FRAMES)), (name, sorted(live))
    for frame, row in live.items():
        routed = routed_at[frame]
        expected = dict(script=script, routed=str(int(routed)), matched=str(int(matched_at[frame])), fade_routed=str(2 * int(routed and not overlay)),
                        fade_refused=str(2 * int(not routed and not overlay)), fade_held=str(2 * int(held_at[frame])), prepared=str(0 if original else 2 * int(not routed)),
                        own_motion=str(2 * FADE_ROUTE_QUAD_PIXELS * int(routed)),
                        mask_set=str(0 if original else 2 * FADE_ROUTE_QUAD_PIXELS * int(not routed)), mask_valid=str(int(not original)), threshold=str(FADE_ROUTE_THRESHOLD), draws='2',
                        overlay_routed=str(2 * int(routed and overlay)), overlay_refused=str(2 * int(not routed and overlay)))
        actual = {k: row[k] for k in expected}
        assert actual == expected, (name, frame, actual, expected)
        assert int(row['covered']) >= 2 * FADE_ROUTE_QUAD_PIXELS, (name, frame, 'both quads must write pixels', row['covered'])
    taa = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('TAA ')}
    assert sorted(taa) == list(range(FADE_ROUTE_FRAMES)), (name, sorted(taa))
    assert all(r['skipped'] == '0' for r in taa.values()), (name, 'every frame resolves')
    history = {f: int(r['history']) for f, r in taa.items()}
    assert history == {f: int(f not in (0, FADE_ROUTE_CUT_FRAME)) for f in range(FADE_ROUTE_FRAMES)}, (name, 'history on every frame but the first and the camera cut', history)
    assert taa[FADE_ROUTE_CUT_FRAME]['cut'] == '1', (name, 'the camera cut frame')
    # Composite oracle at the P samples, at the fade and material routes'
    # qualified RGB tolerance (run_linear_distance_fade_live.validate_samples,
    # run_linear_material RGB_REL_TOL/RGB_ABS_TOL): a refused frame composes
    # as the live fade oracle (pair 0, alpha .078125 for masked, the frame's
    # g_AlphaValue for hover; the bracket itself is untouched, so its bytes
    # are the pre-change ones); a routed frame at alpha 1 writes encode(L)
    # itself (the oracle at alpha 1), its FP16-code error recorded as
    # max_code_error; a held hover frame blends encode(L) natively in the
    # encoded FP16 target (alpha .125 * g_AlphaValue: distance 4, diffuse .5).
    # Every draw keeps the destination alpha. hover: the switch step is the
    # difference between a held and a refused frame at 449 permille over the
    # same `before` (A is the same every frame).
    samples = [fields(l) for l in lines if l.startswith('FADE_ROUTE_SAMPLE ')]
    assert [(int(r['frame']), int(r['x']), int(r['y'])) for r in samples] == [(f, x, y) for f in range(FADE_ROUTE_FRAMES) for x, y in FADE_ROUTE_SAMPLES], (name, 'sample lines')
    component, material = live_fade.component, live_fade.material
    case = live_fade.sample_case(0)
    case['diffuse'][3] = 1.
    routed_linear = material.expected(component.oracle_case(case)).linear_rgb
    encoded = component.compose((0., 0., 0., 1.), [component.fp16_rt_store(x) for x in routed_linear], 1.)[:3]  # the source as stored at alpha 1
    max_code_error = max_tolerance_fraction = 0.0
    composites = {}
    for row in samples:
        frame, routed = int(row['frame']), routed_at[int(row['frame'])]
        before, after = live_fade.rgba(row['before']), live_fade.rgba(row['after'])
        assert after[3] == before[3], (name, row, 'RGB-masked draw keeps alpha')
        composites[(frame, int(row['x']), int(row['y']))] = (before, after)
        if original:
            # Original shading binds the pair's ordinary motion variant (the
            # original pixel program's own colour, no linear oracle): the routed
            # draw must have written the sample, alpha kept as above.
            assert after[:3] != before[:3], (name, row, 'original-shading fade draw (routed or the plain native draw) writes the sample')
            continue
        if script == 'hover':
            alpha = .125 * FADE_ROUTE_HOVER_ALPHA[frame]
            expected = tuple(component.fp16_rt_store(e * alpha + b * (1 - alpha)) for e, b in zip(encoded, before)) if routed else \
                live_fade.expected_composite(before, 0, alpha_value=FADE_ROUTE_HOVER_ALPHA[frame])
        else:
            expected = component.compose(before, [component.fp16_rt_store(x) for x in routed_linear], 1.) if routed else live_fade.expected_composite(before, 0)
        for k in range(3):
            fraction = abs(after[k] - expected[k]) / (.006 * abs(expected[k]) + .00002)
            max_tolerance_fraction = max(max_tolerance_fraction, fraction)
            assert fraction <= 1, (name, row, k, after[k], expected[k], 'routed fade draw equals the fade oracle' if routed else 'bracketed fade draw equals the fade oracle')
            if routed and script != 'hover':
                max_code_error = max(max_code_error, abs(after[k] - expected[k]) / fade_route_fp16_ulp(expected[k]))
    switch_step = None
    if script == 'hover':
        held = [f for f in range(FADE_ROUTE_FRAMES) if held_at[f]]
        refused = [f for f in range(FADE_ROUTE_FRAMES) if not routed_at[f] and FADE_ROUTE_HOVER_PERMILLE[f] == FADE_ROUTE_HOVER_PERMILLE[held[0]]]
        assert held and refused, (name, held, refused)
        switch_step = dict(permille=FADE_ROUTE_HOVER_PERMILLE[held[0]], max_abs=0.0, max_relative=0.0)
        for x, y in FADE_ROUTE_SAMPLES:
            befores = {composites[(f, x, y)][0] for f in range(FADE_ROUTE_FRAMES)}
            assert len(befores) == 1, (name, x, y, 'the same destination under P every frame', befores)
            for h in held:
                for r in refused:
                    for k in range(3):
                        step = abs(composites[(h, x, y)][1][k] - composites[(r, x, y)][1][k])
                        switch_step['max_abs'] = max(switch_step['max_abs'], step)
                        switch_step['max_relative'] = max(switch_step['max_relative'], step / abs(composites[(r, x, y)][1][k]))
    # Route records of the capture frames: A routed, the two fade
    # draws routed by the arm (gate 0, fade_arm 1, the frame's estimate, held
    # by the hysteresis or not) or refused at gate 4 with the estimate
    # (fade_arm 0, attributed to the threshold step: the origin distance is
    # defined for the `behind` rows too), all jittered, in the fade-band state.
    routes = [fields(l) for l in trace.splitlines() if l.startswith('motion_route ')]
    for frame in FADE_ROUTE_CAPTURE:
        routed = routed_at[frame]
        pair = FADE_ROUTE_OVERLAY_PAIR if overlay else FADE_ROUTE_PAIR
        fade = [r for r in routes if int(r['frame']) == frame and r['vs'] == pair[0] and r['ps'] == pair[1] and r['zwrite'] == '0']
        assert len(fade) == 2, (name, frame, 'two fade-pair route records', len(fade))
        for r in fade:
            expected = dict(gate='0' if routed else '4', routed=str(int(routed)), matched=str(int(matched_at[frame])), depth=str(int(routed)), jittered='1',
                            zwrite='0', blend='1', src='5', dst='6', atest='0', mask='7', sepalpha='0', fade_arm=str(int(routed)),
                            fade_permille=str(fade_route_permille(script, frame)), fade_held=str(int(held_at[frame])), result='00000000',
                            unmatched='none' if matched_at[frame] else 'history' if routed else 'overlay_node' if overlay else 'fade_threshold')
            if original:
                # The record's blend triple is the composition shadow's (diagnostics only:
                # composition_blend_field); the composition shadow is absent under original
                # shading, so the record carries the unknown marker while the arm read the
                # triple itself (fade_arm_admits).
                expected.update(src='-1', dst='-1', sepalpha='-1')
            actual = {k: r[k] for k in expected}
            assert actual == expected, (name, frame, actual, expected)
    materials = {int(fields(l)['frame']): fields(l) for l in trace.splitlines() if l.startswith('linear_material_frame ')}
    # The linear_material_frame line is written with linear materials requested only;
    # original shading writes fade_route_frame with the arm's counters instead.
    assert sorted(materials) == ([] if original else list(range(FADE_ROUTE_FRAMES))), (name, sorted(materials))
    fade_frames = {int(fields(l)['frame']): fields(l) for l in trace.splitlines() if l.startswith('fade_route_frame ')}
    assert sorted(fade_frames) == (list(range(FADE_ROUTE_FRAMES)) if original else []), (name, sorted(fade_frames))
    for f, row in fade_frames.items():
        expected = (str(2 * int(routed_at[f] and not overlay)), str(2 * int(not routed_at[f] and not overlay)), str(2 * int(held_at[f])), str(FADE_ROUTE_THRESHOLD), '1',
                    str(2 * int(routed_at[f] and overlay)), str(2 * int(not routed_at[f] and overlay)))
        assert (row['fade_routed'], row['fade_refused'], row['fade_held'], row['fade_route'], row['cutout_caps'], row['overlay_routed'], row['overlay_refused']) == expected, (name, f, row)
    for f, row in materials.items():
        expected = (str(2 * int(routed_at[f])), str(2 * int(not routed_at[f])), str(2 * int(held_at[f])), str(FADE_ROUTE_THRESHOLD))
        assert (row['fade_routed'], row['fade_refused'], row['fade_held'], row['fade_route']) == expected, (name, f, row)
    frames = {int(fields(l)['frame']): fields(l) for l in trace.splitlines() if l.startswith('motion_output_frame ')}
    assert sorted(frames) == list(range(FADE_ROUTE_FRAMES)), (name, sorted(frames))
    for f, row in frames.items():
        expected = ('4', str(1 + 2 * int(routed_at[f])), '1', '0', '0', 'lazy' if lazy else 'perdraw')
        assert (row['draws'], row['routed'], row['taa_resolved'], row['apply_failures'], row['restore_failures'], row['rt_mode']) == expected, (name, f, row)
    # Shift of the ramp quad: raw (the FP16 scene after the quads) versus the
    # jitter delta; resolved stable across two routed frames (the second
    # matched) and equal to the raw shift across two bracketed frames.
    shifts = {}
    raw_evidence = resolved_evidence = 0
    worst_raw = worst_resolved = 0.0
    for frame in range(1, FADE_ROUTE_FRAMES):
        _, jx, jy = expected_jitter(frame); _, pjx, pjy = expected_jitter(frame - 1)
        delta = (jx - pjx, jy - pjy)
        raw_previous, shown_previous = fade_route_images(directory, frame - 1)
        raw_current, shown_current = fade_route_images(directory, frame)
        raw = fade_route_shift(raw_previous, raw_current, FADE_ROUTE_WINDOW, 64)
        resolved = fade_route_shift(shown_previous, shown_current, FADE_ROUTE_WINDOW, 64)
        stable, follows = matched_at[frame], not routed_at[frame] and not routed_at[frame - 1]
        if original and follows:
            # No bracket, no M: a refused quad over the sentinel fill takes the far-plane
            # camera path, exact under the rotating camera, so it stays put too.
            stable, follows = True, False
        shifts[frame] = dict(jitter_delta=delta, raw=raw, resolved=resolved, history=history[frame], stable=stable, follows=follows)
        for axis in range(2):
            if abs(delta[axis]) < FADE_ROUTE_MIN_SHIFT:
                continue
            raw_error = abs(raw[axis] - delta[axis]); worst_raw = max(worst_raw, raw_error); raw_evidence += 1
            assert raw_error <= FADE_ROUTE_RAW_TOLERANCE, (name, frame, axis, 'raw shift equals the jitter delta', raw[axis], delta[axis])
            if not history[frame] or not history[frame - 1] or not (stable or follows):
                continue
            bound = FADE_ROUTE_STABLE_FRACTION * abs(raw[axis]) + FADE_ROUTE_NOISE
            residual = abs(resolved[axis]) if stable else abs(resolved[axis] - raw[axis])
            worst_resolved = max(worst_resolved, residual); resolved_evidence += 1
            assert residual <= bound, (name, frame, axis, 'resolved shift', resolved[axis], raw[axis], 'routed stays put' if stable else 'masked follows the raw jitter')
    assert raw_evidence >= FADE_ROUTE_MIN_EVIDENCE and resolved_evidence >= FADE_ROUTE_MIN_EVIDENCE, (name, raw_evidence, resolved_evidence)
    return dict(script=script, frames=FADE_ROUTE_FRAMES, checks=int(terminal[0]['checks']), history_frames=sum(history.values()),
                routed_frames=sum(routed_at.values()), held_frames=sum(held_at.values()), switch_step=switch_step,
                max_code_error=max_code_error, max_tolerance_fraction=max_tolerance_fraction, raw_evidence=raw_evidence, resolved_evidence=resolved_evidence,
                worst_raw_error_px=worst_raw, worst_resolved_residual_px=worst_resolved,
                shifts={f: {k: (list(v) if isinstance(v, tuple) else v) for k, v in row.items()} for f, row in shifts.items()})


def validate_hook(name, installed, text, trace, directory, hdr=False):
    """Hook script: the patch discipline (refusals, bytes, restore), the trampoline
    contract (one signal per call, before the compositor, registers preserved)
    and the resolve at the hook are the fixture's checks; the DLL's per-frame
    lines must name the resolve point and the cross-check verdict per frame."""
    lines = text.splitlines()
    assert lines and lines[-1].startswith('RESULT PASS ') and 'FAIL' not in text and text.count('RESULT ') == 1, f'{name}: fixture did not pass'
    terminal = fields(lines[-1])
    mode_line = fields([l for l in lines if l.startswith('MODE ')][0])
    assert (mode_line['seam'], mode_line['enabled'], mode_line['taa'], mode_line['hook'], mode_line['state_shadow'], mode_line['camera'], mode_line['hdr']) == ('1', '1', '1', '1', '1', '0', str(int(hdr))), (name, mode_line)
    hook_line = fields([l for l in lines if l.startswith('HOOK ')][0])
    # The last refused install (a site that is not a CALL) leaves its status when nothing is installed.
    assert (hook_line['installed'], hook_line['status']) == (('1', 'active') if installed else ('0', 'callsite_mismatch')), (name, hook_line)
    history, skipped = HOOK_HISTORY[installed], HOOK_SKIPPED[installed]
    # Stage 3, AgX write-back: the fixture skips the raster-colour coverage oracle (two checks per frame).
    agx = any(l.startswith('hdr_tonemap ') and fields(l).get('tonemap') == '1' for l in trace.splitlines())
    assert (int(terminal['checks']), int(terminal['restorations']), int(terminal['frames'])) == (HOOK_CHECKS[installed] - (2 * HOOK_FRAMES if agx else 0), 4 * HOOK_FRAMES, HOOK_FRAMES), (name, terminal)
    assert (terminal['taa'], terminal['taa_frames'], terminal['taa_history_frames'], terminal['taa_reference_frames'], terminal['taa_skipped_frames']) == \
           ('1', str(HOOK_FRAMES), str(len(history)), str(HOOK_FRAMES - len(skipped)), str(len(skipped))), (name, terminal)
    restores = [fields(l) for l in lines if l.startswith('RESTORE ')]
    assert len(restores) == 4 * HOOK_FRAMES and all(r['differences'] == '0' for r in restores), f'{name}: restoration differences'
    assert [r['label'] for r in restores] == ['fill', 'draw', 'draw', 'hook'] * HOOK_FRAMES, (name, [r['label'] for r in restores])
    taa_lines = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('TAA ')}
    assert sorted(taa_lines) == list(range(HOOK_FRAMES)), (name, sorted(taa_lines))
    for frame, t in taa_lines.items():
        e = HOOK_SCRIPT[frame]
        assert (int(t['glow']), int(t['outside'])) == (e['glow'], e['outside']) and t['source'] == HOOK_SOURCE[installed][frame], (name, frame, t)
        assert (t['history'], t['skipped'], t['policy']) == (str(int(frame in history)), str(int(frame in skipped)), '1'), (name, frame, t)
        assert (t['changed'] != '0') == (frame in history), (name, frame, t)
    colors = {int(fields(l)['frame']): fields(l)['hash'] for l in lines if l.startswith('COLOR ')}
    before = {int(fields(l)['frame']): fields(l)['hash'] for l in lines if l.startswith('COLOR_BEFORE ')}
    assert sorted(colors) == sorted(before) == list(range(HOOK_FRAMES)), (name, sorted(colors))
    motion = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('MOTION ')}
    assert sorted(motion) == list(range(HOOK_FRAMES)) and all(m['mismatches'] == '0' and m['depth_mismatches'] == '0' for m in motion.values()), (name, motion)
    tl = trace.splitlines()
    # The production install path in this process: the switch parsed, the
    # exact-executable gate refusing (never a patch of the fixture from the
    # loader); the seam export installed the fixture's site afterwards.
    loader = [fields(l) for l in tl if l.startswith('scene_hook active=')]
    assert loader and all((l['active'], l['status']) == ('0', 'executable_mismatch' if installed else 'disabled') for l in loader), (name, loader)
    modes = [fields(l) for l in tl if l.startswith('motion_output_mode ')]
    assert len(modes) == 1 and (modes[0]['scene_hook'], modes[0]['taa'], modes[0]['state_shadow']) == (str(int(installed)), '1', '1'), (name, modes)
    devices = [fields(l) for l in tl if l.startswith('motion_output_device ')]
    assert len(devices) == 1 and devices[0]['enabled'] == '1' and devices[0]['taa'] == '1' and devices[0]['scene_hook'] == '0', (name, devices)
    frames = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_frame ')}
    assert sorted(frames) == list(range(HOOK_FRAMES)), (name, sorted(frames))
    disagreements = [fields(l) for l in tl if l.startswith('motion_output_scene_hook_disagreement ')]
    for frame, f in frames.items():
        e = HOOK_SCRIPT[frame]
        outside = installed and e['outside']
        assert (f['scene_hook'], f['latched'], f['filled'], f['taa']) == (str(int(installed)), '1', '1', '1'), (name, frame, f)
        assert (f['hook_signals'], f['hook_outside_scene'], f['hook_state']) == ((str(int(installed)), str(int(outside)), '1' if outside else '0')), (name, frame, f)
        assert (f['scene_end_source'], f['scene_end_check'], f['bloom_copy_seen'], f['draws_after_hook']) == (HOOK_SOURCE[installed][frame], HOOK_CHECK[installed][frame], str(e['glow']), '0'), (name, frame, f)
        attempted = frame not in skipped
        assert (f['taa_attempted'], f['taa_resolved'], f['taa_history'], f['taa_skip']) == (str(int(attempted)), str(int(attempted)), str(int(frame in history)), '0' if attempted else '2'), (name, frame, f)
        # Glow on: the fixture's depth rebind after the copy rejects the selector (9); off: the frame ends inside the Scene phase (2).
        assert f['selector_state'] == ('9' if e['glow'] else '2'), (name, frame, f)
        assert f['apply_failures'] == f['restore_failures'] == '0' and f['present'] == '00000000', (name, frame, f)
        assert (f['routed'], f['matched']) == ('2', '2' if frame else '0'), (name, frame, f)
        check_render_state(name, frame, f, True, 0)
    assert [(d['frame'], d['installed'], d['signals'], d['outside_scene'], d['selector_state'], d['bloom_copy_seen'], d['hook_scene_end']) for d in disagreements] == \
           ([('2', '1', '1', '1', '1', '1', '0')] if installed else []), (name, disagreements)
    assert not any(l.startswith(('motion_output_taa_failed', 'motion_output_fill_failed', 'motion_output_apply_failed', 'motion_output_restore_failed')) for l in tl), name
    # Debug readbacks of the captured frames 1-6: the DLL's FP16 image equals the reference resolve on every resolved frame.
    taa_readbacks = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_taa_readback ')}
    assert sorted(taa_readbacks) == [f for f in range(1, HOOK_FRAMES) if f not in skipped], (name, sorted(taa_readbacks))
    for frame, t in taa_readbacks.items():
        resolved = (directory / 'x3-modern-captures' / t['file']).read_bytes()
        assert t['result'] == '00000000' and resolved == (directory / f'reference_taa_{frame}.rgba16f').read_bytes(), f'{name}: frame {frame} FP16 image differs from the reference'
    assert sum(l.startswith('motion_output_release ') for l in tl) == 1, name
    # HDR: the hook frames end at the hook (glow on or off), the outside-Scene frame 2 at the bloom copy.
    hdr_summary = validate_hdr(name, trace, directory, hdr, None, frames=range(HOOK_FRAMES), capture_frames=range(1, HOOK_FRAMES), end='hook', taa=True,
                               ends={f: 'bloom_copy' for f in range(HOOK_FRAMES) if HOOK_SCRIPT[f]['outside']} if installed else {f: 'bloom_copy' if HOOK_SCRIPT[f]['glow'] else 'present' for f in range(HOOK_FRAMES)})
    return {'mode': 'hook', 'installed': installed, 'frames': HOOK_FRAMES, 'checks': int(terminal['checks']), 'restorations': int(terminal['restorations']), 'hdr': hdr_summary,
            'hook_status': hook_line['status'], 'sources': {f: t['source'] for f, t in taa_lines.items()}, 'history_frames': sorted(history), 'skipped_frames': sorted(skipped),
            'scene_end_check': {f: int(frames[f]['scene_end_check']) for f in sorted(frames)}, 'disagreements': len(disagreements),
            'color_hashes': colors, 'color_hashes_before_boundary': before, 'taa_changed_pixels': {f: int(t['changed']) for f, t in taa_lines.items()},
            'matched_pixels': int(terminal['matched_pixels']), 'motion_pixels': int(terminal['motion_pixels'])}

# The production configuration (X3M_STATE_SHADOW unset, the hybrid unhook) for
# every production-DLL case except the shadow-on reference of the shadow twins
# and the mip-bias script cases, whose hand-derived per-frame sampler counts
# describe the hooked configuration.
SHADOW_AUTO_EXEMPT = {'production-on'}
for _case in CASES:
    if _case['mode'] == 'production' and _case['shadow'] is True and _case['name'] not in SHADOW_AUTO_EXEMPT and not _case['mipbias']:
        _case['shadow'] = 'auto'


def arguments(argv=None):
    parser = argparse.ArgumentParser(description='Run motion-output cases; explicit retained binaries skip all builds.')
    parser.add_argument('--dll', type=Path, help='Retained production DLL (requires --seam, --fixture and selected cases)')
    parser.add_argument('--seam', type=Path, help='Retained fixture-seam DLL')
    parser.add_argument('--fixture', type=Path, help='Retained motion-output fixture executable')
    parser.add_argument('cases', nargs='*', help='Known case names; omit for the normal fresh-build suite')
    args = parser.parse_args(argv)
    supplied = (args.dll, args.seam, args.fixture)
    if any(p is not None for p in supplied):
        if not all(p is not None for p in supplied):
            parser.error('--dll, --seam and --fixture must be supplied together')
        if not args.cases:
            parser.error('retained binaries require nonempty explicit case selectors')
        for path in supplied:
            if not path.is_file() or path.stat().st_size == 0:
                parser.error(f'retained binary is not a nonempty file: {path}')
        args.dll, args.seam, args.fixture = (p.resolve() for p in supplied)
    unknown = set(args.cases) - {entry['name'] for entry in CASES + WRAP_CASES}
    if unknown:
        parser.error('unknown case selector(s): ' + ', '.join(sorted(unknown)))
    return args


def main(argv=None):
    args = arguments(argv)
    consume_only = args.dll is not None
    candidate_exe, candidate_seam, candidate_dll = (args.fixture, args.seam, args.dll) if consume_only else (EXE, SEAM, DLL)
    binary_hashes = lambda: {str(p) if consume_only else str(p.relative_to(ROOT)): sha(p) for p in (candidate_exe, candidate_seam, candidate_dll)}
    RESULTS.mkdir(exist_ok=True)
    # Development aid: case names on the command line run only those cases and
    # write a partial summary (no cross-case comparisons); never a pass of the suite.
    only = set(args.cases)
    summary_path = RESULTS / ('motion-output-partial.json' if only else 'motion-output-summary.json')
    report_path = RESULTS / ('motion-output-partial.txt' if only else 'motion-output.txt')
    result = {'passed': False, 'status': 'RUNNING', 'game_launched': False, 'bottle': bottle.describe(),
              'scope': 'Live same-draw route (checkpoint B1 + temporal steps 1 and 3: RT2 current depth, per-draw jitter, cut detector, the temporal resolve at the bloom copy with copy-back) and the FP16 HDR scene path stage 1 (X3M_HDR: redirect, identity write-back, unwind ladder) through the actual proxy DLL with one original synthetic device program, plain and under the ownership wrapper (plus copy-depth and admission); seam DLL adds fixture scope injection, target readback and the reference resolve comparison. Bench runs time the boundary. Not gameplay validation.',
              'variants': VARIANTS, 'consume_only': consume_only, 'selected_cases': sorted(only),
              'cases': {}}
    save = lambda: summary_path.write_text(json.dumps(result, indent=2) + '\n')
    save()
    try:
        no_game()
        assert WINE.is_file(), 'CrossOver Preview Wine missing'
        assert all(p.is_file() for p in RAW), 'Local reviewed shader files missing under /tmp/x3-shader-sweep/programs; skipping is not a pass'
        assert all(sha(p) == h for p, h in RAW.items()), 'Local shader bytes differ from the reviewed pair'
        result['local_inputs'] = {str(p): h for p, h in RAW.items()}
        result['sources_before_build'] = sources()
        commands = [['cmake', '-S', '.', '-B', 'build', '-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake', '-DCMAKE_BUILD_TYPE=RelWithDebInfo'],
                    ['cmake', '--build', 'build', '--clean-first', '-j4'],
                    ['i686-w64-mingw32-g++', '-std=c++17', '-Wall', '-Wextra', '-c', 'verification/probe/abi_check.cpp', '-o', 'verification/probe/build/abi_check.o'],
                    ['sh', 'verification/probe/build_motion_output.sh']] if not consume_only else []
        result['build_commands'] = commands
        if commands:
            with (RESULTS / 'motion-output-build.log').open('w') as out:
                for command in commands:
                    subprocess.run(command, cwd=ROOT, check=True, stdout=out, stderr=subprocess.STDOUT)
        assert sources() == result['sources_before_build'], 'Sources changed during build'
        result['binaries'] = binary_hashes()
        save()
        report = []
        wine_log = (RESULTS / 'motion-output-wine.log').open('w')
        result['bench'] = {}
        for entry in CASES + (WRAP_CASES if only else []):
            name, mode, variant, enabled, jitter, taa, bench, lazy, burst, camera, sentinel, envmap, hook, shadow, hdr, hdr_fault, hdr_env, mip_bias, mipbias, cutout = (entry[k] for k in ('name', 'mode', 'variant', 'enabled', 'jitter', 'taa', 'bench', 'lazy', 'burst', 'camera', 'sentinel', 'envmap', 'hook', 'shadow', 'hdr', 'hdr_fault', 'hdr_env', 'mip_bias', 'mipbias', 'cutout'))
            if only and name not in only:
                continue
            directory = BUILD / ('motion-output-' + name + '-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
            directory.mkdir(parents=True)
            shutil.copy(candidate_exe, directory)
            shutil.copy(candidate_seam if mode in ('seam', 'msaa', 'cutout', 'zonly', 'faderoute', 'lightmapfade', 'shadowreplay', 'shadowretention', 'shadowpool') + HDR_MODES and not name.startswith('production') else candidate_dll, directory / 'd3d9.dll')
            env = dict(os.environ, X3M_CAMERA='vanilla', X3M_CHASE_SCENE_FIX='0', X3M_CHASE_COMBAT_TIGHTNESS='0', X3M_MOTION_OUTPUT=enabled, X3M_MOTION_JITTER='1' if jitter else '0', X3M_MOTION_JITTER_SAMPLES=str(JITTER_SAMPLES),
                       X3M_TAA='1' if taa else '0', X3M_TAA_DEBUG='1' if taa and not bench else '0',
                       X3M_CAPTURE_START='1000' if bench else str(BURST_CAPTURE[0]) if burst else '1',
                       X3M_CAPTURE_FRAMES='1' if bench else str(len(BURST_CAPTURE)) if burst else str(len(ENVMAP_CAPTURE)) if envmap else '8', X3M_TELEMETRY='1',
                       X3M_TELEMETRY_DRAW='1',  # per-draw metrics (gate_us, route_draw_us, ...) are gated behind this switch since a8d4309; the validators require them
                       X3M_FIXTURE_CAMERA='rotate' if camera else 'none', X3M_TAA_SENTINEL=sentinel or 'auto', X3M_FIXTURE_WRAP='0',
                       X3M_MOTION_RT_MODE='lazy' if lazy else 'perdraw', X3M_MOTION_FRAME_LOG='1' if burst else '60',
                       X3M_STATE_SHADOW='1' if shadow else '0', X3M_SCENE_HOOK=hook or '0',  # 'default' leaves the switch unset below
                       X3M_HDR='1' if hdr else '0', X3M_FIXTURE_HDR_FAULT=hdr_fault or '',
                       # Identity cases use fixed zero; case metadata opts into
                       # manual EV or Auto. An inherited EV must not mask Auto.
                       X3M_HDR_EXPOSURE='fixed', X3M_HDR_EV_MANUAL='',
                       X3M_OWNERSHIP='0', X3M_DEPTH_COPY='0', X3M_SCENE_DEPTH_CAPTURE='0', X3M_OBJECT_TRACE='0', X3M_OBJECT_LIFETIME='0',
                       X3M_MESH_CACHE='0', X3M_ADMISSION='0', X3M_FINITE_POSITIONS='0', X3M_MOTION_CAPTURE='0',
                       # Loading optimizations have dedicated fixtures. Keep
                       # their baseline explicit despite inherited host env.
                       X3M_CRYPT_CACHE='0', X3M_LOADING_PROBES='0', X3M_MESH_ADJACENCY='native',
                       X3M_MESH_ADJACENCY_DUMP='0', X3M_RESOURCE_READ='native', X3M_DAT_HANDLES='0',
                       X3M_GZ_BUFFER='0', X3M_GZ_BUFFER_KB='256',
                       # The cascade-0 box and the apply bias at their production
                       # defaults unless a case sets them (an inherited value must not).
                       X3M_SHADOW_REPLAY_EXTENT='250', X3M_SHADOW_REPLAY_DEPTH_HALF='512', X3M_SHADOW_REPLAY_CAP='512',
                       X3M_SUN_SHADOW_BIAS_UNITS=repr(sun_apply.BIAS_UNITS_DEFAULT), X3M_SUN_SHADOW_BIAS_CLAMP_TEXELS=repr(sun_apply.BIAS_CLAMP_TEXELS),
                       X3M_SUN_SHADOW_BIAS_SLOPE_TEXELS='0',  # the cascade fixture's baseline law; the -slope cases set 0.2
                       X3M_FIXTURE_SUNAPPLY_WIDE='0', X3M_FIXTURE_SHADOW_FAR_T='240',
                       # Cascades off unless a case sets them (the single-map path is the default).
                       X3M_SHADOW_CASCADES='0', X3M_FIXTURE_SUNAPPLY_CASCADES='0',
                       # Caster retention off unless a case sets it.
                       X3M_SHADOW_RETENTION_CENSUS='0', X3M_SHADOW_CASTER_RETENTION='0', X3M_SHADOW_RETENTION_TIMING='0')
            for inherited in ('X3M_SHADOW_CASCADE_SIZES', 'X3M_SHADOW_CASCADE_CAPS', 'X3M_SHADOW_CASCADE_BUDGET', 'X3M_FIXTURE_SHADOW_CASCADES',
                              'X3M_SHADOW_CASTER_RETENTION_AGE', 'X3M_SHADOW_CASTER_RETENTION_EPS'):
                env.pop(inherited, None)
            env.update(VARIANTS[variant])
            env.pop('X3M_SUN_SHADOW_RECEIVER_DEPTH', None)  # the former option: the DLL and the fixtures read no such variable
            env.update(hdr_env)
            if taa:
                # ca6ad2e made --taa default to sharpen 0.75 and mip bias -0.5;
                # the fixture's TAA checks assume the unsharpened, unbiased
                # resolve unless a case sets them (hdr_env above, mip_bias below).
                env.setdefault('X3M_TAA_SHARPEN', '0'); env.setdefault('X3M_TAA_MIP_BIAS', '0')
            if hook == 'default':
                del env['X3M_SCENE_HOOK']  # the DLL's default: on with X3M_MOTION_OUTPUT=1
            if shadow == 'auto':
                del env['X3M_STATE_SHADOW']  # the production configuration: the hybrid unhook
            if mip_bias is not None:
                env['X3M_TAA_MIP_BIAS'] = mip_bias
            if mipbias:
                # The mip-bias script: every frame's line, capture in frame 5 only.
                env['X3M_MOTION_FRAME_LOG'] = '1'
                env['X3M_CAPTURE_START'] = str(MIPBIAS_CAPTURE[0]); env['X3M_CAPTURE_FRAMES'] = str(len(MIPBIAS_CAPTURE))
            if mode in HDR_MODES or mode == 'zonly':
                env['X3M_MOTION_FRAME_LOG'] = '1'  # every frame's route and hdr lines
            command = [str(WINE), '--bottle', bottle.BOTTLE, '--no-update', '--dll', 'd3d9=n,b', '--workdir', str(directory),
                       str(directory / candidate_exe.name)] + ['Z:' + str(p) for p in RAW] + ['hook' if hook is not None else 'burst' if burst else 'mipbias' if mipbias else 'envmap' if envmap else mode] + ([bench] if bench else [])
            if mode == 'msaa':
                env['X3M_MOTION_FRAME_LOG'] = '1'; env['X3M_FIXTURE_MSAA'] = '2'
            if mode in HDR_MODES:
                # The regular capture window covers the first frames; the frame lines come every frame.
                # hdrexposure needs no early readback and moves the window (the DLL caps it at eight frames)
                # onto the hazard and NaN frames so their stored FP16 values are on record.
                env['X3M_CAPTURE_START'] = str(EXPOSURE_HAZARD_FRAMES[0]) if mode == 'hdrexposure' else '1'; env['X3M_CAPTURE_FRAMES'] = '8'
            no_game()
            wine_log.write(f'==== {name}\n'); wine_log.flush()
            completed = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=wine_log, text=True, timeout=360)
            text = completed.stdout
            (directory / 'fixture-stdout.txt').write_text(text)  # the script's own lines beside its maps (untracked; the validators read `text`)
            report.append(f'==== {name} exit={completed.returncode}\n{text}')
            traces = list((directory / 'x3-modern-captures').glob('session-*.log'))
            assert completed.returncode == 0 and len(traces) == 1, f'{name}: exit {completed.returncode}, traces {len(traces)}'
            trace = traces[0].read_text()
            sharpen = float(hdr_env.get('X3M_TAA_SHARPEN', '0'))
            if bench:
                case = validate_bench(name, taa, bench, text, trace, hdr, tonemap=hdr_env.get('X3M_HDR_TONEMAP') == 'agx', sharpen=sharpen)
                case.update(exit=completed.returncode, directory=str(directory.relative_to(ROOT)), trace_sha256=sha(traces[0]))
                result['bench'][name] = case
                save()
                print(f'{name}: exit={completed.returncode} boundary_ms={case["boundary_ms"]}', flush=True)
                continue
            if mode == 'msaa':
                case = validate_msaa(name, text, trace)
                case.update(exit=completed.returncode, directory=str(directory.relative_to(ROOT)), trace_sha256=sha(traces[0]),
                            dll_sha256=sha(directory / 'd3d9.dll'), exe_sha256=sha(directory / candidate_exe.name))
                shutil.copy(traces[0], RESULTS / f'motion-output-{name}-capture.log')
                result['cases'][name] = case
                save()
                print(f'{name}: exit={completed.returncode} checks={case["checks"]} refused_frame={case["refused"]["frame"]}', flush=True)
                continue
            if mode == 'zonly':
                case = validate_zonly(name, text, trace)
                case.update(exit=completed.returncode, directory=str(directory.relative_to(ROOT)), trace_sha256=sha(traces[0]),
                            dll_sha256=sha(directory / 'd3d9.dll'), exe_sha256=sha(directory / candidate_exe.name))
                shutil.copy(traces[0], RESULTS / f'motion-output-{name}-capture.log')
                result['cases'][name] = case
                save()
                print(f'{name}: exit={completed.returncode} checks={case["checks"]} holes={case["holes"]} unjittered_depth_writers={case["unjittered_depth_writers"]}', flush=True)
                continue
            if mode in HDR_MODES:
                case = {'hdrvalues': validate_hdrvalues, 'hdrfault': validate_hdrfault, 'hdrramp': validate_hdrramp,
                        'hdrexposure': validate_hdrexposure, 'hdrtonemapfault': validate_hdrtonemapfault}[mode](name, text, trace, directory, hdr_env, hdr_fault)
                case.update(exit=completed.returncode, directory=str(directory.relative_to(ROOT)), trace_sha256=sha(traces[0]),
                            dll_sha256=sha(directory / 'd3d9.dll'), exe_sha256=sha(directory / candidate_exe.name))
                shutil.copy(traces[0], RESULTS / f'motion-output-{name}-capture.log')
                result['cases'][name] = case
                save()
                print(f'{name}: exit={completed.returncode} checks={case["checks"]} frames={case["frames"]}', flush=True)
                continue
            if envmap:
                case = validate_envmap(name, text, trace, directory, hdr)
                case.update(exit=completed.returncode, directory=str(directory.relative_to(ROOT)), trace_sha256=sha(traces[0]),
                            dll_sha256=sha(directory / 'd3d9.dll'), exe_sha256=sha(directory / candidate_exe.name))
                shutil.copy(traces[0], RESULTS / f'motion-output-{name}-capture.log')
                result['cases'][name] = case
                save()
                print(f'{name}: exit={completed.returncode} checks={case["checks"]} rejected={case["rejected_frames"]}', flush=True)
                continue
            if hook is not None:
                case = validate_hook(name, hook != '0', text, trace, directory, hdr)
                case.update(exit=completed.returncode, directory=str(directory.relative_to(ROOT)), trace_sha256=sha(traces[0]),
                            dll_sha256=sha(directory / 'd3d9.dll'), exe_sha256=sha(directory / candidate_exe.name))
                shutil.copy(traces[0], RESULTS / f'motion-output-{name}-capture.log')
                result['cases'][name] = case
                save()
                print(f'{name}: exit={completed.returncode} checks={case["checks"]} hook_status={case["hook_status"]} sources={case["sources"]}', flush=True)
                continue
            if mode == 'sunapply':
                scripted = hdr_env.get('X3M_FIXTURE_SUNAPPLY_CASCADES')
                validator = validate_sun_apply_cascades_faces if hdr_env.get('X3M_FIXTURE_SUNAPPLY_FACES') == '1' else validate_sun_apply_cascades_five if scripted == '5' else validate_sun_apply_cascades if scripted == '1' else validate_sun_apply
                case = validator(name, text, directory, hdr_env)
                case.update(exit=completed.returncode, directory=str(directory.relative_to(ROOT)), trace_sha256=sha(traces[0]),
                            dll_sha256=sha(directory / 'd3d9.dll'), exe_sha256=sha(directory / candidate_exe.name),
                            depth_encoding=SUN_APPLY_ENCODING)
                shutil.copy(traces[0], RESULTS / f'motion-output-{name}-capture.log')
                result['cases'][name] = case
                # The compact per-case record the ledger cites (no readbacks).
                (RESULTS / f'{name}-fixture.json').write_text(json.dumps({'case': name, 'bottle': result['bottle'], 'binaries': result['binaries'], **case}, indent=1) + '\n')
                save()
                print(f'{name}: exit={completed.returncode} checks={case["checks"]} worst_codes={case["worst_codes"]:.3f} ambiguous_max={case["ambiguous_max"]} edge={case["edge_mismatch"]} us={case["us"]["median"]}', flush=True)
                continue
            if mode == 'shadowretention':
                case = validate_shadow_retention(name, text, trace, directory, hdr_env)
                case.update(exit=completed.returncode, directory=str(directory.relative_to(ROOT)), trace_sha256=sha(traces[0]),
                            dll_sha256=sha(directory / 'd3d9.dll'), exe_sha256=sha(directory / candidate_exe.name))
                # The three settings present byte-identical frames (the option changes no presented pixel).
                for sibling in SHADOW_RETENTION_CASES:
                    if sibling != name and name != SHADOW_RETENTION_POLL_CASE and sibling in result['cases']:
                        assert result['cases'][sibling]['color_sha256'] == case['color_sha256'], f'{name}: presented frames differ from {sibling}'
                        case.setdefault('presented_identical_to', []).append(sibling)
                result['cases'][name] = case
                # The compact per-case record the ledger cites (no maps, no logs).
                (RESULTS / f'{name}-fixture.json').write_text(json.dumps({'case': name, 'bottle': result['bottle'], 'binaries': result['binaries'], **case}, indent=1) + '\n')
                save()
                print(f'{name}: exit={completed.returncode} checks={case["checks"]} mode={case["mode"]} frames={case["frames"]} compared={case["compared_frames"]} retained_compared={case["retained_compared_frames"]} max_depth_error={case["map"]["max_depth_error"]}', flush=True)
                continue
            if mode == 'shadowpool':
                case = validate_shadow_pool(name, text, trace, directory, hdr_env)
                case.update(exit=completed.returncode, directory=str(directory.relative_to(ROOT)), trace_sha256=sha(traces[0]),
                            dll_sha256=sha(directory / 'd3d9.dll'), exe_sha256=sha(directory / candidate_exe.name))
                # The three store settings of the static script present byte-identical frames.
                for sibling in SHADOW_POOL_STATIC_CASES:
                    if name in SHADOW_POOL_STATIC_CASES and sibling != name and sibling in result['cases']:
                        assert result['cases'][sibling]['color_sha256'] == case['color_sha256'], f'{name}: presented frames differ from {sibling}'
                        case.setdefault('presented_identical_to', []).append(sibling)
                result['cases'][name] = case
                (RESULTS / f'{name}-fixture.json').write_text(json.dumps({'case': name, 'bottle': result['bottle'], 'binaries': result['binaries'], **case}, indent=1) + '\n')
                save()
                print(f'{name}: exit={completed.returncode} checks={case["checks"]} script={case["script"]} store={case["store"]} frames={case["frames"]} maps={case["map"]["maps"]} max_depth_error={case["map"]["max_depth_error"]} us={case["us"].get("median")}', flush=True)
                continue
            if mode == 'shadowreplay':
                case = (validate_shadow_replay_adaptive if 'X3M_FIXTURE_OWN_SHIP' in hdr_env else validate_shadow_replay_cascades if 'X3M_FIXTURE_SHADOW_CASCADES' in hdr_env else validate_shadow_replay)(name, text, trace, directory, hdr_env, taa)  # the wrapper's own frame lines belong to the 12-frame seam script (validate_ownership); this script has 8 frames and a Reset
                case.update(exit=completed.returncode, directory=str(directory.relative_to(ROOT)), trace_sha256=sha(traces[0]),
                            dll_sha256=sha(directory / 'd3d9.dll'), exe_sha256=sha(directory / candidate_exe.name))
                shutil.copy(traces[0], RESULTS / f'motion-output-{name}-capture.log')
                if 'X3M_FIXTURE_OWN_SHIP' in hdr_env:  # the compact per-case record the ledger cites (no per-texel comparisons)
                    compact = {k: v for k, v in case.items() if k not in ('map', 'per_frame', 'sun', 'refusals', 'targets')}
                    compact['map'] = {k: v for k, v in case['map'].items() if k != 'frames'}
                    (RESULTS / f'{name}-fixture.json').write_text(json.dumps({'case': name, 'bottle': result['bottle'], 'binaries': result['binaries'], **compact}, indent=1) + '\n')
                result['cases'][name] = case
                save()
                print(f'{name}: exit={completed.returncode} checks={case["checks"]} depth={case["depth"]} casters={case["casters"]} us={case.get("us", {}).get("median")} max_depth_error={case.get("map", {}).get("max_depth_error")}', flush=True)
                continue
            if mode == 'lightmapfade':
                case = validate_lightmap_fade(name, LIGHTMAP_FADE_CASES[name], text, trace)
                case.update(exit=completed.returncode, directory=str(directory.relative_to(ROOT)), trace_sha256=sha(traces[0]),
                            dll_sha256=sha(directory / 'd3d9.dll'), exe_sha256=sha(directory / candidate_exe.name))
                shutil.copy(traces[0], RESULTS / f'motion-output-{name}-capture.log')
                result['cases'][name] = case
                # Near draw = today's image: the fade cases against the option-off twin, bit for bit.
                off = result['cases'].get('seam-lightmap-far-fade-off')
                if off and name != 'seam-lightmap-far-fade-off':
                    assert case['steps']['near']['hdr_hash'] == off['steps']['near']['hdr_hash'], (name, 'near image differs from the option-off run')
                    assert case['steps']['near_off']['hdr_hash'] == off['steps']['near_off']['hdr_hash'], (name, 'base image differs from the option-off run')
                    case['near_matches_off'] = True
                save()
                print(f'{name}: exit={completed.returncode} checks={case["checks"]} gains={ {k: v["gain"] for k, v in case["steps"].items()} } near_matches_off={case.get("near_matches_off")}', flush=True)
                continue
            if mode == 'faderoute':
                case = validate_fade_route(name, hdr_env['X3M_FIXTURE_FADE_SCRIPT'], lazy, text, trace, directory)
                if name in LIGHTMAP_FADE_OVERLAY_CASES:
                    case['script_lines'] = [l for l in text.splitlines() if l.startswith(('FADE_ROUTE ', 'FADE_ROUTE_SAMPLE ', 'COLOR '))]
                    gained = [fields(l) for l in trace.splitlines() if l.startswith('hull_lightmap_frame ')]
                    faded = [fields(l) for l in trace.splitlines() if l.startswith('hull_lightmap_far_fade_frame ')]
                    assert gained and all(int(g['admitted']) >= 1 for g in gained), (name, 'the routed hull parent binds the gained variant')
                    case['lightmap'] = dict(gained_frames=len(gained), far_fade_frames=len(faded))
                    if 'X3M_LIGHT_MAP_FAR_FADE' in hdr_env:
                        assert 'light_map_far_fade_configured accepted=1' in trace and len(faded) == len(gained), (name, len(faded), len(gained))
                        assert all(f['faded'] == '0' and f['camera'] == '1' for f in faded), (name, 'near footprint: the full gain')
                        twin = result['cases'].get('seam-taa-fade-route-overlay-lightmap')
                        if twin:
                            assert twin['script_lines'] == case['script_lines'] and case['script_lines'], (name, 'option on at a near footprint differs from the constant gain')
                            case['matches_constant_gain_twin'] = True
                    else:
                        assert not faded, name
                case.update(exit=completed.returncode, directory=str(directory.relative_to(ROOT)), trace_sha256=sha(traces[0]),
                            dll_sha256=sha(directory / 'd3d9.dll'), exe_sha256=sha(directory / candidate_exe.name))
                shutil.copy(traces[0], RESULTS / f'motion-output-{name}-capture.log')
                result['cases'][name] = case
                save()
                print(f'{name}: exit={completed.returncode} checks={case["checks"]} history_frames={case["history_frames"]} worst_raw_error_px={case["worst_raw_error_px"]:.3f} worst_resolved_residual_px={case["worst_resolved_residual_px"]:.3f}', flush=True)
                continue
            if cutout:
                case = validate_cutout(name, cutout, text, trace)
                if shadow is not True:
                    # Hooks off: the candidate marker's repeat reads (ALPHATESTENABLE, the cutout states)
                    # is served by the per-draw cache (rs_hits > 0), every other read
                    # is one native read, nothing is invalidated.
                    frame_lines = {int(fields(l)['frame']): fields(l) for l in trace.splitlines() if l.startswith('motion_output_frame ')}
                    case['render_state'] = {f: {k: int(frame_lines[f][k]) for k in ('rs_queries', 'rs_hits', 'rs_gets', 'rs_invalidations')} | {'mode': frame_lines[f]['rs_mode']}
                                            for f in sorted(frame_lines)}
                    assert all(v['mode'] == shadow_mode(shadow, lazy) and v['rs_invalidations'] == 0 for v in case['render_state'].values()), (name, case['render_state'])
                    assert sum(v['rs_hits'] for v in case['render_state'].values()) > 0, (name, 'the per-draw cache served no repeat read')
                case.update(exit=completed.returncode, directory=str(directory.relative_to(ROOT)), trace_sha256=sha(traces[0]),
                            dll_sha256=sha(directory / 'd3d9.dll'), exe_sha256=sha(directory / candidate_exe.name))
                shutil.copy(traces[0], RESULTS / f'motion-output-{name}-capture.log')
                result['cases'][name] = case
                save()
                print(f'{name}: exit={completed.returncode} checks={case["checks"]} history_frames={case["history_frames"]} cutout_missed_frames={case["cutout_missed_frames"]}', flush=True)
                continue
            if mipbias:
                case = validate_mipbias(name, mode, lazy, mip_bias, text, trace, directory)
                case.update(exit=completed.returncode, directory=str(directory.relative_to(ROOT)), trace_sha256=sha(traces[0]),
                            dll_sha256=sha(directory / 'd3d9.dll'), exe_sha256=sha(directory / candidate_exe.name))
                shutil.copy(traces[0], RESULTS / f'motion-output-{name}-capture.log')
                result['cases'][name] = case
                save()
                print(f'{name}: exit={completed.returncode} checks={case["checks"]} sets={case["sets_per_frame"]} evidence={case["evidence"]}', flush=True)
                continue
            if burst:
                case = validate_burst(name, mode, lazy, text, trace, directory, shadow, wrap=hdr_env.get('X3M_FIXTURE_WRAP') == '1', mask=hdr_env.get('X3M_FIXTURE_BURST_MASK') == '1')
                case.update(exit=completed.returncode, directory=str(directory.relative_to(ROOT)), trace_sha256=sha(traces[0]),
                            dll_sha256=sha(directory / 'd3d9.dll'), exe_sha256=sha(directory / candidate_exe.name))
                shutil.copy(traces[0], RESULTS / f'motion-output-{name}-capture.log')
                result['cases'][name] = case
                save()
                print(f'{name}: exit={completed.returncode} checks={case["checks"]} set_rt={case["set_rt_per_frame"]}', flush=True)
                continue
            case = finish_case(name, mode, variant, enabled == '1', jitter, taa, text, trace, directory, lazy, camera, sentinel, shadow, hdr, hdr_fault, mip_bias, sharpen,
                               copy_draw=hdr_env.get('X3M_FIXTURE_STRETCH_FAULT') == '1', quad_fvf=hdr_env.get('X3M_FIXTURE_QUAD_FVF') == '1')
            if hdr_env.get('X3M_SHADOW_REPLAY_CANDIDATES') == '1':
                case['shadow_replay_candidates'] = validate_shadow_replay_candidates(name, trace)
                case['checks'] += case['shadow_replay_candidates']['checks']
            if sharpen > 0:
                case['sharpen'] = validate_sharpen(name, text, trace, directory, hdr_env, hdr, sharpen)
            elif hdr and taa and hdr_env and hdr_fault is None and mode == 'seam':
                case['hdr_taa'] = validate_hdr_taa(name, text, trace, directory, hdr_env)
            elif hdr and taa and hdr_fault is None and (hdr_env or {}).get('X3M_HDR_TONEMAP', 'identity') == 'identity':
                case['hdr_identity_k'] = validate_identity_k(name, trace, hdr_env)
            case.update(exit=completed.returncode, directory=str(directory.relative_to(ROOT)), trace_sha256=sha(traces[0]),
                        dll_sha256=sha(directory / 'd3d9.dll'), exe_sha256=sha(directory / candidate_exe.name))
            if enabled == '1':
                shutil.copy(traces[0], RESULTS / f'motion-output-{name}-capture.log')
            result['cases'][name] = case
            save()
            print(f'{name}: exit={completed.returncode} checks={case["checks"]} motion_pixels={case["motion_pixels"]}', flush=True)
        wine_log.close()
        # Depth replay twins: the option on must present byte-identical frames
        # to the option-off twin (TAA off and on); compared whenever both ran.
        result['shadow_replay_twins'] = {}
        for on_name, off_name in SHADOW_REPLAY_TWINS.items():
            if on_name not in result['cases'] or off_name not in result['cases']:
                continue
            a, b = result['cases'][on_name], result['cases'][off_name]
            presented = compare_presented(on_name, off_name, ROOT / a['directory'], ROOT / b['directory'], range(SHADOW_REPLAY_FRAMES))
            assert presented['identical'] and a['color_hashes'] == b['color_hashes'], f'{on_name}: presented frames differ from {off_name}: {presented}'
            result['shadow_replay_twins'][on_name] = {'twin': off_name, 'presented': presented, 'color_hashes_identical': True}
            save()
        if only:
            assert set(result['cases']) | set(result['bench']) == only, 'Selected case inventory differs from requested cases'
            assert binary_hashes() == result['binaries'], 'Binaries changed during selected run'
            result['sources_after_run'] = sources()
            assert result['sources_after_run'] == result['sources_before_build'], 'Sources changed during selected run'
            result['status'] = 'PARTIAL'
            save()
            report_path.write_text(''.join(report))
            compare_mask_twins(result, save)
            write_lightmap_fade_record(result)
            print('partial run: no cross-case comparisons, not a pass')
            return
        # Resolve cost: the boundary with the switch on minus off, per size.
        for size in BENCH_SIZES:
            off, on = result['bench'][f'bench-{size}-taa-off']['boundary_ms'], result['bench'][f'bench-{size}-taa-on']['boundary_ms']
            result['bench'][f'resolve-{size}'] = {'median_ms': on['median'] - off['median'], 'min_ms': on['min'] - off['min'],
                                                  'boundary_off_median_ms': off['median'], 'boundary_on_median_ms': on['median']}
            # Sharpen cost: the sharpened boundary minus the unsharpened one, per route (8-bit: the RCAS draw replaces the copy-back; HDR: five AgX evaluations per pixel replace one).
            for label, base_name, sharp_name in (('sharpen', f'bench-{size}-taa-on', f'bench-{size}-taa-sharpen-on'),
                                                 ('sharpen-hdr-tonemap', f'bench-{size}-hdr-tonemap-taa-on', f'bench-{size}-hdr-tonemap-taa-sharpen-on')):
                base, sharp = result['bench'][base_name]['boundary_ms'], result['bench'][sharp_name]['boundary_ms']
                result['bench'][f'{label}-{size}'] = {'median_ms': sharp['median'] - base['median'], 'min_ms': sharp['min'] - base['min'],
                                                      'boundary_unsharpened_median_ms': base['median'], 'boundary_sharpened_median_ms': sharp['median']}
        # Post-resolve sharpen twins: the resolved FP16 history files (frames
        # 1-8, X3M_TAA_DEBUG) equal the unsharpened twin's byte for byte in
        # every sharpen run; off is byte-identical in the presented frames and
        # colour hashes too, on differs in the presented frames.
        def history_files(case_name):
            directory = ROOT / result['cases'][case_name]['directory'] / 'x3-modern-captures'
            files = {p.name: sha(p) for p in sorted(directory.glob('taa_*.rgba16f'))}
            assert len(files) == 8, (case_name, sorted(files))
            return files
        result['sharpen'] = {}
        for sharpen_name, twin in SHARPEN_TWINS.items():
            a, b = result['cases'][sharpen_name], result['cases'][twin]
            assert history_files(sharpen_name) == history_files(twin), f'{sharpen_name}: resolved FP16 history differs from {twin}: the sharpen reached the history'
            presented = compare_presented(sharpen_name, twin, ROOT / a['directory'], ROOT / b['directory'], range(12))
            entry = {'twin': twin, 'history_identical': True, 'presented': presented}
            if sharpen_name.endswith('-off'):
                assert presented['identical'] and a['color_hashes'] == b['color_hashes'], f'{sharpen_name}: sharpen 0 is not byte-identical to {twin}: {presented}'
                assert (a['checks'], a['restorations']) == (b['checks'], b['restorations']), (sharpen_name, twin)
            else:
                assert not presented['identical'] and presented['differing_channels_bgra'][3] == 0, f'{sharpen_name}: presented frames equal {twin} (nothing sharpened) or alpha differs: {presented}'
                entry['reference'] = {k: a['sharpen'][k] for k in ('sharpen', 'gain', 'max_code_error', 'mean_code_error', 'changed_fraction')}
            result['sharpen'][sharpen_name] = entry
        # Color is bit-identical with the route off and on in every environment,
        # and the wrapper/depth/admission environments change nothing either.
        # With jitter on the raster moves: the colour must differ from the
        # unjittered run (the per-pixel coverage oracle above says by how much).
        for mode in ('production', 'seam'):
            reference = result['cases'][mode + '-off']['color_hashes']
            for variant in VARIANTS:
                prefix = mode + '-' if variant == 'plain' else f'{mode}-{variant}-'
                off, on = result['cases'][prefix + 'off']['color_hashes'], result['cases'][prefix + 'on']['color_hashes']
                assert off == on, f'{prefix}: color differs between route off and on'
                assert off == reference, f'{prefix}: color differs from the plain run'
            jittered = result['cases'][mode + '-jitter-on']['color_hashes']
            differing = [f for f in jittered if jittered[f] != reference[f]]
            assert len(differing) >= 6, f'{mode}-jitter-on: jitter changed the colour of only {len(differing)} of 12 frames'
            result['cases'][mode + '-jitter-on']['frames_differing_from_unjittered'] = differing
        assert result['cases']['production-jitter-on']['color_hashes'] == result['cases']['seam-jitter-on']['color_hashes'], 'jitter colour differs between the production and the seam DLL'
        # Lazy RT binding equivalence. Regular script: the lazy runs equal their
        # per-draw twins in colour, readback files, checks and restorations.
        # Burst script: colour, state signature, seam RT1/RT2 hashes and the
        # DLL's readback files agree between the modes; the SetRenderTarget
        # count per frame drops from 20 to 12 outside the capture frames.
        def readback_files(case_name):
            directory = ROOT / result['cases'][case_name]['directory'] / 'x3-modern-captures'
            return {p.name: sha(p) for p in sorted(directory.glob('*.rgba32f')) + sorted(directory.glob('*.r32f'))}
        # Native-Windows fixes: the XYZRHW quad twin is byte-identical to the
        # vs_3_0 quads everywhere (presented frames, RT1/RT2 readbacks, FP16
        # history, checks); the draw copy mode reproduces the stretch mode's
        # history byte for byte and its presented frames within one code.
        result['native_windows'] = {}
        for twin_name, twin in QUAD_TWINS.items():
            a, b = result['cases'][twin_name], result['cases'][twin]
            assert a['color_hashes'] == b['color_hashes'], f'{twin_name}: colour differs from {twin}'
            assert a['color_hashes_before_boundary'] == b['color_hashes_before_boundary'], (twin_name, twin)
            assert (a['checks'], a['restorations'], a['motion_pixels'], a['matched_pixels'], a['depth_written_pixels']) == \
                   (b['checks'], b['restorations'], b['motion_pixels'], b['matched_pixels'], b['depth_written_pixels']), (twin_name, twin)
            assert history_files(twin_name) == history_files(twin), f'{twin_name}: resolved FP16 history differs from {twin}'
            files_a, files_b = readback_files(twin_name), readback_files(twin)
            assert files_a and files_a == files_b, f'{twin_name}: readback files differ from {twin}'
            assert a['taa_copy'] == b['taa_copy'] == 'stretch', (twin_name, a['taa_copy'])
            result['native_windows'][twin_name] = {'twin': twin, 'identical': True, 'readback_files': len(files_a), 'history_files': 8, 'quad': 'xyzrhw_fixed_function'}
        for twin_name, twin in COPY_TWINS.items():
            a, b = result['cases'][twin_name], result['cases'][twin]
            assert a['taa_copy'] == 'draw' and b['taa_copy'] == 'stretch', (twin_name, a['taa_copy'], b['taa_copy'])
            assert (a['checks'], a['restorations'], a['motion_pixels'], a['matched_pixels'], a['depth_written_pixels']) == \
                   (b['checks'], b['restorations'], b['motion_pixels'], b['matched_pixels'], b['depth_written_pixels']), (twin_name, twin)
            assert a['color_hashes_before_boundary'] == b['color_hashes_before_boundary'], (twin_name, twin)
            history_identical = history_files(twin_name) == history_files(twin)
            presented = compare_presented(twin_name, twin, ROOT / a['directory'], ROOT / b['directory'], range(12))
            assert presented['max_code_difference'] <= 1 and presented['differing_channels_bgra'][3] == 0, f'{twin_name}: presented frames differ from {twin} by more than one code or in alpha: {presented}'
            files_a, files_b = readback_files(twin_name), readback_files(twin)
            assert files_a and files_a == files_b, f'{twin_name}: RT1/RT2 readback files differ from {twin}'
            result['native_windows'][twin_name] = {'twin': twin, 'history_identical': history_identical, 'presented': presented,
                                                   'presented_identical': presented['identical'], 'copy': 'staging_stretch_plus_identity_draws'}
        result['native_windows']['seam-msaa'] = result['cases']['seam-msaa']['refused']
        equivalence = {'regular': {}, 'burst': {}}
        for lazy_name, twin in (('production-lazy-on', 'production-on'), ('seam-lazy-on', 'seam-on'),
                                ('seam-ownership-lazy-on', 'seam-ownership-on'), ('seam-taa-lazy-on', 'seam-taa-on')):
            a, b = result['cases'][lazy_name], result['cases'][twin]
            assert a['color_hashes'] == b['color_hashes'], f'{lazy_name}: colour differs from {twin}'
            assert (a['checks'], a['restorations'], a['motion_pixels'], a['matched_pixels'], a['depth_written_pixels']) == \
                   (b['checks'], b['restorations'], b['motion_pixels'], b['matched_pixels'], b['depth_written_pixels']), (lazy_name, twin)
            files_a, files_b = readback_files(lazy_name), readback_files(twin)
            assert files_a and files_a == files_b, f'{lazy_name}: readback files differ from {twin}'
            if 'color_hashes_before_boundary' in b:
                assert a['color_hashes_before_boundary'] == b['color_hashes_before_boundary'], (lazy_name, twin)
            equivalence['regular'][lazy_name] = {'twin': twin, 'readback_files': len(files_a), 'identical': True}
        for dll in ('production', 'seam'):
            per, lz = result['cases'][f'{dll}-burst-perdraw'], result['cases'][f'{dll}-burst-lazy']
            assert per['color_hashes'] == lz['color_hashes'], f'{dll}-burst: colour differs between the modes'
            assert per['state_hashes'] == lz['state_hashes'], f'{dll}-burst: final state differs between the modes'
            assert per['motion_hashes'] == lz['motion_hashes'], f'{dll}-burst: RT1/RT2 differ between the modes'
            assert per['readback_sha256'] == lz['readback_sha256'], f'{dll}-burst: readback files differ between the modes'
            assert all(per['set_rt_per_frame'][f] == BURST_SET_RT['perdraw'] for f in range(BURST_FRAMES)), per['set_rt_per_frame']
            assert all(lz['set_rt_per_frame'][f] == (BURST_SET_RT['perdraw'] if f in BURST_CAPTURE else BURST_SET_RT['lazy']) for f in range(BURST_FRAMES)), lz['set_rt_per_frame']
            equivalence['burst'][dll] = {'frames': BURST_FRAMES, 'capture_frames': list(BURST_CAPTURE), 'identical_color': True, 'identical_state': True,
                                         'identical_motion_depth': bool(per['motion_hashes']) or dll == 'production', 'identical_readback_files': True,
                                         'set_rt_per_frame': {'perdraw': per['set_rt_per_frame'], 'lazy': lz['set_rt_per_frame']},
                                         'lazy_flushes_per_frame': lz['lazy_flushes_per_frame']}
        result['lazy_equivalence'] = equivalence
        compare_mask_twins(result, save)
        # TAA: the production DLL resolves current-only (sentinel routes), so
        # its presented image is bit-identical to the jittered run without the
        # resolve, plain and through the wrapper; the seam's frames without
        # history are identical too, and its history frames differ (the moving
        # edges blend the previous frame). The image before the boundary is the
        # jittered raster in every TAA run.
        jittered = result['cases']['production-jitter-on']['color_hashes']
        for prefix in ('production-taa-on', 'production-ownership-taa-on'):
            assert result['cases'][prefix]['color_hashes'] == jittered, f'{prefix}: current-only resolve changed the colour'
            assert result['cases'][prefix]['color_hashes_before_boundary'] == jittered, f'{prefix}: pre-boundary colour differs'
        for prefix in ('seam-taa-on', 'seam-ownership-taa-on'):
            hashes = result['cases'][prefix]['color_hashes']
            assert result['cases'][prefix]['color_hashes_before_boundary'] == jittered, f'{prefix}: pre-boundary colour differs'
            assert all(hashes[f] == jittered[f] for f in hashes if f not in SEAM_TAA_HISTORY), f'{prefix}: a no-history frame differs from the jittered raster'
            blended = [f for f in hashes if f in SEAM_TAA_HISTORY and hashes[f] != jittered[f]]
            assert blended, f'{prefix}: no history frame changed the colour'
            result['cases'][prefix]['frames_changed_by_history'] = blended
        assert result['cases']['seam-taa-on']['color_hashes'] == result['cases']['seam-ownership-taa-on']['color_hashes'], 'resolved colour differs through the wrapper'
        # Camera reprojection: the switch off with a camera installed equals no
        # camera at all (bit-identical colour, the pre-change behaviour); the
        # strict mode without a camera never resolves (the jittered raster);
        # the camera path changes the colour of history frames (the unrouted
        # pixels now blend the reprojected previous frame) and of no other.
        seam_taa, camera_on = result['cases']['seam-taa-on']['color_hashes'], result['cases']['seam-taa-camera-on']['color_hashes']
        assert result['cases']['seam-taa-camera-sentinel1-on']['color_hashes'] == seam_taa, 'X3M_TAA_SENTINEL=1 with a camera differs from the run without a camera'
        assert result['cases']['seam-taa-sentinel2-nocamera-on']['color_hashes'] == jittered, 'strict mode without a camera changed the colour'
        assert all(camera_on[f] == seam_taa[f] for f in camera_on if f not in SEAM_TAA_HISTORY), 'camera path changed a frame without history'
        camera_changed = [f for f in camera_on if f in SEAM_TAA_HISTORY - CAMERA_CUT_FRAMES and camera_on[f] != seam_taa[f]]
        assert camera_changed, 'camera path changed no history frame'
        assert camera_on[7] == jittered[7], 'the camera-cut frame is not the jittered raster'
        assert result['cases']['seam-taa-camera-sentinel2-on']['color_hashes'] == camera_on, 'strict mode with a camera differs from auto'
        result['camera'] = {'frames_changed_by_camera_path': camera_changed, 'switch_off_equals_no_camera': True, 'strict_without_camera_never_resolves': True,
                            'envmap': {k: result['cases']['seam-taa-envmap'][k] for k in ('rejected_frames', 'routed_per_frame', 'camera_valid_per_frame')}}
        # Render-state shadow A/B: every shadow-off run equals its twin in every
        # observable (colour, pre-boundary colour, readback files, per-draw
        # route decisions, checks, restorations, cost-free counters), and the
        # counters show the shadow answering every route query.
        shadow_report = {}
        for off_name, twin in SHADOW_TWINS.items():
            a, b = result['cases'][off_name], result['cases'][twin]
            assert a['color_hashes'] == b['color_hashes'], f'{off_name}: colour differs from {twin}'
            assert a['route_decisions'] == b['route_decisions'], f'{off_name}: route decisions differ from {twin}'
            assert (a['checks'], a['restorations'], a['motion_pixels'] if 'motion_pixels' in a else None, a['depth_written_pixels']) == \
                   (b['checks'], b['restorations'], b['motion_pixels'] if 'motion_pixels' in b else None, b['depth_written_pixels']), (off_name, twin)
            for key in ('color_hashes_before_boundary', 'state_hashes', 'motion_hashes', 'readback_sha256', 'set_rt_per_frame', 'lazy_flushes_per_frame', 'taa_history_frames'):
                if key in b:
                    assert a[key] == b[key], f'{off_name}: {key} differs from {twin}'
            if not b.get('burst'):
                files_a, files_b = readback_files(off_name), readback_files(twin)
                assert files_a and files_a == files_b, f'{off_name}: readback files differ from {twin}'
            gets_off = sum(v['gets'] for v in a['render_state'].values()); gets_on = sum(v['gets'] for v in b['render_state'].values())
            queries = sum(v['queries'] for v in b['render_state'].values())
            modes_off = {v['mode'] for v in a['render_state'].values()}
            assert len(modes_off) == 1 and modes_off <= {'get', 'native'}, (off_name, modes_off)
            mode_off = modes_off.pop()
            if mode_off == 'native':
                assert sum(v['hits'] for v in a['render_state'].values()) == 0 and gets_off == queries + RS_FILL_GETS * len(a['render_state']), off_name
            else:  # get: the per-draw cache's own accounting, checked per frame; no hook, no invalidation
                queries_off = sum(v['queries'] for v in a['render_state'].values()); hits_off = sum(v['hits'] for v in a['render_state'].values())
                assert gets_off == queries_off - hits_off + RS_FILL_GETS * len(a['render_state']), off_name
                assert sum(v['invalidations'] for v in a['render_state'].values()) == 0, off_name
            shadow_report[off_name] = {'twin': twin, 'identical': True, 'frames': len(b['render_state']), 'route_queries': queries, 'mode': mode_off,
                                       'route_queries_off': sum(v['queries'] for v in a['render_state'].values()),
                                       'native_gets_shadow_off': gets_off, 'native_gets_shadow_on': gets_on,
                                       'shadow_hits': sum(v['hits'] for v in b['render_state'].values()), 'resyncs': sum(v['resyncs'] for v in b['render_state'].values())}
        result['state_shadow'] = shadow_report
        # Engine scene-end hook: the frames both runs resolve (glow on with the
        # signal in the Scene phase, and the outside-Scene frame the copy path
        # resolves in both) are bit-identical between the patched and the
        # unpatched run: the resolve at the hook equals the resolve at the copy.
        # The pre-boundary raster is identical in every frame; the glow-off
        # frames and the frame after them differ (resolved with history at
        # the hook, left raw/without history at the copy).
        on, off = result['cases']['seam-taa-hook-on'], result['cases']['seam-taa-hook-unpatched']
        assert on['color_hashes_before_boundary'] == off['color_hashes_before_boundary'], 'hook script: pre-boundary raster differs'
        same = [f for f in range(HOOK_FRAMES) if on['color_hashes'][f] == off['color_hashes'][f]]
        assert same == [0, 1, 2, 3], f'hook script: identical frames {same}, expected 0-3'
        result['scene_hook'] = {'identical_frames_hook_vs_copy': same, 'frames_only_the_hook_resolves': [4, 5], 'frames_differing_afterwards': [6],
                                'installed': {k: on[k] for k in ('hook_status', 'sources', 'scene_end_check', 'disagreements', 'checks')},
                                'unpatched': {k: off[k] for k in ('hook_status', 'sources', 'scene_end_check', 'disagreements', 'checks')}}
        # FP16 HDR scene path, stage 1: every HDR twin presents the same frames
        # as its twin (per-pixel dumps: exact), the seam readbacks (RT1/RT2
        # written through the four-format MRT) are identical, and the runs with
        # a forced-absent capability equal the plain seam run; the bench
        # reports the redirect's boundary cost per size with the resolve off and on.
        hdr_report = {}
        for hdr_name, twin in HDR_TWINS.items():
            a, b = result['cases'][hdr_name], result['cases'][twin]
            frames = sorted(int(f) for f in a['color_hashes'])
            comparison = compare_presented(hdr_name, twin, ROOT / a['directory'], ROOT / b['directory'], frames)
            accept_hdr_twin(hdr_name, comparison, taa='taa' in hdr_name)
            comparison['color_hashes_identical'] = a['color_hashes'] == b['color_hashes']
            for key in ('checks', 'restorations', 'motion_pixels', 'matched_pixels', 'depth_written_pixels', 'route_decisions', 'taa_history_frames', 'sources', 'scene_end_check'):
                if key in b:
                    assert a[key] == b[key], f'{hdr_name}: {key} differs from {twin}'
            if hdr_name.startswith('seam') and 'envmap' not in hdr_name and 'hook' not in hdr_name:
                files_a, files_b = readback_files(hdr_name), readback_files(twin)
                assert files_a and files_a == files_b, f'{hdr_name}: RT1/RT2 readback files differ from {twin}'
                comparison['readback_files_identical'] = len(files_a)
            hdr_report[hdr_name] = comparison
        for absent in ('seam-hdr-caps-absent', 'seam-hdr-selftest-absent'):
            a, b = result['cases'][absent], result['cases']['seam-on']
            assert a['color_hashes'] == b['color_hashes'] and a['hdr']['enabled'] is False, f'{absent}: the disabled feature changed the colour'
            hdr_report[absent] = {'twin': 'seam-on', 'identical': True, 'reason': a['hdr']['reason']}
        for size in BENCH_SIZES:
            for state in ('off', 'on'):
                on, off = result['bench'][f'bench-{size}-hdr-on-taa-{state}']['boundary_ms'], result['bench'][f'bench-{size}-taa-{state}']['boundary_ms']
                result['bench'][f'hdr-{size}-taa-{state}'] = {'median_ms': on['median'] - off['median'], 'min_ms': on['min'] - off['min'],
                                                               'boundary_hdr_off_median_ms': off['median'], 'boundary_hdr_on_median_ms': on['median'],
                                                               'target_bytes': result['bench'][f'bench-{size}-hdr-on-taa-{state}']['target_bytes']}
        result['hdr'] = {'twins': hdr_report, 'values': result['cases']['seam-hdr-values']['values'], 'fault_script': result['cases']['seam-hdr-fault']['unwinds'],
                         'device': result['cases']['seam-hdr-on']['hdr']['device']}
        result['color_identical_off_vs_on'] = True
        result['color_identical_across_variants'] = True
        result['jitter_changes_color'] = True
        report_path.write_text(''.join(report))
        result['report_sha256'] = sha(report_path)
        assert sources() == result['sources_before_build'], 'Sources changed during run'
        assert all(sha(p) == h for p, h in RAW.items()), 'Local shader bytes changed during run'
        # Mip LOD bias (X3M_TAA_MIP_BIAS). Off equals unset byte for byte
        # (colour, evidence, DLL counters); the regular-script twins with the
        # bias on are indistinguishable from their unbiased twins (no
        # mip-mapped texture is bound there) while the DLL reports the bias
        # configured and never set; the mip-bias script's presented colour
        # differs from the unbiased run in every frame (the last routed draw
        # samples finer ramp levels), the seam and production DLLs agree, the
        # lazy RT mode agrees with per-draw, and the evidence delta doubles
        # from -0.5 to -1.0 (the trilinear sample of the ramp is linear in the
        # LOD, so a bias of one level moves it twice as far as half a level).
        mipbias = {}
        off, zero = result['cases']['production-mipbias-off'], result['cases']['production-mipbias-zero']
        for key in ('color_hashes', 'evidence', 'sets_per_frame', 'restores_per_frame', 'reads_per_frame', 'checks', 'verdicts'):
            assert off[key] == zero[key], f'production-mipbias-zero: {key} differs from the unset run'
        files_off, files_zero = readback_files('production-mipbias-off'), readback_files('production-mipbias-zero')
        assert files_off and files_off == files_zero, 'production-mipbias-zero: readback files differ from the unset run'
        mipbias['zero_equals_unset'] = {'identical': True, 'readback_files': len(files_off)}
        for on_name, twin in MIPBIAS_TWINS.items():
            a, b = result['cases'][on_name], result['cases'][twin]
            assert a['color_hashes'] == b['color_hashes'], f'{on_name}: colour differs from {twin}'
            # The biased twin runs the extra per-frame bias check at the bloom
            # copy (39b31d5); everything else has to match its unbiased twin.
            on_case = next(c for c in CASES if c['name'] == on_name)
            bias_checks = 12 if on_case['taa'] and float(on_case['mip_bias'] or 0) != 0 else 0
            assert (a['checks'] - bias_checks, a['restorations'], a['motion_pixels'], a['matched_pixels'], a['depth_written_pixels']) == \
                   (b['checks'], b['restorations'], b['motion_pixels'], b['matched_pixels'], b['depth_written_pixels']), (on_name, twin, bias_checks)
            if 'color_hashes_before_boundary' in b:
                assert a['color_hashes_before_boundary'] == b['color_hashes_before_boundary'], (on_name, twin)
            files_a, files_b = readback_files(on_name), readback_files(twin)
            assert files_a and files_a == files_b, f'{on_name}: readback files differ from {twin}'
            assert a['mip_bias']['bias'] == float(next(c['mip_bias'] for c in CASES if c['name'] == on_name)), on_name
            mipbias.setdefault('twins', {})[on_name] = {'twin': twin, 'identical': True, 'readback_files': len(files_a), 'bias': a['mip_bias']['bias']}
        on, on1, seam_on, lazy_on = (result['cases'][n] for n in ('production-mipbias-on', 'production-mipbias-on1', 'seam-mipbias-on', 'seam-mipbias-lazy-on'))
        differing = [f for f in on['color_hashes'] if on['color_hashes'][f] != off['color_hashes'][f]]
        assert differing == list(range(MIPBIAS_FRAMES)), f'production-mipbias-on: the bias changed the colour of frames {differing} only'
        assert on['color_hashes'] == seam_on['color_hashes'] == lazy_on['color_hashes'], 'mip-bias colour differs between the production DLL, the seam DLL and the lazy RT mode'
        assert on['evidence'] == seam_on['evidence'] == lazy_on['evidence'], 'mip-bias evidence differs between the production DLL, the seam DLL and the lazy RT mode'
        assert on['sets_per_frame'] == seam_on['sets_per_frame'] == lazy_on['sets_per_frame'], 'mip-bias sets differ between the DLLs or the RT modes'
        assert on['restores_per_frame'] == seam_on['restores_per_frame'] == lazy_on['restores_per_frame'], 'mip-bias restores differ between the DLLs or the RT modes'
        # The seam DLL adds the motion/depth oracle checks: compare per DLL.
        assert on['checks'] == on1['checks'] == off['checks'] and seam_on['checks'] == lazy_on['checks'], 'mip-bias runs differ in their check counts'
        ratios = {}
        for frame, e in on['evidence'].items():
            half, full = e['delta'], on1['evidence'][frame]['delta']
            assert half > 1.0 and full > half, (frame, half, full)
            ratios[frame] = full / half
            assert 1.75 <= ratios[frame] <= 2.25, f'frame {frame}: -1.0 moved the ramp sample {ratios[frame]:.3f}x as far as -0.5 (expected 2x)'
        mipbias['evidence'] = {'unbiased': off['evidence'], 'bias_-0.5': on['evidence'], 'bias_-1.0': on1['evidence'], 'delta_ratio_full_over_half': ratios,
                               'frames_changed_by_bias': differing, 'identical_across_dlls_and_rt_modes': True}
        mipbias['counts'] = {'sets_per_frame': on['sets_per_frame'], 'restores_per_frame': on['restores_per_frame'], 'reads_per_frame': on['reads_per_frame'],
                             'anchors': MIPBIAS_ANCHORS, 'capture_frames': list(MIPBIAS_CAPTURE), 'session': on['summary']}
        result['mip_bias'] = mipbias
        assert binary_hashes() == result['binaries'], 'Binaries changed during run'
        result['sources_after_run'] = sources()
        result['limits'] = ['Synthetic device program; not gameplay validation or temporal image quality.',
                            'Jitter is proven by coverage against a CPU reference at the Halton offsets on a 64x64 target.',
                            'The resolve is proven by byte-exact agreement with the same TemporalPass on a plain device from the same inputs, current-only frames by the 8-bit round trip; the reference is not an independent implementation of the resolve.',
                            'Bench timings are CPU-inclusive wall-clock times of the boundary StretchRect with EVENT synchronization on the Preview backend, not GPU timestamps.',
                            'Ownership modes wrap the synthetic device; the game observers stay inactive, so wrapper interaction is proven for fill, routing, Reset and release, not for object history.',
                            'Object scope is injected through the fixture seam; the game observers are not exercised here.',
                            'The engine scene-end hook is exercised on the fixture\'s own callsite through the seam; the game\'s 0x004721b1 patch is verified only for its bytes and identity gate here, not in gameplay.',
                            'The HDR redirect is proven by identical presented frames against the twins, the FP16 value script and the injected-fault ladder on a 64x64 target; the compositor and the HUD of the game are not exercised.',
                            'CrossOver Preview builtin D3D9 only; Windows is cross-compiled, not verified.']
        result['passed'] = True; result['status'] = 'PASS'
    except BaseException as error:
        result['status'] = 'FAIL'; result['error'] = repr(error)
        if 'report' in locals():
            report_path.write_text(''.join(report))  # the fixture output of the cases that ran, for the diagnosis
        raise
    finally:
        save()
        print(json.dumps({k: v for k, v in result.items() if k in ('status', 'passed', 'error')}))


if __name__ == '__main__':
    main()
