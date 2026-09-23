#!/usr/bin/env python3
"""Fog route step B (docs/architecture/fog-gpu-cost.md): the default (40-bin) fixture figures of a new summary against the
committed summary of the base commit b4e2fcff, with step A's rules (step_a_fixture_identity.py: timing and provenance keys
skipped, worker-scheduling counters printed VARIES, keys the base lacks counted ADDED; the 24-far-bin section and its gates
are ADDED by construction). Exit 0 when nothing compared differs and the 11 pass-off look hashes are equal.
Usage: /usr/bin/python3 verification/results/fog-gpu-cost/step_b_fixture_identity.py <output>/summary.json"""
import importlib.util
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location('step_a', Path(__file__).resolve().parent / 'step_a_fixture_identity.py')
step_a = importlib.util.module_from_spec(spec); spec.loader.exec_module(step_a)
step_a.BASE = 'b4e2fcff'

if __name__ == '__main__':
    sys.exit(step_a.main())
