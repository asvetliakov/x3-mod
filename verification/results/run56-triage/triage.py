#!/usr/bin/env python3
"""Bounded, read-only Run56 capture-session triage."""
from __future__ import annotations

import hashlib
import json
import re
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path('/tmp/x3-bottleX3-run200')
SESSION = ROOT / 'session-20260921-014107-212.log'
STDERR = ROOT / 'launcher-stderr.log'
OUT = Path('/tmp/x3-run56-triage/run56-capture-triage.json')

FRAME_RE = re.compile(r'\bframe=(\d+)\b')
KV_RE = re.compile(r'([A-Za-z][A-Za-z0-9_]*)=([^\s]+)')
PRESENT_RE = re.compile(r'^present_1_(\d+)\.bgra8$')

def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        for part in iter(lambda: f.read(1024 * 1024), b''):
            h.update(part)
    return h.hexdigest()

def kv(line: str) -> dict[str, str]:
    return dict(KV_RE.findall(line))

def ranges(values: list[int]) -> list[list[int]]:
    out: list[list[int]] = []
    for n in sorted(set(values)):
        if not out or n != out[-1][1] + 1:
            out.append([n, n])
        else:
            out[-1][1] = n
    return out

def record(kind: str, line_no: int, line: str, fields: tuple[str, ...]) -> dict:
    data = kv(line)
    return {'line': line_no, 'kind': kind,
            'fields': {key: data.get(key) for key in fields if key in data}}

def main() -> None:
    present_frames = []
    by_kind = Counter()
    extension_counts = Counter()
    for p in ROOT.iterdir():
        if p.is_file():
            extension_counts[p.suffix] += 1
            m = PRESENT_RE.match(p.name)
            if m:
                present_frames.append(int(m.group(1)))

    per_frame: dict[int, dict[str, dict]] = defaultdict(dict)
    capture_end_frames: list[int] = []
    toggles: list[dict] = []
    media: list[dict] = []
    sector: list[dict] = []
    header: dict[str, dict] = {}
    marker_counts = Counter()
    last_session = ''
    wanted = {
        'hull_lightmap_frame': ('gain', 'fill', 'admitted', 'toggled'),
        'hull_lightmap_far_fade_frame': ('admitted', 'faded', 'min_gain', 'floor', 'camera'),
        'fade_route_frame': ('fade_routed', 'fade_refused', 'fade_held', 'fade_route', 'overlay_routed', 'overlay_refused'),
        'motion_output_frame': ('draws', 'routed', 'matched', 'apply_failures', 'restore_failures', 'camera_valid', 'camera_cut', 'taa_resolved', 'taa_history', 'taa_result', 'scene_end_source'),
        'camera_state': ('valid', 'background_valid', 'rotation_deg', 'camera_cut', 'policy', 'reason', 't'),
        'frame_end': ('draws', 'capture', 'elapsed_ms', 'dt_ms', 'qpc'),
    }
    with SESSION.open('rt', errors='replace') as f:
        for line_no, line in enumerate(f, 1):
            last_session = line.rstrip()
            kind = line.split(' ', 1)[0]
            by_kind[kind] += 1
            low = line.lower()
            for marker in ('crash', 'fatal', 'segmentation fault', 'unhandled exception', 'page fault'):
                if marker in low:
                    marker_counts[marker] += 1
            if kind in ('proxy_identity', 'proxy_options', 'fade_route_mode', 'light_map_far_fade_mode',
                        'volumetric_fog_mode', 'media_cue_mode', 'media_cue_site',
                        'media_video_witness', 'motion_output_mode'):
                header[kind] = {'line': line_no, 'fields': kv(line)}
            m = FRAME_RE.search(line)
            frame = int(m.group(1)) if m else None
            if kind in ('volumetric_fog_toggle', 'hull_emission_gain_toggle'):
                toggles.append(record(kind, line_no, line, ('frame', 'key', 'enabled', 'strength', 'density_scale', 'anisotropy', 'disabled', 'accepted', 'gain', 'lightmap_enabled')))
            if kind in ('media_cue', 'media_cue_window'):
                media.append(record(kind, line_no, line, ('frame', 'id', 'kind', 'caller', 'result', 'us', 'attempts_frame', 'cached', 'attempts', 'failures', 'successes', 'video_blits', 'video_failures', 'video_suppressed')))
            if kind == 'sector_background' and frame is not None and frame >= 16000:
                sector.append(record(kind, line_no, line, ('frame', 'status', 'name', 'near', 'far', 'effective_far', 'camera_valid', 'camera_check')))
            if frame is not None and kind == 'frame_end' and 'capture=1' in line:
                capture_end_frames.append(frame)
            if frame is not None and kind in wanted:
                per_frame[frame][kind] = record(kind, line_no, line, wanted[kind])

    stderr_markers = Counter()
    last_stderr = ''
    with STDERR.open('rt', errors='replace') as f:
        for line in f:
            last_stderr = line.rstrip()
            low = line.lower()
            for marker in ('crash', 'fatal', 'segmentation fault', 'unhandled exception', 'page fault', 'lav'):
                if marker in low:
                    stderr_markers[marker] += 1

    capture_ranges = ranges(capture_end_frames)
    capture_set = set(capture_end_frames)
    image_set = set(present_frames)
    compact_frames = {str(n): per_frame[n] for n in sorted(capture_set) if n in per_frame}
    out = {
        'schema': 1,
        'observation_only': True,
        'sources': {str(p): {'bytes': p.stat().st_size, 'sha256': sha256(p), 'mtime_ns': p.stat().st_mtime_ns}
                    for p in (SESSION, STDERR)},
        'capture_root': str(ROOT),
        'capture_inventory': {
            'present_frame_ranges': ranges(present_frames),
            'session_capture_frame_ranges': capture_ranges,
            'present_frames': len(present_frames),
            'session_capture_frames': len(capture_end_frames),
            'sets_equal': image_set == capture_set,
            'extension_counts': dict(sorted(extension_counts.items())),
        },
        'configuration': header,
        'toggles': toggles,
        'media': {'rows': media, 'lav_substring_rows_in_stderr': stderr_markers['lav']},
        'sector_rows_from_frame_16000': sector,
        'capture_frame_summaries': compact_frames,
        'session_marker_counts': dict(marker_counts),
        'stderr_marker_counts': dict(stderr_markers),
        'session_last_record': last_session[:500],
        'stderr_last_record': last_stderr[:500],
        'record_type_counts': {k: by_kind[k] for k in ('volumetric_fog_toggle', 'volumetric_fog_frame', 'hull_lightmap_far_fade_frame', 'fade_route_frame', 'media_cue', 'media_cue_window', 'camera_state', 'frame_end')},
    }
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(out, indent=2, sort_keys=True) + '\n')

if __name__ == '__main__':
    main()
