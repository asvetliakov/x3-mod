"""Reject incomplete or failed real-D3D bridge evidence; no runtime execution."""
import importlib.util
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('bridge_check',ROOT/'verification/probe/fog_route_bridge_check.py')
bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
SCOPE='BRIDGE_SCOPE methods=production_fog_fragment native_d3d=1 synthetic_owner=1 cached_shader_identity=synthetic selector_hook=not_exercised taa_history=request_endpoint_only native_reset=reused_state_fixture'
class FogRouteBridgeTests(unittest.TestCase):
    DENSITY=['IMAGE %s %016x'%(n,i+1) for i,n in enumerate((*bridge.LEGACY_IMAGES,'legacy_pose_a','stored_sector_a','stored_sector_b'))]+[
        'volumetric_fog_cache device=1 frame=3 event=refused reason=density_ps30_slots fallback=legacy result=8876086a',
        'volumetric_fog_cache device=1 frame=90 event=far_ready ms=600.0 nodes=1','volumetric_fog_cache device=1 frame=200 event=fine_ready ms=1500.0 nodes=2',
        'DENSITY_FILL sector=a frames=300 filling_frames=40 far_ready_ms=600.0 fine_ready_ms=1500.0 nodes=100','DENSITY_FILL sector=b frames=300 filling_frames=40 far_ready_ms=600.0 fine_ready_ms=1500.0 nodes=200',
        'DENSITY_COST steady_frames=10 steady_us_median=1.0 steady_us_p99=2.0 upload_frames=5 upload_us_median=20.0 upload_us_p99=30.0 upload_us_max=40.0',
        'DENSITY_RESET frames=100 reuploaded_bytes=8520192 regenerated_nodes=0','DENSITY_RELEASE live_worker=1 join_ms=3.0 device_refs=1 expected=1','DENSITY_ABANDON ms=0.1',bridge.DENSITY_SCOPE]
    def log(self):
        rows=[f'CHECK {n} PASS' for n in sorted(bridge.REQUIRED|bridge.DENSITY_REQUIRED) for _ in range(bridge.CAMERA_COUNTS.get(n,1))]
        return '\n'.join([*rows,*self.DENSITY,bridge.CAMERA_SCOPE,SCOPE,f'RESULT fog_route_bridge checks={len(rows)} PASS'])+'\n'
    def test_complete_report_keeps_limits(self):
        value=bridge.validate(self.log());self.assertEqual(value['checks'],sum(bridge.CAMERA_COUNTS.get(n,1) for n in bridge.REQUIRED|bridge.DENSITY_REQUIRED));self.assertEqual(len(value['limits']),6)
        self.assertIsNone(value['density']['legacy_bit_identical_to_baseline'])
    def test_stored_density_witnesses_logging_and_baseline(self):
        total=len(bridge.DENSITY_REQUIRED)+sum(bridge.CAMERA_COUNTS.get(n,1) for n in bridge.REQUIRED)
        for witness in bridge.DENSITY_REQUIRED:
            with self.assertRaises(ValueError):bridge.validate(self.log().replace(f'CHECK {witness} PASS\n','').replace(f'checks={total}',f'checks={total-1}'))
        refusal=self.DENSITY[8]
        for text in (self.log().replace(bridge.DENSITY_SCOPE,''),self.log().replace(refusal+'\n',''),self.log().replace(refusal,refusal+'\n'+refusal),
                     self.log().replace('event=far_ready','event=x'),self.log()+'volumetric_fog_cache_frame device=1 frame=2 density=1\n',
                     self.log().replace('IMAGE legacy_after_reset','IMAGE other'),self.log().replace('DENSITY_COST','COST')):
            with self.assertRaises(ValueError):bridge.validate(text)
        baseline='\n'.join(self.DENSITY[:5])+'\n'
        self.assertTrue(bridge.validate(self.log(),baseline)['density']['legacy_bit_identical_to_baseline'])
        with self.assertRaises(ValueError):bridge.validate(self.log(),baseline.replace('%016x'%2,'%016x'%99))
        with self.assertRaises(ValueError):bridge.validate(self.log(),'')
    def test_every_required_witness_is_required(self):
        total=sum(bridge.CAMERA_COUNTS.get(n,1) for n in bridge.REQUIRED|bridge.DENSITY_REQUIRED)
        for witness in bridge.REQUIRED:
            text=self.log().replace(f'CHECK {witness} PASS\n','').replace(f'checks={total}',f'checks={total-bridge.CAMERA_COUNTS.get(witness,1)}')
            with self.assertRaises(ValueError):bridge.validate(text)
    def test_failure_scope_and_ambiguous_terminal(self):
        total=sum(bridge.CAMERA_COUNTS.get(n,1) for n in bridge.REQUIRED|bridge.DENSITY_REQUIRED)
        for text in (self.log()+'RESULT FAIL error=injected\n',self.log().replace(SCOPE,''),self.log()+'RESULT fog_route_bridge checks=1 PASS\n',self.log().replace(f'checks={total}','checks=0'),self.log().replace(bridge.CAMERA_SCOPE,'')):
            with self.assertRaises(ValueError):bridge.validate(text)
    def test_camera_sequence_cannot_be_shortened_or_duplicated(self):
        total=sum(bridge.CAMERA_COUNTS.get(n,1) for n in bridge.REQUIRED|bridge.DENSITY_REQUIRED)
        for name in bridge.CAMERA_COUNTS:
            row=f'CHECK {name} PASS\n'
            for text in (self.log().replace(row,'',1).replace(f'checks={total}',f'checks={total-1}'),
                         row+self.log().replace(f'checks={total}',f'checks={total+1}')):
                with self.assertRaises(ValueError):bridge.validate(text)
