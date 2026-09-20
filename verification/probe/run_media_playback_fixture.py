#!/usr/bin/env python3
"""Run a retained standalone EXE under wine_lock.py; never build or launch X3.

The Windows EXE supervises and terminates only its own children. This runner
sets a process-local decoder environment, saves provenance, and validates frame
and cleanup evidence. --backend is a label, never an implicit codec selector.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import signal
import stat
import shutil
import time

import media_lav_evidence as lav_evidence
from prepare_lav_fixture import verify_record
import bottle
from game_guard import game_running

STAGES = ('constructor', 'sample-run', 'pump', 'copy')
POSITION_TRACE_SITES = ('after_pause', 'after_create_sample', 'before_run', 'after_run')
SEEK_TRACE_LIMIT = 64*1024*1024
SEEK_TRACE_POLL_SECONDS = 0.1
SEEK_TRACE_WINEDEBUG = '-all,+timestamp,+pid,+tid,+winegstreamer,+amstream'
SEEK_TRACE_GST_DEBUG = 'WINE:7,baseparse:5,GST_EVENT:5,GST_PLUGIN_LOADING:4,GST_ELEMENT_FACTORY:4'
LAV_GRAPH_TRACE_WINEDEBUG = '-all,+timestamp,+pid,+tid,+amstream,+quartz'


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024*1024), b''):
            h.update(block)
    return h.hexdigest()


def valid_sha(value):
    return isinstance(value, str) and len(value) == 64 and all(c in '0123456789abcdef' for c in value)


def bound_file(item, expected=None, required_bytes=None):
    path = Path(item['path'])
    if not path.is_absolute() or not path.is_file():
        raise ValueError('provenance file must be an existing absolute path')
    if expected is not None and path.resolve() != expected.resolve():
        raise ValueError('provenance path mismatch')
    if (not valid_sha(item['sha256']) or type(item['bytes']) is not int or item['bytes'] <= 0 or
            (required_bytes is not None and item['bytes'] != required_bytes) or
            path.stat().st_size != item['bytes'] or digest(path) != item['sha256']):
        raise ValueError('provenance file size/hash mismatch')
    return path


def validate_derivation(path, media, original, target_ms, allow_generated_timestamps=False):
    if path.stat().st_size > 1024*1024:
        raise ValueError('oversized derivation record')
    data = json.loads(path.read_text())
    if (type(data['schema']) is not int or data['schema'] not in (1, 2) or data['kind'] != 'mpeg1video-stream-copy-matroska' or
            data['codec'] != 'mpeg1video' or media.suffix.lower() != '.mkv' or
            media.resolve() == original.resolve()):
        raise ValueError('unsupported derived-media contract')
    generated = data['schema'] == 2
    if generated != allow_generated_timestamps:
        raise ValueError('schema2 requires explicit generated-timestamps opt-in; schema1 forbids it')
    if generated and data.get('timeline_semantics') != 'generated_timestamps':
        raise ValueError('schema2 must declare generated_timestamps semantics')
    bound_file(data['original'], original)
    bound_file(data['derived'], media)
    payload = data['payload']
    if (not valid_sha(payload['original_sha256']) or payload['original_sha256'] != payload['derived_sha256'] or
            type(payload['bytes']) is not int or payload['bytes'] <= 0):
        raise ValueError('missing stream-copy payload identity')
    preparation = data['preparation']
    if (not isinstance(preparation['command'], list) or not preparation['command'] or
            not preparation['ffmpeg_version'] or not math.isfinite(float(preparation['wall_seconds'])) or
            float(preparation['wall_seconds']) < 0):
        raise ValueError('missing native preparation provenance')
    frames = data['native_frames']
    if not isinstance(frames, list) or not 2 <= len(frames) <= 16:
        raise ValueError('bounded head and target reference frames required')
    times = set()
    for frame in frames:
        if type(frame['seconds']) not in (int, float):
            raise ValueError('reference frame time must be numeric')
        seconds = float(frame['seconds'])
        if not math.isfinite(seconds) or seconds < 0 or seconds in times:
            raise ValueError('invalid reference frame time')
        times.add(seconds)
        bound_file(frame['derived'], required_bytes=1048576)
        if not generated:
            bound_file(frame['original'], required_bytes=1048576)
            if frame['original']['sha256'] != frame['derived']['sha256']:
                raise ValueError('native original/remux frame mismatch')
        elif 'original' in frame:
            raise ValueError('schema2 requested-time references must be derived-only')
    if 0.0 not in times or not any((t*1000 == target_ms if generated else abs(t*1000-target_ms) <= 80)
                                   for t in times if t != 0):
        raise ValueError('head and requested target reference frames required')
    if generated:
        witnesses = data['decoded_ordinal_equivalence']
        if not isinstance(witnesses, list) or not 3 <= len(witnesses) <= 32:
            raise ValueError('schema2 requires bounded head and adjacent decoded-ordinal witnesses')
        ordinals = {}
        for witness in witnesses:
            ordinal = witness['ordinal']
            if type(ordinal) is not int or ordinal < 0 or ordinal in ordinals:
                raise ValueError('invalid or duplicate decoded ordinal')
            for domain in ('original', 'derived'):
                item = witness[domain]
                bound_file(item, required_bytes=1048576)
                actual = item['seconds']
                if type(actual) not in (int, float) or not math.isfinite(actual) or actual < 0:
                    raise ValueError('decoded ordinal requires actual original and derived times')
            if witness['original']['sha256'] != witness['derived']['sha256']:
                raise ValueError('decoded-ordinal original/remux frame mismatch')
            ordinals[ordinal] = witness
        ordered = [ordinals[n] for n in sorted(ordinals)]
        if any(a[domain]['seconds'] >= b[domain]['seconds'] for domain in ('original', 'derived')
               for a, b in zip(ordered, ordered[1:])):
            raise ValueError('decoded-ordinal timestamps must increase within each timeline')
        head = next(f for f in frames if f['seconds'] == 0)
        if 0 not in ordinals or head['derived']['sha256'] != ordinals[0]['derived']['sha256']:
            raise ValueError('native derived head must match decoded ordinal zero')
        # A target reference is selected using the derived file's own requested time.
        # Bind it to a decoded ordinal and an adjacent witness, never add an offset.
        targets = [f for f in frames if f['seconds'] != 0 and abs(f['seconds']*1000-target_ms) <= 80]
        for target in targets:
            matches = [n for n, w in ordinals.items() if n > 0 and
                       w['derived']['sha256'] == target['derived']['sha256'] and
                       abs(w['derived']['seconds']-target['seconds']) <= .08]
            if not any((n-1 in ordinals and n-1 > 0) or n+1 in ordinals for n in matches):
                raise ValueError('native derived target requires matching and adjacent decoded ordinals')
    return dict(path=str(path.resolve()), sha256=digest(path), record=data,
                timeline_semantics='generated_timestamps' if generated else 'strict_same_time_native_equivalence',
                content_reference_domain='native_derived_requested_time', original_timeline_playback_proven=False)


def fnv64(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte)*1099511628211) & 0xffffffffffffffff
    return f'{value:016x}'


def inspect_first_frame(record, output):
    """Exact bytes alone can prove a match; RGB distances are diagnostic only."""
    evidence = dict(requested=record.get('dump_first_frame', False), verified=False,
                    target_content_exact_match=False, comparisons=[])
    if not evidence['requested']:
        return evidence
    try:
        stage = record.get('stages', {}).get('copy', {})
        witness = stage.get('frame_dump')
        if stage.get('header', {}).get('first_frame_dump') != '1' or not witness or not stage.get('chain_valid'):
            raise ValueError('missing first-frame capture witness')
        path = output/'first-frame.bgra'
        info = path.lstat()
        if not stat.S_ISREG(info.st_mode) or info.st_size != 1048576:
            raise ValueError('first-frame dump must be a regular 1048576-byte file')
        raw = path.read_bytes()
        if len(raw) != 1048576 or fnv64(raw) != witness['hash']:
            raise ValueError('first-frame dump does not match copied frame hash')
        evidence.update(verified=True, file=str(path), bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest(),
                        fnv64=witness['hash'], sample_start=int(witness['start']), sample_end=int(witness['end']),
                        format='BGRA8_top_down', width=512, height=512, stride=2048)
        derivation = record.get('derived_media')
        if derivation:
            evidence['timeline_semantics'] = derivation['timeline_semantics']
            evidence['content_reference_domain'] = derivation['content_reference_domain']
            actual_rgb = raw[0::4]+raw[1::4]+raw[2::4]
            for frame in derivation['record']['native_frames']:
                # Recheck references after the fixture run before interpreting the bytes.
                ref_path = bound_file(frame['derived'], required_bytes=1048576)
                reference = ref_path.read_bytes()
                if hashlib.sha256(reference).hexdigest() != frame['derived']['sha256']:
                    raise ValueError('reference changed during read')
                reference_rgb = reference[0::4]+reference[1::4]+reference[2::4]
                total = maximum = changed = 0
                for actual, expected in zip(actual_rgb, reference_rgb):
                    difference = abs(actual-expected)
                    total += difference
                    maximum = max(maximum, difference)
                    changed += difference != 0
                evidence['comparisons'].append(dict(seconds=frame['seconds'], exact_bytes=raw == reference,
                    rgb_mae=total/len(actual_rgb), rgb_max_error=maximum, changed_rgb_channels=changed,
                    reference_sha256=frame['derived']['sha256']))
            target_ms = int(stage['header']['start_ms'])
            generated = derivation['record']['schema'] == 2
            evidence['target_content_exact_match'] = (any(c['exact_bytes'] and c['seconds'] != 0 and
                (float(c['seconds'])*1000 == target_ms if generated else abs(float(c['seconds'])*1000-target_ms) <= 80)
                for c in evidence['comparisons']) and
                not any(c['exact_bytes'] and c['seconds'] == 0 for c in evidence['comparisons']))
            evidence['outcome'] = ('exact_target_content_match' if evidence['target_content_exact_match'] else
                                   'content_unqualified_distances_require_review')
    except (OSError, ValueError, KeyError, TypeError) as error:
        evidence.update(verified=False, target_content_exact_match=False, error=str(error))
    return evidence


def fields(line):
    return dict(token.split('=', 1) for token in line.split()[1:] if '=' in token)


def validate(lines):
    """Stream bounded rows; preserve timeout versus HRESULT failure distinctions."""
    stages = {}
    current = None
    for line in lines:
        if not line.startswith('MP_'):
            continue
        if len(line) > 65536:
            raise ValueError('oversized fixture row')
        kind = line.split()[0]
        row = fields(line)
        if kind == 'MP_SUPERVISOR':
            name = row['stage']
            if name not in STAGES or name in stages:
                raise ValueError('invalid or duplicate stage')
            current = dict(stage=name, child_pid=int(row.get('child_pid','0')), pending=None, calls=0, samples=[], frames=[], failures=[],
                           cleanup_errors=[], modules=[], filters=[], pins=[], completed=False,
                           baseline=False, timeout=None, thread=None, last_seq=-1, returned_calls=[],
                           chain_valid=True, waiting=False, ready=False, stamp_ready=False, copy_chain=[], route=None,
                           variant="baseline", skipped_seek=False, seek_position=None, position_trace=[], frame_dump=None, dump_chain=[])
            stages[name] = current
        elif current is None:
            raise ValueError('fixture row before supervisor')
        elif kind == 'MP_HEADER':
            if row['stage'] != current['stage'] or row['schema'] != '1' or row['flags'] != '8' or row['media_id'] != '2':
                raise ValueError('unexpected fixture contract')
            current['variant'] = row.get('variant', 'baseline')
            if current['variant'] not in ('baseline', 'skip-zero-seek', 'stop-before-seek', 'lav-explicit'):
                raise ValueError('unknown diagnostic variant')
            if current['variant'] == 'lav-explicit' and (row.get('lav_scenario') not in ('seek', 'reference') or row.get('start_ms') not in ('0', '10000') or
                    (row.get('media_kind') != 'original_dat' and not (row.get('media_kind')=='derived_stream_copy_matroska' and row.get('lav_transport') in ('reference','epochs','boundary','pending','reference-matrix','integer-matrix','pending-integer','seek-only-interval','seek-only-recovery')))):
                raise ValueError('invalid explicit provider scenario')
            if (row.get('lav_terminal_decommit', '0') not in ('0', '1') or
                    (row.get('lav_terminal_decommit') == '1' and
                     (current['variant'] != 'lav-explicit' or current['stage'] != 'constructor' or
                      row.get('start_ms') != '0' or row.get('lav_scenario') != 'seek'))):
                raise ValueError('invalid terminal allocator diagnostic declaration')
            if current['variant'] == 'skip-zero-seek' and row.get('start_ms') != '0':
                raise ValueError('skip-zero-seek requires start_ms=0')
            if 'seek_position_trace' in row and row['seek_position_trace'] != str(int(current['variant'] == 'stop-before-seek')):
                raise ValueError('invalid position trace declaration')
            if row.get('first_frame_dump', '0') not in ('0', '1') or (row.get('first_frame_dump') == '1' and current['stage'] != 'copy'):
                raise ValueError('invalid first-frame dump declaration')
            if row.get('media_kind', 'original_dat') not in ('original_dat', 'derived_stream_copy_matroska'):
                raise ValueError('invalid media kind')
            mode=row.get('lav_transport','none')
            if mode!='none' and (mode not in lav_evidence.TRANSPORT_MODES or current['variant']!='lav-explicit' or current['stage']!='copy' or
                    row.get('lav_terminal_decommit','0')!='0' or row.get('first_frame_dump','0')!='0' or
                    row.get('lav_scenario')!=('reference' if mode in lav_evidence.REFERENCE_MODES else 'seek') or
                    row.get('start_ms')!=('10000' if mode in lav_evidence.REFERENCE_MODES else '0')):
                raise ValueError('invalid transport diagnostic declaration')
            current['header'] = row
            current['thread'] = row['thread']
        elif kind == 'MP_ENTER':
            if row['name'] in lav_evidence.TRANSPORT_ARGS and not lav_evidence.transport_mode(current):
                raise ValueError('transport call outside explicit diagnostic')
            if row['name'] in lav_evidence.TERMINAL_ARGS and not lav_evidence.terminal_mode(current):
                raise ValueError('terminal allocator call outside explicit diagnostic')
            if current['pending'] or int(row['seq']) != current['last_seq']+1 or row['thread'] != current['thread']:
                raise ValueError('invalid sequence or STA thread')
            current['pending'] = row
        elif kind == 'MP_LEAVE':
            pending = current['pending']
            if not pending or any(row[k] != pending[k] for k in ('seq', 'name', 'stage', 'phase', 'thread')):
                raise ValueError('unmatched call return')
            elapsed = float(row['elapsed_ms'])
            if not math.isfinite(elapsed) or elapsed < 0 or len(row['hr']) != 8:
                raise ValueError('invalid timing or HRESULT')
            value = int(row['hr'], 16)
            negative = bool(value & 0x80000000)
            name = row['name']
            exact_args = {'initialize': 'STREAMTYPE_READ_AMMSF_NOGRAPHTHREAD_NULL',
                          'add_video': 'NULL_PrimaryVideo_0_NULL', 'create_sample': 'NULL_NULL_0_out',
                          'update': 'SSUPDATE_ASYNC_NULL_NULL_0', 'completion_status': 'flags0_timeout0',
                          'source_lock': 'NULL_desc_flags0_NULL', 'destination_lock': 'NULL_flags0',
                          'stop_before_seek': 'diagnostic_Stop_no_reposition',
                          'seek_position_after': 'diagnostic_out_seconds_while_stopped',
                          'restore_pause_after_seek': 'diagnostic_restore_pre_sample_Pause',
                          'dump_create': 'first-frame.bgra_CREATE_NEW', 'dump_write': 'packed_BGRA8_1048576',
                          'dump_close': 'owned_file'}
            if name.startswith('position_trace_'):
                exact_args[name] = 'diagnostic_out_seconds'
            if name in exact_args and pending.get('args') != exact_args[name]:
                current['chain_valid'] = False
            if name == 'update' and value == 0 and lav_evidence.transport_mode(current):
                current['ready'] = True
                current['loop_observation_consumed'] = False
                current['waiting'] = False
            if name == 'update' and value == 0x40001:
                current['waiting'] = True
            elif name == 'completion_status' and value == 0:
                current['chain_valid'] &= current['waiting']
                current['ready'] = current['waiting']
                current['loop_observation_consumed'] = False
                current['waiting'] = False
            elif name == 'sample_times_ready_diagnostic' and value == 0:
                current['stamp_ready'] = current['ready']
            if name in ('source_lock', 'destination_desc', 'destination_lock', 'destination_unlock', 'source_unlock') and value == 0:
                current['copy_chain'].append(name)
            if name in ('dump_create', 'dump_write', 'dump_close'):
                if (current['header'].get('first_frame_dump') != '1' or len(current['frames']) != 1 or
                        len(current['samples']) != 1 or current['frame_dump'] is not None or row['phase'] != 'pump'):
                    raise ValueError('dump call outside first completed copy')
                if value == 0:
                    current['dump_chain'].append(name)
            if negative and row['phase'] == 'cleanup':
                current['cleanup_errors'].append(row)
            current['last_seq'] = int(row['seq'])
            current['calls'] += 1
            current['returned_calls'].append(dict(name=row['name'], hr=row['hr'], phase=row['phase'], args=pending.get('args'), attempt=int(pending.get('attempt','1')),seq=int(row['seq']),qpc_enter=pending.get('qpc'),qpc_leave=row.get('qpc')))
            current['pending'] = None
        elif lav_evidence.consume(current, kind, row):
            pass
        elif kind == 'MP_SEEK_TRACE':
            index = len(current['position_trace'])
            site = row.get('site')
            if (current['variant'] != 'stop-before-seek' or current['header'].get('seek_position_trace') != '1' or
                    index >= len(POSITION_TRACE_SITES) or site != POSITION_TRACE_SITES[index] or
                    not current['returned_calls'] or current['pending'] is not None):
                raise ValueError('invalid stopped-seek position trace order')
            previous = current['returned_calls'][-1]
            if (previous['name'] != 'position_trace_'+site or previous['phase'] != 'sample-run' or
                    row.get('valid') not in ('0', '1') or
                    (row['valid'] == '1') != (not int(previous['hr'], 16) & 0x80000000) or
                    int(row['requested_ms']) != int(current['header']['start_ms'])):
                raise ValueError('unpaired stopped-seek position trace')
            if not math.isfinite(float(row['actual_seconds'])):
                raise ValueError('invalid stopped-seek position trace value')
            current['position_trace'].append(row)
        elif kind == 'MP_SEEK_POSITION':
            if (current['variant'] != 'stop-before-seek' or current['seek_position'] is not None or
                    row.get('phase') != 'sample-run' or not current['returned_calls'] or
                    current['returned_calls'][-1]['name'] != 'seek_position_after' or
                    int(current['returned_calls'][-1]['hr'], 16) & 0x80000000):
                raise ValueError('invalid stopped-seek position witness')
            actual = float(row['actual_seconds'])
            requested = int(row['requested_ms'])
            if (not math.isfinite(actual) or actual < 0 or
                    requested != int(current['header']['start_ms'])):
                raise ValueError('invalid stopped-seek position value')
            current['seek_position'] = row
        elif kind == 'MP_SKIPPED':
            if (current['variant'] != 'skip-zero-seek' or current['skipped_seek'] or
                    row.get('name') != 'seek' or row.get('reason') != 'skip_zero_seek_variant' or
                    row.get('resume') != 'current_constructor_position' or row.get('start_ms') != '0' or
                    row.get('phase') != 'sample-run' or not current['returned_calls'] or
                    current['returned_calls'][-1]['name'] != 'second_pause'):
                raise ValueError('invalid skipped-seek diagnostic')
            current['skipped_seek'] = True
        elif kind == 'MP_SAMPLE':
            if len(current['samples']) >= 10000:
                raise ValueError('unbounded sample count')
            current['chain_valid'] &= (current['stamp_ready'] and int(row['index']) == len(current['samples']))
            if lav_evidence.loop_mode(current):
                decisions=current.get('lav',{}).get('MP_LAV_LOOP_DECISION',[])
                current['chain_valid'] &= bool(decisions and current['returned_calls'][-1]['name']=='lav_loop_observe' and decisions[-1]['crossing']=='0' and all(decisions[-1].get(k)==row.get(k) for k in ('index','start','end','current')))
            current['samples'].append(row)
            current['stamp_ready'] = current['ready'] = False
            current['copy_chain'] = []
        elif kind == 'MP_FRAME':
            if int(row['index']) != len(current['frames']) or int(row['bytes']) != 1048576 or row['copied'] != '1':
                raise ValueError('invalid frame destination evidence')
            if len(row['hash']) != 16 or not 0 <= int(row['nonzero']) <= 512*512:
                raise ValueError('invalid frame hash/domain')
            int(row['hash'], 16)
            sample = current['samples'][-1] if current['samples'] else {}
            current['chain_valid'] &= all(row.get(k) == sample.get(k) for k in ('index', 'start', 'end'))
            current['chain_valid'] &= current['copy_chain'] == ['source_lock', 'destination_desc', 'destination_lock', 'destination_unlock', 'source_unlock']
            current['frames'].append(row)
        elif kind == 'MP_FRAME_DUMP':
            frame = current['frames'][0] if current['frames'] else {}
            if (current['header'].get('first_frame_dump') != '1' or current['frame_dump'] is not None or
                    current['pending'] is not None or len(current['frames']) != 1 or len(current['samples']) != 1 or
                    current['dump_chain'] != ['dump_create', 'dump_write', 'dump_close'] or
                    [c['name'] for c in current['returned_calls'][-3:]] != ['dump_create', 'dump_write', 'dump_close'] or
                    not all(row.get(k) == frame.get(k) for k in ('index', 'start', 'end', 'hash', 'bytes')) or
                    not all(row.get(k) == v for k, v in dict(width='512', height='512', stride='2048',
                        format='BGRA8_top_down', alpha='255', file='first-frame.bgra').items())):
                raise ValueError('invalid first-frame dump witness')
            current['frame_dump'] = row
        elif kind in ('MP_FAILURE', 'MP_NO_PROGRESS'):
            current['failures'].append(row)
        elif kind == 'MP_BASELINE':
            current['baseline'] = row['success'] == '1'
        elif kind == 'MP_COMPLETE':
            current['completed'] = row['cleanup'] == '1' and row['success'] == '1'
        elif kind == 'MP_TIMEOUT':
            current['timeout'] = row
        elif kind == 'MP_ROUTE':
            current['route'] = row
        elif kind == 'MP_STAGE_RESULT':
            if row['stage'] != current['stage']:
                raise ValueError('stage result mismatch')
            current['exit_code'] = int(row['exit_code'])
            current['chain_valid'] &= (row['timeout'] == str(int(current['timeout'] is not None)))
            if current['timeout']:
                current['chain_valid'] &= (current['exit_code'] != 0 and current['timeout'].get('reaped') == '1')
        elif kind == 'MP_MODULE':
            current['modules'].append(row)
        elif kind == 'MP_FILTER':
            current['filters'].append(row)
        elif kind == 'MP_PIN':
            current['pins'].append(row)
    for item in stages.values():
        samples = item['samples']
        required = ['CoInitialize', 'CreateDevice', 'CreateTexture', 'GetSurfaceLevel', 'activate_stream',
                    'initialize', 'add_video', 'get_graph', 'create_mpeg_video', 'open_file',
                    'get_video', 'qi_ddmedia', 'qi_position', 'qi_control', 'can_seek_forward',
                    'stream_run', 'constructor_pause']
        if item['variant'] == 'lav-explicit':
            required[8:10] = lav_evidence.CONSTRUCT
            required += ['lav_state_probe']
            if lav_evidence.terminal_mode(item):
                i = required.index('get_video')
                required[i:i] = lav_evidence.TERMINAL_ACQUIRE
        if lav_evidence.transport_mode(item):
            i=required.index('get_video')
            required[i:i]=lav_evidence.TRANSPORT_ACQUIRE
        if item['stage'] != 'constructor' and not lav_evidence.transport_mode(item):
            required += ['second_pause']
            if item['variant'] == 'stop-before-seek':
                required += ['stop_before_seek', 'seek', 'seek_position_after', 'restore_pause_after_seek']
            elif item['variant'] == 'lav-explicit':
                if item['header'].get('lav_scenario') == 'seek':
                    required += ['seek']
            elif item['variant'] == 'baseline':
                required += ['seek']
            else:
                item['chain_valid'] &= item['skipped_seek']
            if item['variant'] == 'stop-before-seek' and item['header'].get('seek_position_trace') == '1':
                required += ['position_trace_after_pause', 'create_sample', 'position_trace_after_create_sample',
                             'get_surface', 'get_surface_desc', 'position_trace_before_run', 'control_run', 'position_trace_after_run']
                item['chain_valid'] &= len(item['position_trace']) == 4 and all(r['valid'] == '1' for r in item['position_trace'])
            else:
                required += ['create_sample', 'get_surface', 'get_surface_desc', 'control_run']
        if lav_evidence.transport_mode(item):
            required += ['lav_transport_cleanup_decommit']
        if lav_evidence.terminal_mode(item):
            required += ['lav_terminal_decommit']
        required += ['control_stop', 'stream_stop']
        if item['stage'] != 'constructor':
            required += ['release_sample', 'release_surface']
        required += ['release_ddmedia', 'release_media', 'release_position', 'release_control', 'release_graph',
                     'release_multi', 'release_destination', 'release_texture', 'release_device', 'release_d3d',
                     'destroy_window', 'CoUninitialize']
        if item['variant'] == 'lav-explicit':
            i = required.index('release_position')
            required[i:i] = (lav_evidence.TRANSPORT_RELEASE if lav_evidence.transport_mode(item) else lav_evidence.TERMINAL_RELEASE if lav_evidence.terminal_mode(item) else []) + lav_evidence.RELEASE
            i = required.index('release_graph')
            required[i:i] = ['release_source', 'release_decoder']
            required[-1:-1] = ['lav_deactivate_context', 'lav_release_context']
        calls = item['returned_calls']
        # Failed optional MPEG hint creation is tolerated; the required prefix and cleanup still must run.
        names = [c['name'] for c in calls]
        cursor = 0
        prefix_complete = True
        for name in required:
            try:
                cursor = names.index(name, cursor)+1
            except ValueError:
                prefix_complete = False
                break
        item['prefix_complete'] = prefix_complete
        if item['variant'] == 'skip-zero-seek':
            item['chain_valid'] &= 'seek' not in names
        if item['variant'] != 'stop-before-seek':
            item['chain_valid'] &= not any(name in names for name in ('stop_before_seek', 'seek_position_after', 'restore_pause_after_seek'))
        last_results = {c['name']: int(c['hr'], 16) for c in calls}
        item['required_hresult_success'] = all(not last_results.get(name, 0x80004005) & 0x80000000
                                              for name in required if name not in ('create_mpeg_video', 'can_seek_forward'))
        progressing = []
        for s in samples:
            start, end = int(s['start']), int(s['end'])
            if end >= start and (not progressing or start > progressing[-1]):
                progressing.append(start)
        item['progressing_timestamps'] = len(progressing)
        item['distinct_nonempty_hashes'] = len({f['hash'] for f in item['frames'] if int(f['nonzero']) > 0})
        item['clean_completion'] = bool(item['completed'] and item.get('exit_code') == 0 and
                                        item['pending'] is None and not item['timeout'] and not item['cleanup_errors'])
        item['accepted'] = (item['clean_completion'] and item['baseline'] and not item['failures'] and
                            item['prefix_complete'] and item['required_hresult_success'] and item['chain_valid'])
        if item['stage'] in ('pump', 'copy') and not lav_evidence.loop_mode(item):
            item['accepted'] &= item['progressing_timestamps'] >= 3
        if item['stage'] == 'copy':
            item['accepted'] &= (lav_evidence.loop_mode(item) or item['distinct_nonempty_hashes'] >= 3) and len(item['frames']) == len(samples)
        item['position_trace_first_departure'] = next((r['site'] for r in item['position_trace']
            if r['valid'] == '1' and abs(float(r['actual_seconds'])*1000-int(r['requested_ms'])) > 80), None)
        item['seek_evidence_outcome'] = 'not_applicable'
        item['seek_position_matches'] = False
        item['seek_first_sample_matches'] = False
        if item['variant'] == 'stop-before-seek' and item['stage'] != 'constructor':
            target_ms = int(item['header']['start_ms'])
            if item['seek_position'] is not None:
                item['seek_position_matches'] = abs(float(item['seek_position']['actual_seconds'])*1000-target_ms) <= 40.0
            if samples:
                item['seek_first_sample_matches'] = abs(int(samples[0]['start'])-target_ms*10000) <= 800000
            item['seek_evidence_outcome'] = 'position_readback_matches' if item['seek_position_matches'] else 'position_readback_unqualified'
            if item['stage'] in ('pump', 'copy') and item['seek_position_matches']:
                item['seek_evidence_outcome'] = ('sample_target_matches' if item['seek_first_sample_matches'] else
                                                 'sample_target_or_timestamp_domain_unqualified')
            item['accepted'] &= item['seek_position_matches']
            if item['stage'] in ('pump', 'copy'):
                item['accepted'] &= item['seek_first_sample_matches']
        if item.get('header', {}).get('first_frame_dump') == '1':
            item['accepted'] &= item['frame_dump'] is not None
        lav_structure = bool(item['variant'] == 'lav-explicit' and item['accepted'] and lav_evidence.check_stage(item))
        item['lav_terminal_cleanup_accepted'] = lav_structure and lav_evidence.terminal_mode(item)
        item['lav_transport_epoch_accepted'] = lav_structure and lav_evidence.transport_mode(item)
        item['lav_transport_accepted'] = lav_structure and not lav_evidence.terminal_mode(item) and not lav_evidence.transport_mode(item)
        if lav_evidence.terminal_mode(item) or lav_evidence.transport_mode(item):
            item['accepted'] = False  # intervention is never ordinary constructor acceptance
        original = item.get('header', {}).get('media_kind', 'original_dat') == 'original_dat'
        item['baseline_accepted'] = item['accepted'] and item['variant'] == 'baseline' and original
        item['diagnostic_variant_accepted'] = item['accepted'] and item['variant'] not in ('baseline', 'lav-explicit') and original
        item['outcome'] = ('terminal_allocator_decommit_cleanup_only' if item['lav_terminal_cleanup_accepted'] else
                           'timeout at '+item['timeout']['named_call'] if item['timeout'] else
                           'diagnostic_variant_accepted' if item['diagnostic_variant_accepted'] else
                           'accepted' if item['accepted'] else 'failed_or_incomplete')
        if lav_evidence.transport_mode(item):
            item['lav_dither_settings_verified'] = lav_evidence.check_dither(item)
        del item['returned_calls']  # detailed call evidence stays in the local text log
    if not stages:
        raise ValueError('no supervised stages')
    return dict(stages=stages, accepted=all(s['baseline_accepted'] for s in stages.values()),
                diagnostic_variant_accepted=all(s['diagnostic_variant_accepted'] for s in stages.values()),
                playback_proven=bool(stages.get('copy', {}).get('baseline_accepted')),
                variant_frame_delivery_proven=bool(stages.get('copy', {}).get('diagnostic_variant_accepted')),
                nonzero_seek_frame_delivery_proven=bool(stages.get('copy', {}).get('diagnostic_variant_accepted') and
                    stages['copy']['variant'] == 'stop-before-seek' and int(stages['copy']['header']['start_ms']) > 0))


def fixture_environment(base, runtime, output, seek_backend_trace=False, lav_graph_trace=False):
    """Versioned GST overrides only; native DLL selection stays process-local."""
    env = base.copy()
    env['WINEDLLOVERRIDES'] = 'd3d9=b'
    env['GST_PLUGIN_PATH_1_0'] = str(runtime/'plugins')
    env['GST_REGISTRY_1_0'] = str(output/'registry/fixture.bin')
    env['GST_DEBUG'] = 'GST_PLUGIN_LOADING:6,GST_ELEMENT_FACTORY:4'
    env['GST_DEBUG_NO_COLOR'] = '1'
    env.pop('GST_DEBUG_FILE', None)  # stderr retained separately, not an inherited game capture
    env['WINEDEBUG'] = '-all,+loaddll'
    env.pop('X3M_VOICE_DMO_FALLBACK', None)
    if seek_backend_trace:
        env['WINEDEBUG'] = SEEK_TRACE_WINEDEBUG
        env['GST_DEBUG'] = SEEK_TRACE_GST_DEBUG
    if lav_graph_trace:
        env['WINEDEBUG'] = LAV_GRAPH_TRACE_WINEDEBUG
    return env


def trace_log_limit(seek_backend_trace=False, lav_graph_trace=False):
    return SEEK_TRACE_LIMIT if seek_backend_trace or lav_graph_trace else None


def launcher_prefix(exe, lav_provider=False, lav_graph_trace=False):
    """CrossOver wrapper adapter; ordinary branches retain their existing argv.

    The wrapper overrides host WINEDEBUG and removes WINEDLLOVERRIDES. Its
    supported options must carry these LAV controls before the fixture EXE.
    """
    options = []
    if lav_provider:
        options = ['--debugmsg', LAV_GRAPH_TRACE_WINEDEBUG if lav_graph_trace else '-all,+loaddll',
                   '--dll', 'd3d9=b;winegstreamer=']
    return [bottle.WINE, *bottle.wine_args(), *options, str(exe)]


def terminate_owned_group(proc):
    """Bounded termination of this Popen(start_new_session=True) group only.

    The retained Windows supervisor owns a non-inherited kill-on-close job.
    Closing it on supervisor termination also terminates its fixture children.
    """
    result = dict(owned_process_group=proc.pid, signals=[], reaped=False)
    for sig in (signal.SIGTERM, signal.SIGKILL):
        if proc.poll() is not None:
            result['reaped'] = True
            break
        try:
            os.killpg(proc.pid, sig)
            result['signals'].append(sig.name)
        except ProcessLookupError:
            pass  # raced with normal exit; still reap our Popen child
        except OSError as error:
            result['error'] = repr(error)
            break
        try:
            proc.wait(timeout=5)
            result['reaped'] = True
            break
        except subprocess.TimeoutExpired:
            continue
    result['exit_code'] = proc.poll()
    if not result['reaped'] and result['exit_code'] is not None:
        result['reaped'] = True
    return result


def supervise_host_process(proc, log_paths, outer_seconds, log_limit_bytes=None):
    """Ordinary runs retain wait(); traced runs poll the combined logs at 100ms.

    The cap is a polling threshold, so bytes emitted before termination can
    exceed it. Record actual sizes; never truncate the failure witness.
    """
    record = {}
    if log_limit_bytes is None:
        try:
            return dict(exit_code=proc.wait(timeout=outer_seconds))
        except subprocess.TimeoutExpired:
            record['outer_timeout'] = dict(seconds=outer_seconds, owned_process_group=proc.pid)
    else:
        deadline = time.monotonic()+outer_seconds
        while True:
            exit_code = proc.poll()
            try:
                sizes = {p.name: p.stat().st_size for p in log_paths}
            except OSError as error:
                record['diagnostic_log_monitor_error'] = repr(error)
                break
            record['log_bytes'] = sizes
            if sum(sizes.values()) > log_limit_bytes:
                record['diagnostic_log_limit'] = dict(limit_bytes=log_limit_bytes,
                    observed_bytes=sum(sizes.values()), poll_seconds=SEEK_TRACE_POLL_SECONDS)
                break
            if exit_code is not None:
                record['exit_code'] = exit_code
                return record
            if time.monotonic() >= deadline:
                record['outer_timeout'] = dict(seconds=outer_seconds, owned_process_group=proc.pid)
                break
            time.sleep(SEEK_TRACE_POLL_SECONDS)
    record['termination'] = terminate_owned_group(proc)
    record['exit_code'] = record['termination']['exit_code']
    return record


def diagnostic_outcome(record):
    if record.get('diagnostic_log_limit'):
        return 'log_limit_exceeded_not_backend_seek_failure'
    if record.get('diagnostic_log_monitor_error'):
        return 'log_monitor_error_not_backend_seek_failure'
    if record.get('outer_timeout'):
        return 'outer_timeout'
    return 'completed_observation'


def finalize_result(record, output, stage, stop_before_seek=False):
    """Write evidence even when a diagnostic log disappears or becomes unreadable."""
    log_errors = {}
    try:
        with (output/'stdout.txt').open(errors='replace') as f:
            record.update(validate(f))
    except (ValueError, KeyError) as e:
        record.update(accepted=False, playback_proven=False, validation_error=str(e))
    except OSError as e:
        log_errors['stdout.txt'] = {'read': str(e)}
    # Keep a small module/factory witness; full debug log remains local.
    witnesses = []
    backend_selection = dict(matroskademux=False, avdec_mpeg2video=False, selected_libgstlibav=False)
    try:
        with (output/'stderr.txt').open(errors='replace') as f:
            for line in f:
                if 'GST_ELEMENT_FACTORY' in line and 'creating element' in line:
                    for factory in ('matroskademux', 'avdec_mpeg2video'):
                        backend_selection[factory] |= 'creating element \"'+factory+'\"' in line
                backend_selection['selected_libgstlibav'] |= ('plugin \"'+str(Path(record.get('runtime', '/unselected'))/'plugins/libgstlibav.dylib')+'\" loaded') in line
                if any(token in line.lower() for token in ('libgstlibav', 'avdec_mpeg', 'mpegvideoparse', 'matroska', 'winegstreamer', 'd3d9.dll')):
                    if len(witnesses) < 100:
                        witnesses.append(line.rstrip()[:4096])
    except OSError as e:
        log_errors['stderr.txt'] = {'read': str(e)}
    record['final_log_bytes'] = {}
    for name in ('stdout.txt', 'stderr.txt'):
        try:
            record['final_log_bytes'][name] = (output/name).stat().st_size
        except OSError as e:
            record['final_log_bytes'][name] = None
            log_errors.setdefault(name, {})['stat'] = str(e)
    record['final_log_errors'] = log_errors
    if log_errors:
        # Preserve an earlier monitor failure and its owned-process reaping record.
        record.setdefault('diagnostic_log_monitor_error', {'phase': 'finalization', 'logs': log_errors})
    record['backend_witnesses'] = witnesses
    record['derived_backend_selection'] = backend_selection
    record['backend_modules_verified'] = False  # parent reviews load/factory witnesses; runtime selection alone is not proof
    expected = set(STAGES if stage == 'all' else (stage,))
    record['expected_stages_present'] = set(record.get('stages', {})) == expected
    record['requested_variant_matches'] = all(s['variant'] == record['variant'] for s in record.get('stages', {}).values())
    record['position_trace_expected_matches'] = not stop_before_seek or all(
        s.get('header', {}).get('seek_position_trace') == '1' for s in record.get('stages', {}).values())
    record['first_frame_capture'] = inspect_first_frame(record, output)
    dump_ok = not record.get('dump_first_frame') or record['first_frame_capture']['verified']
    record['media_kind_matches'] = all(s.get('header', {}).get('media_kind', 'original_dat') == record.get('media_kind', 'original_dat')
        for s in record.get('stages', {}).values())
    sound_run = (record['media_kind_matches'] and dump_ok and record['exit_code'] == 0 and all(record['unchanged'].values()) and
                 record['expected_stages_present'] and record['requested_variant_matches'] and
                 record['position_trace_expected_matches'] and not record.get('outer_timeout') and
                 not record.get('diagnostic_log_limit') and not record.get('diagnostic_log_monitor_error') and not log_errors and
                 record.get('termination', {}).get('reaped', True))
    record['diagnostic_variant_accepted'] = record.get('diagnostic_variant_accepted', False) and sound_run
    if not record['diagnostic_variant_accepted']:
        record['variant_frame_delivery_proven'] = False
        record['nonzero_seek_frame_delivery_proven'] = False
    record['accepted'] = (record.get('accepted', False) and record['exit_code'] == 0 and
                          sound_run)
    if not record['accepted']:
        record['playback_proven'] = False
    if record.get('derived_media'):
        record['derived_media_diagnostic_accepted'] = bool(sound_run and record.get('stages', {}).get('copy', {}).get('accepted') and
            record['first_frame_capture']['target_content_exact_match'] and all(backend_selection.values()))
        record['derived_target_content_exact_match'] = record['first_frame_capture']['target_content_exact_match']
        record['derived_media_outcome'] = ('derived_media_diagnostic_accepted' if record['derived_media_diagnostic_accepted'] else
            'derived_media_unqualified_no_original_playback_claim')
        # Preserve per-stage findings, but these media bytes are not the original asset.
        for key in ('accepted', 'playback_proven', 'diagnostic_variant_accepted',
                    'variant_frame_delivery_proven', 'nonzero_seek_frame_delivery_proven'):
            record[key] = False
    if record.get('lav_transport'):
        record['lav_transport_trace']=lav_evidence.trace_transport(output/'stderr.txt',record.get('stages',{}).get('copy',{}))
    if record.get('lav_provider'):
        lav_evidence.finish(record, output, sound_run, fnv64)
        if record.get('lav_derived_source'):
            record['derived_media_outcome'] = record['lav_qualification']['outcome']
    record['diagnostic_outcome'] = diagnostic_outcome(record)
    (output/'result.json').write_text(json.dumps(record, indent=2)+'\n')


def lav_derived_preflight(record):
    """Stage A: inspect existing files only; no media generation or Wine."""
    started=time.monotonic();tool=shutil.which('ffprobe')
    if not tool:raise ValueError('native ffprobe required for derived timeline preflight')
    derivation=record['derived_media'];data=derivation['record']
    if data.get('schema')!=2:raise ValueError('LAV derived source requires schema2')
    # validate_derivation already hashes both immutable media files and all
    # retained native frame witnesses. Full payload identity is reused from that
    # bound preparation record; current packet hashes audit the target prefix.
    observations=[];commands=[]
    for domain in ('original','derived'):
        command=[tool,'-v','error','-select_streams','v:0','-read_intervals','%+#280',
                 '-show_packets','-show_streams','-show_data_hash','sha256','-show_entries',
                 'packet=pts,dts,size,data_hash,flags:stream=codec_name,width,height,avg_frame_rate,r_frame_rate,time_base',
                 '-of','json',data[domain]['path']]
        result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30,check=True)
        if len(result.stdout)>1024*1024:raise ValueError('oversized bounded packet metadata')
        observations.append(json.loads(result.stdout));commands.append(command)
    proof=lav_evidence.packet_mapping(*observations)
    proof.update(commands=commands,ffprobe_version=subprocess.check_output([tool,'-version'],timeout=10,text=True).splitlines()[0],
                 derived_media_record_sha256=derivation['sha256'],
                 original_reference_sha256=record['lav_original_reference_result']['sha256'],
                 full_payload_identity='reused_hash_bound_preparation_record',wall_seconds=time.monotonic()-started)
    _,witnesses,oracle=lav_evidence.load_original_oracle(record,fnv64)
    if record['lav_transport'] in ('epochs','boundary','pending'):
        lav_evidence.load_derived_linear(record,witnesses,oracle,fnv64)
    if record['lav_transport'] in ('boundary','pending'):
        lav_evidence.load_extended_original(record,witnesses,oracle,fnv64)
    if record['lav_transport']=='pending':
        lav_evidence.load_boundary_result(record,fnv64)
    return proof


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', required=True, type=Path)
    p.add_argument('--exe-sha256', required=True)
    p.add_argument('--media', type=Path, default=bottle.game_dir()/'mov/00002.dat')
    p.add_argument('--runtime', type=Path, help='fixture-only v4/runtime or v5/runtime directory')
    p.add_argument('--backend', required=True)
    p.add_argument('--lav-provider-record', type=Path, help='explicit pinned unregistered x86 LAV provider')
    p.add_argument('--lav-graph-provider-record',type=Path,help='explicit reviewed STRICT COM assembly; new graph modes only')
    p.add_argument('--lav-multigop-reference-result',type=Path,help='qualified same-EXE original31 RGB oracle')
    p.add_argument('--lav-cold-interval-result',type=Path,help='exact retained cold interval/control observation; seek-only-recovery only')
    p.add_argument('--lav-pending-result',type=Path,help='accepted composite Gate P prerequisite for seek-only-interval')
    p.add_argument('--lav-integer-matrix-result',type=Path,help='accepted preserved integer15 graph prerequisite for pending-integer')
    p.add_argument('--lav-library-control-result',type=Path,help='saved qualified library15-target control')
    p.add_argument('--lav-library-strict-result',type=Path,help='saved qualified STRICT library15-target result')
    p.add_argument('--lav-transport', choices=lav_evidence.TRANSPORT_MODES, help='stopped transport epochs; exact same-provider capture oracle, never engine playback')
    p.add_argument('--lav-scenario', choices=('reference', 'seek'), default='seek')
    p.add_argument('--lav-reference-result', type=Path, help='same-EXE/provider original-ES sequential result for exact content comparison')
    p.add_argument('--lav-derived-source', action='store_true', help='separate derived-source LAV reference/epochs counter; never original-ES seek acceptance')
    p.add_argument('--lav-extended-reference-result',type=Path,help='qualified v10 original oracle including raw249–257')
    p.add_argument('--lav-boundary-result',type=Path,help='qualified derived boundary runtime prerequisite for pending')
    p.add_argument('--lav-original-reference-result', type=Path, help='pinned v8 original sequential raw pixel/time oracle')
    p.add_argument('--derived-media-record', type=Path, help='hash-bound stream-copy Matroska provenance; never original-media playback acceptance')
    p.add_argument('--allow-generated-timestamps', action='store_true', help='explicit schema2 derived-timeline diagnostic; never original timeline equivalence')
    p.add_argument('--dump-first-frame', action='store_true', help='copy-stage only: save the first actual destination as private packed BGRA8')
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--stage', choices=('all', *STAGES), default='all')
    p.add_argument('--config', type=lambda s: int(s, 0), choices=(0, 0x4000), default=0)
    p.add_argument('--start-ms', type=int, default=0)
    p.add_argument('--frames', type=int, default=6)
    p.add_argument('--call-ms', type=int, default=10000)
    p.add_argument('--total-ms', type=int, default=60000)
    p.add_argument('--progress-ms', type=int, default=10000)
    variants = p.add_mutually_exclusive_group()
    variants.add_argument('--stop-before-seek', action='store_true', help='diagnostic only: Stop, original seek, position readback, restore Pause; requires position and sample timing evidence')
    variants.add_argument('--skip-zero-seek', action='store_true', help='diagnostic only: omit zero seek and resume constructor position; never exact game playback')
    p.add_argument('--seek-backend-trace', action='store_true', help='scoped Wine/GStreamer seek diagnostics, 64MiB combined-log threshold; uses the retained EXE')
    p.add_argument('--lav-terminal-decommit', action='store_true', help='constructor-only terminal allocator Decommit before Stop; never playback or normal-constructor proof')
    p.add_argument('--lav-graph-trace', action='store_true', help='constructor-only LAV graph/amstream trace, 64MiB combined-log threshold; retained EXE unchanged')
    p.add_argument('--diagnostics', action='store_true', help='enumerate graph and Windows modules after baseline, before cleanup')
    a = p.parse_args()
    if a.lav_derived_source and (not a.lav_provider_record or a.lav_transport not in ('reference','epochs','boundary','pending','reference-matrix','integer-matrix','pending-integer','seek-only-interval','seek-only-recovery') or
            not a.derived_media_record or not a.allow_generated_timestamps or not a.lav_original_reference_result):
        p.error('derived LAV requires explicit reference/epochs transport, schema2 opt-in and original reference')
    if a.lav_transport=='reference-extended' and (a.lav_derived_source or not a.lav_original_reference_result):
        p.error('extended original oracle requires original source and pinned original reference')
    if a.lav_transport=='boundary' and not a.lav_derived_source:
        p.error('boundary is derived-source only')
    if a.lav_derived_source and a.lav_transport in ('boundary','pending') and not a.lav_extended_reference_result:
        p.error('boundary/pending requires qualified extended original oracle')
    if a.lav_derived_source and a.lav_transport=='pending' and not a.lav_boundary_result:
        p.error('derived pending requires successful boundary runtime')
    if a.lav_extended_reference_result and a.lav_transport not in lav_evidence.GRAPH_MODES and (not a.lav_derived_source or a.lav_transport not in ('boundary','pending')):
        p.error('extended oracle binding is boundary/pending only')
    if a.lav_boundary_result and (not a.lav_derived_source or a.lav_transport!='pending'):
        p.error('boundary prerequisite is derived pending only')
    if a.lav_original_reference_result and not a.lav_derived_source and a.lav_transport not in ('reference-extended','reference-matrix'):
        p.error('original reference binding requires --lav-derived-source')
    graph_mode=a.lav_transport in lav_evidence.GRAPH_MODES
    if graph_mode:
        if not a.lav_extended_reference_result or not a.lav_original_reference_result:
            p.error('new graph mode requires retained original12 and extended15 references')
        if a.lav_derived_source:
            if not a.lav_graph_provider_record or not a.lav_multigop_reference_result:
                p.error('derived graph mode requires STRICT assembly and original31 reference')
        elif a.lav_transport!='reference-matrix' or a.lav_graph_provider_record or a.lav_multigop_reference_result:
            p.error('original31 requires official unseeked reference mode')
        if a.lav_transport=='integer-matrix' and (not a.lav_library_control_result or not a.lav_library_strict_result):
            p.error('integer graph requires qualified control and STRICT library matrix results')
    if a.lav_transport in ('pending-integer','seek-only-interval','seek-only-recovery') and not a.lav_integer_matrix_result:
        p.error('pending-integer requires accepted preserved integer15 graph')
    if a.lav_integer_matrix_result and a.lav_transport not in ('pending-integer','seek-only-interval','seek-only-recovery'):
        p.error('integer15 prerequisite requires pending-integer or seek-only-interval')
    if bool(a.lav_pending_result)!=(a.lav_transport in ('seek-only-interval','seek-only-recovery')):
        p.error('seek-only-interval requires its accepted composite Gate P prerequisite exclusively')
    if bool(a.lav_cold_interval_result)!=(a.lav_transport=='seek-only-recovery'):
        p.error('seek-only-recovery requires its exact retained cold interval/control exclusively')
    if a.lav_transport=='seek-only-recovery' and a.backend!='lav-cold-miss-recovery-v1':
        p.error('seek-only-recovery requires backend label lav-cold-miss-recovery-v1')
    if a.lav_graph_provider_record and (not graph_mode or not a.lav_derived_source):
        p.error('composed provider is explicit derived graph-mode only')
    if a.lav_multigop_reference_result and (not graph_mode or not a.lav_derived_source):
        p.error('original31 binding is derived graph-mode only')
    if (a.lav_library_control_result or a.lav_library_strict_result) and a.lav_transport!='integer-matrix':
        p.error('library matrix bindings are integer-matrix only')
    if a.lav_provider_record and a.derived_media_record and not a.lav_derived_source:
        p.error('derived LAV requires --lav-derived-source')
    if a.lav_transport and (not a.lav_provider_record or a.stage!='copy' or a.lav_terminal_decommit or a.dump_first_frame or not a.lav_graph_trace or
            a.lav_scenario!=('reference' if a.lav_transport in lav_evidence.REFERENCE_MODES else 'seek') or
            a.start_ms!=(10000 if a.lav_transport in lav_evidence.REFERENCE_MODES else 0) or a.frames!=(260 if a.lav_transport in lav_evidence.REFERENCE_MODES else 6)):
        p.error('transport requires explicit traced LAV copy, fixed reference260/start10000 or seek6/start0, no terminal/dump variant')
    if a.lav_terminal_decommit and (not a.lav_provider_record or a.stage != 'constructor' or a.start_ms or
                                    a.lav_scenario != 'seek' or a.lav_reference_result or a.dump_first_frame):
        p.error('--lav-terminal-decommit requires an explicit LAV constructor at zero, no reference or frame capture')
    if a.lav_graph_trace and (not a.lav_provider_record or a.stage != 'constructor' and not a.lav_transport):
        p.error('--lav-graph-trace requires an explicit LAV constructor trial')
    if a.lav_provider_record:
        if (a.runtime or a.skip_zero_seek or a.stop_before_seek or a.seek_backend_trace or a.config or
                a.stage not in ('constructor', 'copy') or a.start_ms not in (0, 10000) or not a.diagnostics or
                (a.stage == 'copy' and not a.dump_first_frame and not a.lav_transport)):
            p.error('LAV requires explicit constructor/copy, original ES, diagnostics, config0; no other backend/seek variants')
        if a.lav_scenario == 'reference':
            if a.stage != 'copy' or a.start_ms != 10000 or a.frames != 260 or a.lav_reference_result:
                p.error('LAV reference requires copy, start-ms10000, frames260, no reference input')
        elif a.stage == 'copy' and (a.frames != 6 or not a.lav_reference_result):
            p.error('LAV seek copy requires six frames and same-provider reference result')
        try:
            a.lav_provider_record = a.lav_provider_record.resolve()
            verify_record(a.lav_provider_record)
        except (OSError, ValueError, KeyError) as error:
            p.error('invalid LAV provider: '+str(error))
    elif not a.runtime or a.lav_reference_result or a.lav_scenario != 'seek':
        p.error('baseline requires --runtime; LAV arguments require --lav-provider-record')
    graph_provider=None
    if a.lav_graph_provider_record:
        from prepare_lav_graph_provider import verify_record as verify_graph_provider
        try:
            a.lav_graph_provider_record=a.lav_graph_provider_record.resolve()
            graph_provider=verify_graph_provider(a.lav_graph_provider_record,(bottle.bottle_dir().resolve(),))
            if graph_provider['inputs']['official']['record_sha256']!=digest(a.lav_provider_record):
                raise ValueError('compiled/selected official provider binding differs')
        except (OSError,ValueError,KeyError,TypeError) as error:p.error('invalid graph composition: '+str(error))
    if a.allow_generated_timestamps and not a.derived_media_record:
        p.error('--allow-generated-timestamps requires --derived-media-record schema2')
    if a.dump_first_frame and a.stage != 'copy':
        p.error('--dump-first-frame requires --stage copy')
    if a.derived_media_record and not a.lav_derived_source and (not a.dump_first_frame or not a.stop_before_seek or a.stage != 'copy' or a.start_ms <= 0):
        p.error('derived media requires a nonzero stopped-seek copy run with --dump-first-frame')
    if a.skip_zero_seek and a.start_ms != 0:
        p.error('--skip-zero-seek requires --start-ms 0')
    if os.environ.get('X3M_FIXTURE_BOTTLE') != 'X3':
        p.error('set X3M_FIXTURE_BOTTLE=X3 explicitly and invoke through wine_lock.py')
    if game_running():
        p.error('game is running')
    a.exe, a.media, a.output = a.exe.resolve(), a.media.resolve(), a.output.resolve()
    # Preserve /tmp spelling: these runtime libraries use retained absolute install names.
    if a.runtime:
        a.runtime = Path(os.path.abspath(a.runtime.expanduser()))
    if a.exe.is_relative_to(bottle.game_dir().resolve()) or a.output.is_relative_to(bottle.game_dir().resolve()):
        p.error('fixture EXE and output must be outside the game directory')
    if a.exe.with_name('d3d9.dll').exists():
        p.error('fixture directory contains an app-local d3d9.dll')
    original_media = (bottle.game_dir()/'mov/00002.dat').resolve()
    if a.lav_provider_record and ((a.media != original_media and not a.lav_derived_source) or a.lav_provider_record.is_relative_to(bottle.game_dir().resolve())):
        p.error('LAV trial requires the original game 00002.dat and a provider outside the game directory')
    derivation = None
    if a.derived_media_record:
        if a.media.is_relative_to(bottle.game_dir().resolve()):
            p.error('derived media must be outside the game directory')
        try:
            derivation = validate_derivation(a.derived_media_record.resolve(), a.media, original_media, 10000 if a.lav_derived_source else a.start_ms, a.allow_generated_timestamps)
        except (OSError, ValueError, KeyError, TypeError) as error:
            p.error('invalid derived-media provenance: '+str(error))
    elif a.media.name != '00002.dat' or not a.media.is_file():
        p.error('expected read-only original 00002.dat; Matroska requires --derived-media-record')
    if a.runtime and (not (a.runtime/'plugins/libgstlibav.dylib').is_file() or not (a.runtime/'lib').is_dir()):
        p.error('missing private runtime plugin/dependencies')
    if digest(a.exe) != a.exe_sha256:
        p.error('retained EXE hash mismatch')
    if not 3 <= a.frames <= 10000 or not 0 <= a.start_ms <= 3600000 or not all(0 < n <= 3600000 for n in (a.call_ms, a.total_ms, a.progress_ms)):
        p.error('fixture argument out of bounds')
    a.output.mkdir(parents=True, exist_ok=False)
    (a.output/'registry').mkdir()
    env = fixture_environment(os.environ, a.runtime or a.output/'unused-gst', a.output, a.seek_backend_trace, a.lav_graph_trace)
    if a.lav_provider_record:
        # Reject accidental backend bridge loading. No global Wine configuration.
        env['WINEDLLOVERRIDES'] = 'd3d9=b;winegstreamer='
        env['GST_PLUGIN_PATH_1_0'] = ''
        env['GST_PLUGIN_SYSTEM_PATH_1_0'] = ''
    launch = launcher_prefix(a.exe, bool(a.lav_provider_record), a.lav_graph_trace)
    command = [*launch, '--media', 'Z:'+str(a.media).replace('/', '\\'),
               '--stage', a.stage, '--config', str(a.config), '--start-ms', str(a.start_ms), '--frames', str(a.frames),
               '--call-ms', str(a.call_ms), '--total-ms', str(a.total_ms), '--progress-ms', str(a.progress_ms)]
    if a.lav_provider_record:
        command += ['--lav-manifest', 'Z:'+str((a.lav_graph_provider_record or a.lav_provider_record).parent/'provider.manifest').replace('/', '\\')]
        if a.lav_scenario == 'reference':
            command.append('--lav-reference')
        if a.lav_terminal_decommit:
            command.append('--lav-terminal-decommit')
        if a.lav_transport:
            command += ['--lav-transport', a.lav_transport]
    if derivation:
        command.append('--derived-media')
    if a.dump_first_frame:
        command.append('--dump-first-frame')
    if a.diagnostics:
        command.append('--diagnostics')
    if a.skip_zero_seek:
        command.append('--skip-zero-seek')
    if a.stop_before_seek:
        command.append('--stop-before-seek')
    protected = dict(media=a.media, game_exe=bottle.game_dir()/'X3AP.exe', bottle_config=bottle.bottle_dir()/'cxbottle.conf')
    if a.lav_transport in ('pending-integer','seek-only-interval','seek-only-recovery'):protected['pending_build_record']=a.exe.with_suffix('.build.json')
    if derivation:
        protected['original_media'] = original_media
        protected['derived_media_record'] = a.derived_media_record.resolve()
    if a.lav_provider_record:
        protected['lav_provider_record'] = a.lav_provider_record
        for name in verify_record(a.lav_provider_record)['files']:
            protected['lav_file_'+name] = a.lav_provider_record.parent/name
        if a.lav_reference_result:
            a.lav_reference_result = a.lav_reference_result.resolve()
            protected['lav_reference_result'] = a.lav_reference_result
    if a.lav_graph_provider_record:
        protected['lav_graph_provider_record']=a.lav_graph_provider_record
        for name in graph_provider['files']:
            protected['lav_graph_file_'+name]=a.lav_graph_provider_record.parent/name
        from owned_lav_provider import protected_paths as owned_protected_paths
        from owned_lav_provider import verify_strict_record
        strict_path=Path(graph_provider['inputs']['strict']['record_path'])
        review_path=Path(graph_provider['inputs']['patch_review']['path'])
        selected=verify_strict_record(strict_path,review_path)
        protected.update({'graph_input_'+k:v for k,v in owned_protected_paths(strict_path,selected).items()})
        protected['graph_patch_review']=review_path
    if a.lav_original_reference_result:
        a.lav_original_reference_result=a.lav_original_reference_result.resolve()
        protected['lav_original_reference_result']=a.lav_original_reference_result
    for name in ('lav_extended_reference_result','lav_boundary_result','lav_multigop_reference_result','lav_library_control_result','lav_library_strict_result','lav_integer_matrix_result','lav_pending_result','lav_cold_interval_result'):
        path=getattr(a,name)
        if path:
            path=path.resolve();setattr(a,name,path);protected[name]=path
    before = {k: digest(v) for k, v in protected.items()}
    if derivation and (before['media'] != derivation['record']['derived']['sha256'] or
                       before['original_media'] != derivation['record']['original']['sha256'] or
                       before['derived_media_record'] != derivation['sha256']):
        p.error('derived-media provenance changed before launch')
    record = dict(schema=1, variant='lav-explicit' if a.lav_provider_record else 'stop-before-seek' if a.stop_before_seek else 'skip-zero-seek' if a.skip_zero_seek else 'baseline', backend=a.backend, bottle=bottle.describe(), command=command,
                  media_kind='derived_stream_copy_matroska' if derivation else 'original_dat',
                  derived_media=derivation, dump_first_frame=a.dump_first_frame,
                  original_timeline_playback_proven=False,
                  media=dict(path=str(a.media), size=a.media.stat().st_size, sha256=before['media']),
                  exe=dict(path=str(a.exe), sha256=a.exe_sha256), runtime=str(a.runtime),
                  plugin_sha256=digest(a.runtime/'plugins/libgstlibav.dylib') if a.runtime else None,
                  environment={k: env.get(k) for k in ('WINEDLLOVERRIDES', 'GST_PLUGIN_PATH_1_0', 'GST_REGISTRY_1_0',
                               'GST_DEBUG', 'WINEDEBUG', 'GST_PLUGIN_PATH', 'GST_PLUGIN_SYSTEM_PATH', 'DYLD_LIBRARY_PATH')},
                  exact_game_config_known=False, exact_game_seek_known=False, exact_game_destination_known=False,
                  destination='fixture-owned managed 512x512 X8R8G8B8 texture surface', native_windows_run=False,
                  seek_backend_trace=dict(enabled=a.seek_backend_trace,
                      combined_log_limit_bytes=SEEK_TRACE_LIMIT if a.seek_backend_trace else None,
                      poll_seconds=SEEK_TRACE_POLL_SECONDS if a.seek_backend_trace else None,
                      timings_are_performance_measurements=False))
    if a.lav_provider_record:
        record.update(lav_provider=dict(path=str(a.lav_provider_record), sha256=before['lav_provider_record']),
                      lav_scenario=a.lav_scenario, lav_start_ms=a.start_ms, lav_stage=a.stage,
                      lav_terminal_decommit=a.lav_terminal_decommit, lav_transport=a.lav_transport,
                      lav_derived_source=a.lav_derived_source,
                      lav_original_reference_result=dict(path=str(a.lav_original_reference_result),sha256=before['lav_original_reference_result']) if a.lav_original_reference_result else None,
                      lav_transport_settings=dict(lav_evidence.TRANSPORT_SETTINGS) if a.lav_transport else None,
                      lav_reference_result=dict(path=str(a.lav_reference_result), sha256=before['lav_reference_result']) if a.lav_reference_result else None)
        for name in ('lav_extended_reference_result','lav_boundary_result','lav_multigop_reference_result','lav_library_control_result','lav_library_strict_result','lav_integer_matrix_result','lav_pending_result','lav_cold_interval_result'):
            path=getattr(a,name)
            if path:record[name]=dict(path=str(path),sha256=before[name])
        if a.lav_derived_source and a.lav_transport in ('boundary','pending'):
            record['lav_boundary_contract']=dict(lav_evidence.BOUNDARY_CONTRACT)
            record['lav_linear_reference_compatibility']=dict(policy='v10_capture_and_fixed_scenarios_only',
                reference_exe_sha256=lav_evidence.DERIVED_REFERENCE_EXE,current_exe_sha256=record['exe']['sha256'])
        if a.lav_graph_provider_record:
            record['lav_graph_provider']=dict(path=str(a.lav_graph_provider_record),sha256=before['lav_graph_provider_record'])
        record['launcher_controls'] = dict(adapter='CrossOver_wrapper_supported_options', argv_prefix=launch,
            wrapper_sha256=digest(Path(bottle.WINE)), explicit_debugmsg=True, explicit_dll_override=True,
            effective_child_environment_observed=False,
            environment_record_meaning='host_launch_intent; wrapper argv explicitly carries desired child controls',
            prior_run_limitation='Earlier environment-only WINEDEBUG/WINEDLLOVERRIDES intent was not enforced by the wrapper; observed graph/module facts remain evidence.')
    record['lav_graph_trace'] = dict(enabled=a.lav_graph_trace,
        wine_debug=LAV_GRAPH_TRACE_WINEDEBUG if a.lav_graph_trace else None,
        combined_log_limit_bytes=trace_log_limit(lav_graph_trace=a.lav_graph_trace),
        poll_seconds=SEEK_TRACE_POLL_SECONDS if a.lav_graph_trace else None,
        timings_are_performance_measurements=False, acceptance_upgrade=False)
    build_record = a.exe.with_suffix('.build.json')
    if build_record.is_file():
        record['build'] = json.loads(build_record.read_text())
    if graph_mode:
        try:
            if record.get('build',{}).get('lav_provider')!=verify_record(a.lav_provider_record):
                raise ValueError('new graph EXE compile-time official header/provider binding differs')
            if a.lav_transport=='pending-integer':record['lav_graph_crossbuild_compatibility']=dict(lav_evidence.GRAPH_PENDING_COMPATIBILITY)
            if a.lav_transport=='seek-only-interval':record['lav_graph_crossbuild_compatibility']=dict(lav_evidence.GRAPH_LOOP_COMPATIBILITY)
            if a.lav_transport=='seek-only-recovery':record['lav_graph_crossbuild_compatibility']=dict(lav_evidence.GRAPH_RECOVERY_COMPATIBILITY)
            record['lav_graph_preflight']=lav_evidence.graph_preflight(record,fnv64)
        except (OSError,ValueError,KeyError,TypeError) as error:p.error('graph matrix preflight failed: '+str(error))
    if a.lav_transport=='reference-extended':
        try:
            lav_evidence.load_original_oracle(record,fnv64)
        except (OSError,ValueError,KeyError,TypeError) as error:
            p.error('extended original oracle preflight failed before Wine: '+str(error))
    if a.lav_derived_source:
        try:
            record['lav_derived_preflight']=lav_derived_preflight(record)
        except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
            p.error('derived LAV preflight failed before Wine: '+str(error))
    started = time.monotonic()
    with (a.output/'stdout.txt').open('wb') as out, (a.output/'stderr.txt').open('wb') as err:
        # A fresh process group contains only this runner's loader/supervisor.
        # The supervisor's non-inherited Windows job kills its own children if
        # supervisor termination closes the job. No process-name or bottle kill.
        outer_seconds = (4 if a.stage == 'all' else 1)*(a.total_ms/1000+5)+30
        proc = subprocess.Popen(command, cwd=a.output, env=env, stdout=out, stderr=err, start_new_session=True)
        record.update(supervise_host_process(proc, [a.output/'stdout.txt', a.output/'stderr.txt'],
                                             outer_seconds, trace_log_limit(a.seek_backend_trace, a.lav_graph_trace)))
    record['wall_seconds'] = time.monotonic()-started
    record['unchanged'] = {k: digest(v) == before[k] for k, v in protected.items()}
    finalize_result(record, a.output, a.stage, a.stop_before_seek)
    print(json.dumps({k: record.get(k) for k in ('accepted', 'diagnostic_variant_accepted', 'playback_proven', 'variant_frame_delivery_proven', 'nonzero_seek_frame_delivery_proven', 'exit_code', 'wall_seconds', 'diagnostic_outcome', 'derived_media_diagnostic_accepted', 'derived_media_outcome', 'lav_qualification', 'unchanged', 'validation_error')}))
    return 0 if record['accepted'] or record['diagnostic_variant_accepted'] or record.get('derived_media_diagnostic_accepted') or record.get('lav_qualification', {}).get('accepted') or record.get('lav_qualification', {}).get('terminal_decommit_diagnostic_accepted') or record.get('lav_qualification', {}).get('transport_diagnostic_accepted') or record.get('lav_qualification', {}).get('derived_transport_diagnostic_accepted') or record.get('lav_qualification', {}).get('extended_original_reference_ready') or any(record.get('lav_qualification',{}).get(k) for k in ('graph_original_reference_ready','graph_derived_reference_ready','graph_integer_matrix_accepted','graph_pending_integer_accepted','graph_seek_only_interval_accepted','graph_cold_miss_recovery_accepted')) else 2


if __name__ == '__main__':
    raise SystemExit(main())
