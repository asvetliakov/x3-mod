"""The DLL's resolution of its settings, in Python (docs/architecture/config-file.md, section 3): what config::get
(src/proxy/config.cpp with src/config/config_parse.h below_environment) hands each read site for a given environment and
x3m.ini, for the host tests and the launcher dry-run checks. The C++ side is pinned against the same rules by
verification/probe/config_parse_host.cpp (test_config_schema.ConfigResolver).

Order: the environment (a set variable wins, even empty), then the file (unless X3M_CONFIG is none or bare), then the
base: the schema default, or the off value under X3M_CONFIG=bare (None everywhere: unset). A launcher default marker
takes its default only while the key it marks comes from the defaults. A key with `follows` has no base: config::get
leaves it unset and the read site takes the resolved value of the followed key instead (1 when that is unset, invalid or
out of range; the site's value parsing is mirrored by `follow_value`), reported here with source `follows`.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import schema  # noqa: E402


def resolve(environ, file_values=None):
    """{X3M_NAME: (value, source)} with source env | file | default | bare | follows, for every schema entry that resolves to
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
    for e in schema.SETTINGS:
        if e['follows'] and e['env'] not in out:
            followed = value(out, schema.BY_KEY[e['follows']]['env'])
            if followed is not None:
                out[e['env']] = (follow_value(followed, e), 'follows')
    for name, value_ in environ.items():
        if name.startswith('X3M_') and name not in out:
            out[name] = (value_, 'env')
    return out


def value(resolved, name):
    """The value a site acts on: None when unset or empty (a site acts on an empty value as on an unset one; a few log it invalid)."""
    entry = resolved.get(name)
    return entry[0] if entry and entry[0] != '' else None


def follow_value(text, e):
    """The derived value a `follows` site takes from the followed key's text: that value when it parses as finite and
    lies in this key's range, else 1.0 (the followed site keeps 1 = off then), as repr(float) like the launcher sends."""
    try:
        number = float(text)
    except ValueError:
        return '1.0'
    ok = number == number and any((lo < number if open_min else lo <= number) and number <= hi for lo, hi, open_min in e['range'])
    return repr(number if ok else 1.0)
