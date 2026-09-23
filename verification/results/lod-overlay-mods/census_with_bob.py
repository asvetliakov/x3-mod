#!/usr/bin/env python3
"""Run lod_batch_census with winning unpacked .bob members included (the census and bob1.audit
select only winners whose member path ends in .pbb; mods ship .bob members that win the same key).
  python3 census_with_bob.py CENSUS_PY_DIR -- <lod_batch_census args>"""
import sys
sys.path.insert(0, sys.argv[1])
import lod_batch_census as c
c.body_keys = lambda a: sorted(k for k, v in a.entries.items() if v[-1]['path'].lower().endswith(('.pbb', '.bob')))
if __name__ == '__main__':
    c.main(sys.argv[3:])
