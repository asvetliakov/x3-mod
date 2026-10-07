"""Run run_motion_output.py's own validator on a finished case directory.

usage: python3 validate_offline.py CASE_NAME CASE_DIRECTORY

Under CX_GRAPHICS_BACKEND=dxvk the fixture crashes after its last frame, in
teardown (exit 5, `page fault on execute access to 0x74666f73` called from the
fixture image; seen with the binaries before and after the double-buffer
change), so the runner stops at its exit-code assertion before validating.
This feeds the same validator the fixture's own lines and the session log:
the stdout up to the RESULT line when the fixture printed one, else up to the
debugger banner closed by a terminal line marked `synthetic=1` (refused when
any fixture line before the crash says FAIL), minus Wine's crash-report lines.
It prints the validator's verdict and the lag fields; it never edits the case
directory.
"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import run_motion_output as rmo  # noqa: E402


def nan_store_zero_as_nan(substituted):
    """--nan-store-zero: DXVK stores the hdrexposure script's NaN block (frame 35, quadrant 0) as 0 in the FP16
    target; the validator's last block (the hazard record, after every meter, lag, EV and presented assertion) asserts
    a NaN was stored. With the flag, exact-zero RGB texels of that quadrant are read as NaN and counted, so the
    validator can finish; the count is printed. Nothing else is altered."""
    original = rmo.read_half_image

    def patched(path, width, height):
        image = original(path, width, height)
        if path.name == f'hdr_1_{rmo.EXPOSURE_POISON_FRAMES[0]}.rgba16f':
            nan = float('nan')
            for i, px in enumerate(image):
                if (i % width) < width // 2 and (i // width) < height // 2 and px[:3] == (0.0, 0.0, 0.0):
                    image[i] = (nan, nan, nan, px[3])
                    substituted.append(i)
        return image
    rmo.read_half_image = patched


def main():
    name, directory = sys.argv[1], pathlib.Path(sys.argv[2])
    substituted = []
    if '--nan-store-zero' in sys.argv[3:]:
        nan_store_zero_as_nan(substituted)
    entry = next(e for e in rmo.CASES if e['name'] == name)
    lines = (directory / 'fixture-stdout.txt').read_text(errors='replace').splitlines()
    end = next((i for i, l in enumerate(lines) if l.startswith('RESULT ')), None)
    result_line = 'fixture'
    if end is None:
        # The crash came before the fixture printed its RESULT line (inside the
        # teardown Releases, after the last frame). Keep the fixture's own lines
        # up to the debugger banner and close them with a terminal line that is
        # marked synthetic; refuse if any fixture check failed or frames are missing.
        end = next(i for i, l in enumerate(lines) if l.startswith('WineDbg attached') or l.startswith('Unhandled exception:'))
        body = lines[:end]
        assert not any('FAIL' in l for l in body), 'a fixture check failed before the crash'
        checks = sum(1 for l in body if l.startswith('CHECK ') and l.endswith(' PASS'))
        restorations = sum(1 for l in body if l.startswith('RESTORE '))
        lines = body + [f'RESULT PASS checks={checks} restorations={restorations} synthetic=1']
        end = len(lines) - 1
        result_line = 'synthetic (teardown crash before RESULT)'
    kept = [l for l in lines[:end + 1] if not l.startswith('Unhandled exception:') and not l.startswith('WineDbg attached')]
    dropped = len(lines[:end + 1]) - len(kept)
    traces = list((directory / 'x3-modern-captures').glob('session-*.log'))
    assert len(traces) == 1, traces
    trace = traces[0].read_text(errors='replace')
    validator = {'hdrexposure': rmo.validate_hdrexposure, 'hdrtonemapfault': rmo.validate_hdrtonemapfault}[entry['mode']]
    case = validator(name, '\n'.join(kept), trace, directory, entry['hdr_env'], entry['hdr_fault'])
    keep = {k: case[k] for k in ('mode', 'frames', 'checks', 'meter_lag_frames', 'ev_max_error_replayed', 'ev_max_error_end_to_end',
                                 'meter_max_abs_error_log2', 'presented_max_code_error') if k in case}
    if 'lag_transitions' in case:
        keep['lag_transitions'] = sorted(case['lag_transitions'])
    if 'hdr_frames' in case:
        keep['stepped'] = {f: h['stepped'] for f, h in sorted(case['hdr_frames'].items())}
    print(json.dumps(dict(case=name, validator='PASS', result_line=result_line, crash_lines_dropped=dropped,
                          nan_store_zero_texels=len(substituted), **keep)))


if __name__ == '__main__':
    main()
