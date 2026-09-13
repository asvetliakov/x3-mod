import unittest
import run_voice_sync_probe as probe


def fixture(mode='failure'):
    rows=['SYNC_HEADER schema=1 cases=2 processes=1 thread=7 audible=0'];seq=0
    def stage(name,source=0,code='00000000',attempt=1):
        nonlocal seq
        common=f'source={source} wrapper=0 route=0 repeat={0 if source else -1} seq={seq} phase=open name={name} attempt={attempt} required=1'
        rows.extend(['VOICE_BEGIN '+common,'VOICE_STAGE '+common+f' hr={code} wall_ms=1.0 cpu_ms=0.0 cpu_valid=1']);seq+=1
    stage('co_initialize')
    stage('load_wmvcore',code='8007007e' if mode=='load_failure' else '00000000')
    if mode!='load_failure':stage('find_create',code='8007007f' if mode=='lookup_failure' else '00000000')
    for source in (144,244):
        p=f'source={source} wrapper=0 route=0 repeat=0'
        created=mode not in ('activation','load_failure','lookup_failure');opened=mode in ('empty','metadata','close_failure')
        open_hr='00000000' if opened else 'd0000001' if created else '8000000a'
        if mode not in ('load_failure','lookup_failure'):stage('create_sync_reader',source,'00000000' if created else '80040154')
        if created:stage('sync_open',source,open_hr)
        outputs=1 if mode=='metadata' else 0
        if opened:
            stage('output_count',source);rows.append('SYNC_OUTPUTS '+p+f' count={outputs}')
            if outputs:
                for n in ('output_props','media_size','media_type'):stage(n,source)
                rows.append('SYNC_TYPE '+p+' index=0 size=90 major=73647561-0000-0010-8000-00aa00389b71 subtype=00000001-0000-0010-8000-00aa00389b71 format=05589f81-c356-11ce-bf01-00aa0055595a format_bytes=18 tag=1 channels=1 rate=44100 bits=16 align=2 avg=88200 extra=0')
            stage('sync_close',source,'80004005' if mode=='close_failure' else '00000000')
        fatal='none' if opened and mode!='close_failure' else 'sync_close' if opened else 'sync_open' if created else 'create_sync_reader'
        if mode in ('load_failure','lookup_failure'):fatal='load_wmvcore' if mode=='load_failure' else 'find_create'
        hr='00000000' if fatal=='none' else '80004005' if opened else open_hr if created else '80040154'
        if mode in ('load_failure','lookup_failure'):hr='8007007e' if mode=='load_failure' else '8007007f'
        if fatal!='none':rows.append('VOICE_FAILURE '+p+f' name={fatal} hr={hr}')
        stage('release_sync_reader',source)
        rows.append('SYNC_CASE '+p+f' created={int(created)} opened={int(opened)} metadata={int(opened)} outputs={outputs} closed={int(opened and mode!="close_failure")} open_hr={open_hr} fatal={fatal} hr={hr} cleanup=1')
    stage('free_wmvcore');stage('co_uninitialize');rows.append('SYNC_COMPLETE cases=2 processes=1 audible=0')
    return '\n'.join(rows)+'\n'


class SyncTests(unittest.TestCase):
    def test_actual_open_hresult_separate_from_activation(self):
        for mode,created,code in (('failure','1','d0000001'),('activation','0','8000000a')):
            result=probe.validate(fixture(mode));self.assertEqual(result['opened'],0)
            self.assertTrue(all(c['created']==created and c['open_hr']==code for c in result['cases']))

    def test_prerequisite_failure_names_and_bound_activation(self):
        for mode,name in (('load_failure','load_wmvcore'),('lookup_failure','find_create')):
            result=probe.validate(fixture(mode))
            self.assertTrue(all(c['fatal']==name and c['created']=='0' for c in result['cases']))
        for bad in (fixture('load_failure').replace('fatal=load_wmvcore','fatal=find_create'),
                    fixture('activation').replace('name=create_sync_reader','name=lost_creation'),
                    fixture('activation').replace('name=create_sync_reader attempt=1 required=1 hr=80040154','name=create_sync_reader attempt=1 required=1 hr=00000000'),
                    fixture('close_failure').replace('metadata=1','metadata=0'),
                    fixture('empty').replace('name=output_count','name=lost_count')):
            with self.assertRaises(AssertionError):probe.validate(bad)

    def test_success_does_not_invent_outputs_or_decode(self):
        for mode,count in (('empty',0),('metadata',1)):
            result=probe.validate(fixture(mode));self.assertEqual(result['opened'],2)
            self.assertTrue(all(len(c['types'])==count for c in result['cases']))
            self.assertNotIn('decoded',result)

    def test_metadata_size_stage_index_and_completeness(self):
        good=fixture('metadata')
        for bad in (good.replace('size=90','size=65537'),good.replace('index=0','index=1'),
                    good.replace('name=media_type','name=lost_type'),good.replace('SYNC_TYPE','LOST_TYPE'),
                    good.replace('format_bytes=18','format_bytes=91')):
            with self.assertRaises(AssertionError):probe.validate(bad)

    def test_close_failure_preserves_open_result(self):
        result=probe.validate(fixture('close_failure'))
        self.assertTrue(all(c['open_hr']=='00000000' and c['hr']=='80004005' and c['closed']=='0' for c in result['cases']))
        with self.assertRaises(AssertionError):probe.validate(fixture('metadata').replace('name=sync_close','name=lost_close'))

    def test_exact_matrix_first_failure_and_timing(self):
        good=fixture()
        for bad in (good.replace('source=244','source=144'),good.replace('VOICE_FAILURE','LOST_FAILURE'),
                    good.replace('cpu_ms=0.0','cpu_ms=nan'),good.replace('VOICE_STAGE','LOST_STAGE',1),
                    good.replace('SYNC_COMPLETE cases=2','SYNC_COMPLETE cases=1')):
            with self.assertRaises(AssertionError):probe.validate(bad)

if __name__=='__main__':unittest.main()
