#!/usr/bin/env python3
"""Install/remove the owned app-local proxy and launch through CrossOver Preview.

No registry or bottle-wide DLL override is changed. Refuse to overwrite an
unowned d3d9.dll or remove a file whose contents changed after installation.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BOTTLE = os.environ.get('X3M_BOTTLE', 'X3')
GAME = Path.home() / f'Library/Application Support/CrossOver/Bottles/{BOTTLE}/drive_c/X3'
WINE = Path('/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


MARKER = b'X3M_SOURCE_COMMIT='


def dll_source_commit(path):
    """The commit compiled into a built DLL, read from the byte marker
    proxy_identity.cpp embeds; None when the DLL has no marker (an older build)
    or cannot be read."""
    try:
        data = path.read_bytes()
    except OSError:
        return None
    at = data.find(MARKER)
    if at < 0:
        return None
    value = data[at + len(MARKER):data.find(b'\0', at + len(MARKER))]
    try:
        commit = value.decode('ascii')
    except UnicodeDecodeError:
        return None
    return commit or None


def repository_source_commit():
    """Fallback provenance: the repository manage.py itself lives in, HEAD with
    a -dirty suffix when any tracked or untracked build input differs."""
    repository = Path(__file__).resolve().parents[1]

    def query(*arguments):
        return subprocess.run(['git', *arguments], cwd=repository, capture_output=True, text=True, check=True).stdout.strip()
    try:
        head = query('rev-parse', 'HEAD')
        if not head:
            return 'unknown'
        dirty = query('status', '--porcelain', '--untracked-files=normal', '--', 'src', 'cmake', 'tools', 'CMakeLists.txt')
        return head + ('-dirty' if dirty else '')
    except (OSError, subprocess.CalledProcessError):
        return 'unknown'


def source_commit(dll):
    """(commit, origin) for the manifest: the DLL's own compiled-in commit when
    it carries the marker, else this repository's HEAD marked as 'launcher'."""
    commit = dll_source_commit(dll)
    if commit:
        return commit, 'dll'
    return repository_source_commit(), 'launcher'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['install', 'uninstall', 'launch', 'status'])
    parser.add_argument('--game-dir', type=Path, default=GAME)
    parser.add_argument('--bottle', default=BOTTLE, help='CrossOver bottle (default: X3, the arm64/FEX bottle; X3M_BOTTLE overrides; the old x86_64/Rosetta bottle is Steam)')
    parser.add_argument('--dll-source', type=Path, default=ROOT / 'build/d3d9.dll',
                        help='DLL to install (defaults to build/d3d9.dll; other actions do not use it)')
    parser.add_argument('--capture-start', type=int, default=120)
    parser.add_argument('--capture-frames', type=int, choices=range(0, 9), default=1)
    parser.add_argument('--direct', action='store_true', help='Skip launcher and intro using X3 command-line switches')
    parser.add_argument('--vanilla', action='store_true', help='Launch with builtin D3D9, ignoring the installed proxy')
    parser.add_argument('--telemetry', action='store_true', help='Enable bounded loading, presentation and cursor diagnostics')
    parser.add_argument('--game-phases', action='store_true', help='Measure native frame phases and delayed target-lock work (X3M_GAME_PHASES=1; requires --telemetry)')
    parser.add_argument('--audio-sites', action='store_true', help='Load hang witness: add the fourteen byte-verified audio-path markers (media create SetState/Pause returns, message pump entry and drain iterations, the six 0x00498370 manager-update call sites, refill entry, CompletionStatus poll with its HRESULT bucket, Update return, cue play) to the game-phase group (X3M_AUDIO_SITES=1; requires --game-phases); one game_phase_audio line per telemetry window and, with --profile, every 2 s from the sampler thread so the counters stay visible while frames are stopped (docs/architecture/voice-decoder-adapter.md, "Load hang witness build")')
    parser.add_argument('--ownership', action='store_true', help='Enable the experimental normal-D3D9 ownership wrapper')
    parser.add_argument('--depth-copy', action='store_true', help='Enable experimental original-preserving depth copy (requires --ownership)')
    parser.add_argument('--scene-depth-capture', action='store_true', help='Preserve identified scene depth in requested capture frames (requires --ownership --depth-copy)')
    parser.add_argument('--object-trace', action='store_true', help='Capture verified engine submission identity (exact executable only)')
    parser.add_argument('--object-lifetime', action='store_true', help='Observe verified render-registry lifetimes (requires --object-trace --ownership)')
    parser.add_argument('--mesh-cache', action='store_true', help='Enable experimental verified native adjacency reuse (requires --telemetry)')
    parser.add_argument('--mesh-adjacency', choices=['native', 'verify', 'fast'], default='native', help='ID3DXMesh::GenerateAdjacency service (X3M_MESH_ADJACENCY; requires --telemetry): native forwards; verify runs D3DX, recomputes by exact position equality and logs any difference; fast answers from the exact-equality computation and falls through to D3DX on any qualification failure (docs/verification/mesh-adjacency-fast.md)')
    parser.add_argument('--mesh-adjacency-dump', action='store_true', help='With --mesh-adjacency verify: write every mismatching mesh (bounded) as x3-modern-captures/mesh-adjacency-<n>.bin for tools/analysis/replay_mesh_adjacency.py (X3M_MESH_ADJACENCY_DUMP=1; game data, never committed)')
    parser.add_argument('--gz-buffer', action='store_true', help='Read-ahead buffer in front of the zlib gz imports of the savegame decoder (X3M_GZ_BUFFER=1; no --telemetry needed): the ~14 M three-byte gzread calls of a load are served from 256 KB chunks with zlib 1.2.3 semantics kept; one gz_buffer_file line per file in the session log (docs/verification/gz-buffer.md)')
    parser.add_argument('--gz-buffer-kb', type=int, default=256, help='Chunk size in KB of --gz-buffer (X3M_GZ_BUFFER_KB; 1..65536, default 256)')
    parser.add_argument('--crypt-cache', action='store_true', help='CryptoAPI context/key cache in front of the script signature check 0x004cabc0 (X3M_CRYPT_CACHE=1; no --telemetry needed): the per-script CryptAcquireContextA delete/create/delete of the X2EgosoftCSPContainer key container and the CryptImportKey of the constant public key are answered from one cached provider handle and key; hash and signature verification pass through unchanged; one crypt_cache line per telemetry window and at teardown (docs/verification/crypt-cache.md)')
    parser.add_argument('--loading-intervals', action='store_true', help='Retain bounded per-thread loading call intervals until save_load_complete (requires --telemetry; 24 MiB payload)')
    parser.add_argument('--loading-probes', action='store_true', help='Probe batch 2 (X3M_LOADING_PROBES=1; requires --telemetry): light IAT rows on the CryptoAPI, inflateInit2_/inflateEnd, the write-side and per-open KERNEL32 imports, plus entry-counting trampolines on twelve engine loading functions (byte-verified, exact executable only); one loading_probe line per site per report window (docs/verification/loading-probes.md)')
    parser.add_argument('--resource-read', choices=['native', 'verify', 'fast'], default='native', help='Archive reader 0x004e8880 service (X3M_RESOURCE_READ; exact executable only): native leaves the game\'s reader alone; verify runs our whole-extent decode into a scratch buffer, then the original, and logs any difference; fast returns our decode (one fread, word XOR, one inflate, no memset) and falls back to the original on any deviation (docs/verification/resource-reader.md)')
    parser.add_argument('--dat-handles', action='store_true', help='Keep catalogue .dat file handles between resource opens instead of _fopen/_fclose per resource (X3M_DAT_HANDLES=1; exact executable only; docs/reverse-engineering/resource-reader.md)')
    parser.add_argument('--profile', action='store_true', help='Run the in-process sampling profiler (X3M_PROFILE=1): one sampler thread, periodic profile_* reports in the session log; see docs/verification/sampling-profiler.md')
    parser.add_argument('--profile-interval-us', type=int, default=2000, help='Sampling interval in microseconds for --profile (100..1000000, default 2000)')
    parser.add_argument('--profile-raw', action='store_true', help='Load hang witness: with --profile, when a sampled thread\'s EIP resolves to no pinned module, log its raw Eip/Esp/Ebp/SegCs, ContextFlags and 32 stack dwords with module+RVA (profile_raw*, once per thread per report period), plus suspend/context failure codes and per-report failure counts (X3M_PROFILE_RAW=1)')
    parser.add_argument('--finite-positions', action='store_true', help='Validate positions from verified existing buffer uploads (requires --ownership --telemetry)')
    parser.add_argument('--motion-capture', action='store_true', help='Produce private rigid-motion diagnostics during capture (requires scene depth, finite positions and object lifetime)')
    parser.add_argument('--motion-output', action='store_true', help='Route the reviewed material pair through motion-output variants into a private RT1 (history needs --object-trace --object-lifetime; otherwise sentinel-only)')
    parser.add_argument('--motion-jitter', action='store_true', help='Per-draw sub-pixel jitter of every scene draw with a table VS (requires --motion-output; Halton 2,3 sequence, temporal step 1)')
    parser.add_argument('--taa', action='store_true', help='Run the temporal resolve at the bloom copy and present the resolved image (requires --motion-output; implies --motion-jitter; temporal step 3)')
    parser.add_argument('--taa-debug', action='store_true', help='Write the pre-resolve color, the resolved FP16 image and the presented main target (after the sharpen draw / copy-back or the HDR write-back) in capture frames (requires --taa)')
    parser.add_argument('--taa-k', type=float, default=None, help='Fixed k of the resolve luminance weighting on the FP16 scene, 0 = unweighted (X3M_TAA_K; requires --taa and --hdr; default: derived from the write-back exposure)')
    parser.add_argument('--taa-mip-bias', type=float, default=None, help='D3DSAMP_MIPMAPLODBIAS applied to the mip-mapped sampler stages of routed material draws while the TAA jitter is on, restored before every other draw (X3M_TAA_MIP_BIAS; requires --taa; 0 = off; intended value -0.5 for the 4-sample jitter; default: off)')
    parser.add_argument('--taa-sharpen', type=float, default=None, help='Post-resolve sharpen of the presented image, 0..1 (X3M_TAA_SHARPEN; requires --taa): robust contrast-adaptive sharpening of the resolved image only, never of the history; 1 is the strongest setting, 0.5 one stop softer; unset or 0 leaves the output bit-identical to the unsharpened route (docs/architecture/temporal-integration.md, "Post-resolve sharpen")')
    parser.add_argument('--taa-sentinel', choices=['auto', '1', '2'], default='auto', help='Depth-sentinel policy of the resolve (requires --taa): auto reprojects unrouted (background) pixels through the live camera at the far plane whenever the engine camera read yields a transform, 1 keeps them current-only, 2 is strict (skips the resolve on frames without a transform)')
    parser.add_argument('--camera-cut-deg', type=float, default=20.0, help='Camera rotation per frame (degrees) above which the resolve declares a cut (requires --taa; default 20)')
    parser.add_argument('--camera-log', type=int, default=300, help='Cadence in frames of the camera_state log line (requires --taa; capture frames always log; default 300)')
    parser.add_argument('--scene-hook', nargs='?', const='on', default=None, choices=['on', 'off'], help='Engine scene-end hook (X3M_SCENE_HOOK): patch the frame routine\'s compositing callsite (0x004721b1, exact executable and bytes only, otherwise it fails closed to the bloom-copy/selector boundary) so the route learns the scene end from the engine and, with --taa, resolves there before the glow pass. Default on with --motion-output since review 26 (iteration 10: 214/214 agreement); "--scene-hook" alone means on; "--scene-hook off" keeps the copy/selector boundary')
    parser.add_argument('--hdr', action='store_true', help='FP16 HDR scene path (X3M_HDR=1; requires --motion-output): the scene renders into an owned A16B16G16R16F target bound as RT0 at the latching Clear and is written back into the game\'s 8-bit main target at the scene end (--scene-hook, else the bloom copy, else EndScene/Present); fails closed on the capability gate and self test. Without --hdr-tonemap the write-back is the stage-1 identity copy and presented frames equal the non-HDR frames to within one 8-bit code (docs/architecture/hdr-scene-path.md, "Stage 1 implementation")')
    parser.add_argument('--hdr-tonemap', action='store_true', help='AgX write-back of the FP16 scene (X3M_HDR_TONEMAP=agx; requires --hdr), Auto capped at +1.0 EV by default; --hdr-exposure fixed disables frame metering. Ctrl+Shift+F9 compares AUTO and fixed EV0 during play. Default off: identity write-back.')
    parser.add_argument('--linear-distance-fade', action='store_true', default=None, help='Qualify six Asteroid source-over materials in linear light (requires --linear-materials --taa and the material HDR/motion prerequisites; default on with linear materials and TAA, off otherwise; --no-linear-distance-fade disables; full-size composition cost per draw)')
    parser.add_argument('--no-linear-distance-fade', dest='linear_distance_fade', action='store_false', help='Keep the Asteroid source-over materials on the native route even when --linear-materials --taa are on (opt out of the default)')
    parser.add_argument('--fade-witness', type=int, nargs='?', const=30, default=None, metavar='K', help='Diagnostic fade-region witness (X3M_FADE_WITNESS=K; requires --linear-distance-fade or --screen-emission; default off; "--fade-witness" alone means 30): every K-th frame without an admitted emission draw the M coverage target is read back once (GetRenderTargetData to a retained system-memory copy) and the covered pixels outside the union of that frame\'s derived fade rectangles are counted; one fade_witness line per K-th frame plus that frame\'s per-DIP fade_region lines (first 64, with a truncated count) in the session log, validated by verification/probe/run_linear_distance_fade_live.py (docs/architecture/linear-distance-fade-region.md, step 1)')
    parser.add_argument('--sun-shadow-lane', action='store_true', help='Diagnostic sun-share RT2 lane only; applies no shadows (default off; requires --motion-output --taa --hdr --linear-materials).')
    parser.add_argument('--shadow-replay-candidates', action='store_true', help='Lane-independent caster-candidate counter of the motion route (X3M_SHADOW_REPLAY_CANDIDATES=1; requires --motion-output --ownership only, works with original hull shading; default off): one shadow_replay_candidates line per scene end and at most 16 shadow_replay_lock_witness lines per device; integer bookkeeping per routed draw, no allocation, no shadows (docs/architecture/shadow-replay-gates.md, "Implemented")')
    parser.add_argument('--ambient-occlusion', action='store_true', help='Half-resolution GTAO at the scene-end hook, multiplied into the scene target before the temporal resolve (X3M_AMBIENT_OCCLUSION=1; requires --motion-output --taa; default off). Ctrl+Shift+F11 toggles the chain off/on during play for a same-scene comparison (one ambient_occlusion_toggle log line per press; the pass stays attached). docs/architecture/ambient-occlusion.md, "Step 2"')
    parser.add_argument('--ao-radius', type=float, default=None, metavar='METRES', help='Ambient occlusion world radius in metres, 0.1..100, default 2 (X3M_AO_RADIUS; requires --ambient-occlusion; view units are 0.2 m, the calibration is tunable because the view-unit check is inconclusive)')
    parser.add_argument('--ao-strength', type=float, default=None, help='Ambient occlusion strength s of the factor 1 - s (1 - ao), 0..1, default 0.5 (X3M_AO_STRENGTH; requires --ambient-occlusion)')
    parser.add_argument('--ao-debug', action='store_true', help='Ambient occlusion debug view: the factor is written as grayscale instead of multiplied, and the per-frame timing line is on (X3M_AO_DEBUG=1; requires --ambient-occlusion)')
    parser.add_argument('--ao-timing', action='store_true', help='One ambient_occlusion_frame log line per frame with GPU timestamp and CPU wall time of the chain (X3M_AO_TIMING=1; requires --ambient-occlusion; default off)')
    parser.add_argument('--shimmer-trace', action='store_true', help='Diagnostic distant-shimmer trace (X3M_SHIMMER_TRACE=1; requires --motion-output --taa; default off): every frame logs one shimmer_frame line with the TAA state (history, skip, cut, jitter index) and the projection p00/p11 as integers scaled by 1e4, plus up to 32 shimmer_draw lines identifying that frame\'s Asteroid-class scene draws (node/model/lod, vertex, index and primitive counts, the distance-fade f in per mille when the draw was fade-admitted and its derived screen rectangle) with a truncated count beyond 32 (docs/architecture/linear-distance-fade-region.md, "Shimmer trace (diagnostic)")')
    parser.add_argument('--screen-emission', action='store_true', help='Packed screen emission of the bullet draws inside the region bracket (X3M_SCREEN_EMISSION=1, which also sets X3M_SCREEN_EMISSION_BOUND=1; requires --taa --motion-output --ownership --hdr --hdr-tonemap and gamma2.2 decode, with or without --linear-materials; default off): the nine SM1 screen pairs drawn in the native ONE/INVSRCCOLOR state with a locked-prefix bound compose through policy 8 in place; unbounded, unknown-state, capability-refused or otherwise refused draws stay native (docs/architecture/screen-emission-region.md, step C)')
    parser.add_argument('--screen-emission-additive', type=float, default=None, metavar='G', help='Additive bullets (X3M_SCREEN_EMISSION_ADDITIVE=G, finite 1..8; requires --motion-output --hdr; mutually exclusive with --screen-emission; default off): the nine SM1 screen pairs drawn in the native ONE/INVSRCCOLOR state draw in place with DESTBLEND ONE and their colour multiplied by G (G=1 binds the original shader), so the FP16 scene accumulates G*q + D above 1.0 for exposure and bloom; no bracket, bound, copies or temporal work; the blend law changes and native parity is not kept (docs/architecture/screen-emission-region.md, "Additive option")')
    parser.add_argument('--screen-emission-timing', action='store_true', help='Per-frame timing diagnostic of the screen-emission option (X3M_SCREEN_EMISSION_TIMING=1; requires --screen-emission; default off): one screen_emission_frame line per Present with that frame\'s packed_admitted, brackets_px and cpu_us (the wall-clock QueryPerformanceCounter delta since the previous Present). The option itself logs nothing per frame (docs/architecture/screen-emission-region.md, step C)')
    parser.add_argument('--screen-emission-gain', type=float, default=None, metavar='G', help='Step E gain of the packed screen composition, finite 0.5..8, default 1 (X3M_SCREEN_EMISSION_GAIN; requires --screen-emission): the composed bullet is decode(native after) - decode(native before) scaled by G on the decoded scene, so 1 presents the native bolt exactly and larger values lift it into HDR for bloom and exposure (docs/architecture/screen-emission-region.md, step E)')
    parser.add_argument('--linear-emissions', action='store_true', help='Compose reviewed additive scene emissions in linear light (requires --motion-output --taa --hdr --hdr-tonemap and gamma2.2 decode; default off)')
    parser.add_argument('--emission-gain', type=float, default=None, help='Linear emission gain, finite 0..16, default 1 (requires --linear-emissions)')
    parser.add_argument('--emission-source-gain', type=float, default=None, metavar='G', help='Source-only encoded gain of the twenty additive engine/effects emission pairs, finite 1..8, default 1 = off (X3M_EMISSION_SOURCE_GAIN; requires --hdr; excludes --linear-emissions, whose bracket carries its own --emission-gain; needs neither --linear-materials nor --taa): the pixel program of each pair multiplies its native colour output by G before the game\'s own ADD/ONE/ONE blend into the FP16 scene, so bloom and exposure pick the brighter emitters up; alpha, blend state and draw order stay native, and a pair drawn through any other blend stays native. Gain 1 creates no variant and is byte-identical to a build without the option (docs/architecture/linear-emission-cost.md, "Implemented")')
    parser.add_argument('--linear-materials', action='store_true', help='Evaluate the reviewed hull-material pairs in linear space, preserving motion and compatibility-encoding into FP16 (requires --motion-output --hdr --hdr-tonemap and gamma2.2 decode; default off)')
    parser.add_argument('--material-direct-gain', type=float, default=None, help='Linear direct-light gain, finite 0..16, default 1 (requires --linear-materials)')
    parser.add_argument('--material-emissive-gain', type=float, default=None, help='Linear scaled material-emissive gain, finite 0..16, default 1 (requires --linear-materials)')
    parser.add_argument('--lightmap-emissive-gain', type=float, default=None, help='Linear lightmap-emissive gain, finite 0..16, default 1 (requires --linear-materials)')
    parser.add_argument('--material-fill', type=float, default=None, metavar='K', help='Constant hemispherical fill inside the converted material law, finite 0..0.5, default 0.03 with --linear-materials (X3M_MATERIAL_FILL; requires --linear-materials): every converted pixel program adds k*decode(LightDir_Color0)*g_direct to its lobe sum before the albedo multiply, so faces that face no light source keep a floor tinted by the sector sun. Explicit 0 disables fill and keeps the generated programs byte-identical to a build without the option (docs/architecture/fill-light.md)')
    parser.add_argument('--hdr-look', choices=['none', 'golden', 'punchy'], default='none', help='AgX look (X3M_HDR_LOOK; requires --hdr-tonemap; default none)')
    parser.add_argument('--hdr-bloom', action='store_true', help='Replace stock bloom RGB with bloom from the FP16 scene before AgX (X3M_HDR_BLOOM=1; requires --hdr-tonemap and scene hook; default off)')
    parser.add_argument('--hdr-decode', choices=['gamma2.2', 'pow22', 'srgb', 'none'], default='gamma2.2', help='Engine-space decode before the tonemap and the meter (X3M_HDR_DECODE; requires --hdr-tonemap): gamma2.2 (default; pow22 is the same curve), srgb, or none for the A/B against the decoded transform')
    parser.add_argument('--hdr-ev', type=float, default=0.0, help='Exposure offset in EV added to the auto-exposure target (X3M_HDR_EV; requires --hdr-tonemap; default 0)')
    parser.add_argument('--hdr-exposure', choices=['fixed', 'auto'], default=None, help='Exposure policy (X3M_HDR_EXPOSURE; requires --hdr-tonemap when explicit): Auto capped at +1.0 EV by default, or fixed EV0; Ctrl+Shift+F9 switches during play')
    parser.add_argument('--hdr-ev-manual', type=float, default=None, help='Fixed EV instead of auto exposure (X3M_HDR_EV_MANUAL; requires --hdr-tonemap): the meter chain does not run; deterministic; clamped to the EV range (--hdr-ev-min/--hdr-ev-max, -3..+1.0 by default)')
    parser.add_argument('--hdr-ev-min', type=float, default=-3.0, help='Lower clamp of the auto-exposure EV (X3M_HDR_EV_MIN; requires --hdr-tonemap; default -3)')
    parser.add_argument('--hdr-ev-max', type=float, default=1.0, help='Upper clamp of exposure EV (X3M_HDR_EV_MAX; requires --hdr-tonemap; default 1.0)')
    parser.add_argument('--hdr-meter-bg', type=float, default=1.0 / 512.0, help='Tile background floor of the meter in scene-linear units (X3M_HDR_METER_BG; requires --hdr-tonemap): tiles whose geometric-mean luminance is below it are the black sky and excluded from the key rule; default 1/512')
    parser.add_argument('--hdr-white-target', type=float, default=0.9, help='Fraction of the tonemapper\'s white the brightest 1 %% of tiles may reach, the highlight limit of the auto exposure (X3M_HDR_WHITE_TARGET; requires --hdr-tonemap; default 0.9; 0 = no limit)')
    parser.add_argument('--hdr-ev-deadband', type=float, default=0.25, help='Dead band of the auto exposure in EV (X3M_HDR_EV_DEADBAND; requires --hdr-tonemap): the held target moves only when the freshly metered target differs from it by more, so small scene changes while turning do not drift the exposure; default 0.25; 0 = off')
    parser.add_argument('--hdr-edge-weight', type=float, default=0.35, help='Centre weighting of the lit-content statistic (X3M_HDR_METER_EDGE_WEIGHT; requires --hdr-tonemap): the tile weight at the frame corners, 1 at the centre (a raised cosine), so a bright emitter at the edge does not darken what the player looks at; the highlight limit stays unweighted; default 0.35; 1 = unweighted')
    parser.add_argument('--hdr-key-pull', type=float, default=0.25, help='Fraction of the key rule applied when the lit median is brighter than the key, so a bright full frame is pulled down gently instead of to mid-grey (X3M_HDR_KEY_PULL; requires --hdr-tonemap; default 0.25; 1 = the full key rule both ways)')
    parser.add_argument('--hdr-clamp', type=float, default=0.0, help='Clamp of the decoded scene value before the tonemap, the blunt firefly guard (X3M_HDR_CLAMP; requires --hdr-tonemap; default 0 = off)')
    parser.add_argument('--state-shadow', choices=['on', 'off'], default='on', help='Render-state shadow of the route (X3M_STATE_SHADOW): on (default) hooks SetRenderState and answers the per-draw state queries from the shadow; off issues GetRenderState per query (A/B; requires --motion-output)')
    parser.add_argument('--motion-rt-mode', choices=['perdraw', 'lazy'], default='perdraw', help='RT1/RT2 binding policy of the route: perdraw (default) rebinds around every routed draw; lazy keeps the bindings across consecutive routed draws (A/B experiment, requires --motion-output)')
    parser.add_argument('--camera', choices=['vanilla', 'chase'], default='vanilla', help='External back view camera (X3M_CAMERA): vanilla (default) patches nothing; chase installs the byte-verified cockpit-update trampoline (0x00420e06, exact executable only, fails closed to vanilla) and replaces the external back view with the critically damped chase camera; internal/front/side views stay vanilla, so the game\'s view keys remain the switch (docs/architecture/chase-camera.md)')
    # Chase tunables are X3M_CHASE_* (review 31 O7): X3M_CAMERA_CUT_DEG / X3M_CAMERA_LOG above belong to the TAA camera read.
    parser.add_argument('--chase-rot-tau', type=float, default=None, help='Chase camera orientation spring time constant in seconds (X3M_CHASE_ROT_TAU; default 0.28; requires --camera chase)')
    parser.add_argument('--chase-pos-tau', type=float, default=None, help='Chase camera boom spring time constant in seconds (X3M_CHASE_POS_TAU; default 0.38)')
    parser.add_argument('--chase-offset-y', type=float, default=None, help='Fraction of the half screen height the ship sits below centre, -1..1 (X3M_CHASE_OFFSET_Y; default 0.45, about 72.5%% screen height from a centred native anchor; negative puts the ship above centre)')
    parser.add_argument('--chase-pitch-down-deg', type=float, default=None, help='Downward look in degrees relative to ship forward, 0..30 (X3M_CHASE_PITCH_DOWN_DEG; default 13; 0 restores legacy framing geometry)')
    parser.add_argument('--chase-distance-scale', type=float, default=None, help='Multiplier of the vanilla boom length (X3M_CHASE_DISTANCE_SCALE; default 0.9)')
    parser.add_argument('--chase-lag-clamp-deg', type=float, default=None, help='Maximum orientation lag in degrees, the ship-on-screen window (X3M_CHASE_LAG_CLAMP_DEG; default 8)')
    parser.add_argument('--chase-pos-lag-clamp', type=float, default=None, help='Maximum boom lag as a fraction of the boom length, 0..1 (X3M_CHASE_POS_LAG_CLAMP; default 0.10)')
    parser.add_argument('--chase-combat-tightness', type=float, default=None, help='0..1: while the cockpit reports a target lock (+0x1e4 tracking mode 1/4 with a tracked object; unverified in game) both spring time constants are scaled by (1 - tightness) (X3M_CHASE_COMBAT_TIGHTNESS; default 0 = off)')
    parser.add_argument('--chase-scene-fix', action='store_true', help='Also re-express the layer-0 cockpit-scene camera through the smoothed view each applied frame (X3M_CHASE_SCENE_FIX=1; default off until the first run shows an external-view HUD element rendered there; review 31 A5)')
    parser.add_argument('--chase-view-restore', action='store_true', help='After a gate jump or jumpdrive, restore the rear chase view (mode 258) that the engine resets to the internal view: one-use ticket written into the live script assignment at the optimized store seam 0x004a3ffd under the full arm/pending/consume proof, with seven byte-verified cancellation sites (X3M_CHASE_VIEW_RESTORE=1; default off, patches nothing; requires --camera chase; docs/reverse-engineering/chase-view-transition.md)')
    parser.add_argument('--chase-hud-anchor', choices=['forward', 'centre'], default='centre', help='Place the admitted chase HUD group at the ship-forward vanishing point or retain its native centre placement (X3M_CHASE_HUD_ANCHOR; default centre; forward requires --camera chase)')
    parser.add_argument('--voice-decoder', type=Path, default=None, metavar='DIR', help='launch only, opt-in, default off: deliver the process-local WMA decoder plugin built in DIR to this one game process by setting GST_PLUGIN_PATH_1_0=DIR/runtime/plugins and GST_REGISTRY_1_0=DIR/registry/x3-arm64.bin in its environment. Nothing is written into the application, the bottle or any global configuration, no DYLD_LIBRARY_PATH and no unversioned GStreamer variable is touched; only DIR/registry is created if missing. Also sets X3M_VOICE_DMO_FALLBACK=1 so the proxy re-initialises the DMO wrapper the game creates with the registered WMA decoder DMO when the speech decoder class is unregistered (byte-verified hook at 0x004cfd46, inert where Init succeeds; docs/architecture/voice-decoder-adapter.md)')
    parser.add_argument('--lod-scale', type=float, default=None, metavar='FACTOR', help='Push the engine\'s mesh LOD switch distances out by FACTOR, 1..4 (X3M_LOD_SCALE; default absent = vanilla; no other option needed): the LOD threshold multiplier read at 0x0047d44b is replaced by a proxy-owned mirror holding the game\'s value divided by FACTOR (same-length instruction, exact executable and bytes only, otherwise fails closed to vanilla; one lod_scale line in the session log). Cost: about 4-7x the triangles and 13-15x the draw calls per distant station body at 2-3x; the cap of 4 keeps the integer-truncated thresholds away from collapse (docs/architecture/lod-scale.md)')
    parser.add_argument('--dry-run', action='store_true', help='launch only: validate the options and installation, print the command and X3M_* environment as JSON, and exit without launching')
    args = parser.parse_args()
    if args.dry_run and args.action != 'launch':
        parser.error('--dry-run applies to launch only.')
    if args.voice_decoder is not None and args.action != 'launch':
        parser.error('--voice-decoder applies to launch only.')
    if args.depth_copy and not args.ownership:
        parser.error('--depth-copy requires --ownership.')
    if args.scene_depth_capture and not (args.ownership and args.depth_copy):
        parser.error('--scene-depth-capture requires --ownership and --depth-copy.')
    if args.object_lifetime and not (args.object_trace and args.ownership):
        parser.error('--object-lifetime requires --object-trace and --ownership.')
    if args.mesh_cache and not args.telemetry:
        parser.error('--mesh-cache requires --telemetry.')
    if args.loading_intervals and not args.telemetry:
        parser.error('--loading-intervals requires --telemetry (existing loading markers supply the endpoint).')
    if args.loading_probes and not args.telemetry:
        parser.error('--loading-probes requires --telemetry (the probe rows and trampolines are installed by the loading-trace initialization).')
    if args.game_phases and not args.telemetry:
        parser.error('--game-phases requires --telemetry.')
    if args.audio_sites and not args.game_phases:
        parser.error('--audio-sites requires --game-phases.')
    if args.profile_raw and not args.profile:
        parser.error('--profile-raw requires --profile.')
    if args.mesh_adjacency != 'native' and not args.telemetry:
        parser.error('--mesh-adjacency verify|fast requires --telemetry.')
    if args.mesh_adjacency_dump and args.mesh_adjacency != 'verify':
        parser.error('--mesh-adjacency-dump requires --mesh-adjacency verify.')
    if args.finite_positions and not (args.ownership and args.telemetry):
        parser.error('--finite-positions requires --ownership and --telemetry.')
    if args.motion_capture and not (args.scene_depth_capture and args.finite_positions and args.object_lifetime):
        parser.error('--motion-capture requires --scene-depth-capture, --finite-positions and --object-lifetime.')
    if args.motion_output and (args.object_trace != args.object_lifetime):
        parser.error('--motion-output history needs both --object-trace and --object-lifetime, or neither for sentinel-only mode.')
    if args.motion_jitter and not args.motion_output:
        parser.error('--motion-jitter requires --motion-output.')
    if args.taa and not args.motion_output:
        parser.error('--taa requires --motion-output.')
    if args.taa and not (args.object_trace and args.object_lifetime):
        parser.error('--taa requires --object-trace and --object-lifetime: without history every routed draw carries the sentinel, the resolve stays current-only and the jitter only moves the image.')
    if args.taa_debug and not args.taa:
        parser.error('--taa-debug requires --taa.')
    if args.taa_k is not None and not (args.taa and args.hdr):
        parser.error('--taa-k requires --taa and --hdr.')
    if args.taa_k is not None and not 0.0 <= args.taa_k <= 65504.0:
        parser.error('--taa-k must be within [0, 65504].')
    if args.taa_mip_bias is not None and not args.taa:
        parser.error('--taa-mip-bias requires --taa.')
    if args.taa_mip_bias is not None and not -8.0 <= args.taa_mip_bias <= 8.0:
        parser.error('--taa-mip-bias must be within [-8, 8].')
    if args.taa_sharpen is not None and not args.taa:
        parser.error('--taa-sharpen requires --taa.')
    if args.taa_sharpen is not None and not 0.0 <= args.taa_sharpen <= 1.0:
        parser.error('--taa-sharpen must be within [0, 1].')
    if not args.taa and (args.taa_sentinel != 'auto' or args.camera_cut_deg != 20.0 or args.camera_log != 300):
        parser.error('--taa-sentinel, --camera-cut-deg and --camera-log require --taa.')
    if not 0 < args.camera_cut_deg <= 180 or not 1 <= args.camera_log <= 1000000:
        parser.error('--camera-cut-deg must be in (0, 180] and --camera-log in [1, 1000000].')
    if args.motion_rt_mode != 'perdraw' and not args.motion_output:
        parser.error('--motion-rt-mode requires --motion-output.')
    if args.scene_hook == 'on' and not args.motion_output:
        parser.error('--scene-hook requires --motion-output.')
    if args.hdr and not args.motion_output:
        parser.error('--hdr requires --motion-output.')
    if args.hdr_tonemap and not args.hdr:
        parser.error('--hdr-tonemap requires --hdr.')
    if args.linear_distance_fade is None:
        # Default on where its prerequisites hold (user decision 2026-09-14,
        # runs 11/14/15); silently off otherwise, so an unrelated launch does
        # not have to name --no-linear-distance-fade.
        args.linear_distance_fade = bool(args.linear_materials and args.taa)
    elif args.linear_distance_fade and (not args.linear_materials or not args.taa):
        parser.error('--linear-distance-fade requires --linear-materials --taa (and material HDR/motion prerequisites).')
    if args.fade_witness is not None and not (args.linear_distance_fade or args.screen_emission):
        parser.error('--fade-witness requires --linear-distance-fade or --screen-emission.')
    if args.shimmer_trace and not (args.motion_output and args.taa):
        parser.error('--shimmer-trace requires --motion-output --taa.')
    if args.sun_shadow_lane and not (args.motion_output and args.taa and args.hdr and args.linear_materials):
        parser.error('--sun-shadow-lane requires --motion-output --taa --hdr --linear-materials.')
    if args.shadow_replay_candidates and not (args.motion_output and args.ownership):
        parser.error('--shadow-replay-candidates requires --motion-output --ownership.')
    if args.ambient_occlusion and not (args.motion_output and args.taa):
        parser.error('--ambient-occlusion requires --motion-output --taa.')
    if not args.ambient_occlusion and (args.ao_radius is not None or args.ao_strength is not None or args.ao_debug or args.ao_timing):
        parser.error('--ao-radius, --ao-strength, --ao-debug and --ao-timing require --ambient-occlusion.')
    if args.ao_radius is not None and not (math.isfinite(args.ao_radius) and 0.1 <= args.ao_radius <= 100.0):
        parser.error('--ao-radius must be within [0.1, 100].')
    if args.ao_strength is not None and not (math.isfinite(args.ao_strength) and 0.0 <= args.ao_strength <= 1.0):
        parser.error('--ao-strength must be within [0, 1].')
    if args.fade_witness is not None and not 1 <= args.fade_witness <= 100000:
        parser.error('--fade-witness must be within [1,100000].')
    # The packed screen bracket composes on the FP16 scene the AgX/gamma2.2 HDR
    # pass owns; it consumes no linear-material variant or admission state
    # (docs/architecture/linear-material-decoupling.md), so hull shading is free.
    if args.screen_emission and not (args.taa and args.motion_output and args.ownership and args.hdr and args.hdr_tonemap and args.hdr_decode in ('gamma2.2', 'pow22')):
        parser.error('--screen-emission requires --taa --motion-output --ownership --hdr --hdr-tonemap and gamma2.2 decode.')
    if args.screen_emission_timing and not args.screen_emission:
        parser.error('--screen-emission-timing requires --screen-emission.')
    if args.screen_emission_additive is not None and args.screen_emission:
        parser.error('--screen-emission-additive is mutually exclusive with --screen-emission.')
    if args.screen_emission_additive is not None and not (args.motion_output and args.hdr):
        parser.error('--screen-emission-additive requires --motion-output --hdr.')
    if args.screen_emission_additive is not None and not (math.isfinite(args.screen_emission_additive) and 1.0 <= args.screen_emission_additive <= 8.0):
        parser.error('--screen-emission-additive must be finite and within [1, 8].')
    if args.screen_emission_gain is not None and not args.screen_emission:
        parser.error('--screen-emission-gain requires --screen-emission.')
    if args.screen_emission_gain is not None and not (math.isfinite(args.screen_emission_gain) and 0.5 <= args.screen_emission_gain <= 8.0):
        parser.error('--screen-emission-gain must be finite and within [0.5, 8].')
    if args.linear_emissions and (not args.motion_output or not args.taa or not args.hdr or not args.hdr_tonemap or args.hdr_decode not in ('gamma2.2', 'pow22')):
        parser.error('--linear-emissions requires --motion-output --taa --hdr --hdr-tonemap and gamma2.2 decode.')
    if args.emission_gain is not None and not args.linear_emissions:
        parser.error('--emission-gain requires --linear-emissions.')
    if args.emission_gain is not None and (not math.isfinite(args.emission_gain) or not 0 <= args.emission_gain <= 16):
        parser.error('--emission-gain must be finite and within [0,16].')
    if args.emission_source_gain is not None and not args.hdr:
        parser.error('--emission-source-gain requires --hdr.')
    if args.emission_source_gain is not None and args.linear_emissions:
        parser.error('--emission-source-gain excludes --linear-emissions (use --emission-gain inside the linear route).')
    if args.emission_source_gain is not None and not (math.isfinite(args.emission_source_gain) and 1.0 <= args.emission_source_gain <= 8.0):
        parser.error('--emission-source-gain must be finite and within [1, 8].')
    if args.linear_materials and (not args.motion_output or not args.hdr or not args.hdr_tonemap or args.hdr_decode not in ('gamma2.2', 'pow22')):
        parser.error('--linear-materials requires --motion-output --hdr --hdr-tonemap and gamma2.2 decode.')
    material_gains = {'X3M_MATERIAL_DIRECT_GAIN': args.material_direct_gain, 'X3M_MATERIAL_EMISSIVE_GAIN': args.material_emissive_gain,
                      'X3M_LIGHTMAP_EMISSIVE_GAIN': args.lightmap_emissive_gain}
    if any(value is not None for value in material_gains.values()) and not args.linear_materials:
        parser.error('Material gains require --linear-materials.')
    if any(value is not None and not 0.0 <= value <= 16.0 for value in material_gains.values()):
        parser.error('Material gains must be finite and within [0, 16].')
    if args.material_fill is not None and not args.linear_materials:
        parser.error('--material-fill requires --linear-materials.')
    if args.material_fill is not None and not (math.isfinite(args.material_fill) and 0.0 <= args.material_fill <= 0.5):
        parser.error('--material-fill must be finite and within [0, 0.5].')
    if args.hdr_bloom and (not args.hdr_tonemap or args.scene_hook == 'off'):
        parser.error('--hdr-bloom requires --hdr-tonemap and the scene hook.')
    if not args.hdr_tonemap and (args.hdr_exposure is not None or args.hdr_look != 'none' or args.hdr_decode != 'gamma2.2' or args.hdr_ev != 0.0 or args.hdr_ev_manual is not None or args.hdr_clamp != 0.0
                                 or args.hdr_ev_min != -3.0 or args.hdr_ev_max != 1.0 or args.hdr_meter_bg != 1.0 / 512.0 or args.hdr_white_target != 0.9 or args.hdr_key_pull != 0.25
                                 or args.hdr_ev_deadband != 0.25 or args.hdr_edge_weight != 0.35):
        parser.error('--hdr-exposure, --hdr-look, --hdr-decode, --hdr-ev, --hdr-ev-manual, --hdr-clamp, --hdr-ev-min/max, --hdr-meter-bg, --hdr-white-target, --hdr-key-pull, --hdr-ev-deadband and --hdr-edge-weight require --hdr-tonemap.')
    if not -16.0 <= args.hdr_ev <= 16.0 or (args.hdr_ev_manual is not None and not -16.0 <= args.hdr_ev_manual <= 16.0) or not 0.0 <= args.hdr_clamp <= 65504.0:
        parser.error('--hdr-ev and --hdr-ev-manual must be within [-16, 16], --hdr-clamp within [0, 65504].')
    if not -16.0 <= args.hdr_ev_min <= args.hdr_ev_max <= 16.0 or not 1e-4 <= args.hdr_meter_bg <= 64.0 or not 0.0 <= args.hdr_white_target <= 4.0 or not 0.0 <= args.hdr_key_pull <= 1.0 \
            or not 0.0 <= args.hdr_ev_deadband <= 8.0 or not 0.0 <= args.hdr_edge_weight <= 1.0:
        parser.error('--hdr-ev-min <= --hdr-ev-max within [-16, 16], --hdr-meter-bg within [1e-4, 64], --hdr-white-target within [0, 4], --hdr-key-pull, --hdr-edge-weight within [0, 1], --hdr-ev-deadband within [0, 8].')
    if args.state_shadow != 'on' and not args.motion_output:
        parser.error('--state-shadow requires --motion-output.')
    chase_tunables = {'X3M_CHASE_ROT_TAU': args.chase_rot_tau, 'X3M_CHASE_POS_TAU': args.chase_pos_tau, 'X3M_CHASE_OFFSET_Y': args.chase_offset_y,
                      'X3M_CHASE_PITCH_DOWN_DEG': args.chase_pitch_down_deg, 'X3M_CHASE_DISTANCE_SCALE': args.chase_distance_scale, 'X3M_CHASE_LAG_CLAMP_DEG': args.chase_lag_clamp_deg,
                      'X3M_CHASE_POS_LAG_CLAMP': args.chase_pos_lag_clamp, 'X3M_CHASE_COMBAT_TIGHTNESS': args.chase_combat_tightness}
    if args.camera != 'chase' and (args.chase_scene_fix or args.chase_view_restore or args.chase_hud_anchor != 'centre' or any(v is not None for v in chase_tunables.values())):
        parser.error('--chase-rot-tau, --chase-pos-tau, --chase-offset-y, --chase-pitch-down-deg, --chase-distance-scale, --chase-lag-clamp-deg, --chase-pos-lag-clamp, --chase-combat-tightness, --chase-scene-fix, --chase-view-restore and --chase-hud-anchor forward require --camera chase.')
    # The same ranges chase::valid() enforces in the DLL (docs/architecture/
    # chase-camera.md, "Tunables"); offset_y is signed (negative puts the ship
    # above centre). A NaN fails every comparison and is refused here too.
    chase_ranges = {'X3M_CHASE_ROT_TAU': (0.0, 10.0, False), 'X3M_CHASE_POS_TAU': (0.0, 10.0, False),
                    'X3M_CHASE_PITCH_DOWN_DEG': (0.0, 30.0, True), 'X3M_CHASE_OFFSET_Y': (-1.0, 1.0, True), 'X3M_CHASE_DISTANCE_SCALE': (0.0, 10.0, False),
                    'X3M_CHASE_LAG_CLAMP_DEG': (0.0, 90.0, True), 'X3M_CHASE_POS_LAG_CLAMP': (0.0, 1.0, True),
                    'X3M_CHASE_COMBAT_TIGHTNESS': (0.0, 1.0, True)}
    for name, value in chase_tunables.items():
        low, high, low_inclusive = chase_ranges[name]
        if value is not None and not ((low <= value if low_inclusive else low < value) and value <= high):
            parser.error(f'{name} out of range: {value} (expected {"[" if low_inclusive else "("}{low}, {high}])')
    if args.lod_scale is not None and not (math.isfinite(args.lod_scale) and 1.0 <= args.lod_scale <= 4.0):
        parser.error(f'--lod-scale out of range: {args.lod_scale} (expected [1.0, 4.0])')
    if not 100 <= args.profile_interval_us <= 1000000:
        parser.error('--profile-interval-us must be between 100 and 1000000.')
    if args.gz_buffer_kb != 256 and not args.gz_buffer:
        parser.error('--gz-buffer-kb requires --gz-buffer.')
    if args.loading_intervals and not args.telemetry:
        parser.error('--loading-intervals requires --telemetry (existing loading markers supply the endpoint).')
    if args.loading_probes and not args.telemetry:
        parser.error('--loading-probes requires --telemetry.')
    if not 1 <= args.gz_buffer_kb <= 65536:
        parser.error('--gz-buffer-kb must be between 1 and 65536.')
    if args.taa:
        args.motion_jitter = True
    if args.motion_capture and args.capture_frames < 2:
        parser.error('--motion-capture requires --capture-frames between 2 and 8 for adjacent-frame correspondence.')
    game = args.game_dir.resolve()
    dll = game / 'd3d9.dll'
    manifest = game / 'x3-modern-install.json'
    if not (game / 'X3AP.exe').is_file():
        parser.error(f'X3AP.exe not found in {game}')
    owned = json.loads(manifest.read_text()) if manifest.exists() else None
    if args.action == 'status':
        # Session logs: next to the DLL, or the proxy's fallback when that
        # directory is not writable (the first log line says which was taken).
        print(json.dumps({'game': str(game), 'dll_present': dll.exists(),
                          'owned': bool(owned and dll.exists() and digest(dll) == owned['sha256']),
                          'installation': owned,
                          'log_directories': {'game': str(game / 'x3-modern-captures'),
                                              'fallback': r'%LOCALAPPDATA%\x3-modern-renderer\captures (read-only game directory)'}}, indent=2))
        return
    if args.action == 'install':
        source = args.dll_source.resolve()
        if not source.is_file():
            parser.error('Build the DLL first (see README.md).')
        if dll.exists() and (not owned or digest(dll) != owned['sha256']):
            parser.error('Existing d3d9.dll is unowned or changed; refusing to overwrite it.')
        commit, origin = source_commit(source)
        temp = game / 'x3-modern-install.tmp'
        shutil.copy2(source, temp)
        os.replace(temp, dll)
        manifest.write_text(json.dumps({'project': 'x3-modern-renderer', 'sha256': digest(dll),
                                        'source': str(source), 'source_commit': commit,
                                        'manifest_source': origin}, indent=2) + '\n')
        print(f'Installed {dll}; bottle configuration unchanged.')
    elif args.action == 'uninstall':
        if not owned:
            parser.error('No installation manifest; refusing to remove an unowned file.')
        if dll.exists() and digest(dll) != owned['sha256']:
            parser.error('Installed DLL changed; refusing to remove it.')
        if dll.exists():
            dll.unlink()
        manifest.unlink()
        print('Removed owned proxy and manifest; captures retained.')
    elif args.action == 'launch':
        if not WINE.is_file():
            parser.error(f'CrossOver Preview Wine not found: {WINE}')
        if not args.vanilla and (not owned or not dll.exists() or digest(dll) != owned['sha256']):
            parser.error('Install the proxy before launch, or use --vanilla.')
        env = os.environ.copy()
        env['X3M_CAPTURE_START'] = str(max(1, args.capture_start))
        env['X3M_CAPTURE_FRAMES'] = str(args.capture_frames)
        env['X3M_TELEMETRY'] = '1' if args.telemetry else '0'
        env['X3M_GAME_PHASES'] = '1' if args.game_phases else '0'
        env['X3M_OWNERSHIP'] = '1' if args.ownership else '0'
        env['X3M_DEPTH_COPY'] = '1' if args.depth_copy else '0'
        env['X3M_SCENE_DEPTH_CAPTURE'] = '1' if args.scene_depth_capture else '0'
        env['X3M_OBJECT_TRACE'] = '1' if args.object_trace else '0'
        env['X3M_OBJECT_LIFETIME'] = '1' if args.object_lifetime else '0'
        env['X3M_MESH_CACHE'] = '1' if args.mesh_cache else '0'
        env['X3M_MESH_ADJACENCY'] = args.mesh_adjacency
        env['X3M_MESH_ADJACENCY_DUMP'] = '1' if args.mesh_adjacency_dump else '0'
        env['X3M_FINITE_POSITIONS'] = '1' if args.finite_positions else '0'
        env['X3M_MOTION_CAPTURE'] = '1' if args.motion_capture else '0'
        env['X3M_MOTION_OUTPUT'] = '1' if args.motion_output else '0'
        env['X3M_MOTION_JITTER'] = '1' if args.motion_jitter else '0'
        env['X3M_TAA'] = '1' if args.taa else '0'
        env['X3M_TAA_DEBUG'] = '1' if args.taa_debug else '0'
        if args.taa_k is not None:
            env['X3M_TAA_K'] = repr(args.taa_k)
        if args.taa_mip_bias is not None:
            env['X3M_TAA_MIP_BIAS'] = repr(args.taa_mip_bias)
        if args.taa_sharpen is not None:
            env['X3M_TAA_SHARPEN'] = repr(args.taa_sharpen)
        env['X3M_TAA_SENTINEL'] = args.taa_sentinel
        env['X3M_CAMERA_CUT_DEG'] = repr(args.camera_cut_deg)
        env['X3M_CAMERA_LOG'] = str(args.camera_log)
        env['X3M_MOTION_RT_MODE'] = args.motion_rt_mode
        env['X3M_SCENE_HOOK'] = '0' if args.scene_hook == 'off' or not args.motion_output else '1'
        env['X3M_HDR'] = '1' if args.hdr else '0'
        env['X3M_LINEAR_EMISSIONS'] = '1' if args.linear_emissions else '0'
        env['X3M_LINEAR_DISTANCE_FADE'] = '1' if args.linear_distance_fade else '0'
        # Step C screen emission implies the step B bound; both explicit so a
        # stale shell value cannot enable either.
        env['X3M_SCREEN_EMISSION'] = '1' if args.screen_emission else '0'
        env['X3M_SCREEN_EMISSION_BOUND'] = '1' if args.screen_emission else '0'
        env['X3M_SCREEN_EMISSION_TIMING'] = '1' if args.screen_emission_timing else '0'
        env['X3M_SCREEN_EMISSION_GAIN'] = repr(args.screen_emission_gain if args.screen_emission_gain is not None else 1.0)
        # Explicit off value so a stale shell value cannot enable the additive option.
        env['X3M_SCREEN_EMISSION_ADDITIVE'] = repr(args.screen_emission_additive) if args.screen_emission_additive is not None else '0'
        if args.fade_witness is not None:
            env['X3M_FADE_WITNESS'] = str(args.fade_witness)
        if args.shimmer_trace:
            env['X3M_SHIMMER_TRACE'] = '1'
        # Ambient occlusion: every switch explicit so an inherited value cannot enable it.
        env['X3M_SUN_SHADOW_LANE'] = '1' if args.sun_shadow_lane else '0'
        env['X3M_SHADOW_REPLAY_CANDIDATES'] = '1' if args.shadow_replay_candidates else '0'
        env['X3M_AMBIENT_OCCLUSION'] = '1' if args.ambient_occlusion else '0'
        env['X3M_AO_RADIUS'] = repr(args.ao_radius if args.ao_radius is not None else 2.0)
        env['X3M_AO_STRENGTH'] = repr(args.ao_strength if args.ao_strength is not None else 0.5)
        env['X3M_AO_DEBUG'] = '1' if args.ao_debug else '0'
        env['X3M_AO_TIMING'] = '1' if args.ao_timing else '0'
        env['X3M_EMISSION_GAIN'] = repr(args.emission_gain if args.emission_gain is not None else 1.0)
        env['X3M_EMISSION_SOURCE_GAIN'] = repr(args.emission_source_gain if args.emission_source_gain is not None else 1.0)
        env['X3M_LINEAR_MATERIALS'] = '1' if args.linear_materials else '0'
        for name, value in material_gains.items():
            env[name] = str(value if value is not None else 1.0)
        env['X3M_MATERIAL_FILL'] = repr(args.material_fill if args.material_fill is not None else (0.03 if args.linear_materials else 0.0))
        env['X3M_HDR_TONEMAP'] = 'agx' if args.hdr_tonemap else 'identity'
        env['X3M_HDR_BLOOM'] = '1' if args.hdr_bloom else '0'
        env['X3M_HDR_LOOK'] = args.hdr_look
        env['X3M_HDR_DECODE'] = args.hdr_decode
        env['X3M_HDR_EV'] = repr(args.hdr_ev)
        env['X3M_HDR_EXPOSURE'] = 'fixed' if args.hdr_ev_manual is not None else (args.hdr_exposure or 'auto')
        env['X3M_HDR_EV_MANUAL'] = '' if args.hdr_ev_manual is None else repr(args.hdr_ev_manual)
        env['X3M_HDR_CLAMP'] = repr(args.hdr_clamp)
        env['X3M_HDR_EV_MIN'] = repr(args.hdr_ev_min)
        env['X3M_HDR_EV_MAX'] = repr(args.hdr_ev_max)
        env['X3M_HDR_METER_BG'] = repr(args.hdr_meter_bg)
        env['X3M_HDR_WHITE_TARGET'] = repr(args.hdr_white_target)
        env['X3M_HDR_KEY_PULL'] = repr(args.hdr_key_pull)
        env['X3M_HDR_EV_DEADBAND'] = repr(args.hdr_ev_deadband)
        env['X3M_HDR_METER_EDGE_WEIGHT'] = repr(args.hdr_edge_weight)
        env['X3M_STATE_SHADOW'] = '1' if args.state_shadow == 'on' else '0'
        env['X3M_GZ_BUFFER'] = '1' if args.gz_buffer else '0'
        env['X3M_GZ_BUFFER_KB'] = str(args.gz_buffer_kb)
        env['X3M_LOADING_INTERVALS'] = '1' if args.loading_intervals else '0'
        env['X3M_LOADING_PROBES'] = '1' if args.loading_probes else '0'
        env['X3M_CRYPT_CACHE'] = '1' if args.crypt_cache else '0'
        env['X3M_RESOURCE_READ'] = args.resource_read
        env['X3M_DAT_HANDLES'] = '1' if args.dat_handles else '0'
        env['X3M_PROFILE'] = '1' if args.profile else '0'
        env['X3M_PROFILE_INTERVAL_US'] = str(args.profile_interval_us)
        # Witness switches: set only when requested, dropped otherwise so a
        # stale shell value cannot enable them and the plain command is unchanged.
        for name, wanted in (('X3M_PROFILE_RAW', args.profile_raw), ('X3M_AUDIO_SITES', args.audio_sites)):
            if wanted:
                env[name] = '1'
            else:
                env.pop(name, None)
        env['X3M_CAMERA'] = args.camera  # chase installs the trampoline; vanilla (or unset) patches nothing
        # LOD scale: set only when requested and dropped otherwise, so a stale
        # shell value cannot patch the threshold read (docs/architecture/lod-scale.md).
        if args.lod_scale is not None:
            env['X3M_LOD_SCALE'] = repr(args.lod_scale)
        else:
            env.pop('X3M_LOD_SCALE', None)
        for name, value in chase_tunables.items():
            if value is not None:
                env[name] = repr(value)
        # Optional camera corrections stay explicitly off unless requested,
        # even when the shell retains values from an earlier experiment.
        env['X3M_CHASE_SCENE_FIX'] = '1' if args.chase_scene_fix else '0'
        env['X3M_CHASE_COMBAT_TIGHTNESS'] = repr(args.chase_combat_tightness or 0.0)
        env['X3M_CHASE_HUD_ANCHOR'] = args.chase_hud_anchor
        env['X3M_CHASE_VIEW_RESTORE'] = '1' if args.chase_view_restore else '0'  # default off: the seven restore sites stay unpatched
        # Opt-in process-local WMA decoder: exactly the two versioned GStreamer
        # variables reach the child, and only DIR/registry is ever created.
        # CrossOver's unversioned GST_PLUGIN_PATH/GST_REGISTRY/
        # GST_PLUGIN_SYSTEM_PATH and DYLD_LIBRARY_PATH stay untouched
        # (docs/architecture/voice-decoder-adapter.md).
        voice_env = {}
        if args.voice_decoder is not None:
            root = Path(os.path.abspath(args.voice_decoder.expanduser()))  # keep /tmp, do not follow symlinks
            plugins, plugin = root / 'runtime/plugins', root / 'runtime/plugins/libgstlibav.dylib'
            libs, registry = root / 'runtime/lib', root / 'registry'
            if not plugin.is_file():
                parser.error(f'--voice-decoder: {plugin} not found; build the plugin first (docs/architecture/voice-decoder-adapter.md).')
            if not libs.is_dir():
                parser.error(f'--voice-decoder: {libs} is not a directory; the private FFmpeg closure is missing.')
            try:
                registry.mkdir(parents=True, exist_ok=True)
            except OSError as error:
                parser.error(f'--voice-decoder: cannot create the registry directory {registry}: {error}')
            if not registry.is_dir() or not os.access(registry, os.W_OK):
                parser.error(f'--voice-decoder: {registry} must be a writable directory.')
            voice_env = {'GST_PLUGIN_PATH_1_0': str(plugins), 'GST_REGISTRY_1_0': str(registry / 'x3-arm64.bin'),
                         'X3M_VOICE_DMO_FALLBACK': '1'}  # the DMO wrapper fallback hook travels with the decoder
            env.update(voice_env)
        # --dll applies to this child only, preserving the user's other overrides.
        command = [str(WINE), '--bottle', args.bottle, '--no-update',
                   '--dll', 'd3d9=b' if args.vanilla else 'd3d9=n,b',
                   '--workdir', str(game), str(game / 'X3AP.exe')]
        if args.direct:
            command += ['-noabout', '-skipintro', '-runinbg']
        if args.dry_run:
            print(json.dumps({'command': command, 'cwd': str(game),
                              'env': {**{k: env[k] for k in sorted(env) if k.startswith('X3M_')}, **voice_env}}, indent=2))
            return
        print('Launching X3AP through CrossOver Preview.', flush=True)
        raise SystemExit(subprocess.call(command, env=env, cwd=game))


if __name__ == '__main__':
    main()
