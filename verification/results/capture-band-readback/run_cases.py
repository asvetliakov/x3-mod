"""Run the capture-band-readback fixture cases one at a time, each through the Wine lock.

usage: python3 run_cases.py BACKEND BIN_DIR OUT_FILE [LABEL ...]

BACKEND is wined3d or dxvk; BIN_DIR holds d3d9.dll, seam/d3d9.dll and
motion_output_fixture.exe (one build of this change). Every label below is one
run_motion_output.py invocation with --wine-env CX_GRAPHICS_BACKEND=BACKEND and
the label's extra variables in the same value (X3M_FIXTURE_READBACK is the seam-only
switch: whole = the old whole-surface path; rows=N = N-row bands; fail_band /
fail_create = injected failures). Appends `label case exit directory` lines to
OUT_FILE; the case directories stay local under verification/probe/build/.
"""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
BUILD = ROOT / 'verification/probe/build'
CAPTURE_5120 = ['X3M_CAPTURE_START=3', 'X3M_CAPTURE_FRAMES=2']
RUNS = {
    'hdr-band': ('seam-hdr-on', []),
    'hdr-whole': ('seam-hdr-on', ['X3M_FIXTURE_READBACK=whole']),
    'hdr-rows5': ('seam-hdr-on', ['X3M_FIXTURE_READBACK=rows=5']),
    'hdr-failband1': ('seam-hdr-on', ['X3M_FIXTURE_READBACK=rows=5,fail_band=1']),
    'hdr-failband0': ('seam-hdr-on', ['X3M_FIXTURE_READBACK=rows=5,fail_band=0']),
    'hdr-failcreate': ('seam-hdr-on', ['X3M_FIXTURE_READBACK=fail_create']),
    'shadow-band': ('seam-ownership-shadow-replay-cascades', []),
    'shadow-whole': ('seam-ownership-shadow-replay-cascades', ['X3M_FIXTURE_READBACK=whole']),
    'shadow-rows7': ('seam-ownership-shadow-replay-cascades', ['X3M_FIXTURE_READBACK=rows=7']),
    'bench-band': ('bench-5120x1440-hdr-on-taa-off', CAPTURE_5120),
    'bench-whole': ('bench-5120x1440-hdr-on-taa-off', CAPTURE_5120),
    'bench-nocapture': ('bench-5120x1440-hdr-on-taa-off', []),  # control: the runner's capture start 1000, no readback
}


def main(backend, bins, out, labels):
    bins = pathlib.Path(bins)
    for label in labels or list(RUNS):
        case, extra = RUNS[label]
        # The bench case runs the production DLL, which never compiles the seam switch: bench-whole is the same
        # case run with BIN_DIR = a build of the parent commit (the whole-surface code).
        before = set(BUILD.glob(f'motion-output-{case}-*'))
        # CrossOver's wine takes --env once, as one space-separated list (read_env_from_string), applied after
        # the inherited environment, so it also overrides the runner's own X3M_CAPTURE_* values.
        command = ['python3', str(ROOT / 'verification/probe/wine_lock.py'), 'python3',
                   str(ROOT / 'verification/probe/run_motion_output.py'),
                   '--wine-env', ' '.join([f'CX_GRAPHICS_BACKEND={backend}', *extra]), '--dll', str(bins / 'd3d9.dll'),
                   '--seam', str(bins / 'seam/d3d9.dll'), '--fixture', str(bins / 'motion_output_fixture.exe'), case]
        completed = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                   env=dict(__import__('os').environ, X3M_FIXTURE_BOTTLE='X3'))
        made = sorted(set(BUILD.glob(f'motion-output-{case}-*')) - before)
        tail = completed.stdout.strip().splitlines()[-3:]
        with open(out, 'a') as handle:
            handle.write(f'{label} {case} exit={completed.returncode} dir={made[-1] if made else "-"}\n')
            for line in tail:
                handle.write(f'    {line[:300]}\n')
        print(label, completed.returncode, flush=True)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:])
