import unittest
import run_voice_asf_probe as probe


def fixture(outcome='missing_source', decoder_available=True):
    rows=['ASF_HEADER schema=1 sources=2 modes=2 repeats=2 cases=8 thread=9 audible=0'];seq=0
    def stage(name,s=0,m=0,r=-1,phase='startup',code='00000000'):
        nonlocal seq
        common=f'source={s} wrapper={m} route={m} repeat={r} seq={seq} phase={phase} name={name} attempt=1 required=1'
        rows.extend(['VOICE_BEGIN '+common,'VOICE_STAGE '+common+f' hr={code} wall_ms=1.0 cpu_ms=0.0 cpu_valid=1']);seq+=1
    stage('co_initialize');stage('directsound_create');stage('directsound_cooperative')
    rows.append('VOICE_STARTUP ds_hr=00000000 coop_hr=00000000 ds_present=1 window=1 primary_play=0')
    for s,m,r in probe.MATRIX:
        p=f'source={s} wrapper={m} route={m} repeat={r}'
        cap=bool(m and decoder_available);caph='00000000' if not m or cap else '80040154'
        if m:
            for n in ('wma_wrapper_activate','wma_wrapper_qi'):stage(n,s,m,r,'create')
            stage('wma_decoder_init',s,m,r,'create',caph);rows.append('ASF_DECODER '+p+f' available={int(cap)} hr={caph}')
        for n in ('activate_stream','initialize','add_audio','audio_qi_pre','set_pcm','get_graph'):stage(n,s,m,r,'create')
        source=outcome!='missing_source';loaded=source and outcome!='load_failure'
        pins=loaded;connected=loaded and outcome not in ('connect_failure','second_edge_failure') and (not m or cap)
        made=decoded=connected
        fatal='asf_activate' if not source else 'asf_load' if not loaded else 'explicit_decoder_unavailable' if m and not cap else 'connect_decoder_manual' if m and outcome=='second_edge_failure' else 'connect_source_decoder' if m and not connected else 'connect_auto_to_manual' if not connected else 'none'
        code='80040154' if not source or m and not cap else '80040217' if not connected else '00000000'
        stage('asf_activate',s,m,r,'create','00000000' if source else code)
        if source:
            stage('asf_add',s,m,r,'create');stage('asf_file_qi',s,m,r,'create');stage('asf_load',s,m,r,'create','00000000' if loaded else code)
        if loaded:
            stage('get_manual_filter',s,m,r,'create')
            roles=['source','sink']+(['decoder_input']+(['decoder_output'] if outcome!='connect_failure' else []) if m and cap else [])
            for role in roles:
                stem={'source':'source','sink':'sink','decoder_input':'decoder_in','decoder_output':'decoder_out'}[role]
                for n in ('enum_pins','pin_next','direction','enum_types','type_next'):stage(stem+'_'+n,s,m,r,'create')
                stage(stem+'_type_next',s,m,r,'create','00000001');stage(stem+'_pin_next',s,m,r,'create','00000001')
                tag=353 if role in ('source','decoder_input') else 1
                rows.append('ASF_TYPE '+p+f' role={role} pin=0 type=0 major={probe.AUDIO} subtype=00000161-0000-0010-8000-00aa00389b71 format=05589f81-c356-11ce-bf01-00aa0055595a format_bytes=28 tag={tag} channels=1 rate=44100 bits=16 align=2 avg=88200 extra=10')
                rows.append('ASF_PIN '+p+f' role={role} pin=0 direction={1 if role in ("source","decoder_output") else 0} audio=1')
                rows.append('ASF_SELECTION '+p+f' role={role} candidates=1')
            stage('sink_query_source_type',s,m,r,'create','00000001')
            def negotiated(role):
                stage(role+'_connected_type',s,m,r,'create')
                tag=353 if role in ('source','decoder_input') else 1
                sub='00000161' if tag==353 else '00000001'
                rows.append('ASF_NEGOTIATED '+p+f' role={role} major={probe.AUDIO} subtype={sub}-0000-0010-8000-00aa00389b71 format=05589f81-c356-11ce-bf01-00aa0055595a format_bytes=28 tag={tag} channels=1 rate=44100 bits=16 align=2 avg=88200 extra=10')
            if not m:
                stage('connect_auto_to_manual',s,m,r,'create','00000000' if connected else code)
                if connected:negotiated('source');negotiated('sink')
            elif cap:
                stage('decoder_query_source_type',s,m,r,'create')
                first=outcome!='connect_failure'
                stage('connect_source_decoder',s,m,r,'create','00000000' if first else code)
                if first:
                    negotiated('source');negotiated('decoder_input')
                    stage('sink_query_decoder_type',s,m,r,'create')
                    stage('connect_decoder_manual',s,m,r,'create','00000000' if connected else code)
                    if connected:negotiated('decoder_output');negotiated('sink')
        if made:
            for n in ('get_audio','audio_qi_post','get_format','activate_audio_data','set_buffer_native','data_format','create_sample','create_dsound_buffer','position_qi','control_qi','probe_manual_sink_guard','stream_run','control_pause'):stage(n,s,m,r,'create')
            rows.append('VOICE_FORMAT '+p+' tag=1 rate=44100 channels=1 bits=16 align=2 avg=88200 extra=0')
            rows.append('VOICE_SINK '+p+' terminals=1 filters=3 manual_only=1 hr=00000000')
            for cue in (0,1):
                for batch in (0,1):
                    start=(10 if cue==0 else 60)*10000000+batch*1000000
                    rows.append('VOICE_PCM '+p+f' cue={cue} batch={batch} requested_ms={10500 if cue==0 else 60500} seek_ms={10000 if cue==0 else 60000} actual=8820 start={start} end={start+1000000} current={start+1000000} nonzero=4400 peak=1000 energy=900000')
        else:rows.append('VOICE_FAILURE '+p+f' phase=create name={fatal} hr={code}')
        stage('release_multimedia',s,m,r,'cleanup')
        rows.append('ASF_CASE '+p+f' source_available={int(source)} loaded={int(loaded)} pins_selected={int(pins)} decoder_attempted={m} decoder_available={int(cap)} decoder_hr={caph} connected={int(connected)} constructed={int(made)} decoded={int(decoded)} fatal={fatal} hr={code} cleanup=1')
    stage('co_uninitialize',phase='shutdown');rows.append('ASF_COMPLETE cases=8 processes=1 audible=0 owner_thread=1')
    return '\n'.join(rows)+'\n'


class AsfTests(unittest.TestCase):
    def test_source_failure_does_not_hide_independent_decoder_capability(self):
        for available in (True,False):
            result=probe.validate(fixture(decoder_available=available))
            self.assertEqual(len(result['cases']),8)
            self.assertTrue(all(c['source_available']=='0' for c in result['cases']))
            self.assertEqual([c['decoder_available'] for c in result['cases'] if c['route']=='1'],[str(int(available))]*4)

    def test_loaded_source_failure_is_distinct_from_missing_class(self):
        result=probe.validate(fixture('load_failure'))
        self.assertTrue(all(c['source_available']=='1' and c['loaded']=='0' and c['fatal']=='asf_load' for c in result['cases']))

    def test_connected_pcm_passes_both_modes(self):
        result=probe.validate(fixture('success'))
        self.assertEqual(sum(c['decoded']=='1' for c in result['cases']),8)
        self.assertEqual(sum(len(c['pcm']) for c in result['cases']),32)

    def test_decoder_and_edge_failures_remain_unqualified(self):
        result=probe.validate(fixture('success',False))
        self.assertEqual(sum(c['decoded']=='1' for c in result['cases']),4)
        self.assertTrue(all(c['fatal']=='explicit_decoder_unavailable' for c in result['cases'] if c['route']=='1'))
        result=probe.validate(fixture('connect_failure'))
        self.assertTrue(all(c['loaded']=='1' and c['connected']=='0' and not c['pcm'] for c in result['cases']))

    def test_missing_failed_or_ambiguous_enumeration_cannot_claim_connection(self):
        good=fixture('success')
        for bad in (good.replace('name=source_enum_types','name=missing_enum'),
                    good.replace('name=sink_direction','name=missing_direction'),
                    good.replace('role=source candidates=1','role=source candidates=2'),
                    good.replace('role=source pin=0 direction=1 audio=1','role=source pin=0 direction=0 audio=1'),
                    good.replace('name=source_type_next attempt=1 required=1 hr=00000001','name=source_type_next attempt=1 required=1 hr=80004005')):
            with self.assertRaises(AssertionError):probe.validate(bad)

    def test_pcm_format_sink_and_explicit_edges_are_required(self):
        good=fixture('success')
        for bad in (good.replace('VOICE_FORMAT','IGNORED_FORMAT'),good.replace('align=2 avg=88200','align=4 avg=88200'),
                    good.replace('manual_only=1','manual_only=0'),good.replace('name=connect_decoder_manual','name=wrong_edge'),
                    good.replace('nonzero=4400','nonzero=0'),good.replace('seek_ms=60000','seek_ms=10000')):
            with self.assertRaises(AssertionError):probe.validate(bad)

    def test_query_accept_and_role_bound_negotiated_types_required(self):
        good=fixture('success')
        for bad in (good.replace('name=sink_query_source_type','name=lost_query'),
                    good.replace('name=decoder_query_source_type','name=lost_query'),
                    good.replace('name=sink_query_decoder_type','name=lost_query'),
                    good.replace('ASF_NEGOTIATED','LOST_NEGOTIATED'),
                    good.replace('role=decoder_input major='+probe.AUDIO,'role=decoder_input major=wrong'),
                    good.replace('role=decoder_output major='+probe.AUDIO+' subtype=00000001','role=decoder_output major='+probe.AUDIO+' subtype=00000161')):
            with self.assertRaises(AssertionError):probe.validate(bad)

    def test_partial_edges_retain_and_require_their_endpoint_evidence(self):
        for outcome in ('connect_failure','second_edge_failure'):
            good=fixture(outcome);result=probe.validate(good)
            for case in result['cases']:
                if case['route']!='1':continue
                roles={r['role'] for r in case['selections']}
                self.assertIn('decoder_input',roles)
                self.assertTrue(case['pin_inventory'])
                if outcome=='second_edge_failure':
                    self.assertIn('decoder_output',roles)
                    self.assertEqual({r['role'] for r in case['negotiated_types']},{'source','decoder_input'})
            for bad in (good.replace('role=decoder_input candidates=1','role=decoder_input candidates=2'),
                        good.replace('name=decoder_in_enum_types','name=lost_enum'),
                        good.replace('name=decoder_query_source_type','name=lost_query')):
                with self.assertRaises(AssertionError):probe.validate(bad)
            if outcome=='second_edge_failure':
                for bad in (good.replace('role=decoder_output candidates=1','role=decoder_output candidates=0'),
                            good.replace('ASF_NEGOTIATED','LOST_NEGOTIATED')):
                    with self.assertRaises(AssertionError):probe.validate(bad)

    def test_exact_coverage_timing_and_first_error_framing(self):
        good=fixture()
        for bad in (good.replace('ASF_COMPLETE cases=8','ASF_COMPLETE cases=7'),good.replace('cpu_ms=0.0','cpu_ms=nan'),
                    good.replace('cpu_valid=1','cpu_valid=0'),good.replace('VOICE_FAILURE','LOST_FAILURE'),
                    good.replace('VOICE_STAGE','LOST_STAGE',1),good.replace('80040154','00000001')):
            with self.assertRaises(AssertionError):probe.validate(bad)


if __name__=='__main__':unittest.main()
