"""A/B of the fade-band fill selection (motion_wrap_state_fixture.cpp fade_band_fill_selection).

Builds the host fixture twice: against the working tree's bind_variant_pair and against the one at <base>
(default c2838446, the pre-change commit), and prints, per build, the failing CHECK lines inside
fade_band_fill_selection and the case rows they belong to. Expected: the working tree fails none; the base
fails only on the fade rows that now take the fill / gain family (every opaque and overlay row, and every fade
row without the fill and with the light-map flag off, passes on both, i.e. its selection is unchanged).
usage: python3 fade_fill_selection_ab.py [<base rev>]
"""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'verification/analysis'))
from verification.analysis.test_capture_bloom_lifetime import extract_function  # noqa: E402
from verification.analysis.test_motion_wrap_states import MotionWrapStatesTests  # noqa: E402,F401

FUNCTIONS = ['void MotionOutput::set_render_state(', 'HRESULT MotionOutput::get_render_state_native(',
             'HRESULT MotionOutput::render_state(', 'void MotionOutput::invalidate_render_states(',
             'HRESULT MotionOutput::apply_wrap_states(', 'HRESULT MotionOutput::restore_wrap_states(',
             'void MotionOutput::recover_motion_state(', 'HRESULT MotionOutput::undo(',
             'HRESULT MotionOutput::acquire_restore(', 'void MotionOutput::release_restore(',
             'HRESULT MotionOutput::bind_variant_pair(', 'void MotionOutput::after_reset(',
             'void MotionOutput::begin_stateblock(', 'void MotionOutput::end_stateblock(',
             'void MotionOutput::stateblock_applied(']


def run(source, fixture, label):
    start = source.index('constexpr D3DRENDERSTATETYPE shadow_states[')
    end = source.index('const char* scene_end_source_name', start)
    with tempfile.TemporaryDirectory(prefix='x3-fade-fill-ab-') as directory:
        out = Path(directory)
        (out / 'motion_wrap_under_test_inc.h').write_text(
            source[start:end] + '\n' + '\n\n'.join(extract_function(source, f) for f in FUNCTIONS))
        exe = out / 'fixture'
        build = subprocess.run([shutil.which('clang++') or 'c++', '-std=c++17', '-O2', '-w', '-I', directory,
                                str(fixture), '-o', str(exe)], capture_output=True, text=True)
        if build.returncode:
            print(label, 'build failed', build.stderr[:2000])
            return
        result = subprocess.run([str(exe)], capture_output=True, text=True)
    text = fixture.read_text().splitlines()
    first = next(i for i, l in enumerate(text, 1) if l.startswith('void fade_band_fill_selection()'))
    last = next(i for i, l in enumerate(text, 1) if i > first and l == '}')
    fails = [int(m.group(1)) for m in re.finditer(r'FAIL line=(\d+)', result.stdout)]
    inside = [n for n in fails if first <= n <= last]
    summary = result.stdout.strip().splitlines()[-1]
    print(f'{label}: {summary} fade_band_fill_selection_fails={len(inside)} other_fails={len(fails) - len(inside)}')
    for n in sorted(set(inside)):
        print(f'  line {n}: {text[n - 1].strip()} x{inside.count(n)}')
    cases = re.findall(r'fade_band_fill case=(\d+)', result.stdout)
    print('  failing cases:', ','.join(cases) if cases else 'none')


def main():
    base = sys.argv[1] if len(sys.argv) > 1 else 'c2838446'
    fixture = ROOT / 'verification/probe/motion_wrap_state_fixture.cpp'
    run((ROOT / 'src/proxy/motion_output.cpp').read_text(), fixture, 'working tree')
    old = subprocess.run(['git', '-C', str(ROOT), 'show', f'{base}:src/proxy/motion_output.cpp'],
                         capture_output=True, text=True, check=True).stdout
    run(old, fixture, f'base {base}')


if __name__ == '__main__':
    main()
