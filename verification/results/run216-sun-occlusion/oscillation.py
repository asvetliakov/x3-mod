#!/usr/bin/env python3
"""Review F1 (false-lift oscillation) on a source tree's host driver (verification/probe/sun_occlusion_host.cpp).

Scenario (the reviewer's): two classifiable frames, then the sun's unclassifiable body in an answered frame (defer),
then frames alternating between "the vanilla probe hides the sun and another light's core sits at the sun's uv"
(`c` in a `[` bracket) and "the sun's unclassifiable body is drawn" (`u`). The first fix lifted on the hidden-sun
core and cycled every two frames; the hysteresis keeps it deferred. The same tokens run on both drivers.

usage: oscillation.py TREE [TREE...]     (each TREE an exported or checked-out source root)
"""
import os
import shutil
import subprocess
import sys
import tempfile

FRAME = ['b', 'p5', 'k', '[']
SCENARIO = [t for bodies in (['c'], ['c'], ['U']) + (['c'], ['u']) * 4 for t in FRAME + bodies + [']']]

for tree in sys.argv[1:]:
    with tempfile.TemporaryDirectory() as d:
        driver = os.path.join(d, 'drv')
        compiler = shutil.which('clang++') or shutil.which('c++')
        subprocess.run([compiler, '-std=c++17', '-O2', os.path.join(tree, 'verification/probe/sun_occlusion_host.cpp'),
                        os.path.join(tree, 'src/renderer/lens_visibility_variant.cpp'), '-o', driver], check=True)
        print(tree, subprocess.check_output([driver, 'defer'] + SCENARIO, text=True).strip())
