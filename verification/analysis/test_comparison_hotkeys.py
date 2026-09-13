"""Focused host execution: no Wine, game, shader compiler or DLL build."""
import argparse
import ast
import contextlib
import io
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest
from unittest import mock

from verification.analysis.test_capture_bloom_lifetime import extract_function

ROOT = Path(__file__).resolve().parents[2]


class ComparisonHotkeys(unittest.TestCase):
    def test_production_controls_and_exposure_handoff(self):
        source = (ROOT / 'src/renderer/hdr_pass.cpp').read_text()
        functions = [extract_function(source, signature) for signature in (
            'void HdrPass::prepare_constants(', 'bool HdrPass::comparison_exposure(')]
        self.run_host('comparison_controls_fixture.cpp', functions, exposure=True)

    def test_production_notice_state_failure_and_allocation_contract(self):
        self.run_host('comparison_notice_fixture.cpp', [], notice=True)

    def test_production_bloom_handoff_preserves_display_and_off_filtering(self):
        source = (ROOT / 'src/proxy/capture.cpp').read_text()
        functions = [extract_function(source, 'void retain_compositor_scene(')]
        self.run_host('comparison_bloom_handoff_fixture.cpp', functions, handoff=True)

    def run_host(self, fixture, functions, exposure=False, notice=False, handoff=False):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix='x3-comparison-') as temporary:
            directory = Path(temporary)
            (directory / ('comparison_handoff_under_test_inc.h' if handoff else 'comparison_exposure_under_test_inc.h')).write_text('\n'.join(functions))
            stub = ROOT / 'verification/probe/hdr_display_snapshot_stubs/d3d9.h'
            (directory / 'd3d9.h').write_text(f'#include "{stub}"\n' + textwrap.dedent('''
                #define WINAPI
                using LONG=std::int32_t;
                using D3DCOLOR=DWORD;
                struct D3DRECT { LONG x1,y1,x2,y2; };
                struct D3DVIEWPORT9 { DWORD X,Y,Width,Height; float MinZ,MaxZ; };
                enum D3DBACKBUFFER_TYPE { D3DBACKBUFFER_TYPE_MONO };
                constexpr HRESULT D3DERR_NOTFOUND=-99;
                constexpr DWORD D3DCLEAR_TARGET=1;
            '''))
            extra = []
            if exposure:
                extra.append(str(ROOT / 'src/renderer/exposure.cpp'))
            if notice:
                extra.append(str(ROOT / 'src/proxy/comparison_notice.cpp'))
            for name, flags in [('release', ['-O2']), ('sanitized', ['-O1', '-g', '-fsanitize=address,undefined'])]:
                with self.subTest(fixture=fixture, mode=name):
                    executable = directory / name
                    result = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags,
                        '-I', str(directory), str(ROOT / 'verification/probe' / fixture), *extra,
                        '-o', str(executable)], capture_output=True, text=True, timeout=60)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=20)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn('failures=0', result.stdout)
                    print(name, result.stdout.strip())

    def test_launcher_fixed_default_explicit_auto_and_manual_override(self):
        # Execute the real parser/validation AST and exact exposure environment
        # assignments. Stop before filesystem/launch/install handling.
        source = (ROOT / 'tools/manage.py').read_text()
        tree = ast.parse(source)
        main = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == 'main')
        statements = []
        for node in main.body:
            if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name) and node.targets[0].id == 'game':
                break
            statements.append(node)
        assignments = [node for node in ast.walk(main) if isinstance(node, ast.Assign)
            and isinstance(node.targets[0], ast.Subscript)
            and isinstance(node.targets[0].value, ast.Name) and node.targets[0].value.id == 'env'
            and isinstance(node.targets[0].slice, ast.Constant)
            and node.targets[0].slice.value in ('X3M_HDR_EXPOSURE', 'X3M_HDR_EV_MANUAL')]
        main.body = statements + assignments + [ast.Return(value=ast.Name(id='env', ctx=ast.Load()))]
        compiled = compile(ast.fix_missing_locations(ast.Module(body=[main], type_ignores=[])), 'manage_under_test.py', 'exec')
        scope = dict(argparse=argparse, Path=Path, GAME=Path('/unused'), BOTTLE='X3', ROOT=ROOT, __doc__='test')
        exec(compiled, scope)
        base = ['manage.py', 'launch', '--motion-output', '--hdr', '--hdr-tonemap']
        cases = [([], 'fixed', ''), (['--hdr-exposure', 'fixed'], 'fixed', ''),
                 (['--hdr-exposure', 'auto'], 'auto', ''),
                 (['--hdr-ev-manual', '-1'], 'fixed', '-1.0'),
                 (['--hdr-exposure', 'auto', '--hdr-ev-manual', '0.5'], 'fixed', '0.5')]
        for arguments, policy, manual in cases:
            with self.subTest(arguments=arguments), mock.patch.object(sys, 'argv', base + arguments):
                scope['env'] = {'X3M_HDR_EXPOSURE': 'auto', 'X3M_HDR_EV_MANUAL': '2'}
                result = scope['main']()
                self.assertEqual(result, {'X3M_HDR_EXPOSURE': policy, 'X3M_HDR_EV_MANUAL': manual})
        for policy in ('fixed', 'auto'):
            with mock.patch.object(sys, 'argv', ['manage.py', 'launch', '--hdr-exposure', policy]), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    scope['main']()
                self.assertEqual(error.exception.code, 2)

    def test_frame_state_and_capability_wiring(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        present = extract_function(capture, 'HRESULT WINAPI present(')
        self.assertLess(present.index('before_present()'), present.index('comparison_notice.draw('))
        self.assertLess(present.index('comparison_notice.draw('), present.index('const HRESULT hr=fn('))
        self.assertLess(present.index('const HRESULT hr=fn('), present.index('comparison_begin_frame(ctx)'))
        self.assertLess(present.index('struct NoticePin'), present.index('HookGuard lock'))
        self.assertIn('std::shared_ptr<Device> owner;', present)
        self.assertIn('notice_pin.owner=owner', present)
        self.assertLess(present.index('BloomOperation internal(ctx)'), present.index('comparison_notice.draw('))
        self.assertIn('comparison_state_failed(notice.restore)', present)
        self.assertEqual(present.count('const HRESULT hr=fn(d,a,b,w,r)'), 1)
        self.assertIn('const bool down=(GetAsyncKeyState(VK_F8)&0x8000)!=0;', present)
        self.assertIn('ctx.comparison_notice.visible(GetTickCount64()) && comparison_foreground()', present)
        polling = extract_function(capture, 'void comparison_begin_frame(')
        self.assertLess(polling.index('if(!hdr_requested || hdr_config.tonemap!=renderer::HdrTonemap::Agx)return;'),
                        polling.index('comparison_foreground()'))
        reset = extract_function(capture, 'HRESULT reset_common(')
        self.assertIn('ctx.comparison_notice.hide();ctx.comparison.reset_focus()', reset)
        handoff = extract_function(capture, 'void retain_compositor_scene(')
        self.assertIn('call.input.filter.strength=ctx.comparison.bloom_requested?1.f:0.f;', handoff)
        hdr = (ROOT / 'src/renderer/hdr_pass.cpp').read_text()
        self.assertIn('caps_.tonemap && config_.meter_requested()', hdr)
        self.assertIn('hdr_config.exposure=x3m::renderer::ExposureMode::Manual;', capture)
        self.assertIn('hdr_config.allow_auto_toggle=true;', capture)
        self.assertIn('if (caps_.meter) ensure_chain(width, height)', hdr)
        motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        toggle = extract_function(motion, 'bool MotionOutput::comparison_toggle_exposure(')
        self.assertIn('invalidate_taa()', toggle)
        failure = extract_function(motion, 'void MotionOutput::comparison_state_failed(')
        self.assertIn('FAILED(result) && !motion_state_lost_', failure)
        self.assertIn('motion_state_lost_ = true; motion_state_error_ = result', failure)


if __name__ == '__main__':
    unittest.main()
