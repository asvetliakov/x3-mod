"""The DLL's resolution of its settings, in Python (docs/architecture/config-file.md, section 3): what config::get
(src/proxy/config.cpp with src/config/config_parse.h below_environment) hands each read site for a given environment and
x3m.ini, for the host tests and the launcher dry-run checks. The C++ side is pinned against the same rules by
verification/probe/config_parse_host.cpp (test_config_schema.ConfigResolver).

Order: the environment (a set variable wins, even empty), then the file (unless X3M_CONFIG is none or bare), then the
base: the schema default, or the off value under X3M_CONFIG=bare (None everywhere: unset). A launcher default marker
takes its default only while the key it marks comes from the defaults.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import schema  # noqa: E402


def resolve(environ, file_values=None):
    """{X3M_NAME: (value, source)} with source env | file | default | bare, for every schema entry that resolves to
    something and every other X3M_* variable of `environ`. `file_values`: {X3M_NAME: checked value} of x3m.ini."""
    config = environ.get('X3M_CONFIG')
    bare = config == 'bare'
    files = {} if config in ('none', 'bare') else {k: v for k, v in (file_values or {}).items() if not schema.BY_ENV[k]['env_only']}
    out = {}
    for e in schema.SETTINGS:
        name = e['env']
        if name in environ:
            out[name] = (environ[name], 'env')
        elif name in files:
            out[name] = (files[name], 'file')
        else:
            base = e['off'] if bare else e['default']
            if base is None:
                continue
            if not bare and e['marker_of']:
                marked = schema.BY_KEY[e['marker_of']]['env']
                if marked in files or marked in environ:
                    continue
            out[name] = (base, 'bare' if bare else 'default')
    for name, value in environ.items():
        if name.startswith('X3M_') and name not in out:
            out[name] = (value, 'env')
    return out


def value(resolved, name):
    """The value a site acts on: None when unset or empty (a site acts on an empty value as on an unset one; a few log it invalid)."""
    entry = resolved.get(name)
    return entry[0] if entry and entry[0] != '' else None
