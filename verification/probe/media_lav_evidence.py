"""Fail-closed evidence for the explicit, fixture-only portable LAV trial."""
import hashlib
import json
from pathlib import Path
from urllib.parse import unquote
from prepare_lav_fixture import BINARIES, SOURCE_CLSID, VIDEO_CLSID, verify_record

CONSTRUCT = ['lav_create_context', 'lav_activate_context', 'lav_get_current_context',
             'lav_query_context', 'lav_release_current_context',
             'lav_create_source', 'lav_create_decoder', 'lav_created_class', 'lav_module_path',
             'lav_qi_source_settings', 'lav_source_runtime', 'lav_qi_video_settings',
             'lav_video_runtime', 'lav_software', 'lav_threads', 'lav_pixel_format',
             'lav_get_sink', 'lav_sink_clsid', 'lav_add_source', 'lav_add_decoder',
             'lav_qi_file', 'lav_load', 'lav_connect_compressed', 'lav_connect_rgb',
             'lav_rgb_type', 'lav_qi_seeking', 'lav_qi_events', 'lav_seek_caps', 'lav_time_format']
RELEASE = ['lav_release_events', 'lav_release_seeking', 'lav_release_sink_in', 'lav_release_decoder_out',
           'lav_release_decoder_in', 'lav_release_source_out', 'lav_release_file',
           'lav_release_sink', 'lav_release_video_settings', 'lav_release_source_settings']

TERMINAL_ACQUIRE = ['lav_qi_terminal_input', 'lav_get_terminal_allocator', 'lav_terminal_properties']
TERMINAL_RELEASE = ['lav_release_terminal_allocator', 'lav_release_terminal_input']
TERMINAL_ARGS = dict(zip(TERMINAL_ACQUIRE, ['connected_sink_IMemInputPin',
    'connected_selected_allocator', 'selected_allocator_GetProperties']))
TERMINAL_ARGS.update(lav_terminal_decommit='selected_allocator_terminal_no_recommit',
                     **dict.fromkeys(TERMINAL_RELEASE, 'Release_returns_refcount'))


def terminal_mode(item):
    return item.get('header', {}).get('lav_terminal_decommit', '0') == '1'

TRANSPORT_MODES = ('reference', 'reference-extended', 'seek0', 'epochs', 'boundary', 'pending', 'reference-matrix', 'integer-matrix', 'pending-integer', 'seek-only-interval','seek-only-recovery')
REFERENCE_MODES = ('reference', 'reference-extended', 'reference-matrix')
GRAPH_MODES = ('reference-matrix', 'integer-matrix', 'pending-integer', 'seek-only-interval','seek-only-recovery')
GRAPH_REFERENCE_EXE = '62648d975b71e1ca9b881b5977f48e68b8e2802312ee7c32cf0204ce0ffafc65'
GRAPH_PENDING_EXE = '0d64660be71fae58d5ecdbba767f18685bc8f1452f1ee046389ea3406afb836c'
GRAPH_PENDING_SOURCE = '320d4123377e3e4a09a4d92cb87dc2f03c157821bd3771c3886a9498123ebfdc'
GRAPH_PENDING_HELPER = 'd092f2a31da06c8b73f685ae7be24f1c37ff78baeaa7cc3f1ceb057c944987de'
GRAPH_PENDING_COMPATIBILITY = dict(observation='composite_deferred_two_pending_unchanged',candidate_exe_sha256=GRAPH_PENDING_EXE,candidate_source_sha256=GRAPH_PENDING_SOURCE,candidate_helper_sha256=GRAPH_PENDING_HELPER,scope='pending_integer_scenario_only', reference_checkpoint='6a6d48b1', reference_exe_sha256=GRAPH_REFERENCE_EXE, unchanged_transport_settings=True, arbitrary_crossbuild_acceptance=False)
GRAPH_LOOP_EXE = 'f88856cb126c9c92f445720bdef39d2d4d750d757c51fde686e55dca00673fb2'
GRAPH_LOOP_SOURCE = 'd87343cf398b384a75becb4c255a54f2b1cdcb276e3e83b206a50c0c4dffe486'
GRAPH_LOOP_COMPATIBILITY = dict(scope='seek_only_positive_interval_scenario_only',candidate_exe_sha256=GRAPH_LOOP_EXE,candidate_source_sha256=GRAPH_LOOP_SOURCE,candidate_helper_sha256=GRAPH_PENDING_HELPER,reference_checkpoint='6a6d48b1',reference_exe_sha256=GRAPH_REFERENCE_EXE,pending_exe_sha256=GRAPH_PENDING_EXE,unchanged_transport_settings=True,arbitrary_crossbuild_acceptance=False)
GRAPH_RECOVERY_EXE = '3a3433a9850ca50859243c005e95cdfad636a18874f04a76e1853393d208c39a'
GRAPH_RECOVERY_SOURCE = '6521a0947e6fd0f08ea8bb83489819c1245501d06a8c75c7a51a33d087009be2'
GRAPH_RECOVERY_COMPATIBILITY = dict(scope='cold_miss_recovery_scenario_only',candidate_exe_sha256=GRAPH_RECOVERY_EXE,candidate_source_sha256=GRAPH_RECOVERY_SOURCE,candidate_helper_sha256=GRAPH_PENDING_HELPER,reference_checkpoint='804c86ee',reference_exe_sha256=GRAPH_REFERENCE_EXE,pending_exe_sha256=GRAPH_PENDING_EXE,cold_interval_exe_sha256=GRAPH_LOOP_EXE,unchanged_transport_settings=True,arbitrary_crossbuild_acceptance=False)
COLD_INTERVAL_RESULT = 'c619f2feaffc81c0dd89fe341fe10634e1b1ee8f9fec43577ff022205c75cc06'
COLD_INTERVAL_RAW = {'stdout.txt':'26e94e0b908e06b4adea408a5c864ae487d078b7174062ac9cb9a766aeb98470','stderr.txt':'762272b7e4ccb1ae45ae9d8c8cd9d3891733e69ef3bca4bcefea97186f016d71'}
GRAPH_LOOP_PENDING_RESULT = '25707980f6cc4e4b67f783fc08b4803b13bfa7c237795171259cb3575eb5e6a7'
GRAPH_MATRIX_TARGETS = [10000,10001,10080,10120,2199,2200,2201,2160,2161,5079,5080,5081,5040,5041,0]
GRAPH_ORACLE_INDICES = list(range(6))+list(range(53,61))+list(range(125,133))+list(range(249,258))
GRAPH_ORACLE_STARTS = [0,800000,1200000,1600000,2000000,2400000]+list(range(21600000,24400001,400000))+list(range(50400000,53200001,400000))+list(range(100000000,103200001,400000))
BOUNDARY_TARGETS = [10001, 10080, 10120, 10000]
BOUNDARY_CONTRACT = dict(rule='first_original_presentation_start_at_or_after_request',
                         scope='LAV_0_81_derived_provider_only',targets_ms=BOUNDARY_TARGETS,
                         timestamp_normalization=False)
DERIVED_REFERENCE_EXE = 'a381e0a0a06ab97f0f4a084ae9fee4b7119b11905dda2bd07094b06af3181d21'
BOUNDARY_EXE = '6cb5d617982a1d29087a283fc2b2ffc25a5bca1a4d9e1944c947d08e3f521b51'
TRANSPORT_SETTINGS = dict(schema=1, dither_mode='ordered', dither_enum=0,
                          scope='transport_runtime_before_connect')
DITHER_WITNESS = dict(requested='0', actual='0', mode='ordered',
                      scope='transport_runtime_before_connect')


def check_dither(item):
    calls = item.get('returned_calls', [])
    names = [c['name'] for c in calls]
    witnesses = item.get('lav', {}).get('MP_LAV_DITHER', [])
    setting_names = ('lav_dither_set', 'lav_dither_get')
    if not transport_mode(item):
        return not witnesses and not any(n in names for n in setting_names)
    if witnesses != [DITHER_WITNESS] or any(names.count(n) != 1 for n in setting_names):
        return False
    setter, getter = (calls[names.index(n)] for n in setting_names)
    if (setter.get('args') != 'LAVDither_Ordered_runtime_before_connect' or
            setter['hr'] not in ('00000000', '00000001') or
            getter.get('args') != 'GetDitherMode_enum_wrapped_S_OK' or getter['hr'] != '00000000' or
            any(c.get('phase') != 'constructor' or c.get('attempt', 1) != 1 for c in (setter, getter))):
        return False
    try:
        at = names.index('lav_dither_set')
        return (names.index('lav_video_runtime') < at and names[at+1] == 'lav_dither_get' and
                at+1 < names.index('lav_connect_compressed') < names.index('lav_connect_rgb'))
    except ValueError:
        return False

TRANSPORT_ACQUIRE = ['lav_qi_transport_input', 'lav_get_transport_allocator', 'lav_transport_properties']
TRANSPORT_RELEASE = ['lav_release_transport_allocator', 'lav_release_transport_input']
TRANSPORT_ARGS = dict(zip(TRANSPORT_ACQUIRE, (TERMINAL_ARGS[n] for n in TERMINAL_ACQUIRE)))
TRANSPORT_ARGS.update(dict.fromkeys(TRANSPORT_RELEASE, 'Release_returns_refcount'))
TRANSPORT_ARGS.update(lav_transport_decommit='selected_allocator_filter_restart_owns_commit',
    lav_transport_cleanup_decommit='selected_allocator_filter_restart_owns_commit',
    lav_transport_pause='none', lav_transport_stop='none', lav_transport_state='GetState_timeout0',
    lav_transport_abort='COMPSTAT_ABORT_timeout0', lav_transport_retired='flags0_timeout0',
    lav_transport_seek='target_ms_div1000', lav_transport_position='stopped_out_seconds',
    lav_integer_seek='IMediaSeeking_absolute_stop_NULL_NoPositioning',
    lav_integer_position='stopped_IMediaSeeking_GetCurrentPosition',
    lav_transport_create_sample='NULL_NULL_0_out', lav_transport_get_surface='out_surface_rect',
    lav_transport_surface_desc='DDSURFACEDESC108', lav_transport_run='none',
    lav_loop_observe='GetSampleTimes_then_graph_IMediaPosition_deferred',lav_loop_end_pause='positive_interval_strict_greater',
    lav_loop_negative_update='SSUPDATE_ASYNC_NULL_NULL_0_no_Run',lav_loop_negative_abort='COMPSTAT_ABORT_timeout0',lav_loop_negative_retired='flags0_timeout0',
    lav_loop_run_internal='inside_seek_only_adapter',
    lav_pending_pair='Update_ASYNC_NULL_NULL_0_then_CompletionStatus_0_0_deferred',
    lav_transport_pending_update='SSUPDATE_ASYNC_NULL_NULL_0', lav_transport_pending_status='flags0_timeout0',
    lav_transport_dump_create='private_epoch_frame_CREATE_NEW', lav_transport_dump_write='packed_BGRA8_1048576',
    lav_transport_dump_close='owned_file')
RETIRED = ('00000000', '00040002', '80004004', '00040003')  # S_OK, NOUPDATE, E_ABORT, END_OF_STREAM


def transport_mode(item):
    return item.get('header', {}).get('lav_transport', 'none') != 'none'


def transport_targets(mode):
    if mode == 'integer-matrix':return list(GRAPH_MATRIX_TARGETS)
    if mode == 'pending-integer':return [0,10000]
    if mode in ('seek-only-interval','seek-only-recovery'):return [10000]*3
    if mode == 'boundary':return list(BOUNDARY_TARGETS)
    return [0, 10000, 0, 10000] if mode == 'epochs' else [0, 10000] if mode == 'pending' else [0]


def consume_transport(item, kind, row):
    data = item.setdefault('lav', {})
    calls = item['returned_calls']
    epochs = data.setdefault('epochs', [])
    if kind == 'MP_LAV_EPOCH_BEGIN':
        if (item['pending'] or len(epochs) >= (15 if item['header'].get('lav_transport')=='integer-matrix' else 4) or (epochs and 'end' not in epochs[-1]) or
                int(row['epoch']) != len(epochs) or int(row['first_global']) != len(item['frames'])):
            raise ValueError('invalid transport epoch begin')
        epochs.append(dict(begin=row, call_start=len(calls), sample_start=len(item['samples']), frame_start=len(item['frames'])))
        item['waiting'] = item['ready'] = item['stamp_ready'] = False
        return True
    if kind == 'MP_LAV_EPOCH_END':
        if (item['pending'] or not epochs or 'end' in epochs[-1] or
                row.get('epoch') != epochs[-1]['begin']['epoch'] or int(row['total_frames']) != len(item['frames']) or
                not calls or calls[-1]['name'] != 'lav_event_poll' or calls[-1]['hr'] != '80004004'):
            raise ValueError('invalid transport epoch end')
        epochs[-1].update(end=row, call_end=len(calls), sample_end=len(item['samples']), frame_end=len(item['frames']))
        return True
    if kind=='MP_LAV_PENDING_CLEANUP':
        if item['header'].get('lav_transport') not in ('pending-integer','seek-only-interval','seek-only-recovery') or item['pending'] or data.get(kind):
            raise ValueError('invalid pending cleanup scope')
        chain=calls[-5:]
        names=['control_stop','stream_stop','lav_pending_cleanup_abort','lav_pending_cleanup_retired','lav_pending_cleanup_state']
        if len(chain)!=5 or [c['name'] for c in chain]!=names or any(c['phase']!='cleanup' for c in chain):
            raise ValueError('unpaired pending cleanup retirement')
        if any(row.get(k)!=c['hr'] for k,c in zip(('control_stop_hr','stream_stop_hr','abort_hr','settled_hr','state_hr'),chain)):
            raise ValueError('pending cleanup HRESULT witness differs')
        safe=(row.get('decommitted')=='1' and all(row.get(k)=='00000000' for k in ('control_stop_hr','stream_stop_hr','state_hr')) and
              row.get('state')=='0' and row.get('abort_hr') in RETIRED and row.get('settled_hr') in RETIRED)
        if row.get('safe_release')!=str(int(safe)):raise ValueError('pending cleanup release predicate differs')
        data[kind]=[row];return True
    if kind=='MP_LAV_PENDING_UNRESOLVED':
        cleanup=data.get('MP_LAV_PENDING_CLEANUP',[])
        if (item['header'].get('lav_transport') not in ('pending-integer','seek-only-interval','seek-only-recovery') or len(cleanup)!=1 or cleanup[0].get('safe_release')!='0' or
                row!={'releases_skipped':'1','own_child_termination':'1'} or data.get(kind)):
            raise ValueError('invalid pending unresolved termination')
        data[kind]=[row];return True
    expected = {'MP_LAV_EPOCH_READY':('lav_loop_run_internal' if loop_mode(item) and len(epochs)>1 else 'lav_transport_run'), 'MP_LAV_TRANSPORT_STATE':'lav_transport_state',
                'MP_LAV_CANCEL':'lav_transport_retired', 'MP_LAV_TRANSPORT_POSITION':'lav_transport_position',
                'MP_LAV_INTEGER_POSITION':'lav_integer_position', 'MP_LAV_PENDING_PAIR':'lav_pending_pair',
                'MP_LAV_TRANSPORT_DUMP':'lav_transport_dump_close', 'MP_LAV_PENDING':'lav_transport_pending_status'}
    if kind == 'MP_LAV_TRANSPORT_DECOMMIT':
        expected[kind] = 'lav_transport_cleanup_decommit' if row.get('cleanup') == '1' else 'lav_transport_decommit'
    if kind not in expected:
        return False
    if item['pending'] or not calls or calls[-1]['name'] != expected[kind]:
        raise ValueError('unpaired transport witness '+kind)
    paired = data.setdefault('_paired_calls', {}).setdefault(kind, [])
    if len(calls) in paired:
        raise ValueError('reused transport witness')
    paired.append(len(calls))
    entry = dict(row, call_index=len(calls)-1)
    data.setdefault(kind, []).append(entry)
    if kind == 'MP_LAV_CANCEL' and (len(calls)<2 or calls[-2]['name']!='lav_transport_abort' or row.get('abort_hr')!=calls[-2]['hr'] or row.get('settled_hr')!=calls[-1]['hr']):
        raise ValueError('cancel witness differs from paired calls')
    if kind=='MP_LAV_PENDING_PAIR':
        call=calls[-1]
        if (item['header'].get('lav_transport')!='pending-integer' or item['header'].get('lav_pending_probe')!='composite_deferred' or
                call['hr']!='00000000' or call.get('attempt',1)!=1 or row.get('pair_seq')!=str(call.get('seq')) or
                row.get('telemetry')!='deferred_composite' or
                any(row.get(k)!=v for k,v in dict(update_flags='SSUPDATE_ASYNC',update_event='NULL',update_apc='NULL',update_data='0',status_flags='0',status_timeout='0').items())):
            raise ValueError('invalid composite pending pair declaration')
        try:
            before,middle,after=(int(row[k]) for k in ('qpc_before_update','qpc_between_calls','qpc_after_status'))
            if (not 0<int(call['qpc_enter'])<=before<middle<after<=int(call['qpc_leave']) or
                    int(row['qpc_frequency'])<=0 or row['qpc_frequency']!=item['header'].get('qpc_frequency') or
                    any(not 0<=int(row[k])<=0xffffffff for k in ('update_last_error','status_last_error'))):
                raise ValueError('nonmonotonic or invalid composite pending QPC/LastError')
            if any(len(row[k])!=8 or any(c not in '0123456789abcdef' for c in row[k]) for k in ('update_hr','status_hr')):
                raise ValueError('invalid composite raw HRESULT')
            pending_pair=all(int(row[k],16)==0x40001 for k in ('update_hr','status_hr'))
        except (KeyError,TypeError) as error:raise ValueError('missing composite pending timing/result') from error
        if row.get('observed')!=str(int(pending_pair)) or row.get('outcome')!=('pending_observed' if pending_pair else 'pending_not_observed'):
            raise ValueError('composite pending gate differs from actual inner results')
    if kind=='MP_LAV_PENDING' and item['header'].get('lav_pending_probe')=='composite_deferred':
        raise ValueError('fabricated live inner pending record in composite probe')
    if kind == 'MP_LAV_PENDING' and (len(calls)<2 or calls[-2]['name']!='lav_transport_pending_update' or row.get('update_hr')!=calls[-2]['hr'] or row.get('status_hr')!=calls[-1]['hr']):
        raise ValueError('pending witness differs from paired calls')
    if kind == 'MP_LAV_EPOCH_READY':
        if not epochs or 'ready' in epochs[-1] or row.get('epoch') != epochs[-1]['begin']['epoch']:
            raise ValueError('invalid transport ready')
        epochs[-1]['ready'] = row
        epochs[-1]['ready_call'] = len(calls)-1
    if kind == 'MP_LAV_TRANSPORT_DUMP':
        frame = item['frames'][-1] if item['frames'] else {}
        chain = calls[-3:]
        if (not epochs or 'end' in epochs[-1] or len(chain) != 3 or
                [c['name'] for c in chain] != ['lav_transport_dump_create','lav_transport_dump_write','lav_transport_dump_close'] or
                any(c['hr'] != '00000000' for c in chain) or
                not all(row.get(k) == frame.get(k) for k in ('index','start','end','hash','bytes'))):
            raise ValueError('unpaired transport capture')
        local = int(frame['index'])-epochs[-1]['frame_start']
        expected_file = f"transport-e{epochs[-1]['begin']['epoch']}-f{local}.bgra"
        if row.get('file') != expected_file or any(d['file'] == row['file'] for d in data[kind][:-1]):
            raise ValueError('invalid transport capture filename')
    return True


def recovered_transport_attempt(calls, index):
    """Only the first negative return of the explicitly bounded retry pair."""
    first = calls[index]
    if first['name'] not in ('lav_transport_seek','lav_integer_seek','lav_transport_run','lav_loop_run_internal') or index+1 >= len(calls):return False
    second = calls[index+1]
    allowed = ('00000000','00000001') if first['name'] in ('lav_transport_run','lav_loop_run_internal') else ('00000000',)
    return (first.get('attempt',1)==1 and bool(int(first['hr'],16)&0x80000000) and
            second.get('attempt',1)==2 and second['hr'] in allowed and
            all(first[k]==second[k] for k in ('name','phase','args')))


def check_transport(item):
    data, calls = item.get('lav', {}), item['returned_calls']
    mode = item['header']['lav_transport']
    if mode not in TRANSPORT_MODES or item['stage'] != 'copy' or terminal_mode(item):
        return False
    epochs = data.get('epochs', [])
    targets = transport_targets(mode)
    if data.get('MP_LAV_PENDING_CLEANUP') or data.get('MP_LAV_PENDING_UNRESOLVED'):return False
    if len(epochs) != len(targets):
        return False
    drains=data.get('_paired_calls',{}).get('MP_LAV_EVENTS',[])
    if not drains or not 0 < drains[0] <= epochs[0]['call_start']:
        return False
    for index,c in enumerate(calls):
        if c['name'] in TRANSPORT_ARGS and c.get('args') != TRANSPORT_ARGS[c['name']]:
            return False
        if c['name'] in TRANSPORT_ARGS:
            name=c['name']
            expected_phase=('constructor' if name in TRANSPORT_ACQUIRE else 'cleanup' if name in TRANSPORT_RELEASE or name=='lav_transport_cleanup_decommit' else
                            'pump' if name.startswith('lav_transport_dump_') or name in ('lav_loop_observe','lav_loop_end_pause') else 'transport-pending' if name.startswith('lav_transport_pending_') or name=='lav_pending_pair' else 'transport')
            if c['phase']!=expected_phase and not (name=='lav_transport_state' and c['phase']=='pump'):return False
            allowed = (RETIRED if c['name'] in ('lav_transport_abort','lav_transport_retired','lav_loop_negative_abort','lav_loop_negative_retired') else
                       ('00000000','00000001') if c['name'] in ('lav_transport_run','lav_loop_run_internal','lav_loop_end_pause') else
                       ('8004040a',) if c['name']=='lav_loop_negative_update' else
                       ('00040001',) if c['name'] in ('lav_transport_pending_update','lav_transport_pending_status') else ('00000000',))
            if c['hr'] not in allowed and not recovered_transport_attempt(calls,index):
                return False
            if c.get('attempt',1)!=1 and (c.get('attempt')!=2 or not index or not recovered_transport_attempt(calls,index-1)):
                return False
    alloc = data.get('MP_LAV_ALLOCATOR', [])
    if len(alloc) != 1 or alloc[0].get('terminal_only') != '0' or alloc[0].get('selected_after_connection') != '1':
        return False
    try:
        if not all(0 < int(alloc[0][k],16) <= 0xffffffff for k in ('sink_input','mem_input','allocator')):
            return False
        if not all(0 < int(alloc[0][k]) <= 0x7fffffff for k in ('buffers','bytes','alignment')) or not 0 <= int(alloc[0]['prefix']) <= 0x7fffffff:
            return False
    except (KeyError, ValueError):
        return False
    if data.get('transport_ref_releases') != TRANSPORT_RELEASE:
        return False
    names = [c['name'] for c in calls]
    required_once = [*TRANSPORT_ACQUIRE,*TRANSPORT_RELEASE,'lav_transport_cleanup_decommit',
                     'lav_transport_create_sample','lav_transport_get_surface','lav_transport_surface_desc']
    if any(names.count(n) != 1 for n in required_once):
        return False
    if not names.index('lav_connect_rgb') < names.index(TRANSPORT_ACQUIRE[0]) < names.index('stream_run'):
        return False
    cleanup_at = names.index('lav_transport_cleanup_decommit')
    if names[cleanup_at:cleanup_at+3] != ['lav_transport_cleanup_decommit','control_stop','stream_stop']:
        return False
    if not cleanup_at+2 < names.index(TRANSPORT_RELEASE[0]) < names.index(TRANSPORT_RELEASE[1]) < names.index('lav_release_sink_in'):
        return False
    if mode in ('seek-only-interval','seek-only-recovery'):return check_loop_epochs(item)
    ref = mode in REFERENCE_MODES
    integer = mode in ('integer-matrix','pending-integer','seek-only-interval','seek-only-recovery')
    seek_name = 'lav_integer_seek' if integer else 'lav_transport_seek'
    position_name = 'lav_integer_position' if integer else 'lav_transport_position'
    if mode in GRAPH_MODES and item['header'].get('lav_seek_api') != ('IMediaSeeking_integer' if integer else 'legacy_IMediaPosition_double'):return False
    if data.get('MP_LAV_REFERENCE',[]) != ([{'skipped_seek':'1','origin':'unseeked_constructor_start'}] if ref else []) or data.get('MP_LAV_TARGET_DUMP'):
        return False
    wanted = 260 if ref else 6
    expected_identity = None
    for i, (epoch,target) in enumerate(zip(epochs,targets)):
        b, ready, end = epoch['begin'], epoch.get('ready',{}), epoch.get('end',{})
        if (b.get('target_ms') != str(target) or b.get('frames') != str(wanted) or b.get('reference') != str(int(ref)) or
                epoch.get('frame_end',0)-epoch['frame_start'] != wanted or epoch.get('sample_end',0)-epoch['sample_start'] != wanted):
            return False
        identity = (ready.get('sample'),ready.get('surface'))
        try:
            if not all(0 < int(v,16) <= 0xffffffff for v in identity):return False
        except (ValueError,TypeError):return False
        if ready.get('allocator') != alloc[0]['allocator'] or (expected_identity and identity != expected_identity):
            return False
        if (end.get('sample'),end.get('surface')) != identity:
            return False
        if i and (b.get('sample'),b.get('surface')) != identity:
            return False
        if not i:
            try:
                if int(b['sample'],16) or int(b['surface'],16):return False
            except (KeyError,ValueError):return False
        expected_identity = identity
        epoch_calls = calls[epoch['call_start']:epoch['call_end']]
        enames = [c['name'] for c in epoch_calls]
        required = ['second_pause'] if ref else ['lav_transport_pause','lav_transport_decommit','lav_transport_stop','lav_transport_state']
        if not ref:
            if i:required += ['lav_transport_abort','lav_transport_retired']
            required += [seek_name,position_name]
        if not i:required += ['lav_transport_create_sample','lav_transport_get_surface','lav_transport_surface_desc']
        required += ['lav_transport_run','lav_transport_state','sample_times_ready_diagnostic']
        for retry_name in ('lav_transport_seek','lav_integer_seek','lav_transport_run','lav_loop_run_internal'):
            selected=[j for j,c in enumerate(epoch_calls) if c['name']==retry_name]
            if len(selected)>(0 if (ref and retry_name!='lav_transport_run') or (retry_name not in (seek_name,'lav_transport_run')) else 2):return False
            if len(selected)==2 and not recovered_transport_attempt(epoch_calls,selected[0]):return False
        cursor = 0
        try:
            for n in required:cursor = enames.index(n,cursor)+1
        except ValueError:return False
        if not ref:
            at = enames.index('lav_transport_decommit')
            if enames[at:at+3] != ['lav_transport_decommit','lav_transport_stop','lav_transport_state']:return False
        samples = item['samples'][epoch['sample_start']:epoch['sample_end']]
        if (abs(int(samples[0]['start'])) > 800000 or any(int(a['start']) >= int(b['start']) for a,b in zip(samples,samples[1:]))):
            return False
        states = [r for r in data.get('MP_LAV_TRANSPORT_STATE',[]) if epoch['call_start'] <= r['call_index'] < epoch['call_end']]
        if [(r.get('site'),r.get('state'),r.get('hr')) for r in states] != ([] if ref else [('stopped','0','00000000')])+[('running','2','00000000')]:
            return False
        positions = [r for r in data.get('MP_LAV_INTEGER_POSITION' if integer else 'MP_LAV_TRANSPORT_POSITION',[]) if epoch['call_start'] <= r['call_index'] < epoch['call_end']]
        if ref:
            if positions:return False
        elif integer:
            if len(positions)!=1 or any(positions[0].get(k)!=str(v) for k,v in dict(requested_ticks=target*10000,actual_ticks=target*10000,current_flags=1,stop_flags=0,stop_null=1).items()):return False
        elif (len(positions) != 1 or positions[0].get('requested_ms') != str(target) or
              not __import__('math').isfinite(float(positions[0]['actual_seconds'])) or abs(float(positions[0]['actual_seconds'])*1000-target) > 40):
            return False
        captures = [r for r in data.get('MP_LAV_TRANSPORT_DUMP',[]) if epoch['frame_start'] <= int(r['index']) < epoch['frame_end']]
        expected_indices = list(range(6))
        if ref:
            target_count=9 if mode=='reference-extended' else 6
            target_indices = [j for j,s in enumerate(samples) if int(s['start']) >= 100000000][:target_count]
            if len(target_indices) != target_count or abs(int(samples[target_indices[0]]['start'])-100000000)>400000:return False
            expected_indices += target_indices
        if mode=='reference-matrix':expected_indices=list(GRAPH_ORACLE_INDICES)
        if [int(r['index'])-epoch['frame_start'] for r in captures] != expected_indices:return False
        if mode=='reference-matrix' and ([int(r['start']) for r in captures]!=GRAPH_ORACLE_STARTS or any(int(r['end'])!=int(r['start'])+400000 for r in captures)):return False
    dc = data.get('MP_LAV_TRANSPORT_DECOMMIT',[])
    if ([r.get('cleanup') for r in dc] != ([] if ref else ['0']*len(epochs))+['1'] or
            any(r.get('allocator') != alloc[0]['allocator'] or r.get('manual_commit') != '0' for r in dc)):
        return False
    cancels = data.get('MP_LAV_CANCEL',[])
    if len(cancels) != (0 if ref else len(epochs)-1) or any(r.get('abort_hr') not in RETIRED or r.get('settled_hr') not in RETIRED or r.get('discarded') != '1' or r.get('sample') != expected_identity[0] for r in cancels):
        return False
    composite=mode=='pending-integer'
    pending = data.get('MP_LAV_PENDING_PAIR' if composite else 'MP_LAV_PENDING',[])
    if mode in ('pending','pending-integer'):
        if (len(pending)!=1 or pending[0].get('observed')!='1' or pending[0].get('update_hr')!='00040001' or pending[0].get('status_hr')!='00040001' or
                pending[0].get('sample')!=expected_identity[0] or not epochs[0]['call_end'] <= pending[0]['call_index'] < epochs[1]['call_start']):return False
        if mode=='pending-integer':
            if (pending[0].get('surface')!=expected_identity[1] or pending[0].get('allocator')!=alloc[0]['allocator'] or
                    pending[0].get('epoch')!='0' or pending[0].get('outcome')!='pending_observed' or
                    item['header'].get('lav_pending_probe')!='composite_deferred' or names.count('lav_pending_pair')!=1 or
                    names.count('lav_transport_pending_update') or names.count('lav_transport_pending_status')):return False
            if cancels[0].get('retirement')!=('completion_observed_at_retirement' if '00000000' in (cancels[0]['abort_hr'],cancels[0]['settled_hr']) else 'terminal_abort_or_no_update'):return False
    elif pending:return False
    if mode!='pending-integer' and (data.get('MP_LAV_PENDING_PAIR') or 'lav_pending_pair' in names):return False
    forbidden = ('seek','control_run','second_pause') if not ref else ('seek','lav_transport_seek','lav_transport_stop')
    if any(n in names for n in forbidden):return False
    if integer and any(n in names for n in ('lav_transport_seek','lav_transport_position')):return False
    if not integer and any(n in names for n in ('lav_integer_seek','lav_integer_position')):return False
    surfaces,meta = data.get('MP_SURFACE',[]), data.get('MP_COPY_META',[])
    return (len(surfaces)==1 and surfaces[0].get('bits')=='32' and len(meta)==wanted*len(epochs) and
            all(m.get('bits')=='32' and m.get('supported')=='1' and m.get('frame')==str(i) for i,m in enumerate(meta)))


def consume(item, kind, row):
    if item['variant'] == 'lav-explicit' and kind in ('MP_SURFACE', 'MP_COPY_META'):
        item.setdefault('lav', {}).setdefault(kind, []).append(row)
        return True
    if consume_recovery(item,kind,row):return True
    if consume_loop(item,kind,row):return True
    if transport_mode(item) and consume_transport(item,kind,row):
        return True
    if kind == 'MP_REF' and row.get('name') in TRANSPORT_RELEASE:
        pending=item['pending']
        if not transport_mode(item) or not pending or pending['name']!=row['name'] or pending['phase']!='cleanup' or not 0<=int(row['remaining'])<=0xffffffff:
            raise ValueError('unpaired transport reference release')
        item.setdefault('lav',{}).setdefault('transport_ref_releases',[]).append(row['name'])
        return True
    if kind == 'MP_REF' and row.get('name') in TERMINAL_RELEASE:
        pending = item['pending']
        if (not terminal_mode(item) or not pending or pending['name'] != row['name'] or
                pending['phase'] != 'cleanup' or not 0 <= int(row['remaining']) <= 0xffffffff):
            raise ValueError('unpaired terminal allocator reference release')
        item.setdefault('lav', {}).setdefault('terminal_ref_releases', []).append(row['name'])
        return True
    if not kind.startswith('MP_LAV_'):
        return False
    if item['variant'] != 'lav-explicit':
        raise ValueError('provider witness outside explicit variant')
    data = item.setdefault('lav', {})
    if kind == 'MP_LAV_EVENTS':
        calls = item['returned_calls']
        if (item['pending'] or not calls or calls[-1]['name'] != 'lav_event_poll' or
                calls[-1]['hr'] != '80004004' or row.get('drained') != '1' or not 0 <= int(row['count']) < 32):
            raise ValueError('invalid provider event-drain witness')
        paired=data.setdefault('_paired_calls',{}).setdefault(kind,[])
        if len(calls) in paired:raise ValueError('reused provider event witness')
        paired.append(len(calls))
        data.setdefault(kind, []).append(row)
        return True
    if kind == 'MP_LAV_BIND':
        raise ValueError('obsolete COM-section witness is not supported')
    expected = {'MP_LAV_CONTEXT': 'lav_query_context', 'MP_LAV_CLASS': 'lav_created_class', 'MP_LAV_MODULE': 'lav_module_path', 'MP_LAV_ASSEMBLY_MODULE': 'lav_module_path',
                'MP_LAV_SETTING': 'lav_pixel_format', 'MP_LAV_DITHER': 'lav_dither_get', 'MP_LAV_SINK': 'lav_sink_clsid',
                'MP_LAV_RGB': 'lav_rgb_type', 'MP_LAV_CAPS': 'lav_time_format',
                'MP_LAV_REFERENCE': 'second_pause', 'MP_LAV_TARGET_DUMP': 'lav_dump_close',
                'MP_LAV_STATE':'lav_state_probe', 'MP_LAV_EVENT':'lav_event_poll',
                'MP_LAV_ALLOCATOR':'lav_terminal_properties', 'MP_LAV_DECOMMIT':'lav_terminal_decommit'}
    if kind == 'MP_LAV_ALLOCATOR' and transport_mode(item):expected[kind]='lav_transport_properties'
    if kind in expected:
        calls = item['returned_calls']
        if item['pending'] or not calls or calls[-1]['name'] != expected[kind] or int(calls[-1]['hr'], 16) & 0x80000000:
            raise ValueError('unpaired provider witness '+kind)
        witnessed = data.setdefault('_paired_calls', {}).setdefault(kind, [])
        if len(calls) in witnessed:
            raise ValueError('reused provider call witness '+kind)
        witnessed.append(len(calls))
        data.setdefault(kind, []).append(row)
        if kind == 'MP_LAV_STATE' and (row.get('hr') != calls[-1]['hr'] or row.get('state') != '1' or row.get('pending_is_not_readiness') != '1'):
            raise ValueError('invalid paused-state observation')
    if kind in ('MP_LAV_ALLOCATOR', 'MP_LAV_DECOMMIT') and not terminal_mode(item) and not (kind=='MP_LAV_ALLOCATOR' and transport_mode(item)):
        raise ValueError('terminal allocator witness outside explicit diagnostic')
    if kind == 'MP_LAV_TARGET_DUMP':
        if (len(data[kind]) != 1 or item['header'].get('lav_scenario') != 'reference' or
                len(item['frames']) < 2 or row.get('file') != 'reference-target.bgra' or
                [c['name'] for c in item['returned_calls'][-3:]] != ['lav_dump_create', 'lav_dump_write', 'lav_dump_close'] or
                any(c['hr'] != '00000000' for c in item['returned_calls'][-3:]) or
                not all(row.get(k) == item['frames'][-1].get(k) for k in ('index', 'start', 'end', 'hash', 'bytes'))):
            raise ValueError('invalid same-provider reference capture')
    return True


def check_terminal(item):
    """Terminal-only intervention, never ordinary constructor/playback proof."""
    calls = item['returned_calls']
    names = [c['name'] for c in calls]
    data = item.get('lav', {})
    diagnostic_calls = [c for c in calls if c['name'] in TERMINAL_ARGS]
    if transport_mode(item):
        return not diagnostic_calls
    if not terminal_mode(item):
        return not diagnostic_calls and not any(data.get(k) for k in
            ('MP_LAV_ALLOCATOR', 'MP_LAV_DECOMMIT', 'terminal_ref_releases'))
    if (item['stage'] != 'constructor' or item['header'].get('start_ms') != '0' or
            item['header'].get('lav_scenario') != 'seek' or item['frames'] or item['samples'] or
            any(n in names for n in ('seek', 'second_pause', 'create_sample', 'update', 'control_run'))):
        return False
    for name, args in TERMINAL_ARGS.items():
        found = [c for c in diagnostic_calls if c['name'] == name]
        if (len(found) != 1 or found[0]['hr'] != '00000000' or found[0]['args'] != args or
                found[0]['phase'] != ('constructor' if name in TERMINAL_ACQUIRE else 'cleanup')):
            return False
    expected = ['lav_connect_rgb', *TERMINAL_ACQUIRE, 'stream_run', 'constructor_pause',
                'lav_terminal_decommit', 'control_stop', 'stream_stop', *TERMINAL_RELEASE,
                'lav_release_sink_in', 'release_graph', 'CoUninitialize']
    cursor = 0
    try:
        for name in expected:
            cursor = names.index(name, cursor)+1
    except ValueError:
        return False
    at = names.index('lav_terminal_decommit')
    if names[at:at+3] != ['lav_terminal_decommit', 'control_stop', 'stream_stop']:
        return False
    if data.get('terminal_ref_releases') != TERMINAL_RELEASE:
        return False
    allocators, decommits = data.get('MP_LAV_ALLOCATOR', []), data.get('MP_LAV_DECOMMIT', [])
    if len(allocators) != 1 or len(decommits) != 1:
        return False
    a, d = allocators[0], decommits[0]
    try:
        pointers = all(0 < int(a[k], 16) <= 0xffffffff for k in ('sink_input', 'mem_input', 'allocator'))
        properties = (all(0 < int(a[k]) <= 0x7fffffff for k in ('buffers', 'bytes', 'alignment')) and
                      0 <= int(a['prefix']) <= 0x7fffffff)
    except (KeyError, ValueError, TypeError):
        return False
    return (pointers and properties and a.get('selected_after_connection') == '1' and
            a.get('terminal_only') == '1' and d.get('allocator') == a.get('allocator') and
            d.get('terminal_only') == '1' and d.get('recommit') == '0')


def check_stage(item):
    """Structural proof only: content acceptance is a separate cross-run check."""
    data = item.get('lav', {})
    derived=item.get('header',{}).get('media_kind')=='derived_stream_copy_matroska'
    if derived and (not transport_mode(item) or item['header']['lav_transport'] not in ('reference','epochs','boundary','pending','reference-matrix','integer-matrix','pending-integer','seek-only-interval','seek-only-recovery')):
        return False
    if not derived and item.get('header',{}).get('lav_transport') in ('boundary','integer-matrix','pending-integer','seek-only-interval','seek-only-recovery'):return False
    if derived and not any(c['name']=='lav_load' and c.get('args')=='derived_Matroska_NULL_type' and c['hr']=='00000000' for c in item['returned_calls']):
        return False
    if not check_terminal(item) or not check_dither(item):
        return False
    calls = item['returned_calls']
    names = [c['name'] for c in calls]
    if any(n in names for n in ('open_file', 'render', 'add_source', 'create_mpeg_video', 'create_mpeg_splitter')):
        return False
    if any(int(c['hr'], 16) & 0x80000000 for index,c in enumerate(calls) if c['name'].startswith('lav_') and
           not (transport_mode(item) and recovered_transport_attempt(calls,index)) and
           not (loop_mode(item) and c['name']=='lav_loop_negative_update' and c['hr']=='8004040a') and
           not (c['name'] == 'lav_event_poll' and c['hr'] == '80004004') and
           not (transport_mode(item) and c['name'] in ('lav_transport_abort','lav_transport_retired','lav_loop_negative_abort','lav_loop_negative_retired') and c['hr']=='80004004')):
        return False
    drains=len(transport_targets(item['header']['lav_transport']))+2 if transport_mode(item) else 1
    if len(data.get('MP_LAV_STATE', [])) != 1 or len(data.get('MP_LAV_EVENTS', [])) != drains:
        return False
    if sum(int(r['count']) for r in data['MP_LAV_EVENTS']) != len(data.get('MP_LAV_EVENT', [])):
        return False
    if any(int(e['code']) in (2,3,6,7) for e in data.get('MP_LAV_EVENT', [])):
        return False
    contexts = data.get('MP_LAV_CONTEXT', [])
    if (len(contexts) != 1 or contexts[0].get('manifest_matches') != '1' or
            contexts[0].get('com_redirection_observed') != '0' or not contexts[0].get('root_manifest') or
            contexts[0]['root_manifest'].lower() != contexts[0].get('expected', '').lower()):
        return False
    classes = data.get('MP_LAV_CLASS', [])
    if (len(classes) != 2 or {c.get('role') for c in classes} != {'source', 'decoder'} or
            any(c.get('clsid', '').upper() != (SOURCE_CLSID if c['role'] == 'source' else VIDEO_CLSID) or
                c.get('expected', '').upper() != c.get('clsid', '').upper() or c.get('matches') != '1' for c in classes)):
        return False
    modules = data.get('MP_LAV_MODULE', [])
    if len(modules) != 2 or {m.get('name') for m in modules} != {'LAVSplitter.ax', 'LAVVideo.ax'} or any(m.get('exact') != '1' for m in modules):
        return False
    assembly=data.get('MP_LAV_ASSEMBLY_MODULE',[])
    if item['header'].get('lav_transport') in GRAPH_MODES:
        if len(assembly)!=len(BINARIES) or {m.get('name') for m in assembly}!=set(BINARIES) or any(m.get('exact')!='1' for m in assembly):return False
    elif assembly:return False
    settings = data.get('MP_LAV_SETTING', [])
    # Public 0.81 enum: RGB32=11, NB=18. Require all formats explicitly set.
    if [(r.get('format'), r.get('enabled')) for r in settings] != [(str(i), str(int(i == 11))) for i in range(18)]:
        return False
    rgb = data.get('MP_LAV_RGB', [])
    caps = data.get('MP_LAV_CAPS', [])
    sink = data.get('MP_LAV_SINK', [])
    if (len(rgb) != 1 or rgb[0].get('valid') != '1' or rgb[0].get('bits') != '32' or
            rgb[0].get('width') != '512' or rgb[0].get('height') not in ('512', '-512') or
            len(caps) != 1 or caps[0].get('absolute_media_time') != '1' or len(sink) != 1):
        return False
    expected = {SOURCE_CLSID, VIDEO_CLSID, sink[0]['clsid'].upper()}
    if len(item['filters']) != 3 or {f['clsid'].upper() for f in item['filters']} != expected:
        return False
    for n, args in {'lav_get_current_context':'owner_STA_GetCurrentActCtx',
                    'lav_query_context':'QueryActCtxW_DetailedInformation',
                    'lav_release_current_context':'GetCurrentActCtx_reference',
                    'lav_created_class':'created_filter_GetClassID',
                    'lav_source_runtime':'TRUE_before_connect', 'lav_video_runtime':'TRUE_before_connect',
                    'lav_software':'HWAccel_None', 'lav_threads':'1',
                    'lav_connect_compressed':'ConnectDirect_source_decoder_NULL',
                    'lav_connect_rgb':'ConnectDirect_decoder_sink_NULL'}.items():
        # Pinned LAV 0.81 SetHWAccel/SetNumThreads assign their runtime values
        # then return SaveSettings(): S_FALSE means no registry save in runtime
        # mode. Other calls still require S_OK; arbitrary success codes do not.
        allowed = ('00000000', '00000001') if n in ('lav_software', 'lav_threads') else ('00000000',)
        if not any(c['name'] == n and c.get('args') == args and c['hr'] in allowed for c in calls):
            return False
    if transport_mode(item):return check_transport(item)
    if item['stage'] == 'constructor':
        return True
    reference = item['header'].get('lav_scenario') == 'reference'
    if reference:
        if ('seek' in names or data.get('MP_LAV_REFERENCE') != [{'skipped_seek':'1', 'origin':'unseeked_constructor_start'}] or
                len(data.get('MP_LAV_TARGET_DUMP', [])) != 1):
            return False
    elif 'seek' not in names or data.get('MP_LAV_REFERENCE') or data.get('MP_LAV_TARGET_DUMP'):
        return False
    count = 260 if reference else 6
    meta = data.get('MP_COPY_META', [])
    surfaces = data.get('MP_SURFACE', [])
    if (len(meta) != count or len(surfaces) != 1 or surfaces[0].get('bits') != '32' or
            any(r.get('bits') != '32' or r.get('supported') != '1' or r.get('src_width') != '512' or
                r.get('src_height') != '512' or r.get('frame') != str(i) for i, r in enumerate(meta))):
        return False
    return (len(item['frames']) == count and len(item['samples']) == count and
            item['progressing_timestamps'] == count and abs(int(item['samples'][0]['start'])) <= 800000)


def read_capture(directory, witness, fnv):
    path = directory/witness['file']
    if path.is_symlink() or not path.is_file() or path.stat().st_size != 1048576:
        raise ValueError('missing/bad provider frame capture')
    data = path.read_bytes()
    if fnv(data) != witness['hash']:
        raise ValueError('provider capture disagrees with copied frame')
    return data


def finish(record, directory, sound_run, fnv):
    """Never changes original/game acceptance; exact provider images gate success."""
    result = dict(accepted=False, content_proven=False, reference_ready=False,
                  derived_linear_equivalence=False,derived_reference_ready=False,derived_transport_diagnostic_accepted=False,derived_content_proven=False,
                  extended_original_reference_ready=False,derived_boundary_qualified=False,derived_pending_qualified=False,
                  terminal_decommit_diagnostic_accepted=False, transport_diagnostic_accepted=False, transport_reference_ready=False,
                  live_newsegment_observed=False, timestamp_domain='segment_relative_source_contract_and_content',
                  original_game_playback_proven=False, native_windows_qualified=False)
    try:
        provider_path = Path(record['lav_provider']['path'])
        provider = verify_record(provider_path)
        item = record['stages'][record['lav_stage']]
        root = provider_path.parent.resolve()
        if record.get('lav_graph_provider'):
            from prepare_lav_graph_provider import verify_record as verify_graph
            binding=record['lav_graph_provider'];selected_path=Path(binding['path'])
            if hashlib.sha256(selected_path.read_bytes()).hexdigest()!=binding['sha256']:
                raise ValueError('selected graph composition record changed')
            selected=verify_graph(selected_path)
            if (selected['inputs']['official']['record_sha256']!=record['lav_provider']['sha256'] or
                    record.get('lav_transport') not in GRAPH_MODES or not record.get('lav_derived_source')):
                raise ValueError('graph composition official binding or mode differs')
            root=selected_path.parent.resolve()
        context_path = unquote(item['lav']['MP_LAV_CONTEXT'][0]['root_manifest']).replace('\\', '/')
        if context_path[:2].lower() == 'z:':
            context_path = context_path[2:]
        if Path(context_path).resolve() != root/'provider.manifest':
            raise ValueError('observed active manifest differs from pinned provider manifest')
        loaded = {}
        forbidden = []
        for row in item['modules']:
            path = unquote(row['path']).replace('\\', '/')
            if path[:2].lower() == 'z:':
                path = path[2:]
            name = Path(path).name.lower()
            if (name in ('winegstreamer.dll', 'lavaudio.ax', 'intelquicksyncdecoder.dll', 'nvcuvid.dll') or
                    (record.get('lav_transport') in GRAPH_MODES and name not in {n.lower() for n in BINARIES} and
                     ((name.startswith('lav') and name.endswith('.ax')) or '-lav-' in name))):
                forbidden.append(path)
            if name in {n.lower() for n in BINARIES}:
                loaded.setdefault(name, []).append(Path(path).resolve())
        modules_ok = not forbidden and all(loaded.get(n.lower()) == [root/n] for n in BINARIES)
        if record.get('lav_transport') in GRAPH_MODES:
            observed={}
            for row in item['lav'].get('MP_LAV_ASSEMBLY_MODULE',[]):
                path=unquote(row['path']).replace('\\','/')
                if path[:2].lower()=='z:':path=path[2:]
                observed.setdefault(row['name'].lower(),[]).append(Path(path).resolve())
            modules_ok=modules_ok and all(observed.get(n.lower())==[root/n] for n in BINARIES)
        result['module_closure_verified'] = modules_ok
        terminal = record.get('lav_terminal_decommit', False)
        if terminal != terminal_mode(item):
            raise ValueError('requested terminal allocator intervention differs from header')
        transport=record.get('lav_transport')
        if bool(transport)!=transport_mode(item):raise ValueError('requested transport mode differs from header')
        transport_ok = item.get('lav_transport_epoch_accepted') if transport else item.get('lav_terminal_cleanup_accepted') if terminal else item.get('lav_transport_accepted')
        if not sound_run or not transport_ok or not modules_ok:
            raise ValueError('provider graph, module closure, transport or outer controls unqualified')
        if transport:
            if transport in GRAPH_MODES:
                finish_graph_transport(record,directory,item,result,fnv)
            elif record.get('lav_derived_source') or record.get('derived_media') or item['header'].get('media_kind')=='derived_stream_copy_matroska':
                finish_derived_transport(record,directory,item,result,fnv)
            elif transport=='reference-extended':
                finish_extended_original(record,directory,item,result,fnv)
            else:
                finish_transport(record,directory,item,result,fnv)
        elif terminal:
            if record['lav_stage'] != 'constructor' or record.get('lav_start_ms') != 0 or record.get('lav_scenario') != 'seek':
                raise ValueError('terminal allocator intervention outside constructor')
            result.update(terminal_decommit_diagnostic_accepted=True, outcome='terminal_allocator_decommit_cleanup_only')
        elif record['lav_stage'] == 'constructor':
            result.update(accepted=True, outcome='constructor_capability_only')
        elif record['lav_scenario'] == 'reference':
            witness = item['lav']['MP_LAV_TARGET_DUMP'][0]
            target = read_capture(directory, witness, fnv)
            if abs(int(witness['start'])-100000000) > 400000:
                raise ValueError('sequential target missing at 10s frame boundary')
            result.update(accepted=True, reference_ready=True, target_sha256=hashlib.sha256(target).hexdigest(),
                          target_witness=witness, outcome='same_provider_sequential_reference_only')
        else:
            reference_path = Path(record['lav_reference_result']['path'])
            if hashlib.sha256(reference_path.read_bytes()).hexdigest() != record['lav_reference_result']['sha256']:
                raise ValueError('reference record changed')
            ref = json.loads(reference_path.read_text())
            if (not ref.get('lav_qualification', {}).get('reference_ready') or
                    not ref.get('lav_qualification', {}).get('accepted') or
                    not ref.get('lav_qualification', {}).get('module_closure_verified') or
                    not ref.get('stages', {}).get('copy', {}).get('lav_transport_accepted') or
                    ref.get('exit_code') != 0 or not ref.get('unchanged') or not all(ref['unchanged'].values()) or
                    ref.get('outer_timeout') or ref.get('diagnostic_log_limit') or ref.get('diagnostic_log_monitor_error') or
                    ref.get('lav_scenario') != 'reference' or ref.get('media') != record['media'] or
                    ref.get('exe', {}).get('sha256') != record['exe']['sha256'] or
                    ref.get('lav_provider', {}).get('sha256') != record['lav_provider']['sha256']):
                raise ValueError('reference provider/media/EXE not identical')
            ref_item = ref['stages']['copy']
            first = read_capture(directory, item['frame_dump'], fnv)
            witness = ref_item['frame_dump'] if record['lav_start_ms'] == 0 else ref_item['lav']['MP_LAV_TARGET_DUMP'][0]
            target = read_capture(reference_path.parent, witness, fnv)
            head = read_capture(reference_path.parent, ref_item['frame_dump'], fnv)
            exact = first == target
            result.update(content_proven=exact, first_sha256=hashlib.sha256(first).hexdigest(),
                          target_sha256=hashlib.sha256(target).hexdigest(), same_as_head=first == head,
                          matched_reference_source_time_100ns=int(witness['start']),
                          delivered_sample_start_100ns=int(item['samples'][0]['start']))
            if not exact or (record['lav_start_ms'] == 10000 and target == head):
                raise ValueError('seeked copied image is not exact sequential requested content')
            result.update(accepted=True, outcome='explicit_provider_content_diagnostic_accepted')
    except (OSError, ValueError, KeyError, TypeError) as e:
        result.update(accepted=False, outcome='provider_unqualified', error=str(e))
    if record.get('lav_transport')=='pending-integer':
        stage=record.get('stages',{}).get('copy',{})
        rows=stage.get('lav',{}).get('MP_LAV_PENDING_PAIR' if stage.get('header',{}).get('lav_pending_probe')=='composite_deferred' else 'MP_LAV_PENDING',[])
        result['pending_transition_observed']=bool(len(rows)==1 and rows[0].get('observed')=='1' and rows[0].get('update_hr')=='00040001' and rows[0].get('status_hr')=='00040001')
        if (len(rows)==1 and rows[0].get('observed')=='0' and rows[0].get('outcome')=='pending_not_observed' and
                '00000000' in (rows[0].get('update_hr'),rows[0].get('status_hr')) and
                all(rows[0].get(k) in ('00000000','00040001') for k in ('update_hr','status_hr'))):
            result.update(outcome='pending_not_observed',graph_pending_integer_accepted=False)
        if record.get('stages',{}).get('copy',{}).get('lav',{}).get('MP_LAV_PENDING_UNRESOLVED'):
            result.update(outcome='pending_retirement_unresolved_owned_child_terminated',graph_pending_integer_accepted=False,storage_releases_skipped=True)
    if record.get('lav_transport') in ('seek-only-interval','seek-only-recovery'):
        stage=record.get('stages',{}).get('copy',{});data=stage.get('lav',{})
        recovery=record['lav_transport']=='seek-only-recovery'
        if recovery:
            result.setdefault('graph_cold_miss_recovery_accepted',False)
            result.update(cold_start_content_qualified=False,initial_interval_content_qualified=False,graph_seek_only_interval_accepted=False)
            result.setdefault('automatic_recovery_after_observed_zero_frame_interval',False)
            initial=data.get('MP_LAV_RECOVERY_INITIAL',[])
            result['initial_miss_observed']=bool(len(initial)==1 and initial[0].get('initial_miss_observed')=='1')
        outcomes=data.get('MP_LAV_RECOVERY_OUTCOME' if recovery else 'MP_LAV_LOOP_OUTCOME',[])
        if len(outcomes)==1 and outcomes[0].get('reason') in ('positive_end_before_first_frame','loop_oracle_window_exhausted','initial_miss_not_observed','restart_positive_end_before_first_frame'):
            result.update(coverage_reason=outcomes[0]['reason'],graph_seek_only_interval_accepted=False,coverage_complete=False)
            # Coverage never replaces the primary protocol/provenance/content failure.
            try:
                _,ow,oracle=load_graph_original(record,fnv)
                witnesses=data.get('MP_LAV_TRANSPORT_DUMP',[])
                raw=[read_capture(directory,w,fnv) for w in witnesses]
                prefix=loop_observed_prefix(stage,witnesses,raw,ow,oracle)
                result['previous_admitted_prefix_exact']=prefix
                if not prefix:result['content_error']='previously admitted prefix differs'
            except (OSError,ValueError,KeyError,TypeError) as error:
                result['previous_admitted_prefix_exact']=False;result['content_error']=str(error)
        if data.get('MP_LAV_PENDING_UNRESOLVED'):
            result.update(outcome='pending_retirement_unresolved_owned_child_terminated',graph_seek_only_interval_accepted=False,storage_releases_skipped=True)
    record['lav_qualification'] = result
    for key in ('accepted', 'playback_proven', 'diagnostic_variant_accepted',
                'variant_frame_delivery_proven', 'nonzero_seek_frame_delivery_proven'):
        record[key] = False


def trace_transport(path, item):
    """CrossOver observation adapter only. No inferred timestamp offsets/layouts."""
    import re
    result = dict(verified=False, adapter='CrossOver_public_call_trace', segments=[], commits=0, decommits=0,
                  timestamp_normalization=False, native_windows_observed=False)
    try:
        mode = item['header']['lav_transport']
        allocator = int(item['lav']['MP_LAV_ALLOCATOR'][0]['allocator'],16)
        pid = int(item['child_pid'])
        stream = None
        pending_commit = False
        commit_line = None
        with path.open(errors='replace') as file:
            for line_number,line in enumerate(file,1):
                prefix = re.match(r'^\d+\.\d+:([0-9a-fA-F]+):[0-9a-fA-F]+:trace:(?:quartz|amstream):([^ ]+) (.*)$',line)
                if not prefix or int(prefix[1],16) != pid:continue
                function, text = prefix[2], prefix[3]
                match = re.search(r'stream ([0-9a-fA-F]+)',text)
                if not match:continue
                current = int(match[1],16)
                if function == 'ddraw_meminput_NotifyAllocator':
                    selected = re.search(r'allocator ([0-9a-fA-F]+)',text)
                    if selected and int(selected[1],16) == allocator:
                        if stream is not None:raise ValueError('duplicate selected allocator notification')
                        stream=current
                    continue
                if stream is None or current != stream:continue
                if function == 'ddraw_mem_allocator_Commit':
                    if pending_commit:raise ValueError('allocator recommitted without segment')
                    if result['segments'] and not result['segments'][-1]['decommit_lines']:
                        raise ValueError('restart Commit without prior selected allocator Decommit')
                    result['commits']+=1;pending_commit=True;commit_line=line_number
                elif function == 'ddraw_mem_allocator_Decommit':
                    if not result['segments'] or pending_commit:
                        raise ValueError('selected allocator Decommit outside completed segment boundary')
                    result['decommits']+=1
                    result['segments'][-1]['decommit_lines'].append(line_number)
                elif function == 'ddraw_sink_NewSegment':
                    found=re.search(r'start (-?\d+), stop (-?\d+), rate ([-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?)',text)
                    if not found or not pending_commit or float(found[3])!=1.0:
                        raise ValueError('segment lacks matching filter-owned Commit or normal rate')
                    result['segments'].append(dict(start_100ns=int(found[1]),stop_100ns=int(found[2]),receives=0,commit_line=commit_line,segment_line=line_number,decommit_lines=[]))
                    pending_commit=False
                elif mode in ('seek-only-interval','seek-only-recovery') and function=='ddraw_sink_EndOfStream':raise ValueError('EOS is not a positive interval crossing')
                elif function == 'ddraw_meminput_Receive':
                    if not result['segments'] or pending_commit:raise ValueError('Receive before observed segment')
                    result['segments'][-1]['receives']+=1
                if len(result['segments'])>(16 if mode=='integer-matrix' else 8) or result['commits']>(16 if mode=='integer-matrix' else 8) or result['decommits']>(48 if mode=='integer-matrix' else 32):
                    raise ValueError('unbounded transport trace transitions')
        wanted = [0] if mode in REFERENCE_MODES else [0]+[t*10000 for t in transport_targets(mode)]
        segments = result['segments']
        counts = [260] if mode in REFERENCE_MODES else [0]+[6]*len(transport_targets(mode))
        if mode in ('seek-only-interval','seek-only-recovery'):counts=[0]+[e.get('frame_end',0)-e['frame_start']+1 for e in item['lav']['epochs']]
        if (stream is None or pending_commit or not segments or not segments[-1]['decommit_lines'] or result['commits']!=len(wanted) or
                [s['start_100ns'] for s in segments]!=wanted or
                any(not (n <= s['receives'] <= n+int(mode in ('pending','pending-integer') and i==1)) for i,(s,n) in enumerate(zip(segments,counts))) or
                result['decommits'] < (1 if mode in REFERENCE_MODES else len(transport_targets(mode))+1)):
            raise ValueError('actual allocator Commit/segment/Receive epochs missing or inconsistent')
        result.update(verified=True,selected_allocator=f'{allocator:08x}',stream=f'{stream:08x}')
    except (OSError,ValueError,KeyError,TypeError,IndexError) as error:
        result['error']=str(error)
    return result


def finish_transport(record, directory, item, result, fnv):
    """Exact destination bytes, same original provider; no cross-decoder tolerance."""
    mode=record['lav_transport']
    if mode != item['header'].get('lav_transport') or not record.get('lav_transport_trace',{}).get('verified'):
        raise ValueError('transport declaration or actual backend segment/Commit proof absent')
    if record.get('lav_transport_settings') != TRANSPORT_SETTINGS or not item.get('lav_dither_settings_verified') or item.get('lav',{}).get('MP_LAV_DITHER') != [DITHER_WITNESS]:
        raise ValueError('transport ordered-dither declaration or actual witness absent')
    captures=item['lav']['MP_LAV_TRANSPORT_DUMP']
    content=[read_capture(directory,w,fnv) for w in captures]
    result.update(transport_mode=mode,transport_settings=dict(TRANSPORT_SETTINGS),transport_capture_sha256=[hashlib.sha256(b).hexdigest() for b in content],
                  live_newsegment_observed=True,timestamp_domain='actual_source_segment_and_exact_same_provider_content')
    if mode=='reference':
        if len(content)!=12 or content[:6]==content[6:]:
            raise ValueError('reference head/10s content not distinguishable')
        result.update(transport_diagnostic_accepted=True,transport_reference_ready=True,
                      outcome='transport_sequential_reference_only')
        return
    reference_path=Path(record['lav_reference_result']['path'])
    if hashlib.sha256(reference_path.read_bytes()).hexdigest()!=record['lav_reference_result']['sha256']:
        raise ValueError('transport reference record changed')
    ref=json.loads(reference_path.read_text())
    q=ref.get('lav_qualification',{})
    if (ref.get('lav_transport')!='reference' or not q.get('transport_reference_ready') or
            ref.get('lav_transport_settings') != TRANSPORT_SETTINGS or q.get('transport_settings') != TRANSPORT_SETTINGS or
            not ref.get('stages',{}).get('copy',{}).get('lav_dither_settings_verified') or
            ref.get('stages',{}).get('copy',{}).get('lav',{}).get('MP_LAV_DITHER') != [DITHER_WITNESS] or
            not q.get('transport_diagnostic_accepted') or not q.get('module_closure_verified') or
            ref.get('exit_code')!=0 or not ref.get('unchanged') or not all(ref['unchanged'].values()) or
            ref.get('outer_timeout') or ref.get('diagnostic_log_limit') or ref.get('diagnostic_log_monitor_error') or
            not ref.get('lav_transport_trace',{}).get('verified') or
            not ref.get('stages',{}).get('copy',{}).get('lav_transport_epoch_accepted') or
            ref.get('media')!=record.get('media') or ref.get('exe',{}).get('sha256')!=record.get('exe',{}).get('sha256') or
            ref.get('lav_provider',{}).get('sha256')!=record.get('lav_provider',{}).get('sha256')):
        raise ValueError('unqualified or different provider/media/EXE transport reference')
    witnesses=ref['stages']['copy']['lav']['MP_LAV_TRANSPORT_DUMP']
    oracle=[read_capture(reference_path.parent,w,fnv) for w in witnesses]
    if (len(oracle)!=12 or oracle[:6]==oracle[6:] or
            [hashlib.sha256(b).hexdigest() for b in oracle]!=q.get('transport_capture_sha256')):
        raise ValueError('invalid original-ES reference images')
    targets=transport_targets(mode)
    if len(content)!=6*len(targets):raise ValueError('transport epoch images incomplete')
    matches=[content[i*6:(i+1)*6] == (oracle[:6] if target==0 else oracle[6:]) for i,target in enumerate(targets)]
    result['epoch_exact_content_matches']=matches
    if not all(matches):raise ValueError('transport epoch copied pixels differ from exact requested original-ES reference')
    result.update(transport_diagnostic_accepted=True,content_proven=True,
                  pending_transition_observed=mode=='pending',outcome='transport_original_ES_epoch_content_only')

# Fixture provenance pin, not a runtime/production provider requirement. V9 changes
# input admission/label only; its new reference must still match these V8 pixels.
ORIGINAL_REFERENCE_EXE = 'aaadea9c585e252ba0a1d4937722278459aa664d5a41939de7c627df0d00442f'


def packet_mapping(original, derived):
    """Bounded native metadata audit; never normalizes or edits media timestamps."""
    from fractions import Fraction
    streams = [x['streams'] for x in (original, derived)]
    if any(len(s) != 1 for s in streams):raise ValueError('one video stream required')
    a, b = (s[0] for s in streams)
    for s in (a,b):
        if (s.get('codec_name')!='mpeg1video' or s.get('width')!=512 or s.get('height')!=512 or
                Fraction(s.get('avg_frame_rate','0'))!=25):
            raise ValueError('derived codec/dimensions/average frame rate differ')
    bases = [Fraction(s['time_base']) for s in (a,b)]
    if any(t<=0 for t in bases):raise ValueError('invalid media timebase')
    packets=[x['packets'] for x in (original,derived)]
    if any(len(p)!=280 for p in packets):raise ValueError('280 packet boundary audit required')
    preserved=generated=0;boundary=[]
    for i,(x,y) in enumerate(zip(*packets)):
        if (x.get('size'),x.get('data_hash'))!=(y.get('size'),y.get('data_hash')) or not str(x.get('data_hash','')).startswith('SHA256:'):
            raise ValueError('derived packet payload changed')
        if 'pts' not in y:raise ValueError('derived packet PTS missing')
        pts=[Fraction(p['pts'])*t if 'pts' in p else None for p,t in zip((x,y),bases)]
        if pts[0] is not None:
            preserved+=1
            if pts[0]!=pts[1]:raise ValueError('existing original PTS changed')
        else:generated+=1
        if 9 <= pts[1] <= 11:
            boundary.append(dict(packet=i,original_pts=str(pts[0]) if pts[0] is not None else None,
                                 derived_pts=str(pts[1]),payload_sha256=x['data_hash']))
    if not preserved or not generated or not any(r['derived_pts']=='10' for r in boundary):
        raise ValueError('missing generated/existing PTS or target boundary')
    return dict(verified=True,packet_count=280,preserved_pts=preserved,completed_pts=generated,
                streams=[{k:s.get(k) for k in ('codec_name','width','height','avg_frame_rate','r_frame_rate','time_base')} for s in (a,b)],
                boundary=boundary,timestamp_adjustment=False)


def load_original_oracle(record, fnv):
    binding=record['lav_original_reference_result'];path=Path(binding['path'])
    if hashlib.sha256(path.read_bytes()).hexdigest()!=binding['sha256']:
        raise ValueError('original reference record changed')
    ref=json.loads(path.read_text());q=ref.get('lav_qualification',{});stage=ref.get('stages',{}).get('copy',{})
    original=record['derived_media']['record']['original'] if record.get('derived_media') else dict(sha256=record['media']['sha256'],bytes=record['media']['size'])
    if (ref.get('media_kind')!='original_dat' or ref.get('derived_media') or ref.get('lav_transport')!='reference' or
            ref.get('exe',{}).get('sha256')!=ORIGINAL_REFERENCE_EXE or
            ref.get('media',{}).get('sha256')!=original['sha256'] or ref.get('media',{}).get('size')!=original['bytes'] or
            ref.get('lav_provider',{}).get('sha256')!=record['lav_provider']['sha256'] or
            ref.get('lav_transport_settings')!=TRANSPORT_SETTINGS or q.get('transport_settings')!=TRANSPORT_SETTINGS or
            not q.get('transport_reference_ready') or not q.get('transport_diagnostic_accepted') or not q.get('module_closure_verified') or
            stage.get('header',{}).get('media_kind')!='original_dat' or stage.get('header',{}).get('lav_transport')!='reference' or
            not stage.get('lav_transport_epoch_accepted') or not stage.get('lav_dither_settings_verified') or
            stage.get('lav',{}).get('MP_LAV_DITHER')!=[DITHER_WITNESS] or
            not ref.get('lav_transport_trace',{}).get('verified') or ref.get('exit_code')!=0 or
            not ref.get('unchanged') or not all(ref['unchanged'].values()) or
            any(ref.get(k) for k in ('outer_timeout','diagnostic_log_limit','diagnostic_log_monitor_error','final_log_errors'))):
        raise ValueError('unqualified pinned original-ES reference')
    witnesses=stage['lav']['MP_LAV_TRANSPORT_DUMP']
    if [int(w['index']) for w in witnesses]!=list(range(6))+list(range(249,255)):
        raise ValueError('original reference capture indices differ')
    raw=[read_capture(path.parent,w,fnv) for w in witnesses]
    if [hashlib.sha256(b).hexdigest() for b in raw]!=q.get('transport_capture_sha256') or raw[:6]==raw[6:]:
        raise ValueError('original reference capture hashes differ')
    return ref,witnesses,raw


def finish_derived_transport(record,directory,item,result,fnv):
    mode=record.get('lav_transport');derivation=record.get('derived_media',{});preflight=record.get('lav_derived_preflight',{})
    if (not record.get('lav_derived_source') or mode not in ('reference','epochs','boundary','pending','reference-matrix','integer-matrix','pending-integer','seek-only-interval','seek-only-recovery') or
            record.get('media_kind')!='derived_stream_copy_matroska' or item['header'].get('media_kind')!='derived_stream_copy_matroska' or
            not preflight.get('verified') or preflight.get('timestamp_adjustment') is not False or
            preflight.get('derived_media_record_sha256')!=derivation.get('sha256') or
            preflight.get('original_reference_sha256')!=record.get('lav_original_reference_result',{}).get('sha256') or
            derivation.get('record',{}).get('schema')!=2 or derivation.get('timeline_semantics')!='generated_timestamps' or
            record.get('media',{}).get('sha256')!=derivation['record']['derived']['sha256'] or
            record.get('lav_transport_settings')!=TRANSPORT_SETTINGS or not item.get('lav_dither_settings_verified') or
            item.get('lav',{}).get('MP_LAV_DITHER')!=[DITHER_WITNESS] or
            not record.get('lav_transport_trace',{}).get('verified')):
        raise ValueError('derived source declaration/preflight/transport proof absent')
    _,original_witnesses,oracle=load_original_oracle(record,fnv)
    witnesses=item['lav']['MP_LAV_TRANSPORT_DUMP'];content=[read_capture(directory,w,fnv) for w in witnesses]
    result.update(derived_source=True,derived_timeline_semantics='generated_timestamps',
                  derived_capture_sha256=[hashlib.sha256(b).hexdigest() for b in content],
                  original_reference_sha256=record['lav_original_reference_result']['sha256'],
                  live_newsegment_observed=True,timestamp_domain='recorded_source_segment_no_normalization')
    if mode=='reference':
        same_labels=len(witnesses)==12 and all(all(a.get(k)==b.get(k) for k in ('index','start','end')) for a,b in zip(witnesses,original_witnesses))
        result.update(derived_linear_exact_pixels=content==oracle,derived_linear_same_time_labels=same_labels)
        if content!=oracle or not same_labels:
            raise ValueError('derived sequential pixels or time labels differ from original reference')
        result.update(derived_linear_equivalence=True,derived_reference_ready=True,
                      derived_transport_diagnostic_accepted=True,outcome='derived_linear_equivalence_only')
        return
    load_derived_linear(record,original_witnesses,oracle,fnv)
    if mode in ('boundary','pending'):
        extended_witnesses,extended=load_extended_original(record,original_witnesses,oracle,fnv)
        if record.get('lav_boundary_contract')!=BOUNDARY_CONTRACT:
            raise ValueError('provider-specific boundary contract absent')
        if mode=='pending':load_boundary_result(record,fnv)
        matches=[];time_matches=[]
        for e,target in enumerate(transport_targets(mode)):
            eligible=[(w,b) for w,b in zip(extended_witnesses,extended) if int(w['start'])>=target*10000][:6]
            if len(eligible)!=6:raise ValueError('extended oracle lacks target successors')
            matches.append(content[e*6:e*6+6]==[b for w,b in eligible])
            observed=witnesses[e*6:e*6+6]
            time_matches.append(len(observed)==6 and all(int(a[k])==int(w[k])-target*10000
                for a,(w,b) in zip(observed,eligible) for k in ('start','end')))
        result.update(derived_epoch_exact_content_matches=matches,derived_epoch_exact_time_matches=time_matches,
                      provider_boundary_contract=dict(BOUNDARY_CONTRACT))
        if len(content)!=6*len(transport_targets(mode)) or not all(matches) or not all(time_matches):
            raise ValueError('derived boundary/pending pixels or raw segment timestamps differ from original oracle')
        result.update(derived_transport_diagnostic_accepted=True,derived_content_proven=True,
                      derived_boundary_qualified=mode=='boundary',derived_pending_qualified=mode=='pending',
                      outcome='derived_provider_boundary_only' if mode=='boundary' else 'derived_provider_pending_retirement_only')
        return
    matches=[content[e*6:e*6+6]==oracle[(6 if target else 0):(12 if target else 6)] for e,target in enumerate(transport_targets(mode))]
    result['derived_epoch_exact_content_matches']=matches
    if len(content)!=24 or not all(matches):raise ValueError('derived epoch pixels differ from original target reference')
    result.update(derived_transport_diagnostic_accepted=True,derived_content_proven=True,
                  outcome='derived_source_retained_epoch_content_only')


def load_derived_linear(record,original_witnesses,oracle,fnv):
    binding=record['lav_reference_result'];path=Path(binding['path'])
    if hashlib.sha256(path.read_bytes()).hexdigest()!=binding['sha256']:raise ValueError('derived reference record changed')
    ref=json.loads(path.read_text());q=ref.get('lav_qualification',{});rs=ref.get('stages',{}).get('copy',{})
    if (not ref.get('lav_derived_source') or ref.get('lav_transport')!='reference' or not q.get('derived_reference_ready') or
            not q.get('derived_linear_equivalence') or not q.get('derived_transport_diagnostic_accepted') or
            not q.get('module_closure_verified') or ref.get('media')!=record.get('media') or
            not linear_build_compatible(record,ref) or ref.get('lav_provider')!=record.get('lav_provider') or
            ref.get('lav_original_reference_result')!=record.get('lav_original_reference_result') or
            ref.get('derived_media',{}).get('sha256')!=record['derived_media']['sha256'] or
            ref.get('lav_transport_settings')!=TRANSPORT_SETTINGS or
            ref.get('media_kind')!='derived_stream_copy_matroska' or rs.get('header',{}).get('media_kind')!='derived_stream_copy_matroska' or
            rs.get('header',{}).get('lav_transport')!='reference' or rs.get('lav',{}).get('MP_LAV_DITHER')!=[DITHER_WITNESS] or
            not ref.get('lav_derived_preflight',{}).get('verified') or
            ref.get('lav_derived_preflight',{}).get('derived_media_record_sha256')!=record['derived_media']['sha256'] or
            not rs.get('lav_transport_epoch_accepted') or not rs.get('lav_dither_settings_verified') or
            not ref.get('lav_transport_trace',{}).get('verified') or ref.get('exit_code')!=0 or
            not ref.get('unchanged') or not all(ref['unchanged'].values()) or
            any(ref.get(k) for k in ('outer_timeout','diagnostic_log_limit','diagnostic_log_monitor_error','final_log_errors'))):
        raise ValueError('derived sequential equivalence prerequisite absent or changed')
    rw=rs['lav']['MP_LAV_TRANSPORT_DUMP'];rr=[read_capture(path.parent,w,fnv) for w in rw]
    if (rr!=oracle or [hashlib.sha256(b).hexdigest() for b in rr]!=q.get('derived_capture_sha256') or
            len(rw)!=12 or any(any(a.get(k)!=b.get(k) for k in ('index','start','end')) for a,b in zip(rw,original_witnesses))):
        raise ValueError('derived reference pixels/time labels no longer match original')
    return ref


def linear_build_compatible(record, ref):
    if ref.get('exe')==record.get('exe'):return True
    # Explicit fixture-only compatibility for the reviewed v10 capture/scenario
    # extension; never a generic cross-build exemption or production hash gate.
    expected=dict(policy='v10_capture_and_fixed_scenarios_only',
                  reference_exe_sha256=DERIVED_REFERENCE_EXE,
                  current_exe_sha256=record.get('exe',{}).get('sha256'))
    return (record.get('lav_transport') in ('boundary','pending') and
            record.get('exe',{}).get('sha256')==BOUNDARY_EXE and
            ref.get('exe',{}).get('sha256')==DERIVED_REFERENCE_EXE and
            record.get('lav_linear_reference_compatibility')==expected)


def extended_overlap(witnesses, raw, old_witnesses, old_raw):
    return (len(raw)==15 and [int(w['index']) for w in witnesses]==list(range(6))+list(range(249,258)) and
            raw[:12]==old_raw and all(all(a.get(k)==b.get(k) for k in ('index','start','end'))
                for a,b in zip(witnesses[:12],old_witnesses)) and
            all(int(w['start'])==100000000+i*400000 and int(w['end'])==100400000+i*400000
                for i,w in enumerate(witnesses[6:])))


def finish_extended_original(record,directory,item,result,fnv):
    if (record.get('media_kind')!='original_dat' or record.get('derived_media') or
            item['header'].get('lav_transport')!='reference-extended' or item['header'].get('media_kind')!='original_dat' or
            item.get('lav',{}).get('MP_LAV_DITHER')!=[DITHER_WITNESS] or
            record.get('lav_transport_settings')!=TRANSPORT_SETTINGS or not item.get('lav_dither_settings_verified') or
            not record.get('lav_transport_trace',{}).get('verified')):
        raise ValueError('extended original oracle declaration/settings/trace absent')
    _,old_witnesses,old_raw=load_original_oracle(record,fnv)
    witnesses=item['lav']['MP_LAV_TRANSPORT_DUMP'];raw=[read_capture(directory,w,fnv) for w in witnesses]
    if not extended_overlap(witnesses,raw,old_witnesses,old_raw):
        raise ValueError('extended original oracle changed old overlap or target timing')
    result.update(extended_original_reference_ready=True,extended_original_overlap_exact=True,
                  extended_capture_sha256=[hashlib.sha256(b).hexdigest() for b in raw],
                  original_reference_sha256=record['lav_original_reference_result']['sha256'],
                  transport_settings=dict(TRANSPORT_SETTINGS),outcome='extended_original_sequential_oracle_only')


def load_extended_original(record,old_witnesses,old_raw,fnv):
    binding=record['lav_extended_reference_result'];path=Path(binding['path'])
    if hashlib.sha256(path.read_bytes()).hexdigest()!=binding['sha256']:raise ValueError('extended oracle record changed')
    ref=json.loads(path.read_text());q=ref.get('lav_qualification',{});stage=ref.get('stages',{}).get('copy',{})
    original=record['derived_media']['record']['original']
    if (ref.get('lav_transport')!='reference-extended' or ref.get('media_kind')!='original_dat' or ref.get('derived_media') or
            not q.get('extended_original_reference_ready') or not q.get('extended_original_overlap_exact') or not q.get('module_closure_verified') or
            q.get('transport_settings')!=TRANSPORT_SETTINGS or ref.get('lav_transport_settings')!=TRANSPORT_SETTINGS or
            ref.get('media',{}).get('sha256')!=original['sha256'] or ref.get('media',{}).get('size')!=original['bytes'] or
            ref.get('exe')!=record.get('exe') or ref.get('lav_provider')!=record.get('lav_provider') or
            ref.get('lav_original_reference_result')!=record.get('lav_original_reference_result') or
            stage.get('header',{}).get('media_kind')!='original_dat' or stage.get('header',{}).get('lav_transport')!='reference-extended' or
            not stage.get('lav_transport_epoch_accepted') or not stage.get('lav_dither_settings_verified') or
            stage.get('lav',{}).get('MP_LAV_DITHER')!=[DITHER_WITNESS] or
            not ref.get('lav_transport_trace',{}).get('verified') or ref.get('exit_code')!=0 or
            not ref.get('unchanged') or not all(ref['unchanged'].values()) or
            any(ref.get(k) for k in ('outer_timeout','diagnostic_log_limit','diagnostic_log_monitor_error','final_log_errors'))):
        raise ValueError('extended oracle prerequisite absent or incompatible')
    witnesses=stage['lav']['MP_LAV_TRANSPORT_DUMP'];raw=[read_capture(path.parent,w,fnv) for w in witnesses]
    if (not extended_overlap(witnesses,raw,old_witnesses,old_raw) or
            [hashlib.sha256(b).hexdigest() for b in raw]!=q.get('extended_capture_sha256')):
        raise ValueError('extended original oracle bytes/times changed')
    return witnesses,raw


def load_boundary_result(record,fnv):
    binding=record['lav_boundary_result'];path=Path(binding['path'])
    if hashlib.sha256(path.read_bytes()).hexdigest()!=binding['sha256']:raise ValueError('boundary result changed')
    ref=json.loads(path.read_text());q=ref.get('lav_qualification',{})
    if (ref.get('lav_transport')!='boundary' or not q.get('derived_boundary_qualified') or
            not q.get('derived_transport_diagnostic_accepted') or not q.get('module_closure_verified') or
            any(ref.get(k)!=record.get(k) for k in ('media','exe','lav_provider','lav_original_reference_result',
                'lav_extended_reference_result','lav_reference_result','lav_boundary_contract','lav_linear_reference_compatibility')) or
            not ref.get('lav_transport_trace',{}).get('verified') or not ref.get('stages',{}).get('copy',{}).get('lav_transport_epoch_accepted') or
            ref.get('exit_code')!=0 or not ref.get('unchanged') or not all(ref['unchanged'].values()) or
            any(ref.get(k) for k in ('outer_timeout','diagnostic_log_limit','diagnostic_log_monitor_error','final_log_errors'))):
        raise ValueError('successful matching boundary runtime required before derived pending')
    # Recheck retained byte/time witnesses, not just an old acceptance boolean.
    proof={};finish_derived_transport(ref,path.parent,ref['stages']['copy'],proof,fnv)
    if not proof.get('derived_boundary_qualified'):raise ValueError('boundary runtime failed requalification')
    return ref


def graph_result_binding(record, key):
    binding=record[key];path=Path(binding['path'])
    if hashlib.sha256(path.read_bytes()).hexdigest()!=binding['sha256']:
        raise ValueError(key+' record changed')
    ref=json.loads(path.read_text())
    if (ref.get('exit_code')!=0 or not ref.get('unchanged') or not all(ref['unchanged'].values()) or
            any(ref.get(k) for k in ('outer_timeout','diagnostic_log_limit','diagnostic_log_monitor_error','final_log_errors','validation_error')) or
            not ref.get('lav_qualification',{}).get('module_closure_verified') or
            not ref.get('lav_transport_trace',{}).get('verified') or
            not ref.get('stages',{}).get('copy',{}).get('lav_transport_epoch_accepted') or
            ref.get('lav_transport_settings')!=TRANSPORT_SETTINGS):
        raise ValueError(key+' result is not a completed qualified transport')
    return path,ref


def graph_capture_content(path, ref, fnv, hash_field):
    witnesses=ref['stages']['copy']['lav']['MP_LAV_TRANSPORT_DUMP']
    raw=[read_capture(path.parent,w,fnv) for w in witnesses]
    if [hashlib.sha256(x).hexdigest() for x in raw]!=ref['lav_qualification'].get(hash_field):
        raise ValueError('graph oracle captured bytes changed')
    return witnesses,raw


def graph_oracle_layout(witnesses):
    return (len(witnesses)==31 and [int(w['index']) for w in witnesses]==GRAPH_ORACLE_INDICES and
            [int(w['start']) for w in witnesses]==GRAPH_ORACLE_STARTS and
            all(int(w['end'])==int(w['start'])+400000 for w in witnesses))


def load_graph_old15(record, oldw, oldraw, fnv):
    path,old=graph_result_binding(record,'lav_extended_reference_result')
    if (old.get('lav_transport')!='reference-extended' or old.get('media_kind')!='original_dat' or old.get('derived_media') or
            old.get('exe',{}).get('sha256')!=BOUNDARY_EXE or old.get('lav_provider')!=record.get('lav_provider') or
            old.get('lav_original_reference_result')!=record.get('lav_original_reference_result') or
            not old['lav_qualification'].get('extended_original_reference_ready') or
            not old['lav_qualification'].get('extended_original_overlap_exact')):
        raise ValueError('old15 oracle binding differs')
    ow,orr=graph_capture_content(path,old,fnv,'extended_capture_sha256')
    if not extended_overlap(ow,orr,oldw,oldraw):raise ValueError('old15 overlap is no longer exact')
    return ow,orr


def graph_original_overlap(record, witnesses, raw, fnv):
    _,oldw,oldraw=load_original_oracle(record,fnv)
    ow,orr=load_graph_old15(record,oldw,oldraw,fnv)
    selected=[(w,b) for w,b in zip(witnesses,raw) if int(w['index'])<6 or int(w['index'])>=249]
    if (len(selected)!=15 or [b for w,b in selected]!=orr or
            any(any(a[k]!=b[k] for k in ('index','start','end')) for (a,_),b in zip(selected,ow))):
        raise ValueError('new31 oracle changed retained old15 bytes/time labels')


def graph_reference_exe_matches(record, ref):
    mode=record.get('lav_transport')
    if mode not in ('pending-integer','seek-only-interval','seek-only-recovery'):return ref.get('exe')==record.get('exe')
    compatibility,exe,source=(GRAPH_RECOVERY_COMPATIBILITY,GRAPH_RECOVERY_EXE,GRAPH_RECOVERY_SOURCE) if mode=='seek-only-recovery' else (GRAPH_LOOP_COMPATIBILITY,GRAPH_LOOP_EXE,GRAPH_LOOP_SOURCE) if mode=='seek-only-interval' else (GRAPH_PENDING_COMPATIBILITY,GRAPH_PENDING_EXE,GRAPH_PENDING_SOURCE)
    return (record.get('lav_graph_crossbuild_compatibility')==compatibility and
            ref.get('exe',{}).get('sha256')==GRAPH_REFERENCE_EXE and record.get('exe',{}).get('sha256')==exe and
            record.get('build',{}).get('exe_sha256')==exe and record.get('build',{}).get('source_sha256')==source and
            record.get('build',{}).get('lav_helper_sha256')==GRAPH_PENDING_HELPER)


def load_graph_original(record, fnv):
    path,ref=graph_result_binding(record,'lav_multigop_reference_result')
    original=record['derived_media']['record']['original']
    if (ref.get('lav_transport')!='reference-matrix' or ref.get('media_kind')!='original_dat' or ref.get('derived_media') or
            ref.get('lav_graph_provider') or not graph_reference_exe_matches(record,ref) or ref.get('lav_provider')!=record.get('lav_provider') or
            ref.get('media',{}).get('sha256')!=original['sha256'] or ref.get('media',{}).get('size')!=original['bytes'] or
            ref.get('lav_original_reference_result')!=record.get('lav_original_reference_result') or
            ref.get('lav_extended_reference_result')!=record.get('lav_extended_reference_result') or
            not ref['lav_qualification'].get('graph_original_reference_ready')):
        raise ValueError('original31 oracle provider/media/EXE/prerequisite mismatch')
    w,raw=graph_capture_content(path,ref,fnv,'graph_capture_sha256')
    if not graph_oracle_layout(w):raise ValueError('original31 capture time/ordinal layout differs')
    graph_original_overlap(ref,w,raw,fnv)
    return ref,w,raw


def graph_sample_labels(item):
    return [{k:s[k] for k in ('index','start','end')} for s in item['samples']]


def load_graph_linear(record, original, ow, oracle, fnv):
    path,ref=graph_result_binding(record,'lav_reference_result')
    if (ref.get('lav_transport')!='reference-matrix' or not ref.get('lav_derived_source') or
            ref.get('media_kind')!='derived_stream_copy_matroska' or ref.get('media')!=record.get('media') or
            not graph_reference_exe_matches(record,ref) or ref.get('lav_provider')!=record.get('lav_provider') or
            ref.get('lav_graph_provider')!=record.get('lav_graph_provider') or
            ref.get('lav_multigop_reference_result')!=record.get('lav_multigop_reference_result') or
            ref.get('derived_media',{}).get('sha256')!=record['derived_media']['sha256'] or
            not ref['lav_qualification'].get('graph_derived_reference_ready')):
        raise ValueError('STRICT derived31 reference identity or readiness differs')
    w,raw=graph_capture_content(path,ref,fnv,'graph_capture_sha256')
    if (raw!=oracle or not graph_oracle_layout(w) or any(any(a[k]!=b[k] for k in ('index','start','end')) for a,b in zip(w,ow)) or
            graph_sample_labels(ref['stages']['copy'])!=graph_sample_labels(original['stages']['copy'])):
        raise ValueError('STRICT derived31 reference pixels or all260 sample labels differ')
    return ref


def load_graph_library_matrix(record):
    """Reparse the qualified library controls; graph targets are not invented here."""
    from run_lav_packet_fixture import load_control_binding, analyze
    from owned_lav_provider import verify_strict_record
    from prepare_lav_graph_provider import verify_record as verify_graph
    graph=verify_graph(Path(record['lav_graph_provider']['path']))
    strict_path=Path(graph['inputs']['strict']['record_path'])
    review_path=Path(graph['inputs']['patch_review']['path'])
    strict_provider=verify_strict_record(strict_path,review_path)
    bindings=[]
    for key in ('lav_library_control_result','lav_library_strict_result'):
        binding=record[key];path=Path(binding['path'])
        if hashlib.sha256(path.read_bytes()).hexdigest()!=binding['sha256']:raise ValueError('library matrix result changed')
        bindings.append((path,json.loads(path.read_text())))
    control_path,control=bindings[0];strict_result_path,strict=bindings[1]
    _,matrix,linear=load_control_binding(control_path)
    if matrix.get('targets_ticks')!=[t*10000 for t in GRAPH_MATRIX_TARGETS]:raise ValueError('qualified library target matrix differs')
    if (control['provider_selection']['selected_provider_record_sha256']!=strict_provider['base_control']['record_sha256'] or
            strict.get('provider_selection',{}).get('selected_provider_record_sha256')!=graph['inputs']['strict']['record_sha256'] or
            strict.get('control_binding')!=dict(result_sha256=record['lav_library_control_result']['sha256'],matrix=matrix) or
            strict.get('exit_code')!=0 or strict.get('diagnostic_outcome')!='completed_observation' or not strict.get('measurement_complete') or
            not strict.get('all_targets_exact') or strict.get('compared_frames')!=90 or
            strict.get('matrix')!=matrix or not strict.get('unchanged') or not all(strict['unchanged'].values()) or
            any(strict.get(k) for k in ('outer_timeout','diagnostic_log_limit','diagnostic_log_monitor_error','validation_error'))):
        raise ValueError('qualified library STRICT/control provider or acceptance mismatch')
    for r in (control,strict):
        if (r.get('protected_before',{}).get('media')!=record['media']['sha256'] or
                r.get('protected_before',{}).get('derived_record')!=record['derived_media']['sha256'] or
                r.get('protected_before',{}).get('original')!=record['derived_media']['record']['original']['sha256']):
            raise ValueError('library control media/provenance differs from graph')
    for path,r,provider,expected,oracle in (
        (control_path,control,Path(strict_provider['base_control']['record_path']).parent,None,None),
        (strict_result_path,strict,strict_path.parent,matrix,linear)):
        with (path.parent/'stdout.txt').open() as f:observed=analyze(f,provider,expected,oracle)
        if any(observed[k]!=r.get(k) for k in ('matrix','cases','measurement_complete','all_targets_exact','compared_frames')):
            raise ValueError('library raw matrix observation differs from saved qualification')
    return matrix


def graph_preflight(record, fnv):
    if record.get('lav_transport') not in GRAPH_MODES:raise ValueError('not an integer graph qualification mode')
    if record.get('lav_derived_source'):
        if not record.get('lav_graph_provider'):raise ValueError('derived graph requires explicit STRICT composition')
        original,w,raw=load_graph_original(record,fnv)
        if record['lav_transport'] in ('integer-matrix','pending-integer','seek-only-interval','seek-only-recovery'):
            load_graph_linear(record,original,w,raw,fnv)
            if record['lav_transport']=='integer-matrix':load_graph_library_matrix(record)
            else:
                load_graph_matrix(record,fnv)
                if record['lav_transport'] in ('seek-only-interval','seek-only-recovery'):load_graph_pending(record,fnv)
                if record['lav_transport']=='seek-only-recovery':load_cold_interval(record)
    else:
        if record.get('lav_transport')!='reference-matrix' or record.get('lav_graph_provider'):raise ValueError('original graph mode requires official unseeked oracle')
        # Validate old15 independently before launching the new oracle.
        _,w,raw=load_original_oracle(record,fnv)
        load_graph_old15(record,w,raw,fnv)
    return dict(verified=True,mode=record['lav_transport'],targets_ticks=[t*10000 for t in transport_targets(record['lav_transport'])] if record['lav_transport'] in ('integer-matrix','pending-integer','seek-only-interval','seek-only-recovery') else [],timestamp_normalization=False)


def finish_graph_transport(record,directory,item,result,fnv):
    mode=record['lav_transport']
    if (record.get('lav_graph_preflight')!=dict(verified=True,mode=mode,targets_ticks=[t*10000 for t in transport_targets(mode)] if mode in ('integer-matrix','pending-integer','seek-only-interval','seek-only-recovery') else [],timestamp_normalization=False) or not record.get('lav_transport_trace',{}).get('verified') or
            record.get('lav_transport_settings')!=TRANSPORT_SETTINGS or not item.get('lav_dither_settings_verified')):
        raise ValueError('graph matrix preflight/settings/segment proof absent')
    w=item['lav']['MP_LAV_TRANSPORT_DUMP'];raw=[read_capture(directory,x,fnv) for x in w]
    result.update(graph_capture_sha256=[hashlib.sha256(b).hexdigest() for b in raw],graph_mode=mode,
                  original_game_playback_proven=False,native_windows_qualified=False,live_newsegment_observed=True)
    if mode=='reference-matrix' and not record.get('lav_derived_source'):
        if not graph_oracle_layout(w):raise ValueError('original31 capture layout differs')
        graph_original_overlap(record,w,raw,fnv)
        result.update(graph_original_reference_ready=True,graph_original_overlap_exact=True,outcome='official_original31_oracle_only');return
    if not record.get('lav_derived_source') or not record.get('lav_graph_provider'):raise ValueError('STRICT derived graph source required')
    derivation=record.get('derived_media',{});proof=record.get('lav_derived_preflight',{})
    if (record.get('media_kind')!='derived_stream_copy_matroska' or not proof.get('verified') or
            proof.get('timestamp_adjustment') is not False or proof.get('derived_media_record_sha256')!=derivation.get('sha256') or
            proof.get('original_reference_sha256')!=record.get('lav_original_reference_result',{}).get('sha256') or
            derivation.get('record',{}).get('schema')!=2 or derivation.get('timeline_semantics')!='generated_timestamps' or
            record.get('media',{}).get('sha256')!=derivation.get('record',{}).get('derived',{}).get('sha256')):
        raise ValueError('graph derived source schema2/preflight binding differs')
    original,ow,oracle=load_graph_original(record,fnv)
    if mode=='reference-matrix':
        same_labels=graph_sample_labels(item)==graph_sample_labels(original['stages']['copy'])
        if (raw!=oracle or not graph_oracle_layout(w) or not same_labels or
                any(any(a[k]!=b[k] for k in ('index','start','end')) for a,b in zip(w,ow))):
            raise ValueError('STRICT derived31 RGB or260 sequential labels differ from original')
        result.update(graph_derived_reference_ready=True,graph_linear_exact_rgb=True,graph_linear_exact260_labels=True,outcome='STRICT_derived31_linear_equivalence_only');return
    load_graph_linear(record,original,ow,oracle,fnv)
    if mode in ('seek-only-interval','seek-only-recovery'):
        load_graph_matrix(record,fnv);load_graph_pending(record,fnv)
        if mode=='seek-only-recovery':load_cold_interval(record)
        finish_loop_content(item,result,w,raw,ow,oracle);return
    if mode=='pending-integer':
        load_graph_matrix(record,fnv);matrix={'targets_ticks':[0,100000000]}
    else:matrix=load_graph_library_matrix(record)
    pixels=[];times=[]
    for e,target in enumerate(matrix['targets_ticks']):
        eligible=[(x,b) for x,b in zip(ow,oracle) if int(x['start'])>=target][:6]
        if len(eligible)!=6:raise ValueError('original31 lacks six target successors')
        pixels.append(raw[e*6:e*6+6]==[b for x,b in eligible])
        observed=w[e*6:e*6+6]
        times.append(len(observed)==6 and all(int(a[k])==int(x[k])-target for a,(x,b) in zip(observed,eligible) for k in ('start','end')))
    result.update(graph_epoch_exact_rgb=pixels,graph_epoch_exact_raw_times=times,graph_targets_ticks=matrix['targets_ticks'])
    expected_count=12 if mode=='pending-integer' else 90
    if len(raw)!=expected_count or not all(pixels) or not all(times):raise ValueError('integer graph target RGB or raw time differs; no normalization permitted')
    if mode=='pending-integer':
        result.update(graph_pending_integer_accepted=True,graph_compared_frames=12,pending_transition_observed=True,
                      pending_retirement=item['lav']['MP_LAV_CANCEL'][0]['retirement'],
                      pending_cancellation_race_proven=False,outcome='STRICT_derived_pending_integer_only')
    else:result.update(graph_integer_matrix_accepted=True,graph_compared_frames=90,outcome='STRICT_derived_integer15_graph_only')


def load_graph_matrix(record, fnv):
    """Gate P admits only the preserved accepted integer15 graph and raw pixels."""
    path,ref=graph_result_binding(record,'lav_integer_matrix_result')
    if (record.get('lav_transport') not in ('pending-integer','seek-only-interval','seek-only-recovery') or not graph_reference_exe_matches(record,ref) or
            ref.get('lav_transport')!='integer-matrix' or not ref.get('lav_derived_source') or
            any(ref.get(k)!=record.get(k) for k in ('lav_provider','lav_graph_provider','media','lav_multigop_reference_result','lav_reference_result','lav_original_reference_result','lav_extended_reference_result')) or
            ref.get('derived_media',{}).get('sha256')!=record['derived_media']['sha256'] or
            not ref['lav_qualification'].get('graph_integer_matrix_accepted') or ref['lav_qualification'].get('graph_compared_frames')!=90):
        raise ValueError('accepted integer15 graph provider/media/EXE/reference prerequisite differs')
    # Recheck raw observations and exact existing90 images without executing anything.
    from run_media_playback_fixture import validate
    with (path.parent/'stdout.txt').open() as f:stage=validate(f)['stages']['copy']
    trace=trace_transport(path.parent/'stderr.txt',stage)
    if not stage.get('lav_transport_epoch_accepted') or not trace.get('verified'):
        raise ValueError('retained integer15 graph raw protocol/trace differs')
    candidate=dict(ref,stages={'copy':stage},lav_transport_trace=trace)
    finish(candidate,path.parent,True,fnv)
    proof=candidate['lav_qualification']
    if (not proof.get('graph_integer_matrix_accepted') or
            proof.get('graph_capture_sha256')!=ref['lav_qualification'].get('graph_capture_sha256')):
        raise ValueError('retained integer15 graph exact pixels differ')
    return ref


def loop_mode(item):
    return item.get('header',{}).get('lav_transport') in ('seek-only-interval','seek-only-recovery')


def loop_clock_decision(bits, position_hr, times_hr):
    import math,struct
    seconds=struct.unpack('<d',int(bits,16).to_bytes(8,'little'))[0]
    scaled=seconds*1000
    converted=position_hr=='00000000' and math.isfinite(scaled) and -2147483648<=scaled<2147483648
    milliseconds=math.trunc(scaled) if converted else 0
    return seconds,converted,milliseconds,bool(converted and times_hr=='00000000' and milliseconds>10320)


def consume_loop(item,kind,row):
    if not kind.startswith('MP_LAV_LOOP_'):return False
    if not loop_mode(item):raise ValueError('loop witness outside explicit interval mode')
    data=item.setdefault('lav',{});calls=item['returned_calls'];epochs=data.get('epochs',[])
    if item['pending'] or not calls or not epochs:raise ValueError('unpaired loop witness')
    epoch=epochs[-1];e=len(epochs)-1
    if 'epoch' in row and row['epoch']!=str(e):raise ValueError('loop witness epoch differs')
    expected={'MP_LAV_LOOP_NEGATIVE':'lav_loop_negative_update','MP_LAV_LOOP_NEGATIVE_RETIRED':'lav_loop_negative_retired',
              'MP_LAV_LOOP_INVALIDATE':'lav_integer_position','MP_LAV_LOOP_ADAPTER_END':'lav_loop_run_internal',
              'MP_LAV_LOOP_CALLER_RESTORED':'lav_loop_run_internal','MP_LAV_LOOP_DECISION':'lav_loop_observe','MP_LAV_LOOP_FINISH':'lav_loop_end_pause'}
    if kind in expected and calls[-1]['name']!=expected[kind]:raise ValueError('loop witness not paired to expected call')
    if kind=='MP_LAV_LOOP_ADAPTER_BEGIN' and len(calls)!=epoch['call_start']:raise ValueError('adapter began after transport calls')
    if kind not in expected and kind not in ('MP_LAV_LOOP_ADAPTER_BEGIN','MP_LAV_LOOP_OUTCOME'):raise ValueError('unknown loop witness')
    if kind=='MP_LAV_LOOP_DECISION':
        import math,struct
        c=calls[-1]
        if (not item['ready'] or item['waiting'] or item.get('loop_observation_consumed') or c['hr']!='00000000' or c.get('attempt',1)!=1 or
                row.get('pair_seq')!=str(c.get('seq')) or row.get('telemetry')!='deferred_composite' or
                row.get('index')!=str(len(item['frames'])) or row.get('local')!=str(len(item['frames'])-epoch['frame_start']) or
                row.get('state')!='4' or row.get('content_epoch')!=str(e) or row.get('bound_ms')!='10320'):
            raise ValueError('invalid fresh loop observation')
        try:
            a,b,d=(int(row[k]) for k in ('qpc_before_times','qpc_between_calls','qpc_after_position'))
            if not 0<int(c['qpc_enter'])<=a<b<d<=int(c['qpc_leave']) or int(row['qpc_frequency'])<=0 or row['qpc_frequency']!=item['header'].get('qpc_frequency'):
                raise ValueError('invalid loop QPC order/frequency')
            if any(len(row[k])!=8 or any(x not in '0123456789abcdef' for x in row[k]) for k in ('times_hr','position_hr')) or any(not 0<=int(row[k])<=0xffffffff for k in ('times_last_error','position_last_error')):
                raise ValueError('invalid loop raw HRESULT/LastError')
            if len(row['position_bits'])!=16:raise ValueError('invalid loop double bits')
            seconds,valid,ms,crossing=loop_clock_decision(row['position_bits'],row['position_hr'],row['times_hr'])
            printed=float(row['position_seconds'])
            if not (math.isnan(seconds) and math.isnan(printed)) and struct.pack('<d',seconds)!=struct.pack('<d',printed):raise ValueError('loop decimal differs from raw double bits')
            if row.get('converted')!=str(int(valid)) or row.get('current_ms')!=str(ms) or row.get('crossing')!=str(int(crossing)):
                raise ValueError('loop clock conversion or strict-greater decision differs')
        except (KeyError,TypeError,OverflowError) as error:raise ValueError('missing loop observation') from error
        predecessor=calls[-2] if len(calls)>1 else {}
        wanted=('lav_transport_state',) if row['local']=='0' else ('update','completion_status')
        if predecessor.get('name') not in wanted or predecessor.get('hr')!='00000000':raise ValueError('loop decision not immediately after completed sample/state observation')
        item['loop_observation_consumed']=True
        item['stamp_ready']=bool(valid and row['times_hr']=='00000000' and not crossing)
        if crossing:item['ready']=False
    if kind=='MP_LAV_LOOP_NEGATIVE' and row.get('update_hr')!=calls[-1]['hr']:raise ValueError('negative Update witness differs')
    if kind=='MP_LAV_LOOP_NEGATIVE_RETIRED':
        if len(calls)<2 or calls[-2]['name']!='lav_loop_negative_abort' or row.get('abort_hr')!=calls[-2]['hr'] or row.get('settled_hr')!=calls[-1]['hr']:
            raise ValueError('negative control retirement differs')
    data.setdefault(kind,[]).append(dict(row,call_index=len(calls)-1))
    return True


def check_loop_epochs(item):
    data=item['lav'];epochs=data['epochs'];calls=item['returned_calls'];names=[c['name'] for c in calls]
    recovery=item['header'].get('lav_transport')=='seek-only-recovery'
    if item['header'].get('lav_seek_api')!='IMediaSeeking_integer' or len(epochs)!=3:return False
    forbidden=('seek','control_run','second_pause','lav_transport_seek','lav_transport_position','lav_pending_pair','lav_transport_pending_update','lav_transport_pending_status')
    if any(n in names for n in forbidden) or data.get('MP_LAV_LOOP_OUTCOME'):return False
    if recovery:
        if len(data.get('MP_LAV_RECOVERY_SCOPE',[]))!=1 or data.get('MP_LAV_RECOVERY_INITIAL',[])!=[dict(epoch='0',admitted='0',initial_miss_observed='1',initial_interval_content_qualified='0')]:return False
        if data.get('MP_LAV_RECOVERY_OUTCOME',[])!=[dict(reason='cold_miss_recovery_qualified',epoch='2')]:return False
    if any(int(x['code'])==1 for x in data.get('MP_LAV_EVENT',[])):return False # EC_COMPLETE is not a positive interval crossing
    alloc=data['MP_LAV_ALLOCATOR'][0]['allocator'];identity=None
    states=data.get('MP_LAV_TRANSPORT_STATE',[])
    state_sites=['stopped','running','stopped','running','stopped','running'] if recovery else ['stopped','negative_stopped','running','stopped','running','stopped','running']
    if [x['site'] for x in states]!=state_sites or any(x['hr']!='00000000' or x['state']!=('2' if x['site']=='running' else '0') for x in states):return False
    positions=data.get('MP_LAV_INTEGER_POSITION',[])
    if len(positions)!=3 or any(any(x.get(k)!=v for k,v in dict(requested_ticks='100000000',actual_ticks='100000000',current_flags='1',stop_flags='0',stop_null='1').items()) for x in positions):return False
    for e,ep in enumerate(epochs):
        begin,ready,end=ep['begin'],ep.get('ready',{}),ep.get('end',{})
        count=ep.get('frame_end',0)-ep['frame_start']
        if ((count!=0 if recovery and e==0 else not 1<=count<=9) or ep.get('sample_end',0)-ep['sample_start']!=count or begin.get('target_ms')!='10000' or begin.get('frames')!='9' or begin.get('reference')!='0'):return False
        current=(ready.get('sample'),ready.get('surface'))
        try:
            if any(not 0<int(x,16)<=0xffffffff for x in current):return False
        except (TypeError,ValueError):return False
        if identity and current!=identity:return False
        identity=current
        if ready.get('allocator')!=alloc or (end.get('sample'),end.get('surface'))!=identity:return False
        if e and (begin.get('sample'),begin.get('surface'))!=identity:return False
        if not e and (int(begin['sample'],16) or int(begin['surface'],16)):return False
        ec=calls[ep['call_start']:ep['call_end']]
        effective=[c['name'] for j,c in enumerate(ec) if c['name'] in TRANSPORT_ARGS and not recovered_transport_attempt(ec,j)]
        prefix=['lav_transport_pause','lav_transport_decommit','lav_transport_stop','lav_transport_state']
        if e:prefix+=['lav_transport_abort','lav_transport_retired']
        prefix+=['lav_integer_seek','lav_integer_position']
        if not e:
            prefix+=['lav_transport_create_sample','lav_transport_get_surface','lav_transport_surface_desc']
            if not recovery:prefix+=['lav_transport_state','lav_loop_negative_update','lav_loop_negative_abort','lav_loop_negative_retired']
            prefix+=['lav_transport_run']
        else:prefix+=['lav_loop_run_internal']
        prefix+=['lav_transport_state']
        # Exact transport prefix; every later observed call is paired to a completed sample/copy or the one crossing.
        if effective[:len(prefix)]!=prefix:return False
        suffix=effective[len(prefix):]
        expected_suffix=[]
        for j in range(count):expected_suffix+=['lav_loop_observe','lav_transport_dump_create','lav_transport_dump_write','lav_transport_dump_close']
        expected_suffix+=['lav_loop_observe','lav_loop_end_pause']
        if suffix!=expected_suffix:return False
        decisions=[x for x in data.get('MP_LAV_LOOP_DECISION',[]) if x.get('epoch')==str(e)]
        finishes=[x for x in data.get('MP_LAV_LOOP_FINISH',[]) if x.get('epoch')==str(e)]
        if len(decisions)!=count+1 or len(finishes)!=1:return False
        if [x['crossing'] for x in decisions]!=['0']*count+['1']:return False
        if any(x['times_hr']!='00000000' or x['position_hr']!='00000000' or x['converted']!='1' or (x.get('sample'),x.get('surface'))!=identity or x.get('allocator')!=alloc for x in decisions):return False
        samples=item['samples'][ep['sample_start']:ep['sample_end']]
        if any(any(d[k]!=s[k] for k in ('index','start','end','current')) for d,s in zip(decisions,samples)):return False
        if any(int(s['start'])!=i*400000 or int(s['end'])!=(i+1)*400000 for i,s in enumerate(samples)):return False
        dumps=[x for x in data.get('MP_LAV_TRANSPORT_DUMP',[]) if ep['frame_start']<=int(x['index'])<ep['frame_end']]
        if len(dumps)!=count:return False
        f=finishes[0]
        if any(f.get(k)!=v for k,v in dict(admitted=str(count),reason='positive_interval',state='4',content_epoch=str(e),end_ms='10320',sample=identity[0],surface=identity[1]).items()):return False
        if not decisions[-1]['call_index']<f['call_index']<ep['call_end']:return False
        if e:
            for kind in ('MP_LAV_LOOP_ADAPTER_BEGIN','MP_LAV_LOOP_INVALIDATE','MP_LAV_LOOP_ADAPTER_END','MP_LAV_LOOP_CALLER_RESTORED'):
                if len([x for x in data.get(kind,[]) if x.get('epoch')==str(e)])!=1:return False
            b=next(x for x in data['MP_LAV_LOOP_ADAPTER_BEGIN'] if x['epoch']==str(e))
            inv=next(x for x in data['MP_LAV_LOOP_INVALIDATE'] if x['epoch']==str(e))
            a=next(x for x in data['MP_LAV_LOOP_ADAPTER_END'] if x['epoch']==str(e))
            c=next(x for x in data['MP_LAV_LOOP_CALLER_RESTORED'] if x['epoch']==str(e))
            if any(b.get(k)!=v for k,v in dict(state='4',content_epoch=str(e-1),active='1',playing='1',looping='1',positive_interval='1',sample=identity[0],surface=identity[1],allocator=alloc).items()):return False
            if any(inv.get(k)!=v for k,v in dict(state_before='4',state_after='1',content_epoch='-1',retired='1',sample=identity[0]).items()):return False
            if any(a.get(k)!=v for k,v in dict(state='1',content_epoch=str(e),active='1',playing='1',looping='1',internal_run='1',sample=identity[0],surface=identity[1],allocator=alloc).items()):return False
            if any(c.get(k)!=v for k,v in dict(end_ms='10320',active='1',playing='1',looping='1',caller_run='0').items()):return False
            if not b['call_index']<inv['call_index']<a['call_index']==c['call_index']==ep['ready_call']:return False
    for kind in ('MP_LAV_LOOP_ADAPTER_BEGIN','MP_LAV_LOOP_INVALIDATE','MP_LAV_LOOP_ADAPTER_END','MP_LAV_LOOP_CALLER_RESTORED'):
        if len(data.get(kind,[]))!=2:return False
    n=data.get('MP_LAV_LOOP_NEGATIVE',[]);ret=data.get('MP_LAV_LOOP_NEGATIVE_RETIRED',[])
    if recovery:
        if n or ret or any(n.startswith('lav_loop_negative_') for n in names):return False
    else:
        if len(n)!=1 or len(ret)!=1:return False
        if any(n[0].get(k)!=v for k,v in dict(update_hr='8004040a',state='0',run_calls='0',copied='0',sample=identity[0],surface=identity[1],allocator=alloc).items()):return False
        if ret[0].get('abort_hr') not in RETIRED or ret[0].get('settled_hr') not in RETIRED or ret[0].get('discarded')!='1' or ret[0].get('sample')!=identity[0]:return False
    cancels=data.get('MP_LAV_CANCEL',[])
    if len(cancels)!=2 or any(x['abort_hr'] not in RETIRED or x['settled_hr'] not in RETIRED or x['discarded']!='1' or x['sample']!=identity[0] for x in cancels):return False
    decommits=data.get('MP_LAV_TRANSPORT_DECOMMIT',[])
    if [x['cleanup'] for x in decommits]!=['0','0','0','1'] or any(x['allocator']!=alloc or x['manual_commit']!='0' for x in decommits):return False
    meta=data.get('MP_COPY_META',[]);surfaces=data.get('MP_SURFACE',[])
    return len(surfaces)==1 and surfaces[0]['bits']=='32' and len(meta)==len(item['frames']) and all(x.get('bits')=='32' and x.get('supported')=='1' and x.get('frame')==str(i) for i,x in enumerate(meta))


def load_graph_pending(record,fnv):
    path,ref=graph_result_binding(record,'lav_pending_result')
    if (record.get('lav_transport') not in ('seek-only-interval','seek-only-recovery') or record['lav_pending_result']['sha256']!=GRAPH_LOOP_PENDING_RESULT or
            not graph_reference_exe_matches(record,{'exe':{'sha256':GRAPH_REFERENCE_EXE}}) or
            ref.get('lav_transport')!='pending-integer' or ref.get('exe',{}).get('sha256')!=GRAPH_PENDING_EXE or
            ref.get('lav_graph_crossbuild_compatibility')!=GRAPH_PENDING_COMPATIBILITY or
            not graph_reference_exe_matches(ref,{'exe':{'sha256':GRAPH_REFERENCE_EXE}}) or
            any(ref.get(k)!=record.get(k) for k in ('lav_provider','lav_graph_provider','media','lav_multigop_reference_result','lav_reference_result','lav_original_reference_result','lav_extended_reference_result','lav_integer_matrix_result')) or
            ref.get('derived_media',{}).get('sha256')!=record['derived_media']['sha256'] or
            not ref['lav_qualification'].get('graph_pending_integer_accepted')):
        raise ValueError('accepted composite Gate P prerequisite differs')
    from run_media_playback_fixture import validate
    with (path.parent/'stdout.txt').open() as f:stage=validate(f)['stages']['copy']
    trace=trace_transport(path.parent/'stderr.txt',stage)
    candidate=dict(ref,stages={'copy':stage},lav_transport_trace=trace)
    finish(candidate,path.parent,True,fnv)
    proof=candidate['lav_qualification']
    if not proof.get('graph_pending_integer_accepted') or proof.get('graph_capture_sha256')!=ref['lav_qualification'].get('graph_capture_sha256'):
        raise ValueError('retained composite Gate P raw protocol/trace/pixels differ')
    return ref


def finish_loop_content(item,result,w,raw,ow,oracle):
    recovery=item['header'].get('lav_transport')=='seek-only-recovery'
    selected=[(x,b) for x,b in zip(ow,oracle) if 100000000<=int(x['start'])<=103200000]
    if len(selected)!=9:raise ValueError('positive interval original9 oracle absent')
    pixels=[];times=[];counts=[];cursor=0
    for ep in item['lav']['epochs']:
        count=ep['frame_end']-ep['frame_start'];counts.append(count)
        observed=w[cursor:cursor+count]
        pixels.append(raw[cursor:cursor+count]==[b for x,b in selected[:count]])
        times.append(len(observed)==count and all(int(a[k])==int(x[k])-100000000 for a,(x,b) in zip(observed,selected[:count]) for k in ('start','end')))
        cursor+=count
    result.update(graph_interval_admitted_counts=counts,graph_epoch_exact_rgb=pixels,graph_epoch_exact_raw_times=times,
                  graph_targets_ticks=[100000000]*3,graph_compared_frames=len(raw),caller_Run_count=0,adapter_internal_Run_count=2,
                  positive_interval_bound_ms=10320,clock_offset_ms=0,
                  pending_cancellation_race_proven=False,production_loop_adapter_proven=False)
    if recovery:result.update(minimum_fresh_frames_per_restart=1,initial_required_frames=0)
    else:result['minimum_fresh_frames_per_interval']=1
    if len(counts)!=3 or (counts[0]!=0 or any(not 1<=n<=9 for n in counts[1:]) if recovery else any(not 1<=n<=9 for n in counts)) or len(raw)!=cursor or len(w)!=cursor or not all(pixels) or not all(times):
        raise ValueError('positive interval admitted prefix RGB/raw time differs; no exclusions or normalization allowed')
    if recovery:
        result.update(graph_cold_miss_recovery_accepted=True,automatic_recovery_after_observed_zero_frame_interval=True,initial_miss_observed=True,initial_interval_content_qualified=False,cold_start_content_qualified=False,graph_seek_only_interval_accepted=False,coverage_complete=True,outcome='STRICT_derived_cold_miss_recovery_fixture_only')
    else:result.update(graph_seek_only_interval_accepted=True,coverage_complete=True,outcome='STRICT_derived_seek_only_positive_interval_fixture_only')


def loop_observed_prefix(item,w,raw,ow,oracle):
    selected=[(x,b) for x,b in zip(ow,oracle) if 100000000<=int(x['start'])<=103200000]
    if len(selected)!=9 or len(raw)!=len(w) or len(raw)!=len(item.get('frames',[])):return False
    cursor=0
    epochs=item.get('lav',{}).get('epochs',[])
    for index,ep in enumerate(epochs):
        end=ep.get('frame_end',len(item['frames']))
        count=end-ep['frame_start']
        if not 0<=count<=9:return False
        if raw[cursor:cursor+count]!=[b for x,b in selected[:count]]:return False
        if any(int(a[k])!=int(x[k])-100000000 for a,(x,b) in zip(w[cursor:cursor+count],selected[:count]) for k in ('start','end')):return False
        cursor+=count
    return cursor==len(raw)


def consume_recovery(item,kind,row):
    if not kind.startswith('MP_LAV_RECOVERY_'):return False
    if item['header'].get('lav_transport')!='seek-only-recovery' or item['pending']:
        raise ValueError('recovery witness outside explicit completed-call scope')
    data=item.setdefault('lav',{});epochs=data.get('epochs',[]);calls=item['returned_calls']
    if data.get(kind):raise ValueError('duplicate recovery witness')
    if kind=='MP_LAV_RECOVERY_SCOPE':
        if epochs or item['frames'] or row!=dict(mode='seek-only-recovery',initial_required='zero',restart_required='2'):
            raise ValueError('invalid recovery scope')
    elif kind=='MP_LAV_RECOVERY_INITIAL':
        finishes=data.get('MP_LAV_LOOP_FINISH',[])
        if (len(epochs)!=1 or len(finishes)!=1 or finishes[0]['epoch']!='0' or not calls or calls[-1]['name']!='lav_loop_end_pause' or
                row!=dict(epoch='0',admitted=finishes[0]['admitted'],initial_miss_observed=str(int(finishes[0]['admitted']=='0')),initial_interval_content_qualified='0')):
            raise ValueError('initial recovery witness differs from actual first finish')
    elif kind=='MP_LAV_RECOVERY_OUTCOME':
        if not epochs or not calls or set(row)!={'reason','epoch'} or row['epoch']!=str(len(epochs)-1):raise ValueError('invalid terminal recovery scope')
        reason=row['reason'];e=len(epochs)-1
        finishes=[x for x in data.get('MP_LAV_LOOP_FINISH',[]) if x['epoch']==str(e)]
        if reason=='cold_miss_recovery_qualified':
            if len(epochs)!=3 or 'end' not in epochs[-1] or calls[-1]['name']!='lav_event_poll':raise ValueError('recovery claimed before all epochs ended')
        elif reason in ('initial_miss_not_observed','restart_positive_end_before_first_frame'):
            if len(finishes)!=1 or calls[-1]['name']!='lav_loop_end_pause':raise ValueError('coverage reason lacks actual end crossing')
            if reason=='initial_miss_not_observed' and (e!=0 or int(finishes[0]['admitted'])<1):raise ValueError('initial miss absence differs')
            if reason=='restart_positive_end_before_first_frame' and (e not in (1,2) or finishes[0]['admitted']!='0'):raise ValueError('restart zero coverage differs')
        elif reason=='loop_oracle_window_exhausted':
            decisions=data.get('MP_LAV_LOOP_DECISION',[])
            if not decisions or decisions[-1]['epoch']!=str(e) or decisions[-1]['local']!='9' or decisions[-1]['crossing']!='0' or calls[-1]['name']!='lav_loop_observe':raise ValueError('oracle limit lacks ninth noncrossing decision')
        elif reason!='recovery_contract_failure':raise ValueError('unknown recovery outcome')
    else:raise ValueError('unknown recovery witness')
    data[kind]=[dict(row)];return True


def load_cold_interval(record):
    """Reuse only the exact preserved negative control; no failed-result wildcard."""
    binding=record['lav_cold_interval_result'];path=Path(binding['path'])
    if record.get('lav_transport')!='seek-only-recovery' or binding['sha256']!=COLD_INTERVAL_RESULT or hashlib.sha256(path.read_bytes()).hexdigest()!=COLD_INTERVAL_RESULT:
        raise ValueError('cold interval result is not the preserved control')
    ref=json.loads(path.read_text())
    for name,sha in COLD_INTERVAL_RAW.items():
        if hashlib.sha256((path.parent/name).read_bytes()).hexdigest()!=sha:raise ValueError('cold interval raw control changed')
    if (ref.get('lav_transport')!='seek-only-interval' or ref.get('exit_code')!=2 or ref.get('diagnostic_outcome')!='completed_observation' or
            not ref.get('unchanged') or not all(ref['unchanged'].values()) or
            any(ref.get(k) for k in ('validation_error','outer_timeout','diagnostic_log_limit','diagnostic_log_monitor_error')) or
            not graph_reference_exe_matches(ref,{'exe':{'sha256':GRAPH_REFERENCE_EXE}}) or
            not graph_reference_exe_matches(record,{'exe':{'sha256':GRAPH_REFERENCE_EXE}}) or
            any(ref.get(k)!=record.get(k) for k in ('lav_provider','lav_graph_provider','media','lav_multigop_reference_result','lav_reference_result','lav_original_reference_result','lav_extended_reference_result','lav_integer_matrix_result','lav_pending_result','lav_transport_settings')) or
            ref.get('derived_media',{}).get('sha256')!=record['derived_media']['sha256'] or
            ref.get('lav_qualification',{}).get('coverage_reason')!='positive_end_before_first_frame' or
            not ref['lav_qualification'].get('module_closure_verified') or ref['lav_qualification'].get('graph_seek_only_interval_accepted')):
        raise ValueError('cold interval provider/media/EXE/prerequisite mismatch')
    exe=Path(ref['exe']['path'])
    if hashlib.sha256(exe.read_bytes()).hexdigest()!=GRAPH_LOOP_EXE or json.loads(exe.with_suffix('.build.json').read_text())!=ref['build']:
        raise ValueError('cold interval frozen EXE/build changed')
    from run_media_playback_fixture import validate
    with (path.parent/'stdout.txt').open() as f:stage=validate(f)['stages']['copy']
    data=stage['lav'];negative=data.get('MP_LAV_LOOP_NEGATIVE',[]);retired=data.get('MP_LAV_LOOP_NEGATIVE_RETIRED',[]);decisions=data.get('MP_LAV_LOOP_DECISION',[])
    if (stage['chain_valid'] is not True or stage['frames'] or stage['samples'] or stage['failures'] or stage['cleanup_errors'] or stage['timeout'] or
            len(data['epochs'])!=1 or len(negative)!=1 or len(retired)!=1 or len(decisions)!=1 or
            any(negative[0].get(k)!=v for k,v in dict(update_hr='8004040a',state='0',run_calls='0',copied='0').items()) or
            any(retired[0].get(k)!=v for k,v in dict(abort_hr='00000000',settled_hr='00000000',discarded='1').items()) or
            decisions[0].get('epoch')!='0' or decisions[0].get('local')!='0' or decisions[0].get('crossing')!='1' or
            data.get('MP_LAV_LOOP_ADAPTER_BEGIN') or data.get('MP_LAV_TRANSPORT_DUMP') or
            data.get('MP_LAV_LOOP_OUTCOME',[{}])[0].get('reason')!='positive_end_before_first_frame'):
        raise ValueError('retained cold control actual negative/retirement/zero crossing differs')
    trace=trace_transport(path.parent/'stderr.txt',stage)
    if trace!=ref['lav_transport_trace'] or [x['start_100ns'] for x in trace['segments']]!=[0,100000000] or [x['receives'] for x in trace['segments']]!=[0,1]:
        raise ValueError('retained cold control trace differs')
    return dict(verified=True,result_sha256=COLD_INTERVAL_RESULT,raw_sha256=dict(COLD_INTERVAL_RAW),negative_update_hr='8004040a',terminal_retirement_hr=['00000000','00000000'],copied_frames=0,automatic_restarts=0,reused_control_only=True)
