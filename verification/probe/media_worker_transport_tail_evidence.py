"""Actual two-graph EOF suffix check of the shared production transport."""
import hashlib
import json
from pathlib import Path
import media_worker_clock_evidence as common
import media_worker_sample_evidence as old_worker
import media_lav_evidence as old_lav
from run_media_playback_fixture import digest, fnv64

KIND=common.EOF_KIND
require=common.require
TARGET=old_worker.EOF_TARGET


def references(record):
    # Reuse the unchanged cohort and original/derived source binding checks;
    # runtime source paths may differ only through the driver's byte binding.
    common.references(record)
    ref_path,ref=old_lav.graph_result_binding(record,'eof_reference')
    require(record['eof_reference']['sha256']==old_worker.EOF_REFERENCE_SHA and ref['exe']['sha256']==old_worker.EOF_REFERENCE_EXE,'frozen suffix reference identity differs')
    require(ref['lav_qualification'].get('eof_reference_accepted') and ref['lav_eof_preflight'].get('event_setup_protocol')==2,'suffix reference protocol unqualified')
    require(ref['lav_provider']==record['lav_provider'] and ref['build']['lav_helper_sha256']==old_worker.HELPER,'suffix provider/helper provenance differs')
    mapping_path=Path(ref['lav_tail_reference_result']['path']);mapping_sha=digest(mapping_path)
    require(mapping_sha==ref['lav_tail_reference_result']['sha256']==ref['lav_eof_preflight']['mapping_sha256']=='50d390ce417ccf8ac7db68dcefff7f33f405a64b0e4cc1cbf52ea52219e1c106','suffix packet mapping changed')
    mapping=json.loads(mapping_path.read_text());frames=ref['lav_qualification']['eof_reference_captures']
    require(mapping['measurement_complete'] and not mapping['errors'] and mapping['artifact']['sha256']==ref['media']['sha256'] and len(frames)==16 and [r['index'] for r in frames]==list(range(16)),'suffix mapping incomplete')
    expected={}
    for ordinal,frame in enumerate(frames[10:16]):
        path=ref_path.parent/f'transport-e0-f{frame["index"]}.bgra'
        require(path.is_file() and not path.is_symlink() and path.stat().st_size==common.FRAME_BYTES,'suffix capture missing')
        raw=path.read_bytes();require(hashlib.sha256(raw).hexdigest()==frame['sha256'] and fnv64(raw)==frame['fnv64'],'suffix capture changed')
        start,end=frame['mapped_absolute_start_100ns'],frame['mapped_absolute_end_100ns']
        require((start,end)==(TARGET+ordinal*400000,TARGET+(ordinal+1)*400000) and mapping['mapping']['frames'][frame['index']]['derived_pts']*10000==start,'suffix exact interval differs')
        expected[(start,end)]=dict(rgb_sha256=old_worker.rgb_digest(raw),raw=raw)
    return expected


def selections(rows):
    require(not rows.get('MC_CLOCK'),'EOF-only mode fabricated a clock transaction')
    selected=[r for r in rows['MC_EVENT'] if r['name']=='tail_selection']
    require(len(selected)==12,'EOF-only mode requires twelve actual selected leases')
    result=[]
    for index,row in enumerate(selected):
        epoch=index//6+1
        require(row['owner']=='main' and row['instance']=='0' and row['session']==str(epoch) and common.full_identity(row)==(0,epoch,303,epoch) and int(row['b'])==index and int(row['f'])==epoch,'tail selection full identity/order differs')
        result.append(dict(instance='0',tick=row['a'],qpc=row['begin'],token=str(index+1),frame_generation=str(epoch),start=row['c'],stop=row['d'],slot=row['e'],session=row['session'],**{key:row[key] for key in ('handle_slot','handle_generation','operation','epoch')}))
    return result


def boundaries(rows,groups,expected):
    events=groups[('worker',0)];main=groups[('main',2)]
    require(not any(r['name']=='window' for r in events),'EOF substituted a finite verification pump cap')
    commands=[r for r in main if r['name']=='command']
    require(len(commands)==2 and [tuple(int(r[k]) for k in 'abcef') for r in commands]==[(1,1,TARGET,2,1),(1,2,TARGET,2,1)],'tail command target/source changed')
    phases=[r for r in main if r['name']=='tail_phase']
    require(len(phases)==2 and [r['a'] for r in phases]==['1','2'] and all(r['b']=='6' and r['c']=='3' for r in phases),'six captures/Free3 boundary absent')
    for session in (1,2):
        own=[r for r in events if r['session']==str(session)]
        ready=[r for r in own if r['name']=='slot' and r['label']=='ready']
        require(len(ready)==6 and [(int(r['d']),int(r['e'])) for r in ready]==list(expected) and all(common.full_identity(r)==(0,session,303,session) for r in ready),'tail source frames differ from full suffix')
        eos=[r for r in own if r['name']=='eof' and r['label'] in ('eof_update','eof_completion')]
        complete=[r for r in own if r['name']=='eof' and r['label']=='graph_complete']
        paired=[r for r in own if r['name']=='eof' and r['label']=='paired_empty']
        guard=[r for r in own if r['name']=='retirement' and r['label']=='guard']
        require(len(eos)==len(complete)==len(paired)==len(guard)==1,'fresh EOF/public graph completion pair absent or duplicated')
        e,c,p,g=eos[0],complete[0],paired[0],guard[0]
        require(e['a']==str(session) and e['b']=='6' and e['hr']=='00040003','sample EOS is not actual terminal after six pictures')
        previous=events[int(e['index'])-1]
        require(previous['name']=='call' and previous['label']==('update' if e['label']=='eof_update' else 'completion') and previous['hr']=='00040003' and int(previous['end'])<=int(e['begin']),'EOS is not paired to public Update/Completion')
        require(c['a']==str(session) and c['b']=='6' and c['c']=='1' and c['d']==c['e']=='0','EC_COMPLETE parameter/count differs')
        raw=[r for r in own if r['name']=='provider_event']
        require(all(int(r['c']) in (1,10,13) for r in raw) and sum(r['c']=='1' for r in raw)==1,'unknown/error/duplicate EOF graph event')
        require(p['a']==str(session) and p['b']=='6' and p['c']=='1' and max(int(e['end']),int(c['end']))<=int(p['begin'])<=int(g['begin']) and g['b']=='0','physical EOF pair did not precede retirement')
        empties=[r for r in own if r['name']=='drain' and r['label']=='eof_wait' and r['c']=='1' and int(r['begin'])>=int(e['end']) and int(r['end'])<=int(p['begin'])]
        require(empties,'fresh EOF lacks Empty queue observation')
        require(not any(r['name']=='call' and r['label'] in ('update','completion') and int(r['begin'])>int(e['end']) for r in own),'sample requested after actual EOF')
        require(all(int(r['end'])<=int(e['begin']) for r in ready) and not any(r['name']=='slot' and r['label'] in ('writing','ready') and int(r['begin'])>int(e['end']) for r in own),'pixel publication after actual EOF')
        abandoned=[r for r in own if r['name']=='slot' and r['label']=='abandon']
        require(len(abandoned)==1 and int(abandoned[0]['c'])==session*6 and int(g['end'])<=int(abandoned[0]['begin']),'ordinary EOS WRITING lease not abandoned after physical guard')
        after=[r for r in own if r['name']=='seek_position' and r['label']=='after']
        require(len(after)==1 and int(after[0]['c'])>=TARGET+2400000,'seek stop excludes full suffix endpoint')
        end=next(r for r in own if r['name']=='session' and r['label']=='end')
        final=next(r for r in main if r['name']=='worker_event' and r['session']==str(session) and int(r['a'])&10==10)
        require(int(end['end'])<=int(final['begin'])<=int(phases[session-1]['begin']),'main advanced before actual EOF graph retirement')
        # Reconstruct actual slot state at the phase; a scalar Free3 is not proof.
        states={}
        for r in sorted((r for r in rows['MC_EVENT'] if r['name']=='slot' and int(r['end'])<=int(phases[session-1]['begin'])),key=lambda r:int(r['end'])):
            states[int(r['a'])]=r['label']
        require(len(states)==3 and all(label in ('free','abandon') for label in states.values()),'replacement phase retained CPU lease')
        if session==1:
            begin2=next(r for r in events if r['name']=='session' and r['label']=='begin' and r['session']=='2')
            require(int(phases[0]['end'])<=int(commands[1]['begin'])<=int(begin2['begin']),'fresh graph began before retired six/Free3 boundary')
    return dict(actual_sample_eos_sessions=2,fresh_graph_complete_sessions=2,retained_dd=True,source_frame_count=12)


def finish(record,output,rows,expected):
    result=dict(kind=KIND,worker_transport_eof_accepted=False,selected_sink_callback_observed=False,retained_eof_replay_accepted=False,clock_policy_tested=False,simultaneous_playbacks_tested=False,native_runtime_verified=False,production_integration_accepted=False)
    try:
        h=old_worker.one(rows,'MC_HEADER');frequency=int(h['qpc_frequency']);result['costs']=common.costs(rows,frequency)
        require(record.get('kind')==h.get('kind')==KIND and record.get('mode')=='eof-tail' and h.get('mode')=='eof-tail' and common.shared_transport(rows) and h.get('support_seeking')=='1' and h.get('module_pin')=='process_lifetime','EOF runner/header mode mismatch')
        require(record['exit_code']==0 and record.get('unchanged') and all(record['unchanged'].values()) and not any(record.get(k) for k in ('outer_timeout','diagnostic_log_limit','diagnostic_log_monitor_error')) and not rows.get('MC_TIMEOUT'),'EOF process/input/bounds failed')
        groups=common.validate_trace(rows);selected=selections(rows);common.validate_leases(rows,selected)
        common.validate_shared_contract(rows,groups);result['package_config']=common.validate_package(rows,groups,record);result['workers']=common.validate_workers(rows,groups);result['event_drains']=common.validate_events(rows,groups)
        provider=Path(record.get('runtime_paths',{}).get('manifest',str(Path(record['lav_graph_provider']['path']).parent/'provider.manifest'))).parent
        for session in ('1','2'):
            metadata={k:[r for r in vals if r.get('instance')=='0' and r.get('session')==session] for k,vals in rows.items() if k.startswith('MP_LAV_')};common.validate_provider_session(metadata,provider)
        result.update(boundaries(rows,groups,expected));result.update(common.validate_render(rows,selected,output,expected))
        terminal=old_worker.one(rows,'MC_RESULT');require(terminal['functional']==terminal['A_clean']=='1' and terminal['eof_sessions']=='2' and terminal['captures']==terminal['readbacks']=='12' and terminal['transition_readbacks']=='0','EOF terminal count/cleanup differs')
        result.update(worker_transport_eof_accepted=True,outcome='shared_transport_actual_eof_twice_accepted')
    except (ValueError,KeyError,TypeError,OSError,StopIteration,IndexError,ZeroDivisionError) as error:
        result.update(validation_error=str(error),outcome='shared_transport_eof_failed_or_incomplete')
    return result
