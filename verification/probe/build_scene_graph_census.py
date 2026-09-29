#!/usr/bin/env python3
"""Build the scene-graph census CPU fixture (scene_graph_census_fixture.cpp). Never runs Wine.

The production module is compiled with its production flags (SSE2, no exceptions) plus the fixture
seam X3M_SCENE_GRAPH_CENSUS_FIXTURE (synthetic image globals); engine_memory.cpp without SSE/MMX as in
CMakeLists.txt. Run with run_scene_graph_census.py under wine_lock.py.
"""
import json
import subprocess
from pathlib import Path
from run_chase_aim_trace import FLAGS
ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/verification/scene-graph-census'
NO_SSE = ['-mno-sse', '-mno-mmx', '-mfpmath=387']
FIXTURE_DEFINE = '-DX3M_SCENE_GRAPH_CENSUS_FIXTURE'


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    objects = []
    for source, stem, extra in [('verification/probe/scene_graph_census_fixture.cpp', 'fixture', [FIXTURE_DEFINE]),
                                ('src/proxy/scene_graph_census.cpp', 'module', [FIXTURE_DEFINE]),
                                ('src/proxy/engine_memory.cpp', 'memory', NO_SSE)]:
        out = BUILD / (stem + '.o')
        subprocess.run(['i686-w64-mingw32-g++', *FLAGS, *extra, '-c', str(ROOT / source), '-o', str(out)], check=True, cwd=ROOT)
        objects.append(out)
    exe = BUILD / 'scene_graph_census_fixture.exe'
    subprocess.run(['i686-w64-mingw32-g++', *map(str, objects), '-static', '-static-libgcc', '-static-libstdc++', '-o', str(exe)],
                   check=True, cwd=ROOT)
    return {'binary': str(exe.relative_to(ROOT)), 'runtime': 'not run'}


if __name__ == '__main__':
    print(json.dumps(build(), indent=2))
