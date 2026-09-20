#!/usr/bin/env python3
"""Host-side transcoder for the X3 video-cue experiment.

The game hands ``mov\\NNNNN.dat`` to a DirectShow graph and never demuxes a
byte itself (docs/reverse-engineering/media-cue-playback.md sec. 8.5), so the
container and codec are free as long as the file keeps its ``%05d.dat`` name
and a decoder is reachable inside the graph.  Under CrossOver the graph is
Wine's quartz over GStreamer, whose shipped plugin set has no MPEG-1 decoder
but does have ``h264parse`` + VideoToolbox ``vtdec``; native Windows
DirectShow demuxes AVI and decodes H.264 with the stock DTV-DVD decoder.
H.264-in-AVI is therefore the one combination plausibly decodable on both
platforms with no custom codec runtime.

Subcommands:
  probe    ffprobe summary of every ``mov/*.dat``
  build    transcode one cue to H.264 (AVI or MP4) plus a JSON sidecar
  install  swap a transcoded file into the game directory, keeping the original
  restore  put the original back

``build`` never writes into the game directory.  ``install``/``restore`` are
the only subcommands that touch it and both refuse while X3AP.exe is running.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from fractions import Fraction
from pathlib import Path

DEFAULT_GAME_DIR = Path(
    os.environ.get(
        "X3M_GAME_DIR",
        os.path.expanduser(
            "~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3"
        ),
    )
)

FFMPEG = os.environ.get("X3M_FFMPEG", "/opt/homebrew/bin/ffmpeg")
FFPROBE = os.environ.get("X3M_FFPROBE", "/opt/homebrew/bin/ffprobe")

CONTAINERS = ("avi", "mp4")
SIDECAR_VERSION = 1
# frame-count / duration agreement required between source and result
TOLERANCE_FRAMES = 1


class ToolError(RuntimeError):
    """User-facing failure; reported without a traceback."""


# --------------------------------------------------------------------------
# helpers


def sha256_file(path: Path, chunk: int = 1 << 20) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for block in iter(lambda: fh.read(chunk), b""):
            h.update(block)
    return h.hexdigest()


def _run(cmd: list[str], **kw) -> subprocess.CompletedProcess:
    try:
        return subprocess.run(cmd, capture_output=True, text=True, **kw)
    except FileNotFoundError as exc:  # pragma: no cover - environment
        raise ToolError(f"missing executable: {cmd[0]} ({exc})") from exc


def _ffprobe_json(args: list[str]) -> dict:
    proc = _run([FFPROBE, "-v", "error", *args, "-of", "json"])
    if proc.returncode != 0:
        raise ToolError(f"ffprobe failed: {proc.stderr.strip()}")
    try:
        return json.loads(proc.stdout or "{}")
    except json.JSONDecodeError as exc:
        raise ToolError(f"ffprobe returned non-JSON output: {exc}") from exc


def _fraction(text: str | None) -> Fraction | None:
    if not text or text in ("0/0", "N/A"):
        return None
    try:
        return Fraction(text)
    except (ValueError, ZeroDivisionError):
        return None


def probe_stream(path: Path, count_packets: bool = True) -> dict:
    """Summarise the first video stream.

    Raw MPEG-1 elementary streams carry no index: ffprobe's ``format.duration``
    is derived from a guessed bitrate and ``r_frame_rate`` reports the field
    rate (60 for a 30 fps stream).  The frame rate is therefore taken from the
    demuxer's own packet duration in the stream time base, and the duration is
    computed from the counted packets, both of which are exact.
    """
    path = Path(path)
    if not path.is_file():
        raise ToolError(f"no such file: {path}")

    info = _ffprobe_json(
        [
            "-select_streams",
            "v:0",
            "-show_entries",
            "stream=codec_name,profile,width,height,pix_fmt,r_frame_rate,"
            "avg_frame_rate,time_base,nb_frames,duration",
            "-show_entries",
            "format=format_name,duration,bit_rate",
            "-show_entries",
            "packet=duration",
            "-read_intervals",
            "%+#1",
            str(path),
        ]
    )
    streams = info.get("streams") or []
    if not streams:
        raise ToolError(f"no video stream in {path}")
    st = streams[0]
    fmt = info.get("format") or {}
    packets = info.get("packets") or []

    time_base = _fraction(st.get("time_base"))
    pkt_dur = packets[0].get("duration") if packets else None
    fps: Fraction | None = None
    fps_source = None
    if time_base and pkt_dur:
        try:
            span = Fraction(int(pkt_dur)) * time_base
        except (TypeError, ValueError):
            span = None
        if span and span > 0:
            fps = 1 / span
            fps_source = "packet_duration"
    if fps is None:
        fps = _fraction(st.get("avg_frame_rate")) or _fraction(st.get("r_frame_rate"))
        fps_source = "avg_frame_rate" if _fraction(st.get("avg_frame_rate")) else "r_frame_rate"
    if fps is None or fps <= 0:
        raise ToolError(f"cannot determine frame rate of {path}")

    frames = None
    nb = st.get("nb_frames")
    if nb not in (None, "N/A"):
        try:
            frames = int(nb)
        except ValueError:
            frames = None
    if frames is None and count_packets:
        proc = _run(
            [
                FFPROBE,
                "-v",
                "error",
                "-count_packets",
                "-select_streams",
                "v:0",
                "-show_entries",
                "stream=nb_read_packets",
                "-of",
                "csv=p=0",
                str(path),
            ]
        )
        if proc.returncode != 0:
            raise ToolError(f"ffprobe packet count failed: {proc.stderr.strip()}")
        digits = re.findall(r"\d+", proc.stdout)
        if digits:
            frames = int(digits[0])

    duration = float(frames / fps) if frames else None
    if duration is None:
        try:
            duration = float(st.get("duration") or fmt.get("duration"))
        except (TypeError, ValueError):
            duration = None

    return {
        "path": str(path),
        "size": path.stat().st_size,
        "format_name": fmt.get("format_name"),
        "codec_name": st.get("codec_name"),
        "profile": st.get("profile"),
        "width": st.get("width"),
        "height": st.get("height"),
        "pix_fmt": st.get("pix_fmt"),
        "frame_rate": str(fps),
        "frame_rate_value": float(fps),
        "frame_rate_source": fps_source,
        "frame_count": frames,
        "duration_s": duration,
        "bit_rate": fmt.get("bit_rate"),
    }


def cue_path(game_dir: Path, cue_id: int) -> Path:
    return Path(game_dir) / "mov" / f"{cue_id:05d}.dat"


def parse_cue_id(text: str) -> int:
    try:
        value = int(text, 10)
    except ValueError:
        raise argparse.ArgumentTypeError(f"cue id must be an integer, got {text!r}")
    if not 0 <= value <= 99999:
        raise argparse.ArgumentTypeError(f"cue id out of range 0..99999: {value}")
    return value


def x3_running() -> str | None:
    """Return the matching pgrep line when X3AP.exe is up, else None."""
    proc = subprocess.run(
        ["pgrep", "-fl", "X3AP.exe"], capture_output=True, text=True
    )
    lines = [ln for ln in proc.stdout.splitlines() if "X3AP.exe" in ln]
    return lines[0] if lines else None


# --------------------------------------------------------------------------
# probe


def cmd_probe(args) -> int:
    mov = Path(args.game_dir) / "mov"
    if not mov.is_dir():
        raise ToolError(f"no mov directory under {args.game_dir}")
    files = sorted(mov.glob("*.dat"))
    if not files:
        raise ToolError(f"no *.dat files in {mov}")
    header = (
        f"{'file':<14}{'codec':<12}{'size':<8}{'fps':>8}{'frames':>10}"
        f"{'duration_s':>12}{'MiB':>10}"
    )
    print(header)
    print("-" * len(header))
    for path in files:
        try:
            info = probe_stream(path, count_packets=not args.fast)
        except ToolError as exc:
            print(f"{path.name:<14}error: {exc}")
            continue
        size = f"{info['width']}x{info['height']}"
        frames = info["frame_count"]
        dur = info["duration_s"]
        print(
            f"{path.name:<14}{str(info['codec_name']):<12}{size:<8}"
            f"{info['frame_rate_value']:>8.3f}"
            f"{(frames if frames is not None else -1):>10}"
            f"{(dur if dur is not None else float('nan')):>12.2f}"
            f"{info['size'] / (1 << 20):>10.1f}"
        )
    return 0


# --------------------------------------------------------------------------
# build


def encode_args(src: Path, dst: Path, info: dict, container: str,
                crf: int, preset: str, profile: str) -> list[str]:
    fps = info["frame_rate"]
    args = [
        FFMPEG,
        "-y",
        "-hide_banner",
        "-nostdin",
        "-threads",
        "0",
        "-r",
        fps,
        "-i",
        str(src),
        "-an",
        "-map",
        "0:v:0",
        "-c:v",
        "libx264",
        "-preset",
        preset,
        "-crf",
        str(crf),
        "-pix_fmt",
        "yuv420p",
        "-profile:v",
        profile,
        "-g",
        "12",
        "-keyint_min",
        "12",
        "-sc_threshold",
        "0",
        "-bf",
        "0",
        "-r",
        fps,
        "-fps_mode",
        "cfr",
        "-s",
        f"{info['width']}x{info['height']}",
    ]
    if container == "avi":
        # H264 FourCC is what the stock Windows DTV-DVD decoder and
        # winegstreamer's avi demuxer both key on.
        args += ["-vtag", "H264", "-f", "avi"]
    else:
        args += ["-movflags", "+faststart", "-f", "mp4"]
    args.append(str(dst))
    return args


def cmd_build(args) -> int:
    if args.container not in CONTAINERS:
        raise ToolError(f"container must be one of {CONTAINERS}")
    game_dir = Path(args.game_dir)
    src = cue_path(game_dir, args.id)
    if not src.is_file():
        raise ToolError(f"no such cue file: {src}")

    out_dir = Path(args.out).resolve()
    if _is_within(out_dir, game_dir.resolve()):
        raise ToolError(
            f"refusing to write into the game directory: {out_dir}"
        )
    out_dir.mkdir(parents=True, exist_ok=True)
    dst = out_dir / f"{args.id:05d}.{args.container}.dat"
    sidecar = dst.with_suffix(dst.suffix + ".json")

    src_info = probe_stream(src)
    if not src_info["frame_count"]:
        raise ToolError(f"could not count frames of {src}")

    cmd = encode_args(src, dst, src_info, args.container, args.crf,
                      args.preset, args.profile)
    print("encoding:", " ".join(cmd), flush=True)
    started = time.time()
    proc = subprocess.run(cmd, stdout=subprocess.DEVNULL)
    wall = time.time() - started
    if proc.returncode != 0:
        raise ToolError(f"ffmpeg failed with exit code {proc.returncode}")

    dst_info = probe_stream(dst)
    frame_delta = (dst_info["frame_count"] or -1) - src_info["frame_count"]
    duration_delta = (dst_info["duration_s"] or 0.0) - (src_info["duration_s"] or 0.0)
    frame_seconds = 1.0 / src_info["frame_rate_value"]

    record = {
        "tool": "media_transcode",
        "sidecar_version": SIDECAR_VERSION,
        "cue_id": args.id,
        "container": args.container,
        "created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "ffmpeg": _tool_version(FFMPEG),
        "ffprobe": _tool_version(FFPROBE),
        "encode": {
            "command": cmd,
            "wall_time_s": round(wall, 3),
            "crf": args.crf,
            "preset": args.preset,
            "profile": args.profile,
            "gop": 12,
            "b_frames": 0,
        },
        "source": {**src_info, "sha256": sha256_file(src)},
        "result": {**dst_info, "sha256": sha256_file(dst)},
        "checks": {
            "tolerance_frames": TOLERANCE_FRAMES,
            "frame_count_delta": frame_delta,
            "duration_delta_s": round(duration_delta, 6),
            "frame_seconds": frame_seconds,
        },
    }
    sidecar.write_text(json.dumps(record, indent=2) + "\n")

    problems = []
    if abs(frame_delta) > TOLERANCE_FRAMES:
        problems.append(
            f"frame count {dst_info['frame_count']} != source "
            f"{src_info['frame_count']} (delta {frame_delta})"
        )
    if abs(duration_delta) > TOLERANCE_FRAMES * frame_seconds + 1e-6:
        problems.append(
            f"duration {dst_info['duration_s']} != source "
            f"{src_info['duration_s']} (delta {duration_delta:.6f} s)"
        )
    if (dst_info["width"], dst_info["height"]) != (
        src_info["width"],
        src_info["height"],
    ):
        problems.append("frame size changed")
    if dst_info["frame_rate"] != src_info["frame_rate"]:
        problems.append(
            f"frame rate {dst_info['frame_rate']} != source {src_info['frame_rate']}"
        )

    print(
        f"source : {src_info['codec_name']} {src_info['width']}x{src_info['height']} "
        f"{src_info['frame_rate']} fps {src_info['frame_count']} frames "
        f"{src_info['duration_s']:.3f} s {src_info['size']} B"
    )
    print(
        f"result : {dst_info['codec_name']} {dst_info['width']}x{dst_info['height']} "
        f"{dst_info['frame_rate']} fps {dst_info['frame_count']} frames "
        f"{(dst_info['duration_s'] or float('nan')):.3f} s {dst_info['size']} B"
    )
    print(f"wall   : {wall:.1f} s -> {dst}")
    print(f"sidecar: {sidecar}")
    if problems:
        raise ToolError("; ".join(problems))
    return 0


def _tool_version(exe: str) -> str:
    proc = _run([exe, "-version"])
    first = (proc.stdout or proc.stderr).splitlines()
    return first[0].strip() if first else "unknown"


def _is_within(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


# --------------------------------------------------------------------------
# install / restore


def managed_media_mutation(operation):
    """Share the managed install lock across legacy original-file mutations."""
    def run(args):
        try:
            try:
                import media_package
            except ModuleNotFoundError:
                from tools import media_package
            with media_package.installer_lock(Path(args.game_dir)):
                media_package.guard_legacy(Path(args.game_dir), args.id)
                return operation(args)
        except (OSError, ValueError, KeyError, TypeError) as error:
            raise ToolError(str(error)) from error
    return run


@managed_media_mutation
def cmd_install(args) -> int:

    game_dir = Path(args.game_dir)
    target = cue_path(game_dir, args.id)
    if not target.is_file():
        raise ToolError(f"no such cue file: {target}")
    src_dir = Path(getattr(args, "from_dir"))
    candidates = sorted(src_dir.glob(f"{args.id:05d}.*.dat"))
    candidates = [c for c in candidates if not c.name.endswith(".json")]
    if args.container:
        candidates = [c for c in candidates
                      if c.name == f"{args.id:05d}.{args.container}.dat"]
    if not candidates:
        raise ToolError(f"no transcoded file for id {args.id} in {src_dir}")
    if len(candidates) > 1:
        raise ToolError(
            "several transcoded files present, pass --container: "
            + ", ".join(c.name for c in candidates)
        )
    new_file = candidates[0]

    orig = target.with_name(target.name + ".orig")
    target_sha = sha256_file(target)
    if orig.exists():
        orig_sha = sha256_file(orig)
        if orig_sha != target_sha:
            raise ToolError(
                f"{orig.name} already exists and differs from the current "
                f"{target.name} (orig {orig_sha[:16]}, current "
                f"{target_sha[:16]}); a transcode is probably installed - run "
                f"`restore --id {args.id}` first"
            )
    else:
        shutil.copy2(target, orig)
        orig_sha = sha256_file(orig)
        if orig_sha != target_sha:
            raise ToolError("copy of the original did not verify; aborting")

    new_sha = sha256_file(new_file)
    tmp = target.with_name(target.name + ".new")
    shutil.copy2(new_file, tmp)
    if sha256_file(tmp) != new_sha:
        tmp.unlink(missing_ok=True)
        raise ToolError("staged copy did not verify; game file untouched")
    os.replace(tmp, target)
    final_sha = sha256_file(target)
    print(f"original saved : {orig} sha256={orig_sha}")
    print(f"installed      : {new_file} sha256={new_sha}")
    print(f"game file now  : {target} sha256={final_sha}")
    if final_sha != new_sha:
        raise ToolError("installed bytes do not match the source file")
    return 0


@managed_media_mutation
def cmd_restore(args) -> int:
    game_dir = Path(args.game_dir)
    target = cue_path(game_dir, args.id)
    orig = target.with_name(target.name + ".orig")
    if not orig.is_file():
        raise ToolError(f"no saved original: {orig}")
    orig_sha = sha256_file(orig)
    current_sha = sha256_file(target) if target.is_file() else None
    if current_sha == orig_sha:
        print(f"already original: {target} sha256={orig_sha}")
        return 0
    tmp = target.with_name(target.name + ".new")
    shutil.copy2(orig, tmp)
    os.replace(tmp, target)
    final_sha = sha256_file(target)
    print(f"restored : {target} sha256={final_sha}")
    print(f"original : {orig} sha256={orig_sha}")
    if final_sha != orig_sha:
        raise ToolError("restored bytes do not match the saved original")
    print(f"{orig.name} kept in place")
    return 0


# --------------------------------------------------------------------------


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="media_transcode.py", description=__doc__.splitlines()[0]
    )
    sub = parser.add_subparsers(dest="command", required=True)

    def add_common(p):
        p.add_argument("--game-dir", default=str(DEFAULT_GAME_DIR),
                       help="X3 game directory (default: bottle X3)")

    p = sub.add_parser("probe", help="ffprobe summary of every mov/*.dat")
    add_common(p)
    p.add_argument("--fast", action="store_true",
                   help="skip the packet count (frame counts may be missing)")
    p.set_defaults(func=cmd_probe)

    p = sub.add_parser("build", help="transcode one cue to H.264")
    add_common(p)
    p.add_argument("--id", type=parse_cue_id, required=True)
    p.add_argument("--container", default="avi", choices=CONTAINERS)
    p.add_argument("--out", required=True, help="output directory (never the game dir)")
    p.add_argument("--crf", type=int, default=20)
    p.add_argument("--preset", default="veryfast")
    p.add_argument("--profile", default="baseline",
                   choices=("baseline", "main", "high"))
    p.set_defaults(func=cmd_build)

    p = sub.add_parser("install", help="swap a transcoded file into mov/")
    add_common(p)
    p.add_argument("--id", type=parse_cue_id, required=True)
    p.add_argument("--from", dest="from_dir", required=True,
                   help="directory holding the transcoded file")
    p.add_argument("--container", choices=CONTAINERS, default=None)
    p.set_defaults(func=cmd_install)

    p = sub.add_parser("restore", help="put the saved original back")
    add_common(p)
    p.add_argument("--id", type=parse_cue_id, required=True)
    p.set_defaults(func=cmd_restore)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except ToolError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
