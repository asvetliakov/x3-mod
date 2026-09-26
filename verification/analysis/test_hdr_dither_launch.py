"""--hdr-dither in tools/manage.py (X3M_HDR_DITHER) and its wiring into the
write-back. Host only: the launcher's parser/validation and the exact
environment assignment run through its AST, stopping before any filesystem,
install or launch step (the pattern of test_comparison_hotkeys). No Wine."""
import argparse
import ast
import contextlib
import io
import math
from pathlib import Path
import sys
import unittest
from unittest import mock
from source_text import source_text

ROOT = Path(__file__).resolve().parents[2]


def launcher_main(names):
    """main() of tools/manage.py reduced to its parse/validation prefix plus the
    env assignments of `names`, returning env."""
    tree = ast.parse(source_text(ROOT / 'tools/manage.py'))
    main = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == 'main')
    statements = []
    for node in main.body:
        if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name) and node.targets[0].id == 'game':
            break
        statements.append(node)
    assignments = [node for node in ast.walk(main) if isinstance(node, ast.Assign)
                   and isinstance(node.targets[0], ast.Subscript)
                   and isinstance(node.targets[0].value, ast.Name) and node.targets[0].value.id == 'env'
                   and isinstance(node.targets[0].slice, ast.Constant) and node.targets[0].slice.value in names]
    main.body = statements + assignments + [ast.Return(value=ast.Name(id='env', ctx=ast.Load()))]
    defaults = []
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name) and node.targets[0].id.isupper():
            try:
                ast.literal_eval(node.value)
            except (ValueError, TypeError):
                continue
            defaults.append(node)
    referenced = {n.id for n in ast.walk(main) if isinstance(n, ast.Name)}
    helpers = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in referenced]
    code = compile(ast.fix_missing_locations(ast.Module(body=defaults + helpers + [main], type_ignores=[])), 'manage_under_test.py', 'exec')
    scope = dict(argparse=argparse, math=math, Path=Path, GAME=Path('/unused'), BOTTLE='X3', ROOT=ROOT, __doc__='test')
    exec(code, scope)
    return scope


class HdrDitherLaunch(unittest.TestCase):
    def test_default_on_with_hdr_explicit_off_and_refused_without_hdr(self):
        scope = launcher_main(('X3M_HDR_DITHER',))
        base = ['manage.py', 'launch', '--motion-output', '--hdr']
        for arguments, expected in (([], '1'), (['--hdr-tonemap'], '1'), (['--hdr-dither', 'on'], '1'),
                                    (['--hdr-dither', 'off'], '0'), (['--hdr-tonemap', '--hdr-dither', 'off'], '0')):
            for inherited in ('0', '1'):
                with self.subTest(arguments=arguments, inherited=inherited), mock.patch.object(sys, 'argv', base + arguments):
                    scope['env'] = {'X3M_HDR_DITHER': inherited}   # a stale shell value never survives
                    self.assertEqual(scope['main'](), {'X3M_HDR_DITHER': expected})
        # Without --hdr the variable is written off, and an explicit value is refused.
        with mock.patch.object(sys, 'argv', ['manage.py', 'launch', '--motion-output', '--no-hdr']):  # --hdr is a launcher default since 2026-09-25
            scope['env'] = {'X3M_HDR_DITHER': '1'}
            self.assertEqual(scope['main'](), {'X3M_HDR_DITHER': '0'})
        for value in ('on', 'off'):
            with mock.patch.object(sys, 'argv', ['manage.py', 'launch', '--motion-output', '--no-hdr', '--hdr-dither', value]), \
                    contextlib.redirect_stderr(io.StringIO()) as error, self.assertRaises(SystemExit) as refused:
                scope['main']()
            self.assertEqual(refused.exception.code, 2)
            self.assertIn('--hdr-dither requires --hdr.', error.getvalue())
        with mock.patch.object(sys, 'argv', base + ['--hdr-dither', 'maybe']), contextlib.redirect_stderr(io.StringIO()), \
                self.assertRaises(SystemExit):
            scope['main']()

    def test_dll_reads_the_switch_and_every_8bit_write_takes_it(self):
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
        # A value of 32+ characters is refused (the buffer would still hold the previous variable's text).
        self.assertIn('{const DWORD n=x3m::config::get(L"X3M_HDR_DITHER",setting,32);if(n>0&&n<32)hdr_config.dither=!wcscmp(setting,L"1")||!wcscmp(setting,L"on");}', capture)
        self.assertIn('{const DWORD n=x3m::config::get(L"X3M_HDR_CLAMP",setting,32);if(n>0&&n<32){', capture)
        self.assertIn('bool dither = false;', source_text(ROOT / 'src/renderer/hdr_pass.h'))  # off unless set
        hdr = source_text(ROOT / 'src/renderer/hdr_pass.cpp')
        self.assertIn('x3::temporal::set_dither(agx_, caps_.dither);', hdr)
        self.assertIn('if (sharpen && !use_tonemap) sharpen_.values[3] = caps_.dither ? x3::temporal::kDisplayDitherAmplitude : 0.f;', hdr)
        self.assertIn('hdr_writeback_dither_program()', hdr)
        # The self tests keep the undithered programs: prepare() zeroes c8.z, the identity check draws shader_.
        self.assertIn('out.exposure[2] = out.exposure[3] = 0.f;', source_text(ROOT / 'src/temporal/agx.h'))
        bloom = source_text(ROOT / 'src/renderer/bloom_pass.cpp')
        self.assertIn('if (p.sharpen > 0) agx.exposure[2] = 0.f;', bloom)
        self.assertIn('sharp.values[3] = p.agx.exposure[2];', bloom)
        self.assertIn('p.agx.exposure[2] != x3::temporal::kDisplayDitherAmplitude) return E_INVALIDARG;', bloom)
        # The 8-bit route's sharpen keeps c23.w = 0 (prepare_sharpen).
        self.assertIn('out.values[3] = 0.f;', source_text(ROOT / 'src/temporal/sharpen.h'))


if __name__ == '__main__':
    unittest.main()
