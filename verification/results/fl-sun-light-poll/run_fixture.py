#!/usr/bin/env python3
"""Builds and runs verification/probe/fl_sun_light_poll.cpp natively (WSL interop) through the host test's
build_and_run and writes the compact summary fixture.json beside this script (RESULT, ANCHOR, CASE, NODE_SPAN,
BOUND, TIMING and the fl.sun_light_sample rows; FAIL lines if any).

usage: PYTHONPATH=verification/probe:verification/analysis python3 verification/results/fl-sun-light-poll/run_fixture.py
"""
import json
import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / 'verification/analysis'))
import test_fl_sun_light_poll as t  # noqa: E402

with tempfile.TemporaryDirectory(prefix='x3-fl-sun-') as tmp:
    run = t.build_and_run(tmp)
if run is None:
    sys.exit('needs i686-w64-mingw32-g++ and WSL interop')
keep = ('RESULT', 'ANCHOR', 'CASE', 'NODE_SPAN', 'BOUND', 'TIMING', 'FAIL', 'LOG fl.sun_light_sample')
lines = [l for l in run.stdout.splitlines() if l.startswith(keep)]
out = {'returncode': run.returncode, 'status_rows': run.stdout.count('LOG fl.sun_light status='), 'lines': lines}
(HERE / 'fixture.json').write_text(json.dumps(out, indent=1) + '\n')
print('\n'.join(lines))
