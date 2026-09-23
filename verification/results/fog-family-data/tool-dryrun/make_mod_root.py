#!/usr/bin/env python3
"""Rebuild the synthetic vanilla+mod root of verification/results/lod-overlay-mods/commands.txt
(symlinks only; nothing is copied or written into the bottle or the mod trees).

  make_mod_root.py DEST [--bottle DIR] [--mod1 /tmp/x3-mod1] [--mod2 /tmp/x3-mod2]

DEST: bottle root 01..13.cat/.dat, bottle addon/01..04 (the installed LOD overlay addon/05 and its
marker excluded), mod1 addon/05..11, mod2 addon/12 (replaces mod1 12); loose: bottle t/ and
addon/t, then mod1 and mod2 addon/t (later wins), mod1 addon/types and addon/maps,
objects/cut/00749.bod.
"""
import argparse
from pathlib import Path

BOTTLE = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'


ROOT_DEST = None


def link(src, dst, replace=False):
    # Never write through a symlinked directory into a source tree: DEST is created fresh and
    # every parent must be a real directory under it.
    if src.is_symlink() and not src.exists():
        raise SystemExit(f'refusing a broken source link {src}')
    # Parents are checked before anything is created: every existing one below DEST must be a
    # real directory, and DEST must be an ancestor.
    if ROOT_DEST not in dst.parents:
        raise SystemExit(f'refusing {dst}: outside {ROOT_DEST}')
    for parent in [dst.parent, *dst.parent.parents]:
        if parent == ROOT_DEST:
            break
        if parent.is_symlink():
            raise SystemExit(f'refusing to write through symlinked directory {parent}')
    dst.parent.mkdir(parents=True, exist_ok=True)
    if replace and dst.is_symlink():
        dst.unlink()  # a later tree wins; the link itself lives in DEST
    if dst.exists() or dst.is_symlink():
        raise SystemExit(f'refusing to replace {dst}')
    dst.symlink_to(src)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('dest', type=Path)
    p.add_argument('--bottle', type=Path, default=BOTTLE)
    p.add_argument('--mod1', type=Path, default=Path('/tmp/x3-mod1'))
    p.add_argument('--mod2', type=Path, default=Path('/tmp/x3-mod2'))
    a = p.parse_args()
    global ROOT_DEST
    d = ROOT_DEST = a.dest.resolve()
    d.mkdir(parents=True, exist_ok=False)  # a fresh directory only
    for n in range(1, 14):
        for ext in ('.cat', '.dat'):
            link(a.bottle / f'{n:02d}{ext}', d / f'{n:02d}{ext}')
    for n in range(1, 5):
        for ext in ('.cat', '.dat'):
            link(a.bottle / 'addon' / f'{n:02d}{ext}', d / 'addon' / f'{n:02d}{ext}')
    for n in range(5, 12):
        for ext in ('.cat', '.dat'):
            link(a.mod1 / 'addon' / f'{n:02d}{ext}', d / 'addon' / f'{n:02d}{ext}')
    for ext in ('.cat', '.dat'):
        link(a.mod2 / 'addon' / f'12{ext}', d / 'addon' / f'12{ext}')
    for src, dst in ((a.bottle / 't', d / 't'), (a.bottle / 'addon' / 't', d / 'addon' / 't'),
                     (a.mod1 / 'addon' / 't', d / 'addon' / 't'), (a.mod2 / 'addon' / 't', d / 'addon' / 't'),
                     (a.mod1 / 'addon' / 'types', d / 'addon' / 'types'), (a.mod1 / 'addon' / 'maps', d / 'addon' / 'maps'),
                     (a.mod1 / 'objects' / 'cut', d / 'objects' / 'cut')):
        if src.is_dir():
            for f in sorted(src.iterdir()):
                if f.is_symlink() and not f.exists():
                    raise SystemExit(f'refusing a broken source link {f}')
                if f.is_file():
                    link(f, dst / f.name, replace=True)
    print(d)


if __name__ == '__main__':
    main()
