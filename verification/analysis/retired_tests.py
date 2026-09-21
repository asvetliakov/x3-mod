"""Default-discovery skip for retired feature tests.

Each retired module imports ``load_tests`` from here, so ``unittest`` loads no
tests from it unless ``X3M_INCLUDE_RETIRED=1`` is set (``run_host_suite.py
--include-retired`` sets it). The modules stay in place, keep their paths and
their cross-imports, and still pass when they are run.

Two retired subjects, both still compiled into the DLL behind default-off,
non-flown options: the linear material conversion (``--linear-materials`` and
its dependents) and the full-surface emission bracket (``--linear-emissions``,
rejected 2026-09-15). What each module covers, and why the kept linear modules
are kept, is tabulated in ``docs/verification/host-suite.md``.
"""
import os
import unittest

RETIRED = frozenset({
    # Linear material conversion (--linear-materials and its dependent options)
    'test_linear_alpha_test_fixture',
    'test_linear_distance_fade',
    'test_linear_distance_fade_live_report',
    'test_linear_distance_fade_report',
    'test_linear_glass_live_report',
    'test_linear_glass_reference',
    'test_linear_material_constant_port',
    'test_linear_material_fill',
    'test_linear_material_live',
    'test_linear_material_live_report',
    'test_linear_material_profiles',
    'test_linear_material_reference',
    'test_linear_material_report',
    'test_linear_material_transformer',
    'test_material_exposure',
    # Full-surface linear emission bracket (--linear-emissions), rejected
    'test_linear_emission_coverage_report',
    'test_linear_emission_fused_report',
    'test_linear_emission_live',
    'test_linear_emission_mrt_report',
    'test_linear_emission_original_report',
    'test_linear_emission_pass_report',
    'test_linear_emission_report',
    'test_linear_emission_transformer',
})


def include_retired():
    return os.environ.get('X3M_INCLUDE_RETIRED', '') not in ('', '0')


def load_tests(loader, standard_tests, pattern):
    """unittest hook: hide a retired module's tests from default discovery."""
    return standard_tests if include_retired() else unittest.TestSuite()
