"""Scoped evidence for the worker sample experiment; no old gate inheritance."""
import hashlib
import json
from bisect import bisect_left, bisect_right
from pathlib import Path
from urllib.parse import unquote

import media_lav_evidence as old
from run_media_playback_fixture import digest, fnv64

KIND = 'worker_dd_two_session_reuse_v1'
EOF_KIND = 'worker_eof_fresh_graph_v1'
EOF_TARGET = 19393200000
EOF_REFERENCE_SHA = '29caa8a5d2dcfa29c3096b88a4799c26692ad8924032d796d933c173f2289326'
EOF_REFERENCE_EXE = '1a28505f4529ece7eb30bfd4d530aed5ecfc4dafe6c1a567a8a88eecc9f6b780'
CAPTURES = 24
FRAME_BYTES = 1048576
HELPER = old.GRAPH_PENDING_HELPER
TARGETS = (0, 100000000, 22000000, 100000000)


def require(value, message):
    if not value:
        raise ValueError(message)


def binding(path):
    path = Path(path).resolve()
    return dict(path=str(path), sha256=digest(path))


def references(record):
    """Reuse frozen content loaders, with an explicit new-build compatibility scope.

    Never pretend the new worker EXE equals the old sequential fixture. The
    unchanged helper/settings, verified cohorts and actual RGB/time comparison
    are the compatibility evidence for this different ownership configuration.
    """
    original_path, original = old.graph_result_binding(record, 'original_reference')
    strict_path, strict = old.graph_result_binding(record, 'strict_reference')
    require(original.get('lav_transport') == strict.get('lav_transport') == 'reference-matrix', 'reference modes differ')
    require(original.get('exe', {}).get('sha256') == strict.get('exe', {}).get('sha256') == old.GRAPH_REFERENCE_EXE,
            'reference executable differs')
    require(original['lav_qualification'].get('graph_original_reference_ready') and
            strict['lav_qualification'].get('graph_derived_reference_ready'), 'reference readiness absent')
    require(original.get('media_kind') == 'original_dat' and not original.get('lav_graph_provider') and
            strict.get('media_kind') == 'derived_stream_copy_matroska', 'reference source roles differ')
    require(original.get('lav_provider') == strict.get('lav_provider') == record['lav_provider'], 'official cohort binding differs')
    require(strict.get('lav_graph_provider') == record['lav_graph_provider'], 'strict cohort binding differs')
    require(strict.get('lav_multigop_reference_result') == record['original_reference'], 'strict original reference binding differs')
    require(strict.get('media') == record['media'] and
            strict.get('derived_media', {}).get('sha256') == record['derived_media']['sha256'], 'source derivation binding differs')
    require(all(x.get('build', {}).get('lav_helper_sha256') == HELPER for x in (original, strict, record)), 'frozen helper differs')
    require(record['build'].get('kind') == 'worker_dd_reuse_build_v1' and
            record['build'].get('exe_sha256') == record['exe']['sha256'], 'new build identity differs')
    require(original['media']['sha256'] == record['derived_media']['record']['original']['sha256'], 'original source differs')
    if record.get('mode') == 'worker-eof-fresh':
        return eof_references(record)
    ow, pixels = old.graph_capture_content(original_path, original, fnv64, 'graph_capture_sha256')
    sw, sp = old.graph_capture_content(strict_path, strict, fnv64, 'graph_capture_sha256')
    require(old.graph_oracle_layout(ow) and old.graph_oracle_layout(sw), 'reference layout differs')
    require(pixels == sp and old.graph_sample_labels(original['stages']['copy']) == old.graph_sample_labels(strict['stages']['copy']),
            'original/strict RGB or sample labels differ')
    old.graph_original_overlap(original, ow, pixels, fnv64)
    expected = []
    for generation, target in enumerate(TARGETS, 1):
        eligible = [(w, raw) for w, raw in zip(ow, pixels) if int(w['start']) >= target][:6]
        require(len(eligible) == 6, 'six successors absent')
        expected.extend(dict(session=1 if generation<4 else 2, generation=generation, start=int(w['start'])-target, end=int(w['end'])-target,
                             rgb_sha256=rgb_digest(raw), raw=raw) for w, raw in eligible)
    return expected


def eof_references(record):
    """Frozen suffix V2 mapping supplies exact six full-file terminal pictures."""
    ref_path, ref = old.graph_result_binding(record, 'eof_reference')
    require(record['eof_reference']['sha256']==EOF_REFERENCE_SHA and ref['exe']['sha256']==EOF_REFERENCE_EXE,
            'frozen EOF reference identity differs')
    require(ref['lav_qualification'].get('eof_reference_accepted') and ref['lav_eof_preflight'].get('event_setup_protocol')==2,
            'EOF reference protocol not qualified')
    require(ref['lav_provider']==record['lav_provider'] and ref['build']['lav_helper_sha256']==record['build']['lav_helper_sha256']==HELPER,
            'EOF reference cohort/helper differs')
    require(record['build'].get('kind')=='worker_dd_reuse_build_v1' and record['build']['exe_sha256']==record['exe']['sha256'], 'EOF new build identity differs')
    mapping_path=Path(ref['lav_tail_reference_result']['path'])
    require(digest(mapping_path)==ref['lav_tail_reference_result']['sha256'], 'EOF mapping changed')
    mapping_record=json.loads(mapping_path.read_text())
    require(ref['lav_tail_reference_result']['sha256']==ref['lav_eof_preflight']['mapping_sha256']=='50d390ce417ccf8ac7db68dcefff7f33f405a64b0e4cc1cbf52ea52219e1c106', 'EOF mapping binding differs')
    require(mapping_record['measurement_complete'] and not mapping_record['errors'] and mapping_record['artifact']['sha256']==ref['media']['sha256'], 'EOF suffix mapping incomplete')
    mapping=ref['lav_qualification']['eof_reference_captures']
    require(len(mapping)==16 and [x['index'] for x in mapping]==list(range(16)), 'EOF suffix mapping differs')
    expected=[]
    for session in (1,2):
        for index, frame in enumerate(mapping[10:16]):
            path=ref_path.parent/f'transport-e0-f{frame["index"]}.bgra'
            require(path.is_file() and not path.is_symlink() and path.stat().st_size==FRAME_BYTES, 'EOF reference capture missing')
            raw=path.read_bytes()
            require(hashlib.sha256(raw).hexdigest()==frame['sha256'] and fnv64(raw)==frame['fnv64'], 'EOF reference capture changed')
            require(mapping_record['mapping']['frames'][frame['index']]['derived_pts']*10000==frame['mapped_absolute_start_100ns'], 'EOF packet mapping differs')
            start=frame['mapped_absolute_start_100ns']-EOF_TARGET
            end=frame['mapped_absolute_end_100ns']-EOF_TARGET
            require((start,end)==(index*400000,(index+1)*400000), 'EOF exact full-file time mapping differs')
            expected.append(dict(session=session,generation=session,start=start,end=end,rgb_sha256=rgb_digest(raw),raw=raw))
    return expected


def rgb_digest(raw):
    return hashlib.sha256(raw[0::4]+raw[1::4]+raw[2::4]).hexdigest()


def fields(line):
    return dict(part.split('=', 1) for part in line.strip().split()[1:] if '=' in part)


def parse(text):
    rows = {}
    session = '0'
    for line in text.splitlines():
        name = line.split(' ', 1)[0]
        if name.startswith(('MW_', 'MP_LAV_', 'MP_FAILURE')):
            row = fields(line)
            if name == 'MW_SESSION' and row.get('state') == 'begin':
                session = row.get('session', '0')
            if name.startswith(('MP_LAV_', 'MP_FAILURE')):
                row['session'] = session
            rows.setdefault(name, []).append(row)
            if name == 'MW_SESSION' and row.get('state') == 'end':
                session = '0'
    return rows


def one(rows, name):
    values = rows.get(name, [])
    require(len(values) == 1, 'missing/duplicate '+name)
    return values[0]


def integer(row, key):
    return int(row[key])


def successful(hr):
    return int(hr, 16) < 0x80000000


def overlap_summary(events, frequency):
    heartbeats = [r for r in events if r['name'] == 'heartbeat']
    operations = [r for r in events if r['name'] == 'operation']
    summary = []
    for op in operations:
        begin, end = integer(op, 'begin'), integer(op, 'end')
        completed = [h for h in heartbeats if begin <= integer(h, 'end') <= end]
        contained = [h for h in completed if integer(h, 'begin') >= begin]
        intersected = [h for h in heartbeats if integer(h, 'begin') < end and integer(h, 'end') > begin]
        ends = [integer(h, 'end') for h in heartbeats]
        gaps = [(b-a)*1000/frequency for a, b in zip(ends, ends[1:]) if a < end and b > begin]
        summary.append(dict(max_completion_gap_ms=max(gaps, default=None), operation=op['label'], session=integer(op, 'session'), operation_id=integer(op, 'a'), begin=begin, end=end,
                            duration_ms=(end-begin)*1000/frequency, completed_heartbeats=len(completed),
                            contained_heartbeats=len(contained), intersected_heartbeats=len(intersected),
                            observation='concurrent_completion' if completed else 'incomplete_no_concurrent_completion',
                            max_heartbeat_api_ms=max(((integer(h, 'end')-integer(h, 'begin'))*1000/frequency for h in intersected), default=None)))
    ends = [integer(h, 'end') for h in heartbeats]
    return dict(operations=summary, count=len(heartbeats),
                max_completion_gap_ms=max(((b-a)*1000/frequency for a, b in zip(ends, ends[1:])), default=None),
                max_clear_ms=max(((integer(h, 'a')-integer(h, 'begin'))*1000/frequency for h in heartbeats), default=None),
                max_present_ms=max(((integer(h, 'end')-integer(h, 'a'))*1000/frequency for h in heartbeats), default=None),
                responsiveness_budget_defined=False, universal_no_stall_claim=False)


FATAL_EVENTS = {2, 3, 6, 7, 8, 0x45}
CLEANUP_FIELDS = ('graph_guard_safe', 'graph_interfaces_released', 'service_cleanup_safe',
                  'cpu_slots_retired', 'operation_success', 'events_clean')


def validate_event_drains(rows, require_clean=True):
    """Check the local queue protocol, including incomplete negative witnesses."""
    worker = [r for r in rows.get('MW_EVENT', []) if r['owner'] == 'worker']
    drains = [r for r in worker if r['name'] == 'drain']
    all_batches = [r for r in worker if r['name'] == 'event_batch']
    require(drains, 'event drains absent')
    positions = {id(r): i for i, r in enumerate(worker)}
    end_ticks = [integer(r,'end') for r in worker]
    require(end_ticks == sorted(end_ticks), 'worker trace not chronological')
    batch_ticks = [integer(r,'begin') for r in all_batches]
    anchors = [r for r in worker if r['name']=='command' and r['label']=='take' or r['name']=='deadline_anchor']
    anchor_ticks = [integer(r,'end') for r in anchors]
    writings = [r for r in worker if r['name']=='slot' and r['label']=='writing']
    writing_positions = [positions[id(r)] for r in writings]
    posts_by_key = {}
    for r in rows.get('MW_EVENT', []):
        if r['owner']=='main' and r['name']=='command' and r['label']=='post':
            posts_by_key.setdefault(tuple(r[k] for k in 'abcde'), []).append(r)
    seen_batches, seen_calls = [], []
    summaries = []
    for drain in drains:
        inside = lambda r: r['session'] == drain['session'] and integer(drain, 'begin') <= integer(r, 'begin') <= integer(r, 'end') <= integer(drain, 'end')
        batches = [r for r in all_batches[bisect_left(batch_ticks, integer(drain,'begin')):bisect_right(batch_ticks, integer(drain,'end'))] if inside(r)]
        require(batches and len(batches) == integer(drain, 'f'), 'event batch coverage missing')
        require([integer(r, 'b') for r in batches] == list(range(1, len(batches)+1)), 'event batch ordinal differs')
        anchor_index = bisect_right(anchor_ticks, integer(drain,'begin'))-1
        require(anchor_index>=0, 'immutable event deadline anchor missing')
        anchor = anchors[anchor_index]
        if anchor['name']=='command':
            posts = posts_by_key.get(tuple(anchor[k] for k in 'abcde'), [])
            require(len(posts)==1 and integer(posts[0],'e')==integer(posts[0],'begin')<=integer(anchor,'begin'), 'command deadline anchor not bound to post')
        else:
            position = positions[id(anchor)]
            previous = worker[position-1]
            writing_index = bisect_left(writing_positions, position)-1
            require(previous['name']=='call' and previous['label'] in ('update','completion') and previous['hr']=='00000000' and writing_index>=0 and all(anchor[k]==writings[writing_index][k] for k in ('b','c')), 'frame deadline anchor not bound to sample completion')
        tick = integer(anchor, 'd' if anchor['name']=='command' else 'a')
        require(integer(drain, 'd') == tick, 'event deadline anchor reset')
        total = 0
        previous_batch_position = bisect_left(end_ticks, integer(drain,'begin'))-1
        for index, batch in enumerate(batches):
            require(batch['a'] == drain['b'] and integer(batch, 'd') == tick, 'event deadline/drain changed between batches')
            reason = batch['label']
            require(reason in ('Empty', 'More', 'PollError', 'FreeError', 'ProviderError', 'CompletionError', 'UnknownEvent', 'Deadline', 'Cancelled', 'Storage'), 'unknown event batch outcome')
            require(index == len(batches)-1 or reason == 'More', 'batch after terminal outcome')
            elapsed = (integer(batch, 'e')-tick) & 0xffffffff
            require((elapsed >= 10000) if reason=='Deadline' else (elapsed < 10000 or reason in ('Storage', 'Cancelled', 'PollError', 'FreeError', 'ProviderError')), 'event deadline exceeded without failure')
            require(integer(batch, 'f') == 131072-integer(batch, 'index') and integer(batch, 'f') >= 450, 'retirement trace reserve missing')
            items = [r for r in worker[max(previous_batch_position+1, bisect_left(end_ticks, integer(batch,'begin'))):positions[id(batch)]] if r['session']==drain['session'] and integer(batch, 'begin') <= integer(r, 'begin') <= integer(r, 'end') <= integer(batch, 'end') and
                     (r['name']=='provider_event' or r['name']=='call' and r['label'] in ('worker_event_poll', 'worker_event_free'))]
            retrieved, pos, terminal, failure = 0, 0, None, None
            while pos < len(items):
                poll = items[pos];pos += 1
                require(poll['name']=='call' and poll['label']=='worker_event_poll', 'event poll/free pairing differs')
                seen_calls.append(poll)
                if poll['hr'] != '00000000':
                    terminal = 'Empty' if poll['hr']=='80004004' else 'PollError'
                    require(pos == len(items), 'poll after terminal HRESULT')
                    break
                require(pos+1 < len(items), 'retrieved event not freed')
                event, freed = items[pos:pos+2];pos += 2
                require(event['name']=='provider_event' and event['label']=='scalar' and freed['name']=='call' and freed['label']=='worker_event_free', 'retrieved event not freed exactly once')
                retrieved += 1;total += 1;seen_calls.extend((event, freed))
                require(event['a']==drain['b'] and integer(event, 'b')==total, 'event scalar sequence differs')
                if freed['hr']!='00000000': failure='FreeError'
                elif integer(event, 'c') in FATAL_EVENTS: failure='ProviderError'
                if failure:
                    require(pos==len(items), 'event read after provider/free failure')
            require(retrieved==integer(batch, 'c') and retrieved<=32, 'event batch retrieval count differs')
            if reason=='More':
                require(retrieved==32 and terminal is None and failure is None, 'More is not a full benign batch')
            elif reason=='Empty':
                require(terminal=='Empty' and failure is None, 'false Empty or hidden provider error')
            elif reason in ('CompletionError', 'UnknownEvent'):
                require(failure is None and terminal is None and retrieved and items[-2]['name']=='provider_event' and (integer(items[-2],'c')==1 if reason=='CompletionError' else integer(items[-2],'c') not in FATAL_EVENTS|{1,10,13}), 'EOF event classification differs')
            elif reason in ('PollError', 'FreeError', 'ProviderError'):
                require((failure or terminal)==reason, 'event error classification differs')
            else:
                require(failure is None and terminal!='PollError' and (reason=='Deadline' or not items), 'terminal policy discarded an event')
                if reason=='Cancelled':
                    require(drain['label']!='cleanup_stopped' and any(r['owner']=='main' and r['name']=='abort' and integer(r, 'begin') <= integer(batch, 'end') for r in rows['MW_EVENT']), 'cancellation lacks main abort or affects cleanup')
            require((batch['hr']=='00000000') == (reason in ('Empty', 'More')), 'batch HRESULT/outcome differs')
            if reason=='Storage':
                require(integer(batch,'f') < 612, 'storage exhaustion without exhausted batch budget')
            seen_batches.append(batch)
            previous_batch_position = positions[id(batch)]
        clean = batches[-1]['label']=='Empty'
        require(integer(drain, 'e')==total and integer(drain, 'c')==int(clean), 'drain summary differs')
        require(not require_clean or clean, 'event drain incomplete')
        summaries.append(dict(session=int(drain['session']), site=drain['label'], events=total, batches=len(batches), outcome=batches[-1]['label']))
    require(len(seen_batches)==len(all_batches), 'unbound event batch')
    expected = [r for r in worker if r['name']=='provider_event' or r['name']=='call' and r['label'] in ('worker_event_poll', 'worker_event_free')]
    require(len(seen_calls)==len(expected) and {id(r) for r in seen_calls}=={id(r) for r in expected}, 'unobserved event poll/free')
    return summaries


def cleanup_observation(rows):
    """Physical cleanup is independently observable even after operation failure."""
    result = one(rows, 'MW_RESULT')
    events = rows.get('MW_EVENT', [])
    main = [r for r in events if r['owner']=='main']
    exits = [r for r in main if r['name']=='worker_exit']
    require(len(exits)==1, 'worker exit absent for CPU disposal')
    final = [r for r in main if r['name']=='cpu_final']
    require(len(final)==3 and {integer(r, 'a') for r in final}=={0,1,2} and all(integer(r, 'begin')>=integer(exits[0], 'end') for r in final), 'final CPU slot observation absent')
    # Reconstruct each lease, including failure-only disposal after worker exit.
    states = {i: 'free' for i in range(3)}
    for row in sorted((r for r in events if r['name']=='slot'), key=lambda r: (integer(r, 'begin'), integer(r, 'index'))):
        slot, label = integer(row, 'a'), row['label']
        require(slot in states, 'invalid CPU slot')
        previous = states[slot]
        owner = 'worker' if label in ('writing', 'ready', 'abandon') else 'main'
        require(row['owner']==owner, 'wrong CPU disposal owner')
        require((previous, label) in (('free','writing'), ('writing','ready'), ('ready','reading'), ('reading','free'), ('writing','abandon')), 'invalid CPU lease transition')
        if label=='abandon':
            require(any(g['name']=='retirement' and g['a']=='1' and g['session']==row['session'] and integer(g,'end')<=integer(row,'begin') for g in events), 'WRITING abandoned before public retirement')
        states[slot]='free' if label=='abandon' else label
    for row in final:
        require(integer(row,'b')=={'free':0,'writing':1,'ready':2,'reading':3}[states[integer(row,'a')]], 'CPU final state differs from leases')
    for discard in (r for r in main if r['name']=='discard' and r['label']=='failed_exit'):
        require(integer(discard,'begin')>=integer(exits[0],'end'), 'failure CPU disposal before worker exit')
        lease = [r for r in main if r['name']=='slot' and all(r[k]==discard[k] for k in ('a','b','c'))]
        require(len(lease)==2 and [r['label'] for r in lease]==['reading','free'] and integer(exits[0],'end')<=integer(lease[0],'begin')<=integer(discard,'begin')<=integer(lease[1],'end'), 'failure discard missing consumer lease')
    retired = all(r['b']=='0' for r in final)
    require(result.get('cpu_slots_retired')==str(int(retired)), 'CPU retirement result differs')
    require(all(result.get(k) in ('0','1') for k in CLEANUP_FIELDS), 'separate cleanup outcomes absent')
    return dict(cpu_slots_retired_verified=retired, failed_exit_discarded=sum(r['name']=='discard' and r['label']=='failed_exit' for r in main))


def validate_rows(rows):
    """Reconstruct ownership, enabled event drains and both graph lifetimes."""
    header = one(rows, 'MW_HEADER')
    eof = header.get('mode')=='worker-eof-fresh'
    captures, sources = (12,12) if eof else (CAPTURES,27)
    require(header.get('kind') == (EOF_KIND if eof else KIND) and header.get('mode') == ('worker-eof-fresh' if eof else 'worker-dd-reuse') and
            header.get('slots') == '3' and header.get('capture_limit') == str(captures) and
            header.get('bytes_per_frame') == str(FRAME_BYTES) and header.get('graph_clock') == 'none_explicit' and header.get('event_policy') == 'worker_batches32_v1', 'header contract differs')
    frequency = integer(header, 'qpc_frequency')
    require(frequency > 0, 'invalid QPC frequency')
    traces = rows.get('MW_TRACE', [])
    require(len(traces) == 2 and {r['owner'] for r in traces} == {'main', 'worker'} and
            all(r['overflow'] == '0' for r in traces), 'missing/overflowed timing data')
    events = rows.get('MW_EVENT', [])
    groups = {owner: [r for r in events if r['owner'] == owner] for owner in ('main', 'worker')}
    require(len(events) == sum(map(len, groups.values())), 'unknown timing owner')
    owners = {}
    for owner, group in groups.items():
        require([integer(r, 'index') for r in group] == list(range(len(group))), 'timing sequence hole')
        require(len(group) == integer(next(r for r in traces if r['owner'] == owner), 'count'), 'timing count differs')
        threads = {r['thread'] for r in group}
        require(len(threads) == 1 and '0' not in threads, 'inconsistent thread ownership')
        owners[owner] = next(iter(threads))
        require(all(integer(r, 'begin') > 0 and integer(r, 'end') >= integer(r, 'begin') and r['session'] in ('0', '1', '2') for r in group), 'invalid timing span/session')
        require(not any(r['name'] == 'failure' for r in group), 'runtime failure')
    require(owners['main'] == header['main_thread'] and owners['main'] != owners['worker'], 'owner threads not distinct')
    main, worker = groups['main'], groups['worker']
    select = lambda name, group=worker: [r for r in group if r['name'] == name]
    require(not select('call', main) and not any(r['name'] in ('heartbeat', 'destination', 'admit') for r in worker), 'wrong-thread API ownership')
    calls = select('call')
    require(calls and all(integer(b, 'begin') >= integer(a, 'end') for a, b in zip(calls, calls[1:])), 'overlapping worker control calls')
    terminal = {'00000000', '00040002', '00040003', '80004004'}
    for r in calls:
        allowed = r['label'] in ('seek_abort', 'seek_settled', 'cleanup_abort', 'cleanup_settled', 'worker_event_poll') and r['hr'] == '80004004'
        require(successful(r['hr']) or allowed, 'unexpected HRESULT: '+r['label'])
    operations = select('operation')
    expected_operations = [('service_initialize', 0), ('construct', 1), ('seek', 1), ('seek', 2), ('seek', 3),
                           ('session_cleanup', 1), ('construct', 2), ('seek', 4), ('session_cleanup', 2), ('service_cleanup', 0)]
    if eof:
        expected_operations=[('service_initialize',0),('construct',1),('seek',1),('session_cleanup',1),('construct',2),('seek',2),('session_cleanup',2),('service_cleanup',0)]
    require([(r['label'], integer(r, 'a')) for r in operations] == expected_operations and all(r['b'] == '1' for r in operations), 'operation completion absent')
    require(all(integer(a, 'end') <= integer(b, 'begin') for a, b in zip(operations, operations[1:])), 'session operations overlap')
    service = [r for r in calls if r['session'] == '0']
    service_names = [r['label'] for r in service]
    for name in ('CoInitialize', 'CreateWindow', 'DirectDrawCreateEx', 'SetCooperativeLevel', 'qi_dd', 'service_dd_identity',
                 'release_dd_identity', 'release_dd', 'release_dd7', 'destroy_worker_window', 'CoUninitialize'):
        require(sum(r['label'] == name for r in calls) == 1 and service_names.count(name) == 1, 'missing/recreated service resource: '+name)
    cooperative = select('cooperative')
    require(len(cooperative) == 1 and cooperative[0]['session'] == '0' and integer(cooperative[0], 'a') == 0x408 and
            cooperative[0]['b'] == owners['worker'] and successful(cooperative[0]['hr']), 'private cooperative contract differs')
    dd = select('dd_identity')
    ready = select('service')
    require(len(ready) == 1 and integer(ready[0], 'a') != 0 and [r['session'] for r in dd] == ['1', '2'] and
            all(r['b'] == r['c'] == ready[0]['a'] and r['d'] == '1' and r['e'] == r['f'] == '0' for r in dd), 'public DirectDraw IUnknown identity differs')
    require([(r['session'], r['state']) for r in rows.get('MW_SESSION', [])] == [('1', 'begin'), ('1', 'end'), ('2', 'begin'), ('2', 'end')], 'session boundaries absent')
    retained_lav = {'lav_release_'+name for name in ('transport_allocator', 'transport_input', 'events', 'seeking', 'sink_in', 'decoder_out', 'decoder_in', 'source_out', 'file', 'sink', 'video_settings', 'source_settings')}
    retained_native = {'release_'+name for name in ('sample', 'surface', 'ddmedia', 'media', 'control', 'source', 'decoder', 'graph_filter', 'notify', 'graph', 'multi')}
    lifetimes=(('1',(1,),operations[1],operations[3]),('2',(2,),operations[4],operations[6])) if eof else (('1',(1,2,3),operations[1],operations[5]),('2',(4,),operations[6],operations[8]))
    for session, generations, construct, cleanup in lifetimes:
        owned = [r for r in worker if r['session'] == session]
        scalls = [r for r in calls if r['session'] == session]
        names = [r['label'] for r in scalls]
        named = lambda name: [r for r in scalls if r['label'] == name]
        for name in ('activate_stream', 'get_graph', 'qi_notify', 'set_notify_flags', 'add_video', 'session_get_dd', 'session_dd_identity',
                     'create_sample', 'get_surface', 'set_sync_source', *retained_native, *retained_lav):
            require(names.count(name) == 1, 'missing/recreated session resource: '+name)
        require(all(integer(construct, 'begin') <= integer(r, 'begin') <= integer(r, 'end') <= integer(cleanup, 'end') for r in scalls), 'session call outside lifetime')
        # No assumption that a fresh graph receives a different memory address.
        require(names.index('get_graph')+1 == names.index('qi_notify') and
                names.index('set_notify_flags') < names.index('lav_create_context') < names.index('set_sync_source') < names.index('stream_run') < names.index('run'), 'event/no-clock setup ordering differs')
        if eof:
            require(names.count('support_seeking')==1 and names.index('lav_create_context')<names.index('support_seeking')<names.index('stream_run') and named('support_seeking')[0]['hr']=='00000000', 'EOF SupportSeeking TRUE setup absent')
        clocks = [r for r in owned if r['name'] == 'clock']
        require([(r['label'], integer(r, 'a')) for r in clocks] == [('setup', 0)]+[(site, gen) for gen in generations for site in ('after_seek', 'after_run')] and
                all(r['b'] == '1' and r['hr'] == '00000000' for r in clocks), 'explicit no-clock observations missing')
        surfaces = [r for r in owned if r['name'] == 'surface']
        require(len(surfaces) == 2 and {r['label'] for r in surfaces} == {'caps_format', 'masks_rect'}, 'per-session surface observations absent')
        desc = next(r for r in surfaces if r['label'] == 'caps_format')
        masks = next(r for r in surfaces if r['label'] == 'masks_rect')
        require(desc['b'] == desc['c'] == '512' and desc['e'] == '32' and int(desc['f']) & 0x40 and masks['e'] == masks['f'] == '512', 'source format unsuitable')
        identities = [r for r in owned if r['name'] == 'identity']
        require([integer(r, 'a') for r in identities] == list(generations) and
                len({tuple(r[k] for k in ('b', 'c', 'd')) for r in identities}) == 1 and
                all(integer(identities[0], k) for k in ('b', 'c', 'd')), 'within-session sample identity changed')
        drains = [r for r in owned if r['name'] == 'drain']
        expected_drains = [('before_seek', 1), ('seek_stopped', 1), ('after_six', 1), ('before_seek', 2), ('seek_stopped', 2), ('after_six', 2),
                           ('queue_full', 2), ('before_seek', 3), ('seek_stopped', 3), ('after_six', 3), ('cleanup_stopped', 3)] if session == '1' else [('before_seek', 4), ('seek_stopped', 4), ('after_six', 4), ('cleanup_stopped', 4)]
        if eof:
            sites=[r['label'] for r in drains]
            require(sites[:3]==['before_seek','seek_stopped','after_six'] and sites[-1]=='cleanup_stopped' and sites[3:-1] and all(x=='eof_wait' for x in sites[3:-1]), 'EOF event drain boundary differs')
            expected_drains=[(site,int(session)) for site in sites]
        require([(r['label'], integer(r, 'a')) for r in drains] == expected_drains and
                [integer(r, 'b') for r in drains] == list(range(1, len(drains)+1)) and all(r['c'] == '1' for r in drains), 'bounded provider-event drains absent')
        flags = [r for r in owned if r['name'] == 'notify']
        require([r['label'] for r in flags] == ['initial', 'enable', 'configured']+['before_drain']*len(drains) and
                all(r['a'] == session and r['hr'] == '00000000' for r in flags) and
                all(r['c'] == '0' for r in flags[1:]), 'event queue configuration/readback differs')
        require(integer(flags[2], 'end') <= integer(named('lav_create_context')[0], 'begin'), 'event queue enabled after provider construction')
        for observation in flags:
            previous = worker[integer(observation, 'index')-1]
            require(previous['name'] == 'call' and previous['session'] == session and previous['hr'] == observation['hr'] and
                    previous['label'] == ('set_notify_flags' if observation['label'] == 'enable' else 'get_notify_flags'), 'unpaired event flag HRESULT')
        for flag, drain in zip(flags[3:], drains):
            require(flag['b'] == drain['b'] and integer(flag, 'end') <= integer(drain, 'begin'), 'drain without enabled event queue')
        guard = [r for r in owned if r['name'] == 'retirement']
        require(len(guard) == 1 and guard[0]['a'] == guard[0]['c'] == '1' and guard[0]['d'] == '0', 'safe retirement absent')
        for label in ('cleanup_stop', 'cleanup_stream_stop', 'cleanup_state'):
            found = named(label)
            require(len(found) == 1 and found[0]['hr'] == '00000000' and integer(found[0], 'end') <= integer(guard[0], 'begin'), 'retirement stop/state failed')
        for label in ('cleanup_abort', 'cleanup_settled'):
            require(len(named(label)) == 1 and named(label)[0]['hr'] in terminal, 'sample retirement incomplete')
        releases = [r for r in scalls if r['label'] in retained_native|retained_lav]
        require(all(integer(r, 'begin') >= integer(guard[0], 'end') and integer(r, 'begin') >= integer(drains[-1], 'end') for r in releases), 'release before retirement')
        require(integer(drains[-1], 'begin') >= integer(named('cleanup_stream_stop')[0], 'end'), 'cleanup event drain before Stop')
        done = [r for r in owned if r['name'] == 'session_cleanup']
        require(len(done) == 1 and done[0]['a'] == session and done[0]['b'] == '3' and
                all(integer(r, 'end') <= integer(done[0], 'begin') for r in releases), 'session resources/CPU slots not retired')
    positions = select('seek_position')
    require([(integer(r, 'a'), integer(r, 'b'), integer(r, 'c')) for r in positions] == [(g, t, t) for g, t in enumerate((EOF_TARGET,EOF_TARGET) if eof else TARGETS, 1)], 'integer seek observations differ')
    heartbeats = select('heartbeat', main)
    require(len(heartbeats) >= 2 and all(integer(r, 'begin') <= integer(r, 'a') <= integer(r, 'end') and
            0 <= integer(r, 'b') < 0x80000000 and 0 <= integer(r, 'c') < 0x80000000 for r in heartbeats), 'actual Clear/Present missing/failed')
    commands, takes = select('command', main), select('command')
    expected_commands = [(1, 1, 0), (2, 2, 10000), (2, 3, 2200), (4, 4, 10000), (3, 4, 0)]
    if eof: expected_commands=[(1,1,EOF_TARGET),(4,2,EOF_TARGET),(3,2,0)]
    require([tuple(integer(r, k) for k in ('a', 'b', 'c')) for r in commands] == expected_commands and
            [tuple(integer(r, k) for k in ('a', 'b', 'c')) for r in takes] == expected_commands and
            all(integer(a, 'begin') <= integer(b, 'begin') and a['d']==b['d'] and a['e']==b['e']==a['begin'] for a, b in zip(commands, takes)), 'bounded command publication differs')
    require(integer(heartbeats[0], 'end') <= integer(commands[0], 'begin') <= integer(takes[0], 'begin') <= integer(operations[0], 'begin'), 'cold initialization before heartbeat/construct command')
    for call in service:
        op = operations[-1] if call['label'] in ('release_dd_identity', 'release_dd', 'release_dd7', 'destroy_worker_window', 'CoUninitialize') else operations[0]
        require(integer(op, 'begin') <= integer(call, 'begin') <= integer(call, 'end') <= integer(op, 'end'), 'service API outside measured cold/cleanup interval')
    full = select('queue', main)
    require(not full if eof else len(full)==1 and (full[0]['a'],full[0]['b'],full[0]['c'])==('2','3','12'), 'missing natural queue-full witness')
    invalidates = select('invalidate', main)
    require([(r['a'], r['b']) for r in invalidates] == ([('1','2'),('2','3')] if eof else [('1','2'),('2','3'),('3','4'),('4','5')]), 'generation invalidation absent')
    if not eof: require(integer(full[0], 'begin') <= integer(invalidates[1], 'begin') <= integer(commands[2], 'begin'), 'cancel order differs')
    admissions, discards = select('admit', main), select('discard', main)
    sequences = list(range(12)) if eof else list(range(12))+list(range(15,27))
    require(len(admissions) == captures and [integer(r, 'a') for r in admissions] == list(range(captures)) and
            [integer(r, 'c') for r in admissions] == sequences, 'admission count/sequence differs')
    require([integer(r, 'b') for r in admissions] == ([1]*6+[2]*6 if eof else [1]*6+[2]*6+[3]*6+[4]*6) and
            all(r['b'] == r['f'] and r['session'] == (r['b'] if eof else '1' if integer(r,'b')<4 else '2') for r in admissions), 'stale generation/session admitted')
    for invalidation in invalidates:
        require(all(integer(r, 'b') >= integer(invalidation, 'b') for r in admissions if integer(r, 'begin') >= integer(invalidation, 'begin')), 'admission after invalidation is stale')
    require([(integer(r, 'b'), integer(r, 'c'), integer(r, 'd')) for r in discards] == ([] if eof else [(2,i,3) for i in range(12,15)]), 'three stale slots not discarded')
    slots = select('slot', events)
    require(len(slots) == sources*4, 'missing/unexpected slot ownership transition')
    for seq in range(sources):
        chain = [r for r in slots if integer(r, 'c') == seq]
        by_label = {r['label']: r for r in chain}
        require(len(chain) == 4 and set(by_label) == {'writing', 'ready', 'reading', 'free'}, 'slot lease incomplete')
        ordered = [by_label[k] for k in ('writing', 'ready', 'reading', 'free')]
        gen = (1 if seq<6 else 2) if eof else 1 if seq<6 else 2 if seq<15 else 3 if seq<21 else 4
        require(all(integer(r, 'a') == seq%3 and integer(r, 'b') == gen and r['session'] == (str(gen) if eof else '1' if seq<21 else '2') for r in ordered), 'slot session/identity differs')
        require([r['owner'] for r in ordered] == ['worker', 'worker', 'main', 'main'] and
                all(integer(a, 'begin') <= integer(b, 'begin') for a, b in zip(ordered, ordered[1:])), 'lease publication order differs')
        if seq>=3:
            previous = next(r for r in slots if r['label'] == 'free' and integer(r, 'c') == seq-3)
            require(integer(previous, 'begin') <= integer(ordered[0], 'begin'), 'worker overwrote leased slot')
        if not eof and 12<=seq<15:
            require(integer(ordered[1], 'begin') <= integer(full[0], 'begin') and integer(operations[4], 'end') <= integer(ordered[2], 'begin'), 'full queue released before seek completed')
        if seq<(6 if eof else 21):
            require(integer(ordered[-1], 'end') <= integer(operations[3 if eof else 5], 'begin'), 'session1 lease survived retirement')
    copies = select('destination', main)
    require(len(copies) == captures and [integer(r, 'b') for r in copies] == sequences and
            all(r['c'] == '1' and r['d'] == str(FRAME_BYTES) and successful(r['hr']) for r in copies), 'actual destination comparison missing/failed')
    for copied, admitted in zip(copies, admissions):
        require(copied['a'] == admitted['b'] and copied['session'] == admitted['session'] and integer(copied, 'end') <= integer(admitted, 'begin'), 'admission preceded destination verification')
    source = select('source_copy')
    require(len(source) == sources and [integer(r, 'b') for r in source] == list(range(sources)) and
            all(r['c'] == str(FRAME_BYTES) and integer(r, 'd')>=2048 and r['e'] == '1' and successful(r['hr']) for r in source), 'source copy evidence differs')
    for copied in source:
        seq = integer(copied, 'b')
        writing = next(r for r in slots if r['label'] == 'writing' and integer(r, 'c') == seq)
        published = next(r for r in slots if r['label'] == 'ready' and integer(r, 'c') == seq)
        require(integer(writing, 'begin') <= integer(copied, 'begin') <= integer(copied, 'end') <= integer(published, 'begin'), 'source copy outside writing lease')
    cleanup, exits = select('cleanup'), select('worker_exit', main)
    require(len(cleanup) == len(exits) == 1 and cleanup[0]['a'] == exits[0]['a'] == '1' and
            integer(exits[0], 'begin') >= integer(operations[-1], 'end'), 'safe cleanup/actual worker exit missing')
    require(all(integer(r, 'begin') >= integer(operations[-2], 'end') for r in service if r['label'].startswith('release_')), 'service released before session retirement')
    require(one(rows, 'MW_RESULT') == dict(ok='1', admitted=str(captures), safe_cleanup='1', **{k:'1' for k in CLEANUP_FIELDS}) and not rows.get('MW_TIMEOUT') and not rows.get('MP_FAILURE'), 'fixture failed/incomplete')
    if eof: validate_eof_boundaries(rows, lifetimes)
    event_drains = validate_event_drains(rows)
    cleanup_outcomes = cleanup_observation(rows)
    capture_rows = rows.get('MW_CAPTURE', [])
    require(len(capture_rows) == captures, 'capture count differs')
    for index, (capture, admitted) in enumerate(zip(capture_rows, admissions)):
        require(capture.get('index') == str(index) and capture.get('file') == f'worker-f{index:02}.bgra' and
                capture.get('bytes') == str(FRAME_BYTES) and capture.get('written') == '1' and capture.get('session') == admitted['session'] and
                all(capture.get(key) == admitted[field] for key, field in (('generation', 'b'), ('sequence', 'c'), ('start', 'd'), ('end', 'e'))), 'capture/admission binding differs')
    return dict(owners=owners, event_drains=event_drains, cleanup_outcomes=cleanup_outcomes, admitted=captures, discarded=0 if eof else 3, source_copied_bytes=sources*FRAME_BYTES,
                destination_copied_bytes=captures*FRAME_BYTES, heartbeat=overlap_summary(events, frequency),
                source_surface=select('surface'), actual_destination_compared=True, safe_cleanup_observed=True,
                event_queue_observability_verified=True, two_sessions_retired=True, retained_dd_identity_verified=True)


def validate_eof_boundaries(rows, lifetimes):
    worker=[r for r in rows['MW_EVENT'] if r['owner']=='worker']
    main=[r for r in rows['MW_EVENT'] if r['owner']=='main']
    for session, generations, construct, cleanup in lifetimes:
        owned=[r for r in worker if r['session']==session]
        eos=[r for r in owned if r['name']=='eof' and r['label'] in ('eof_update','eof_completion')]
        complete=[r for r in owned if r['name']=='eof' and r['label']=='graph_complete']
        paired=[r for r in owned if r['name']=='eof' and r['label']=='paired_empty']
        stop=[r for r in owned if r['name']=='eof' and r['label']=='stop_unchanged']
        require(len(eos)==len(complete)==len(paired)==len(stop)==1, 'fresh per-session EOF/completion pair missing or duplicate')
        require(eos[0]['a']==session and eos[0]['b']=='6' and eos[0]['hr']=='00040003', 'sample EOF terminal differs')
        previous=worker[worker.index(eos[0])-1]
        require(previous['name']=='call' and previous['label']==eos[0]['label'] and previous['hr']=='00040003', 'EOF public method origin absent')
        require(complete[0]['a']==session and complete[0]['b']=='6' and complete[0]['c']=='1' and complete[0]['d']=='0', 'early/failed graph completion')
        raw=[r for r in owned if r['name']=='provider_event']
        require(all(integer(r,'c') in {1,10,13} for r in raw) and sum(r['c']=='1' for r in raw)==1, 'unknown/fatal/duplicate EOF graph event')
        event=next(r for r in raw if r['c']=='1')
        require(event['d']=='0' and integer(event,'end')<=integer(complete[0],'begin'), 'EC_COMPLETE HRESULT differs')
        require(stop[0]['a']==session and stop[0]['d']==stop[0]['f']==str(EOF_TARGET) and stop[0]['c']==stop[0]['e'] and integer(stop[0],'e')>=EOF_TARGET+2400000, 'EOF stop changed or integer seek differs')
        before=[r for r in owned if r['name']=='call' and r['label']=='eof_positions_before']
        after=[r for r in owned if r['name']=='call' and r['label']=='eof_positions_after']
        seek=[r for r in owned if r['name']=='call' and r['label']=='integer_seek']
        require(len(before)==len(after)==len(seek)==1 and before[0]['hr']==after[0]['hr']==seek[0]['hr']=='00000000' and integer(before[0],'end')<=integer(seek[0],'begin')<=integer(seek[0],'end')<=integer(after[0],'begin')<=integer(after[0],'end')<=integer(stop[0],'begin'), 'EOF actual GetPositions contract differs')
        guard=next(r for r in owned if r['name']=='retirement')
        require(guard['b']=='0', 'EOF pending guard not cleared by public terminal')
        require(paired[0]['a']==session and paired[0]['b']=='6' and paired[0]['c']=='1' and max(integer(eos[0],'end'),integer(complete[0],'end'))<=integer(paired[0],'begin')<integer(cleanup,'begin'), 'EOF pair not collected before retirement')
        ready=[r for r in owned if r['name']=='slot' and r['label']=='ready']
        require(len(ready)==6 and integer(ready[-1],'end')<=min(integer(eos[0],'begin'),integer(complete[0],'begin')), 'completion before six terminal pictures')
        require(not any(r['name']=='call' and r['label'] in ('update','completion','eof_update','eof_completion') and integer(r,'begin')>integer(eos[0],'end') for r in owned), 'sample submitted after terminal EOS')
        empty=[r for r in owned if r['name']=='drain' and r['label']=='eof_wait' and r['c']=='1' and integer(r,'end')<=integer(paired[0],'begin')]
        require(empty and integer(empty[-1],'begin')>=integer(eos[0],'end'), 'EOF pair lacks fresh empty drain')
        invalid=[r for r in main if r['name']=='invalidate' and r['a']==session]
        require(len(invalid)==1 and integer(paired[0],'end')<=integer(invalid[0],'begin'), 'replacement intent before actual EOF pair')
        require(not any(r['name']=='slot' and r['label'] in ('writing','ready') and integer(r,'begin')>integer(eos[0],'end') for r in owned), 'CPU publication after terminal EOF')


def validate_provider_session(rows, graph_directory):
    from prepare_lav_fixture import BINARIES
    modules = rows.get('MP_LAV_ASSEMBLY_MODULE', [])
    require(len(modules) == len(BINARIES) and {r['name'] for r in modules} == set(BINARIES), 'loaded module closure absent')
    for row in modules:
        expected = 'Z:'+str(Path(graph_directory).resolve()/row['name']).replace('/', '\\')
        require(row.get('exact') == '1' and unquote(row['path']).lower() == expected.lower() and
                unquote(row['expected']).lower() == expected.lower(), 'loaded provider path differs')
    require(one(rows, 'MP_LAV_RGB').get('valid') == '1' and
            one(rows, 'MP_LAV_DITHER').get('actual') == '0', 'RGB/dither setup differs')
    require(one(rows, 'MP_LAV_CONTEXT').get('manifest_matches') == '1', 'activation context mismatch')
    classes = rows.get('MP_LAV_CLASS', [])
    require(len(classes) == 2 and {r['role'] for r in classes} == {'source', 'decoder'} and
            all(r['matches'] == '1' for r in classes), 'provider classes differ')


def validate_provider(rows, graph_directory):
    for session in ('1', '2'):
        selected = {kind: [r for r in values if r.get('session') == session] for kind, values in rows.items() if kind.startswith('MP_LAV_')}
        validate_provider_session(selected, graph_directory)


def finish(record, output, rows, expected):
    eof=record.get('mode')=='worker-eof-fresh'
    captures=12 if eof else CAPTURES
    predicate='worker_eof_fresh_accepted' if eof else 'worker_dd_reuse_accepted'
    result = dict(kind=EOF_KIND if eof else KIND, worker_dd_reuse_accepted=False, native_runtime_verified=False,
                  production_integration_accepted=False, reset_behavior_tested=False,
                  clock_policy_implemented=False, gate_L_inherited=False, recovery_inherited=False,
                  gate_P_inherited=False)
    result[predicate]=False
    if eof: result.update(retained_eof_replay_accepted=False, simultaneous_playbacks_qualified=False, selected_sink_callback_observed=False)
    result['recorded_cleanup_outcomes'] = {k: one(rows, 'MW_RESULT').get(k) for k in CLEANUP_FIELDS} if len(rows.get('MW_RESULT', []))==1 else {}
    # Recorded graph/service flags are not promoted to validated negative outcomes.
    # CPU lease retirement can be independently validated after operation failure.
    try:
        result['cpu_retirement_observation'] = cleanup_observation(rows)
    except (ValueError, KeyError, TypeError) as error:
        result['cleanup_observation_error'] = str(error)
    try:
        result['event_drains'] = validate_event_drains(rows, require_clean=False)
    except (ValueError, KeyError, TypeError) as error:
        result['event_observation_error'] = str(error)
    # Preserve whatever timing was buffered, even if subsequent coverage fails.
    try:
        frequency = int(one(rows, 'MW_HEADER')['qpc_frequency'])
        result['heartbeat'] = overlap_summary(rows.get('MW_EVENT', []), frequency)
    except (KeyError, ValueError, TypeError, ZeroDivisionError):
        pass
    try:
        require(record.get('exit_code') == 0 and record.get('unchanged') and all(record['unchanged'].values()) and
                not any(record.get(k) for k in ('outer_timeout', 'diagnostic_log_limit', 'diagnostic_log_monitor_error')),
                'process failed or bound inputs changed')
        header=one(rows,'MW_HEADER')
        require(header.get('mode')==record.get('mode','worker-dd-reuse') and header.get('kind')==result['kind'] and record.get('kind',result['kind'])==result['kind'], 'runner/header mode or kind mismatch')
        result.update(validate_rows(rows))
        validate_provider(rows, Path(record['lav_graph_provider']['path']).parent)
        require(len(expected) == captures, 'reference count differs')
        capture_rows = rows['MW_CAPTURE']
        require(len(capture_rows)==len(expected)==captures, 'mode capture/reference count differs')
        require(len(list(Path(output).glob('*.bgra'))) == captures, 'unexpected capture files')
        matched = []
        for capture, reference in zip(capture_rows, expected):
            path = Path(output)/capture['file']
            require(path.is_file() and not path.is_symlink() and path.stat().st_size == FRAME_BYTES, 'capture missing/oversized')
            raw = path.read_bytes()
            require(all(integer(capture, k) == reference[k] for k in ('session', 'generation', 'start', 'end')), 'exact sample time mapping differs')
            require(raw[3::4] == bytes([255])*(FRAME_BYTES//4) and rgb_digest(raw) == reference['rgb_sha256'], 'exact RGB/alpha mismatch')
            matched.append(hashlib.sha256(raw).hexdigest())
        result['capture_sha256'] = matched
        result['exact_reference_frames'] = captures
        observations = result['heartbeat']['operations']
        result['concurrent_heartbeat_observation_complete'] = all(r['completed_heartbeats'] for r in observations)
        result[predicate] = True
        result['outcome'] = ('eof_fresh_graph_accepted' if eof else 'functional_dd_reuse_accepted') + ('' if result['concurrent_heartbeat_observation_complete'] else '_timing_overlap_incomplete')
    except (ValueError, KeyError, TypeError, OSError, StopIteration) as error:
        result['validation_error'] = str(error)
        result['outcome'] = 'worker_ownership_failed_or_incomplete'
    return result
