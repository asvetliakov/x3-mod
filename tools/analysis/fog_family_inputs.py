"""Cheap stat fingerprint of the inputs of <game>/x3m/fog-families.bin, standard library only.

fog_families.py records it as `launch_inputs` in fog-families.json; tools/manage.py launch
compares it at every modded launch to say whether the file is stale. It covers what decides
which families exist and which catalogue layers resolve their assets: every catalogue layer the
generator read (<game>/NN.cat, <game>/addon/NN.cat, then the recorded --mod-cat layers, in that
order) with the byte size and mtime of the .cat and its .dat, and the loose
types/TBackgrounds.{pck,txt} of the base and addon trees. Nothing is read or decoded; loose
bodies and textures are not stat'ed (fog_families.py --check hashes every input).
"""
from pathlib import Path

LOOSE = tuple(f'{base}types/TBackgrounds{ext}' for base in ('', 'addon/') for ext in ('.pck', '.txt'))


def layer_name(cat, game):
    """The layer name sector_fog_census.Assets records (relative inside the game directory)."""
    return cat.relative_to(game).as_posix() if cat.is_relative_to(game) else cat.as_posix()


def _stat(path):
    try:
        stat = path.stat()
    except OSError:
        return None
    return [stat.st_size, stat.st_mtime_ns]


def launch_inputs(game, mod_cats=()):
    """dict(layers=[name...], files={name: [bytes, mtime_ns] or None}); stat calls only."""
    game = Path(game).resolve()
    cats = sorted(game.glob('[0-9][0-9].cat')) + sorted((game / 'addon').glob('[0-9][0-9].cat'))
    cats += [Path(m).resolve() for m in mod_cats]
    layers = [layer_name(cat, game) for cat in cats]
    files = {}
    for cat, name in zip(cats, layers):
        files[name] = _stat(cat)
        files[name[:-4] + '.dat'] = _stat(cat.with_suffix('.dat'))
    for name in LOOSE:
        files[name] = _stat(game / name)
    return dict(layers=layers, files=files)


def launch_difference(game, record):
    """None when the recorded launch_inputs still match, else a short reason."""
    recorded = record.get('launch_inputs')
    mods = record.get('mod_cats') or ()
    now = launch_inputs(game, mods)
    if recorded is None:  # a record written before launch_inputs existed: layer list only
        if record.get('catalogue_layers') != now['layers']:
            return 'catalogue list changed'
        return None
    if recorded['layers'] != now['layers']:
        added = [n for n in now['layers'] if n not in recorded['layers']]
        removed = [n for n in recorded['layers'] if n not in now['layers']]
        return 'catalogue list changed (' + ', '.join([f'+{n}' for n in added] + [f'-{n}' for n in removed] or ['order']) + ')'
    for name, value in now['files'].items():
        if recorded['files'].get(name) != value:
            return f'{name} changed (size or mtime)'
    return None
