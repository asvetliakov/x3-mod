"""Connected fixture facts: real readbacks, identity/ordering and frozen RGB oracles.

No clock, decoder or worker simulation lives here. This module never executes Wine.
"""
import hashlib
import json
import struct
from pathlib import Path
from fractions import Fraction

KIND = 'media_connected_v1'
FRAME_BYTES = 512 * 512 * 4

def require(value, message):
    if not value:
        raise ValueError(message)

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def binding(path):
    path = Path(path).resolve()
    return dict(path=str(path), sha256=digest(path))

def rgb_digest(raw):
    require(len(raw) == FRAME_BYTES, 'readback frame size differs')
    # Fixed BGRA transport/native surface, compare RGB without alpha semantics.
    rgb = bytearray(512*512*3)
    rgb[0::3], rgb[1::3], rgb[2::3] = raw[2::4], raw[1::4], raw[0::4]
    return hashlib.sha256(rgb).hexdigest()

def audit_image(path):
    data = Path(path).read_bytes()
    require(data[:2] == b'MZ', 'not PE')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    require(data[pe:pe+4] == b'PE\0\0', 'bad PE signature')
    machine, count = struct.unpack_from('<HH', data, pe+4)
    optional_size = struct.unpack_from('<H', data, pe+20)[0]
    opt = pe+24
    base = struct.unpack_from('<I', data, opt+28)[0]
    require(machine == 0x14c and base == 0x400000 and not struct.unpack_from('<H', data, opt+70)[0]&0x40,
            'fixture x86/base/ASLR mismatch')
    sections = []
    for index in range(count):
        at = opt+optional_size+index*40
        name = data[at:at+8].rstrip(b'\0').decode('ascii')
        size, rva, raw_size, raw = struct.unpack_from('<IIII', data, at+8)
        flags = struct.unpack_from('<I', data, at+36)[0]
        sections.append((name, base+rva, size, raw_size, raw, flags))
    maps = [s for s in sections if s[0] == '.x3map']
    require(len(maps) == 1, 'fixture map absent/duplicated')
    _, address, size, raw_size, raw, flags = maps[0]
    require(address == 0x401000 and size == 0x21f000 and flags&0x80000000 and not flags&0x20000000,
            'fixture map layout differs')
    require(len(data[raw:raw+raw_size]) == raw_size and not any(data[raw:raw+raw_size]), 'fixture map not zero')
    ordered = sorted((s for s in sections if s[2]), key=lambda s:s[1])
    require(all(a[1]+max(a[2],a[3]) <= b[1] for a,b in zip(ordered,ordered[1:])), 'PE sections overlap')
    require(any(s[0] == '.text' and s[1] == 0x630000 for s in sections), 'fixture text base differs')
    return dict(image_base='0x400000', owned_map=['0x401000','0x620000'], real_text='0x630000',
                sections_nonoverlapping=True, synthetic_bytes_zero=True)

def audit_counter_contract(root):
    """Bind the third read to the compiled source path; cardinality is also runtime checked.

    This deliberately refuses a changed call shape for review, rather than quietly
    reassigning a Counter read ordinal. Header/TU hashes bind the actual build.
    """
    root = Path(root)
    consumer = (root/'src/proxy/media_engine_adapter.cpp').read_text()
    services = (root/'src/proxy/media_services.cpp').read_text()
    body = consumer.split('if(id==SiteId::pump){',1)[1].split('    if(!e->live){if(id==SiteId::loop_seek)',1)[0]
    require(body.count('service_.publish(state_,session);') == 2 and body.count('service_.pump(') == 1,
            'Consumer pump Counter call shape changed')
    pump = services.split('media_engine::PumpResult MediaServices::pump(',1)[1].split('Diagnostics MediaServices::diagnostics',1)[0]
    publish = services.split('void MediaServices::publish(',1)[1].split('void MediaServices::poll_events',1)[0]
    require(pump.count('publish(adapter_,s->session);') == 1 and pump.count('schedule(*s,now())') == 1 and
            pump.count('now()') == 1 and publish.count('now()') == 1 and
            '!synchronize(*s,snapshot,now())' in publish, 'Services schedule Counter call shape changed')
    require(pump.index('publish(adapter_,s->session);') < pump.index('schedule(*s,now())'),
            'Services Counter ordering changed')
    return dict(kind='consumer_publish_services_publish_schedule_terminal_publish_v1',
                schedule_read_index=2,pending_reads=3,terminal_reads=4,
                position='public committed truncated milliseconds after Consumer return; Clock end freezes terminal position')

def load_oracles(record):
    """Reuse frozen reference readers; connected build has its OWN identity/schema."""
    import media_lav_evidence as original
    from run_media_playback_fixture import fnv64
    original_path, first = original.graph_result_binding(record, 'original_reference')
    strict_path, second = original.graph_result_binding(record, 'strict_reference')
    require(first.get('lav_transport') == second.get('lav_transport') == 'reference-matrix', 'reference modes differ')
    require(first['lav_qualification'].get('graph_original_reference_ready') and
            second['lav_qualification'].get('graph_derived_reference_ready'), 'reference readiness absent')
    require(first['lav_provider'] == second['lav_provider'] == record['lav_provider'] and
            second['lav_graph_provider'] == record['lav_graph_provider'], 'reference provider cohort differs')
    require(second['media']['sha256'] == record['media']['sha256'] and
            first['media']['sha256'] == record['derived_media']['record']['original']['sha256'], 'reference source differs')
    before, bp = original.graph_capture_content(original_path, first, fnv64, 'graph_capture_sha256')
    after, ap = original.graph_capture_content(strict_path, second, fnv64, 'graph_capture_sha256')
    require(original.graph_oracle_layout(before) and original.graph_oracle_layout(after) and bp == ap,
            'strict/original reference RGB/time layout differs')
    windows = {'A1': {}, 'B1': {}, 'A2': {}}
    for row, pixels in zip(after, ap):
        start, end = int(row['start']), int(row['end'])
        group = 'A1' if 0 <= start < 2800000 else 'B1' if 100000000 <= start < 103600000 else None
        if group: windows[group][rgb_digest(pixels)] = (start, end)
    require(len(windows['A1']) == 6 and len(windows['B1']) == 9, 'initial oracle window counts differ')
    tail_path, tail = original.graph_result_binding(record, 'eof_reference')
    import media_worker_sample_evidence as worker
    require(record['eof_reference']['sha256'] == worker.EOF_REFERENCE_SHA and
            tail['exe']['sha256'] == worker.EOF_REFERENCE_EXE and
            tail['lav_qualification'].get('eof_reference_accepted') and
            tail['lav_eof_preflight'].get('event_setup_protocol') == 2, 'physical EOF reference identity differs')
    mapping_binding = tail['lav_tail_reference_result']
    require(digest(mapping_binding['path']) == mapping_binding['sha256'] ==
            tail['lav_eof_preflight']['mapping_sha256'], 'suffix mapping changed')
    frames = tail['lav_qualification']['eof_reference_captures']
    require(len(frames) == 16, 'suffix reference frame count differs')
    for index, frame in enumerate(frames[10:16]):
        path = tail_path.parent/f'transport-e0-f{frame["index"]}.bgra'
        raw = path.read_bytes()
        require(len(raw) == FRAME_BYTES and hashlib.sha256(raw).hexdigest() == frame['sha256'] and
                fnv64(raw) == frame['fnv64'], 'suffix RGB changed')
        start, end = frame['mapped_absolute_start_100ns'], frame['mapped_absolute_end_100ns']
        require((start,end) == (19393200000+index*400000,19393200000+(index+1)*400000), 'suffix interval differs')
        windows['A2'][rgb_digest(raw)] = (start,end)
    return windows

def parse(text):
    rows = {}
    for line in text.splitlines():
        if not line.startswith('CXR_'): continue
        parts = line.split()
        kind = parts[0]
        require(kind not in ('CXR_FAIL','CXR_TIMEOUT'), 'fixture reports failure/timeout: '+line)
        row = {}
        for field in parts[1:]:
            key, sep, value = field.partition('=')
            require(sep and key not in row, 'malformed/duplicate fixture field')
            row[key] = value
        rows.setdefault(kind, []).append(row)
    return rows

def integer(row, key):
    value = int(row[key])
    require(value >= 0, 'negative unsigned field '+key)
    return value

def validate(rows, directory, oracles):
    require(len(rows.get('CXR_HEADER', [])) == len(rows.get('CXR_RESULT', [])) == 1, 'missing/duplicate header/result')
    head, result = rows['CXR_HEADER'][0], rows['CXR_RESULT'][0]
    require(head['kind'] == KIND and head['copy'] == 'native' and head['clock'] == 'production' and
            head['engine'] == 'authored_handler_frames' and head['workers'] == '2' and head['source'] == '2',
            'wrong connected path')
    require(integer(head,'frequency') > 0 and integer(result,'checks') > 0 and result['failures'] == '0', 'runtime failed')
    require(all(result[key] == '0' for key in ('assigned','draining','leases')) and
            all(result[key] == '1' for key in ('B_after_cancel','A2_callbacks','B_callbacks')), 'final state differs')
    surfaces = rows.get('CXR_SURFACE', [])
    require(len(surfaces) == 2 and {x['index'] for x in surfaces} == {'0','1'} and
            len({x['key'] for x in surfaces}) == 2 and len({x['device'] for x in surfaces}) == 1,
            'native destinations are not distinct on one device')
    require(all(x['width'] == x['height'] == '512' and x['format'] == '21' and x['pool'] == '2' for x in surfaces),
            'actual destination descriptor differs')
    records = rows.get('CXR_RECORD', [])
    require([(x['name'],x['lifetime']) for x in records] == [('1','1'),('2','1'),('1','2')], 'record lifetime order differs')
    a,b,a2 = records
    require(a['record'] == a2['record'] != b['record'] and a['shell'] == a2['shell'] != b['shell'], 'authored reuse/independence differs')
    require(a['key_generation'] != a2['key_generation'] and a['session_generation'] != a2['session_generation'], 'reuse retained old identity')
    require((a['session_slot'],a['session_generation']) != (b['session_slot'],b['session_generation']), 'simultaneous sessions aliased')
    plays = rows.get('CXR_PLAY', [])
    require([(x['name'],x['lifetime'],x['start'],x['end']) for x in plays] ==
            [('1','1','0','-1'),('2','1','10000','10359'),('1','2','1939320','-1')], 'production play scenario differs')
    operations = {(x['name'],x['lifetime']):(x['operation'],x['epoch']) for x in plays}
    events = rows.get('CXR_EVENT', [])
    overlap = [x for x in events if x['name'] == 'overlap_cancel']
    reused = [x for x in events if x['name'] == 'reused']
    rates = [x for x in events if x['name'] == 'rate']
    refusals = [x for x in events if x['name'] == 'draining_refusal']
    require(len(overlap) == len(reused) == len(rates) == 1 and rates[0]['record'] == '2' and rates[0]['a'] == '10000',
            'overlap/reuse/actual rate witness missing')
    vacant = [x for x in events if x['name'] == 'vacant']
    require(len(vacant) == 1 and vacant[0]['a'] == '1' and vacant[0]['b'] == '0', 'vacant assignment observation missing')
    require(len(refusals) == 1 and integer(overlap[0],'qpc') <= integer(refusals[0],'qpc') < integer(vacant[0],'qpc') <= integer(reused[0],'qpc'),
            'reuse was not refused while exact assignment was draining')
    callbacks = rows.get('CXR_CALLBACK', [])
    require(sorted((x['name'],x['lifetime'],x.get('reason')) for x in callbacks) ==
            [('1','1','retired'),('1','2','endpoint'),('2','1','endpoint')] and
            all(x['status'] == x['count'] == '1' for x in callbacks), 'natural callback duplicated/missing/wrong status')
    for row in callbacks:
        require((row['operation'],row['epoch']) == operations[(row['name'],row['lifetime'])], 'callback identity changed')
    captures = rows.get('CXR_CAPTURE', [])
    require(0 < len(captures) <= 32 and len(captures) == integer(result,'captures') and
            [integer(x,'index') for x in captures] == list(range(len(captures))), 'capture bound/order differs')
    selected = {'A1': [], 'B1': [], 'A2': []}
    retained = {}
    content = []
    for row in captures:
        group = ('A' if row['name'] == '1' else 'B')+row['lifetime']
        require(group in selected, 'unexpected capture identity')
        require((row['operation'],row['epoch']) == operations[(row['name'],row['lifetime'])], 'stale capture identity')
        path = Path(directory)/f'capture-{integer(row,"index"):02d}.bgra'
        require(path.is_file() and not path.is_symlink() and path.stat().st_size == FRAME_BYTES, 'capture missing/size/reparse')
        rgb = rgb_digest(path.read_bytes())
        require(rgb in oracles[group], 'native RGB not in immutable '+group+' source window')
        start,end = oracles[group][rgb]
        require(integer(row,'pitch') >= 2048 and row['lock_hr'] == row['unlock_hr'] == '0', 'actual readback pitch/HRESULT missing')
        sequence = integer(row,'sequence')
        require(integer(row,'binding') > 0, 'missing physical written binding acknowledgement')
        if row['reason'] == 'selected':
            if selected[group]:
                require(sequence > selected[group][-1]['sequence'] and start >= selected[group][-1]['start'],
                        'presentation sequence/source time reversed')
            selected[group].append(dict(sequence=sequence,start=start,end=end,qpc=integer(row,'qpc'),rgb=rgb,schedule=integer(row,'schedule'),position=integer(row,'position'),binding=integer(row,'binding')))
        elif row['reason'] in ('terminal','hold'):
            require(selected[group] and rgb == selected[group][-1]['rgb'] and sequence == selected[group][-1]['sequence'],
                    'terminal/hold differs from final actual write')
            require((group,row['reason']) not in retained, 'duplicate terminal/hold readback')
            retained[group,row['reason']] = integer(row,'qpc')
        else: raise ValueError('unknown readback reason')
        content.append(dict(index=integer(row,'index'),group=group,sequence=sequence,start=start,end=end,rgb_sha256=rgb))
    require(selected['A1'] and selected['B1'] and selected['A1'][0]['sequence'] == selected['B1'][0]['sequence'] == 0,
            'fresh workers did not acknowledge sequence-zero first writes')
    require(len(selected['A1']) == 1 and len(selected['B1']) >= 2 and len(selected['A2']) >= 2, 'real presentations missing')
    require(selected['A2'][0]['start'] == 19393200000 and selected['A2'][-1]['end'] == 19395600000,
            'physical suffix first/last actual picture missing')
    require(any(x['sequence'] > integer(overlap[0],'a') and x['qpc'] > integer(overlap[0],'qpc') for x in selected['B1']),
            'B has no actual post-cancellation presentation')
    require(integer(callbacks[0],'qpc') > 0, 'callback time absent')
    for group,key in [('A2',('1','2')),('B1',('2','1'))]:
        callback = next(x for x in callbacks if (x['name'],x['lifetime']) == key)
        require(integer(callback,'qpc') >= selected[group][-1]['qpc'], 'callback precedes final selected picture')
        require((group,'terminal') in retained and (group,'hold') in retained and
                retained[group,'hold']-retained[group,'terminal'] >= integer(head,'frequency')//4,
                'post-terminal hold interval absent')
    temporal = validate_time(rows, selected, plays, callbacks, head)
    return dict(kind=KIND,temporal=temporal,checks=integer(result,'checks'),captures=len(captures),callbacks=len(callbacks),natural_callbacks=2,cancellation_callbacks=1,
                B_after_cancel=True,reused_record=True,actual_native_RGB=True,content=content,
                max_owner_pass_seconds=integer(result,'max_pass_qpc')/integer(head,'frequency'),
                limitations=['Engine handler frames/continuations and allocator are authored.',
                             'Native worker DD identity and general unwind are not newly observed.',
                             'SYSTEMMEM D3D writes are not GPU billboard rendering or game acceptance.'])


def legacy_position_preimage(position):
    """Conservative outward preimage of the two binary64 nearest-even rounds.

    For positive normal values each round has relative error <= u=2^-53.
    RN(RN(exact_ms/1000)*1000) in [p,p+1) therefore implies exact_ms
    in [p/(1+u)^2,(p+1)/(1-u)^2). Zero is included exactly. Clock's
    source/rate/QPC integer domain cannot approach binary64 subnormal values.
    This is an outward bound, not an exact inversion of legacy_ms.
    """
    require(0 <= position < 2147483648, 'public position out of range')
    u = Fraction(1,1 << 53)
    return Fraction(position)/(1+u)**2, Fraction(position+1)/(1-u)**2


class AnchorInterval:
    def __init__(self, played):
        self.low, self.high = Fraction(played), None
        self.low_closed, self.high_closed = True, False

    def intersect(self, low, high, low_closed, high_closed):
        if low > self.low:
            self.low, self.low_closed = low, low_closed
        elif low == self.low:
            self.low_closed &= low_closed
        if self.high is None or high < self.high:
            self.high, self.high_closed = high, high_closed
        elif high == self.high:
            self.high_closed &= high_closed
        require(self.low < self.high or
                (self.low == self.high and self.low_closed and self.high_closed),
                'inconsistent real QPC/rate/position constraints')

    def position(self, schedule, start, scale, lower, upper):
        # exact source position in [lower,upper) reverses the anchor endpoints.
        self.intersect(Fraction(schedule)-(upper-start)/scale,
                       Fraction(schedule)-(lower-start)/scale, False, True)


def validate_time(rows, selected, plays, callbacks, header):
    """Intersect actual QPC, outward public-position and selected-interval bounds.

    Cold preparation is excluded through a per-operation anchor after play. The
    same rational interval includes every selected source interval and endpoint;
    no second runtime clock or assumed exact floor conversion is used.
    """
    frequency = integer(header,'frequency'); unit = 1 << 38
    continuity = rows.get('CXR_CONTINUITY', [])
    require([x['phase'] for x in continuity] == ['before_cancel','after_cancel','after_refusal'],
            'B cancellation continuity snapshots missing')
    fields = ('operation','epoch','position','rate','generation','revision')
    require(all(tuple(x[k] for k in fields) == tuple(continuity[0][k] for k in fields) for x in continuity),
            'A cancellation changed B clock/operation/revision')
    require(continuity[0]['assigned'] == continuity[1]['assigned'] == continuity[2]['assigned'] == '2' and
            [x['draining'] for x in continuity] == ['0','1','1'], 'cancellation drain transition missing')
    startup = rows.get('CXR_STARTUP', [])
    require(len(startup) == 1 and 0 < integer(startup[0],'requested') <= integer(startup[0],'begin') <=
            integer(startup[0],'end') <= integer(startup[0],'ready'), 'actual bootstrap/ready timestamps missing')
    clocks = rows.get('CXR_CLOCK', [])
    require(clocks and len(clocks) <= 32, 'bounded clock observations missing')
    play_keys = {(x['name'],x['lifetime']) for x in plays}
    identities = [(x['name'],x['lifetime'],integer(x,'schedule')) for x in clocks]
    require(all(x[:2] in play_keys for x in identities) and len(set(identities)) == len(identities),
            'unknown or duplicate clock identity')
    proof = {}
    for play in plays:
        key = (play['name'],play['lifetime']); group = ('A' if key[0] == '1' else 'B')+key[1]
        own = [x for x in clocks if (x['name'],x['lifetime']) == key]
        require(own, 'no actual clock observation for '+group)
        observed = [x for x in own if x['reason'] == 'selected']
        require(len(observed) == len(selected[group]) and
                {integer(x,'schedule') for x in observed} == {x['schedule'] for x in selected[group]},
                'selected capture/clock observations are not bijective')
        first = observed[0]
        if group == 'B1':
            require(tuple(continuity[0][k] for k in fields) == tuple(first[k] for k in fields) and
                    (first['operation'],first['epoch']) == (play['operation'],play['epoch']),
                    'B cancellation continuity not bound to play/first selected clock')
            require(integer(first,'after') <= integer(continuity[0],'qpc') <=
                    integer(continuity[1],'qpc') <= integer(continuity[2],'qpc'),
                    'B cancellation continuity precedes first selected pump')
        start = int(play['start']); played = integer(play,'qpc')
        require(played >= integer(startup[0],'ready'), 'play precedes actual ready')
        if group in ('B1','A2'):
            require(selected[group][0]['start'] == start*10000 and selected[group][0]['position'] == start,
                    'target-aligned first picture did not anchor at requested start')
        rate = 10000*2748779 if group == 'B1' else unit
        scale = Fraction(1000*rate,frequency*unit)
        anchor = AnchorInterval(played)
        record = next(x for x in rows['CXR_RECORD'] if (x['name'],x['lifetime']) == key)
        terminal = []; previous = played
        for row in own:
            require((row['operation'],row['epoch']) == (play['operation'],play['epoch']) and integer(row,'rate') == rate,
                    'clock tuple/rate changed')
            before, after, schedule = integer(row,'before'), integer(row,'after'), integer(row,'schedule')
            reads = integer(row,'reads'); is_terminal = row['reason'] == 'terminal'
            require(integer(row,'post_epoch') == integer(play,'epoch')+int(is_terminal) and
                    row['active'] == row['playing'] == ('0' if is_terminal else '1') and row['live'] == '1' and
                    row['intent'] in (('0',) if is_terminal else ('1','2')) and
                    (row['session_slot'],row['session_generation']) == (record['session_slot'],record['session_generation']),
                    'pump did not preserve identity/exact terminal state transition')
            require(not terminal and before >= previous, 'clock observations reordered or after terminal')
            previous = after
            require(all(integer(row,k) == integer(first,k)+int(is_terminal) for k in ('generation','revision')),
                    'clock generation/revision changed outside terminal stop')
            require(reads == (4 if is_terminal else 3) and before <= integer(row,'c0') <= integer(row,'c1') <=
                    integer(row,'c2') == schedule <= after and
                    (schedule <= integer(row,'c3') <= after if is_terminal else integer(row,'c3') == 0),
                    'actual schedule Counter cardinality/order differs')
            require(played <= before and schedule >= played, 'pump/clock precedes play')
            position = integer(row,'position')
            lower, upper = legacy_position_preimage(position)
            lower = max(lower,Fraction(start))
            if row['reason'] == 'selected':
                sample = next(x for x in selected[group] if x['schedule'] == schedule)
                require(sample['position'] == position and sample['qpc'] >= after and
                        sample['sequence'] == integer(row,'sequence') and sample['binding'] == integer(row,'binding') > 0,
                        'selected readback position/time differs from completed pump')
                lower = max(lower,Fraction(sample['start'],10000))
                upper = min(upper,Fraction(sample['end'],10000))
                if group == 'B1': require(position <= 10359, 'selected finite frame beyond strict end')
            elif is_terminal:
                terminal.append(row)
                if group == 'B1':
                    require(position >= 10360, 'early finite end')
                    lower = max(lower,legacy_position_preimage(10360)[0])
                elif group == 'A2':
                    lower = max(lower,Fraction(1939560))  # EOF compares exact source ticks.
                else: require(False, 'unexpected A1 natural endpoint')
            else: require(row['reason'] == 'advance', 'unknown clock observation reason')
            require(lower < upper, 'selected RGB or endpoint interval incompatible with public position')
            anchor.position(schedule,start,scale,lower,upper)
        if group in ('B1','A2'):
            require(len(terminal) == 1, 'natural endpoint scheduling observation missing/duplicated')
            endpoint = legacy_position_preimage(10360)[0] if group == 'B1' else Fraction(1939560)
            row = terminal[0]
            callback = next(x for x in callbacks if (x['name'],x['lifetime']) == key)
            require(integer(callback,'qpc') >= integer(row,'after') >= integer(row,'schedule') and
                    integer(callback,'qpc') >= played and
                    Fraction(integer(callback,'qpc')) >= anchor.low+(endpoint-start)/scale,
                    'callback precedes play or real endpoint deadline')
        proof[group] = dict(anchor_lower=str(anchor.low),anchor_upper=str(anchor.high),
                            lower_closed=anchor.low_closed,upper_closed=anchor.high_closed,
                            position_bound='outward_two_binary64_rounds',observations=len(own),rate_numerator=rate)
    return proof
