#!/usr/bin/env python3
"""Both install paths on a synthetic FL layout (test_engine_bodies.fl_game: X3FL.exe, root 01.cat, AP addon/, addon2/01.cat
with addon2/types/Bodies): tools/manage.py install_engine_bodies and tools/regenerate/x3m_regenerate.py engine_step
write <game>/x3m/engine_bodies.json (creating x3m/) with generated_from.edition = fl. No game tree is touched.

usage: python3 verification/results/fl-engine-effects/install_paths_check.py
"""
import json, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools'), str(ROOT / 'tools/analysis'), str(ROOT / 'tools/effects'),
                str(ROOT / 'tools/regenerate'), str(ROOT / 'verification/analysis'), str(ROOT / 'verification/probe')]
import test_engine_bodies as teb
import manage
with tempfile.TemporaryDirectory() as tmp:
    root = teb.fl_game(Path(tmp) / 'fl')
    r = manage.install_engine_bodies(root)
    t = json.loads((root / 'x3m/engine_bodies.json').read_text())
    print('manage', r['status'], r['bodies'], t['generated_from'].get('edition'), t['generated_from']['bodies_txt']['source'])
    import x3m_regenerate as xr
    class Log:
        def say(self, m): print('say', m)
        def detail(self, m): pass
    xr.engine_step(Log(), root)
    t = json.loads((root / 'x3m/engine_bodies.json').read_text())
    print('regenerate', t['generated_from'].get('edition'), len(t['bodies']))
