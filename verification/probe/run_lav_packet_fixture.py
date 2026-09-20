#!/usr/bin/env python3
"""Bounded pinned-library observation, never COM graph/game qualification."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time
import bottle
from game_guard import game_running
from prepare_lav_fixture import verify_record
from prepare_lav_packet_headers import verify as verify_headers
from owned_lav_provider import (verify_record as verify_owned_record,
    protected_paths as owned_protected_paths)
from run_media_playback_fixture import (digest, valid_sha, validate_derivation,
    supervise_host_process, diagnostic_outcome, SEEK_TRACE_LIMIT)

TARGETS=(10000,10001,10080,10120)
ORIGINAL_TARGET_TICKS=tuple(x*10000 for x in TARGETS)
MODULES=('avutil-lav-60.dll','swresample-lav-6.dll','avcodec-lav-62.dll','avformat-lav-62.dll')


def windows(path):return 'Z:'+str(path.resolve()).replace('/','\\')


def require(value,message):
    if not value:raise ValueError(message)


def rescale(value, numerator, denominator):
    """Exact public av_rescale/AV_ROUND_NEAR_INF semantics; no float tolerance."""
    require(type(value) is int and type(numerator) is int and type(denominator) is int and
            numerator>0 and denominator>0,'invalid rational conversion')
    magnitude=abs(value)*numerator
    rounded=(magnitude+denominator//2)//denominator
    return -rounded if value<0 else rounded


def timestamp_ticks(value,num,den):
    # C++ explicitly retains AV_NOPTS_VALUE before calling av_rescale.
    return value if value==-(1<<63) else rescale(value,num*10000000,den)


def select_provider(kind, path, headers, forbidden_roots=(), patch_review=None):
    """Select one explicit provenance contract; official remains the default."""
    header_record=verify_headers(headers)
    if kind=='official':
        return verify_record(path),header_record
    if kind=='owned-control':
        provider=verify_owned_record(path,forbidden_roots)
        require(provider['headers']['commit']==header_record['commit'] and
                provider['headers']['record_sha256']==digest(headers),
                'owned provider/header identity mismatch')
        return provider,header_record
    if kind=='owned-strict':
        require(patch_review is not None,'strict provider requires patch review')
        from owned_lav_provider import verify_strict_record
        provider=verify_strict_record(path,patch_review,forbidden_roots)
        require(provider['headers']['commit']==header_record['commit'] and
                provider['headers']['record_sha256']==digest(headers),
                'strict provider/header identity mismatch')
        return provider,header_record
    raise ValueError('unknown provider kind')


def add_provider_kind_argument(parser):
    parser.add_argument('--provider-kind',choices=('official','owned-control','owned-strict'),default='official')
    parser.add_argument('--compiled-provider',type=Path,
        help='verified official provider record used to build the frozen probe; required for owned providers')
    parser.add_argument('--patch-review',type=Path,help='reviewed strict patch record; required for owned-strict')


def verify_compiled_official_provider(path, expected_sha256):
    provider=verify_record(path)
    require(valid_sha(expected_sha256) and digest(path)==expected_sha256,
            'compiled official provider record hash mismatch')
    return provider


def load_control_binding(path):
    record=json.loads(path.read_text())
    require(record.get('schema')==2 and record.get('matrix_mode')=='control' and record.get('measurement_complete') is True and
            record.get('provider_selection',{}).get('kind')=='owned-control' and
            record.get('original_control_outcomes')==[True,False,False,True] and 'validation_error' not in record,
            'saved control result is not a completed owned-control matrix observation')
    require(record.get('exit_code')==0 and record.get('diagnostic_outcome')=='completed_observation' and
            not any(record.get(key) for key in ('outer_timeout','diagnostic_log_limit','diagnostic_log_monitor_error')) and
            record.get('protocol_supervisor')==dict(type='supervisor',timeout=False,reaped=True,exit_code=0),
            'saved control outer/supervisor controls failed')
    protected=record.get('protected_before');unchanged=record.get('unchanged')
    required={'exe','provider','headers','media','original','derived_record'}
    require(type(protected) is dict and required<=set(protected) and all(valid_sha(protected[key]) for key in required) and
            type(unchanged) is dict and unchanged and set(protected)<=set(unchanged) and all(unchanged[key] is True for key in protected) and
            record['provider_selection'].get('selected_provider_record_sha256')==protected['provider'],
            'saved control protected-input binding failed')
    matrix=record.get('matrix');require(type(matrix) is dict,'saved control matrix absent')
    cases=record.get('cases');require(type(cases) is list and cases,'saved control sequential evidence absent')
    return record,matrix,{key:cases[0][key] for key in ('frames','packets','parsed')}


def _matrix_encoding(matrix):
    n=matrix['neighborhoods']
    values=[matrix['cue_count'],n[0]['ordinal'],n[0]['source_timestamp'],n[0]['k_ticks'],n[0]['f_ticks'],
        n[1]['ordinal'],n[1]['source_timestamp'],n[1]['k_ticks'],n[1]['f_ticks'],*matrix['targets_ticks']]
    return ','.join(str(x) for x in values)


def _validate_matrix(matrix,cue_index,cues,control,stream):
    require(type(matrix) is dict and set(matrix)=={'type','cue_count','neighborhoods','targets_ticks'} and matrix['type']=='matrix','matrix contract')
    require(cue_index==[dict(type='cue_index',count=matrix['cue_count'])],'cue index count changed')
    require(type(matrix['cue_count']) is int and 2<=matrix['cue_count']<=100000,'cue count range')
    require(type(matrix['neighborhoods']) is list and len(matrix['neighborhoods'])==2 and len(cues)==2,'cue coverage missing')
    expected=[]
    for i,(neighborhood,cue) in enumerate(zip(matrix['neighborhoods'],cues)):
        require(set(neighborhood)=={'ordinal','source_timestamp','k_ticks','f_ticks'},'neighborhood contract')
        require(cue==dict(type='cue',threshold_ticks=(20000000,50000000)[i],**neighborhood),'cue/matrix mismatch')
        require(all(type(neighborhood[x]) is int for x in neighborhood),'cue integer contract')
        require(0<=neighborhood['ordinal']<matrix['cue_count'] and (i==0 or neighborhood['ordinal']>matrix['neighborhoods'][0]['ordinal']),'cue ordinal order')
        require(neighborhood['k_ticks']==timestamp_ticks(neighborhood['source_timestamp'],stream['num'],stream['den']),'cue timestamp conversion')
        require(neighborhood['k_ticks']>=(20000000,50000000)[i] and neighborhood['k_ticks']<98000000,'cue range')
        frames=[f for f in control['frames'] if f['absolute_pts']<neighborhood['k_ticks'] and not f['key']]
        require(frames and neighborhood['f_ticks']==frames[-1]['absolute_pts']<neighborhood['k_ticks'],'sequential F mismatch')
        expected.extend((neighborhood['k_ticks']-10000,neighborhood['k_ticks'],neighborhood['k_ticks']+10000,
            neighborhood['f_ticks'],neighborhood['f_ticks']+10000))
    generated=[*ORIGINAL_TARGET_TICKS,*expected,0]
    deduplicated=[]
    for target in generated:
        if target not in deduplicated:deduplicated.append(target)
    targets=matrix['targets_ticks']
    require(targets==deduplicated and 5<=len(targets)<=15 and len(targets)==len(set(targets)),'target matrix/order/dedup')
    require(all(type(x) is int and 0<=x<=104000000 and x%10000==0 for x in targets),'target range/ticks')
    require(all(len([f for f in control['frames'] if f['absolute_pts']>=x])>=6 for x in targets),'six successors missing')
    return dict(type='matrix',cue_count=matrix['cue_count'],neighborhoods=[dict(x) for x in matrix['neighborhoods']],targets_ticks=list(targets))


def analyze(lines,provider,expected_matrix=None,control_binding=None):
    rows=[]
    for line in lines:
        require(len(line)<16384,'oversized protocol line')
        rows.append(json.loads(line))
        require(len(rows)<=10000,'protocol row cap')
    require(rows,'empty protocol')
    legacy=rows and rows[0]==dict(type='header',schema=1,scope='public_library_only',integer_targets=True,negative_frames_retained=True,parser='lav_0_81_mpeg_cache')
    matrix_mode=None if legacy else rows[0].get('matrix_mode') if rows else None
    if not legacy:require(rows[0]==dict(type='header',schema=2,scope='public_library_only',integer_targets=True,negative_frames_retained=True,parser='lav_0_81_mpeg_cache',matrix_mode=matrix_mode) and matrix_mode in ('control','bound'),'header contract')
    require(rows[-1]==dict(type='supervisor',timeout=False,reaped=True,exit_code=0) and rows[-2].get('type')=='complete' and rows[-2].get('cleanup') is True,'incomplete/failed/timeout supervisor')
    modules=[];versions=[];cases=[];case=None;cue_index=[];cues=[];matrices=[]
    for row in rows[1:-2]:
        kind=row['type']
        require(kind not in ('error','api_error'),'reported API failure')
        if kind=='module':
            require(case is None and not cases,'module outside startup')
            raw=row['path_utf16hex'];require(len(raw)%4==0,'module encoding')
            modules.append(''.join(chr(int(raw[i:i+4],16)) for i in range(0,len(raw),4)))
        elif kind=='versions':versions.append(row)
        elif kind=='begin':
            require(case is None and len(cases)<16,'nested/excess case')
            index=len(cases)
            target_ticks=0 if index==0 else (ORIGINAL_TARGET_TICKS[index-1] if legacy else matrices[0]['targets_ticks'][index-1] if matrices else -1)
            require(target_ticks>=0 and row==dict(type='begin',mode='linear' if index==0 else 'seek',target_ms=target_ticks//10000,target_ticks=target_ticks),'case sequence/target')
            case=dict(begin=row,packets=[],frames=[],parsed=[],seeks=[],streams=[],demux_parsers=[])
        elif kind in ('packet','frame','parsed','seek','stream','demux_parser'):
            require(case is not None,'row outside case');case[dict(packet='packets',frame='frames',parsed='parsed',seek='seeks',stream='streams',demux_parser='demux_parsers')[kind]].append(row)
        elif kind=='end':
            require(case is not None and row.get('complete') is True,'end without case')
            case['end']=row;cases.append(case);case=None
        elif kind=='cue_index':require(case is None and not legacy and len(cases)==1 and not cue_index, 'cue index placement');cue_index.append(row)
        elif kind=='cue':require(case is None and not legacy and len(cases)==1 and len(cues)<2,'cue placement');cues.append(row)
        elif kind=='matrix':require(case is None and not legacy and len(cases)==1 and not matrices,'matrix placement');matrices.append(row)
        else:raise ValueError('unknown protocol row')
    expected_cases=5 if legacy else (len(matrices[0]['targets_ticks'])+1 if matrices else -1)
    require(case is None and len(cases)==expected_cases and rows[-2]==dict(type='complete',cases=expected_cases,cleanup=True),'missing cases')
    require([p.lower() for p in modules]==[windows(provider/n).lower() for n in MODULES],'loaded module paths differ')
    require(len(versions)==1 and all(versions[0][n]==versions[0]['header_'+n] and versions[0][n]>0 for n in ('format','codec','util')),'runtime/header versions differ')
    for index,c in enumerate(cases):
        target=c['begin']['target_ticks'];packets=c['packets'];frames=c['frames'];end=c['end']
        require(len(c['streams'])==1,'stream witness absent')
        stream=c['streams'][0];num,den=stream['num'],stream['den']
        require(type(num) is int and type(den) is int and 0<num<=2147483647 and 0<den<=2147483647 and
                stream['format_start']==0 and stream['stream_start']==0,'input timeline differs')
        require(stream==cases[0]['streams'][0],'input rational changed between fresh cases')
        require(0<end['packets']<=(400 if index==0 else 128) and 0<end['frames']<=400,'case bounds')
        require(packets and frames and c['parsed'],'missing packet/parser/frame evidence')
        require([p['ordinal'] for p in packets]==list(range(end['packets'])),'packet records missing/duplicated')
        require([p['row'] for p in c['parsed']]==list(range(end['parser_rows'])) and
                sum(p['bytes']>0 for p in c['parsed'])==end['parsed_packets'],'parser records missing/duplicated')
        require([p['input_packet'] for p in c['parsed']]==sorted(p['input_packet'] for p in c['parsed']) and
                all(sum(r['used'] for r in c['parsed'] if r['input_packet']==p['ordinal'])==p['bytes'] for p in packets),'parser input consumption incomplete')
        require(all(valid_sha(p['sha256']) and p['bytes']>0 and p['pos']>=0 and p['relative_pts']==p['absolute_pts']-target and p['absolute_pts']==timestamp_ticks(p['source_pts'],num,den) and p['absolute_dts']==timestamp_ticks(p['source_dts'],num,den) for p in packets),'packet identity/domain')
        require(all(valid_sha(f['sha256']) and f['format']==0 and f['absolute_pts']==f['relative_pts']+target for f in frames),'decoded frame identity/domain')
        require(all(a['absolute_pts']<b['absolute_pts'] for a,b in zip(frames,frames[1:])),'decoded PTS not progressing')
        require(all(p['used']>=0 and p['bytes']>=0 and 0<=p['input_packet']<end['packets'] for p in c['parsed']),'parser bounds')
        require([x['site'] for x in c['demux_parsers']]==(['after_info'] if index==0 else ['after_info','after_seek']) and all(not x['needed'] or x['after']&8192 for x in c['demux_parsers']),'demux parser configuration absent')
        if index==0:
            labels=[f['absolute_pts'] for f in frames]
            if legacy:require(not c['seeks'] and labels==list(range(98000000,104000001,400000)),'sequential oracle labels incomplete')
            else:require(not c['seeks'] and len(frames)==end['frames']==260 and
                    [f['ordinal'] for f in frames]==list(range(260)) and labels[:2]==[0,800000] and labels[-1]==104000000 and
                    [x for x in labels if x>=98000000]==list(range(98000000,104000001,400000)),
                    'sequential oracle labels incomplete')
        else:
            require([f['ordinal'] for f in frames]==list(range(end['frames'])),'seek packet/frame records missing')
            require(end['positive']>=12 and sum(f['relative_pts']>=0 for f in frames)==end['positive'],'positive sample count')
            seeks=c['seeks'];require(len(seeks) in (1,2) and seeks[0]['flags']==1 and seeks[-1]['result']>=0,'seek result absent')
            require(len(seeks)==1 or legacy and seeks[0]['result']<0 and seeks[1]['flags']==5,'invalid fallback seek')
            require(all(s['timestamp']==rescale(target,den,num*10000000) for s in seeks),'integer seek timestamp differs')
    control=cases[0];observations=[]
    for c in cases[1:]:
        target=c['begin']['target_ticks'];wanted=[f for f in control['frames'] if f['absolute_pts']>=target][:6]
        actual=[f for f in c['frames'] if f['relative_pts']>=0][:6]
        exact=len(actual)==len(wanted)==6 and all((a['absolute_pts'],a['sha256'])==(b['absolute_pts'],b['sha256']) for a,b in zip(actual,wanted))
        landing=c['packets'][0]
        packet_matches=[p for p in control['packets'] if p['pos']==landing['pos']]
        require(len(packet_matches)==1 and all(landing[k]==packet_matches[0][k] for k in ('source_pts','sha256','bytes')),'seek landing absent/changed in sequential packet control')
        observations.append(dict(target_ms=c['begin']['target_ms'],target_ticks=target,landing=landing,negative_frames=sum(f['relative_pts']<0 for f in c['frames']),
            exact_first_six=exact,expected_absolute_pts=[f['absolute_pts'] for f in wanted],actual_first_six=actual))
    result=dict(measurement_complete=True,all_targets_exact=all(x['exact_first_six'] for x in observations),observations=observations,
        versions=versions[0],cases=cases,protocol_supervisor=rows[-1],scope='same_library_parser_decode_mechanism_only',graph_source_delivery_observed=False,
        game_playback_proven=False,native_windows_qualified=False,original_es_seek_qualified=False)
    if not legacy:
        matrix=_validate_matrix(matrices[0],cue_index,cues,control,cases[0]['streams'][0])
        require(expected_matrix is None or matrix==expected_matrix,'saved target matrix changed')
        expected_original={100000000:100000000,100010000:100400000,100800000:100800000,101200000:101200000,0:0}
        by_target={x['target_ticks']:x for x in observations}
        require(all(t in by_target and by_target[t]['expected_absolute_pts'][0]==value for t,value in expected_original.items()),'original/zero sequential starts changed')
        result['original_control_outcomes']=[by_target[t]['exact_first_six'] for t in ORIGINAL_TARGET_TICKS]
        classes={x:'original' for x in ORIGINAL_TARGET_TICKS};classes[0]='fresh_zero'
        for n in matrix['neighborhoods']:
            classes.update({n['k_ticks']-10000:'pre_cue',n['k_ticks']:'exact_cue',n['k_ticks']+10000:'between_frame',
                n['f_ticks']:'pre_cue_frame',n['f_ticks']+10000:'between_frame'})
        for observation in observations:observation['target_class']=classes[observation['target_ticks']]
        result.update(matrix_mode=matrix_mode,matrix=matrix,compared_frames=len(observations)*6)
        require(result['compared_frames']<=90,'compared frame cap')
        if control_binding is not None:
            require(matrix_mode=='bound','control binding on non-bound run')
            for key in ('frames','packets','parsed'):
                require(cases[0][key]==control_binding[key],'strict sequential control changed')
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('exe','provider','headers','media','derived-record','output'):p.add_argument('--'+name,required=True,type=Path)
    add_provider_kind_argument(p)
    p.add_argument('--matrix-mode',choices=('legacy','control','bound'),default='legacy')
    p.add_argument('--control-result',type=Path,help='completed owned-control result; required for bound mode')
    p.add_argument('--exe-sha256',required=True)
    a=p.parse_args()
    for name in ('exe','provider','headers','media','derived_record','output'):setattr(a,name,getattr(a,name).resolve())
    if a.compiled_provider:a.compiled_provider=a.compiled_provider.resolve()
    if a.patch_review:a.patch_review=a.patch_review.resolve()
    if a.control_result:a.control_result=a.control_result.resolve()
    if a.matrix_mode=='bound' and (a.provider_kind!='owned-strict' or not a.control_result):p.error('bound mode requires owned-strict and --control-result')
    if a.matrix_mode!='bound' and (a.provider_kind=='owned-strict' or a.control_result):p.error('strict provider/control result are bound-mode only')
    if (a.provider_kind=='owned-strict')!=(a.patch_review is not None):p.error('--patch-review is required only for owned-strict')
    if bottle.BOTTLE!='X3':p.error('X3M_FIXTURE_BOTTLE=X3 required')
    if game_running():p.error('game running; no fixture execution')
    if a.output.exists() or any(x.is_relative_to(bottle.game_dir().resolve()) for x in (a.exe,a.output,a.provider,a.media)):p.error('fresh private output and inputs outside game required')
    if not valid_sha(a.exe_sha256) or digest(a.exe)!=a.exe_sha256:p.error('EXE hash mismatch')
    try:
        provider,headers=select_provider(a.provider_kind,a.provider,a.headers,
            (bottle.bottle_dir().resolve(),),a.patch_review)
    except (OSError,ValueError,KeyError,TypeError,json.JSONDecodeError) as error:
        p.error(str(error))
    original=(bottle.game_dir()/'mov/00002.dat').resolve()
    derivation=validate_derivation(a.derived_record,a.media,original,10000,True)
    build=json.loads(a.exe.with_suffix('.build.json').read_text())
    if build['exe_sha256']!=a.exe_sha256 or build['headers_record_sha256']!=digest(a.headers):p.error('build provenance mismatch')
    selected_provider_sha256=digest(a.provider)
    compiled_provider_sha256=build.get('provider_record_sha256')
    if a.provider_kind=='official':
        if compiled_provider_sha256!=selected_provider_sha256:p.error('build provenance mismatch')
    else:
        if not a.compiled_provider:p.error('--compiled-provider is required for owned providers')
        if a.compiled_provider.is_relative_to(bottle.bottle_dir().resolve()):p.error('compiled provider must be outside bottle')
        try:verify_compiled_official_provider(a.compiled_provider,compiled_provider_sha256)
        except (OSError,ValueError,KeyError,IndexError,TypeError,json.JSONDecodeError) as error:p.error(str(error))
        if (compiled_provider_sha256==selected_provider_sha256 or build.get('header_commit')!=headers['commit']):
            p.error('owned runtime selection does not match frozen build/header provenance')
    protected=dict(exe=a.exe,provider=a.provider,headers=a.headers,media=a.media,original=original,derived_record=a.derived_record,
        game_exe=bottle.game_dir()/'X3AP.exe',bottle_config=bottle.bottle_dir()/'cxbottle.conf')
    if a.provider_kind=='official':
        protected.update({'provider_'+name:a.provider.parent/name for name in provider['files']})
    else:
        protected.update(owned_protected_paths(a.provider,provider))
        protected['compiled_official_provider_record']=a.compiled_provider
    if a.patch_review:protected['patch_review']=a.patch_review
    control_record=expected_matrix=control_sequential=None
    if a.control_result:
        try:control_record,expected_matrix,control_sequential=load_control_binding(a.control_result)
        except (OSError,ValueError,KeyError,IndexError,TypeError,json.JSONDecodeError) as error:p.error(str(error))
        if control_record['provider_selection']['selected_provider_record_sha256']!=provider.get('base_control',{}).get('record_sha256'):
            p.error('saved runtime control differs from strict provider base control')
        protected['control_result']=a.control_result
    before={k:digest(v) for k,v in protected.items()}
    if control_record is not None:
        for key in ('exe','headers','media','derived_record','original'):
            if control_record.get('protected_before',{}).get(key)!=before[key]:p.error('saved control source/build inputs differ')
        if control_record.get('build',{}).get('exe_sha256')!=a.exe_sha256 or control_record['build'].get('headers_record_sha256')!=before['headers']:
            p.error('saved control executable/header binding differs')
    a.output.mkdir(parents=True)
    command=[bottle.WINE,*bottle.wine_args(),'--debugmsg','-all','--dll','winegstreamer=',str(a.exe),windows(a.provider.parent),windows(a.media)]
    if a.matrix_mode=='control':command.append('--multigop-control')
    elif a.matrix_mode=='bound':command.extend(('--multigop-bound',_matrix_encoding(expected_matrix)))
    record=dict(schema=1 if a.matrix_mode=='legacy' else 2,matrix_mode=a.matrix_mode,command=command,bottle=bottle.describe(),build=build,derivation=derivation,protected_before=before,
        provider_selection=dict(kind=a.provider_kind,compiled_official_provider_record_sha256=compiled_provider_sha256,
            selected_provider_record_sha256=selected_provider_sha256,
            hashes_differ=compiled_provider_sha256!=selected_provider_sha256),
        scope='public_library_only',game_playback_proven=False,native_windows_qualified=False,
        limits=dict(child_seconds=60,outer_seconds=95,combined_log_bytes=SEEK_TRACE_LIMIT,poll_seconds=0.1))
    if a.control_result:record['control_binding']=dict(result_sha256=before['control_result'],matrix=expected_matrix)
    start=time.monotonic()
    with (a.output/'stdout.txt').open('wb') as out,(a.output/'stderr.txt').open('wb') as err:
        proc=subprocess.Popen(command,cwd=a.output,env=os.environ.copy(),stdout=out,stderr=err,start_new_session=True)
        record.update(supervise_host_process(proc,[a.output/'stdout.txt',a.output/'stderr.txt'],95,SEEK_TRACE_LIMIT))
    record['wall_seconds']=time.monotonic()-start
    record['diagnostic_outcome']=diagnostic_outcome(record)
    record['measurement_complete']=False
    try:
        record['unchanged']={k:digest(v)==before[k] for k,v in protected.items()}
        require(record['exit_code']==0 and all(record['unchanged'].values()) and record['diagnostic_outcome']=='completed_observation','outer controls failed')
        if a.provider_kind=='owned-control':
            verify_owned_record(a.provider,(bottle.bottle_dir().resolve(),))
        elif a.provider_kind=='owned-strict':
            from owned_lav_provider import verify_strict_record
            verify_strict_record(a.provider,a.patch_review,(bottle.bottle_dir().resolve(),))
        with (a.output/'stdout.txt').open() as stream:record.update(analyze(stream,a.provider.parent,expected_matrix,control_sequential))
        require(a.matrix_mode!='control' or record['original_control_outcomes']==[True,False,False,True],
            'unmodified control did not reproduce original four outcomes')
        require(a.matrix_mode!='bound' or record['all_targets_exact'],'strict target comparison failed')
    except (OSError,ValueError,KeyError,IndexError,TypeError) as error:
        record['measurement_complete']=False;record['validation_error']=str(error)
    (a.output/'result.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps({k:record.get(k) for k in ('measurement_complete','all_targets_exact','exit_code','wall_seconds','validation_error')}))
    return 0 if record['measurement_complete'] else 2


if __name__=='__main__':raise SystemExit(main())
