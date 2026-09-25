"""Execute production capture compositor lifetime/Reset control flow on the host.

The selected function bodies are extracted from capture.cpp at test time and
compiled unchanged against scripted COM doubles and the real C++ standard
library mutex/shared_ptr. This verifies host lifetime/control flow, not Windows
SEH/ABI behavior, GPU recovery/state restoration, or native Windows execution.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def strip_comments(source: str) -> str:
    """Remove // and /* */ comments, leaving string/character literals intact.

    Whitespace-compacted snippets have no line breaks, so a surviving line
    comment would swallow the rest of the text for any later brace scan.
    """
    out = []
    state = 'code'
    index = 0
    while index < len(source):
        char = source[index]
        following = source[index + 1] if index + 1 < len(source) else ''
        if state == 'code':
            if char == '/' and following == '/':
                state = 'line_comment'; index += 2; continue
            if char == '/' and following == '*':
                state = 'block_comment'; index += 2; continue
            if char == '"': state = 'string'
            elif char == "'": state = 'character'
        elif state == 'line_comment':
            if char == '\n': state = 'code'
        elif state == 'block_comment':
            if char == '*' and following == '/':
                state = 'code'; index += 2; out.append(' '); continue
        elif state in ('string', 'character'):
            if char == '\\':
                out.append(source[index:index + 2]); index += 2; continue
            if (state == 'string' and char == '"') or (state == 'character' and char == "'"):
                state = 'code'
        if state in ('code', 'string', 'character'):
            out.append(char)
        elif char == '\n':
            out.append(char)
        index += 1
    return ''.join(out)


def extract_function(source: str, signature: str) -> str:
    """Return one complete function definition, ignoring braces in comments/strings."""
    start = source.index(signature)
    opening = source.index('{', start + len(signature))
    depth = 0
    state = 'code'
    index = opening
    while index < len(source):
        char = source[index]
        following = source[index + 1] if index + 1 < len(source) else ''
        if state == 'code':
            if char == '/' and following == '/':
                state = 'line_comment'; index += 2; continue
            if char == '/' and following == '*':
                state = 'block_comment'; index += 2; continue
            if char == '"': state = 'string'
            elif char == "'": state = 'character'
            elif char == '{': depth += 1
            elif char == '}':
                depth -= 1
                if depth == 0: return source[start:index + 1]
        elif state == 'line_comment':
            if char == '\n': state = 'code'
        elif state == 'block_comment':
            if char == '*' and following == '/':
                state = 'code'; index += 2; continue
        elif state in ('string', 'character'):
            if char == '\\': index += 2; continue
            if (state == 'string' and char == '"') or (state == 'character' and char == "'"):
                state = 'code'
        index += 1
    raise ValueError(f'unbalanced function starting at {signature!r}')


class CaptureBloomLifetimeTests(unittest.TestCase):
    def test_production_lifetime_and_reset_control_flow(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        source = (ROOT / 'src/proxy/capture.cpp').read_text()
        signatures = [
            'void revoke_compositor(Device& ctx) noexcept',
            'ULONG WINAPI release_device(IDirect3DDevice9* d)',
            'bool compositor_current(const CompositorInvocation& call,bool refresh_owner=true) noexcept',
            'void compositor_pre(const X3mCompositorFrame* frame,void* storage,void*)',
            'void compositor_post(const X3mCompositorFrame*,void* storage,void*)',
            'void compositor_cleanup(const X3mCompositorFrame*,void* storage,void*,int abnormal)',
            'HRESULT reset_common(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS* p,D3DDISPLAYMODEEX* mode,bool extended)',
            'HRESULT WINAPI reset(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS* p)',
            'HRESULT WINAPI reset_ex(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS* p,D3DDISPLAYMODEEX* mode)',
            'const char* comparison_bloom_reason(const Device& ctx) noexcept',
            'bool comparison_bloom_ready(const Device& ctx) noexcept',
            'void comparison_notice_text(Device& ctx) noexcept',
        ]
        functions = [extract_function(source, signature) for signature in signatures]
        functions.append(extract_function(source, 'struct NoticePin') + ';')
        with tempfile.TemporaryDirectory(prefix='x3-capture-bloom-lifetime-') as temporary:
            directory = Path(temporary)
            (directory / 'capture_bloom_lifetime_under_test_inc.h').write_text('\n\n'.join(functions))
            executable = directory / 'capture_bloom_lifetime'
            build = subprocess.run([
                compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread',
                '-I', str(directory), str(ROOT / 'verification/probe/capture_bloom_lifetime_fixture.cpp'),
                '-o', str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout, 'capture_bloom_lifetime scenarios=42 checks=214 failures=0\n')
            self.assertEqual(run.stderr, '')


if __name__ == '__main__':
    unittest.main()
