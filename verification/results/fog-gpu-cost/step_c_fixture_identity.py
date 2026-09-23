#!/usr/bin/env python3
"""Fog route step C (docs/architecture/fog-gpu-cost.md): the default (scale 2) fixture figures of a new summary against the
committed summary of the base commit 638b19ad (step B's record), with step A's rules (step_a_fixture_identity.py: timing and
provenance keys skipped, worker-scheduling counters printed VARIES, keys the base lacks counted ADDED; the quarter-resolution
section and its q4_* gates are ADDED by construction). Two counts are expected to change because the step C cases are added
to the same run: fixture_checks (the shader fixture's CHECK lines) and generation.atlases (the atlases it generates); they
print EXPECTED. Exit 0 when nothing else compared differs and the 11 pass-off look hashes are equal.
Usage: /usr/bin/python3 verification/results/fog-gpu-cost/step_c_fixture_identity.py <output>/summary.json"""
import importlib.util
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location('step_a', Path(__file__).resolve().parent / 'step_a_fixture_identity.py')
step_a = importlib.util.module_from_spec(spec); spec.loader.exec_module(step_a)
step_a.BASE = '638b19ad'
step_a.EXPECTED = ('fixture_checks', 'generation.atlases')

if __name__ == '__main__':
    sys.exit(step_a.main())
