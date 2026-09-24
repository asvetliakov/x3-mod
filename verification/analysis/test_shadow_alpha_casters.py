"""Host contracts of the alpha-tested sun-shadow casters (docs/architecture/
shadow-replay-gates.md, "Alpha-tested casters"; X3M_SHADOW_ALPHA_CASTERS):
the launcher option, the alpha-test classification of the pure header
(shadow_replay_candidates.h, alpha_caster) and the token structure of the two
authored caster programs in src/renderer/shadow_replay_pass.cpp. No Wine."""
import contextlib
import importlib.util
import io
import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
DEPTH = ('--motion-output', '--ownership', '--shadow-replay-depth')


def launch(directory, *args):
    spec = importlib.util.spec_from_file_location('alpha_casters_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    game = Path(directory) / 'game'; game.mkdir(exist_ok=True); (game / 'X3AP.exe').touch()
    wine = Path(directory) / 'wine'; wine.touch()
    argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', str(game), *args]
    output, error = io.StringIO(), io.StringIO()
    with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), mock.patch.object(module, 'VOICE_DECODER_REPO', None), \
            mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
            contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
        try: module.main()
        except SystemExit as exit_error: return exit_error.code, output.getvalue(), error.getvalue()
    return 0, output.getvalue(), error.getvalue()


class Launcher(unittest.TestCase):
    def test_default_off_and_explicit(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch.dict('os.environ', {'X3M_SHADOW_ALPHA_CASTERS': '1'}):
            code, output, _ = launch(directory, *DEPTH)
            self.assertEqual(code, 0); self.assertEqual(json.loads(output)['env']['X3M_SHADOW_ALPHA_CASTERS'], '0')  # an inherited value cannot enable it
            code, output, _ = launch(directory, *DEPTH, '--shadow-alpha-casters', 'on')
            self.assertEqual(code, 0); self.assertEqual(json.loads(output)['env']['X3M_SHADOW_ALPHA_CASTERS'], '1')
            code, output, _ = launch(directory, *DEPTH, '--shadow-alpha-casters', 'off')
            self.assertEqual(code, 0); self.assertEqual(json.loads(output)['env']['X3M_SHADOW_ALPHA_CASTERS'], '0')
            code, output, _ = launch(directory, '--motion-output', '--ownership')
            self.assertEqual(code, 0); self.assertEqual(json.loads(output)['env']['X3M_SHADOW_ALPHA_CASTERS'], '0')

    def test_refusals(self):
        with tempfile.TemporaryDirectory() as directory:
            code, _, error = launch(directory, '--motion-output', '--ownership', '--shadow-alpha-casters', 'on')
            self.assertNotEqual(code, 0); self.assertIn('--shadow-alpha-casters on requires --shadow-replay-depth', error)
            code, _, error = launch(directory, *DEPTH, '--shadow-alpha-casters', 'yes')
            self.assertNotEqual(code, 0); self.assertIn("invalid choice: 'yes'", error)
            code, output, _ = launch(directory, '--motion-output', '--ownership', '--shadow-alpha-casters', 'off')  # off needs nothing
            self.assertEqual(code, 0); self.assertEqual(json.loads(output)['env']['X3M_SHADOW_ALPHA_CASTERS'], '0')


DRIVER = r'''
#include "proxy/shadow_replay_candidates.h"
#include <cstdio>
using namespace x3m::shadow_replay;
int main() {
    unsigned failures = 0;
    auto expect = [&](unsigned func, unsigned ref, AlphaCaster kind, float threshold) {
        float t = -1.f; const AlphaCaster k = alpha_caster(func, ref, t);
        const bool ok = k == kind && t == threshold;
        if (!ok) { ++failures; std::printf("FAIL func=%u ref=%u kind=%u threshold=%.9g\n", func, ref, unsigned(k), double(t)); }
    };
    expect(8, 0, AlphaCaster::Opaque, 0.f);                     // ALWAYS
    expect(8, 200, AlphaCaster::Opaque, 0.f);
    expect(7, 0, AlphaCaster::Opaque, 0.f);                     // GREATEREQUAL 0: every alpha passes
    expect(7, 1, AlphaCaster::Tested, 1.f / 255.f);             // the game's passes (GREATEREQUAL ref 1)
    expect(7, 128, AlphaCaster::Tested, 128.f / 255.f);
    expect(7, 0x1FF, AlphaCaster::Tested, 1.f);                 // the low eight bits: 255
    expect(5, 0, AlphaCaster::Tested, .5f / 255.f);             // GREATER 0: alpha 0 discarded
    expect(5, 1, AlphaCaster::Tested, 1.5f / 255.f);
    for (unsigned func : {1u, 2u, 3u, 4u, 6u, 0u, 9u, 0xFFFFFFFFu}) expect(func, 1, AlphaCaster::Refused, 0.f); // NEVER, LESS, EQUAL, LESSEQUAL, NOTEQUAL, invalid
    std::printf("RESULT %s failures=%u\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
'''


class Classification(unittest.TestCase):
    def test_alpha_caster(self):
        compiler = shutil.which('c++') or shutil.which('clang++') or shutil.which('g++')
        if compiler is None:
            self.skipTest('no host C++ compiler')
        with tempfile.TemporaryDirectory(prefix='x3-alpha-casters-') as directory:
            work = Path(directory)
            (work / 'driver.cpp').write_text(DRIVER)
            subprocess.run([compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'), str(work / 'driver.cpp'), '-o', str(work / 'driver')], check=True)
            result = subprocess.run([str(work / 'driver')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertIn('RESULT PASS failures=0', result.stdout)


def program(name):
    source = (ROOT / 'src/renderer/shadow_replay_pass.cpp').read_text()
    body = re.search(r'constexpr DWORD ' + name + r'\[\] = \{(.*?)\};', source, re.S).group(1)
    body = re.sub(r'//[^\n]*', '', body)
    return [int(t.strip().rstrip('u'), 16) if t.strip().startswith('0x') else int(t.strip().rstrip('u')) for t in body.split(',') if t.strip()]


def instructions(words):
    """(opcode, operand tokens) of an SM3 token stream: the version token, then
    each instruction's length field (bits 24-27) up to the end token."""
    out, i = [], 1
    while words[i] != 0x0000ffff:
        opcode, length = words[i] & 0xffff, (words[i] >> 24) & 0xf
        out.append((opcode, words[i + 1:i + 1 + length])); i += 1 + length
    assert i == len(words) - 1, 'the end token is the last word'
    return out


def register(token):
    return ((token >> 28) & 7) | ((token >> 8) & 0x18), token & 0x7ff


class Programs(unittest.TestCase):
    def test_vertex(self):
        words = program('alpha_vertex_words')
        self.assertEqual(words[0], 0xfffe0300)
        ops = instructions(words)
        dcls = [(t[0] & 0x1f, (t[0] >> 16) & 0xf, register(t[1]), (t[1] >> 16) & 0xf) for op, t in ops if op == 0x1f]
        # POSITION0 v0, TEXCOORD0 v1 in; POSITION0 o0, TEXCOORD0 o1.x (the depth), TEXCOORD1 o2.xy (the UV) out.
        self.assertEqual(dcls, [(0, 0, (1, 0), 0xf), (5, 0, (1, 1), 0xf), (0, 0, (6, 0), 0xf), (5, 0, (6, 1), 0x1), (5, 1, (6, 2), 0x3)])
        # The depth-only program's instructions, then mov o2.xy, v1.
        base = [op for op, _ in instructions(program('vertex_words')) if op != 0x1f]
        self.assertEqual([op for op, _ in ops if op != 0x1f], base + [0x01])
        self.assertEqual(ops[-1][1], [0xe0030002, 0x90e40001])

    def test_pixel(self):
        words = program('alpha_pixel_words')
        self.assertEqual(words[0], 0xffff0300)
        ops = instructions(words)
        self.assertEqual([op for op, _ in ops], [0x51, 0x1f, 0x1f, 0x1f, 0x42, 0x02, 0x41, 0x0b, 0x01])  # def, 3 dcl, texld, add, texkill, max, mov
        dcl_sampler = ops[3][1]
        self.assertEqual((dcl_sampler[0] >> 27) & 0xf, 2)            # D3DSTT_2D
        self.assertEqual(register(dcl_sampler[1]), (10, 0))          # s0
        self.assertEqual(register(ops[4][1][1]), (1, 1))             # texld from v1 (TEXCOORD1: the UV)
        add = ops[5][1]
        self.assertEqual(((add[1] >> 16) & 0xff, (add[2] >> 24) & 0xf, register(add[2]), (add[2] >> 16) & 0xff), (0xff, 1, (2, 0), 0))  # r0.w - c0.x
        self.assertEqual(ops[6][1], [0x800f0000])                    # texkill r0 (every lane the same difference)
        self.assertEqual(register(ops[7][1][2]), (2, 1))             # max against c1 (c0 is the threshold)
        self.assertEqual(register(ops[8][1][0]), (8, 0))             # oC0


if __name__ == '__main__':
    unittest.main()
