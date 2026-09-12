"""CrossOver bottle selection shared by the fixture runners (no game launch).

`X3M_FIXTURE_BOTTLE` names the bottle the runners pass to `wine --bottle`.
The default stays `Steam` (x86_64 Wine under Rosetta) so the recorded results
under `verification/results/` remain comparable; the game's current bottle is
`X3` (CrossOver Preview's native arm64 Wine with FEX x86 emulation,
`FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`). Any bottle other than Steam writes
its results under `verification/results/bottle-<name>/` so the two records never
overwrite each other. `describe()` reads the bottle's `cxbottle.conf` (WineArch
and the emulation-relevant environment lines) so every summary the runners write
says which Wine ran the fixture. `tools/manage.py` keeps its own `X3M_BOTTLE`
default (X3) for launching the game; this module is for the fixtures only.
"""
import os
import re
from pathlib import Path

DEFAULT_BOTTLE = 'Steam'
BOTTLE = os.environ.get('X3M_FIXTURE_BOTTLE') or DEFAULT_BOTTLE  # an empty value is the default, not a bottle named ''
BOTTLES = Path.home() / 'Library/Application Support/CrossOver/Bottles'
WINE = '/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine'
RECORDED_ENVIRONMENT = ('FEX_X87REDUCEDPRECISION', 'WINEMSYNC')


def bottle_dir(name=None):
    """The bottle's directory (holds cxbottle.conf, drive_c, dosdevices)."""
    return BOTTLES / (name or BOTTLE)


def game_dir(name=None):
    """The game installation inside the bottle (X3AP.exe, d3dx9_37.dll, ...)."""
    return bottle_dir(name) / 'drive_c/X3'


def wine_args(name=None):
    """The loader arguments that select the bottle: ['--bottle', <name>, '--no-update']."""
    return ['--bottle', name or BOTTLE, '--no-update']


def results_dir(root, name=None, create=True):
    """Where a runner writes its records: verification/results for Steam, else results/bottle-<name>."""
    name = name or BOTTLE
    path = root / 'verification/results'
    if name != DEFAULT_BOTTLE:
        path = path / ('bottle-' + name)
    if create:
        path.mkdir(parents=True, exist_ok=True)
    return path


def describe(name=None):
    """Bottle name, WineArch and the recorded environment lines of cxbottle.conf, for result records."""
    name = name or BOTTLE
    conf = bottle_dir(name) / 'cxbottle.conf'
    info = {'name': name, 'wine_arch': None, 'environment': {}, 'cxbottle_conf': str(conf)}
    section = None
    try:
        for line in conf.read_text(errors='replace').splitlines():
            line = line.strip()
            if not line or line.startswith(';'):
                continue
            if line.startswith('['):
                section = line
                continue
            match = re.match(r'^"([^"]+)"\s*=\s*"([^"]*)"$', line)
            if not match:
                continue
            key, value = match.groups()
            if key == 'WineArch':
                info['wine_arch'] = value
            elif section == '[EnvironmentVariables]' and key in RECORDED_ENVIRONMENT:
                info['environment'][key] = value
    except OSError as error:
        info['error'] = repr(error)
    return info


def label(name=None):
    """One-line text form for the runners' .txt headers."""
    info = describe(name)
    environment = ' '.join(f'{k}={v}' for k, v in sorted(info['environment'].items())) or 'no recorded environment'
    return f"{info['name']} bottle, WineArch={info['wine_arch']}, {environment}"


if __name__ == '__main__':
    import json
    print(json.dumps(dict(describe(), results_dir=str(results_dir(Path(__file__).resolve().parents[2], create=False)),
                          wine_args=wine_args()), indent=2))
