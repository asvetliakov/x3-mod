"""Independent actual-QPC replay and lease/render witnesses for two real workers."""
import hashlib
import json
import struct
from bisect import bisect_right
from collections import defaultdict
from fractions import Fraction
from pathlib import Path

import media_lav_evidence as old
import media_worker_sample_evidence as worker
from run_media_playback_fixture import digest, fnv64

KIND='worker_owned_clock_two_textures_v2'
EOF_KIND='worker_shared_transport_eof_tail_v1'

def tail_mode(rows):
    return bool(rows.get('MC_HEADER') and rows['MC_HEADER'][0].get('kind')==EOF_KIND)

def worker_instances(groups):
    return tuple(sorted(key[1] for key in groups if key[0]=='worker'))

def shared_transport(rows):
    return bool(rows.get('MC_HEADER') and rows['MC_HEADER'][0].get('transport')=='production_lav_worker')

def full_identity(row):
    return tuple(int(row[k]) for k in ('handle_slot','handle_generation','operation','epoch'))
FRAME_BYTES=1048576
UNIT=1<<38
FACTOR=2748779
TICKS=10000000
WINDOWS={0:(2800000,6),22000000:(24800000,7),100000000:(103600000,9)}
CLOCK_HEADERS={'clock.h':'2af6ddfb16486fbc4fd9ab7eda225899aa476df38090deaa6608b2877c9c7cbd',
               'exact_time.h':'53aeb725c5b4c5eb93c23ae7bc8dabb97b323364b1c2bcfdb136bb10397414de'}
require=worker.require
binding=worker.binding
integer=worker.integer


def references(record):
    op,original=old.graph_result_binding(record,'original_reference')
    sp,strict=old.graph_result_binding(record,'strict_reference')
    require(original.get('lav_transport')==strict.get('lav_transport')=='reference-matrix','reference modes differ')
    require(original['exe']['sha256']==strict['exe']['sha256']==old.GRAPH_REFERENCE_EXE,'reference executable differs')
    require(original['lav_qualification'].get('graph_original_reference_ready') and strict['lav_qualification'].get('graph_derived_reference_ready'),'reference readiness absent')
    require(original['lav_provider']==strict['lav_provider']==record['lav_provider'] and strict['lav_graph_provider']==record['lav_graph_provider'],'provider reference cohort differs')
    require(strict['lav_multigop_reference_result']==record['original_reference'] and strict['media']==record['media'] and strict['derived_media']['sha256']==record['derived_media']['sha256'],'reference source binding differs')
    require(original['media']['sha256']==record['derived_media']['record']['original']['sha256'],'original payload differs')
    require(all(x['build']['lav_helper_sha256']==worker.HELPER for x in (original,strict,record)),'frozen helper differs')
    require(record['build'].get('kind')=='worker_clock_build_v2' and record['build']['exe_sha256']==record['exe']['sha256'] and record['build'].get('clock_commit')=='0abe0a44' and record['build'].get('clock_headers')==CLOCK_HEADERS,'new clock fixture build binding differs')
    ow,pixels=old.graph_capture_content(op,original,fnv64,'graph_capture_sha256')
    sw,spixels=old.graph_capture_content(sp,strict,fnv64,'graph_capture_sha256')
    require(old.graph_oracle_layout(ow) and old.graph_oracle_layout(sw) and pixels==spixels and old.graph_sample_labels(original['stages']['copy'])==old.graph_sample_labels(strict['stages']['copy']),'reference RGB/time layouts differ')
    old.graph_original_overlap(original,ow,pixels,fnv64)
    expected={}
    for start,(end,count) in WINDOWS.items():
        selected=[(r,p) for r,p in zip(ow,pixels) if start<=int(r['start'])<end]
        require(len(selected)==count,'reference window count differs')
        for row,raw in selected:
            key=(int(row['start']),int(row['end']))
            require(key[1]<=end,'reference picture crosses window')
            expected[key]=dict(rgb_sha256=worker.rgb_digest(raw),raw=raw)
    return expected


def parse(text):
    rows={}
    for line in text.splitlines():
        if line.startswith('MC_META '):
            prefix=line.split(' ',3);body=prefix[3];name=body.split(' ',1)[0]
            row=worker.fields(body);row.update(worker.fields('x '+prefix[1]+' '+prefix[2]))
        else:
            name=line.split(' ',1)[0]
            if not name.startswith('MC_'):continue
            row=worker.fields(line)
        rows.setdefault(name,[]).append(row)
    return rows


def bits(value):return struct.unpack('<Q',struct.pack('<d',value))[0]


class Replay:
    """Fraction arithmetic independently extends the reviewed clock host tests."""
    def __init__(self,frequency):
        self.frequency=frequency;self.position=Fraction(0);self.rate=UNIT;self.last=0
        self.operation=self.generation=self.start=self.end_ms=0
        self.intent=self.paused=self.armed=self.loop=False
        self.end=0;self.queue=[];self.seen=False;self.last_start=0

    def advance(self,now):
        require(now>=self.last,'QPC regressed')
        if self.armed and self.intent and not self.paused and self.end==0:
            self.position+=Fraction((now-self.last)*self.rate*TICKS,self.frequency*UNIT)
        self.last=now

    def reset(self,now):
        self.generation+=1;self.last=now;self.position=Fraction(self.start)
        self.armed=False;self.end=0;self.queue=[];self.seen=False;self.last_start=0

    def step(self,row):
        name=row['name'];a,b,c,d=(int(row[k]) for k in 'abcd');now=int(row['qpc'])
        selected=None;consumed=late=superseded=0;result=1
        if name=='begin':
            require(a>0 and b>=0 and now>=self.last,'invalid begin')
            self.operation=a;self.start=b;self.end_ms=c;self.loop=bool(d);self.intent=True;self.paused=False;self.reset(now)
        elif name=='seek':
            require(self.operation and a>=0 and now>=self.last,'invalid seek')
            self.start=a;self.end_ms=b;self.reset(now)
        elif name=='restart':
            require(self.intent and self.loop and self.end in (1,2),'invalid automatic restart')
            self.reset(now)
        elif name in ('pause','resume','stop','rate'):
            require(self.operation,'command before begin')
            if name=='rate':require(a>0,'invalid rate')
            if name in ('pause','resume'):require(self.intent and self.end==0,'invalid pause/resume intent')
            self.advance(now)
            if name=='pause':self.paused=True
            elif name=='resume':self.paused=False
            elif name=='stop':self.intent=False;self.paused=False;self.generation+=1;self.queue=[]
            else:self.rate=a*FACTOR
        elif name=='submit':
            if a!=self.generation:result=1
            elif not self.intent or self.end:result=2
            elif c<0 or d<=c or self.seen and c<self.last_start:self.end=3;result=4
            elif len(self.queue)==3:result=3
            else:self.queue.append(dict(generation=a,token=b,start=c,end=d));self.seen=True;self.last_start=c;result=0
        elif name=='update':
            self.advance(now)
            if self.intent and not self.paused and not self.end:
                if not self.armed:
                    while self.queue and self.queue[0]['end']<=self.position:self.queue.pop(0);consumed+=1;late+=1
                    if self.queue:self.armed=True
                if self.armed:
                    scaled=float(self.position/TICKS)*1000.0
                    if not 0<=scaled<2147483648:self.end=4
                    elif self.end_ms>0 and int(scaled)>self.end_ms:self.end=1
                    if not self.end:
                        while self.queue and consumed<3 and self.queue[0]['start']<=self.position:
                            frame=self.queue.pop(0);consumed+=1
                            if frame['end']<=self.position:late+=1;continue
                            if selected:superseded+=1
                            selected=frame
        else:raise ValueError('unknown clock transaction '+name)
        seconds=float(self.position/TICKS);scaled=seconds*1000.0;valid=0<=scaled<2147483648
        expected=dict(result=result,operation=self.operation,generation=self.generation,rate=self.rate,armed=int(self.armed),paused=int(self.paused),intent=int(self.intent),queued=len(self.queue),end=self.end,ms=int(scaled) if valid else -(1<<31),in_range=int(valid),seconds=bits(seconds),scaled=bits(scaled),selected=int(selected is not None),token=selected['token'] if selected else 0,frame_generation=selected['generation'] if selected else 0,start=selected['start'] if selected else 0,stop=selected['end'] if selected else 0,consumed=consumed,late=late,superseded=superseded)
        for key,value in expected.items():require(int(row[key])==value,f'clock replay differs at {row.get("index")} {key}')
        numerator=self.position*self.frequency*UNIT
        require(numerator.denominator==1 and int(row['numerator'],16)==numerator.numerator,'exact clock numerator differs')
        return expected


def replay_clock(rows,frequency):
    clocks=rows.get('MC_CLOCK',[]);require(clocks,'clock transactions absent')
    require([int(r['index']) for r in clocks]==list(range(len(clocks))),'clock sequence hole')
    models=[Replay(frequency),Replay(frequency)];previous=0;selected=[]
    for row in clocks:
        ident=int(row['instance']);require(ident in (0,1) and int(row['qpc'])>=previous,'clock instance/QPC order differs');previous=int(row['qpc'])
        expected=models[ident].step(row)
        if row['name']=='submit':require(expected['result']==0,'runtime malformed/inactive metadata submission')
        if expected['selected']:selected.append(row)
    return selected


def validate_trace(rows):
    header=worker.one(rows,'MC_HEADER');tail=tail_mode(rows);count=1 if tail else 2;require(header['kind']==(EOF_KIND if tail else KIND) and header['instances']==str(count) and header['slots_per_instance']=='3' and header['capture_limit']==('12' if tail else '40') and header['readback_limit']==('12' if tail else '52') and header['clock_commit']=='0abe0a44' and header['graph_clock']=='none_explicit','fixture header differs')
    events=rows.get('MC_EVENT',[]);traces=rows.get('MC_TRACE',[])
    groups={('main',2):[r for r in events if r['owner']=='main']}
    groups.update({('worker',i):[r for r in events if r['owner']=='worker' and int(r['instance'])==i] for i in range(count)})
    require(len(traces)==count+1 and sum(len(x) for x in groups.values())==len(events),'trace ownership absent')
    threads=[]
    for key,group in groups.items():
        t=next((r for r in traces if r['owner']==key[0] and int(r['instance'])==key[1]),None)
        require(t and t['overflow']=='0' and int(t['count'])==len(group),'trace missing/overflowed')
        require([int(r['index']) for r in group]==list(range(len(group))),'trace sequence hole')
        ids={r['thread'] for r in group};require(len(ids)==1 and '0' not in ids,'trace thread differs');threads.extend(ids)
        require(all(int(r['begin'])>0 and int(r['end'])>=int(r['begin']) for r in group),'invalid QPC span')
        require(all(int(a['end'])<=int(b['end']) for a,b in zip(group,group[1:])),'owner QPC chronology regressed')
    require(len(set(threads))==count+1 and threads[0]==header['main_thread'],'worker/main owners not distinct')
    require(not any(r['name']=='call' for r in groups[('main',2)]),'graph call on main')
    return groups


def validate_leases(rows,selected):
    events=rows['MC_EVENT'];clocks=rows.get('MC_CLOCK',[]);leases={};ready={};selected_by_tick={(int(r['instance']),int(r['tick'])):r for r in selected}
    require(len(selected_by_tick)==len(selected),'multiple destination copies per instance tick')
    for ident in ((0,) if tail_mode(rows) else (0,1)):
        relevant=[r for r in events if int(r['instance'])==ident and r['name']=='slot']
        rank={'writing':0,'ready':1,'reading':2,'free':3,'abandon':3}
        relevant.sort(key=lambda r:(int(r['begin']),int(r['c']),rank[r['label']]))
        states=[None]*3;writing_at={}
        for r in relevant:
            index,gen,seq=(int(r[k]) for k in 'abc');label=r['label'];require(0<=index<3,'invalid CPU slot')
            session=int(r['session'] if r['owner']=='worker' else r['d']);identity=(session,gen,seq,full_identity(r) if "handle_slot" in r else None)
            if label=='writing':require(r['owner']=='worker' and states[index] is None,'worker overwrote leased CPU slot');states[index]=('writing',identity);writing_at[index]=int(r['begin'])
            elif label=='ready':
                require(r['owner']=='worker' and states[index]==('writing',identity),'READY without WRITING');states[index]=('ready',identity)
                require((ident,seq+1) not in ready,'published token reused across sessions/generations');ready[(ident,seq+1)]=r
            elif label=='reading':require(r['owner']=='main' and states[index]==('ready',identity),'READING without READY');states[index]=('reading',identity);leases[(ident,seq+1)]=[r,None]
            elif label=='free':
                require(r['owner']=='main' and states[index]==('reading',identity),'FREE without owned READING');states[index]=None
                leases[(ident,seq+1)][1]=r
            elif label=='abandon':
                require(r['owner']=='worker' and states[index]==('writing',identity),'worker abandoned another owner')
                guards=[g for g in events if g['owner']=='worker' and int(g['instance'])==ident and g['name']=='retirement' and g['label'] in ('guard','seek_guard','cancel_guard') and g['session']==str(session) and g['a']=='1' and writing_at[index]<=int(g['end'])<=int(r['begin'])]
                require(guards,'WRITING abandoned before actual retirement')
                guard=max(guards,key=lambda g:int(g['end']))
                intervening=[c for c in events if c['owner']=='worker' and int(c['instance'])==ident and c['name']=='call' and c['label'] in ('update','completion','run','stream_run') and int(guard['end'])<int(c['begin'])<int(r['begin'])]
                require(not intervening,'WRITING abandoned after retirement invalidated');states[index]=None
        require(states==[None]*3,'CPU lease retained at exit')
        exits=[r for r in events if int(r['instance'])==ident and r['name']=='worker_exit'];require(len(exits)==1 and exits[0]['a']=='0' and exits[0]['b']=='1','worker did not exit safely')
        final=[r for r in events if int(r['instance'])==ident and r['name']=='cpu_final'];require(len(final)==3 and {r['a'] for r in final}=={'0','1','2'} and all(r['b']=='0' for r in final),'CPU final observations absent')
        require(all(int(r['begin'])>=int(exits[0]['end']) for r in final),'CPU final before worker exit')
    for r in clocks:
        if r['name']!='submit':continue
        key=(int(r['instance']),int(r['b']));require(key in leases and key in ready,'metadata token lacks real source lease')
        start,end=leases[key];raw=ready[key]
        require(end and int(start['begin'])<=int(r['qpc'])<=int(end['begin']) and r['a']==raw['b'] and r['c']==raw['d'] and r['d']==raw['e'],'metadata not immutable source lease')
    # Replay ownership separately from the arithmetic: queued metadata must keep
    # its real READING lease until consumption or explicit clock invalidation.
    queues=[[],[]];retired={}
    for r in clocks:
        ident=int(r['instance']);q=queues[ident];now=int(r['qpc'])
        if r['name']=='submit':
            key=(ident,int(r['b']));require(key not in retired and key not in q,'metadata token submitted twice');q.append(key)
        elif r['name']=='update':
            count=int(r['consumed']);require(count<=len(q),'clock consumed unleased metadata')
            for key in q[:count]:retired[key]=now
            del q[:count]
        elif r['name'] in ('begin','seek','restart','stop'):
            for key in q:retired[key]=now
            q.clear()
        require(len(q)==int(r['queued']),'lease mirror differs from clock queue')
    require(not any(queues),'clock metadata retained at exit')
    for key,when in retired.items():require(leases[key][1] and int(leases[key][1]['begin'])>=when,'CPU lease freed while metadata queued')
    for r in selected:
        key=(int(r['instance']),int(r['token']));require(key in leases,'selected token absent')
        acquired,released=leases[key]
        if tail_mode(rows):
            raw=ready[key]
            require(r['start']==raw['d'] and r['stop']==raw['e'] and r['frame_generation']==raw['b'] and r['slot']==raw['a']==acquired['a'] and r['session']==raw['session'] and full_identity(r)==full_identity(raw)==full_identity(acquired) and int(acquired['end'])<=int(r['qpc'])<=int(released['begin']),'tail selected interval/identity is not its actual READY lease')
        uploads=[x for x in events if x['name']=='graphics' and x['label']=='texture_unlock' and int(x['instance'])==key[0] and x['a']==r['tick'] and int(x['b'])+1==key[1]]
        require(len(uploads)==1 and uploads[0]['hr']=='00000000' and int(r['qpc'])<=int(uploads[0]['begin'])<=int(uploads[0]['end'])<=int(released['begin']),'selected lease released before texture upload')
    return ready,leases


def validate_source_windows(rows,expected):
    for ident in (0,1):
        windows={1:(0,6),2:(22000000,7)} if ident==0 else {g:(100000000,9) for g in (1,2,3)}
        grouped=defaultdict(list)
        for r in rows['MC_EVENT']:
            if int(r['instance'])!=ident or r['name']!='slot' or r['label']!='ready':continue
            gen=int(r['b']);require(gen in windows and r['session']=='1','source published outside allowed epoch')
            start,count=windows[gen];interval=(int(r['d']),int(r['e']))
            require(interval in expected and start<=interval[0]<WINDOWS[start][0] and r['f']==str(FRAME_BYTES),'actual source interval outside frozen window')
            grouped[gen].append(interval)
        for gen,values in grouped.items():
            require(len(values)<=windows[gen][1] and all(a[0]<b[0] for a,b in zip(values,values[1:])),'source exceeded bounded ordered window')


def validate_retirements(events):
    guards=[r for r in events if r['name']=='retirement' and r['label'] in ('guard','seek_guard','cancel_guard')]
    terminal={0,0x40002,0x40003,0x80004004}
    previous=-1
    for guard in guards:
        require(guard['a']==guard['c']=='1' and guard['d']=='0','sample retirement guard failed')
        calls=[r for r in events if r['name']=='call' and r['session']==guard['session'] and previous<int(r['index'])<int(guard['index'])]
        seek=guard['label']=='seek_guard'
        names=('lav_transport_decommit','seek_stop','state_stopped','seek_abort','seek_settled') if seek else ('lav_transport_cleanup_decommit','cleanup_stop','cleanup_stream_stop','cleanup_state','cleanup_abort','cleanup_settled')
        paired=[]
        for name in names:
            found=[r for r in calls if r['label']==name]
            require(found,'retirement public call absent: '+name);paired.append(found[-1])
        require(all(int(a['index'])<int(b['index']) and int(a['end'])<=int(b['begin']) for a,b in zip(paired,paired[1:])) and int(paired[-1]['end'])<=int(guard['begin']),'retirement public call order differs')
        require(all(r['hr']=='00000000' for r in paired[:-2]),'retirement Stop/state/decommit not exact S_OK')
        for r,key in zip(paired[-2:],'ef'):
            value=int(r['hr'],16)
            require(value in terminal and value==(int(guard[key])&0xffffffff),'sample retirement status not terminal/paired')
        previous=int(guard['index'])


def validate_failed_source(events):
    errors=[r for r in events if r['name']=='source_error' and r['label']=='published']
    require(len(errors)==1 and errors[0]['instance']=='0' and errors[0]['session']=='2' and errors[0]['a']=='1' and int(errors[0]['hr'],16)>=0x80000000,'missing real expected Load failure')
    calls=[r for r in events if r['name']=='call' and r['session']=='2']
    loads=[r for r in calls if r['label']=='lav_load']
    require(len(loads)==1 and loads[0]['hr']==errors[0]['hr'] and int(loads[0]['end'])<=int(errors[0]['begin']),'source failure not bound to actual Load')
    facts=[r for r in events if r['name']=='facts' and r['session']=='2']
    require(len(facts)==1 and facts[0]['a']=='1' and all(facts[0][k]=='0' for k in 'bcdef') and facts[0]['hr']==errors[0]['hr'],'failed Load was not preconnection/never-started')
    forbidden={'lav_connect_compressed','lav_connect_rgb','lav_get_transport_allocator','lav_get_terminal_allocator','stream_run','run','create_sample','update','completion'}
    require(not any(r['label'] in forbidden for r in calls),'partial guard contradicts public construction calls')
    partial=[r for r in events if r['name']=='retirement' and r['label']=='preconnection']
    require(len(partial)==1 and partial[0]['session']=='2' and partial[0]['a']=='1' and partial[0]['d']=='0','partial construction retirement unproved')
    for name in ('partial_stop','partial_stream_stop','partial_state'):
        found=[r for r in calls if r['label']==name]
        require(len(found)==1 and found[0]['hr']=='00000000' and int(facts[0]['end'])<=int(found[0]['begin'])<=int(found[0]['end'])<=int(partial[0]['begin']),'partial public Stop/state failed')
    require(not any(r['name']=='slot' and r['session']=='2' for r in events),'negative source acquired CPU storage')


def validate_service_identities(groups):
    identities=[]
    for ident in worker_instances(groups):
        events=groups[('worker',ident)]
        service=[r for r in events if r['name']=='service' and r['label']=='ready']
        observed=[r for r in events if r['name']=='dd_identity']
        require(len(service)==1 and observed and int(service[0]['a'])!=0 and all(r['b']==r['c']==service[0]['a'] and r['d']=='1' and r['e']==r['f']=='0' for r in observed),'private DD observation absent')
        identities.append(service[0]['a'])
        coop=[r for r in events if r['name']=='cooperative']
        require(len(coop)==1 and coop[0]['a']==str(0x408) and coop[0]['b']==events[0]['thread'] and coop[0]['hr']=='00000000','worker cooperative window/flags differ')
    require(len(set(identities))==len(identities),'workers shared canonical DD identity')


def validate_graph_clocks(events):
    observed=[r for r in events if r['name']=='clock'];calls=[r for r in events if r['name']=='call']
    positions=[r for r in events if r['name']=='seek_position' and r['label']=='integer']
    expected=[('setup','0')]+[(name,r['a']) for r in positions for name in ('after_seek','after_run')]
    require([(r['label'],r['a']) for r in observed]==expected and all(r['b']=='1' and r['hr']=='00000000' for r in observed),'NULL graph clock observations incomplete')
    getters=[r for r in calls if r['label']=='get_sync_source'];setters=[r for r in calls if r['label']=='set_sync_source']
    require(len(setters)==1 and setters[0]['hr']=='00000000' and len(getters)==len(observed) and int(setters[0]['end'])<=int(getters[0]['begin']),'explicit NULL graph clock setup absent')
    require(all(c['hr']=='00000000' and int(c['end'])<=int(r['begin']) for c,r in zip(getters,observed)),'NULL clock getter not paired')
    for r in positions:
        after_seek=next(x for x in observed if x['label']=='after_seek' and x['a']==r['a'])
        require(int(r['end'])<=int(after_seek['begin']),'clock observed before actual seek')


def validate_workers(rows,groups):
    validate_service_identities(groups);summary=[]
    for ident in worker_instances(groups):
        tail=tail_mode(rows);negative=ident==0 and not tail;positive_sessions=('1','2') if tail else ('1',);sessions_expected=('1','2') if tail or negative else ('1',)
        events=groups[('worker',ident)];validate_retirements(events);calls=[r for r in events if r['name']=='call'];names=[r['label'] for r in calls]
        for name in ('CoInitialize','CreateWindow','DirectDrawCreateEx','SetCooperativeLevel','qi_dd','service_dd_identity','release_dd_identity','release_dd','release_dd7','destroy_worker_window','CoUninitialize'):
            require(names.count(name)==1,'worker service lifetime differs: '+name)
        operations=[r for r in events if r['name']=='operation']
        service=next(r for r in operations if r['label']=='service_initialize')
        take=next(r for r in groups[('main',2)] if int(r['instance'])==ident and r['name']=='service' and r['label']=='start') if shared_transport(rows) else next(r for r in events if r['name']=='command' and r['a']=='1')
        first_heartbeat=next(r for r in groups[('main',2)] if r['name']=='heartbeat')
        require(int(service['begin'])>=max(int(take['end']),int(first_heartbeat['end'])),'cold service prewarmed before heartbeat/start')
        for r in calls:
            if r['label'] in ('CoInitialize','CreateWindow','DirectDrawCreateEx','SetCooperativeLevel','qi_dd','service_dd_identity'):
                require(int(service['begin'])<=int(r['begin'])<=int(r['end'])<=int(service['end']),'service creation outside cold operation')
        identities=[r for r in events if r['name']=='dd_identity'];require(len(identities)==len(positive_sessions) and all(r['b']==r['c'] and r['d']=='1' for r in identities),'private DD observation absent')
        sessions=[r for r in events if r['name']=='session'];require([(r['label'],int(r['a'])) for r in sessions]==([('begin',1),('end',1),('begin',2),('end',2)] if tail or negative else [('begin',1),('end',1)]),'source session lifetime differs')
        errors=[r for r in events if r['name']=='source_error' and r['label']=='published']
        require(len(errors)==(1 if negative else 0),'source error crossed instances')
        if negative:validate_failed_source(events)
        failures=[r for r in events if r['name']=='failure'];require(all(negative and r['session']=='2' and r['label']=='lav_load' and int(r['hr'],16)>=0x80000000 for r in failures),'unexpected worker failure')
        guards=[r for r in events if r['name']=='retirement' and r['label']=='guard'];require(len(guards)==len(positive_sessions) and all(r['a']==r['c']=='1' and r['d']=='0' for r in guards),'full sample retirement unproved')
        terminal={0,0x40002,0x40003,0x80004004,-2147467260}
        require(all(int(g[k]) in terminal for g in guards for k in ('e','f')),'pending sample released')
        retained={'release_sample','release_surface','release_ddmedia','release_media','lav_release_transport_allocator','lav_release_transport_input','lav_release_events','lav_release_seeking','lav_release_sink_in','lav_release_decoder_out','lav_release_decoder_in','lav_release_source_out','lav_release_file','lav_release_sink','lav_release_video_settings','lav_release_source_settings','release_control','release_source','release_decoder','release_graph_filter','release_notify','release_graph','release_multi'}
        for session in sessions_expected:
            guard=next(r for r in events if r['name']=='retirement' and r['session']==session and r['label'] in ('guard','preconnection'))
            released=[r for r in calls if r['session']==session and r['label'] in retained]
            required=retained if session in positive_sessions else {'lav_release_file','lav_release_sink','lav_release_video_settings','lav_release_source_settings','release_control','release_source','release_decoder','release_notify','release_graph','release_multi'}
            require({r['label'] for r in released}==required and len(released)==len(required),'retained interface release set incomplete')
            require(all(int(r['begin'])>=int(guard['end']) for r in released),'interfaces released before ownership guard')
            cleanup=next(r for r in events if r['name']=='session_cleanup' and r['session']==session)
            require(cleanup['c']==cleanup['d']=='1' and all(int(r['end'])<=int(cleanup['begin']) for r in released),'session cleanup lacks actual releases')
            if negative:require(cleanup['b']=='3','A advanced session with retained CPU lease')
            contexts=[r for r in calls if r['session']==session and r['label'] in ('lav_deactivate_context','lav_release_context')]
            require([r['label'] for r in contexts]==['lav_deactivate_context','lav_release_context'] and int(contexts[0]['begin'])>=max(int(r['end']) for r in released),'activation context retired before graph interfaces')
            first_control=next(r for r in calls if r['session']==session and r['label']=='qi_control');load=next(r for r in calls if r['session']==session and r['label']=='lav_load')
            require(int(first_control['end'])<=int(load['begin']),'control absent before Load')
            flags=[r for r in events if r['session']==session and r['name']=='notify'];require(flags and [r['label'] for r in flags[:3]]==['initial','enable','configured'] and all(r['c']=='0' and r['hr']=='00000000' for r in flags[1:]),'event notification setup absent')
            first_provider=next(r for r in calls if r['session']==session and r['label']=='lav_create_context')
            require(int(flags[2]['end'])<=int(first_provider['begin']),'event notification enabled after provider construction')
            flag_calls=[r for r in calls if r['session']==session and r['label']=='get_notify_flags']
            observations=[r for r in flags if r['label']!='enable']
            require(len(flag_calls)==len(observations) and all(c['hr']==o['hr']=='00000000' and int(c['end'])<=int(o['begin']) for c,o in zip(flag_calls,observations)),'notification flag readback not paired')
            enable=[r for r in calls if r['session']==session and r['label']=='set_notify_flags']
            require(len(enable)==1 and enable[0]['hr']=='00000000' and int(flags[0]['end'])<=int(enable[0]['begin'])<=int(enable[0]['end'])<=int(flags[1]['begin']),'event notification enable not paired')
            drains=[r for r in events if r['session']==session and r['name']=='drain'];require(drains and drains[-1]['c']=='1' and (shared_transport(rows) or all(r['c']=='1' for r in drains)),'events not drained')
            require(all(int(r['begin'])>=int(drains[-1]['end']) for r in released),'release preceded cleanup event drain')
        for r in calls:
            if int(r['hr'],16)<0x80000000:continue
            require(r['hr']=='80004004' and r['label'] in ('worker_event_poll','cleanup_abort','cleanup_settled','seek_abort','seek_settled') or negative and r['session']=='2' and r['label']=='lav_load','unexpected public API failure')
        service_cleanup=next(r for r in operations if r['label']=='service_cleanup')
        require(int(service_cleanup['begin'])>=int(sessions[-1]['end']),'DD service released with live source session')
        for r in calls:
            if r['label'] in ('release_dd_identity','release_dd','release_dd7','destroy_worker_window','CoUninitialize'):
                require(int(service_cleanup['begin'])<=int(r['begin'])<=int(r['end'])<=int(service_cleanup['end']),'DD service release outside cleanup operation')
        for session in positive_sessions:validate_graph_clocks([r for r in events if r['session']==session])
        for session in positive_sessions:
            surfaces=[r for r in events if r['name']=='surface' and r['session']==session]
            require(len(surfaces)==2 and surfaces[0]['label']=='caps_format' and surfaces[0]['b']==surfaces[0]['c']=='512' and surfaces[0]['e']=='32','source surface contract absent')
            masks=surfaces[1];values=[int(masks[k]) for k in 'abc']
            require(masks['label']=='masks_rect' and masks['e']==masks['f']=='512' and masks['d']=='0' and int(surfaces[0]['d'])>=2048 and int(surfaces[0]['f'])&0x40 and not int(surfaces[0]['f'])&4 and all(values) and not(values[0]&values[1] or values[0]&values[2] or values[1]&values[2]),'source RGB masks/pitch/rectangle differ')
        summary.append(dict(instance=ident,thread=events[0]['thread'],sessions=len(sessions_expected)))
    return summary


def normalized_drive_path(encoded):
    """Only remove the documented extended drive prefix; preserve the full path."""
    from urllib.parse import unquote
    value=unquote(encoded)
    if len(value)>=7 and value[:4]=='\\\\?\\' and value[4] in 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz' and value[5:7]==':\\':
        value=value[4:]
    return value.lower()


def validate_provider_session(rows,graph_directory):
    # Retain all existing provider predicates, normalizing only path spelling.
    # Re-encode literal percent signs before the retained parser's one unquote.
    from urllib.parse import quote
    normalized={name:[dict(r) for r in values] for name,values in rows.items()}
    for row in normalized.get('MP_LAV_ASSEMBLY_MODULE',[]):
        for key in ('path','expected'):
            row[key]=quote(normalized_drive_path(row[key]),safe='\\:')
    worker.validate_provider_session(normalized,graph_directory)


def validate_package(rows,groups,record):
    from urllib.parse import unquote
    h=worker.one(rows,'MC_HEADER');selection=record.get('package_config',{})
    require(h.get('package_config')=='1' and selection.get('enabled') and selection.get('source_id')==2 and selection.get('effective_flags')==8,'actual package reader qualification absent')
    values=rows.get('MC_PACKAGE',[])
    def one(kind):
        found=[r for r in values if r.get('kind')==kind];require(len(found)==1,'package observation missing/duplicate: '+kind);return found[0]
    read,paths,transfer,released=(one(name) for name in ('read','paths','owners_transferred','owners_released'))
    require(len(values)==4 and read['status']==read['error']==read['system']=='0' and read['module_relative']=='1' and int(read['module']) and int(read['owner']) and read['thread']!=h['main_thread'] and int(read['begin'])<=int(read['end']),'package preparation did not run successfully off main')
    runtime=record['runtime_paths']
    def windows(path):return 'z:'+str(path).replace('/','\\').lower()
    require(paths['id']=='2' and paths['flags']=='8' and normalized_drive_path(paths['manifest'])==windows(runtime['manifest']) and normalized_drive_path(paths['media'])==windows(runtime['media']),'actual package paths differ from byte-bound runtime inputs')
    starts=[r for r in groups[('main',2)] if r['name']=='service' and r['label']=='start']
    first=next(r for r in groups[('main',2)] if r['name']=='heartbeat')
    require(int(first['end'])<=int(read['begin']) and all(int(read['end'])<=int(r['begin'])<=int(r['end'])<=int(transfer['qpc']) and r['a']==read['module'] and r['b']==read['owner'] for r in starts),'package was not read before service/owner transfer')
    count=len(worker_instances(groups));require(len(starts)==count and transfer['count']==transfer['expected']==str(count) and transfer['alive']=='1','package pins not owned exclusively by service configs')
    for ident in worker_instances(groups):
        own=groups[('worker',ident)]
        for name,label in (('service','ready'),('cleanup','safe_complete')):
            observed=[r for r in own if r['name']==name and r['label']==label]
            require(len(observed)==1 and observed[0]['b']==read['owner'] and int(observed[0]['end'])<=int(released['qpc']),'worker dropped immutable package owner during lifetime')
    exits=[r for r in groups[('main',2)] if r['name']=='worker_exit']
    watch=worker.one(rows,'MC_WATCHDOG')
    require(watch['wait']=='0' and all(int(r['end'])<=int(watch['qpc']) for r in exits) and int(watch['qpc'])<=int(released['qpc']),'package release lacks actual watchdog exit')
    require(len(exits)==count and released['count']=='0' and released['expired']=='1' and all(int(r['end'])<=int(released['qpc']) for r in exits),'package pins released before actual worker retirement')
    return dict(actual_module_relative_reader=True,preparation_thread=read['thread'],read_ms=(int(read['end'])-int(read['begin']))*1000/int(h['qpc_frequency']),same_owner_services=count,identity_pins_retained_through_services=True,pins_released_after_retirement=True)


def validate_shared_commands(rows,groups):
    for ident in worker_instances(groups):
        posts=[r for r in groups[('main',2)] if int(r['instance'])==ident and r['name']=='command']
        takes=[r for r in groups[('worker',ident)] if r['name']=='command']
        require(len(posts)==len(takes),'shared command lost between owners')
        for post,take in zip(posts,takes):
            require(post['label']=='post' and take['label']=='take' and all(post[k]==take[k] for k in 'abcdf') and full_identity(post)==full_identity(take) and int(post['end'])<=int(take['begin']),'shared mailbox payload changed')
            expected_budget=0 if tail_mode(rows) or post['e']=='4294967295' else 9 if ident==1 else 7 if post['b']=='2' else 6
            require(int(take['e'])==expected_budget,'verification pump changed source window')
            expected_handle=int(post['b']) if tail_mode(rows) else 2 if ident==0 and post['e']=='4294967295' else 1
            require(full_identity(post)==(ident,expected_handle,303 if tail_mode(rows) else 202 if ident else 101,int(post['b'])),'command full identity differs')
            publications=[r for r in groups[('main',2)] if int(r['instance'])==ident and r['name']=='desired' and r['label']=='publish' and int(r['end'])<=int(post['begin'])]
            require(publications and full_identity(publications[-1])==full_identity(post) and publications[-1]['b']=='1' and publications[-1]['c']==post['f'],'command does not match latest coherent desired tuple')


def validate_initial_quiescence(groups):
    main=groups[('main',2)]
    for ident in worker_instances(groups):
        own=groups[('worker',ident)];local=[r for r in main if int(r['instance'])==ident]
        first_command=next(r for r in local if r['name']=='command')
        before=[r for r in local if int(r['end'])<int(first_command['begin'])]
        pubs=[r for r in before if r['name']=='desired' and r['label']=='publish']
        require(len(pubs)==4 and [p['d'] for p in pubs]==['1','2','3','4'] and [p['b'] for p in pubs]==['0','1','0','1'] and all(p['c']=='0' for p in pubs[:3]),'initial quiescence desired serial/live protocol differs')
        facts=[r for r in own if r['name']=='desired' and r['label']=='assignment_quiescent' and int(r['end'])<int(first_command['begin'])]
        observed=[r for r in before if r['name']=='quiescence' and r['label']=='observed']
        refused=[r for r in before if r['name']=='quiescence' and r['label']=='old_fact_refused']
        require(len(facts)==len(observed)==2 and len(refused)==1 and refused[0]['a']=='2' and refused[0]['b']=='1','initial worker quiescence/new-publication refusal absent')
        ready=next(r for r in own if r['name']=='service' and r['label']=='ready')
        for index,serial in enumerate(('1','3')):
            fact,ack,pub=facts[index],observed[index],pubs[index*2]
            require(fact['a']==ack['a']==serial and fact['b']==fact['c']=='0' and fact['d']==ack['d']=='3' and fact['session']==ack['session']=='0' and full_identity(fact)==full_identity(ack)==full_identity(pub) and int(pub['end'])<=int(fact['begin'])<=int(fact['end'])<=int(ack['begin']) and int(ready['end'])<=int(fact['begin']),'quiescence fact not bound to actual canceled assignment')
            require(not any(r['name'] in ('session','slot','command','worker_event') and int(r['end'])<=int(ack['end']) for r in own+local),'no-graph cancellation witness already performed source work')
        require(int(observed[0]['end'])<=int(pubs[1]['begin'])<=int(refused[0]['begin'])<=int(refused[0]['end'])<=int(pubs[2]['begin']) and int(observed[1]['end'])<=int(pubs[3]['begin']),'old fact reused across publication renewal')
        heartbeats=[r for r in main if r['name']=='heartbeat' and int(pubs[0]['begin'])<=int(r['begin']) and int(r['end'])<=int(first_command['begin'])]
        require(len(heartbeats)>=2,'main heartbeat absent during quiescence polling')
    return dict(initial_no_graph_services=2,canceled_facts=4,old_facts_refused_after_live_publication=2,physical_slots_free_per_service=3,graph_retirement_quiescence_runtime_exercised=False)


def validate_shared_contract(rows,groups):
    """Additional witnesses for the actual reusable transport, not copied flags."""
    validate_shared_commands(rows,groups)
    for ident in worker_instances(groups):
        events=groups[('worker',ident)]
        starts=[r for r in groups[('main',2)] if int(r['instance'])==ident and r['name']=='service' and r['label']=='start']
        require(len(starts)==1 and int(starts[0]['a'])!=0,'service start/module pin observation absent')
        successful=[r for r in events if r['name']=='call' and r['label']=='support_seeking']
        positive_sessions=('1','2') if tail_mode(rows) else ('1',)
        require(len(successful)==len(positive_sessions),'shared renderer completion forwarding absent')
        for session in positive_sessions:
            supports=[r for r in successful if r['session']==session]
            graph_run=next(r for r in events if r['session']==session and r['name']=='call' and r['label']=='stream_run')
            require(len(supports)==1 and supports[0]['hr']=='00000000' and int(supports[0]['end'])<=int(graph_run['begin']),'SupportSeeking missing or enabled after graph Run')
        first_run=next(r for r in events if r['name']=='call' and r['label']=='stream_run')
        # The snapshot enumerates every module, and each required basename occurs
        # exactly once at the configured path. Per-filter basename lookup alone
        # cannot establish this once-per-service cohort condition.
        cohort=[r for r in rows.get('MP_LAV_COHORT',[]) if r['instance']==str(ident)]
        require(len(cohort)==9 and {int(r['index']) for r in cohort}==set(range(9)) and all(r['session']=='1' and r['count']==r['same']=='1' and r['path'].lower()==r['expected'].lower() for r in cohort),'unique normalized provider cohort absent')
        snapshots=[r for r in events if r['name']=='call' and r['label']=='provider_module_snapshot']
        first=[r for r in events if r['name']=='call' and r['label']=='provider_module_first']
        nexts=[r for r in events if r['name']=='call' and r['label']=='provider_module_next']
        closes=[r for r in events if r['name']=='call' and r['label']=='provider_snapshot_close']
        require(snapshots and snapshots[-1]['hr']=='00000000' and len(first)==len(closes)==1 and first[0]['hr']==closes[0]['hr']=='00000000' and nexts and nexts[-1]['hr']=='00000001' and all(r['hr']=='00000000' for r in nexts[:-1]),'module snapshot incomplete')
        require(int(snapshots[-1]['end'])<=int(first[0]['begin'])<=int(nexts[-1]['end'])<=int(closes[0]['begin'])<=int(closes[0]['end'])<=int(first_run['begin']),'cohort was not checked before Run')
        validate_position_stop_pairs(events)
        current=None
        for row in events:
            if row['name']=='command':current=full_identity(row)
            if row['name']=='slot' and row['label'] in ('writing','ready'):
                require(current is not None and full_identity(row)==current and int(row['b'])==current[3],'pixel publication lost full command identity')
        observed=[r for r in groups[('main',2)] if int(r['instance'])==ident and r['name']=='worker_event']
        terminal=[r for r in observed if int(r['a'])!=1]
        require(terminal and all(int(r['a'])&32==0 for r in terminal),'unsafe service result')
        require(all(int(a['b'])<int(b['b']) for a,b in zip(terminal,terminal[1:])),'terminal revision lost/regressed')
        prior={}
        for row in terminal:
            key=full_identity(row);flags=int(row['a']);previous=prior.get(key)
            if previous:require(flags|int(previous['a'])==flags and (int(previous['c'])>=0 or row['c']==previous['c']),'terminal fact disappeared')
            prior[key]=row
        final=terminal[-1];require(int(final['a'])&24==24 and final['d']==final['e']==final['f']=='1','physical service retirement not observed')
        if ident==0 and not tail_mode(rows):
            errors=[r for r in events if r['name']=='source_error' and r['label']=='published']
            require(len(errors)==1 and int(final['a'])&4 and int(final['c'])&0xffffffff==int(errors[0]['hr'],16),'Load failure lost in terminal channel')
        else:require(not int(final['a'])&4 and final['c']=='0','peer worker failed')


def validate_position_stop_pairs(events):
    before=[r for r in events if r['name']=='seek_position' and r['label']=='before']
    after=[r for r in events if r['name']=='seek_position' and r['label']=='after']
    positions=[r for r in events if r['name']=='seek_position' and r['label']=='integer']
    calls=[r for r in events if r['name']=='call' and r['label']=='integer_seek']
    b_calls=[r for r in events if r['name']=='call' and r['label']=='positions_before_seek']
    a_calls=[r for r in events if r['name']=='call' and r['label']=='positions_after_seek']
    require(len(before)==len(after)==len(positions)==len(calls)==len(b_calls)==len(a_calls) and positions,'integer seek stop observations absent')
    for b,a,p,c,bc,ac in zip(before,after,positions,calls,b_calls,a_calls):
        require(b['hr']==a['hr']==c['hr']==bc['hr']==ac['hr']=='00000000' and b['a']==a['a']==p['a'] and b['c']==a['c'] and int(a['c'])>int(p['b']),'source stop changed during seek')
        require(int(bc['end'])<=int(b['begin'])<=int(b['end'])<=int(c['begin'])<=int(c['end'])<=int(ac['begin'])<=int(ac['end'])<=int(a['begin'])<=int(p['begin']),'stop getters do not bracket actual integer seek')


def validate_shared_anchors(events,main):
    previous_call=None;pending_frame=None;writing=None
    for r in events:
        if r['name']=='call':previous_call=r
        if r['name']=='slot' and r['label']=='writing':writing=r
        if r['name']=='deadline_anchor':
            label=r['label']
            if label=='frame':
                require(previous_call and previous_call['label'] in ('update','completion') and previous_call['hr']=='00000000' and int(previous_call['end'])<=int(r['begin']) and full_identity(previous_call)==full_identity(r),'frame deadline lacks actual successful completion')
                pending_frame=r
            elif label=='sample_request':
                require(writing and writing['b']==r['b'] and writing['c']==r['c'] and full_identity(writing)==full_identity(r) and int(writing['end'])<=int(r['begin']),'request deadline lacks newly acquired WRITING slot')
                following=events[int(r['index'])+1:]
                later=next((c for c in following if c['name']=='call'),None)
                if not(later and later['label']=='update'):
                    canceled=next((c for c in following if c['name']=='desired' and c['label']=='cancelled_before_call'),None)
                    guard=next((c for c in following if c['name']=='retirement' and c['label'] in ('guard','cancel_guard')),None)
                    publications=[p for p in main if canceled and p['name']=='desired' and p['label'] in ('publish','shutdown') and p['instance']==r['instance'] and int(p['end'])<=int(canceled['begin'])]
                    latest=max(publications,key=lambda p:int(p['end'])) if publications else None
                    revoked=latest and (latest['label']=='shutdown' or latest['b']=='0' or full_identity(latest)!=full_identity(r))
                    require(canceled and later and guard and revoked and full_identity(canceled)==full_identity(r)==full_identity(guard) and int(r['end'])<=int(canceled['begin'])<=int(canceled['end'])<=int(later['begin'])<=int(guard['begin']) and guard['a']=='1','request deadline lacks actual pre-call cancellation and safe retirement')
                else:
                    require(int(r['end'])<=int(later['begin']),'request deadline was reset during pending polling')
                writing=None
            elif label in ('pending_command','desired_cancel','shutdown'):
                name='command' if label=='pending_command' else 'desired'
                postlabel='post' if label=='pending_command' else 'publish' if label=='desired_cancel' else 'shutdown'
                candidates=[p for p in main if p['name']==name and p['label']==postlabel and p['begin']==r['d'] and (p['d'] if name=='command' else p['a'])==r['a'] and int(p['end'])<=int(r['begin'])]
                require(len(candidates)==1,'cleanup anchor does not bind immutable actual post')
            else:raise ValueError('unknown shared deadline anchor '+label)
        if r['name']=='slot' and r['label']=='ready':
            require(pending_frame and full_identity(pending_frame)==full_identity(r) and pending_frame['b']==r['b'] and pending_frame['c']==r['c'],'frame deadline generation/sequence differs');pending_frame=None


def validate_events(rows,groups):
    summaries=[]
    for ident in worker_instances(groups):
        events=groups[('worker',ident)];posts=[r for r in groups[('main',2)] if int(r['instance'])==ident and r['name']=='command']
        takes=[r for r in events if r['name']=='command'];require(len(posts)==len(takes),'command lost between owners')
        if shared_transport(rows):validate_shared_commands(rows,groups)
        else:
            for post,take in zip(posts,takes):require(all(post[k]==take[k] for k in 'abcdef') and int(post['end'])<=int(take['begin']),'mailbox payload changed')
        anchors=[r for r in events if r['name']=='command' or r['name']=='deadline_anchor'];batches=[r for r in events if r['name']=='event_batch']
        raw=[r for r in events if r['name']=='provider_event'];polls=[r for r in events if r['name']=='call' and r['label']=='worker_event_poll'];frees=[r for r in events if r['name']=='call' and r['label']=='worker_event_free']
        require(len(raw)==len(frees) and all(r['hr']=='00000000' for r in frees),'retrieved event not freed exactly once')
        require(len(raw)==sum(r['hr']=='00000000' for r in polls),'raw event/poll count differs')
        seen=set();batch_groups=defaultdict(list);flag_groups=defaultdict(list)
        for batch in batches:batch_groups[(batch['session'],batch['a'])].append(batch)
        for flag in events:
            if flag['name']=='notify' and flag['label']=='before_drain':flag_groups[(flag['session'],flag['b'])].append(flag)
        anchor_ends=[int(a['end']) for a in anchors]
        if shared_transport(rows):validate_shared_anchors(events,[r for r in groups[('main',2)] if int(r['instance'])==ident])
        else:
            previous_call=None;pending_frame=None
            for r in events:
                if r['name']=='call':previous_call=r
                elif r['name']=='deadline_anchor':
                    require(previous_call and previous_call['label'] in ('update','completion') and previous_call['hr']=='00000000' and int(previous_call['end'])<=int(r['begin']),'frame deadline lacks actual successful completion')
                    pending_frame=r
                elif r['name']=='slot' and r['label']=='ready':
                    require(pending_frame and pending_frame['session']==r['session'] and pending_frame['b']==r['b'] and pending_frame['c']==r['c'],'frame deadline generation/sequence differs');pending_frame=None
        for drain in (r for r in events if r['name']=='drain'):
            anchor_index=bisect_right(anchor_ends,int(drain['begin']))-1;anchor=anchors[anchor_index] if anchor_index>=0 else None;require(anchor is not None,'drain deadline anchor absent')
            tick=anchor['d'] if anchor['name']=='command' else anchor['a'];require(drain['d']==tick,'drain deadline reset')
            flags=flag_groups[(drain['session'],drain['b'])];require(len(flags)==1 and flags[0]['c']=='0' and int(flags[0]['end'])<=int(drain['begin']),'unobserved event queue flags')
            included=batch_groups[(drain['session'],drain['b'])];require(len(included)==int(drain['f']) and included,'missing event batch')
            total=0
            for ordinal,batch in enumerate(included,1):
                require(batch['d']==tick and int(batch['b'])==ordinal and ((int(batch['e'])-int(tick))&0xffffffff)<10000,'event deadline or ordinal differs')
                if shared_transport(rows):require(batch['f']=='0','production drain depends on diagnostic trace capacity')
                else:require(int(batch['f'])==131072-int(batch['index']) and int(batch['f'])>=512,'retirement trace reserve exhausted')
                cancelled=shared_transport(rows) and batch['label']=='Cancelled'
                require(cancelled and batch['hr']=='800704c7' or batch['label'] in ('More','Empty') and batch['hr']=='00000000','event drain failed')
                if cancelled:
                    revocations=[p for p in groups[('main',2)] if int(p['instance'])==ident and p['name']=='desired' and int(p['end'])<=int(batch['end']) and (p['label']=='shutdown' or p['label']=='publish' and (p['b']=='0' or full_identity(p)!=full_identity(batch)))]
                    require(revocations,'drain cancellation lacks actual revocation')
                begin,end=int(batch['begin']),int(batch['end'])
                items=[r for r in events[int(included[ordinal-2]['index'])+1 if ordinal>1 else int(flags[0]['index'])+1:int(batch['index'])] if begin<=int(r['begin'])<=int(r['end'])<=end and (r['name']=='provider_event' or r['name']=='call' and r['label'] in ('worker_event_poll','worker_event_free'))]
                count=0;position=0;empty=False
                while position<len(items):
                    poll=items[position];position+=1;seen.add(id(poll));require(poll['label']=='worker_event_poll','event read/free pairing differs')
                    if poll['hr']=='80004004':require(position==len(items),'poll after Empty');empty=True;break
                    require(poll['hr']=='00000000' and position+1<len(items),'event poll failed or unfreed')
                    event,freed=items[position:position+2];position+=2;seen.update((id(event),id(freed)));count+=1;total+=1
                    require(event['name']=='provider_event' and event['a']==drain['b'] and int(event['b'])==total and freed['label']=='worker_event_free' and freed['hr']=='00000000','event scalar/free sequence differs')
                    code=int(event['c']);require(tail_mode(rows) or code!=1,'unexpected physical EOF in bounded source window')
                    if code in worker.FATAL_EVENTS:
                        facts=[r for r in events if r['name']=='facts' and r['session']=='2']
                        match=[r for r in events if r['name']=='source_error' and r['label']=='attributable_event' and int(freed['end'])<=int(r['begin'])<=end and r['a']==event['c'] and r['b']==event['d'] and r['c']==event['e']]
                        require(ident==0 and event['session']=='2' and code==3 and facts and int(event['d'])&0xffffffff==int(facts[0]['hr'],16) and len(match)==1,'unattributable asynchronous error')
                require(count==int(batch['c']) and count<=32 and (not count and not empty if cancelled else empty if batch['label']=='Empty' else count==32 and not empty),'false Empty/More batch')
            cancelled=shared_transport(rows) and included[-1]['label']=='Cancelled'
            require((cancelled and drain['c']=='0' or included[-1]['label']=='Empty' and drain['c']=='1') and total==int(drain['e']),'logical drain incomplete')
            if cancelled:
                later=[r for r in events if r['name']=='drain' and int(r['end'])>int(drain['end']) and r['session']==drain['session'] and r['label'] in ('cancel_stopped','cleanup_stopped') and r['c']=='1']
                require(later,'cancelled drain was not followed by stopped retirement drain')
            summaries.append(dict(instance=ident,session=int(drain['session']),site=drain['label'],events=total,batches=len(included)))
        require(seen=={id(r) for r in raw+polls+frees},'unbound provider event')
    return summaries


def validate_render(rows,selected,output,expected):
    events=rows['MC_EVENT'];captures=rows.get('MC_CAPTURE',[]);result=worker.one(rows,'MC_RESULT')
    require(0<len(captures)<=40 and len(captures)==len(selected)==int(result['captures']),'selected capture count differs')
    require(len(list(Path(output).glob('*.bgra')))==len(captures),'unexpected capture artifact')
    textures=[r for r in events if r['name']=='texture'];require(len(textures)==2 and {r['instance'] for r in textures}=={'0','1'} and len({r['a'] for r in textures})==2 and all(int(r['a']) for r in textures),'destinations not distinct')
    texture={int(r['instance']):r['a'] for r in textures}
    heartbeats=[r for r in events if r['name']=='heartbeat'];require(heartbeats and all(r['c']=='1' and r['hr']=='00000000' for r in heartbeats),'actual draw/Present heartbeat failed')
    setup=[r for r in events if r['name']=='render_setup'];require(len(setup)==1 and setup[0]['label']=='point_1to1' and setup[0]['a']=='1024' and setup[0]['b']=='512' and setup[0]['c']=='21' and setup[0]['d']=='2','required exact render/readback configuration absent')
    graphics=[r for r in events if r['name']=='graphics'];require(all(r['hr']=='00000000' for r in graphics),'graphics API failure')
    readbacks=[r for r in graphics if r['label']=='readback_transfer'];require(len(readbacks)<=52 and len(readbacks)==int(result['readbacks']) and int(result['transition_readbacks'])<=12,'readback bound differs')
    regions=[r for r in events if r['name']=='render_region'];require(all(r['d']=='1' for r in regions),'held destination changed')
    by_selection={(int(r['instance']),int(r['tick'])):r for r in selected};hashes=[]
    draws=defaultdict(list);region_map=defaultdict(list);graphics_map=defaultdict(list)
    for r in events:
        if r['name']=='draw':draws[(int(r['instance']),int(r['a']))].append(r)
        elif r['name']=='render_region':region_map[(int(r['instance']),int(r['a']))].append(r)
        elif r['name']=='graphics':graphics_map[(r['label'],int(r['a']))].append(r)
    readback_ticks={int(r['a']) for r in readbacks}
    def stage(name,tick):
        found=graphics_map[(name,tick)];require(len(found)==1,'missing/duplicate graphics stage '+name);return found[0]
    for index,capture in enumerate(captures):
        require(int(capture['index'])==index and capture['file']==f'clock-f{index:02}.bgra' and capture['bytes']==str(FRAME_BYTES) and capture['written']=='1','capture identity or bound differs')
        ident,tick=int(capture['instance']),int(capture['tick']);selected_frame=by_selection.get((ident,tick));require(selected_frame,'snapshot lacks real clock selection')
        require(capture['generation']==selected_frame['frame_generation'] and int(capture['sequence'])+1==int(selected_frame['token']) and capture['start']==selected_frame['start'] and capture['end']==selected_frame['stop'],'rendered frame differs from clock token')
        draw=draws[(ident,tick)]
        require(len(draw)==1 and draw[0]['b']==texture[ident] and int(draw[0]['c'])==index and draw[0]['hr']=='00000000','capture was not sampled from its destination')
        upload=[next((r for r in graphics_map[(name,tick)] if int(r['instance'])==ident and r['b']==capture['sequence']),None) for name in ('texture_lock','texture_rows','texture_unlock')]
        require(all(upload) and int(selected_frame['qpc'])<=int(upload[0]['begin']) and all(int(a['end'])<=int(b['begin']) for a,b in zip(upload,upload[1:])) and int(upload[-1]['end'])<=int(draw[0]['begin']),'selected texture write not completed before draw')
        observed=[r for r in region_map[(ident,tick)] if int(r['b'])==index and r['c']=='1'];require(len(observed)==1,'new selected rendered region absent')
        sequence=[stage(name,tick) for name in ('end_scene','readback_transfer','readback_lock','snapshot_compare','readback_unlock','compose_present')]
        require(all(sequence) and int(draw[0]['end'])<=int(sequence[0]['begin']) and all(int(a['end'])<=int(b['begin']) for a,b in zip(sequence,sequence[1:])),'draw/readback/presentation ordering differs')
        path=Path(output)/capture['file'];require(path.is_file() and not path.is_symlink() and path.stat().st_size==FRAME_BYTES,'missing/oversized selected snapshot')
        raw=path.read_bytes();reference=expected.get((int(capture['start']),int(capture['end'])));require(reference is not None,'selected source interval outside frozen oracle')
        require(raw[3::4]==bytes([255])*(FRAME_BYTES//4) and worker.rgb_digest(raw)==reference['rgb_sha256'],'rendered region exact RGB/alpha differs')
        hashes.append(hashlib.sha256(raw).hexdigest())
    if tail_mode(rows):require({r['instance'] for r in captures}=={'0'},'EOF tail rendered an absent/extra instance')
    # Every normal heartbeat samples each initialized texture; every readback also
    # observes its unchanged peer, even after pause/end/failure.
    for ident in ((0,) if tail_mode(rows) else (0,1)):
        own=[r for r in captures if int(r['instance'])==ident];require(own,'instance never acquired rendered destination')
        for hb in heartbeats:
            tick=int(hb['a']);available=[c for c in own if int(c['tick'])<=tick]
            if not available:continue
            current=max(available,key=lambda c:int(c['tick']))
            draw=draws[(ident,tick)]
            require(len(draw)==1 and draw[0]['b']==texture[ident] and draw[0]['c']==current['index'],'last texture not retained/drawn')
            if tick in readback_ticks:
                require(any(r['b']==current['index'] for r in region_map[(ident,tick)]),'readback omitted unchanged peer')
    return dict(capture_sha256=hashes,actual_two_destinations_sampled={int(r['instance']) for r in captures}=={0,1},held_regions_verified=True,readbacks=len(readbacks))


def validate_protocol(rows,groups):
    """Fixed phase inputs; runtime coverage may still honestly be incomplete."""
    plans={0:[(1,1,0,6,1),(2,2,22000000,7,1),(2,4,0,6,0),(5,4,0,0,0)],
           1:[(1,1,100000000,9,1),(2,2,100000000,9,1),(2,3,100000000,9,1),(3,4,0,0,0)]}
    for ident in (0,1):
        commands=[r for r in groups[('main',2)] if r['name']=='command' and int(r['instance'])==ident]
        if shared_transport(rows):
            native={0:[(1,1,0,2,1),(2,2,22000000,2,1),(2,4,0,2,0),(1,4,0,4294967295,0)],1:[(1,1,100000000,2,1),(2,2,100000000,2,1),(2,3,100000000,2,1)]}
            require([tuple(int(r[k]) for k in 'abcef') for r in commands]==native[ident],'fixed shared worker command plan changed')
            shutdowns=[r for r in groups[('main',2)] if int(r['instance'])==ident and r['name']=='desired' and r['label']=='shutdown']
            require(len(shutdowns)==1,'service shutdown intent absent')
            if ident==1:
                stops=[r for r in rows['MC_CLOCK'] if r['instance']=='1' and r['name']=='stop']
                require(len(stops)==1 and int(stops[0]['qpc'])<=int(shutdowns[0]['begin']),'B shutdown before positive stop')
        else:require([tuple(int(r[k]) for k in 'abcef') for r in commands]==plans[ident],'fixed worker command plan changed')
        positions=[r for r in groups[('worker',ident)] if r['name']=='seek_position' and r['label']=='integer']
        seeks=[p for p in plans[ident] if p[0] in (1,2)]
        require([(int(r['a']),int(r['b']),int(r['c'])) for r in positions]==[(p[1],p[2],p[2]) for p in seeks],'actual public integer seeks differ')
        own=[r for r in rows['MC_CLOCK'] if int(r['instance'])==ident]
        controls=[(r['name'],tuple(int(r[k]) for k in 'abcd')) for r in own if r['name'] not in ('update','submit')]
        expected=[('begin',(101,0,280,0)),('pause',(0,0,0,0)),('seek',(22000000,2480,0,0)),('rate',(50000,0,0,0)),('resume',(0,0,0,0))] if ident==0 else [('begin',(202,100000000,10360,1)),('rate',(50000,0,0,0)),('restart',(0,0,0,0)),('restart',(0,0,0,0)),('stop',(0,0,0,0))]
        if ident==0:
            # Missing the fast-rate selection is scoped as incomplete coverage;
            # changing any actual input or adding an extra epoch is a failure.
            if any(r['name']=='rate' and r['a']=='200000' for r in own):expected.append(('rate',(200000,0,0,0)))
            expected.extend([('stop',(0,0,0,0)),('seek',(0,280,0,0))])
        require(controls==expected,'clock operation/rate/epoch protocol changed')
        epoch_controls=[r for r in own if r['name'] in ('begin','seek','restart')]
        source_commands=[r for r in commands if r['e']=='2'] if shared_transport(rows) else [r for r in commands if r['a'] in ('1','2')]
        require(len(epoch_controls)==len(source_commands) and all(r['generation']==c['b'] and int(r['qpc'])<=int(c['begin']) for r,c in zip(epoch_controls,source_commands)),'source command preceded clock epoch invalidation')
        if ident==1:
            prior=None
            for r in own:
                if r['name']=='restart':require(prior and prior['name']=='update' and prior['end']=='1' and int(r['tick'])==int(prior['tick'])+1,'loop restart not next manager pass')
                prior=r


def coverage(rows,selected,groups):
    clocks=rows['MC_CLOCK'];events=rows['MC_EVENT'];missing=[]
    def cover(name,condition):
        if not condition:missing.append(name)
    by={i:[r for r in clocks if int(r['instance'])==i] for i in (0,1)}
    a,b=by[0],by[1];sa=[r for r in selected if r['instance']=='0'];sb=[r for r in selected if r['instance']=='1']
    cover('both_cold_first_selections',any(r['frame_generation']=='1' for r in sa) and any(r['frame_generation']=='1' for r in sb))
    pauses=[r for r in a if r['name']=='pause'];seeks=[r for r in a if r['name']=='seek' and r['a']=='22000000']
    paused=[r for r in a if r['name']=='update' and r['paused']=='1'];backlogs=[r for r in events if r['name']=='phase' and r['label']=='paused_three_leases']
    actual_backlog=False
    if len(backlogs)==1:
        phase=backlogs[0];leased=set()
        for r in sorted((r for r in events if r['owner']=='main' and r['instance']=='0' and r['name']=='slot' and int(r['end'])<=int(phase['begin'])),key=lambda r:int(r['begin'])):
            key=(r['d'],r['b'],r['c'],r['a'])
            if r['label']=='reading':leased.add(key)
            elif r['label']=='free':leased.discard(key)
        latest=next((r for r in reversed(paused) if int(r['qpc'])<=int(phase['begin'])),None)
        actual_backlog=len(leased)==3 and latest and latest['queued']=='3' and all(k[1]==latest['generation'] for k in leased)
    cover('A_genuine_paused_three_leases',len(backlogs)==1 and backlogs[0]['a']=='3' and actual_backlog)
    pre_seek_paused=[r for r in paused if pauses and seeks and int(pauses[0]['qpc'])<int(r['qpc'])<int(seeks[0]['qpc'])]
    cover('A_paused_position_unchanged',len({r['qpc'] for r in pre_seek_paused})>=2 and len({r['numerator'] for r in pre_seek_paused})==1)
    cover('B_fresh_while_A_paused',bool(pauses and seeks and any(int(pauses[0]['qpc'])<int(r['qpc'])<int(seeks[0]['qpc']) for r in sb)))
    cover('A_seek_preserves_pause',len(seeks)==1 and seeks[0]['paused']=='1' and seeks[0]['intent']=='1')
    rates=[r for r in a if r['name']=='rate'];cover('A_both_rate_regimes_selected', [r['a'] for r in rates]==['50000','200000'] and all(any(r['frame_generation']=='2' and int(r['rate'])==rate*FACTOR for r in sa) for rate in (50000,200000)))
    if len(rates)==2:
        threshold=Fraction(22800000);freq=int(worker.one(rows,'MC_HEADER')['qpc_frequency'])
        reaches=[r for r in a if r['name']=='update' and r['generation']=='2' and int(r['numerator'],16)>=threshold*freq*UNIT and r['end']=='0']
        require(reaches and int(reaches[0]['qpc'])<=int(rates[1]['qpc']) and not any(r['name']=='update' and int(reaches[0]['qpc'])<int(r['qpc'])<int(rates[1]['qpc']) for r in a),'A changed fast rate after skipping its actual trigger')
    positives=[r for r in a if r['name']=='update' and r['end']=='1'];cover('A_positive_end',bool(positives))
    stopped=[r for r in a if r['name']=='seek' and r['a']=='0'];cover('A_stopped_seek_inactive',len(stopped)==1 and stopped[0]['intent']==stopped[0]['armed']=='0')
    if stopped:require(not any(int(r['qpc'])>=int(stopped[0]['qpc']) for r in sa),'stopped seek readiness was copied')
    restarts=[r for r in b if r['name']=='restart'];cover('B_three_fixed_loop_epochs',len(restarts)==2 and all(r['operation']=='202' and int(r['rate'])==50000*FACTOR and r['intent']=='1' for r in restarts))
    cover('B_two_exact_selections_each_epoch',all(len({r['start'] for r in sb if int(r['frame_generation'])==g})>=2 for g in (1,2,3)))
    require([r['a'] for r in b if r['name']=='rate']==['50000'] and len([r for r in b if r['name']=='begin'])==1 and not any(r['name']=='resume' for r in b),'B operation/rate/restart intent changed')
    errors=[r for r in groups[('worker',0)] if r['name']=='source_error' and r['label']=='published'];cover('B_fresh_after_A_real_Load_failure',len(errors)==1 and any(int(r['qpc'])>int(errors[0]['end']) for r in sb))
    a_seek=[r for r in groups[('worker',0)] if r['name']=='operation' and (r['label']=='seek' and r['a']=='2' or shared_transport(rows) and r['label']=='cancel' and r['b']=='2')]
    cover('B_selection_during_A_actual_seek',bool(a_seek and any(int(span['begin'])<=int(r['qpc'])<=int(span['end']) for span in a_seek for r in sb)))
    return dict(complete=not missing,missing=missing)


def costs(rows,frequency):
    events=rows.get('MC_EVENT',[]);spans={}
    for row in events:
        if row['name'] in ('graphics','operation','source_copy','call'):
            key=f'{row["instance"]}:{row["name"]}:{row["label"]}'
            span=(int(row['end'])-int(row['begin']))*1000/frequency
            values=spans.setdefault(key,dict(count=0,total_ms=0,max_ms=0));values['count']+=1;values['total_ms']+=span;values['max_ms']=max(values['max_ms'],span)
    beats=[r for r in events if r['name']=='heartbeat'];gaps=[(int(b['end'])-int(a['end']))*1000/frequency for a,b in zip(beats,beats[1:])]
    operations=[];selected=[r for r in rows.get('MC_CLOCK',[]) if r.get('selected')=='1']
    for row in events:
        if row['name']!='operation':continue
        begin,end=int(row['begin']),int(row['end'])
        intersect=[(int(b['end'])-int(a['end']))*1000/frequency for a,b in zip(beats,beats[1:]) if int(a['end'])<=end and int(b['end'])>=begin]
        operations.append(dict(instance=int(row['instance']),session=int(row['session']),name=row['label'],duration_ms=(end-begin)*1000/frequency,max_completion_gap_ms=max(intersect,default=None),peer_selections=sum(r['instance']!=row['instance'] and begin<=int(r['qpc'])<=end for r in selected)))
    clears=[(int(r['b'])-int(r['begin']))*1000/frequency for r in beats]
    return dict(spans=spans,operations=operations,max_clear_ms=max(clears,default=None),heartbeats=len(beats),max_completion_gap_ms=max(gaps,default=None),gpu_execution_time_measured=False,universal_no_stall_claim=False)


def finish(record,output,rows,expected):
    result=dict(kind=KIND,worker_clock_accepted=False,functional_status='failed',coverage_status='unassessed',native_runtime_verified=False,production_integration_accepted=False,reset_behavior_tested=False,engine_record_identity_proven=False,full_file_eof_claim=False)
    try:
        h=worker.one(rows,'MC_HEADER');frequency=int(h['qpc_frequency']);result['costs']=costs(rows,frequency)
        require(record.get('kind')==h.get('kind')==KIND and record.get('mode')=='worker-clock-two-textures' and shared_transport(rows) and h.get('support_seeking')=='1' and h.get('module_pin')=='process_lifetime','runner/header configuration differs')
        require(record['exit_code']==0 and record.get('unchanged') and all(record['unchanged'].values()) and not record['negative_source']['exists_before'] and not record['negative_source']['exists_after'],'process/input/negative-source contract failed')
        require(not any(record.get(k) for k in ('outer_timeout','diagnostic_log_limit','diagnostic_log_monitor_error')) and not rows.get('MC_TIMEOUT'),'runtime bounds failed')
        groups=validate_trace(rows);selected=replay_clock(rows,frequency);validate_leases(rows,selected);validate_source_windows(rows,expected)
        if shared_transport(rows):validate_shared_contract(rows,groups)
        result['package_config']=validate_package(rows,groups,record)
        result['assignment_quiescence']=validate_initial_quiescence(groups)
        result['workers']=validate_workers(rows,groups);result['event_drains']=validate_events(rows,groups)
        for ident in (0,1):
            metadata={k:[r for r in vals if r.get('instance')==str(ident) and r.get('session')=='1'] for k,vals in rows.items() if k.startswith('MP_LAV_')}
            validate_provider_session(metadata,Path(record.get('runtime_paths',{}).get('manifest',str(Path(record['lav_graph_provider']['path']).parent/'provider.manifest'))).parent)
        result.update(validate_render(rows,selected,output,expected))
        terminal=worker.one(rows,'MC_RESULT');require(terminal['functional']=='1' and terminal['A_clean']==terminal['B_clean']=='1','terminal cleanup incomplete')
        validate_protocol(rows,groups)
        result['functional_status']='passed';result['coverage']=coverage(rows,selected,groups)
        result['coverage_status']='complete' if result['coverage']['complete'] else 'incomplete_real_schedule'
        result['worker_clock_accepted']=result['coverage']['complete'];result['outcome']='owned_clock_two_textures_accepted' if result['worker_clock_accepted'] else 'functional_pass_coverage_incomplete'
    except (ValueError,KeyError,TypeError,OSError,StopIteration,IndexError,ZeroDivisionError) as error:
        result['validation_error']=str(error);result['outcome']='worker_clock_failed_or_incomplete'
    return result
