#!/usr/bin/env python3
"""Install/remove the owned app-local proxy and launch through CrossOver Preview.

No registry or bottle-wide DLL override is changed. Refuse to overwrite an
unowned d3d9.dll or remove a file whose contents changed after installation.
"""
import argparse
import hashlib
import json
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
    parser.add_argument('--loading-probes', action='store_true', help='Probe batch 2 (X3M_LOADING_PROBES=1; requires --telemetry): light IAT rows on the CryptoAPI, inflateInit2_/inflateEnd, the write-side and per-open KERNEL32 imports, plus entry-counting trampolines on twelve engine loading functions (byte-verified, exact executable only); one loading_probe line per site per report window (docs/verification/loading-probes.md)')
    parser.add_argument('--resource-read', choices=['native', 'verify', 'fast'], default='native', help='Archive reader 0x004e8880 service (X3M_RESOURCE_READ; exact executable only): native leaves the game\'s reader alone; verify runs our whole-extent decode into a scratch buffer, then the original, and logs any difference; fast returns our decode (one fread, word XOR, one inflate, no memset) and falls back to the original on any deviation (docs/verification/resource-reader.md)')
    parser.add_argument('--dat-handles', action='store_true', help='Keep catalogue .dat file handles between resource opens instead of _fopen/_fclose per resource (X3M_DAT_HANDLES=1; exact executable only; docs/reverse-engineering/resource-reader.md)')
    parser.add_argument('--profile', action='store_true', help='Run the in-process sampling profiler (X3M_PROFILE=1): one sampler thread, periodic profile_* reports in the session log; see docs/verification/sampling-profiler.md')
    parser.add_argument('--profile-interval-us', type=int, default=2000, help='Sampling interval in microseconds for --profile (100..1000000, default 2000)')
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
    parser.add_argument('--hdr-tonemap', action='store_true', help='Stage 2 (X3M_HDR_TONEMAP=agx; requires --hdr): the write-back is the AgX tonemap of the FP16 scene (decode, clamp, exposure, look; alpha carried), auto exposure metered by the log-luminance reduction chain over the FP16 target and adapted on the host (tau 0.4 s up / 1.2 s down); the presented image is still LDR to the game\'s bloom and GUI, and the decode of a gamma-space scene is a documented approximation ("Stage 2 implementation"); default off: identity write-back')
    parser.add_argument('--hdr-look', choices=['none', 'golden', 'punchy'], default='none', help='AgX look (X3M_HDR_LOOK; requires --hdr-tonemap; default none)')
    parser.add_argument('--hdr-decode', choices=['gamma2.2', 'pow22', 'srgb', 'none'], default='gamma2.2', help='Engine-space decode before the tonemap and the meter (X3M_HDR_DECODE; requires --hdr-tonemap): gamma2.2 (default; pow22 is the same curve), srgb, or none for the A/B against the decoded transform')
    parser.add_argument('--hdr-ev', type=float, default=0.0, help='Exposure offset in EV added to the auto-exposure target (X3M_HDR_EV; requires --hdr-tonemap; default 0)')
    parser.add_argument('--hdr-ev-manual', type=float, default=None, help='Fixed EV instead of auto exposure (X3M_HDR_EV_MANUAL; requires --hdr-tonemap): the meter chain does not run; deterministic; clamped to the EV range (X3M_HDR_EV_MIN/MAX, -8..8 by default)')
    parser.add_argument('--hdr-clamp', type=float, default=0.0, help='Clamp of the decoded scene value before the tonemap, the blunt firefly guard (X3M_HDR_CLAMP; requires --hdr-tonemap; default 0 = off)')
    parser.add_argument('--state-shadow', choices=['on', 'off'], default='on', help='Render-state shadow of the route (X3M_STATE_SHADOW): on (default) hooks SetRenderState and answers the per-draw state queries from the shadow; off issues GetRenderState per query (A/B; requires --motion-output)')
    parser.add_argument('--motion-rt-mode', choices=['perdraw', 'lazy'], default='perdraw', help='RT1/RT2 binding policy of the route: perdraw (default) rebinds around every routed draw; lazy keeps the bindings across consecutive routed draws (A/B experiment, requires --motion-output)')
    parser.add_argument('--camera', choices=['vanilla', 'chase'], default='vanilla', help='External back view camera (X3M_CAMERA): vanilla (default) patches nothing; chase installs the byte-verified cockpit-update trampoline (0x00420e06, exact executable only, fails closed to vanilla) and replaces the external back view with the critically damped chase camera; internal/front/side views stay vanilla, so the game\'s view keys remain the switch (docs/architecture/chase-camera.md)')
    parser.add_argument('--camera-rot-tau', type=float, default=None, help='Chase camera orientation spring time constant in seconds (X3M_CAMERA_ROT_TAU; default 0.20; requires --camera chase)')
    parser.add_argument('--camera-pos-tau', type=float, default=None, help='Chase camera boom spring time constant in seconds (X3M_CAMERA_POS_TAU; default 0.30)')
    parser.add_argument('--camera-offset-y', type=float, default=None, help='Fraction of the half screen height the ship sits below centre (X3M_CAMERA_OFFSET_Y; default 0.12)')
    parser.add_argument('--camera-distance-scale', type=float, default=None, help='Multiplier of the vanilla boom length (X3M_CAMERA_DISTANCE_SCALE; default 1.0)')
    parser.add_argument('--camera-lag-clamp-deg', type=float, default=None, help='Maximum orientation lag in degrees, the ship-on-screen window (X3M_CAMERA_LAG_CLAMP_DEG; default 10)')
    parser.add_argument('--camera-combat-tightness', type=float, default=None, help='Reserved 0..1 lag reduction while a target is locked (X3M_CAMERA_COMBAT_TIGHTNESS; parsed, inactive until a readable lock state exists)')
    parser.add_argument('--dry-run', action='store_true', help='launch only: validate the options and installation, print the command and X3M_* environment as JSON, and exit without launching')
    args = parser.parse_args()
    if args.dry_run and args.action != 'launch':
        parser.error('--dry-run applies to launch only.')
    if args.depth_copy and not args.ownership:
        parser.error('--depth-copy requires --ownership.')
    if args.scene_depth_capture and not (args.ownership and args.depth_copy):
        parser.error('--scene-depth-capture requires --ownership and --depth-copy.')
    if args.object_lifetime and not (args.object_trace and args.ownership):
        parser.error('--object-lifetime requires --object-trace and --ownership.')
    if args.mesh_cache and not args.telemetry:
        parser.error('--mesh-cache requires --telemetry.')
    if args.loading_probes and not args.telemetry:
        parser.error('--loading-probes requires --telemetry (the probe rows and trampolines are installed by the loading-trace initialization).')
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
    if not args.hdr_tonemap and (args.hdr_look != 'none' or args.hdr_decode != 'gamma2.2' or args.hdr_ev != 0.0 or args.hdr_ev_manual is not None or args.hdr_clamp != 0.0):
        parser.error('--hdr-look, --hdr-decode, --hdr-ev, --hdr-ev-manual and --hdr-clamp require --hdr-tonemap.')
    if not -16.0 <= args.hdr_ev <= 16.0 or (args.hdr_ev_manual is not None and not -16.0 <= args.hdr_ev_manual <= 16.0) or not 0.0 <= args.hdr_clamp <= 65504.0:
        parser.error('--hdr-ev and --hdr-ev-manual must be within [-16, 16], --hdr-clamp within [0, 65504].')
    if args.state_shadow != 'on' and not args.motion_output:
        parser.error('--state-shadow requires --motion-output.')
    camera_tunables = {'X3M_CAMERA_ROT_TAU': args.camera_rot_tau, 'X3M_CAMERA_POS_TAU': args.camera_pos_tau, 'X3M_CAMERA_OFFSET_Y': args.camera_offset_y,
                       'X3M_CAMERA_DISTANCE_SCALE': args.camera_distance_scale, 'X3M_CAMERA_LAG_CLAMP_DEG': args.camera_lag_clamp_deg,
                       'X3M_CAMERA_COMBAT_TIGHTNESS': args.camera_combat_tightness}
    if args.camera != 'chase' and any(v is not None for v in camera_tunables.values()):
        parser.error('--camera-rot-tau, --camera-pos-tau, --camera-offset-y, --camera-distance-scale, --camera-lag-clamp-deg and --camera-combat-tightness require --camera chase.')
    for name, value in camera_tunables.items():
        if value is not None and not (0 < value <= 10 if name.endswith(('TAU', 'SCALE')) else 0 <= value <= (90 if name.endswith('DEG') else 1)):
            parser.error(f'{name} out of range: {value}')
    if not 100 <= args.profile_interval_us <= 1000000:
        parser.error('--profile-interval-us must be between 100 and 1000000.')
    if args.gz_buffer_kb != 256 and not args.gz_buffer:
        parser.error('--gz-buffer-kb requires --gz-buffer.')
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
        temp = game / 'x3-modern-install.tmp'
        shutil.copy2(source, temp)
        os.replace(temp, dll)
        manifest.write_text(json.dumps({'project': 'x3-modern-renderer', 'sha256': digest(dll),
                                        'source': str(source)}, indent=2) + '\n')
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
        env['X3M_HDR_TONEMAP'] = 'agx' if args.hdr_tonemap else 'identity'
        env['X3M_HDR_LOOK'] = args.hdr_look
        env['X3M_HDR_DECODE'] = args.hdr_decode
        env['X3M_HDR_EV'] = repr(args.hdr_ev)
        env['X3M_HDR_EV_MANUAL'] = '' if args.hdr_ev_manual is None else repr(args.hdr_ev_manual)
        env['X3M_HDR_CLAMP'] = repr(args.hdr_clamp)
        env['X3M_STATE_SHADOW'] = '1' if args.state_shadow == 'on' else '0'
        env['X3M_GZ_BUFFER'] = '1' if args.gz_buffer else '0'
        env['X3M_GZ_BUFFER_KB'] = str(args.gz_buffer_kb)
        env['X3M_LOADING_PROBES'] = '1' if args.loading_probes else '0'
        env['X3M_CRYPT_CACHE'] = '1' if args.crypt_cache else '0'
        env['X3M_RESOURCE_READ'] = args.resource_read
        env['X3M_DAT_HANDLES'] = '1' if args.dat_handles else '0'
        env['X3M_PROFILE'] = '1' if args.profile else '0'
        env['X3M_PROFILE_INTERVAL_US'] = str(args.profile_interval_us)
        env['X3M_CAMERA'] = args.camera  # chase installs the trampoline; vanilla (or unset) patches nothing
        for name, value in camera_tunables.items():
            if value is not None:
                env[name] = repr(value)
        # --dll applies to this child only, preserving the user's other overrides.
        command = [str(WINE), '--bottle', args.bottle, '--no-update',
                   '--dll', 'd3d9=b' if args.vanilla else 'd3d9=n,b',
                   '--workdir', str(game), str(game / 'X3AP.exe')]
        if args.direct:
            command += ['-noabout', '-skipintro', '-runinbg']
        if args.dry_run:
            print(json.dumps({'command': command, 'cwd': str(game),
                              'env': {k: env[k] for k in sorted(env) if k.startswith('X3M_')}}, indent=2))
            return
        print('Launching X3AP through CrossOver Preview.', flush=True)
        raise SystemExit(subprocess.call(command, env=env, cwd=game))


if __name__ == '__main__':
    main()
