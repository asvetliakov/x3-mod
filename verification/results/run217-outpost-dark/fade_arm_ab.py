"""A/B of the fade arm's XT BUMP row (motion_wrap_state_fixture.cpp fade_arm_selection).

Builds the host fixture twice: against the working tree's fade_route_core.h and fade_arm_admits, and against the
ones at <base> (default b1ec6b0f, before the row), and prints, per build, the failing checks of each fade_arm case.
Expected: the working tree fails none; on the base the two Run 217 band rows (run217_5845_hull/_tested), the
band-end threshold row and the b0-read failure fail on their outcome (the XT program had no row: the overlay arm
refused them); the three material rows (b0 FALSE) fail only the getter count (the base reads no b0 there: the one
getter the guard adds), so their outcome is unchanged; owner-off, glass and asteroid rows pass on both.
usage: python3 fade_arm_ab.py [<base rev>]
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
sys.path.insert(0, str(ROOT / 'verification/probe'))
from verification.analysis.test_capture_bloom_lifetime import extract_function  # noqa: E402

FUNCTIONS = ['void MotionOutput::set_render_state(', 'HRESULT MotionOutput::get_render_state_native(',
             'HRESULT MotionOutput::render_state(', 'void MotionOutput::invalidate_render_states(',
             'HRESULT MotionOutput::apply_wrap_states(', 'HRESULT MotionOutput::restore_wrap_states(',
             'void MotionOutput::recover_motion_state(', 'HRESULT MotionOutput::undo(',
             'HRESULT MotionOutput::acquire_restore(', 'void MotionOutput::release_restore(',
             'HRESULT MotionOutput::bind_variant_pair(', 'void MotionOutput::after_reset(',
             'void MotionOutput::begin_stateblock(', 'void MotionOutput::end_stateblock(',
             'void MotionOutput::stateblock_applied(']
ARM = ['bool MotionOutput::blend_known(', 'bool MotionOutput::fade_arm_admits(']


def run(motion, core, label):
    start = motion.index('constexpr D3DRENDERSTATETYPE shadow_states[')
    end = motion.index('const char* scene_end_source_name', start)
    with tempfile.TemporaryDirectory(prefix='x3-fade-arm-ab-') as directory:
        out = Path(directory)
        # The fixture includes ../../src/proxy/fade_route_core.h relative to itself.
        (out / 'verification/probe').mkdir(parents=True)
        (out / 'src/proxy').mkdir(parents=True)
        fixture = out / 'verification/probe/motion_wrap_state_fixture.cpp'
        shutil.copy(ROOT / 'verification/probe/motion_wrap_state_fixture.cpp', fixture)
        shutil.copy(ROOT / 'src/proxy/sse_scalar.h', out / 'src/proxy/sse_scalar.h')
        (out / 'src/proxy/fade_route_core.h').write_text(core)
        (out / 'motion_wrap_under_test_inc.h').write_text(
            motion[start:end] + '\n' + '\n\n'.join(extract_function(motion, f) for f in FUNCTIONS))
        (out / 'motion_wrap_fade_arm_inc.h').write_text('\n\n'.join(extract_function(motion, f) for f in ARM))
        exe = out / 'fixture'
        build = subprocess.run([shutil.which('clang++') or 'c++', '-std=c++17', '-O2', '-w', '-I', directory,
                                str(fixture), '-o', str(exe)], capture_output=True, text=True)
        if build.returncode:
            print(label, 'build failed', build.stderr[:2000])
            return
        text = subprocess.run([str(exe)], capture_output=True, text=True).stdout
        source = fixture.read_text().splitlines()
        lines = [int(m.group(1)) for m in re.finditer(r'FAIL line=(\d+)', text)]
        body = [i for i, l in enumerate(source, 1) if 'void fade_arm_selection()' in l][0]
        print(f'{label}: fade_arm failing checks={sum(1 for n in lines if n > body)}')
        pending = []  # FAIL lines precede their case's "fade_arm case=" row
        for row in text.splitlines():
            m = re.match(r'FAIL line=(\d+)', row)
            if m and int(m.group(1)) > body:
                pending.append(int(m.group(1)))
            elif row.startswith('fade_arm case='):
                checks = ', '.join(source[n - 1].strip().split('CHECK(', 1)[1].split(');')[0] for n in pending)
                print(f'  {row.split("=", 1)[1]}: {checks}')
                pending = []
        print(' ', [l for l in text.splitlines() if l.startswith(('fade_arm_cases', 'motion_wrap_states'))])


def main():
    base = sys.argv[1] if len(sys.argv) > 1 else 'b1ec6b0f'
    show = lambda path: subprocess.check_output(['git', '-C', str(ROOT), 'show', f'{base}:{path}'], text=True)
    run((ROOT / 'src/proxy/motion_output.cpp').read_text(), (ROOT / 'src/proxy/fade_route_core.h').read_text(),
        'working tree')
    run(show('src/proxy/motion_output.cpp'), show('src/proxy/fade_route_core.h'), f'base {base}')


if __name__ == '__main__':
    main()
