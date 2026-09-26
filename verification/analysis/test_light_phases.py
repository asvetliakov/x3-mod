import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
import verify_light_phase_sites as probe
from source_text import source_text
ROOT=Path(__file__).resolve().parents[2]
class LightPhases(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data=probe.common.DEFAULT_EXE.read_bytes();cls.decoded=probe.decode()
        cls.source=source_text(probe.SOURCE);cls.claims,cls.anchored=probe.shared.other_claims(ROOT/'src/proxy',probe.SOURCE)
    def inspect(self,**changes):
        args=dict(data=self.data,decoded=self.decoded,source=self.source,claims=self.claims,anchored=self.anchored);args.update(changes)
        return probe.inspect(**args)
    def test_real_sites(self):self.assertEqual(self.inspect()['result'],'PASS')
    def test_mutations(self):
        for source in (self.source.replace('6,0,0','6,4,0'),self.source.replace('0x5f','0x5e'),self.source.replace('5,0,0','5,0,1')):
            self.assertFalse(self.inspect(source=source)['checks']['source'])
        self.assertFalse(self.inspect(claims=self.claims+[(0x47d5e1,0x47d5e2)])['checks']['claims'])
        decoded=dict(self.decoded);decoded[probe.REGIONS[0]]=decoded[probe.REGIONS[0]][:-1]
        self.assertFalse(self.inspect(decoded=decoded)['checks']['regions'])
        for target,check in ((0x47d5e1,'raw_interior'),(0x47d5e0,'raw_callers'),(0x47d9ab,'raw_exit_edges')):
            data=bytearray(self.data);off=probe.shared.TEXT_OFFSET+16;va=probe.shared.TEXT_BASE+16
            data[off:off+5]=b'\xe9'+struct.pack('<i',target-va-5)
            self.assertFalse(self.inspect(data=bytes(data))['checks'][check])
        data=self.data+struct.pack('<I',0x47d9ab)
        self.assertFalse(self.inspect(data=data)['checks']['absolute_references'])
    def test_host(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe=Path(tmp)/'test'
            subprocess.run([shutil.which('clang++') or 'c++','-std=c++17','-O2','-Wall','-Wextra','-Werror',str(ROOT/'verification/probe/light_phases_host.cpp'),'-o',str(exe)],check=True)
            r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout);print(r.stdout.strip())
    def test_wiring_and_hot_path(self):
        source=source_text(ROOT/'src/proxy/light_phases.cpp')
        handler=source.split('x3m_light_phase_enter(unsigned')[1].split('namespace x3m::light_phases {')[0]
        self.assertLess(handler.index('fnstcw %0'),handler.index('GetCurrentThreadId'))
        self.assertLess(handler.index('refuse_mode();return;'),handler.index('LightCallBoundary'))
        self.assertNotIn('log(',handler);self.assertNotIn('new ',handler)
        self.assertEqual(handler.count('QueryPerformanceCounter('),1)
        self.assertIn('SavedEsp:x3m::lean_stub::SavedEbp',handler)
        self.assertIn('token+4',handler)
        self.assertIn('lean_stub::emit_context',source);self.assertIn('stamp::install_group',source)
        self.assertIn('light_phases::frame(frame,taken);',source_text(ROOT/'src/proxy/frame_phases.cpp'))
        self.assertIn('light_phases::initialize();',source_text(ROOT/'src/proxy/capture.cpp'))
        self.assertEqual(source_text(ROOT/'CMakeLists.txt').count('src/proxy/light_phases.cpp'),2)
        self.assertIn("'_x3m_light_phase_enter'",source_text(ROOT/'verification/probe/check_no_x87.py'))
        manage=source_text(ROOT/'tools/manage.py');self.assertNotIn("--light-phases",manage)  # a --draw-trace member since 2026-09-26
        self.assertIn('if(!log_tier::draw_trace_flag(L"X3M_LIGHT_PHASES"))return false;',source)

class Rows(unittest.TestCase):
    def test_log_contract_and_coverage(self):
        import importlib.util
        spec=importlib.util.spec_from_file_location('light_rows',ROOT/'tools/analysis/summarize_light_phases.py')
        rows=importlib.util.module_from_spec(spec);spec.loader.exec_module(rows)
        import re
        source=source_text(ROOT/'src/proxy/light_phases.cpp')
        log=source[source.index('log("light_phases qpc='):source.index('now,s.frame,s.frames')]
        names=re.findall(r'(\w+)=%',''.join(re.findall(r'"([^"]*)"',log)))
        self.assertEqual(sorted(names),sorted(rows.FIELDS));self.assertEqual(len(names),len(set(names)))
        fields={key:0 for key in rows.FIELDS};fields.update(frames=300,valid_frames=300)
        def line():return '[1] light_phases '+' '.join(f'{key}={value}' for key,value in fields.items())
        self.assertTrue(rows.summarize([line()])['complete_coverage'])
        self.assertFalse(rows.summarize([line()])['self_calibrated'])
        for health in rows.HEALTH+('unknown_entries',):
            fields[health]=1;self.assertFalse(rows.summarize([line()])['complete_coverage']);fields[health]=0
        fields['cockpit_entries']=1;self.assertFalse(rows.summarize([line()])['complete_coverage'])
        fields['dispatch_cost_ns']=100;self.assertIsNone(rows.parse_row(line()))
        fields['self_calibrated']=1;self.assertIsNotNone(rows.parse_row(line()))
        self.assertIsNone(rows.parse_row(line()+' frames=300'))
        self.assertIsNone(rows.parse_row(line().replace('frames=300','frames=0')))

class RunnerProvenance(unittest.TestCase):
    def test_success_output_cannot_hide_changed_fixture_or_sources(self):
        import run_light_phase_cpu as runner
        from unittest.mock import patch
        for mutation in ('none','source','exe','handler','duplicate_summary'):
            with self.subTest(mutation=mutation),tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp);directory=root/'build/verification/light-phases';directory.mkdir(parents=True)
                exe=directory/'light_phase_cpu_fixture.exe';exe.write_bytes(b'fixture-before')
                handler=directory/'production_handler.o';handler.write_bytes(b'handler-before')
                source=root/'fixture.cpp';source.write_text('qualified-source')
                def fake_run(command,**kwargs):
                    target={'source':source,'exe':exe,'handler':handler}.get(mutation)
                    if target:target.write_bytes(b'changed-during-execution')
                    text='LIGHT PHASE CPU sites=2 checks=2093 failures=0\n'
                    if mutation=='duplicate_summary':text+=text
                    return subprocess.CompletedProcess(command,0,text,'')
                with patch.object(runner,'ROOT',root),patch.object(runner,'source_inputs',return_value=[source]),patch.object(runner,'game_running',return_value=False),patch.object(runner.subprocess,'run',side_effect=fake_run),patch.object(runner.bottle,'results_dir',return_value=root),patch.object(runner.bottle,'describe',return_value={'name':'mock'}):
                    result=runner.run(no_build=True)
                self.assertEqual(result['result'],'PASS' if mutation=='none' else 'FAIL')
                self.assertEqual(result['provenance']['build_mode'],'retained')
                self.assertIsNotNone(result['handler_sha256'])
                self.assertEqual(result['provenance']['stable'],mutation in ('none','duplicate_summary'))
