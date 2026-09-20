"""Bounded state-only policy/parser and production draw-hook integration; no Wine."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from lattice_state_packet import validate, load, RENDER, SOURCE, POSITION, SELECTOR, WORD_LIMIT
from verification.analysis.test_capture_bloom_lifetime import extract_function

ROOT = Path(__file__).resolve().parents[2]


def packet():
    p = dict(schema=1, selector=SELECTOR, status='complete', device=1, frame=8, generation=0,
             draw_input_coherence='unqualified', payload_copy_valid='not_attempted',
             scope_active_at_arm=True, candidates=2, query_ticks=100, qpc_frequency=1000, matches=[1, 1], records=[])
    for slot in range(2):
        r = dict(slot=slot, draw=slot+1, submitted=True, result='00000000', fields=[])
        def add(kind, index=0, words=None, count=1, hr='00000000'):
            if words is None: words = [0] * count
            r['fields'].append(dict(kind=kind, index=index, hr=hr, words=[f'{w:08x}' for w in words]))
        add('caps', words=[1, 1, 256, 0xfffe0300, 0xffff0300, 4])
        for stage, source in enumerate(SOURCE):
            add('source_shader', stage, [source & 0xffffffff, source >> 32])
            add('shader', stage, [0xfffe0300 if stage == 0 else 0xffff0300, 0xffff])
            add('declaration', stage, [v for offset, usage in [(0, 0), (8, 5), (16, 3), (24, 6), (32, 7)] for v in [offset << 16, 16 | usage << 16]] + [255, 17])
            add('constants_f', stage, count=1024 if stage == 0 else 896)
            add('constants_i', stage, count=64)
            add('constants_b', stage, count=16)
        add('arguments', words=[4, 3784 if slot == 0 else 940, 0, 9680 if slot == 0 else 1267, 0, 0])
        add('object', words=[127, 1, 1, 7, 0, 0x54b3, 0, 0x1000, 10] + [0]*9)
        add('object', 1, POSITION)
        for i, n in [(2, 9), (3, 4), (4, 16), (5, 16), (6, 16), (7, 16)]: add('object', i, count=n)
        add('route', words=[0]*5 + [1] + [0]*14)
        for state in RENDER: add('render', state)
        add('viewport', count=6); add('scissor', count=4); add('clip', count=4)
        add('stream', words=[1, 1234, 56, 0, 0]); add('stream_frequency', words=[0, 40, 1])
        add('vertex_desc', count=6); add('indices', words=[1, 123, 78, 0, 0]); add('index_desc', count=5)
        for i in range(5): add('target', i, words=[], hr='88760866')
        for stage in [0, 3]:
            for state in range(1, 14): add('sampler', stage*16+state)
            add('texture', stage, [1, 888, 900, 0, 0])
            add('texture_lod', stage, [3, 0, 2])
            for level in range(2): add('texture_desc', stage*32+level, count=8)
        p['records'].append(r)
    return p


class LatticeStateCaptureTests(unittest.TestCase):
    def test_production_policy_ambiguity_missing_and_bounds(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp)/'policy'
            subprocess.run([shutil.which('clang++') or 'c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT/'src/proxy'), str(ROOT/'verification/probe/lattice_state_policy_host.cpp'),
                            '-o', str(binary)], check=True, capture_output=True, text=True)
            out = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn('failures=0', out.stdout)

    def test_complete_bits_and_round_trip(self):
        p = packet()
        q=next(q for q in p['records'][0]['fields'] if q['kind']=='constants_f')
        q['words'][0] = '80000000'
        q['words'][1] = '7fc01234'
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'state.json'; path.write_text(json.dumps(p))
            self.assertEqual(load(path, True), p)

    def test_missing_field_query_failure_and_invented_values(self):
        for change in ['missing', 'failed', 'invented']:
            p = packet(); fields = p['records'][0]['fields']
            q = next(q for q in fields if q['kind']=='render' and q['index']==195)
            if change == 'missing': fields.remove(q)
            else:
                q['hr']='8876086a'
                if change == 'failed': q['words']=[]
            with self.subTest(change=change), self.assertRaises(ValueError): validate(p, True)

    def test_partial_and_ambiguous_never_load_as_complete(self):
        for status in ['no_match', 'partial', 'ambiguous', 'reset', 'unavailable', 'capacity', 'submission_failed']:
            p=packet();p['status']=status
            validate(p)
            with self.subTest(status=status), self.assertRaises(ValueError): validate(p, True)
        for mutation in [lambda p:p.update(matches=[2,1]),
                         lambda p:p['records'][0].update(submitted=False),
                         lambda p:p['records'][0].update(result='8876086c')]:
            p=packet();mutation(p)
            with self.assertRaises(ValueError): validate(p, True)

    def test_payload_claim_caps_and_resource_byte_bounds(self):
        cases = [lambda p:p.update(payload_copy_valid=True),
                 lambda p:p.update(draw_input_coherence='qualified'),
                 lambda p:p.update(candidates=66),
                 lambda p:p['records'].append(copy.deepcopy(p['records'][0])),
                 lambda p:p['records'][0]['fields'].extend(copy.deepcopy(p['records'][0]['fields']))]
        for change in cases:
            p=packet();change(p)
            with self.assertRaises(ValueError): validate(p)
        p=packet();q=next(q for q in p['records'][0]['fields'] if q['kind']=='shader');q['words']=['00000000']*WORD_LIMIT
        with self.assertRaises(ValueError): validate(p)

    def test_selector_and_within_frame_object_identity(self):
        for kind,index,word,value in [('object',1,0,'00000000'),('object',0,7,'00009999'),('source_shader',0,0,'00000000'),('arguments',0,1,'00000001'),('declaration',0,1,'00000000')]:
            p=packet();q=next(q for q in p['records'][1]['fields'] if (q['kind'],q['index'])==(kind,index));q['words'][word]=value
            with self.subTest(kind=kind), self.assertRaises(ValueError): validate(p, True)

    def test_missing_identity_timing_and_inconsistent_draw_metadata(self):
        for key in ['device', 'frame', 'generation', 'query_ticks', 'qpc_frequency']:
            for value in [None, -1, 1 << 64, True]:
                p=packet()
                if value is None: p.pop(key)
                else: p[key]=value
                with self.subTest(key=key,value=value), self.assertRaises(ValueError):validate(p, True)
        for key in ['device', 'qpc_frequency']:
            p=packet();p[key]=0
            with self.assertRaises(ValueError):validate(p, True)
        for slot in [0, 1]:
            p=packet();p['records'][slot].pop('draw')
            with self.assertRaises(ValueError):validate(p, True)
        for change in [lambda p:p['records'][1].update(draw=1),
                       lambda p:p['records'][0].update(draw=0),
                       lambda p:p.update(candidates=1),lambda p:p.update(candidates=65)]:
            p=packet();change(p)
            with self.assertRaises(ValueError):validate(p, True)

    def test_duplicate_json_key_is_not_silently_accepted(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'state.json';path.write_text('{"status":"ambiguous","status":"complete"}')
            with self.assertRaisesRegex(ValueError,'duplicate JSON key'):load(path)

    def test_launcher_explicit_selector_and_scope_prerequisite(self):
        from verification.analysis.test_volumetric_fog import FogLauncherTests
        launcher = FogLauncherTests()
        status, output, error = launcher.launch('--lattice-state', SELECTOR)
        self.assertNotEqual(status, 0)
        self.assertIn('requires --object-trace', error)
        status, output, error = launcher.launch('--lattice-state', SELECTOR, '--object-trace')
        self.assertEqual(status, 0, error)
        self.assertIn('"X3M_LATTICE_STATE": "'+SELECTOR+'"', output)
        self.assertIn('"X3M_OBJECT_TRACE": "1"', output)
        status, output, error = launcher.launch(environment={'X3M_LATTICE_STATE': SELECTOR})
        self.assertEqual(status, 0, error)
        self.assertIn('"X3M_LATTICE_STATE": "0"', output)
        p=packet();p['scope_active_at_arm']=False
        with self.assertRaises(ValueError):validate(p, True)

    def test_real_hook_dispatch_and_boundary(self):
        source=(ROOT/'src/proxy/capture.cpp').read_text()
        function=extract_function(source,'HRESULT WINAPI draw_indexed(')
        with tempfile.TemporaryDirectory() as tmp:
            tmp=Path(tmp);(tmp/'draw_under_test_inc.h').write_text(function)
            exe=tmp/'hook'
            subprocess.run([shutil.which('clang++') or 'c++','-std=c++17','-O2','-Wall','-Wextra','-Werror',
                            '-I',str(tmp),str(ROOT/'verification/probe/lattice_state_hook_host.cpp'),'-o',str(exe)],
                           check=True,capture_output=True,text=True)
            out=subprocess.run([str(exe)],check=True,capture_output=True,text=True)
            self.assertIn('failures=0',out.stdout)
        self.assertLess(function.index('observer->original'),function.index('auto route='))
        self.assertLess(function.index('observer->effective'),function.index('cpu.before_original();'))
        self.assertLess(function.index('observer->result'),function.index('ctx.motion_output.after_draw'))
        helper=(ROOT/'src/proxy/lattice_state_capture.cpp').read_text()
        for forbidden in ['->Lock(', '->LockRect(', '->SetRenderState(', '->SetSamplerState(', '->SetTexture(', '->SetPrivateData(', '->GetRenderTarget(', 'GetRenderTargetData(', 'StretchRect(']:
            self.assertNotIn(forbidden,helper)
        before_publish=helper.split('bool Capture::publish')[0]
        self.assertNotIn('fopen',before_publish);self.assertNotIn('fprintf',before_publish)
        self.assertIn('get_target(d,i,&s)',helper)
        self.assertIn('Kind::StreamFrequency,i,FAILED(hr)?hr:frequency,stream',helper)
        original=extract_function(helper,'int Capture::original(')
        self.assertGreater(original.index('auto& r=records_[slot]'), original.index('policy_.accept_match'))
        self.assertNotIn('records_[slot]',original[:original.index('policy_.accept_match')])
        self.assertIn('ctx.lattice_state->invalidate()',source)
        self.assertIn('down&&!ctx.key_down',source)

if __name__ == '__main__': unittest.main()
