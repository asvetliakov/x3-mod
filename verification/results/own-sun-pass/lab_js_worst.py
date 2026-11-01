#!/usr/bin/env python3
"""Prints the worst |lab JS law - own_sun_host pixel| that test_own_sun_lab.test_page_js_law_matches_the_reference
asserts under 1e-4 (needs node). Run from the repository root with -P: python3 -P verification/results/own-sun-pass/lab_js_worst.py
"""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / 'verification/probe'), str(ROOT / 'verification/analysis')]
import test_own_sun_lab as lab  # noqa: E402

BASE = next(v for v in vars(lab).values() if isinstance(v, type) and hasattr(v, 'test_page_js_law_matches_the_reference'))


class Worst(BASE):
    def assertLess(self, a, b, msg=None):
        if b == 1e-4:
            print('worst lab-JS vs C++ difference %.3e (bound %g)' % (a, b))
        super().assertLess(a, b, msg)


result = unittest.TextTestRunner(verbosity=0).run(unittest.TestSuite([Worst('test_page_js_law_matches_the_reference')]))
sys.exit(0 if result.wasSuccessful() and not result.skipped else 1)
