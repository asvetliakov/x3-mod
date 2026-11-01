"""Numbered CAT/DAT mount order of an installed X3 game, shared by the archive scans.

The executable mounts two numbered series, each counted from `01` and ended
by its first missing catalogue (a gap hides every later number):

* root `NN.cat`, then the edition's expansion directory;
* X3AP.exe: `addon/NN.cat` (the AP tools' long-standing model);
* X3FL.exe: `addon2/NN.cat` and no physical `addon` stage
  (docs/reverse-engineering/fl/resource-resolver.md, section 1: descriptor
  `+0x34` `%02d.cat`, `+0x38` `addon2\\%02d.cat`, constructor
  `0x50a609..0x50a93d`).

Catalogues are returned in mount order; a virtual path present in several
catalogues resolves to the last one (later numbered catalogues and the
expansion override earlier ones). Selected mods (`addon2/mods/*.cat`) are a
user choice and are not mounted here.

An FL installation still carries AP's `addon/` directory on disk; the edition
is FL when `X3FL.exe` and `addon2/01.cat` are both present, AP otherwise.
"""
from pathlib import Path

EXPANSIONS = {'ap': 'addon', 'fl': 'addon2'}


def edition(game):
    """'fl' for a Farnham's Legacy installation, 'ap' otherwise."""
    game = Path(game)
    return 'fl' if (game / 'X3FL.exe').is_file() and (game / 'addon2' / '01.cat').is_file() else 'ap'


def numbered_run(directory):
    """`01.cat`, `02.cat`, ... up to the first missing number."""
    run = []
    for number in range(1, 100):
        cat = Path(directory) / ('%02d.cat' % number)
        if not cat.is_file():
            break
        run.append(cat)
    return run


def add_edition_argument(parser):
    """The scans' `--edition auto|ap|fl` option (auto: the installation decides)."""
    parser.add_argument('--edition', choices=('auto', *EXPANSIONS), default='auto',
                        help='mount order: ap = root + addon, fl = root + addon2; auto picks fl when '
                             'X3FL.exe and addon2/01.cat exist')


def mounted_catalogues(game, which=None):
    """Every numbered catalogue the edition's executable mounts, in mount order.

    `which` forces 'ap' or 'fl'; None or 'auto' lets the installation decide.
    """
    game = Path(game)
    which = edition(game) if which in (None, 'auto') else which
    if which not in EXPANSIONS:
        raise ValueError('unknown edition %r' % which)
    return numbered_run(game) + numbered_run(game / EXPANSIONS[which])
