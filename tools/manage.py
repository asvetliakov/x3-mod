#!/usr/bin/env python3
"""Install/remove the owned app-local proxy and launch through CrossOver Preview.

No registry or bottle-wide DLL override is changed. Refuse to overwrite an
unowned d3d9.dll or remove a file whose contents changed after installation.
"""
import argparse
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import threading

ROOT = Path(__file__).resolve().parents[1]
BOTTLE = os.environ.get('X3M_BOTTLE', 'X3')
GAME = Path.home() / f'Library/Application Support/CrossOver/Bottles/{BOTTLE}/drive_c/X3'
WINE = Path('/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine')
# Framing defaults the launcher always forwards in chase mode (user selection
# 2026-09-16, docs/architecture/chase-hud-reticle-survey.md); they match the
# DLL's own fallback in src/proxy/chase_camera_math.h.
CHASE_FRAMING_DEFAULTS = {'X3M_CHASE_PITCH_DOWN_DEG': 0.5, 'X3M_CHASE_OFFSET_Y': 0.50}
# TAA image defaults the launcher always forwards in TAA mode (user selection
# after run 27, 2026-09-16, docs/architecture/temporal-integration.md); they
# match the DLL's own fallback in src/proxy/capture.cpp. An explicit 0 still
# disables either one and keeps the bit-identical route.
TAA_MIP_BIAS_DEFAULT = -0.5
TAA_SHARPEN_DEFAULT = 0.75
# Hull light-map gain the launcher forwards in HDR mode when the option is
# unset (user selection after run 41 C / run128, 2026-09-18,
# docs/architecture/linear-emission-cost.md, "Hull light-map gain"). The DLL's
# own fallback stays 1 = off; an explicit --hull-lightmap-gain 1 disables it,
# and the converted route (--linear-materials) keeps 1.0 because its own
# --lightmap-emissive-gain applies there instead.
HULL_LIGHTMAP_GAIN_DEFAULT = 4.0
# Small-parts cull the launcher forwards on every modded launch when the option
# is unset (user selection after run 43 B, 2026-09-19,
# docs/verification/cull-small-parts.md): 2 px at scope `all` took the busy view
# from 884 to 477 draws and ~30 to ~42 fps with no visible pop-in. The DLL's own
# fallback stays off (no variable = nothing patched); an explicit
# --cull-small-parts 0 turns it off, and a --vanilla launch forwards nothing.
CULL_SMALL_PARTS_DEFAULT_PX = 2.0
CULL_SMALL_PARTS_DEFAULT_SCOPE = 'all'


# Collision narrow phase: the SSE2 separating-axis test and the no-contact memo are
# forwarded on every modded launch (user selection after runs 45 A and 155/156,
# 2026-09-19/20, docs/reverse-engineering/sector-collide.md 12.8 and 14: collide
# phase 27 -> 12.7 ms with the SAT, a further ~60 % of node pairs skipped by the
# memo, 808,408 verified answers with 0 mismatches). The DLL's own fallback stays
# off (no variable = nothing patched); --no-collide-sat-sse2 / --no-collide-memo
# turn them off, and a --vanilla launch forwards nothing unless asked explicitly.
def collide_default(explicit, args):
    """An explicit --x / --no-x wins; unset means on for a modded launch, off under --vanilla."""
    return explicit if explicit is not None else not args.vanilla


def cull_small_parts_px(args):
    """The PX the launcher forwards: the explicit --cull-small-parts when given
    (0 = off), else the launcher default on a modded launch and nothing under
    --vanilla."""
    if args.cull_small_parts is not None:
        return args.cull_small_parts
    return 0.0 if args.vanilla else CULL_SMALL_PARTS_DEFAULT_PX


def hull_emitters_requested(args):
    """The ONE/ONE guide-light population (--hull-emitters), implied by an
    --emission-source-gain above 1: the guide lights belong to the effects
    group (Ctrl+Shift+F6) and take that gain, so the user selects both with
    one option (docs/architecture/linear-emission-cost.md, "Implemented")."""
    return bool(args.hull_emitters or (args.emission_source_gain is not None and args.emission_source_gain > 1.0))


# The proxy writes its session-*.log into <game dir>\x3-modern-captures; the
# launcher's own teed terminal output joins it there, so one preserved run
# directory (tools/analysis/snapshot_x3_run.py) holds both clocks
# (docs/verification/sampling-profiler.md, "audio correlation").
CAPTURE_SUBDIRECTORY = 'x3-modern-captures'
LAUNCHER_STDERR = 'launcher-stderr.log'


def utc_stamp(when=None):
    """[YYYY-MM-DDTHH:MM:SS.mmmZ] of this launcher process, so a child line
    carrying only a local-time stamp (GLib prints HH:MM:SS.mmm) still has a UTC
    reading next to it."""
    moment = when or datetime.datetime.now(datetime.timezone.utc)
    return f'[{moment:%Y-%m-%dT%H:%M:%S}.{moment.microsecond // 1000:03d}Z]'


def tee_stream(source, terminal, log, lock, clock=utc_stamp, chunk=65536):
    """Drain one child stream to EOF and copy it: the terminal keeps the exact
    bytes, the log gets each line behind this launcher's UTC stamp.

    Draining is unconditional. A sink that raises (a terminal whose reader has
    exited, a full disk) is dropped, reported once to the other sink and the
    read side continues, because a pump that stops reading fills the child's
    pipe and blocks the game in write(). Reads are bounded, and a chunk without
    any newline is flushed as one line, so a child that never emits a newline
    cannot grow the buffer. Returns the number of lines written to the log."""
    sinks = {'terminal': terminal, 'log': log}
    lines, pending = 0, b''

    def write(name, data, notify=True):
        sink = sinks[name]
        if sink is None:
            return
        try:
            if name == 'log':
                with lock:
                    sink.write(data)
                    sink.flush()
            else:
                sink.write(data)
                sink.flush()
        except (OSError, ValueError) as error:
            sinks[name] = None
            if notify:
                write('log' if name == 'terminal' else 'terminal',
                      f'{clock()} launcher tee: the {name} sink was dropped ({error}); '
                      f'the child is still being drained.\n'.encode('utf-8', 'replace'), notify=False)

    read = getattr(source, 'read1', source.read)
    while True:
        try:
            data = read(chunk)
        except (OSError, ValueError):
            break
        if not data:
            break
        write('terminal', data)
        pending += data
        while b'\n' in pending:
            line, pending = pending.split(b'\n', 1)
            write('log', clock().encode('ascii') + b' ' + line.rstrip(b'\r') + b'\n')
            lines += 1
        if len(pending) >= chunk:  # no newline in sight: flush what is held
            write('log', clock().encode('ascii') + b' ' + pending + b'\n')
            lines += 1
            pending = b''
    if pending:
        write('log', clock().encode('ascii') + b' ' + pending + b'\n')
        lines += 1
    return lines


def launch_teed(command, env, cwd, log_path, *, stdout=None, stderr=None, clock=utc_stamp, header=None):
    """Run the child with both streams teed into log_path (one fresh file per
    launch) and return its exit code. `header` is one extra line written before
    the child's output, so a preserved session records what was launched. A
    directory or file that cannot be written costs the copy, never the launch."""
    stdout = sys.stdout.buffer if stdout is None else stdout
    stderr = sys.stderr.buffer if stderr is None else stderr
    try:
        log_path.parent.mkdir(parents=True, exist_ok=True)
        if log_path.exists():
            log_path.unlink()  # a new file, so the snapshot sees this launch's creation time
        log = open(log_path, 'wb')
        # Whose file this is: a second concurrent launch replaces it, and the
        # preserved copy must still name the process that wrote the lines.
        log.write(f'{clock()} launcher_tee pid={os.getpid()} log={log_path}\n'.encode('utf-8', 'replace'))
        if header:
            log.write(f'{clock()} {header}\n'.encode('utf-8', 'replace'))
        log.flush()
    except OSError as error:
        print(f'Launcher output is not preserved ({error}); the terminal output is unchanged.', file=sys.stderr)
        return subprocess.call(command, env=env, cwd=cwd)
    with log:
        child = subprocess.Popen(command, env=env, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        lock = threading.Lock()
        pumps = [threading.Thread(target=tee_stream, args=(child.stdout, stdout, log, lock, clock), daemon=True),
                 threading.Thread(target=tee_stream, args=(child.stderr, stderr, log, lock, clock), daemon=True)]
        for pump in pumps:
            pump.start()
        code = child.wait()
        for pump in pumps:
            pump.join()
        child.stdout.close()
        child.stderr.close()
    return code


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
    parser.add_argument('--capture-frames', type=int, choices=range(0, 65), metavar='0..64', default=1,
                        help='Consecutive capture frames (X3M_CAPTURE_FRAMES). Above 8 is meant for the raw --taa-debug dumps (32 frames separate the 8-frame jitter ripple from slower crawl): about 40 MB per frame at 1280x768 on the HDR route (hdr + taa rgba16f 7.9 MB each, motion rgba32f 15.7 MB, depth 3.9 MB or 15.7 MB on the sun lane, present bgra8 3.9 MB), so 1.3-1.7 GB for 32 frames')
    parser.add_argument('--direct', action='store_true', help='Skip launcher and intro using X3 command-line switches')
    parser.add_argument('--vanilla', action='store_true', help='Launch with builtin D3D9, ignoring the installed proxy')
    parser.add_argument('--d3dx', choices=['native', 'builtin'], default='native', help="Which d3dx9_37.dll serves the game child, for the busy-frame experiment in "
                             "docs/architecture/effect-pass-replay.md: native (default) leaves the command as it is, so the "
                             "bottle decides (usually the native redistributable in the game directory); builtin appends "
                             "d3dx9_37=b to this child's --dll override, forcing Wine's builtin D3DX. The installed DLL's "
                             "loaded_module line reports which one actually loaded.")
    parser.add_argument('--fex-tso', choices=['on', 'off'], default=None, help="FEX-Emu memory-ordering experiment for the busy frame (docs/architecture/effect-pass-replay.md, "
                             "\"Environment experiments\"): off sets FEX_TSOENABLED=0 in this child's environment, on sets it to 1 "
                             "explicitly, unset (default) leaves the bottle's FEX configuration alone. off trades the x86 "
                             "total-store-order guarantees FEX emulates for speed and can expose latent multithreading races in the "
                             "game and in Wine, so it is an experiment, never a default; a session that misbehaves must be rerun "
                             "without it before its result counts.")
    parser.add_argument('--wined3d', default=None, metavar='CONFIG', help="Wine Direct3D settings for this child only (WINE_D3D_CONFIG=CONFIG), the documented environment "
                             "override of the HKCU\\Software\\Wine\\Direct3D registry keys; several settings are separated by ';', "
                             "e.g. 'csmt=0x0;renderer=vulkan'. The experiment in docs/architecture/effect-pass-replay.md uses csmt "
                             "(the command-stream thread, 0x0 runs the draw path on the calling thread) and renderer (gl or vulkan). "
                             "The proxy's loaded_module line for d3d9.dll does not reflect either key, so there is no log evidence "
                             "beyond behaviour and frame time; compare --pass-phases medians and frame dt.")
    parser.add_argument('--telemetry', action='store_true', help='Enable bounded loading, presentation and cursor diagnostics')
    parser.add_argument('--frame-timing', action='store_true', help='Per-300-frame frame-time window: one frame_timing line with dt/draws/present percentiles, the proxy draw/scene/state buckets with the state call mix, the pre-draw/between-draws/post-draw split of the game time between hooked calls, and up to four frame_timing_slow witnesses (X3M_FRAME_TIMING=1; requires --telemetry; docs/verification/sampling-profiler.md, "Frame timing diagnostic")')
    parser.add_argument('--frame-end-stride', type=int, default=300, metavar='N',
                        help='Frames between two frame_end lines, 1..100000, default 300 (X3M_FRAME_END_STRIDE; no prerequisite: frame_end exists in every mode): 1 logs every frame, which makes the frame cost readable per toggle state and shows periodic events the 300-frame cadence hides, at about 100 B of log per frame. Capture frames always log one. The other 300-frame reports of the Present path (chase camera, admission, finite upload) keep their own cadence')
    parser.add_argument('--fps-overlay', action='store_true', help='On-screen frame-rate line on the presented image (X3M_FPS_OVERLAY=1; default off; no prerequisite): "FPS 61.3  16.3 MS  DRAWS 638" from a one-second sliding window of the Present-to-Present interval (the ms figure is the frame interval, not GPU time), refreshed every 250 ms, plus "SHADOWS ON|OFF" when --sun-shadow-apply is on. Ctrl+Alt+F7 hides and shows it (Alt is the Option key under Wine on macOS; Shift must be up, so the Ctrl+Shift+F7 telemetry marker never fires on it). Drawn with Clear rectangles like the comparison notice, no GPU objects (docs/architecture/comparison-hotkeys.md, "FPS overlay")')
    parser.add_argument('--frame-timing-state-stamps', type=int, default=0, metavar='N',
                        help='Stamp every Nth hooked state call in the frame-timing diagnostic (X3M_FRAME_TIMING_STATE_STAMPS; requires --frame-timing; default 0 = count the calls without reading the clock, so state_us is reported as -1). Two QueryPerformanceCounter reads cost about 136 ns per state call under FEX, which is several ms per busy frame; N>0 stamps one call in N and scales the sum by N (reported as state_sampled=N)')
    parser.add_argument('--game-phases', action='store_true', help='Measure native frame phases and delayed target-lock work (X3M_GAME_PHASES=1; requires --telemetry)')
    parser.add_argument('--game-phase-threshold-ms', type=int, default=20, metavar='N',
                        help='Frame time at or above which the game-phase group writes its segment tape: one game_phase_slow_frame line plus its game_phase_segment rows (X3M_GAME_PHASE_THRESHOLD_MS; requires --game-phases; 1..10000, default 20, lowered from the built-in 50 so a 25-45 ms frame is attributed). Each qualifying frame writes up to 96 segment rows, so a low threshold on a steadily slow scene is verbose')
    parser.add_argument('--telemetry-draw', action='store_true',
                        help='Per-draw proxy cost metrics (X3M_TELEMETRY_DRAW=1; requires --telemetry): gate_us, route_draw_us, set_rt_us, lazy_flush_us and jitter_us on the motion_output_frame line. Off, the route takes no QueryPerformanceCounter stamp per draw (under Wine each stamp is a syscall; docs/verification/route-cost-run1.md)')
    parser.add_argument('--frame-phases', action='store_true', help='Per-frame engine phase stamps: ten byte-verified sites inside the render routine partition the frame into pre_render, prologue, scene_update, begin_scene, views, overlays, text, scene_end and present, plus per-view setup/submit sums; one frame_phases line per 300-frame window and up to four frame_phases_slow witnesses keyed by frame (X3M_FRAME_PHASES=1; requires --telemetry; independent of --game-phases; docs/verification/sampling-profiler.md, "Frame phases")')
    parser.add_argument('--pass-phases', action='store_true', help='Per-draw effect-pass stamps: four byte-verified sites in the D3DX pass loop of the material submission routine split each material draw into pass-apply (BeginPass), the device draw and EndPass, accumulated per frame through a lean stub (no x87 save) and reduced at the frame-phase boundary; one pass_phases line per 300-frame window with passes, apply/draw/end p50/p95, their sum, the same window\'s view_submit and the stamps\' own estimated cost self_p50_us (X3M_PASS_PHASES=1; requires --telemetry and --frame-phases; about 4,000 dispatches per busy frame, budget under 1.5 ms; docs/verification/sampling-profiler.md, "Pass phases")')
    parser.add_argument('--residual-phases', action='store_true', help='Residual attribution stamps: two byte-verified sites split what the pass and frame groups leave unattributed, the ID3DXEffect::Begin dispatch of the material submission routine (engine per-object preparation from the last pass_end, D3DX setup to the first pass_begin) and the particles-call return of the frame routine\'s per-view loop (the particles pass from view_submit_end; the rest of the views phase is computed as views - view_setup - view_submit - particles), accumulated per frame through the lean stub and reduced at the frame-phase boundary; one residual_phases line per 300-frame window with materials/particle views/passes/views, prepare/setup/particles/other p50/p95, prepare and setup per pass in ns, and the stamps\' own estimated cost (X3M_RESIDUAL_PHASES=1; requires --telemetry and implies --frame-phases and --pass-phases, which it pairs with; about 1,000 dispatches per busy frame; docs/verification/sampling-profiler.md, "Residual phases")')
    parser.add_argument('--submit-phases', action='store_true', help='view_submit candidate stamps: twenty-two byte-verified sites bracket the draw-queue sort 0x0047e620 (time, calls, queue length), the per-node cache walk (time, misses, sampled iterations), the ID3DXEffect SetTechnique and End dispatches, the Begin-to-pass-loop block, the two D3DXMatrixInverse calls, 0x004c0150 and 0x004bdee0, and write one submit_phases line per 300-frame window (p50/p95 per pair, calls, the stamps\' own self cost). Diagnostic: about 12 dispatches per draw plus 2 per node, roughly 1 ms per busy frame. Requires --telemetry and --frame-phases (docs/verification/sampling-profiler.md, Submit phases).')
    parser.add_argument('--loop-phases', action='store_true', help='Per-sector update stamps: six byte-verified sites inside the main loop\'s per-sector update driver split the input_part=0 stall region into its callees (collide, simulate, post, economy+attach), accumulated per frame over every container the driver visits through the lean stub and reduced at the frame-phase boundary; one loop_phases line per 300-frame window with sectors/containers, the four intervals p50/p95, their sum, the frame\'s pre_render (or the --game-phases input phase), the largest single interval and its owner, plus one loop_phases_slow line for each of the first 64 frames whose sum exceeds 50 ms (X3M_LOOP_PHASES=1; requires --telemetry and --frame-phases; six dispatches per active sector and two per skipped container per frame; docs/verification/sampling-profiler.md, "Loop phases")')
    parser.add_argument('--media-cue-trace', action='store_true', help='Trace every media-record build: one byte-verified gate on the allocator 0x00498140 records the media id, caller (selector/speech/script/savegame/query/other), constructor flags, result (the record or 0) and build duration of every call, drained at the Present boundary as media_cue lines (first 32 per second) plus one media_cue_window line per 300 frames with attempts/failures/refusals, per-frame attempt p50/max and the top ids (X3M_MEDIA_CUE_TRACE=1; requires --telemetry; docs/verification/media-cues.md, "Gate")')
    parser.add_argument('--media-cue-cache', choices=('on', 'off'), default='on', help='Negative cache for the sector selector\'s cue restart: a media id whose selector-path build returned 0 is refused (EAX 0, the state a failed build leaves) on the same gate for --media-cue-retry-s seconds instead of rebuilding the DirectShow graph every frame; speech, script, savegame and query callers are never refused (X3M_MEDIA_CUE_CACHE; default on since run 34, independent of --telemetry; pass off to restore the stock per-frame retry)')
    parser.add_argument('--media-cue-retry-s', type=int, default=30, metavar='N', help='Seconds before a cached media-cue failure is retried (X3M_MEDIA_CUE_RETRY_S; default 30; 1..3600; meaningless with --media-cue-cache off)')
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
    parser.add_argument('--taa-mip-bias', type=float, default=None, help='D3DSAMP_MIPMAPLODBIAS applied to the mip-mapped sampler stages of routed material draws while the TAA jitter is on, restored before every other draw (X3M_TAA_MIP_BIAS; requires --taa; 0 = off; the value for the 4-sample jitter; default -0.5 with --taa; 0 disables)')
    parser.add_argument('--taa-sharpen', type=float, default=None, help='Post-resolve sharpen of the presented image, 0..1 (X3M_TAA_SHARPEN; requires --taa): robust contrast-adaptive sharpening of the resolved image only, never of the history; 1 is the strongest setting, 0.5 one stop softer; default 0.75 with --taa; 0 disables, leaving the output bit-identical to the unsharpened route (docs/architecture/temporal-integration.md, "Post-resolve sharpen")')
    parser.add_argument('--taa-current-filter', type=float, default=None, metavar='A', help='Filtered current sample of the TAA resolve, 0..4 (X3M_TAA_CURRENT_FILTER; requires --taa): the current colour that enters the history blend becomes the exp(-A d^2) average of the 3x3 current samples (d in pixels from the pixel centre to each jittered sample position) instead of the point sample; the neighbourhood clip is unchanged. Default 0 = off, the unchanged resolve program; 1.0 is the modelled optimum, 2.29 the sharper setting (docs/verification/motion-output.md, "Run 139")')
    parser.add_argument('--taa-line-filter', default=None, metavar='A[,W]', help='Line-masked filtered current sample of the TAA resolve (X3M_TAA_LINE_FILTER; requires --taa; not with --taa-current-filter > 0; default absent = off; suggested 1.0, 2.0 is milder): the exp(-A d^2) average of --taa-current-filter, A within 0..4, applied only where the 3x3 depth holds a line-like pixel: geometry with background or farther geometry on both sides along one of four directions, W = 1 (default) or 2 px wide; silhouettes and surfaces keep the point sample. Against roping of sub-pixel lattices and distant struts (docs/architecture/taa-lattice-crawl.md section 9).')
    parser.add_argument('--taa-far-stabiliser', default=None, metavar='W[,A[,F0,F1[,LO,HI]]]', help='Far-gated stabiliser of the TAA resolve against shimmer of distant sub-pixel detail (X3M_TAA_FAR_STABILISER; requires --taa; not with --taa-adaptive-weight or --taa-current-filter > 0; default absent = off; suggested 0.985 for the weight alone, 0.985,1 with the filter): on pixels whose footprint exceeds F0 world units per pixel, fully at F1 (default 80,130), W is the history weight (0 off, else history weight..0.99; falls back to the history weight across the LO..HI speed gate) and A the exp(-A d^2) current-sample filter (0 off, else up to 4; must equal --taa-line-filter A when both are given). The weight is full while the far content moves at most LO px/frame on screen and back at the history weight from HI (default 0.03,0.25: a long history softens sliding detail). The two components are separate: W,0 is weight only, 0,A filter only (docs/architecture/taa-distant-line-fade.md section 9).')
    parser.add_argument('--taa-history-weight', type=float, default=None, metavar='W', help='History weight of the TAA resolve, 0.5..0.98 (X3M_TAA_HISTORY_WEIGHT; requires --taa): the fraction of the accepted history kept per frame. Default absent = 0.9; 0.95 halves the per-frame ripple and doubles the convergence time and the life of clamp-bounded ghost trails')
    parser.add_argument('--taa-thin-clip', type=float, default=None, metavar='S', help='Thin-feature soft clip of the TAA resolve, 0..1 (X3M_TAA_THIN_CLIP; requires --taa; default absent = off; suggested 0.75): where the 3x3 depth mixes the empty-depth sentinel and geometry the history is pulled only (1 - S) of the way to the clip box, fading out between 2 and 4 px/frame (docs/architecture/taa-flicker-suppression.md)')
    parser.add_argument('--taa-adaptive-weight', default=None, metavar='WMAX[,LO,HI]', help='Per-pixel age/speed history weight, w = min(n/(n+1), wmax(speed)) (X3M_TAA_ADAPTIVE_WEIGHT; requires --taa and --taa-thin-clip; default absent = off; suggested 0.97): WMAX within [history weight, 0.99] for slow content, falling to the history weight between LO and HI px/frame (default 0.1,0.5; the wide gate is 0.8,1.5). Costs two R32F targets (8 bytes per pixel)')
    parser.add_argument('--taa-alpha-history', action='store_true', help='Time-accumulate the resolved alpha on the HDR route (X3M_TAA_ALPHA_HISTORY=1; requires --taa; has an effect only with --hdr, where bloom reads it as the authored-glow weight)')
    parser.add_argument('--taa-sentinel', choices=['auto', '1', '2'], default='auto', help='Depth-sentinel policy of the resolve (requires --taa): auto reprojects unrouted (background) pixels through the live camera at the far plane whenever the engine camera read yields a transform, 1 keeps them current-only, 2 is strict (skips the resolve on frames without a transform)')
    parser.add_argument('--camera-cut-deg', type=float, default=20.0, help='Camera rotation per frame (degrees) above which the resolve declares a cut (requires --taa; default 20)')
    parser.add_argument('--camera-log', type=int, default=300, help='Cadence in frames of the camera_state log line (requires --taa; capture frames always log; default 300)')
    parser.add_argument('--scene-hook', nargs='?', const='on', default=None, choices=['on', 'off'], help='Engine scene-end hook (X3M_SCENE_HOOK): patch the frame routine\'s compositing callsite (0x004721b1, exact executable and bytes only, otherwise it fails closed to the bloom-copy/selector boundary) so the route learns the scene end from the engine and, with --taa, resolves there before the glow pass. Default on with --motion-output since review 26 (iteration 10: 214/214 agreement); "--scene-hook" alone means on; "--scene-hook off" keeps the copy/selector boundary')
    parser.add_argument('--hdr', action='store_true', help='FP16 HDR scene path (X3M_HDR=1; requires --motion-output): the scene renders into an owned A16B16G16R16F target bound as RT0 at the latching Clear and is written back into the game\'s 8-bit main target at the scene end (--scene-hook, else the bloom copy, else EndScene/Present); fails closed on the capability gate and self test. Without --hdr-tonemap the write-back is the stage-1 identity copy and presented frames equal the non-HDR frames to within one 8-bit code (docs/architecture/hdr-scene-path.md, "Stage 1 implementation")')
    parser.add_argument('--hdr-tonemap', action='store_true', help='AgX write-back of the FP16 scene (X3M_HDR_TONEMAP=agx; requires --hdr), Auto capped at +1.3 EV by default; --hdr-exposure fixed disables frame metering. Ctrl+Shift+F9 compares AUTO and fixed EV0 during play. Default off: identity write-back.')
    parser.add_argument('--linear-distance-fade', action='store_true', default=None, help='Qualify six Asteroid source-over materials in linear light (requires --linear-materials --taa and the material HDR/motion prerequisites; default on with linear materials and TAA, off otherwise; --no-linear-distance-fade disables; full-size composition cost per draw)')
    parser.add_argument('--no-linear-distance-fade', dest='linear_distance_fade', action='store_false', help='Keep the Asteroid source-over materials on the native route even when --linear-materials --taa are on (opt out of the default)')
    parser.add_argument('--fade-witness', type=int, nargs='?', const=30, default=None, metavar='K', help='Diagnostic fade-region witness (X3M_FADE_WITNESS=K; requires --linear-distance-fade or --screen-emission; default off; "--fade-witness" alone means 30): every K-th frame without an admitted emission draw the M coverage target is read back once (GetRenderTargetData to a retained system-memory copy) and the covered pixels outside the union of that frame\'s derived fade rectangles are counted; one fade_witness line per K-th frame plus that frame\'s per-DIP fade_region lines (first 64, with a truncated count) in the session log, validated by verification/probe/run_linear_distance_fade_live.py (docs/architecture/linear-distance-fade-region.md, step 1)')
    parser.add_argument('--sun-shadow-lane', action='store_true', help='Sun-share RT2 lane (X3M_SUN_SHADOW_LANE=1; default off; requires --motion-output --taa --hdr): on original shading the reviewed original programs bind their own code-value share variant (composed with --original-fill), with --linear-materials the converted materials bind theirs; applies no shadows by itself (docs/architecture/legacy-sun-application.md, 4).')
    parser.add_argument('--sun-shadow-apply', action='store_true', help='Scene-end sun-shadow application (X3M_SUN_SHADOW_APPLY=1; default off; requires --sun-shadow-lane --shadow-replay-depth): one quad multiplies the FP16 scene by 1 - (1 - f) s, s the lane share and f a 3x3 PCF of the same frame\'s replay map, before AO and the TAA resolve; exponent 1 on original shading (code values both sides), 1/2.2 with --linear-materials (the converted lane\'s linear law); one sun_shadow_apply_frame line per frame; a frame missing the lane, the replay or the FP16 owner is left byte-identical (docs/architecture/legacy-sun-application.md, 2). Ctrl+Shift+F12 switches the shadows off and on at a frame boundary for an at-rest A/B of their GPU cost: off, no map is cleared or drawn and the quad is skipped (no shadow_replay_depth and no sun_shadow_apply_frame line), every retained basis is voided so the first frame back on replays every cascade, and one sun_shadow_toggle line records each press (docs/architecture/comparison-hotkeys.md, "Sun shadows at rest").')
    parser.add_argument('--shadow-replay-candidates', action='store_true', help='Lane-independent caster-candidate counter of the motion route (X3M_SHADOW_REPLAY_CANDIDATES=1; requires --motion-output --ownership only, works with original hull shading; default off): one shadow_replay_candidates line per scene end and at most 16 shadow_replay_lock_witness lines per device; integer bookkeeping per routed draw, no allocation, no shadows (docs/architecture/shadow-replay-gates.md, "Implemented")')
    parser.add_argument('--shadow-replay-depth', action='store_true', help='One-cascade depth replay of the slice-0 caster candidates into a private sun-space map at every scene end (X3M_SHADOW_REPLAY_DEPTH=1; implies --shadow-replay-candidates and requires its prerequisites --motion-output --ownership only; default off): one shadow_replay_depth line per frame, nothing samples the map, no shadows are applied (docs/architecture/shadow-replay-gates.md, "Implemented: cascade-0 depth replay fixture")')
    parser.add_argument('--shadow-replay-size', type=int, default=None, metavar='N', help='Side of the square depth replay map in texels, 64..4096, default 1024 (X3M_SHADOW_REPLAY_SIZE; requires --shadow-replay-depth); map memory is 4 N^2 bytes (R32F) plus the depth attachment')
    parser.add_argument('--shadow-replay-extent', type=float, default=None, metavar='E', help='Half-extent of the cascade-0 map box in world units in the sun basis, 50..4000, default 250 (X3M_SHADOW_REPLAY_EXTENT; requires --shadow-replay-depth): the world texel is 2 E / N, so a station-wide box (E 1000-1500) needs --shadow-replay-size 2048-4096 for the same texel; the candidate box test, the replay projection and the apply quad share the value')
    parser.add_argument('--shadow-replay-depth-half', type=float, default=None, metavar='D', help='Half depth range of the cascade-0 map box along the sun in world units, 128..8192, default 512 (X3M_SHADOW_REPLAY_DEPTH_HALF; requires --shadow-replay-depth); the apply bias is expressed in world units and rescaled from it per frame')
    parser.add_argument('--shadow-replay-cap', type=int, default=None, metavar='N', help='Managed caster candidates recorded and replayed per frame, 1..1024, default 512 (X3M_SHADOW_REPLAY_CAP; requires --shadow-replay-candidates or --shadow-replay-depth); the rest count capped in the shadow_replay_candidates line')
    parser.add_argument('--shadow-cascades', default=None, metavar='E0,E1,...|default', help='Sun-shadow cascades (X3M_SHADOW_CASCADES; default off: the single --shadow-replay-extent map; requires --shadow-replay-depth): 1..5 ascending half-extents in world units, each 50..150000, of camera-centred texel-snapped maps replayed in one transaction; "default" means 250,1500,7500,25000 (a 30 km reach: 250,1500,7500,37500,150000). Every cascade reaches 2 x the largest extent towards the light, the apply quad selects the first cascade containing a pixel with a 10 %% blend band and fades the last one to lit; the far cascade replays on even frames only while the frame exceeds --shadow-cascade-budget (docs/architecture/shadow-cascades.md)')
    parser.add_argument('--shadow-cascade-sizes', default=None, metavar='N[,N...]', help='Map side per cascade, 64..4096, one value for all or one per cascade, default 4096 (X3M_SHADOW_CASCADE_SIZES; requires --shadow-cascades); memory is 4 N^2 bytes per map plus one depth attachment of the largest size')
    parser.add_argument('--shadow-cascade-caps', default=None, metavar='N[,N...]', help='Caster records per cascade and frame, 1..4096 (bounded by that cascade\'s --shadow-cascade-records), one value for all or one per cascade, default 128,512,1024,1024,1024 (X3M_SHADOW_CASCADE_CAPS; requires --shadow-cascades); the drops count capped<i> in the shadow_replay_candidates line')
    parser.add_argument('--shadow-cascade-records', default=None, metavar='N[,N...]', help='Record capacity per cascade, 1..4096, one value for all or one per cascade, default 1024 (X3M_SHADOW_CASCADE_RECORDS; requires --shadow-cascades): the caster list is sized to the largest at device creation (state memory, about 400 bytes per record), so a far cascade can carry more than the 1,024 the default list holds (docs/architecture/shadow-cascade-extents.md, "Caster pool control")')
    parser.add_argument('--shadow-cascade-static-from', type=int, default=None, metavar='K', help='Cascades K and beyond (1..cascades-1) admit static casters only (X3M_SHADOW_CASCADE_STATIC_FROM; requires --shadow-cascades): the retention store\'s verdict for nodes it knows, else the draw\'s world rows unchanged since its previous sighting within --shadow-caster-retention-eps; a first sighting counts as moving. Refusals count static_only_refused<i>. Under --shadow-cascade-adaptive-c0 the policy follows the slid extents: a slid cascade is static-only when the configured cascade whose extent it now most closely matches is (a corvette on 250/1500/7500/37500 with --shadow-cascade-static-from 3 makes its slid 16875 cascade static-only), and --shadow-cascade-large-min scales with that cascade\'s extent over its match\'s')
    parser.add_argument('--shadow-cascade-large-min', type=float, default=None, metavar='UNITS', help='A static-only cascade (--shadow-cascade-static-from) also admits a moving caster whose world AABB extent is at least UNITS (0..1000000; X3M_SHADOW_CASCADE_LARGE_MIN; requires --shadow-cascades; default 0: static only). The extent is the draw\'s (a mesh part, not the whole ship): 1500 passes M7 and larger hulls (M7 about 3,500 u, TL 5,000, M2/M1 7,500-10,000) and refuses fighters and small parts (M3 250 u, M6 900 u; run-115 census parts 100-610 u). Admissions count large_admitted<i>')
    parser.add_argument('--shadow-cascade-backface-from', default=None, metavar='K|none', help='Cascades K and beyond (0..cascades-1) replay their casters\' BACK faces (CW and CCW swapped per draw; NONE unchanged), so a lit surface never compares against its own depth on the knife edge re-rolled by the TAA jitter (X3M_SHADOW_CASCADE_BACKFACE_FROM; requires --shadow-cascades). Default (absent): every cascade whose world texel is at least 8 units (37,500 / 4096 = 18.3 u qualifies, 7,500 / 4096 = 3.7 u does not); "none" turns it off. Trade-off: a back-face map casts no contact shadow from geometry thinner than one texel of that cascade (a hull plate at 18-73 u texels is invisible either way) and a pancaked caster (nearer the light than the map\'s near plane) is flattened by its back faces as before')
    parser.add_argument('--shadow-cascade-drop-order', choices=('submission', 'importance'), default=None, help='What a cascade drops when its candidates exceed its cap (X3M_SHADOW_CASCADE_DROP_ORDER; requires --shadow-cascades): submission (default: the last submitted) or importance (the smallest projected size at the camera, decided at the scene end; stable across submission order; dropped_min_size<i> shows the largest caster dropped)')
    parser.add_argument('--shadow-sun-poll', choices=('on', 'off'), default=None, help='Sun position for the cascades from the engine\'s brightest directional light node instead of one LightDir_Dir0 constant (X3M_SHADOW_SUN_POLL; default on with --shadow-cascades; verified executable only, cross-checked against the constants, the constant latch otherwise; requires --shadow-cascades).')
    parser.add_argument('--shadow-sun-trace', action='store_true', help='Per-frame sun trace of the cascades (X3M_SHADOW_SUN_TRACE=1; default off; requires --shadow-cascades): one shadow_sun_frame line per frame with the frame\'s sun source, the reason and the poll status, the cascades that re-derived their direction (rederived= and the bit mask rederived_mask=), the poll/constant agreement angle and the light distance, so the re-derivation rate while moving and at rest is measurable between the sparse shadow_replay_sun_point lines (about 200 B per frame; read by tools/analysis/shadow_sun_frame.py)')
    parser.add_argument('--shadow-cascade-budget', type=int, default=None, metavar='B', help='Draw issues per frame above which the far cascade replays on even frames only, 1..4096, default 640 (X3M_SHADOW_CASCADE_BUDGET; requires --shadow-cascades)')
    parser.add_argument('--shadow-cascade-adaptive-c0', type=float, default=None, metavar='K', help='Own-ship-adaptive near cascade (X3M_SHADOW_CASCADE_ADAPTIVE_C0; default off; requires --shadow-cascades; suggested 1.5): the first cascade\'s half-extent becomes max(its configured value, K x the own ship\'s radius), the radius being the largest object-space AABB corner distance over the player ship\'s z-writing draws (the ship is the active cockpit\'s ref object of the verified executable), committed at once on a ship change and after eight stable frames on a > 20 %% size change, clamped to the last cascade\'s extent; the texel is 2 E0 / size. While E0 is above its configured value the configured ladder slides with it (--shadow-cascade-ladder-ratio): cascade i becomes max(its configured extent, E0 x R^i), capped at the last cascade\'s configured extent, and a cascade whose slid extent reaches the next one\'s is dropped (its map stays allocated, nothing replays into it, the apply owns no pixel with it); every slid cascade re-anchors its texel grid once per commit. One shadow_cascade_set line (own_radius= e0= texel0= active_mask= slid= extents=) per commit and per F8 frame (docs/architecture/shadow-cascade-extents.md, 5)')
    parser.add_argument('--shadow-cascade-ladder-ratio', type=float, default=None, metavar='R', help='Ratio between consecutive cascades of the slid ladder under --shadow-cascade-adaptive-c0 (X3M_SHADOW_CASCADE_LADDER_RATIO; 2..16, default 5; requires --shadow-cascade-adaptive-c0): with a corvette at E0 675 the set 250 / 1500 / 7500 / 37500 becomes 675 / 3375 / 16875 / 37500; a fighter at the configured E0 keeps the configured set. The per-cascade caps and records, --shadow-cascade-static-from and --shadow-cascade-large-min slide with the extents: each live cascade takes the policy of the configured cascade its extent most closely matches (a dropped cascade keeps no cap; the active caps are scaled down together when their sum would exceed the configured storage); every slid or dropped/restored cascade re-anchors its grid once per commit')
    parser.add_argument('--shadow-retention-census', action='store_true', help='Caster retention census (X3M_SHADOW_RETENTION_CENSUS=1; default off; requires --shadow-cascades): the node-keyed retention store runs with every expiry decision taken as if live but holds no references and replays nothing; one shadow_retention_frame line per frame, one cumulative shadow_retention_resight line every 300 frames and shadow_retention_caster lines on F8 frames calibrate eps, the age cap and the budget before --shadow-caster-retention is trusted (docs/architecture/shadow-caster-retention.md, stage 1)')
    parser.add_argument('--shadow-caster-retention', action='store_true', help='Retention of static sun-shadow casters the engine stopped submitting (X3M_SHADOW_CASTER_RETENTION=1; default off; requires --shadow-cascades; wins over --shadow-retention-census): nodes whose world rows held still for 8 sightings keep their draws, with the store\'s own references on VB, IB and declaration, and are replayed into the cascades they meet inside the per-cascade caps and the issue budget until the node retires, leaves 2 x the outermost box, its buffers change, the age cap passes, the sun re-latches or the device resets (docs/architecture/shadow-caster-retention.md, stage 2)')
    parser.add_argument('--shadow-caster-retention-age', type=int, default=None, metavar='FRAMES', help='Frames an unseen static caster is kept, 1..10000000, default 7200 (X3M_SHADOW_CASTER_RETENTION_AGE; requires --shadow-retention-census or --shadow-caster-retention)')
    parser.add_argument('--shadow-caster-retention-eps', type=float, default=None, metavar='UNITS', help='Largest AABB-corner displacement between two sightings of a static caster in world units, 0.0001..100, default 0.05 (X3M_SHADOW_CASTER_RETENTION_EPS; requires --shadow-retention-census or --shadow-caster-retention)')
    parser.add_argument('--shadow-retention-timing', action='store_true', help='Per-draw cost of the caster retention record hook on the shadow_retention_frame line (X3M_SHADOW_RETENTION_TIMING=1; default off; requires --shadow-retention-census or --shadow-caster-retention): two counter reads per recorded draw, draw_us / draw_calls')
    parser.add_argument('--sun-shadow-receiver-depth', choices=('device', 'linear'), default=None, help='Deprecated no-op: the sun-shadow apply quad always reads the receiver depth from the lane\'s A32B32G32R32F RT2 (.b = linear view depth; docs/architecture/shadow-receiver-depth.md, flipped 2026-09-18 after run 41 A2). `linear` is accepted for run-41 command lines and forwards nothing; `device` (the old G32R32F z/w law) no longer exists and is refused')
    parser.add_argument('--sun-shadow-bias-units', type=float, default=None, metavar='B', help='Constant sun-shadow compare bias in world units, 0..1000, default 0.53571875 (X3M_SUN_SHADOW_BIAS_UNITS; requires --sun-shadow-apply): the quad subtracts B plus one world texel of the map, divided by 2 D, from every compare; with --sun-shadow-bias-clamp-texels the defaults resolve to the former 0.001 / 0.01 at the default 250 / 512 / 1024 cascade; capture frames print the resolved values in sun_shadow_apply_params')
    parser.add_argument('--sun-shadow-bias-clamp-texels', type=float, default=None, metavar='T', help='Receiver-plane bias clamp and non-planar fallback of the sun-shadow quad in world texels of the map (2 E / N), 1..64, default 20.97152 (X3M_SUN_SHADOW_BIAS_CLAMP_TEXELS; requires --sun-shadow-apply): the default is the former 0.01 at the default cascade; the detached fixture was tuned at 4 texels and the wide fixture shows the default lighting a few silhouette pixels of a receiver\'s own faces (docs/verification/directional-shadows.md)')
    parser.add_argument('--sun-shadow-bias-slope-texels', type=float, default=None, metavar='S', help='Slope-scaled margin of the cascade sun-shadow compare in texels of the receiver plane\'s depth slope, 0..8, default 0.2 (X3M_SUN_SHADOW_BIAS_SLOPE_TEXELS; requires --sun-shadow-apply; 0 keeps the constant + plane law)')
    parser.add_argument('--ambient-occlusion', action='store_true', help='Half-resolution GTAO at the scene-end hook, multiplied into the scene target before the temporal resolve (X3M_AMBIENT_OCCLUSION=1; requires --motion-output --taa; default off). Ctrl+Shift+F11 toggles the chain off/on during play for a same-scene comparison (one ambient_occlusion_toggle log line per press; the pass stays attached). docs/architecture/ambient-occlusion.md, "Step 2"')
    parser.add_argument('--ao-radius', type=float, default=None, metavar='METRES', help='Ambient occlusion world radius in metres, 0.1..100, default 2 (X3M_AO_RADIUS; requires --ambient-occlusion; view units are 0.2 m, the calibration is tunable because the view-unit check is inconclusive)')
    parser.add_argument('--ao-strength', type=float, default=None, help='Ambient occlusion strength s of the factor 1 - s (1 - ao), 0..1, default 0.5 (X3M_AO_STRENGTH; requires --ambient-occlusion)')
    parser.add_argument('--ao-debug', action='store_true', help='Ambient occlusion debug view: the factor is written as grayscale instead of multiplied, and the per-frame timing line is on (X3M_AO_DEBUG=1; requires --ambient-occlusion)')
    parser.add_argument('--ao-timing', action='store_true', help='One ambient_occlusion_frame log line per frame with GPU timestamp and CPU wall time of the chain (X3M_AO_TIMING=1; requires --ambient-occlusion; default off)')
    parser.add_argument('--shimmer-trace', action='store_true', help='Diagnostic distant-shimmer trace (X3M_SHIMMER_TRACE=1; requires --motion-output --taa; default off): every frame logs one shimmer_frame line with the TAA state (history, skip, cut, jitter index) and the projection p00/p11 as integers scaled by 1e4, plus up to 32 shimmer_draw lines identifying that frame\'s Asteroid-class scene draws (node/model/lod, vertex, index and primitive counts, the distance-fade f in per mille when the draw was fade-admitted and its derived screen rectangle) with a truncated count beyond 32 (docs/architecture/linear-distance-fade-region.md, "Shimmer trace (diagnostic)")')
    parser.add_argument('--screen-emission', action='store_true', help='Packed screen emission of the bullet draws inside the region bracket (X3M_SCREEN_EMISSION=1, which also sets X3M_SCREEN_EMISSION_BOUND=1; requires --taa --motion-output --ownership --hdr --hdr-tonemap and gamma2.2 decode, with or without --linear-materials; default off): the nine SM1 screen pairs drawn in the native ONE/INVSRCCOLOR state with a locked-prefix bound compose through policy 8 in place; unbounded, unknown-state, capability-refused or otherwise refused draws stay native (docs/architecture/screen-emission-region.md, step C)')
    parser.add_argument('--screen-emission-additive', type=float, default=None, metavar='G', help='Additive bullets (X3M_SCREEN_EMISSION_ADDITIVE=G, finite 1..8; requires --motion-output --hdr; mutually exclusive with --screen-emission; default off): the nine SM1 screen pairs drawn in the native ONE/INVSRCCOLOR state draw in place with DESTBLEND ONE and their colour multiplied by G (G=1 binds the original shader), so the FP16 scene accumulates G*q + D above 1.0 for exposure and bloom; no bracket, bound, copies or temporal work; the blend law changes and native parity is not kept (docs/architecture/screen-emission-region.md, "Additive option"). Ctrl+Shift+F5 switches these draws between G and native during play (no shader is recreated; one screen_emission_additive_toggle line per press). With --telemetry, one screen_emission_additive_frame line per Present reports the admitted and refused draws of that frame and the hex mask of the nine pairs admitted')
    parser.add_argument('--screen-emission-additive-alpha', type=float, default=None, metavar='K', help='Per-source bloom attenuation of the additive bullets (X3M_SCREEN_EMISSION_ADDITIVE_ALPHA=K, finite 0..1; requires --screen-emission-additive; absent keeps the native alpha law): the admitted additive draw writes K*a + D.a to the scene alpha the bloom extract uses as its per-pixel authored weight, through separate-alpha blending (DESTBLENDALPHA ONE with SRCBLENDALPHA ZERO at K=0, ONE at K=1, else BLENDFACTOR with K in every lane; the colour law stays ONE/ONE/ADD and reads no blend factor). K=0 makes the bolts bloom only through the thresholded highlight term while engines, sun and every other alpha-authored emitter keep their channel; their presented brightness is unchanged because the colour law is untouched. A device without D3DPMISCCAPS_SEPARATEALPHABLEND, or without D3DPBLENDCAPS_BLENDFACTOR for a K strictly between 0 and 1, refuses the draw to the native path with screen_emission_additive_refused reason=alpha_caps. Ctrl+Shift+F5 turns it off with the rest of the option (docs/architecture/bloom-per-source-attenuation.md, option 1)')
    parser.add_argument('--screen-emission-timing', action='store_true', help='Per-frame timing diagnostic of the screen-emission option (X3M_SCREEN_EMISSION_TIMING=1; requires --screen-emission; default off): one screen_emission_frame line per Present with that frame\'s packed_admitted, brackets_px and cpu_us (the wall-clock QueryPerformanceCounter delta since the previous Present). The option itself logs nothing per frame (docs/architecture/screen-emission-region.md, step C)')
    parser.add_argument('--screen-emission-gain', type=float, default=None, metavar='G', help='Step E gain of the packed screen composition, finite 0.5..8, default 1 (X3M_SCREEN_EMISSION_GAIN; requires --screen-emission): the composed bullet is decode(native after) - decode(native before) scaled by G on the decoded scene, so 1 presents the native bolt exactly and larger values lift it into HDR for bloom and exposure (docs/architecture/screen-emission-region.md, step E)')
    parser.add_argument('--linear-emissions', action='store_true', help='Compose reviewed additive scene emissions in linear light (requires --motion-output --taa --hdr --hdr-tonemap and gamma2.2 decode; default off)')
    parser.add_argument('--emission-gain', type=float, default=None, help='Linear emission gain, finite 0..16, default 1 (requires --linear-emissions)')
    parser.add_argument('--emission-source-gain', type=float, default=None, metavar='G', help='Source-only encoded gain of the twenty engine/effects emission pairs (ship engine glow, jump gate, weapon impact flashes, muzzle glows, explosion sprites), finite 1..8, default 1 = off (X3M_EMISSION_SOURCE_GAIN; requires --hdr; excludes --linear-emissions, whose bracket carries its own --emission-gain; needs neither --linear-materials nor --taa): the pixel program of each pair multiplies its native colour output by G before the game\'s own blend into the FP16 scene, so bloom and exposure pick the brighter emitters up. An ADD/ONE/ONE draw keeps its blend (G*S + D); a screen ADD/ONE/INVSRCCOLOR draw (most engine materials) draws with DESTBLEND ONE substituted for that draw only (G*S + D: identical to native over black for S <= 1, brighter by D*S over a lit background); alpha, draw order and every other state stay native, and a pair drawn through any other blend stays native. Gain 1 creates no variant and is byte-identical to a build without the option (docs/architecture/linear-emission-cost.md, "Implemented" and "Screen substitution"). Ctrl+Shift+F6 switches all twenty pairs and the --hull-emitters guide lights, which follow this gain, between G and native during play (no shader is recreated; one emission_source_gain_toggle and one hull_emission_gain_toggle line per press)')
    parser.add_argument('--hull-emission-gain', type=float, default=None, metavar='G', help='Own gain of the --hull-emitters population, finite 1..8 (X3M_HULL_EMISSION_GAIN=G; requires --hull-emitters and --hdr; bracket 2 / 4 / 8). Independent of --emission-source-gain, which may be absent, so the guide lights can be judged alone; without this option they take the value of --emission-source-gain, which above 1 also implies --hull-emitters')
    parser.add_argument('--hull-emitters', action='store_true', help='Source gain G over the hull-program emitters (X3M_HULL_EMISSION_GAIN=G, default off = 1.0; requires --hdr and a gain: --hull-emission-gain G, else the value of --emission-source-gain G): position lights, deco flares, warning signs and warp tunnels are drawn by twelve standard_lighting / XT_standard_lighting material programs with ADD ONE/ONE materials, which the twenty effects pairs cannot reach (docs/architecture/emitter-plan.md phase 3). Their art is diffuse-authored with the lightmap slot black, so each covered program gets one variant that multiplies the whole colour output of the draw by G (the final colour instruction redirected to a temporary, one MUL into oC0.xyz); the native alpha and every other state stay native. Admission is per draw and keyed on blend state: only an ADD ONE/ONE draw of a covered program takes the variant, opaque and screen-blended draws of the same program stay native (hull_emission_frame telemetry per frame; on an F8 capture frame one hull_emission_draw line per admitted draw with the node, model and LOD, joined per model by tools/analysis/summarize_hull_emitters.py). G=1 creates no variant. Implied by an --emission-source-gain above 1, whose value it then takes, because the guide lights belong to the effects group: Ctrl+Shift+F6 switches them together with the twenty effects pairs between G and native during play (one hull_emission_gain_toggle line per press, key=ctrl_shift_f6); Ctrl+Shift+F4 is the hull light-map gain only')
    parser.add_argument('--effect-source-gain', type=float, default=None, metavar='G', help=argparse.SUPPRESS)  # removed 2026-09-16: refused below, naming the replacement
    parser.add_argument('--linear-materials', action='store_true', help='Evaluate the reviewed hull-material pairs in linear space, preserving motion and compatibility-encoding into FP16 (requires --motion-output --hdr --hdr-tonemap and gamma2.2 decode; default off)')
    parser.add_argument('--material-direct-gain', type=float, default=None, help='Linear direct-light gain, finite 0..16, default 1 (requires --linear-materials)')
    parser.add_argument('--material-emissive-gain', type=float, default=None, help='Linear scaled material-emissive gain, finite 0..16, default 1 (requires --linear-materials)')
    parser.add_argument('--lightmap-emissive-gain', type=float, default=None, help='Linear lightmap-emissive gain, finite 0..16, default 1 (requires --linear-materials)')
    parser.add_argument('--material-fill', type=float, default=None, metavar='K', help='Constant hemispherical fill inside the converted material law, finite 0..0.5, default 0.05 with --linear-materials (X3M_MATERIAL_FILL; requires --linear-materials): every converted pixel program adds k*decode(LightDir_Color0)*g_direct to its lobe sum before the albedo multiply, so faces that face no light source keep a floor tinted by the sector sun. Explicit 0 disables fill and keeps the generated programs byte-identical to a build without the option (docs/architecture/fill-light.md)')
    parser.add_argument('--original-fill', type=float, default=None, metavar='K', help='Fill in linear light inside the ORIGINAL hull pixel programs, finite 0..0.5, default 0 = off (X3M_ORIGINAL_FILL; requires --hdr; excludes --linear-materials, whose converted programs take --material-fill instead; needs neither --taa nor --hdr-tonemap): the 108 reviewed hull/asteroid/palette/glass/XT pixel programs get sum = encode(decode(sum) + K*decode(LightDir_Color0)) at their lobe-sum site before the albedo multiply, with the exact 2.2 power law and everything else in the program untouched, so shadow sides keep a floor tinted by the sector sun on original shading. K=0 creates no variant and is byte-identical to a build without the option (docs/architecture/original-shading-critique.md, 1a "Implemented")')
    parser.add_argument('--hull-lightmap-gain', type=float, default=None, metavar='G', help='Gain on the self-illumination (light-map) term inside the ORIGINAL hull pixel programs, finite 1..8, launcher default 4 with --hdr, 1 = off (X3M_HULL_LIGHTMAP_GAIN; requires --hdr; excludes --linear-materials, whose converted programs take --lightmap-emissive-gain instead; composes with --original-fill): 100 of the 108 reviewed hull/palette/XT pixel programs fetch a light map (station windows, hull lights) as their last texture read and add its RGB unscaled to the lit colour, so each gets one variant with one MUL of that sample by G right after the fetch, keeping the alpha, the lit colour and every other word native (docs/reverse-engineering/hull-self-illumination.md); the four glass and four asteroid programs have no such term and stay native. Every opaque draw of those programs carries it (placeholder black light maps multiply to zero). G=1 creates no variant. With --hdr the launcher forwards 4 unless another value is given (the DLL default stays 1); --hull-lightmap-gain 1 turns it off. Ctrl+Shift+F4 switches this gain alone between G and native during play (one hull_emission_gain_toggle line per press, key=ctrl_shift_f4); the guide lights moved to Ctrl+Shift+F6')
    parser.add_argument('--hdr-look', choices=['none', 'golden', 'punchy'], default='none', help='AgX look (X3M_HDR_LOOK; requires --hdr-tonemap; default none)')
    parser.add_argument('--hdr-bloom', action='store_true', help='Replace stock bloom RGB with bloom from the FP16 scene before AgX (X3M_HDR_BLOOM=1; requires --hdr-tonemap and scene hook; default off)')
    parser.add_argument('--bloom-source-clamp', type=float, default=None, metavar='C', help='Decoded-space ceiling on the bloom extraction source only (X3M_BLOOM_SOURCE_CLAMP=C, finite 0 < C <= 64; requires --hdr-bloom; absent keeps today\'s unbounded feed). The pyramid then sees at most code C, so an over-bright emitter (additive bolts at gain 5, overlapping sprites) can no longer feed tens or hundreds of units into the halo and saturate it into a white disk; the presented scene keeps its full HDR value and every source at code C or below is bit-identical to today. Recommended value 1.0, the ceiling of the native A8R8G8B8 scene map the original compositor read (docs/architecture/bloom-falloff.md)')
    parser.add_argument('--hdr-decode', choices=['gamma2.2', 'pow22', 'srgb', 'none'], default='gamma2.2', help='Engine-space decode before the tonemap and the meter (X3M_HDR_DECODE; requires --hdr-tonemap): gamma2.2 (default; pow22 is the same curve), srgb, or none for the A/B against the decoded transform')
    parser.add_argument('--hdr-ev', type=float, default=0.0, help='Exposure offset in EV added to the auto-exposure target (X3M_HDR_EV; requires --hdr-tonemap; default 0)')
    parser.add_argument('--hdr-exposure', choices=['fixed', 'auto'], default=None, help='Exposure policy (X3M_HDR_EXPOSURE; requires --hdr-tonemap when explicit): Auto capped at +1.3 EV by default, or fixed EV0; Ctrl+Shift+F9 switches during play')
    parser.add_argument('--hdr-ev-manual', type=float, default=None, help='Fixed EV instead of auto exposure (X3M_HDR_EV_MANUAL; requires --hdr-tonemap): the meter chain does not run; deterministic; clamped to the EV range (--hdr-ev-min/--hdr-ev-max, -3..+1.3 by default)')
    parser.add_argument('--hdr-ev-min', type=float, default=-3.0, help='Lower clamp of the auto-exposure EV (X3M_HDR_EV_MIN; requires --hdr-tonemap; default -3)')
    parser.add_argument('--hdr-ev-max', type=float, default=1.3, help='Upper clamp of exposure EV (X3M_HDR_EV_MAX; requires --hdr-tonemap; default 1.3)')
    parser.add_argument('--hdr-meter-bg', type=float, default=1.0 / 512.0, help='Tile background floor of the meter in scene-linear units (X3M_HDR_METER_BG; requires --hdr-tonemap): tiles whose geometric-mean luminance is below it are the black sky and excluded from the key rule; default 1/512')
    parser.add_argument('--hdr-white-target', type=float, default=0.9, help='Fraction of the tonemapper\'s white the brightest 1 %% of tiles may reach, the highlight limit of the auto exposure (X3M_HDR_WHITE_TARGET; requires --hdr-tonemap; default 0.9; 0 = no limit)')
    parser.add_argument('--hdr-ev-deadband', type=float, default=0.25, help='Dead band of the auto exposure in EV (X3M_HDR_EV_DEADBAND; requires --hdr-tonemap): the held target moves only when the freshly metered target differs from it by more, so small scene changes while turning do not drift the exposure; default 0.25; 0 = off')
    parser.add_argument('--hdr-edge-weight', type=float, default=0.35, help='Centre weighting of the lit-content statistic (X3M_HDR_METER_EDGE_WEIGHT; requires --hdr-tonemap): the tile weight at the frame corners, 1 at the centre (a raised cosine), so a bright emitter at the edge does not darken what the player looks at; the highlight limit stays unweighted; default 0.35; 1 = unweighted')
    parser.add_argument('--hdr-key-pull', type=float, default=0.25, help='Fraction of the key rule applied when the lit median is brighter than the key, so a bright full frame is pulled down gently instead of to mid-grey (X3M_HDR_KEY_PULL; requires --hdr-tonemap; default 0.25; 1 = the full key rule both ways)')
    parser.add_argument('--hdr-clamp', type=float, default=0.0, help='Clamp of the decoded scene value before the tonemap, the blunt firefly guard (X3M_HDR_CLAMP; requires --hdr-tonemap; default 0 = off)')
    parser.add_argument('--state-shadow', choices=['auto', 'on', 'off'], default='auto', help='Render-state configuration of the route (X3M_STATE_SHADOW): auto (default) leaves the variable unset, so the DLL runs the hybrid unhook, with SetRenderState/SetSamplerState unhooked in production and the state read at the draw, and installs the hooks only under --frame-timing or a failed Get* check; on always installs the hooks and answers the per-draw state queries from the shadow (the run-31 behaviour); off issues a legacy GetRenderState per query (A/B; requires --motion-output)')
    parser.add_argument('--motion-rt-mode', choices=['perdraw', 'lazy'], default='perdraw', help='RT1/RT2 binding policy of the route: perdraw (default) rebinds around every routed draw; lazy keeps the bindings, never the write masks, across consecutive routed draws and installs no SetRenderState/GetRenderState hook (route-per-draw-cost.md lever 3; unflown, read lazy_flushes and lazy_mask_writes on the motion_output_frame line; requires --motion-output)')
    parser.add_argument('--camera', choices=['vanilla', 'chase'], default='vanilla', help='External back view camera (X3M_CAMERA): vanilla (default) patches nothing; chase installs the byte-verified cockpit-update trampoline (0x00420e06, exact executable only, fails closed to vanilla) and replaces the external back view with the critically damped chase camera; internal/front/side views stay vanilla, so the game\'s view keys remain the switch (docs/architecture/chase-camera.md)')
    # Chase tunables are X3M_CHASE_* (review 31 O7): X3M_CAMERA_CUT_DEG / X3M_CAMERA_LOG above belong to the TAA camera read.
    parser.add_argument('--chase-rot-tau', type=float, default=None, help='Chase camera orientation spring time constant in seconds (X3M_CHASE_ROT_TAU; default 0.28; requires --camera chase)')
    parser.add_argument('--chase-pos-tau', type=float, default=None, help='Chase camera boom spring time constant in seconds (X3M_CHASE_POS_TAU; default 0.38)')
    parser.add_argument('--chase-offset-y', type=float, default=None, help='Fraction of the half screen height the ship sits below centre, -1..1 (X3M_CHASE_OFFSET_Y; default 0.50, about 75%% screen height from a centred native anchor; negative puts the ship above centre)')
    parser.add_argument('--chase-pitch-down-deg', type=float, default=None, help='Downward look in degrees relative to ship forward, 0..30 (X3M_CHASE_PITCH_DOWN_DEG; default 0.5, the near-parallel elevated framing; 0 restores legacy framing geometry)')
    parser.add_argument('--chase-distance-scale', type=float, default=None, help='Multiplier of the vanilla boom length (X3M_CHASE_DISTANCE_SCALE; default 0.9)')
    parser.add_argument('--chase-lag-clamp-deg', type=float, default=None, help='Maximum orientation lag in degrees, the ship-on-screen window (X3M_CHASE_LAG_CLAMP_DEG; default 8)')
    parser.add_argument('--chase-pos-lag-clamp', type=float, default=None, help='Maximum boom lag as a fraction of the boom length, 0..1 (X3M_CHASE_POS_LAG_CLAMP; default 0.10)')
    parser.add_argument('--chase-combat-tightness', type=float, default=None, help='0..1: while the cockpit reports a target lock (+0x1e4 tracking mode 1/4 with a tracked object; unverified in game) both spring time constants are scaled by (1 - tightness) (X3M_CHASE_COMBAT_TIGHTNESS; default 0 = off)')
    parser.add_argument('--chase-scene-fix', action='store_true', help='Also re-express the layer-0 cockpit-scene camera through the smoothed view each applied frame (X3M_CHASE_SCENE_FIX=1; default off until the first run shows an external-view HUD element rendered there; review 31 A5)')
    parser.add_argument('--chase-view-restore', action='store_true', help='After a gate jump or jumpdrive, restore the rear chase view (mode 258) that the engine resets to the internal view: one-use ticket written into the live script assignment at the optimized store seam 0x004a3ffd under the full arm/pending/consume proof, with seven byte-verified cancellation sites (X3M_CHASE_VIEW_RESTORE=1; default off, patches nothing; requires --camera chase; docs/reverse-engineering/chase-view-transition.md)')
    parser.add_argument('--chase-hud-anchor', choices=['forward', 'centre'], default='centre', help='Place the admitted chase HUD group at the ship-forward vanishing point or retain its native centre placement (X3M_CHASE_HUD_ANCHOR; default centre; forward requires --camera chase)')
    parser.add_argument('--voice-decoder', type=Path, default=None, metavar='DIR', help='launch only, opt-in, default off: deliver the process-local WMA decoder plugin built in DIR to this one game process by setting GST_PLUGIN_PATH_1_0=DIR/runtime/plugins and GST_REGISTRY_1_0=DIR/registry/x3-arm64.bin in its environment. Nothing is written into the application, the bottle or any global configuration, no DYLD_LIBRARY_PATH and no unversioned GStreamer variable is touched; only DIR/registry is created if missing. Also sets X3M_VOICE_DMO_FALLBACK=1 so the proxy re-initialises the DMO wrapper the game creates with the registered WMA decoder DMO when the speech decoder class is unregistered (byte-verified hook at 0x004cfd46, inert where Init succeeds; docs/architecture/voice-decoder-adapter.md)')
    parser.add_argument('--lod-scale', type=float, default=None, metavar='FACTOR', help='Push the engine\'s mesh LOD switch distances out by FACTOR, 1..4 (X3M_LOD_SCALE; default absent = vanilla; no other option needed): the LOD threshold multiplier read at 0x0047d44b is replaced by a proxy-owned mirror holding the game\'s value divided by FACTOR (same-length instruction, exact executable and bytes only, otherwise fails closed to vanilla; one lod_scale line in the session log). Cost: about 4-7x the triangles and 13-15x the draw calls per distant station body at 2-3x; the cap of 4 keeps the integer-truncated thresholds away from collapse (docs/architecture/lod-scale.md)')
    parser.add_argument('--point-light-root-admission', action='store_true', help='Admit a point light for a mesh node whose root object is in range, not only when the node itself is (X3M_POINT_LIGHT_ROOT_ADMISSION=1; default absent = vanilla per-node cull): the six-byte range-test branch at 0x004c27af is replaced by a detour that keeps the native decision for an in-range node and otherwise walks the node\'s parent chain (at most 8 bounds-checked hops) and applies the same range predicate to the root; exact executable and bytes only, otherwise fails closed to vanilla; one point_light_root_admission line in the session log (docs/reverse-engineering/camera-and-lights.md, "Point-light admission site")')
    parser.add_argument('--cull-census', action='store_true', help='Log the engine\'s own cull/LOD census on F8 capture frames (X3M_CULL_CENSUS=1; default absent = nothing patched): two read-only trampolines on the per-node cull/LOD pass 0x0047cfe0 record, per node, the LOD metric s = r*640/D, the small-object measure, the two per-node thresholds, the cull verdict and the selected LOD index into a bounded ring (8192 entries, overflow= counted), emitted as cull_census rows at Present; outside capture frames each stub is one compare and a dead branch. Exact executable and bytes only, otherwise fails closed to vanilla; summarise with tools/analysis/cull_census.py (docs/reverse-engineering/lod-selection.md, "Cull census sites")')
    parser.add_argument('--collide-box-cull', action='store_true', help='Insert the missing integer bounding-box early-out in the engine\'s sector collision pass (X3M_COLLIDE_BOX_CULL=1; default absent = nothing patched): two trampolines at the square-root pair tests 0x0045d58e (all-pairs loop of 0x0045d250) and 0x0045cc7c (swept scan of 0x0045cab0) jump to the engine\'s own continue label when max(|dx|,|dy|,|dz|) exceeds the engine\'s reject radius plus a margin that covers its float32 and truncation error, so only pairs the engine\'s own compare discards are skipped; class-7 pairs always take the engine path. Counts pairs and box rejects per frame (collide_census line per 300 frames, collide_census_frame on F8 frames); compare loop_phases collide_p50_us with the option on and off (--loop-phases). Exact executable and bytes only, otherwise fails closed to vanilla (docs/reverse-engineering/sector-collide.md, section 10)')
    parser.add_argument('--collide-narrow-census', action='store_true', help='One-flight diagnostic of the sector collision narrow phase (X3M_COLLIDE_NARROW_CENSUS=1; default absent = nothing patched; independent of --collide-box-cull): the call 0x0045d665 -> 0x0048ac80 is bracketed per accepted pair (objects, class/subtype/model, positions, transform hash, result, BVH node-pair visits, microseconds) into a 256-entry ring, 0x0048a9a5 counts mesh-pair tests, the entry of 0x004e2530 counts BVH node-pair visits and the entry of 0x004e2190 counts leaf triangle tests. One collide_narrow line per 300 frames (accepted / mesh_pairs / node_pairs / narrow_us / tri_tests p50, max, sum; the share of pairs a cross-frame no-contact memo would answer) and, on F8 frames, one collide_narrow_pair row per accepted pair ordered by visits. Exact executable and bytes only, otherwise fails closed to vanilla (docs/reverse-engineering/sector-collide.md, section 11.7)')
    parser.add_argument('--no-collide-sat-sse2', dest='collide_sat_sse2', action='store_false', default=None, help='Turn the SSE2 separating-axis test off (it is on by default on a modded launch)')
    parser.add_argument('--collide-sat-sse2', dest='collide_sat_sse2', action='store_true', default=None, help='[launcher default on modded launches; --no-collide-sat-sse2 = off; not forwarded under --vanilla unless given] Replace the engine\'s x87 OBB-OBB separating-axis test of the collision BVH descent with an SSE2 reimplementation (X3M_COLLIDE_SAT_SSE2=1; default absent = nothing patched; independent of --collide-box-cull and --collide-narrow-census): the sole call of 0x004e3280, at 0x004e25a3, is redirected. The box test only prunes the descent and the replacement separates only where the engine\'s compare separates with a 2^-20 relative margin to spare, and on an unordered (NaN) compare exactly as the engine does, so the same contacts are found. No counters of its own: fly it with --collide-narrow-census and compare narrow_us / node_pairs / tri_tests with the option on and off. Exact executable and bytes only, otherwise fails closed to vanilla (docs/reverse-engineering/sector-collide.md, section 12.8)')
    parser.add_argument('--no-collide-memo', dest='collide_memo', action='store_false', default=None, help='Turn the no-contact collision memo off (it is on by default on a modded launch); refused together with --collide-memo-verify')
    parser.add_argument('--collide-memo', dest='collide_memo', action='store_true', default=None, help='[launcher default on modded launches; --no-collide-memo = off; not forwarded under --vanilla unless given] Answer a mesh-pair collision query from a memo when its complete input (both transforms and scales, models with a content stamp, flags, contact cap, tolerance, running minimum) equals, bit for bit, a query that found no contact in this or the previous frame (X3M_COLLIDE_MEMO=1; default absent = nothing patched; independent of --collide-sat-sse2, --collide-box-cull and --collide-narrow-census): the sole call of 0x004e29f0, at 0x0047f329, is redirected. Contacts are never stored, so every contact is computed; nothing is capped or skipped on a stride. One collide_memo line per 300 frames (hits, skipped_visits). Exact executable and bytes only, otherwise fails closed to vanilla (docs/reverse-engineering/sector-collide.md, section 14)')
    parser.add_argument('--collide-memo-verify', action='store_true', help='Diagnostic form of --collide-memo (implies it; X3M_COLLIDE_MEMO_VERIFY=1): nothing is skipped, every query that would have been answered from the memo runs in the engine as well and the two are compared; verify_mismatches in the collide_memo line must stay 0. Costs what vanilla costs: for one flight')
    parser.add_argument('--cull-small-parts', type=float, default=None, metavar='PX', help='Cull mesh nodes whose projected radius is under PX pixels, 0 < PX <= 64 (X3M_CULL_SMALL_PARTS_PX; launcher default 2 on every modded launch, --cull-small-parts 0 = off, nothing patched; --vanilla forwards nothing and the DLL\'s own fallback stays off): one trampoline on the per-node cull/LOD pass 0x0047cfe0 at 0x0047d2a2 sends a node whose engine metric s = r*640/D is below the per-frame threshold (PX converted with the live projection scale and the back-buffer width, the cull-census bucket rule) down the engine\'s own size-cull instruction at 0x0047d2c3; every other node runs the vanilla compare. Run131 census at the run117 station view: 2 px = 403 of the 878 census-attributed draws (901 in the frame; about 9.6 ms at 23.7 us/draw), 4 px = 458; lower bounds, because a culled node also culls its 0x40000-flagged children (0x0047d055). The threshold applies in every view (small casters leave the shadow and env maps too) and is scaled by the one main-view projection. Exact executable and bytes only, otherwise fails closed to vanilla; risk: popping of thin parts (antennas, clamps) whose radius is small, cascading to their descendants (none seen at 2 px in run 43 B) (docs/architecture/engine-frame-time.md 2.3, docs/reverse-engineering/lod-selection.md "Cull small parts site")')
    parser.add_argument('--cull-small-parts-scope', choices=('all', 'bodies'), default=None, help='Which nodes --cull-small-parts may cull (X3M_CULL_SMALL_PARTS_SCOPE; default all; refused when the cull is off, enables nothing on its own). bodies = only nodes without a parent link ([node+0x18] == 0, the test the displaced instruction already performs): whole distant objects, which at 2 px are invisible anyway, while the glowing sub-parts of nearer stations (a few px, visible) stay. all = every node under the threshold. Fixture replay of the run131 rows at 2 px: all 97 nodes / 403 draws, bodies 89 / 395 (body-flagged rows; the rows carry no parent link, docs/verification/cull-small-parts.md). Run 43 B: at 2 px `all` took the busy view from 884 to 477 draws and ~30 to ~42 fps with no visible pop-in, while `bodies` culled 36 nodes/frame and saved nothing (nearly every small node has a parent), so `all` is the default in the launcher and in the DLL')
    parser.add_argument('--dry-run', action='store_true', help='launch only: validate the options and installation, print the command and X3M_* environment as JSON, and exit without launching')
    args = parser.parse_args()
    if args.dry_run and args.action != 'launch':
        parser.error('--dry-run applies to launch only.')
    if args.voice_decoder is not None and args.action != 'launch':
        parser.error('--voice-decoder applies to launch only.')
    if args.fex_tso is not None and args.action != 'launch':
        parser.error('--fex-tso applies to launch only.')
    if args.wined3d is not None:
        if args.action != 'launch':
            parser.error('--wined3d applies to launch only.')
        if not re.fullmatch(r'[A-Za-z0-9_]+=[A-Za-z0-9_x]+(;[A-Za-z0-9_]+=[A-Za-z0-9_x]+)*', args.wined3d):
            parser.error("--wined3d: expected key=value settings separated by ';', e.g. 'csmt=0x0;renderer=vulkan'.")
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
    if args.game_phase_threshold_ms != 20 and not args.game_phases:
        parser.error('--game-phase-threshold-ms requires --game-phases.')
    if not 1 <= args.game_phase_threshold_ms <= 10000:
        parser.error('--game-phase-threshold-ms must be between 1 and 10000.')
    if args.telemetry_draw and not args.telemetry:
        parser.error('--telemetry-draw requires --telemetry.')
    if args.frame_timing and not args.telemetry:
        parser.error('--frame-timing requires --telemetry.')
    if args.frame_phases and not args.telemetry:
        parser.error('--frame-phases requires --telemetry.')
    if args.residual_phases and not args.telemetry:
        parser.error('--residual-phases requires --telemetry.')
    if args.residual_phases:  # pairs with both stamp groups, so it turns them on
        args.frame_phases = args.pass_phases = True
    if args.pass_phases and not (args.telemetry and args.frame_phases):
        parser.error('--pass-phases requires --telemetry and --frame-phases.')
    if args.submit_phases and not (args.telemetry and args.frame_phases):
        parser.error('--submit-phases requires --telemetry and --frame-phases.')
    if args.loop_phases and not (args.telemetry and args.frame_phases):
        parser.error('--loop-phases requires --telemetry and --frame-phases.')
    if args.frame_timing_state_stamps and not args.frame_timing:
        parser.error('--frame-timing-state-stamps requires --frame-timing.')
    if not 0 <= args.frame_timing_state_stamps <= 100000:
        parser.error('--frame-timing-state-stamps must be between 0 and 100000.')
    if not 1 <= args.frame_end_stride <= 100000:
        parser.error('--frame-end-stride must be within [1, 100000].')
    if args.media_cue_trace and not args.telemetry:
        parser.error('--media-cue-trace requires --telemetry.')
    if args.media_cue_retry_s != 30 and args.media_cue_cache != 'on':
        parser.error('--media-cue-retry-s requires --media-cue-cache on.')
    if not 1 <= args.media_cue_retry_s <= 3600:
        parser.error('--media-cue-retry-s must be between 1 and 3600.')
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
    # `not lo <= v <= hi` also rejects NaN.
    if args.taa_current_filter is not None and not args.taa:
        parser.error('--taa-current-filter requires --taa.')
    if args.taa_current_filter is not None and not 0.0 <= args.taa_current_filter <= 4.0:
        parser.error('--taa-current-filter must be within [0, 4].')
    if args.taa_line_filter is not None and not args.taa:
        parser.error('--taa-line-filter requires --taa.')
    if args.taa_line_filter is not None:
        amount, comma, width = args.taa_line_filter.partition(',')
        try:
            amount = float(amount)
        except ValueError:
            amount = float('nan')
        if not 0.0 <= amount <= 4.0 or width not in (('1', '2') if comma else ('',)):
            parser.error('--taa-line-filter takes A[,W]: A within [0, 4], W 1 or 2.')
        if amount > 0 and args.taa_current_filter:
            parser.error('--taa-line-filter and --taa-current-filter exclude each other (the global filter already covers every pixel).')
        args.taa_line_filter = '%.5g' % amount + (',' + width if width else '')
    if args.taa_far_stabiliser is not None:
        if not args.taa:
            parser.error('--taa-far-stabiliser requires --taa.')
        try:
            far = [float(field) for field in args.taa_far_stabiliser.split(',')]
        except ValueError:
            far = []
        if len(far) not in (1, 2, 4, 6):
            parser.error('--taa-far-stabiliser takes W[,A[,F0,F1[,LO,HI]]].')
        far += [0.0, 80.0, 130.0, 0.03, 0.25][len(far) - 1:]
        base_weight = args.taa_history_weight if args.taa_history_weight is not None else 0.9
        if not (far[0] == 0.0 or base_weight <= far[0] <= 0.99) or not 0.0 <= far[1] <= 4.0 or not 0.0 < far[2] < far[3] <= 1e6 or not 0.0 <= far[4] < far[5] <= 64.0:
            parser.error('--taa-far-stabiliser: W is 0 or within [history weight, 0.99], A within [0, 4], 0 < F0 < F1 <= 1e6, 0 <= LO < HI <= 64.')
        if far[0] > 0 or far[1] > 0:
            if args.taa_adaptive_weight is not None:
                parser.error('--taa-far-stabiliser and --taa-adaptive-weight exclude each other (one history-weight gate).')
            if args.taa_current_filter:
                parser.error('--taa-far-stabiliser and --taa-current-filter exclude each other (the global filter already covers every pixel).')
            if far[1] > 0 and args.taa_line_filter is not None and float(args.taa_line_filter.partition(',')[0]) not in (0.0, far[1]):
                parser.error('--taa-far-stabiliser A must equal --taa-line-filter A when both are given (one Gaussian per frame).')
        args.taa_far_stabiliser = ','.join('%.6g' % value for value in far)
    if args.taa_history_weight is not None and not args.taa:
        parser.error('--taa-history-weight requires --taa.')
    if args.taa_history_weight is not None and not 0.5 <= args.taa_history_weight <= 0.98:
        parser.error('--taa-history-weight must be within [0.5, 0.98].')
    if (args.taa_thin_clip is not None or args.taa_adaptive_weight is not None or args.taa_alpha_history) and not args.taa:
        parser.error('--taa-thin-clip, --taa-adaptive-weight and --taa-alpha-history require --taa.')
    if args.taa_thin_clip is not None and not 0.0 <= args.taa_thin_clip <= 1.0:
        parser.error('--taa-thin-clip must be within [0, 1].')
    if args.taa_adaptive_weight is not None:
        try:
            adaptive = [float(part) for part in args.taa_adaptive_weight.split(',')]
        except ValueError:
            adaptive = []
        if len(adaptive) not in (1, 3):
            parser.error('--taa-adaptive-weight takes WMAX or WMAX,LO,HI.')
        history_weight = 0.9 if args.taa_history_weight is None else args.taa_history_weight
        if not max(history_weight, 0.5) <= adaptive[0] <= 0.99:
            parser.error('--taa-adaptive-weight WMAX must be within [history weight, 0.99].')
        if len(adaptive) == 3 and not 0.0 <= adaptive[1] < adaptive[2] <= 64.0:
            parser.error('--taa-adaptive-weight needs 0 <= LO < HI <= 64 px/frame.')
        if not (args.taa_thin_clip is not None and args.taa_thin_clip > 0.0):
            parser.error('--taa-adaptive-weight requires --taa-thin-clip > 0 (alone it dims thin lattices).')
        # Short fixed format: the DLL reads the value through a 32-character buffer (three components <= 23 characters).
        args.taa_adaptive_weight = ','.join('%.5g' % value for value in adaptive)
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
    if args.sun_shadow_lane and not (args.motion_output and args.taa and args.hdr):
        parser.error('--sun-shadow-lane requires --motion-output --taa --hdr.')
    if args.sun_shadow_apply and not (args.sun_shadow_lane and args.shadow_replay_depth):
        parser.error('--sun-shadow-apply requires --sun-shadow-lane --shadow-replay-depth.')
    if args.sun_shadow_receiver_depth == 'device':
        parser.error('--sun-shadow-receiver-depth device is gone: the lane\'s RT2 is always A32B32G32R32F with the linear receiver depth (docs/architecture/shadow-receiver-depth.md). Drop the option (linear is the only encoding).')
    if args.shadow_replay_candidates and not (args.motion_output and args.ownership):
        parser.error('--shadow-replay-candidates requires --motion-output --ownership.')
    if args.shadow_replay_depth and not (args.motion_output and args.ownership):
        parser.error('--shadow-replay-depth requires --motion-output --ownership.')
    if args.shadow_replay_size is not None and not args.shadow_replay_depth:
        parser.error('--shadow-replay-size requires --shadow-replay-depth.')
    if args.shadow_replay_size is not None and not 64 <= args.shadow_replay_size <= 4096:
        parser.error('--shadow-replay-size must be within [64, 4096].')
    if (args.shadow_replay_extent is not None or args.shadow_replay_depth_half is not None) and not args.shadow_replay_depth:
        parser.error('--shadow-replay-extent and --shadow-replay-depth-half require --shadow-replay-depth.')
    if args.shadow_replay_extent is not None and not (math.isfinite(args.shadow_replay_extent) and 50.0 <= args.shadow_replay_extent <= 4000.0):
        parser.error('--shadow-replay-extent must be within [50, 4000].')
    if args.shadow_replay_depth_half is not None and not (math.isfinite(args.shadow_replay_depth_half) and 128.0 <= args.shadow_replay_depth_half <= 8192.0):
        parser.error('--shadow-replay-depth-half must be within [128, 8192].')
    if args.shadow_replay_cap is not None and not (args.shadow_replay_candidates or args.shadow_replay_depth):
        parser.error('--shadow-replay-cap requires --shadow-replay-candidates or --shadow-replay-depth.')
    if args.shadow_replay_cap is not None and not 1 <= args.shadow_replay_cap <= 1024:
        parser.error('--shadow-replay-cap must be within [1, 1024].')
    # Sun-shadow cascades (docs/architecture/shadow-cascades.md; the DLL's defaults
    # are the single named constants of src/renderer/shadow_replay_projection.h).
    SHADOW_CASCADE_EXTENTS = '250,1500,7500,25000'
    SHADOW_CASCADE_MAX, SHADOW_CASCADE_EXTENT_RANGE, SHADOW_CASCADE_BUDGET_RANGE = 5, (50.0, 150000.0), (1, 4096)
    SHADOW_CASCADE_ADAPTIVE_RANGE = (0.5, 8.0)  # shadow_cascade_adaptive_k_min/max
    SHADOW_CASCADE_LADDER_RANGE = (2.0, 16.0)  # shadow_cascade_ladder_ratio_min/max
    def shadow_cascade_env(parser, args):
        """Validates --shadow-cascades and its companions; returns their environment
        ('X3M_SHADOW_CASCADES': '0' when off, the companions only when given)."""
        companions = (('--shadow-cascade-sizes', args.shadow_cascade_sizes), ('--shadow-cascade-caps', args.shadow_cascade_caps),
                      ('--shadow-cascade-budget', args.shadow_cascade_budget), ('--shadow-sun-poll', args.shadow_sun_poll),
                      ('--shadow-cascade-records', args.shadow_cascade_records), ('--shadow-cascade-static-from', args.shadow_cascade_static_from),
                      ('--shadow-cascade-drop-order', args.shadow_cascade_drop_order), ('--shadow-cascade-large-min', args.shadow_cascade_large_min),
                      ('--shadow-cascade-adaptive-c0', args.shadow_cascade_adaptive_c0), ('--shadow-cascade-ladder-ratio', args.shadow_cascade_ladder_ratio),
                      ('--shadow-sun-trace', args.shadow_sun_trace or None),
                      ('--shadow-cascade-backface-from', args.shadow_cascade_backface_from))
        if args.shadow_cascades is None:
            for option, value in companions:
                if value is not None:
                    parser.error(f'{option} requires --shadow-cascades.')
            return {'X3M_SHADOW_CASCADES': '0', 'X3M_SHADOW_SUN_POLL': '0', 'X3M_SHADOW_SUN_TRACE': '0'}
        if not args.shadow_replay_depth:
            parser.error('--shadow-cascades requires --shadow-replay-depth.')
        try:
            extents = [float(v) for v in (SHADOW_CASCADE_EXTENTS if args.shadow_cascades == 'default' else args.shadow_cascades).split(',')]
        except ValueError:
            extents = []
        low, high = SHADOW_CASCADE_EXTENT_RANGE
        if not 1 <= len(extents) <= SHADOW_CASCADE_MAX or not all(math.isfinite(e) and low <= e <= high for e in extents) \
                or any(b <= a for a, b in zip(extents, extents[1:])):
            parser.error(f'--shadow-cascades takes 1..{SHADOW_CASCADE_MAX} ascending half-extents within [{low:g}, {high:g}].')
        env = {'X3M_SHADOW_CASCADES': ','.join(repr(e) for e in extents), 'X3M_SHADOW_SUN_POLL': '0' if args.shadow_sun_poll == 'off' else '1',
               'X3M_SHADOW_SUN_TRACE': '1' if args.shadow_sun_trace else '0'}

        def integers(option, text, low, high):
            try:
                values = [int(v) for v in text.split(',')]
            except ValueError:
                values = []
            if len(values) not in (1, len(extents)) or not all(low <= v <= high for v in values):
                parser.error(f'{option} takes one value or one per cascade within [{low}, {high}].')
            return ','.join(str(v) for v in values)
        if args.shadow_cascade_sizes is not None:
            env['X3M_SHADOW_CASCADE_SIZES'] = integers('--shadow-cascade-sizes', args.shadow_cascade_sizes, 64, 4096)
        if args.shadow_cascade_caps is not None:
            env['X3M_SHADOW_CASCADE_CAPS'] = integers('--shadow-cascade-caps', args.shadow_cascade_caps, 1, 4096)
        if args.shadow_cascade_records is not None:
            env['X3M_SHADOW_CASCADE_RECORDS'] = integers('--shadow-cascade-records', args.shadow_cascade_records, 1, 4096)
        if args.shadow_cascade_static_from is not None:
            if not 1 <= args.shadow_cascade_static_from <= len(extents) - 1:
                parser.error(f'--shadow-cascade-static-from must be within [1, cascades-1] ({len(extents)} cascades configured); cascade 0 always admits moving casters.')
            env['X3M_SHADOW_CASCADE_STATIC_FROM'] = str(args.shadow_cascade_static_from)
        if args.shadow_cascade_drop_order is not None:
            env['X3M_SHADOW_CASCADE_DROP_ORDER'] = args.shadow_cascade_drop_order
        if args.shadow_cascade_backface_from is not None:
            value = args.shadow_cascade_backface_from.strip().lower()
            if value != 'none':
                if not value.isdigit() or not 0 <= int(value) <= len(extents) - 1:
                    parser.error(f'--shadow-cascade-backface-from must be within [0, cascades-1] ({len(extents)} cascades configured) or none.')
                value = str(int(value))
            env['X3M_SHADOW_CASCADE_BACKFACE_FROM'] = value
        if args.shadow_cascade_large_min is not None:
            if not (math.isfinite(args.shadow_cascade_large_min) and 0.0 <= args.shadow_cascade_large_min <= 1000000.0):
                parser.error('--shadow-cascade-large-min must be within [0, 1000000].')
            env['X3M_SHADOW_CASCADE_LARGE_MIN'] = repr(args.shadow_cascade_large_min)
        if args.shadow_cascade_budget is not None:
            low, high = SHADOW_CASCADE_BUDGET_RANGE
            if not low <= args.shadow_cascade_budget <= high:
                parser.error('--shadow-cascade-budget must be within [1, 4096].')
            env['X3M_SHADOW_CASCADE_BUDGET'] = str(args.shadow_cascade_budget)
        if args.shadow_cascade_adaptive_c0 is not None:
            low, high = SHADOW_CASCADE_ADAPTIVE_RANGE
            if not (math.isfinite(args.shadow_cascade_adaptive_c0) and low <= args.shadow_cascade_adaptive_c0 <= high):
                parser.error('--shadow-cascade-adaptive-c0 must be within [0.5, 8].')
            env['X3M_SHADOW_CASCADE_ADAPTIVE_C0'] = repr(args.shadow_cascade_adaptive_c0)
        if args.shadow_cascade_ladder_ratio is not None:
            if args.shadow_cascade_adaptive_c0 is None:
                parser.error('--shadow-cascade-ladder-ratio requires --shadow-cascade-adaptive-c0.')
            low, high = SHADOW_CASCADE_LADDER_RANGE
            if not (math.isfinite(args.shadow_cascade_ladder_ratio) and low <= args.shadow_cascade_ladder_ratio <= high):
                parser.error('--shadow-cascade-ladder-ratio must be within [2, 16].')
            env['X3M_SHADOW_CASCADE_LADDER_RATIO'] = repr(args.shadow_cascade_ladder_ratio)
        return env
    args.shadow_cascade_env = shadow_cascade_env(parser, args)
    # Caster retention (docs/architecture/shadow-caster-retention.md): rides the cascades.
    if (args.shadow_retention_census or args.shadow_caster_retention) and args.shadow_cascades is None:
        parser.error('--shadow-retention-census and --shadow-caster-retention require --shadow-cascades.')
    if (args.shadow_caster_retention_age is not None or args.shadow_caster_retention_eps is not None or args.shadow_retention_timing) and not (args.shadow_retention_census or args.shadow_caster_retention):
        parser.error('--shadow-caster-retention-age, --shadow-caster-retention-eps and --shadow-retention-timing require --shadow-retention-census or --shadow-caster-retention.')
    if args.shadow_caster_retention_age is not None and not 1 <= args.shadow_caster_retention_age <= 10000000:
        parser.error('--shadow-caster-retention-age must be within [1, 10000000].')
    if args.shadow_caster_retention_eps is not None and not (math.isfinite(args.shadow_caster_retention_eps) and 1e-4 <= args.shadow_caster_retention_eps <= 100.0):
        parser.error('--shadow-caster-retention-eps must be within [0.0001, 100].')
    if args.sun_shadow_bias_units is not None and not args.sun_shadow_apply:
        parser.error('--sun-shadow-bias-units requires --sun-shadow-apply.')
    if args.sun_shadow_bias_units is not None and not (math.isfinite(args.sun_shadow_bias_units) and 0.0 <= args.sun_shadow_bias_units <= 1000.0):
        parser.error('--sun-shadow-bias-units must be within [0, 1000].')
    if args.sun_shadow_bias_clamp_texels is not None and not args.sun_shadow_apply:
        parser.error('--sun-shadow-bias-clamp-texels requires --sun-shadow-apply.')
    if args.sun_shadow_bias_clamp_texels is not None and not (math.isfinite(args.sun_shadow_bias_clamp_texels) and 1.0 <= args.sun_shadow_bias_clamp_texels <= 64.0):
        parser.error('--sun-shadow-bias-clamp-texels must be within [1, 64].')
    if args.sun_shadow_bias_slope_texels is not None and not args.sun_shadow_apply:
        parser.error('--sun-shadow-bias-slope-texels requires --sun-shadow-apply.')
    if args.sun_shadow_bias_slope_texels is not None and not (math.isfinite(args.sun_shadow_bias_slope_texels) and 0.0 <= args.sun_shadow_bias_slope_texels <= 8.0):
        parser.error('--sun-shadow-bias-slope-texels must be within [0, 8].')
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
    if args.screen_emission_additive_alpha is not None and args.screen_emission_additive is None:
        parser.error('--screen-emission-additive-alpha requires --screen-emission-additive.')
    if args.screen_emission_additive_alpha is not None and not (math.isfinite(args.screen_emission_additive_alpha) and 0.0 <= args.screen_emission_additive_alpha <= 1.0):
        parser.error('--screen-emission-additive-alpha must be finite and within [0, 1].')
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
    if args.hull_emission_gain is not None and not hull_emitters_requested(args):
        parser.error('--hull-emission-gain requires --hull-emitters (or an --emission-source-gain above 1, which implies it).')
    if args.hull_emission_gain is not None and not (math.isfinite(args.hull_emission_gain) and 1.0 < args.hull_emission_gain <= 8.0):
        parser.error('--hull-emission-gain must be finite and within (1, 8]: gain 1 is the native program (no variant), so an explicit hull gain must be above 1; omit --hull-emitters for off.')
    if args.hull_emitters and args.hull_emission_gain is None and args.emission_source_gain is None:
        parser.error('--hull-emitters requires a gain: --hull-emission-gain G, or --emission-source-gain G whose value it then takes.')
    if args.hull_emitters and not args.hdr:
        parser.error('--hull-emitters requires --hdr.')
    if args.effect_source_gain is not None:
        parser.error('--effect-source-gain was removed: the single --emission-source-gain G now covers all twenty emission pairs (engines, gate and effect sprites).')
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
    if args.original_fill is not None and not args.hdr:
        parser.error('--original-fill requires --hdr.')
    if args.original_fill is not None and args.linear_materials:
        parser.error('--original-fill excludes --linear-materials (the converted programs take --material-fill instead).')
    if args.original_fill is not None and not (math.isfinite(args.original_fill) and 0.0 <= args.original_fill <= 0.5):
        parser.error('--original-fill must be finite and within [0, 0.5].')
    if args.hull_lightmap_gain is not None and not args.hdr:
        parser.error('--hull-lightmap-gain requires --hdr.')
    if args.hull_lightmap_gain is not None and args.linear_materials:
        parser.error('--hull-lightmap-gain excludes --linear-materials (the converted programs take --lightmap-emissive-gain instead).')
    if args.hull_lightmap_gain is not None and not (math.isfinite(args.hull_lightmap_gain) and 1.0 <= args.hull_lightmap_gain <= 8.0):
        parser.error('--hull-lightmap-gain must be finite and within [1, 8].')
    if args.hdr_bloom and (not args.hdr_tonemap or args.scene_hook == 'off'):
        parser.error('--hdr-bloom requires --hdr-tonemap and the scene hook.')
    if args.bloom_source_clamp is not None and not args.hdr_bloom:
        parser.error('--bloom-source-clamp requires --hdr-bloom.')
    if args.bloom_source_clamp is not None and not (math.isfinite(args.bloom_source_clamp)
                                                   and 0.0 < args.bloom_source_clamp <= 64.0):
        parser.error('--bloom-source-clamp must be finite and within (0, 64].')
    if not args.hdr_tonemap and (args.hdr_exposure is not None or args.hdr_look != 'none' or args.hdr_decode != 'gamma2.2' or args.hdr_ev != 0.0 or args.hdr_ev_manual is not None or args.hdr_clamp != 0.0
                                 or args.hdr_ev_min != -3.0 or args.hdr_ev_max != 1.3 or args.hdr_meter_bg != 1.0 / 512.0 or args.hdr_white_target != 0.9 or args.hdr_key_pull != 0.25
                                 or args.hdr_ev_deadband != 0.25 or args.hdr_edge_weight != 0.35):
        parser.error('--hdr-exposure, --hdr-look, --hdr-decode, --hdr-ev, --hdr-ev-manual, --hdr-clamp, --hdr-ev-min/max, --hdr-meter-bg, --hdr-white-target, --hdr-key-pull, --hdr-ev-deadband and --hdr-edge-weight require --hdr-tonemap.')
    if not -16.0 <= args.hdr_ev <= 16.0 or (args.hdr_ev_manual is not None and not -16.0 <= args.hdr_ev_manual <= 16.0) or not 0.0 <= args.hdr_clamp <= 65504.0:
        parser.error('--hdr-ev and --hdr-ev-manual must be within [-16, 16], --hdr-clamp within [0, 65504].')
    if not -16.0 <= args.hdr_ev_min <= args.hdr_ev_max <= 16.0 or not 1e-4 <= args.hdr_meter_bg <= 64.0 or not 0.0 <= args.hdr_white_target <= 4.0 or not 0.0 <= args.hdr_key_pull <= 1.0 \
            or not 0.0 <= args.hdr_ev_deadband <= 8.0 or not 0.0 <= args.hdr_edge_weight <= 1.0:
        parser.error('--hdr-ev-min <= --hdr-ev-max within [-16, 16], --hdr-meter-bg within [1e-4, 64], --hdr-white-target within [0, 4], --hdr-key-pull, --hdr-edge-weight within [0, 1], --hdr-ev-deadband within [0, 8].')
    if args.state_shadow == 'off' and not args.motion_output:
        parser.error('--state-shadow off requires --motion-output.')
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
    # 0 is off; otherwise the value must survive the DLL's fixed-point parser ([+]digits[.digits], (0, 64]).
    if args.cull_small_parts is not None and not (math.isfinite(args.cull_small_parts) and (args.cull_small_parts == 0.0 or 0.0001 <= args.cull_small_parts <= 64.0)):
        parser.error(f'--cull-small-parts out of range: {args.cull_small_parts} (expected 0 or [0.0001, 64])')
    if args.collide_memo is False and args.collide_memo_verify:
        parser.error('--collide-memo-verify implies the memo: it cannot be combined with --no-collide-memo')
    if args.cull_small_parts_scope is not None and not cull_small_parts_px(args):
        parser.error('--cull-small-parts-scope requires a non-zero --cull-small-parts')
    if not 100 <= args.profile_interval_us <= 1000000:
        parser.error('--profile-interval-us must be between 100 and 1000000.')
    if args.gz_buffer_kb != 256 and not args.gz_buffer:
        parser.error('--gz-buffer-kb requires --gz-buffer.')
    if args.loading_intervals and not args.telemetry:
        parser.error('--loading-intervals requires --telemetry (existing loading markers supply the endpoint).')
    if args.loading_probes and not args.telemetry:
        parser.error('--loading-probes requires --telemetry.')
    if args.frame_timing and not args.telemetry:
        parser.error('--frame-timing requires --telemetry.')
    if args.frame_timing_state_stamps and not args.frame_timing:
        parser.error('--frame-timing-state-stamps requires --frame-timing.')
    if not 0 <= args.frame_timing_state_stamps <= 100000:
        parser.error('--frame-timing-state-stamps must be between 0 and 100000.')
    if not 1 <= args.gz_buffer_kb <= 65536:
        parser.error('--gz-buffer-kb must be between 1 and 65536.')
    if args.taa:
        args.motion_jitter = True
    if args.motion_capture and not 2 <= args.capture_frames <= 8:
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
        env['X3M_GAME_PHASE_THRESHOLD_MS'] = str(args.game_phase_threshold_ms)
        env['X3M_TELEMETRY_DRAW'] = '1' if args.telemetry_draw else '0'
        env['X3M_FRAME_TIMING'] = '1' if args.frame_timing else '0'
        env['X3M_FRAME_END_STRIDE'] = str(args.frame_end_stride)  # explicit, so an inherited value cannot change the cadence
        env['X3M_FPS_OVERLAY'] = '1' if args.fps_overlay else '0'
        env['X3M_FRAME_PHASES'] = '1' if args.frame_phases else '0'  # implied by --residual-phases above
        env['X3M_PASS_PHASES'] = '1' if args.pass_phases else '0'
        env['X3M_RESIDUAL_PHASES'] = '1' if args.residual_phases else '0'
        env['X3M_SUBMIT_PHASES'] = '1' if args.submit_phases else '0'
        env['X3M_LOOP_PHASES'] = '1' if args.loop_phases else '0'
        env['X3M_MEDIA_CUE_TRACE'] = '1' if args.media_cue_trace else '0'
        env['X3M_MEDIA_CUE_CACHE'] = '1' if args.media_cue_cache == 'on' else '0'
        env['X3M_MEDIA_CUE_RETRY_S'] = str(args.media_cue_retry_s)
        env['X3M_FRAME_TIMING_STATE_STAMPS'] = str(args.frame_timing_state_stamps if args.frame_timing else 0)
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
        # The mip bias and the post-resolve sharpen are always forwarded in TAA
        # mode, at the production defaults when unset, so a stale shell value
        # can neither change nor enable them; an explicit 0 disables one and
        # keeps its bit-identical route. Outside TAA mode both are dropped.
        if args.taa:
            env['X3M_TAA_MIP_BIAS'] = repr(TAA_MIP_BIAS_DEFAULT if args.taa_mip_bias is None else args.taa_mip_bias)
            env['X3M_TAA_SHARPEN'] = repr(TAA_SHARPEN_DEFAULT if args.taa_sharpen is None else args.taa_sharpen)
        else:
            env.pop('X3M_TAA_MIP_BIAS', None)
            env.pop('X3M_TAA_SHARPEN', None)
        # The two resolve A/B options are forwarded only when given: a stale
        # shell value can neither enable the filter nor change the weight.
        for name, value in (('X3M_TAA_CURRENT_FILTER', args.taa_current_filter), ('X3M_TAA_HISTORY_WEIGHT', args.taa_history_weight), ('X3M_TAA_LINE_FILTER', args.taa_line_filter), ('X3M_TAA_FAR_STABILISER', args.taa_far_stabiliser),
                            ('X3M_TAA_THIN_CLIP', args.taa_thin_clip), ('X3M_TAA_ADAPTIVE_WEIGHT', args.taa_adaptive_weight),
                            ('X3M_TAA_ALPHA_HISTORY', '1' if args.taa_alpha_history else None)):
            if value is not None:
                env[name] = value if isinstance(value, str) else ('%.5g' % value if name == 'X3M_TAA_THIN_CLIP' else repr(value))
            else:
                env.pop(name, None)
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
        # Absent means the native alpha law: drop the variable entirely (the
        # environment is inherited, so a stale shell value must not survive)
        # rather than exporting an empty value.
        if args.screen_emission_additive_alpha is not None:
            env['X3M_SCREEN_EMISSION_ADDITIVE_ALPHA'] = repr(args.screen_emission_additive_alpha)
        else:
            env.pop('X3M_SCREEN_EMISSION_ADDITIVE_ALPHA', None)
        if args.fade_witness is not None:
            env['X3M_FADE_WITNESS'] = str(args.fade_witness)
        if args.shimmer_trace:
            env['X3M_SHIMMER_TRACE'] = '1'
        # Ambient occlusion: every switch explicit so an inherited value cannot enable it.
        env['X3M_SUN_SHADOW_LANE'] = '1' if args.sun_shadow_lane else '0'
        env['X3M_SUN_SHADOW_APPLY'] = '1' if args.sun_shadow_apply else '0'
        env['X3M_SHADOW_REPLAY_CANDIDATES'] = '1' if (args.shadow_replay_candidates or args.shadow_replay_depth) else '0'
        env['X3M_SHADOW_REPLAY_DEPTH'] = '1' if args.shadow_replay_depth else '0'
        env['X3M_SHADOW_REPLAY_SIZE'] = str(args.shadow_replay_size if args.shadow_replay_size is not None else 1024)
        env['X3M_SHADOW_REPLAY_EXTENT'] = repr(args.shadow_replay_extent if args.shadow_replay_extent is not None else 250.0)
        env['X3M_SHADOW_REPLAY_DEPTH_HALF'] = repr(args.shadow_replay_depth_half if args.shadow_replay_depth_half is not None else 512.0)
        env['X3M_SHADOW_REPLAY_CAP'] = str(args.shadow_replay_cap if args.shadow_replay_cap is not None else 512)
        # Explicit off value ("0") and companions dropped when absent, so an
        # inherited value cannot enable or reshape the cascades.
        for name in ('X3M_SHADOW_CASCADES', 'X3M_SHADOW_CASCADE_SIZES', 'X3M_SHADOW_CASCADE_CAPS', 'X3M_SHADOW_CASCADE_BUDGET',
                     'X3M_SHADOW_CASCADE_RECORDS', 'X3M_SHADOW_CASCADE_STATIC_FROM', 'X3M_SHADOW_CASCADE_DROP_ORDER', 'X3M_SHADOW_CASCADE_LARGE_MIN', 'X3M_SHADOW_CASCADE_ADAPTIVE_C0',
                     'X3M_SHADOW_CASCADE_LADDER_RATIO', 'X3M_SHADOW_SUN_TRACE',
                     'X3M_SHADOW_CASCADE_BACKFACE_FROM'):
            env.pop(name, None)
        env.update(args.shadow_cascade_env)
        # Caster retention: explicit switches, companions only when given.
        env['X3M_SHADOW_RETENTION_CENSUS'] = '1' if args.shadow_retention_census else '0'
        env['X3M_SHADOW_CASTER_RETENTION'] = '1' if args.shadow_caster_retention else '0'
        for name in ('X3M_SHADOW_CASTER_RETENTION_AGE', 'X3M_SHADOW_CASTER_RETENTION_EPS', 'X3M_SHADOW_RETENTION_TIMING'):
            env.pop(name, None)
        if args.shadow_caster_retention_age is not None:
            env['X3M_SHADOW_CASTER_RETENTION_AGE'] = str(args.shadow_caster_retention_age)
        if args.shadow_caster_retention_eps is not None:
            env['X3M_SHADOW_CASTER_RETENTION_EPS'] = repr(args.shadow_caster_retention_eps)
        if args.shadow_retention_timing:
            env['X3M_SHADOW_RETENTION_TIMING'] = '1'
        env['X3M_SUN_SHADOW_BIAS_UNITS'] = repr(args.sun_shadow_bias_units if args.sun_shadow_bias_units is not None else 0.53571875)
        env['X3M_SUN_SHADOW_BIAS_CLAMP_TEXELS'] = repr(args.sun_shadow_bias_clamp_texels if args.sun_shadow_bias_clamp_texels is not None else 20.97152)
        env['X3M_SUN_SHADOW_BIAS_SLOPE_TEXELS'] = repr(args.sun_shadow_bias_slope_texels if args.sun_shadow_bias_slope_texels is not None else 0.2)
        env['X3M_AMBIENT_OCCLUSION'] = '1' if args.ambient_occlusion else '0'
        env['X3M_AO_RADIUS'] = repr(args.ao_radius if args.ao_radius is not None else 2.0)
        env['X3M_AO_STRENGTH'] = repr(args.ao_strength if args.ao_strength is not None else 0.5)
        env['X3M_AO_DEBUG'] = '1' if args.ao_debug else '0'
        env['X3M_AO_TIMING'] = '1' if args.ao_timing else '0'
        env['X3M_EMISSION_GAIN'] = repr(args.emission_gain if args.emission_gain is not None else 1.0)
        env['X3M_EMISSION_SOURCE_GAIN'] = repr(args.emission_source_gain if args.emission_source_gain is not None else 1.0)
        # The guide lights follow the effects gain and its key (Ctrl+Shift+F6):
        # an --emission-source-gain above 1 implies --hull-emitters and hands
        # them its value, so one option covers engines, effects and guide
        # lights; an explicit --hull-emission-gain still overrides the value.
        hull_gain = args.hull_emission_gain if args.hull_emission_gain is not None else args.emission_source_gain
        env['X3M_HULL_EMISSION_GAIN'] = repr(hull_gain if (hull_emitters_requested(args) and hull_gain is not None) else 1.0)
        env['X3M_LINEAR_MATERIALS'] = '1' if args.linear_materials else '0'
        for name, value in material_gains.items():
            env[name] = str(value if value is not None else 1.0)
        env['X3M_MATERIAL_FILL'] = repr(args.material_fill if args.material_fill is not None else (0.05 if args.linear_materials else 0.0))
        # Explicit off value so a stale shell value cannot enable the original fill.
        env['X3M_ORIGINAL_FILL'] = repr(args.original_fill if args.original_fill is not None else 0.0)
        # Always explicit so a stale shell value can neither change nor enable
        # the light-map gain: the production default (4) in HDR mode without
        # the converted route, 1.0 (off) everywhere else, and an explicit
        # --hull-lightmap-gain 1 still turns it off.
        env['X3M_HULL_LIGHTMAP_GAIN'] = repr(args.hull_lightmap_gain if args.hull_lightmap_gain is not None
            else (HULL_LIGHTMAP_GAIN_DEFAULT if args.hdr and not args.linear_materials else 1.0))
        env['X3M_HDR_TONEMAP'] = 'agx' if args.hdr_tonemap else 'identity'
        env['X3M_HDR_BLOOM'] = '1' if args.hdr_bloom else '0'
        # Absent means the unbounded feed: drop the inherited variable entirely
        # rather than exporting an empty value.
        if args.bloom_source_clamp is not None:
            env['X3M_BLOOM_SOURCE_CLAMP'] = repr(args.bloom_source_clamp)
        else:
            env.pop('X3M_BLOOM_SOURCE_CLAMP', None)
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
        # auto leaves the variable unset so the DLL's hybrid unhook decides
        # (docs/architecture/state-call-fast-path.md, "Hybrid unhook"); a stale
        # shell value must not force a configuration, so it is dropped.
        if args.state_shadow == 'auto':
            env.pop('X3M_STATE_SHADOW', None)
        else:
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
        # Point-light root admission: same rule, set only when requested so a
        # stale shell value cannot patch the range-test branch.
        if args.point_light_root_admission:
            env['X3M_POINT_LIGHT_ROOT_ADMISSION'] = '1'
        else:
            env.pop('X3M_POINT_LIGHT_ROOT_ADMISSION', None)
        # Collide box cull: same rule, set only when requested so a stale shell
        # value cannot patch the collision pair tests.
        if args.collide_box_cull:
            env['X3M_COLLIDE_BOX_CULL'] = '1'
        else:
            env.pop('X3M_COLLIDE_BOX_CULL', None)
        # Collide narrow census: same rule (diagnostic; independent of the box cull).
        if args.collide_narrow_census:
            env['X3M_COLLIDE_NARROW_CENSUS'] = '1'
        else:
            env.pop('X3M_COLLIDE_NARROW_CENSUS', None)
        # Collide SAT SSE2 and memo: launcher defaults on a modded launch (collide_default), still dropped from an inherited
        # environment when off.
        if collide_default(args.collide_sat_sse2, args):
            env['X3M_COLLIDE_SAT_SSE2'] = '1'
        else:
            env.pop('X3M_COLLIDE_SAT_SSE2', None)
        # Collide memo: same rule; the verify switch implies the memo and never travels alone.
        if collide_default(args.collide_memo, args) or args.collide_memo_verify:
            env['X3M_COLLIDE_MEMO'] = '1'
        else:
            env.pop('X3M_COLLIDE_MEMO', None)
        if args.collide_memo_verify:
            env['X3M_COLLIDE_MEMO_VERIFY'] = '1'
        else:
            env.pop('X3M_COLLIDE_MEMO_VERIFY', None)
        # Cull census: same rule, set only when requested so a stale shell
        # value cannot patch the cull/LOD pass.
        if args.cull_census:
            env['X3M_CULL_CENSUS'] = '1'
        else:
            env.pop('X3M_CULL_CENSUS', None)
        # Small-parts cull: same rule; 0 is the documented off and is not
        # forwarded, and neither is anything under --vanilla. When the option is
        # unset a modded launch takes the production default (2 px, scope all).
        cull_px = cull_small_parts_px(args)
        if cull_px:
            env['X3M_CULL_SMALL_PARTS_PX'] = f'{cull_px:.4f}'  # fixed-point: the DLL parser takes no exponent form
            env['X3M_CULL_SMALL_PARTS_SCOPE'] = args.cull_small_parts_scope or CULL_SMALL_PARTS_DEFAULT_SCOPE
        else:
            env.pop('X3M_CULL_SMALL_PARTS_PX', None)
            env.pop('X3M_CULL_SMALL_PARTS_SCOPE', None)
        # The two framing constants are always forwarded at their production
        # defaults so a stale shell value cannot reframe the camera; the rest
        # fall through to the DLL's compiled defaults when unset.
        for name, value in chase_tunables.items():
            if value is None:
                value = CHASE_FRAMING_DEFAULTS.get(name)
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
        # Busy-frame environment experiments, set only when requested so an unset
        # option leaves the bottle's FEX and Wine Direct3D configuration exactly
        # as it is (docs/architecture/effect-pass-replay.md, "Environment
        # experiments"). CrossOver's wine script passes its own environment on to
        # the child, adding the bottle's [EnvironmentVariables] on top, so these
        # two variables (which the bottle does not set) reach the game.
        experiment_env = {}
        if args.fex_tso is not None:
            experiment_env['FEX_TSOENABLED'] = '1' if args.fex_tso == 'on' else '0'
        if args.wined3d is not None:
            experiment_env['WINE_D3D_CONFIG'] = args.wined3d
        for name in ('FEX_TSOENABLED', 'WINE_D3D_CONFIG'):
            if name in experiment_env:
                env[name] = experiment_env[name]
            else:
                env.pop(name, None)  # a stale shell value must not enter the experiment
        # --dll applies to this child only, preserving the user's other overrides;
        # ';' separates entries exactly as in WINEDLLOVERRIDES, which is what
        # CrossOver's wine --dll feeds (docs/architecture/effect-pass-replay.md,
        # "bottle experiments": builtin vs native D3DX).
        overrides = 'd3d9=b' if args.vanilla else 'd3d9=n,b'
        if args.d3dx == 'builtin':
            overrides += ';d3dx9_37=b'
        command = [str(WINE), '--bottle', args.bottle, '--no-update',
                   '--dll', overrides,
                   '--workdir', str(game), str(game / 'X3AP.exe')]
        if args.direct:
            command += ['-noabout', '-skipintro', '-runinbg']
        launcher_log = game / CAPTURE_SUBDIRECTORY / LAUNCHER_STDERR
        if args.dry_run:
            print(json.dumps({'command': command, 'cwd': str(game), 'launcher_stderr': str(launcher_log),
                              'overrides': overrides, 'd3dx': args.d3dx,
                              'env': {**{k: env[k] for k in sorted(env) if k.startswith('X3M_')}, **voice_env,
                                      **experiment_env}}, indent=2))
            return
        print(f'Launching X3AP through CrossOver Preview; terminal output is also teed to {launcher_log}.', flush=True)
        # What was actually launched, first in the preserved log: the exact
        # argv, the --dll string this child got and the resolved --d3dx choice.
        header = (f'launcher command={json.dumps(command)} overrides={overrides} d3dx={args.d3dx}'
                  f' fex_tso={args.fex_tso or "unset"} wined3d={args.wined3d or "unset"}')
        raise SystemExit(launch_teed(command, env, game, launcher_log, header=header))


if __name__ == '__main__':
    main()
