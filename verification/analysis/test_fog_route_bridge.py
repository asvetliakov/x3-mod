"""Reject incomplete or failed real-D3D bridge evidence; no runtime execution."""
import importlib.util
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('bridge_check',ROOT/'verification/probe/fog_route_bridge_check.py')
bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
SCOPE='BRIDGE_SCOPE methods=production_fog_fragment native_d3d=1 synthetic_owner=1 cached_shader_identity=synthetic selector_hook=not_exercised taa_history=request_endpoint_only native_reset=reused_state_fixture'
class FogRouteBridgeTests(unittest.TestCase):
    def log(self):
        return '\n'.join([*(f'CHECK {n} PASS' for n in sorted(bridge.REQUIRED)),SCOPE,f'RESULT fog_route_bridge checks={len(bridge.REQUIRED)} PASS'])+'\n'
    def test_complete_report_keeps_limits(self):
        value=bridge.validate(self.log());self.assertEqual(value['checks'],len(bridge.REQUIRED));self.assertEqual(len(value['limits']),5)
    def test_every_required_witness_is_required(self):
        for witness in bridge.REQUIRED:
            text=self.log().replace(f'CHECK {witness} PASS\n','').replace(f'checks={len(bridge.REQUIRED)}',f'checks={len(bridge.REQUIRED)-1}')
            with self.assertRaises(ValueError):bridge.validate(text)
    def test_failure_scope_and_ambiguous_terminal(self):
        for text in (self.log()+'RESULT FAIL error=injected\n',self.log().replace(SCOPE,''),self.log()+'RESULT fog_route_bridge checks=1 PASS\n',self.log().replace(f'checks={len(bridge.REQUIRED)}','checks=0')):
            with self.assertRaises(ValueError):bridge.validate(text)
